# DirectCraft++

DirectCraft++ is a small Windows-native voxel renderer and gameplay vertical slice. Version `1.0.2` uses C++17, Win32, DirectX 12, CMake and CTest.

## Features

- Win32 window and DirectX 12 renderer with depth buffering.
- Deterministic procedural terrain generated from a fixed seed.
- Visible voxel mesh faces with directional and ambient lighting.
- FPS controls: WASD, Raw Input mouse look, Space to jump, LMB to break, RMB to place, and ESC to release the pointer lock.
- AABB player collision with gravity, acceleration, friction, ground and ceiling contacts.
- WARP-backed render-test mode for a repeatable BMP smoke frame.
- Optional Named Pipe JSONL automation, PNG capture, JSON metadata and per-frame JSONL traces.

## Requirements

- Windows 10 1903+ or Windows 11, x64.
- Visual Studio 2022 Build Tools or Visual Studio with Desktop C++ workload.
- Windows SDK with DirectX 12 headers/libraries.
- CMake 3.25+ and Git.

DirectX 12 is provided by the Windows SDK; no separate DirectX 12 package is required.

## Build and test

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-debug
cmake --build --preset build-debug --parallel
ctest --test-dir build/debug -C Debug --output-on-failure
```

For Release, use `windows-release`, `build-release`, and `build/release`.

## Run

```powershell
build/debug/Debug/DirectCraft.exe
```

Click inside the window to lock the pointer at the client center. Raw mouse deltas drive yaw/pitch; `Esc` releases the lock, and clicking again reacquires it. WASD uses acceleration/friction, Space jumps, LMB breaks a ray-hit block and RMB places a block if it does not overlap the player AABB. The world is intentionally limited to one chunk for v1.0.2.

The deterministic render test can be run directly:

```powershell
build/debug/Debug/DirectCraft.exe --render-test build/debug/directcraft_smoke.bmp
```

## Test layout

`DirectCraftUnitTests` covers deterministic generation, block mutation, mesh creation and ray casting. `DirectCraftMatrixTests` covers the CPU/HLSL projection convention. `DirectCraftPhysicsTests` covers spawn, gravity, ground and wall contacts. `DirectCraftCameraTests` covers raw mouse-to-yaw/pitch mapping and pitch clamping. GPU smoke/golden and Named Pipe integration tests run through CTest.

## Debugging and automation

Run with `--debug-tools` to enable the local Named Pipe endpoint, F3 diagnostics, F4 wireframe mode and F12 PNG capture. `DirectCraftTool.exe` can launch the game, replay a JSON scenario and write the JSON response:

```powershell
build/debug/Debug/DirectCraftTool.exe --launch build/debug/Debug/DirectCraft.exe --pipe-name DirectCraftPP.Manual --scenario tests/scenarios/basic.json --out build/debug/debug_result.json
```

Scenario commands include `lockPointer`, `unlockPointer`, `keyDown`, `keyUp`, raw-count `mouseMove`, mouse buttons, `waitFrames`, `setCamera`, `placeBlock`, `breakBlock`, `capture`, `startTrace`, `stopTrace`, `getState` and `quit`.

`capture` writes `debug_captures/<name>.png` plus adjacent JSON metadata. `startTrace` writes `debug_captures/<name>.jsonl`; each line contains input, pointer-lock state, camera basis/orientation, player AABB/physics, raycast block/face/chunk coordinates, mesh and renderer diagnostics.

See [DEBUGGING.md](DEBUGGING.md) for the protocol, trace workflow and optional RenderDoc/PIX capture guidance.

## License

Source code and self-generated assets are released under the MIT License. See [LICENSE](LICENSE).
