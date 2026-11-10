# Defense Guide

## The 60-second explanation

“My project applies a 3×3 box blur to an RGB image. I wrote one serial reference and three parallel variants. OpenMP shares independent output rows among threads. MPI explicitly divides the image into contiguous horizontal strips. A process owns its strip, but a 3×3 filter also needs one row above and below, so neighboring ranks exchange one halo row before every iteration. The hybrid version uses MPI between row blocks and OpenMP within each block. All variants use the same integer formula, and `--verify` compares every output byte with the serial result. My benchmark measures strong scaling, speedup, efficiency, and weak scaling.”

## What to point to in the code

- `src/filter.cpp`, `blur_rows`: the serial loop and OpenMP pragma.
- `src/main.cpp`, `build_partition`: quotient/remainder row distribution.
- `src/main.cpp`, `exchange_halo_rows`: the two neighbor exchanges.
- `src/main.cpp`, `blur_local_rows`: work performed only on owned rows.
- `src/main.cpp`, `filter_distributed`: scatter → exchange/compute → gather.
- `src/benchmark.cpp`, `add_metrics`: exact formulas used for graphs.

## Five ideas to draw on the board

1. Draw an image as horizontal strips labeled rank 0, rank 1, and rank 2.
2. Add one dashed halo row above and below each strip.
3. Draw arrows only between adjacent ranks.
4. Write `S(p) = T_serial / T_parallel(p)`.
5. Write `E(p) = S(p) / p` and say ideal efficiency is 1 (100%).

## Likely questions

### Why is every output pixel safe to compute in parallel?

Every thread reads from the immutable `current` buffer and writes a different location in the `next` buffer. Buffers are swapped only after the parallel loop ends, which has an implicit OpenMP barrier. There is no write/write race.

### Why are halo rows required?

The first owned row needs the row immediately above it; the last owned row needs the row immediately below it. Those rows belong to neighboring MPI processes. A filter radius of one therefore requires exactly one halo row on each side.

### Why use two `MPI_Sendrecv` calls?

Every process must send and receive in both directions. `MPI_Sendrecv` combines one send and one receive and lets MPI avoid the deadlock that a poorly ordered sequence of blocking sends could cause.

### What is explicit data ownership?

After `MPI_Scatterv`, a rank owns only its assigned output rows. Halo rows are read-only copies used for boundaries; a rank never computes or writes a neighbor's output rows.

### What happens when height is not divisible by the number of ranks?

The first `height % ranks` processes receive one extra row. `MPI_Scatterv` and `MPI_Gatherv` accept different counts, so no row is lost or duplicated.

### Why does verification use exact equality rather than a tolerance?

Every implementation sums the same nine 8-bit integers in the same integer type and divides by 9. There is no floating-point reordering, so correct results should match byte for byte.

### Why might parallel execution be slower on a small image?

Thread creation/synchronization, MPI startup, scattering, halo messages, and gathering are fixed overheads. If computation is too small, overhead exceeds the time saved. This is an expected result, not a correctness failure.

### Why does speedup stop improving?

MPI communication grows in importance as strips become thinner. OpenMP eventually reaches memory-bandwidth limits. Serial allocation and synchronization also remain. These are practical examples of communication overhead and Amdahl's law.

### What does strong scaling mean here?

The image and iteration count stay fixed while the number of workers grows. A good result makes elapsed time fall, speedup rise, and efficiency remain reasonably high.

### What does weak scaling mean here?

Image area grows in proportion to worker count, so pixels per worker stay approximately constant. Ideal normalized time remains 1; growth above 1 represents parallel overhead.

### Why is hybrid useful?

On a cluster, one or a few MPI ranks can be used per machine while threads use cores that share memory. This reduces the number of MPI processes and often reduces communication or memory duplication. On one small laptop it is not guaranteed to beat pure OpenMP or pure MPI.

## Live demo order

1. Run `make test`; emphasize four `VERIFY status=PASS` lines and identical checksums.
2. Generate a 1024×1024 image.
3. Run serial and hybrid commands from the README with 5–10 iterations.
4. Show the input and output image in an image viewer.
5. Open `results/speedup.svg`, `results/efficiency.svg`, and `results/weak_scalability.svg`.
6. Explain one non-ideal point instead of claiming perfect scaling.

## Safe claims

- “The result is deterministic and verified against the serial implementation.”
- “MPI ownership is explicit and communication is only between neighbors.”
- “The reported distributed time includes communication.”
- “Efficiency below 100% is expected because parallelism has overhead.”

Avoid saying “parallel is always faster,” “hybrid is always best,” or “MPI automatically shares memory.” The measurements should decide which mode is fastest on the tested hardware.
