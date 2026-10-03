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
# DirectCraft++ Architecture

## v1.1.5 streaming pipeline

The world uses an Euclidean circle in chunk coordinates. Render distance is 8 by default or 16 from the command line; the resident radius is always one chunk larger. Distance selection never depends on camera yaw. Generation jobs are submitted to a bounded priority queue and immutable chunk snapshots are meshed by worker threads. The main thread commits results, uploads GPU resources and submits visible cached meshes.

Frustum culling happens only when assembling the draw mesh. It does not prevent a resident chunk from being pre-meshed, so turning the camera does not create a directional streaming stall. Chunks leaving the resident circle move through `Inactive -> Cache`; an LRU retains up to 64 CPU chunks before eviction.

Block edits increment a mesh epoch for the edited chunk and its four horizontal neighbors. A worker result is committed only when its coordinate epoch still matches. This makes edits local while preserving border-face correctness.

The CPU mesh contains AO values and a packed representation. The renderer uploads packed vertices and samples a deterministic procedural 16x16-per-material atlas generated from a dedicated texture seed. The atlas is uploaded once per renderer initialization.
