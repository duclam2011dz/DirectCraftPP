# DirectCraft++ Performance

## Benchmark contract

The repeatable benchmark is headless and uses the integer terrain pattern, seed 12345, 16x32x16 chunks, load radius 9 and circular render radius 8.

Run:

    build/debug/Debug/DirectCraft.exe --benchmark --seed 12345 --frames 600 --output performance/benchmark-v1.1.2.json

It writes JSON and CSV beside the requested output. The report includes frame-time percentiles, mesh counts, loaded/visible chunks, distance/frustum culling and the selected SIMD backend. A benchmark result is hardware-specific and must record build configuration, CPU, GPU, driver and WARP/hardware mode. CI uses WARP as a separate compatibility baseline.

## Version history

### v1.1.2 GPU instrumentation

The renderer now exposes D3D12 timestamp-query availability and GPU frame time in state JSON. `gpuUtilizationPercent` remains `-1` with source `PIX/WPA export required` unless a profiler report is explicitly imported. Frame time is never converted into utilization.

| Metric | v1.1.2 built-in result |
|---|---:|
| GPU timestamp query | Hardware-dependent; reported in state JSON |
| GPU frame time | `renderer.gpuFrameMs` when available |
| GPU utilization | Unavailable until PIX/WPA export is imported |
| Profiler binaries in repository | 0 |

### v1.1.1 measured local result


Command: DirectCraft.exe --benchmark --seed 12345 --frames 120
Build: Debug, Windows x64, DirectX 12 hardware path
CPU: 11th Gen Intel Core i5-11400H, 6 cores / 12 logical processors
GPU: NVIDIA GeForce RTX 2050; WARP was not used

| Metric | Measured value |
|---|---:|
| Theoretical FPS equivalent from p50 (1000 / p50) | 17,094 |
| Frame time p50 | 0.0585 ms |
| Frame time p95 | 0.0796 ms |
| Frame time p99 | 4.6383 ms |
| Maximum frame time | 1,248.1638 ms |
| Average frame time | 10.5731 ms |
| Process CPU time | 1.28125 s |
| Process CPU utilization across 12 logical processors | 8.4153% |
| Average chunk generation | 0.03798 ms |
| Average streaming bookkeeping | 0.05918 ms |
| Average full-world mesh sample | 315.3938 ms |
| Loaded / visible chunks | 253 / 197 |
| Distance-culled chunks | 56 |
| Greedy mesh vertices / indices | 46,428 / 69,642 |
| SIMD backend | AVX2 |
| GPU utilization | Not available in built-in benchmark |

The maximum frame is the initial resident-world mesh build. The p99 improvement and low steady-state timings show that the v1.1.1 mesh cache removes repeated whole-world remeshing; a block edit or newly streamed border still reports its isolated mesh-build and upload timings in state JSON.

GPU utilization percentage is intentionally recorded as unavailable rather than guessed. Use PIX timing/GPU captures or WPR/WPA GPU counters for adapter utilization and record the capture-specific result beside this table.

### v1.0.2 baseline

v1.0.2 did not have a streaming benchmark. Its single-chunk GPU smoke/golden test remains a correctness baseline, not a performance comparison.

## Profiling workflow

Run `powershell -ExecutionPolicy Bypass -File scripts/profile_tools.ps1` for a WPR ETL capture. It records PIX/WPA/WPR discovery in `*.tools.json`; use `scripts/import_gpu_report.ps1` only with a measured PIX/WPA export.

- Visual Studio Profiler: use the Release build and collect CPU sampling around startup, chunk streaming and block edits.
- Windows Performance Recorder/Analyzer: record CPU sampling, context switches and GPU activity with WPR, then inspect thread and queue stalls in WPA.
- PIX on Windows: capture one GPU frame and one timing capture, then inspect draw, upload and Present timing. See [GPU captures](https://devblogs.microsoft.com/pix/gpu-captures/) and [timing captures](https://devblogs.microsoft.com/pix/timing-captures/).
- RenderDoc: capture a frame and verify input layout, index range, winding, depth state, frustum-selected chunks and shader constants.
- Tracy: use an optional local profiling build to inspect CPU zones, worker contention, memory allocation and GPU zones. Do not commit profiler binaries.

## Interpretation

Compare p50/p95/p99 frame time separately from first-load time. A spike during a chunk transition indicates generation/meshing/upload pressure; stable CPU time with high GPU time indicates rasterization or overdraw. Culling counters and mesh triangle counts should accompany every result.
