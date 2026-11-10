# Presentation Outline (10 slides)

## 1. Title

**Parallel Image Filtering with OpenMP and MPI**  
Names, course, date.

Speaker note: “The project focuses on a repeated 3×3 blur as a clear stencil-computation case study.”

## 2. Problem and motivation

- Large images and repeated filters contain millions of similar calculations.
- Each output pixel depends on a small neighborhood.
- Goal: preserve exact output while reducing execution time.

Visual: input and blurred output side by side.

## 3. Filter definition

- RGB PPM image.
- Average the 3×3 neighborhood separately for red, green, and blue.
- Clamp coordinates at image edges.
- Complexity: `O(KWH)` for `K` iterations.

Visual: a highlighted 3×3 grid around one pixel.

## 4. Four implementations

| Version | Processes | Threads per process | Purpose |
|---|---:|---:|---|
| Serial | 1 | 1 | Reference/baseline |
| OpenMP | 1 | many | Shared-memory parallelism |
| MPI | many | 1 | Distributed ownership |
| Hybrid | many | many | Both techniques |

## 5. OpenMP design

- Output rows are independent during one iteration.
- All threads read `current`; each writes separate pixels in `next`.
- `schedule(static)` gives predictable contiguous work.
- Swap buffers after the implicit barrier.

Include the short `#pragma omp parallel for` code fragment.

## 6. MPI ownership and halo exchange

- Contiguous row strips; remainder rows go to the first ranks.
- One upper and one lower halo row per rank.
- `Scatterv` → repeated `Sendrecv + compute` → `Gatherv`.

Visual: draw three horizontal process blocks and neighbor arrows.

## 7. Correctness and reproducibility

- Same integer arithmetic in every version.
- Built-in `--verify` compares every byte with serial.
- Automated test compares all four PPM files.
- Show a terminal capture containing the PASS lines and matching checksum.

## 8. Strong scaling, speedup, and efficiency

Insert:

- `results/strong_scaling_time.svg`
- `results/speedup.svg`
- `results/efficiency.svg`

State the CPU, image size, iterations, MPI version, repeats, and that the median is shown. Discuss actual observations from your rerun rather than promising linear speedup.

## 9. Weak scalability, challenges, optimization

Insert `results/weak_scalability.svg`.

- Challenge: process boundaries need neighboring pixels.
- Solution: exchange only one row each way.
- Challenge: small tasks are dominated by overhead.
- Optimization: static scheduling, contiguous buffers, no communication for interior pixels, slowest-rank timing.
- Future work: 2D decomposition, nonblocking halo overlap, vectorization, more filters.

## 10. Conclusion and demo

- Exact, tested output.
- Clear shared-memory and distributed-memory designs.
- Performance depends on problem size and worker count.
- Demo: build, test, run hybrid, show output and graphs.

End with: “Questions?”
