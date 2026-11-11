# Measured results: Apple Silicon Mac

This report uses `results/mac-release/benchmark.csv` and `samples.csv`, collected on 11 November
2026. The final run contains 158 configurations and 1,106 measured samples. Every measured output
passed byte-for-byte serial verification, and checksums agree across modes for matching inputs.

## Environment and method

- Apple M5 Pro; 15 physical/logical cores. macOS reports two core groups of 5 and 10 cores.
- macOS 27.0.1 arm64, Apple Clang 21.0.0, Open MPI 5.0.11, release `-O3` with assertions enabled.
- Workers: 1, 2, 4, 8, 15. Hybrid uses two threads per rank at even worker counts; at 15 workers
  it uses 15 ranks × one thread and is effectively the MPI configuration under another mode name.
- One warm-up and seven samples per process launch. Medians are reported; raw samples, ranges,
  standard deviations, compiler commands, binary hash, and kernel hashes are retained.
- Strong scaling: 128², 1024², 2048², 4096². Blur uses 10 passes. Pixel art uses block size 8,
  at most 64 palette colors, ordered dithering, and one conversion.
- Weak scaling: width 512, height 512×workers, giving exactly 262144 pixels per worker.
- Primary time: prepared input to completed output, including distribution, computation, palette
  reduction/construction/broadcast, halo exchanges, and collection. Setup, allocation, file I/O,
  verification, checksums, process launch, and the final timing reduction are excluded.
- MPI binding is disabled; `OMP_PROC_BIND=false`, `OMP_DYNAMIC=false`. No oversubscription.
  These are OS-scheduled runs on a working laptop, not isolated or pinned hardware experiments.

## Strong scaling at 4096×4096

| Filter | Mode | Workers | Median (ms) | Speedup | Efficiency |
|---|---|---:|---:|---:|---:|
| blur | serial | 1 | 422.284 | 1.00× | 100.0% |
| blur | openmp | 4 | 113.389 | 3.72× | 93.1% |
| blur | mpi | 4 | 118.207 | 3.57× | 89.3% |
| blur | hybrid | 4 | 113.785 | 3.71× | 92.8% |
| blur | openmp | 8 | 80.257 | 5.26× | 65.8% |
| blur | mpi | 8 | 84.414 | 5.00× | 62.5% |
| blur | hybrid | 8 | 82.629 | 5.11× | 63.9% |
| blur | openmp | 15 | 54.730 | 7.72× | 51.4% |
| blur | mpi | 15 | 62.435 | 6.76× | 45.1% |
| blur | hybrid | 15 | 62.136 | 6.80× | 45.3% |
| pixel | serial | 1 | 22.899 | 1.00× | 100.0% |
| pixel | openmp | 4 | 8.275 | 2.77× | 69.2% |
| pixel | mpi | 4 | 8.548 | 2.68× | 67.0% |
| pixel | hybrid | 4 | 8.516 | 2.69× | 67.2% |
| pixel | openmp | 8 | 6.085 | 3.76× | 47.0% |
| pixel | mpi | 8 | 7.162 | 3.20× | 40.0% |
| pixel | hybrid | 8 | 6.745 | 3.39× | 42.4% |
| pixel | openmp | 15 | 5.246 | 4.37× | 29.1% |
| pixel | mpi | 15 | 8.011 | 2.86× | 19.1% |
| pixel | hybrid | 15 | 7.760 | 2.95× | 19.7% |

OpenMP is fastest at 4096² in this run: blur reaches 7.72× and pixel art reaches 4.37× at 15
workers. Four-worker blur efficiency is about 93%; it falls to 51% at 15 workers. Pixel-art
efficiency falls from about 69% to 29%. These are comparisons against the optimized serial
implementation, not the old naive blur.

Pixel MPI improves through eight workers (3.20×), then drops to 2.86× at fifteen. Its 15-worker
samples range from 6.671 to 13.799 ms; hybrid at the same rank/thread layout ranges from 5.759
to 24.213 ms. The difference between those equivalent layouts illustrates scheduling/run-to-run
variability, not a distinct algorithmic benefit. The earlier complete run in `development-mac`
also shows high-rank variability; it is preserved rather than selecting only favorable samples.

At 128² the pixel pipeline is dominated by overhead: more workers can be slower than serial.
The suite intentionally keeps these small-input results. Submillisecond measurements warrant
caution, especially without affinity control or background-load isolation.

Graphs: `results/mac-release/{blur,pixel}/strong_scaling_time_4096.svg`, `speedup_4096.svg`,
and `efficiency_4096.svg`. Additional graphs cover every measured strong-scaling size.

## Weak scaling

Normalized time is `T_mode(p) / T_mode(1)` with constant pixels and blocks per worker. Ideal is 1.
| Filter | Mode | 2 workers | 4 workers | 8 workers | 15 workers |
|---|---|---:|---:|---:|---:|
| blur | openmp | 1.03 | 1.23 | 1.59 | 2.02 |
| blur | mpi | 1.05 | 1.19 | 1.65 | 3.74 |
| blur | hybrid | 1.03 | 1.15 | 1.65 | 2.96 |
| pixel | openmp | 0.78 | 0.95 | 1.56 | 2.27 |
| pixel | mpi | 1.15 | 2.00 | 2.66 | 20.81 |
| pixel | hybrid | 1.27 | 1.83 | 2.87 | 9.70 |

Both filters lose weak-scaling efficiency at larger worker counts. Pixel MPI is particularly
variable: the final 15-rank normalized time is 20.81, compared with 5.66 in the earlier development
run. Do not treat a single noisy laptop point as a stable machine limit. Pixel OpenMP weak scaling
can appear better than ideal at low worker counts because the one-worker baseline itself varies.
The raw samples are retained so that this is visible rather than hidden by a single plotted median.

Aspect ratio changes during weak scaling: the width is fixed and height grows. This holds the
blur halo width constant while root distribution/gather volume still grows. This is a specified
one-dimensional weak-scaling experiment, not every possible image-growth geometry.

## What the stage measurements establish

For 4096² pixel art at 15 workers, median palette-stage rank maxima are approximately 0.147 ms
for OpenMP and 2.460 ms for MPI. The MPI palette stage includes histogram reduction, root palette
construction, and broadcast. Its higher cost supports identifying global communication as part
of the bottleneck; these timings alone do not isolate network latency from memory/scheduling costs.

The whole image must still be read, expanded, and moved. Memory traffic, synchronization, and
heterogeneous cores are plausible limits on scaling, but no hardware-counter profiling was done.
Palette workload also depends on image content: these benchmark images are deterministic synthetic
patterns, while the separate demo uses the existing Pixelz artwork.

Stage maxima and their medians are not additive. Different ranks/samples can determine each stage
maximum; the reported total is measured directly and reduced independently.

## Before/after blur context

The original Mac 2048²/10-pass serial median was 290.837 ms. The final optimized serial median is
101.594 ms, about 2.86× lower elapsed time. This is contextual evidence, not an isolated kernel
speedup: the original serial timer included allocation, while the new timer consistently excludes
it in all modes. The old distributed timer also had a different allocation boundary. Use only the
new matched-boundary data for the presentation’s parallel speedup/efficiency claims.

## Validation and limitations

- C++ unit tests: hand-calculated blur, independent scalar blur, block means, partial blocks,
  palette bounds, deterministic ties, Bayer phase/period, codec handling, and allocation auditing.
- Integration: 101 execution/failure cases plus codec checks; separate Python oracles, all four
  modes, uneven/tiny inputs, multiple repetitions, malformed input, and collective failure exits.
- AddressSanitizer and UndefinedBehaviorSanitizer checks pass after the documented stb JPEG
  fixture-writer fix. Project sources pass the 100-column/70-line-function checks.
- Example images preserve dimensions and were inspected visually; graph layout was checked.
- Linux and multi-node execution were not available for validation. These results establish
  single-machine behavior on this Mac only. No universal speedup or maximum-optimization claim.
