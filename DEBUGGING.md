# DirectCraft++ Debugging and Automation

## Start the debug endpoint

The game only creates its local Windows Named Pipe when debug mode is explicitly enabled:

```powershell
build/debug/Debug/DirectCraft.exe --debug-tools
build/debug/Debug/DirectCraft.exe --debug-tools --pipe-name DirectCraftPP.Manual
```

The endpoint accepts one compact JSON transaction per line. It is intended for local test tooling and is not enabled by a normal game launch.

## DirectCraftTool

The C++ client can connect to an existing game or launch one for a scenario:

```powershell
build/debug/Debug/DirectCraftTool.exe `
  --launch build/debug/Debug/DirectCraft.exe `
  --pipe-name DirectCraftPP.Manual `
  --scenario tests/scenarios/basic.json `
  --out build/debug/debug_result.json
```

Inline JSON is also supported with `--json`. The client normalizes multi-line JSON into one JSONL request, waits for the transaction response, prints it, and optionally writes it to `--out`.

Supported operations include `keyDown`, `keyUp`, `mouseMove`, `mouseButtonDown`, `mouseButtonUp`, `click`, `waitFrames`, `setCamera`, `placeBlock`, `breakBlock`, `capture`, `getState`, and `quit`.

## Captures and overlay

- `F3` toggles diagnostics in the game title bar.
- `F4` toggles wireframe rendering.
- `F12` writes a PNG capture and JSON metadata while debug tools are enabled.
- Scenario `capture` writes to `debug_captures/<name>.png` and the adjacent `.json` state file.

The metadata contains frame, camera matrices, viewport, world seed/chunk dimensions, raycast result, mesh sizes/bounds, geometry/clip-space validity, adapter name, WARP status, and recent gameplay events.

## Reproducing the projection bug

The renderer uses one explicit convention: the CPU stores the logical `view * projection` matrix transposed in the constant buffer, and HLSL evaluates the position as a row-vector multiplication. `DirectCraftMatrixTests` checks the logical transform and `DirectCraft.GpuGolden` verifies the resulting WARP frame.
