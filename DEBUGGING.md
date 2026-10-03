# DirectCraft++ Debugging and Automation

## Start the debug endpoint

The game creates its local Windows Named Pipe only when debug mode is explicitly enabled:

```powershell
build/debug/Debug/DirectCraft.exe --debug-tools
build/debug/Debug/DirectCraft.exe --debug-tools --pipe-name DirectCraftPP.Manual
```

The endpoint accepts one compact JSON transaction per line. It is intended for local test tooling and is not enabled by a normal game launch.

## DirectCraftTool

```powershell
build/debug/Debug/DirectCraftTool.exe `
  --launch build/debug/Debug/DirectCraft.exe `
  --pipe-name DirectCraftPP.Manual `
  --scenario tests/scenarios/basic.json `
  --out build/debug/debug_result.json
```

Inline JSON is also supported with `--json`. Mouse `dx/dy` values are raw relative counts and are converted using the game sensitivity. `setCamera.position` is the camera eye position; `setCamera` remains the deterministic absolute camera setup operation.

Supported operations include `lockPointer`, `unlockPointer`, `keyDown`, `keyUp`, `mouseMove`, `mouseButtonDown`, `mouseButtonUp`, `click`, `waitFrames`, `setCamera`, `placeBlock`, `breakBlock`, `capture`, `startTrace`, `stopTrace`, `getState` and `quit`.

## Captures, state and traces

- `F3` toggles the native D3D12 DevTools overlay when launched with `--debug-tools`.
- `F4` toggles wireframe rendering.
- `F12` writes a PNG capture and JSON metadata while debug tools are enabled.
- Scenario `capture` writes `debug_captures/<name>.png` and the adjacent `.json` state file.
- `startTrace` writes one state snapshot per rendered frame to `debug_captures/<name>.jsonl` until `stopTrace`.

State and trace metadata include frame/timing, pointer lock and cursor center, raw/applied mouse deltas, keys/buttons, player feet/eye position, velocity/acceleration/grounded state, AABB, camera yaw/pitch/basis/matrices, world/chunk/block edits, ray origin/direction/hit/previous/face normal, selection outline, mesh bounds/counts, adapter, WARP, feature level, pipeline/resource state, GPU timestamp/frame time, explicit GPU utilization source, GPU validation, DRED and device removal status.

## Reproducing a camera/raycast issue

1. Start the game with `--debug-tools`.
2. Run a scenario with `setCamera`, `lockPointer`, `startTrace`, several raw `mouseMove` commands, `waitFrames`, `capture`, `stopTrace` and `getState`.
3. Compare yaw/pitch and camera forward with the raycast direction.
4. Inspect the PNG beside its JSON metadata and correlate the frame with the JSONL trace.

## GPU tools

RenderDoc can capture a D3D12 frame for pipeline, vertex/index and shader inspection; use its official [Quick Start](https://github.com/baldurk/renderdoc/blob/v1.x/docs/getting_started/quick_start.rst). PIX on Windows can capture Direct3D 12 API calls and timing data; see [PIX GPU Captures](https://devblogs.microsoft.com/pix/gpu-captures/) and [PIX Timing Captures](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/timing-captures/pix-timing-captures).

`scripts/profile_tools.ps1` detects WPR/WPA/PIX and records a WPR capture when WPR is present. DirectCraft never fabricates GPU utilization; import measured profiler data with `scripts/import_gpu_report.ps1`.
## World and performance diagnostics

State snapshots now include the player ChunkCoord, load/render radii, loaded/visible/meshed chunk counts and distance/frustum culling counts. Raycast coordinates are world coordinates and use DDA, so the previous cell and hit normal can be inspected at chunk boundaries.

For a headless reproducible run:

    build/debug/Debug/DirectCraft.exe --benchmark --seed 12345 --frames 600 --output performance/benchmark-v1.1.2.json

The adjacent CSV is suitable for plotting p50/p95/p99 frame time. Use Visual Studio Profiler for CPU samples, WPR/WPA for scheduling and ETW, PIX for D3D12 GPU/timing captures, RenderDoc for pipeline inspection and Tracy for optional live zones. See PERFORMANCE.md for the capture checklist.
# Debugging v1.1.5

Use `--render-distance 8` or `--render-distance 16` to compare circular streaming modes. F3 shows resident/visible chunks, LOD counts, queue/cache counters, packed bytes, AO vertices and atlas size. A camera turn should change frustum-visible geometry without causing old resident chunks to be regenerated.

The state JSON now includes `streaming.renderDistance`, `streaming.residentRadius`, `streaming.renderShape`, cache counters and mesh packed/unpacked byte counts. After a break/place operation, inspect the trace: only the edited chunk and adjacent border chunks should receive a new mesh epoch.

Benchmark both modes separately:

```powershell
DirectCraft.exe --benchmark --render-distance 8 --seed 12345 --frames 600 --output performance/benchmark-v1.1.5-r8.json
DirectCraft.exe --benchmark --render-distance 16 --seed 12345 --frames 600 --output performance/benchmark-v1.1.5-r16.json
```

GPU utilization remains unavailable unless supplied by PIX/WPA; GPU timestamp duration is not a utilization percentage.
