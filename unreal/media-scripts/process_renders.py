#!/usr/bin/env python3
"""Pack Unreal MRQ final-color and world-depth EXRs into 1920x1080 RGB PNGs."""

from __future__ import annotations

import argparse
from pathlib import Path

import Imath
import numpy as np
import OpenEXR
from PIL import Image


WIDTH = 1920
HEIGHT = 1080
FINAL_WIDTH = 1440
DEPTH_WIDTH = 480
DEFAULT_END_FRAME = 11075
HALF_FLOAT_MAX = 65504.0


try:
    RESAMPLE_LANCZOS = Image.Resampling.LANCZOS
except AttributeError:
    RESAMPLE_LANCZOS = Image.LANCZOS


def frame_path(directory: Path, pattern: str, frame: int) -> Path:
    return directory / pattern.format(frame=frame)


def exr_size(exr: OpenEXR.InputFile) -> tuple[int, int]:
    data_window = exr.header()["dataWindow"]
    width = data_window.max.x - data_window.min.x + 1
    height = data_window.max.y - data_window.min.y + 1
    return width, height


def read_channel(path: Path, channel: str) -> np.ndarray:
    pixel_type = Imath.PixelType(Imath.PixelType.FLOAT)
    exr = OpenEXR.InputFile(str(path))
    try:
        width, height = exr_size(exr)
        if width != WIDTH or height != HEIGHT:
            raise ValueError(f"{path} is {width}x{height}, expected {WIDTH}x{HEIGHT}")
        if channel not in exr.header()["channels"]:
            raise ValueError(f"{path} does not contain channel {channel!r}")
        return np.frombuffer(exr.channel(channel, pixel_type), dtype=np.float32).reshape(
            height, width
        )
    finally:
        exr.close()


def read_final_rgb(path: Path, linear_to_srgb: bool) -> Image.Image:
    red = read_channel(path, "R")
    green = read_channel(path, "G")
    blue = read_channel(path, "B")
    rgb = np.stack((red, green, blue), axis=-1)
    rgb = np.nan_to_num(rgb, nan=0.0, posinf=1.0, neginf=0.0)
    rgb = np.clip(rgb, 0.0, 1.0)

    if linear_to_srgb:
        rgb = np.where(
            rgb <= 0.0031308,
            rgb * 12.92,
            1.055 * np.power(rgb, 1.0 / 2.4) - 0.055,
        )

    rgb8 = np.clip(np.rint(rgb * 255.0), 0, 255).astype(np.uint8)
    return Image.fromarray(rgb8, mode="RGB")


def make_depth_rgb(path: Path) -> Image.Image:
    depth = read_channel(path, "R")
    finite = np.isfinite(depth)
    if not finite.any():
        raise ValueError(f"{path} has no finite R-channel depth values")

    valid_depth = finite & (depth < HALF_FLOAT_MAX)
    if not valid_depth.any():
        valid_depth = finite

    reference = float(depth[valid_depth].max())
    gray = np.abs(reference - depth) / 100.0
    gray = np.nan_to_num(gray, nan=255.0, posinf=255.0, neginf=0.0)
    gray8 = np.clip(np.rint(gray), 0, 255).astype(np.uint8)
    return Image.fromarray(gray8, mode="L").convert("RGB")


def process_frame(
    frame: int,
    input_dir: Path,
    output_dir: Path,
    final_pattern: str,
    depth_pattern: str,
    linear_to_srgb: bool,
) -> Path:
    final_path = frame_path(input_dir, final_pattern, frame)
    depth_path = frame_path(input_dir, depth_pattern, frame)
    if not final_path.is_file():
        raise FileNotFoundError(final_path)
    if not depth_path.is_file():
        raise FileNotFoundError(depth_path)

    final = read_final_rgb(final_path, linear_to_srgb).resize(
        (FINAL_WIDTH, HEIGHT), RESAMPLE_LANCZOS
    )
    depth = make_depth_rgb(depth_path).resize((DEPTH_WIDTH, HEIGHT), RESAMPLE_LANCZOS)

    combined = Image.new("RGB", (WIDTH, HEIGHT))
    combined.paste(final, (0, 0))
    combined.paste(depth, (FINAL_WIDTH, 0))

    output_path = output_dir / f"processed.{frame:05d}.png"
    combined.save(output_path, "PNG")
    return output_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create 1920x1080 PNGs from MRQ final-color/depth EXR pairs."
    )
    parser.add_argument("--input-dir", type=Path, default=Path(".."))
    parser.add_argument("--output-dir", type=Path, default=Path("output"))
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--end", type=int, default=DEFAULT_END_FRAME)
    parser.add_argument(
        "--test",
        nargs="?",
        const=1,
        type=int,
        help="Process one frame, or N frames when N is supplied.",
    )
    parser.add_argument(
        "--final-pattern",
        default="_sequence.FinalImage.{frame:05d}.exr",
        help="Input final-image filename pattern. Must contain {frame}.",
    )
    parser.add_argument(
        "--depth-pattern",
        default="_sequence.FinalImagedepth.{frame:05d}.exr",
        help="Input depth-image filename pattern. Must contain {frame}.",
    )
    parser.add_argument(
        "--no-srgb",
        action="store_true",
        help="Write clipped linear EXR values directly to 8-bit PNG instead of converting to sRGB.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.start < 0 or args.end < args.start:
        raise SystemExit("--start/--end must describe a non-empty frame range")
    if args.test is not None and args.test < 1:
        raise SystemExit("--test count must be at least 1")

    input_dir = args.input_dir.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    frames = list(range(args.start, args.end + 1))
    if args.test is not None:
        frames = frames[: args.test]

    total = len(frames)
    for index, frame in enumerate(frames, start=1):
        output_path = process_frame(
            frame=frame,
            input_dir=input_dir,
            output_dir=output_dir,
            final_pattern=args.final_pattern,
            depth_pattern=args.depth_pattern,
            linear_to_srgb=not args.no_srgb,
        )
        print(f"[{index}/{total}] wrote {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
