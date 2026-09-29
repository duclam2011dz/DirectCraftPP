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

- `F3` toggles diagnostics in the game title bar.
- `F4` toggles wireframe rendering.
- `F12` writes a PNG capture and JSON metadata while debug tools are enabled.
- Scenario `capture` writes `debug_captures/<name>.png` and the adjacent `.json` state file.
- `startTrace` writes one state snapshot per rendered frame to `debug_captures/<name>.jsonl` until `stopTrace`.

State and trace metadata include frame/timing, pointer lock and cursor center, raw/applied mouse deltas, keys/buttons, player feet/eye position, velocity/acceleration/grounded state, AABB, camera yaw/pitch/basis/matrices, world/chunk/block edits, ray origin/direction/hit/previous/face normal, mesh bounds/counts, adapter, WARP, feature level, pipeline/resource state, GPU validation, DRED and device removal status.

## Reproducing a camera/raycast issue

1. Start the game with `--debug-tools`.
2. Run a scenario with `setCamera`, `lockPointer`, `startTrace`, several raw `mouseMove` commands, `waitFrames`, `capture`, `stopTrace` and `getState`.
3. Compare yaw/pitch and camera forward with the raycast direction.
4. Inspect the PNG beside its JSON metadata and correlate the frame with the JSONL trace.

## GPU tools

RenderDoc can capture a D3D12 frame for pipeline, vertex/index and shader inspection; use its official [Quick Start](https://github.com/baldurk/renderdoc/blob/v1.x/docs/getting_started/quick_start.rst). PIX on Windows can capture Direct3D 12 API calls and timing data; see [PIX GPU Captures](https://devblogs.microsoft.com/pix/gpu-captures/) and [PIX Timing Captures](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/timing-captures/pix-timing-captures).
