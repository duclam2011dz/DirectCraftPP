# Changelog

All notable changes to DirectCraft++ are documented here.

## [1.1.0] - 2026-09-30

### Added

- Infinite session world with deterministic integer stepped terrain, signed chunk coordinates, load radius 9 and circular render radius 8.
- Binary-mask greedy meshing, face winding validation, frustum culling, distance culling and runtime SIMD backend reporting.
- Amanatides-Woo DDA raycast across resident chunks with world/chunk hit diagnostics.
- Headless JSON/CSV benchmark mode and PERFORMANCE.md profiling workflow.

### Fixed

- Corrected all six block-face windings while retaining DirectX back-face culling, preventing missing or apparently transparent faces.
- Block break/place now operates in world coordinates and remains reliable across chunk boundaries.

## [1.0.2] - 2026-09-29

### Fixed

- Replaced absolute `WM_MOUSEMOVE` accumulation with Win32 Raw Input relative deltas.
- Added client-center pointer locking, recentering and ESC unlock behavior.
- Corrected camera replay so mouse input updates yaw/pitch deterministically without moving blocks or raycasts unexpectedly.

### Added

- Standard FPS AABB player physics with gravity, acceleration, friction, jump, ground, wall and ceiling contacts.
- Center-surface spawn at the middle of the generated chunk.
- Player-overlap protection for block placement.
- Hit-face normals and expanded block/chunk/camera/player diagnostics.
- On-demand per-frame JSONL traces with `startTrace` and `stopTrace`.
- Camera-input and physics regression tests.
- Optional RenderDoc and PIX debugging workflow documentation.

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

- Initial Windows x64 C++17/Win32/DirectX 12 voxel vertical slice.
- Procedural deterministic chunk terrain, FPS movement, camera look and block interaction.
- CTest unit coverage and WARP GPU smoke rendering.
- Debug/Release CMake presets and Windows GitHub Actions workflow.
- README, architecture documentation and MIT license.
