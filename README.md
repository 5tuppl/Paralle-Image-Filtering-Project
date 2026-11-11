# Parallel image filtering with OpenMP and MPI

Two deterministic RGB filters, implemented in C++17 in four execution modes:

| Filter | Operation | Communication |
|---|---|---|
| Blur | Repeated, edge-clamped 3×3 integer box blur | One halo row in each direction per pass |
| Pixel art | Block averaging → adaptive palette → ordered dithering → block expansion | Histogram reduction and palette broadcast |

`serial` uses one rank/thread, `openmp` shares work among threads, `mpi` owns bands per
process, and `hybrid` combines rank ownership with threads inside each band.

## Build

Requirements: C++17, Open MPI, OpenMP, Make, and Python 3.9+ for tests/benchmarks.
The filtering program is C++; Python tools use only the standard library. Pinned stb headers
provide PNG/JPEG decoding and PNG encoding. No downloads occur during the build.

On Apple Silicon macOS:

```sh
brew install open-mpi libomp
make -j4
make test
```

The Makefile detects Apple Clang and passes `-Xpreprocessor -fopenmp`, the Homebrew
`libomp` include/library paths, and an rpath. `OMP_PREFIX` can override that location.
The default macOS deployment target is the current OS version; override
`MACOSX_DEPLOYMENT_TARGET` only with compatible MPI/OpenMP libraries.

On Debian/Ubuntu Linux:

```sh
sudo apt install build-essential libopenmpi-dev openmpi-bin python3
make -j4
make test
```

The Linux path uses GCC-compatible `-fopenmp`. The current implementation has been executed
on macOS; Linux instructions remain unverified here. Test/benchmark launchers target Open MPI.
MPI needs local sockets even on one computer; a restrictive sandbox may prevent its startup.

```sh
make debug                 # bin/debug/*
make sanitize              # bin/sanitize/*, ASan + UBSan
make BUILD=sanitize test
make release               # restore bin/* convenience links to release builds
```

Build products are isolated by configuration. Assertions remain enabled in release builds.
Changing compiler/flags within a configuration requires `make clean` before rebuilding.

## Run

```sh
mkdir -p data
bin/generate_image data/input.png --width 1024 --height 1024

bin/parallel_filter --input data/input.png --output data/blur.png \
  --mode openmp --threads 4 --iterations 10 --verify

mpirun --bind-to none -np 2 bin/parallel_filter \
  --input data/input.png --output data/pixel.png --filter pixel \
  --mode hybrid --threads 2 --block-size 8 --colors 64 --dither ordered --verify
```

Use any PNG, JPEG, or binary 8-bit PPM as input. Output supports PNG and PPM. Grayscale is
expanded to RGB; alpha is discarded without compositing. Colors are processed as stored 8-bit
RGB values, without gamma conversion. PPM supports comments, LF/CRLF headers, and a maximum
sample value of 255; other PPM encodings are rejected.

Existing blur commands work unchanged. `--filter` defaults to `blur`. For pixel art,
`--block-size` defaults to 8, `--colors` to 64, and `--dither` to `ordered`; use `none` to disable
it. Dimensions are preserved, including partial edge blocks. `--iterations` applies only to
blur; pixel art requires its default value of 1.

`--verify` compares every measured output byte with a serial reference outside the timer.
Blur has an independent scalar stencil reference. Pixel verification reuses the serial
pipeline; separate Python oracles and hand-calculated unit tests check its actual formula.

## Measure and plot

```sh
make benchmark-quick
make benchmark
# Explicit worker counts/repetitions and a new output directory:
python3 scripts/benchmark.py --workers 1,2,4,8 --repeats 7 --output results/my-run
```

`bin/benchmark` remains a convenience entry point. Run commands from the repository root.
Each run creates a new output directory rather than overwriting previous evidence.

The full suite covers 128², 1024², 2048², and 4096² strong-scaling images, plus weak scaling
with width 512 and height `512 × workers`. Weak scaling therefore holds pixels and blocks
per worker exactly constant, while changing aspect ratio. Defaults use workers 1, 2, 4, 8
up to available logical CPUs, plus the full available count. Hybrid uses two threads per
rank for even worker counts and one thread per rank for odd counts.

Every configuration has one warm-up and seven measured samples within one process launch.
Every repetition restarts from the original input. Reported time includes input copying or
scatter, halo exchanges or histogram reduction, palette construction/broadcast, computation,
and output copying or gather. Setup/allocation, file I/O, reference verification, checksums,
reporting, process startup, and final timing reduction are excluded in all modes. MPI reports
the maximum rank duration. Stage maxima are diagnostics, not additive parts of the total.

Outputs include `samples.csv`, `benchmark.csv`, `environment.json`, and per-filter SVG graphs
for time, speedup, efficiency, and weak scaling. Samples retain all stage times and checksums;
summaries include median, minimum, maximum, and sample standard deviation.

The committed Mac run is in [results/mac-release](results/mac-release/).
Read [the measured findings](docs/RESULTS.md), including limitations, before using the graphs.
Historical Linux artifacts are in `results/historical-linux`; original Mac blur measurements
are in `results/baseline-mac`. They use the old timing boundaries and are labeled accordingly.

## Design and limits

- Contiguous RGB buffers, separate palette/histogram arrays, reusable workspaces, static
  scheduling, and portable compiler-vectorizable loops. No project allocation in filter loops.
- Every output byte has one writer. MPI ranks own row bands; pixel bands align to whole blocks.
- Pixel histograms count block means, not original pixels. Deterministic median cut and
  global-coordinate Bayer dithering give byte-identical results across rank/thread layouts.
- MPI calls stay on the initializing thread; the available `MPI_THREAD_FUNNELED` level is checked.
- Images are limited to `INT_MAX` RGB bytes. Allocation failures and I/O failures are handled;
  MPI errors abort the job. Rank count may not exceed image rows or pixel block rows.
- Pixel palettes have at most 256 entries. Quantization into 32³ bins can merge nearby colors,
  so the actual palette may contain fewer colors than requested.
- No explicit NEON/AVX, multi-node measurements, or Floyd–Steinberg error diffusion in this version.

See [design and TigerStyle adaptation](docs/DESIGN.md), [demo/defense guide](docs/DEFENSE_GUIDE.md),
the [presentation outline](docs/PRESENTATION_OUTLINE.md), and the
[two-person presentation script](docs/PRESENTATION_SCRIPT.md).

## Repository contents and reproducibility

Source, tests, pinned codec dependencies, documentation, and curated benchmark evidence are
versioned. Build products, generated test images, new benchmark runs, editor state, and local
reference material are ignored. The optional Pixelz checkout, `tiger_style.md`, original demo
artwork, and PowerPoint export remain local and are not required to build or test the project.
The design document records the project's TigerStyle adaptation without requiring that file.

Generate a complete demo from a fresh checkout with:

```sh
make -j4
make test
make demo
```

`make demo` creates a deterministic synthetic input and verified blur/pixel outputs in
`data/demo/`. See [demo images](results/demo/README.md) for details. The presentation script
also works with these images, although they differ from the local deck's reference artwork.

To regenerate the committed Mac graphs from their saved CSV data, without rerunning timings:

```sh
python3 - <<'PY'
import csv
import sys
from pathlib import Path
sys.path.insert(0, "scripts")
from graphs import make_graphs
directory = Path("results/mac-release")
with (directory / "benchmark.csv").open() as stream:
    make_graphs(directory, list(csv.DictReader(stream)))
PY
```

Fresh measurements are reproducible experiments, not promises of identical elapsed times.
Run `make benchmark` to collect a new timestamped dataset on the current machine.
