# Parallel Image Filtering with OpenMP and MPI

This project applies a repeated **3×3 box blur** to a color image in four ways:

1. **Serial** — one process and one thread; used as the correctness and timing baseline.
2. **OpenMP** — one process; image rows are divided among threads.
3. **MPI** — image rows are divided among processes; neighboring processes exchange one halo row.
4. **Hybrid** — the MPI row decomposition plus OpenMP threads inside every process.

The filter is intentionally simple. It is still a real stencil computation, demonstrates explicit data ownership and communication, and is easy to derive on a whiteboard during a defense.

## Requirements

- A C++17 compiler
- OpenMP
- MPI (`mpic++` and `mpirun`; Open MPI or MPICH)

There are no Python, plotting-library, or image-library dependencies. The C++ benchmark writes its graphs directly as SVG files.

## Build and verify

```bash
make
make test
```

The test creates a small deterministic image, runs all four implementations for three blur iterations, invokes the built-in serial verification, and then compares the four output files byte for byte.

## Run a demonstration

Create an input image:

```bash
bin/generate_image data/demo.ppm --width 1024 --height 1024
```

Run each implementation:

```bash
# Serial baseline
bin/parallel_filter --input data/demo.ppm --output data/serial.ppm \
  --mode serial --iterations 5 --verify

# Four OpenMP threads
bin/parallel_filter --input data/demo.ppm --output data/openmp.ppm \
  --mode openmp --threads 4 --iterations 5 --verify

# Four MPI processes
mpirun -np 4 bin/parallel_filter --input data/demo.ppm --output data/mpi.ppm \
  --mode mpi --iterations 5 --verify

# Two MPI processes, each with two OpenMP threads
OMP_NUM_THREADS=2 mpirun -np 2 bin/parallel_filter \
  --input data/demo.ppm --output data/hybrid.ppm \
  --mode hybrid --threads 2 --iterations 5 --verify
```

Only binary PPM (`P6`) images with maximum color value 255 are accepted. PPM keeps the project dependency-free and lets the code focus on parallelism rather than an image-decoding library. GIMP, ImageMagick, and many other tools can convert PNG/JPEG to PPM if desired.

The program prints machine-readable lines like:

```text
VERIFY status=PASS differing_bytes=0 max_difference=0
RESULT mode=hybrid ranks=2 threads=2 workers=4 width=1024 height=1024 iterations=5 seconds=... megapixels_per_second=... checksum=...
```

Image reading, serial verification, and file writing are outside the reported time. The serial/OpenMP time is the filter call; MPI/hybrid time includes scattering, halo communication, computation, and gathering. MPI reports the slowest rank because the whole job finishes only when its slowest rank finishes.

## Generate performance graphs

For a quick check:

```bash
make benchmark-quick
```

For presentation-quality measurements (three repetitions per point, median reported):

```bash
make benchmark
```

You can select the worker counts and repetitions:

```bash
bin/benchmark --workers 1,2,4,8 --repeats 5
```

The script creates:

- `results/benchmark.csv` — raw times and calculated metrics.
- `results/strong_scaling_time.svg` — execution time for a fixed image.
- `results/speedup.svg` — `S(p) = T_serial / T_parallel(p)`.
- `results/efficiency.svg` — `E(p) = S(p) / p`.
- `results/weak_scalability.svg` — time while image area grows with worker count.
- `results/environment.txt` — enough machine context to discuss reproducibility.

Do not present the included measurements as universal. Rerun the full benchmark on the machine or cluster used for your final report, close unrelated applications, and state the CPU and MPI implementation.

## Algorithm and ownership

For every output channel, the box blur computes:

```text
output(row, col) = sum of the 3×3 neighborhood / 9
```

Coordinates outside the image are clamped to the closest edge pixel. Division is integer division in every implementation, so results are deterministic and byte-exact.

MPI uses a one-dimensional row decomposition. For example, 10 rows over 3 ranks become `4 + 3 + 3`; the first ranks receive the remainder. Each process stores two extra rows:

```text
upper halo       <- last owned row of the upper neighbor
owned rows       <- only these rows are computed by this process
lower halo       <- first owned row of the lower neighbor
```

`MPI_Scatterv` distributes uneven row blocks. Two `MPI_Sendrecv` calls exchange halos without deadlock. `MPI_Gatherv` reconstructs the final image. The hybrid mode calls the same MPI communication from the main thread and uses an OpenMP `parallel for` over the owned rows. This satisfies `MPI_THREAD_FUNNELED` and avoids subtle MPI thread-safety issues.

Time complexity is `O(iterations × width × height)` in every version. The useful work per process is approximately divided by the number of workers, while MPI adds `O(width)` communication per process per iteration. This surface-to-volume effect explains why larger images usually scale better than small images.

## Project layout

```text
include/              Public C interfaces
src/image.cpp         PPM input/output, checksum, comparison
src/filter.cpp        Serial and OpenMP implementation
src/main.cpp          CLI, MPI decomposition, halo exchange, hybrid mode
src/generate_image.cpp  Deterministic C++ image generator
src/benchmark.cpp     C++ experiments, CSV, and native SVG graphs
src/test_correctness.cpp  C++ end-to-end correctness runner
docs/DEFENSE_GUIDE.md Short explanation and likely oral questions
docs/PRESENTATION_OUTLINE.md
docs/RESULTS.md       Measured results and defensible interpretation
```

## Limitations you can state honestly

- PPM files are larger than PNG/JPEG because they are uncompressed.
- One-dimensional row partitioning eventually stops scaling when communication dominates.
- The simple box blur is memory-bandwidth intensive; adding threads cannot exceed the machine's memory bandwidth.
- MPI ranks must not exceed the number of image rows in this educational implementation.

All project code is original and uses only C++17, OpenMP, and MPI. Image generation, benchmarking, CSV output, and SVG graph generation are implemented in C++.
