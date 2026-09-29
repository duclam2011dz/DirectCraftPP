# Changelog

All notable changes to DirectCraft++ are documented here.

## [1.0.1] - 2026-09-29

### Fixed

- Corrected the CPU/HLSL view-projection matrix convention that caused voxel geometry to stretch into large triangles.
- Added mesh index, finite-value, bounds and clip-space diagnostics.

### Added

- PNG screenshot capture with adjacent JSON metadata.
- F3 diagnostics, F4 wireframe mode and F12 debug capture.
- Named Pipe JSONL debug server and `DirectCraftTool.exe` scenario client.
- Deterministic frame barriers, input injection, state snapshots and integration tests.
- Matrix regression test and corrected WARP golden frame.
## [1.0.0] - 2026-09-28

### Added

- Initial C++17/CMake project for Windows x64.
- Win32 application loop and DirectX 12 device/swapchain setup.
- Procedural deterministic voxel terrain and visible-face mesh generation.
- FPS camera, movement, jump, mouse capture, block placement and block removal.
- Directional plus ambient lighting in HLSL.
- CTest unit coverage and WARP GPU smoke rendering.
- Debug/Release CMake presets and Windows GitHub Actions workflow.
- README, architecture documentation and MIT license.
