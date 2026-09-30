# DirectCraft++ Architecture

The application is intentionally split into four small layers:

1. `platform` owns the Win32 window, Raw Input registration, keyboard state, mouse buttons, pointer capture, cursor recentering and ESC unlock.
2. `voxel` is platform-independent game data. A `Chunk` stores a fixed 16 x 32 x 16 block volume, generates terrain from a seed, exposes block mutation, builds visible-face meshes and provides a stepped voxel raycast with hit-face normals.
3. `gameplay` owns camera orientation and deterministic player physics. The player is represented by a feet-positioned AABB and moves against solid voxel blocks one axis at a time.
4. `renderer` owns the DirectX 12 device, WARP/hardware adapter selection, swapchain, render/depth targets, root signature, HLSL pipeline, upload buffers and PNG/BMP readback.

`main.cpp` composes the layers, applies raw/injected input, updates physics, performs camera raycasts, rebuilds the mesh after edits, serializes diagnostics and submits the camera matrix every frame.

## Frame flow

The loop pumps Win32 messages, consumes Raw Input deltas, applies camera look and pointer recentering, calculates WASD wish movement, integrates acceleration/friction/gravity, resolves AABB collisions independently on X/Y/Z, performs a block raycast, processes click edges, writes an optional trace frame and renders the current camera view.

## Rendering path

The CPU keeps the logical `view * projection` matrix in DirectXMath form, transposes it exactly once for the constant-buffer upload, and the HLSL vertex shader consumes it with row-vector `mul(position, viewProjection)`. Matrix regression and WARP golden tests cover this boundary. D3D12 debug builds enable the debug layer, GPU-based validation and DRED breadcrumb/page-fault settings.

## Debug automation

The optional debug server receives one JSON transaction per Named Pipe line. The main thread owns command execution and frame barriers. `DirectCraftTool` launches/replays scenarios and validates response state, PNG captures and JSONL traces. Trace recording is off by default and is enabled only by `startTrace` until `stopTrace`.

## Deliberate v1.0.2 limits

## v1.1.0 world pipeline

World maps signed world coordinates to ChunkCoord, keeps a circular resident radius of nine chunks, and exposes an eight-chunk render radius. Generation is deterministic and uses a fixed integer stepped pattern. Edits are session-only and are remeshed in the affected chunk and neighboring visibility boundary.

The chunk mesher builds per-face binary masks and greedily merges equal block runs. Face winding is validated against its normal while back-face culling remains enabled. The SIMD selector reports SSE2 or AVX2 on supported MSVC x64 systems, with a scalar fallback.

Each frame applies distance culling, optional frustum AABB culling and face culling in that order. The current upload path keeps GPU resource ownership on the render thread; a bounded worker pool generates chunks while mesh assembly and GPU upload remain on the main thread.

Ray interaction uses Amanatides-Woo DDA across resident chunks and returns world block, previous cell, face normal and hit distance. This removes the stepping error that made break/place unreliable at chunk boundaries.

--benchmark runs the deterministic headless measurement path and exports JSON/CSV. External Visual Studio Profiler, WPA, PIX, RenderDoc and Tracy workflows are documented in PERFORMANCE.md; they are optional and no third-party binaries are committed.
