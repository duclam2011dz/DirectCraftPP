# DirectCraft++ Architecture

## Runtime layers

The application is intentionally split into three small layers:

1. `platform` owns the Win32 window, message pump, keyboard state, mouse capture and mouse deltas.
2. `voxel` is platform-independent game data. A `Chunk` stores a fixed 16 x 32 x 16 block volume, generates terrain from a seed, exposes block mutation, builds visible-face meshes and provides a stepped voxel raycast.
3. `renderer` owns the DirectX 12 device, WARP/hardware adapter selection, swapchain, render/depth targets, root signature, HLSL pipeline, upload buffers and BMP readback.

`main.cpp` composes the layers, advances the FPS player, applies input to the chunk, rebuilds its mesh after edits and submits the camera matrix every frame.

## Frame flow

The interactive loop pumps Win32 messages, consumes mouse movement, updates yaw/pitch and movement, applies simple gravity/floor handling, performs a block raycast on click edges, rebuilds the mesh when a block changes, and renders the current camera view. The renderer waits on a fence after each submitted frame in v1.0.1 to keep synchronization simple and deterministic.

## Rendering path

The mesh uses position, normal, UV and per-face material color attributes. The HLSL vertex shader applies the transposed view-projection matrix. The pixel shader combines a fixed directional light with ambient light. The render-test path selects the WARP adapter, uses a hidden Win32 window, renders a fixed camera and copies the backbuffer into a 24-bit BMP.

The CPU keeps the logical `view * projection` matrix in DirectXMath form, transposes it exactly once for the constant-buffer upload, and the HLSL vertex shader consumes it with row-vector `mul(position, viewProjection)`. The matrix regression test and WARP golden frame cover this boundary.

## Build and test boundaries

The voxel library has no DirectX dependency and can be unit tested independently. The game executable links D3D12, DXGI, DXGUID and D3DCompiler. CTest runs unit tests first and then the WARP smoke test. GitHub Actions uses the same CMake presets on a Windows runner.

## Debug automation

The optional debug server receives one JSON transaction per Named Pipe line. The main loop owns command execution, wait-frame barriers, captures and state serialization, so screenshots and state snapshots are taken after deterministic frame application. `DirectCraftTool` is a separate C++ client used by CTest and local scenario replay.

## Deliberate v1.0.1 limits

There is no persistence, networking, audio, inventory, texture atlas, multi-chunk streaming or mod API yet. These boundaries keep the first release focused on a verifiable native rendering and interaction slice.
