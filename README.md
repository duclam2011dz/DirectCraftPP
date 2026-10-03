# DirectCraft++

DirectCraft++ is a small Windows-native voxel renderer and gameplay vertical slice. Version `1.1.5` uses C++17, Win32, DirectX 12, CMake and CTest.

## Features

- Win32 window and DirectX 12 renderer with depth buffering.
- Deterministic procedural terrain generated from a fixed seed.
- Visible voxel mesh faces with directional and ambient lighting.
- FPS controls: WASD, Raw Input mouse look, Space to jump, LMB to break, RMB to place, and ESC to release the pointer lock.
- AABB player collision with gravity, acceleration, friction, ground and ceiling contacts.
- WARP-backed render-test mode for a repeatable BMP smoke frame.
- Euclidean circular render distance selectable with `--render-distance 8` or `--render-distance 16`; the resident radius is always render distance + 1.
- Bounded prioritized worker pool for generation and meshing, per-chunk state/cache lifecycle, LOD tiers, AO values, packed vertices and a procedural 16×16 texture atlas.
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

Click inside the window to lock the pointer at the client center. Raw mouse deltas drive yaw/pitch; `Esc` releases the lock, and clicking again reacquires it. WASD uses acceleration/friction, Space jumps, LMB breaks a ray-hit block and RMB places a block if it does not overlap the player AABB. The world streams deterministic chunks with nine resident chunks of load radius and a circular render radius of eight chunks.

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

When launched with `--debug-tools`, F3 opens the native D3D12 DevTools overlay, including frame timings, GPU timestamp status, chunks, player/camera and raycast selection. The selected block receives a black outline and is the only block eligible for LMB break/RMB place. See [DEBUGGING.md](DEBUGGING.md) for the protocol, trace workflow and optional RenderDoc/PIX capture guidance.

## Infinite world and benchmark

Terrain uses an integer-only periodic stepped pattern, so equal seeds and world coordinates always produce equal blocks. Chunk edits are session-only. The binary-mask greedy mesher merges equal coplanar runs and reports the selected SSE2/AVX2 backend. Rotating the camera does not unload resident chunks: all chunks in the circular render radius are prepared independently of the frustum, while only visible cached meshes are submitted. An edit increments dirty epochs only for the edited chunk and its border neighbors.

Run a deterministic headless benchmark with JSON and CSV output:

    build/debug/Debug/DirectCraft.exe --benchmark --render-distance 8 --seed 12345 --frames 600 --output performance/benchmark-v1.1.5-r8.json
    build/debug/Debug/DirectCraft.exe --benchmark --render-distance 16 --seed 12345 --frames 600 --output performance/benchmark-v1.1.5-r16.json

See PERFORMANCE.md for version baselines and Visual Studio Profiler, WPA, PIX, RenderDoc and Tracy workflows.

## License

Source code and self-generated assets are released under the MIT License. See [LICENSE](LICENSE).
