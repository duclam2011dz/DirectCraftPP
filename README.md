# DirectCraft++

DirectCraft++ is a small Windows-native voxel renderer and gameplay vertical slice. Version `1.0.1` uses C++17, Win32, DirectX 12, CMake and CTest.

## Features

- Win32 window and DirectX 12 renderer with depth buffering.
- Deterministic procedural terrain generated from a fixed seed.
- Exposed voxel mesh faces with simple directional and ambient lighting.
- FPS controls: `WASD`, mouse look, `Space` to jump, left click to remove a block, right click to place one, and `Esc` to release or close the mouse capture.
- WARP-backed render-test mode for a repeatable BMP smoke frame.

## Requirements

- Windows 10 1903+ or Windows 11, x64.
- Visual Studio 2022 Build Tools or Visual Studio with Desktop C++ and Windows SDK 10.0.26100 (or newer).
- CMake 3.25+ and Git.
- A DirectX 12-capable GPU for the interactive hardware path. The automated smoke test uses WARP.

DirectX 12 is supplied by the Windows SDK; it is not a separate package in this project.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-debug
cmake --build --preset build-debug --parallel
ctest --test-dir build/debug -C Debug --output-on-failure
```

For Release, use `windows-release`, `build-release`, and `build/release`.

If CMake is provided by Visual Studio Build Tools, its executable is normally under `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`.

## Run

```powershell
build/debug/Debug/DirectCraft.exe
```

Click inside the window to capture the mouse. `Esc` releases the mouse; press it again to close the game. The current world is intentionally limited to one small chunk for v1.0.1.

The deterministic render test can be run directly:

```powershell
build/debug/Debug/DirectCraft.exe --render-test build/debug/directcraft_smoke.bmp
```

## Test layout

`DirectCraftUnitTests` covers deterministic generation, block mutation, mesh creation and ray casting. `DirectCraft.GpuSmoke` initializes D3D12 through WARP, renders a fixed camera and writes a BMP frame.

## Debugging and automation

Run with `--debug-tools` to enable the local Named Pipe endpoint, F3 diagnostics, F4 wireframe mode and F12 PNG capture. `DirectCraftTool.exe` can launch the game, replay a JSON scenario and write the JSON response:

```powershell
build/debug/Debug/DirectCraftTool.exe --launch build/debug/Debug/DirectCraft.exe --pipe-name DirectCraftPP.Manual --scenario tests/scenarios/basic.json --out build/debug/debug_result.json
```

See [DEBUGGING.md](DEBUGGING.md) for the protocol, supported commands and metadata fields.
## License

Source code and self-generated assets are released under the MIT License. See [LICENSE](LICENSE).
