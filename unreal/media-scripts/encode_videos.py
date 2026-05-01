#!/usr/bin/env python3
"""Encode processed PNG frames into web-seekable H.264 and AV1 MP4 files."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


DEFAULT_FPS = 60
DEFAULT_GOP_FRAMES = 30
DEFAULT_H264_BITRATE = "18M"
DEFAULT_AV1_BITRATE = "10M"
DEFAULT_H264_BUFSIZE_SECONDS = 2.0
DEFAULT_AV1_BUFSIZE_SECONDS = 2.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encode output/processed.%05d.png frames into H.264 and AV1 MP4s."
    )
    parser.add_argument("--input-dir", type=Path, default=Path("output"))
    parser.add_argument("--output-dir", type=Path, default=Path("videos"))
    parser.add_argument("--stem", default="processed")
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--end", type=int)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument(
        "--gop-frames",
        type=int,
        default=DEFAULT_GOP_FRAMES,
        help="Keyframe interval. 30 frames at 60 FPS gives 0.5-second seek points.",
    )
    parser.add_argument("--h264-bitrate", default=DEFAULT_H264_BITRATE)
    parser.add_argument("--av1-bitrate", default=DEFAULT_AV1_BITRATE)
    parser.add_argument(
        "--h264-bufsize-seconds",
        type=float,
        default=DEFAULT_H264_BUFSIZE_SECONDS,
        help="H.264 VBV buffer size in seconds of target bitrate. Larger helps startup/keyframe quality without raising average bitrate.",
    )
    parser.add_argument(
        "--av1-bufsize-seconds",
        type=float,
        default=DEFAULT_AV1_BUFSIZE_SECONDS,
        help="AV1 rate-control buffer size in seconds of target bitrate.",
    )
    parser.add_argument(
        "--h264-preset",
        default="slow",
        help="Use medium/faster if encode time matters more than compression efficiency.",
    )
    parser.add_argument(
        "--av1-cpu-used",
        default="4",
        help="libaom AV1 speed/quality setting. Lower is slower/better; 4 is practical.",
    )
    parser.add_argument(
        "--only",
        choices=("both", "h264", "av1"),
        default="both",
        help="Limit encoding to one output.",
    )
    parser.add_argument(
        "--test",
        nargs="?",
        const=120,
        type=int,
        help="Encode only the first N frames, defaulting to 120 frames.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--overwrite", action="store_true", default=True)
    return parser.parse_args()


def require_ffmpeg() -> None:
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg was not found on PATH")


def frame_numbers(input_dir: Path) -> set[int]:
    pattern = re.compile(r"^processed\.(\d{5})\.png$")
    frames: set[int] = set()
    for path in input_dir.glob("processed.*.png"):
        match = pattern.match(path.name)
        if match:
            frames.add(int(match.group(1)))
    return frames


def resolve_frame_range(args: argparse.Namespace) -> tuple[int, int, int]:
    frames = frame_numbers(args.input_dir)
    if not frames:
        raise SystemExit(f"No processed PNG frames found in {args.input_dir}")

    start = args.start
    end = args.end if args.end is not None else max(frames)
    if args.test is not None:
        if args.test < 1:
            raise SystemExit("--test count must be at least 1")
        end = min(end, start + args.test - 1)
    if end < start:
        raise SystemExit("--end must be greater than or equal to --start")

    missing = [frame for frame in range(start, end + 1) if frame not in frames]
    if missing:
        preview = ", ".join(f"{frame:05d}" for frame in missing[:10])
        suffix = "" if len(missing) <= 10 else f" ... and {len(missing) - 10} more"
        raise SystemExit(f"Missing input frames: {preview}{suffix}")

    return start, end, end - start + 1


def bitrate_kbits(bitrate: str) -> int:
    text = bitrate.strip().lower()
    if text.endswith("m"):
        return int(float(text[:-1]) * 1000)
    if text.endswith("k"):
        return int(float(text[:-1]))
    return int(int(text) / 1000)


def run_command(command: list[str], dry_run: bool) -> None:
    print(" ".join(command))
    if dry_run:
        return
    subprocess.run(command, check=True)


def common_input_args(args: argparse.Namespace, start: int, frame_count: int) -> list[str]:
    return [
        "-framerate",
        str(args.fps),
        "-start_number",
        str(start),
        "-i",
        str(args.input_dir / "processed.%05d.png"),
        "-frames:v",
        str(frame_count),
        "-an",
        "-fps_mode",
        "cfr",
        "-r",
        str(args.fps),
    ]


def h264_command(args: argparse.Namespace, start: int, frame_count: int) -> list[str]:
    bitrate = args.h264_bitrate
    buffer_size = f"{int(bitrate_kbits(bitrate) * args.h264_bufsize_seconds)}k"
    output = args.output_dir / f"{args.stem}_h264_1080p60.mp4"
    return [
        "ffmpeg",
        "-hide_banner",
        "-y",
        *common_input_args(args, start, frame_count),
        "-c:v",
        "libx264",
        "-preset",
        args.h264_preset,
        "-profile:v",
        "high",
        "-level:v",
        "4.2",
        "-pix_fmt",
        "yuv420p",
        "-b:v",
        bitrate,
        "-minrate",
        bitrate,
        "-maxrate",
        bitrate,
        "-bufsize",
        buffer_size,
        "-g",
        str(args.gop_frames),
        "-keyint_min",
        str(args.gop_frames),
        "-sc_threshold",
        "0",
        "-forced-idr",
        "1",
        "-force_key_frames",
        f"expr:gte(t,n_forced*{args.gop_frames / args.fps:.12g})",
        "-x264-params",
        f"keyint={args.gop_frames}:min-keyint={args.gop_frames}:scenecut=0:open-gop=0:force-cfr=1:nal-hrd=vbr:vbv-init=0.9",
        "-movflags",
        "+faststart",
        "-video_track_timescale",
        str(args.fps * 1000),
        str(output),
    ]


def av1_command(args: argparse.Namespace, start: int, frame_count: int) -> list[str]:
    bitrate = args.av1_bitrate
    buffer_size = f"{int(bitrate_kbits(bitrate) * args.av1_bufsize_seconds)}k"
    output = args.output_dir / f"{args.stem}_av1_1080p60.mp4"
    return [
        "ffmpeg",
        "-hide_banner",
        "-y",
        *common_input_args(args, start, frame_count),
        "-c:v",
        "libaom-av1",
        "-cpu-used",
        args.av1_cpu_used,
        "-row-mt",
        "1",
        "-pix_fmt",
        "yuv420p10le",
        "-b:v",
        bitrate,
        "-minrate",
        bitrate,
        "-maxrate",
        bitrate,
        "-bufsize",
        buffer_size,
        "-g",
        str(args.gop_frames),
        "-force_key_frames",
        f"expr:gte(t,n_forced*{args.gop_frames / args.fps:.12g})",
        "-movflags",
        "+faststart",
        "-video_track_timescale",
        str(args.fps * 1000),
        str(output),
    ]


def main() -> int:
    args = parse_args()
    require_ffmpeg()
    args.input_dir = args.input_dir.resolve()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    if args.fps < 1:
        raise SystemExit("--fps must be at least 1")
    if args.gop_frames < 1:
        raise SystemExit("--gop-frames must be at least 1")
    if args.h264_bufsize_seconds <= 0:
        raise SystemExit("--h264-bufsize-seconds must be greater than 0")
    if args.av1_bufsize_seconds <= 0:
        raise SystemExit("--av1-bufsize-seconds must be greater than 0")

    start, end, frame_count = resolve_frame_range(args)
    seconds = frame_count / args.fps
    print(
        f"Encoding frames {start:05d}..{end:05d} "
        f"({frame_count} frames, {seconds:.3f}s at {args.fps} FPS)"
    )

    if args.only in ("both", "h264"):
        run_command(h264_command(args, start, frame_count), args.dry_run)
    if args.only in ("both", "av1"):
        run_command(av1_command(args, start, frame_count), args.dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
