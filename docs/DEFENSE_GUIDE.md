# Live demo and defense guide

## Five-minute demo

Run from the repository root. Use release binaries and keep a saved benchmark run available.

```sh
make -j4 release
make test
mkdir -p data
bin/generate_image data/demo.png --width 1024 --height 1024
bin/parallel_filter --input data/demo.png --output data/blur.png \
  --mode openmp --threads 4 --iterations 10 --verify
mpirun --bind-to none -np 2 bin/parallel_filter \
  --input data/demo.png --output data/pixel.png --filter pixel \
  --mode hybrid --threads 2 --block-size 8 --colors 16 --verify
```

Show input/output images. `make demo` creates a deterministic input and both filtered examples
under `data/demo/` without external artwork. Optional local Pixelz artwork and saved images
under `results/demo/` are excluded from version control.
Then show `results/mac-release/blur/speedup_2048.svg` and the corresponding pixel graph, followed
by each filter's weak-scaling plot. Use the saved CSV to explain one non-ideal result.

Do not run a full benchmark live unless time permits. A quick measurement uses:

```sh
bin/parallel_filter --input data/demo.png --filter pixel --mode openmp \
  --threads 4 --warmup 1 --repeats 7 --verify
```

## The 60-second explanation

“We implemented two RGB filters with serial, OpenMP, MPI, and hybrid execution. Blur is a stencil:
rank-owned row bands need one neighbor row on each side. Pixel art groups pixels into blocks,
reduces compact histograms to produce one global palette, and applies ordered dithering independently
within each owned band. All rounding and tie-breaking are deterministic. We verify byte-identical
output, then measure the complete in-memory filtering operation including communication. The two
filters demonstrate different limits: neighbor exchange and memory traffic for blur, versus global
reduction, palette work, and memory traffic for pixel art.”

## Where to look

- `src/filter.cpp`: separable blur and its exact integer arithmetic.
- `src/pixel.cpp`: block means, private histograms, deterministic median cut, and expansion.
- `src/execution.cpp`: explicit partitions, scatter/gather, halo exchange, and palette reduction.
- `src/reference.cpp` and `scripts/test_integration.py`: independent correctness evidence.
- `scripts/benchmark.py`: timing records, worker configurations, scaling formulas, environment.

## Questions to rehearse

**Why is this data-oriented?** Data ownership and traversal dictate the design: contiguous RGB
bands, plain configuration, preallocated scratch, thread-private histogram arrays, and separate
palette channels. There are no objects or heap allocations per pixel.

**Why not use separate R/G/B image planes everywhere?** Both kernels consume RGB neighbors together.
Interleaved RGB also avoids conversion and packing at file/MPI boundaries. Separate arrays are used
where access patterns benefit, such as palette channels and histogram fields.

**Why does separable blur remain exact?** Horizontal sums retain all precision. We sum three of them
vertically and divide once by 9; intermediate division would introduce a different result.

**Why ordered dithering instead of Floyd–Steinberg?** Floyd–Steinberg carries error into later pixels.
Ordered dithering depends only on the current block, global coordinates, and the shared palette,
so it is deterministic and independent across bands. This is a deliberate algorithm change from Pixelz.

**Why one palette?** Independent rank palettes create visible seams. Integer histogram reduction
produces the same global counts/sums regardless of decomposition, and all ranks use the same palette.

**How do you avoid races?** Output regions never overlap. Histograms are private per thread, then
merged by disjoint fields. Barriers separate read/write stages. Only the initializing thread calls MPI.

**What happens with uneven images?** Remainder ownership goes to the first ranks. Pixel bands align
with block rows; right/bottom blocks average only the pixels that actually exist.

**What happens with too many ranks?** A collective setup check rejects it before processing.
Malformed files and rank-zero output failures also produce a coordinated nonzero exit.

**What is timed?** Prepared input to completed output, including data movement and communication.
Allocation, decoding, process startup, verification, checksums, and encoding are outside every timer.
Distributed time is the maximum rank duration. Stage maxima cannot be summed into a total.

**What does efficiency mean?** Strong speedup is `T_serial / T_parallel`; efficiency is speedup divided
by ranks×threads. Weak normalized time is `T_mode(p) / T_mode(1)` at constant pixels per worker.
Weak efficiency is its reciprocal. Parallel slowdown is a valid measured result.

**Does this demonstrate multiple machines?** No. These runs demonstrate MPI processes on one Mac.
Multi-node scalability and Linux execution need their own measurements before making those claims.
