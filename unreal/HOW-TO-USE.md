# SpotInvadersGfx C++ Tools: How To Use

## Procedural City Generator

Place `ASpotSciFiMazeGeneratorVolume` in the level.

Resize its `GenerationBounds` box to cover the area where the city should appear. Assign road, building, obstacle, landmark meshes and materials if you have them. If arrays are empty, the generator uses basic engine meshes.

Useful buttons in Details:

- `GenerateCityMaze`: builds the city.
- `ClearGenerated`: deletes generated actors from this generator.
- `RandomizeSeedAndGenerate`: picks a new seed and regenerates.

Important settings:

- `Seed`: deterministic generation.
- `CellSize`: block/grid spacing.
- `RoadWidth`: corridor width.
- `BuildingDensity`: higher fills more parcels.
- `AlienPatternIntensity`: higher adds more irregular sci-fi variation.
- `MaxBuildings` and `MaxCorridors`: cap output size.

Generated roads do not block navigation. Buildings, landmarks, and obstacles block.

## Cinematic Path Setup

Use these actors together:

- `ASpotLoopingLandscapeWaypoint`: visible editor marker for route points.
- `ASpotLoopingLandscapePathDriver`: moves along a closed spline route.
- `ASpotTerrainFollowingCineCamera`: follows the driver X/Y and traces terrain height.

Basic setup:

1. Place one `ASpotLoopingLandscapePathDriver`.
2. Place several `ASpotLoopingLandscapeWaypoint` actors around the landscape.
3. Select the path driver.
4. Add the waypoint actors to `Path > Waypoints` in route order.
5. Press or call `RebuildSpline` if the spline does not update immediately.
6. Place one `ASpotTerrainFollowingCineCamera`.
7. Leave `PathDriverActor` empty if there is exactly one path driver; the camera auto-finds it.

The path driver starting position is the first point. Do not add another waypoint at the same starting point; the spline is closed-loop automatically.

Waypoint and driver spheres are editor markers. They have no collision and are hidden in game/cinematic playback.

## Path Smoothness

The path driver uses `Path > Smoothing > bUseCinematicSmoothTangents` by default. This gives each waypoint matching arrive/leave tangents so the camera glides through the point instead of making a hard turn.

- Increase `PathSmoothingStrength` for wider, more orbital arcs.
- Decrease `PathSmoothingStrength` if the path swings too far away from your intended route.
- Lower `MaxTangentLengthRatio` if unevenly spaced waypoints create overshoot.
- Keep `PathReparamStepsPerSegment` fairly high for smooth constant-speed Sequencer playback.

## Sequencer Workflow

For exact Sequencer timing:

1. Select the path driver.
2. Enable `Sequencer > bUseSequencerPlaybackAlpha`.
3. Add the path driver actor to the Level Sequence as a possessable actor.
4. Add a property track for `PlaybackAlpha`.
5. Key `PlaybackAlpha = 0.0` at the shot start.
6. Key `PlaybackAlpha = 1.0` at the shot end.
7. Add the `ASpotTerrainFollowingCineCamera` to Sequencer.
8. Bind the Camera Cuts track to that camera.

`PlaybackAlpha` means:

- `0.0`: start of loop.
- `0.5`: halfway around the loop.
- `1.0`: back to the start on a looping path.

For runtime auto-motion instead, disable `bUseSequencerPlaybackAlpha` and use `LoopDurationSeconds`.

## Common Checks

If the driver moves when editing `PlaybackAlpha` in Details but not in Sequencer:

- Confirm the Sequencer track is on the placed path driver actor, not a stale/missing binding.
- Confirm `bUseSequencerPlaybackAlpha` is enabled.
- After rebuilding C++, restart the editor so the `PlaybackAlpha` Sequencer custom accessor is registered.
- Delete and re-add the `PlaybackAlpha` track after C++ changes so Sequencer rebuilds its property binding and notify metadata.
- Remove any Transform track on the path driver unless you intentionally want Sequencer to override its location.
- Re-add the path driver to Sequencer after renaming/replacing C++ classes.

If Camera Cuts says the bound object is missing:

- Delete the broken Camera Cuts binding.
- Add the placed `ASpotTerrainFollowingCineCamera` actor to Sequencer again.
- Rebind Camera Cuts to that camera.

If World Partition reports spatial loading reference errors:

- Select the camera, driver, and waypoint actors.
- In Details, confirm `Is Spatially Loaded` is unchecked.
- Save the level.

Landscape/ground meshes must block the selected `TraceChannel`, usually `Visibility`, for terrain following to work.
