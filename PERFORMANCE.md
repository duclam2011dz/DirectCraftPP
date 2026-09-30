# DirectCraft++ Performance

## Benchmark contract

The repeatable benchmark is headless and uses the integer terrain pattern, seed 12345, 16x32x16 chunks, load radius 9 and circular render radius 8.

Run:

    build/debug/Debug/DirectCraft.exe --benchmark --seed 12345 --frames 600 --output performance/benchmark-v1.1.0.json

It writes JSON and CSV beside the requested output. The report includes frame-time percentiles, mesh counts, loaded/visible chunks, distance/frustum culling and the selected SIMD backend. A benchmark result is hardware-specific and must record build configuration, CPU, GPU, driver and WARP/hardware mode. CI uses WARP as a separate compatibility baseline.

## Version history

### v1.1.0

The benchmark harness is included in the release, but no local hardware result is fabricated in source control. Run the command above on the target machine and attach the generated JSON/CSV to the release report.

### v1.0.2 baseline

v1.0.2 did not have a streaming benchmark. Its single-chunk GPU smoke/golden test remains a correctness baseline, not a performance comparison.

## Profiling workflow

- Visual Studio Profiler: use the Release build and collect CPU sampling around startup, chunk streaming and block edits.
- Windows Performance Recorder/Analyzer: record CPU sampling, context switches and GPU activity with WPR, then inspect thread and queue stalls in WPA.
- PIX on Windows: capture one GPU frame and one timing capture, then inspect draw, upload and Present timing. See [GPU captures](https://devblogs.microsoft.com/pix/gpu-captures/) and [timing captures](https://devblogs.microsoft.com/pix/timing-captures/).
- RenderDoc: capture a frame and verify input layout, index range, winding, depth state, frustum-selected chunks and shader constants.
- Tracy: use an optional local profiling build to inspect CPU zones, worker contention, memory allocation and GPU zones. Do not commit profiler binaries.

## Interpretation

Compare p50/p95/p99 frame time separately from first-load time. A spike during a chunk transition indicates generation/meshing/upload pressure; stable CPU time with high GPU time indicates rasterization or overdraw. Culling counters and mesh triangle counts should accompany every result.
