# Measured Results and Interpretation

These measurements were produced by `bin/benchmark` on the current development machine. They are a complete example of how to discuss the results, but the benchmark should be rerun on the machine used for the final demonstration.

## Test environment

- CPU: Intel Core i5-10300H, 4 physical cores and 8 hardware threads.
- MPI: Open MPI 4.1.6.
- Compiler: GCC 13.3.0 with `-O3` and OpenMP.
- Strong scaling: 2048×2048 RGB image, 10 filter iterations.
- Weak scaling: 512×512 pixels per worker at one worker, with image area proportional to workers, 8 iterations.
- Each point is the median of three runs.
- The MPI/hybrid time includes scatter, all halo exchanges, computation, and gather. File input/output and serial verification are excluded.

## Strong-scaling summary

| Implementation | Workers | Time (s) | Speedup | Efficiency |
|---|---:|---:|---:|---:|
| Serial | 1 | 0.3070 | 1.00 | 100% |
| OpenMP | 2 | 0.1599 | 1.92 | 96% |
| OpenMP | 4 | 0.0862 | 3.56 | 89% |
| OpenMP | 8 | 0.0816 | 3.76 | 47% |
| MPI | 2 | 0.2815 | 1.09 | 55% |
| MPI | 4 | 0.4320 | 0.71 | 18% |
| MPI | 8 | 0.2965 | 1.04 | 13% |
| Hybrid | 2 (1 rank × 2 threads) | 0.1584 | 1.94 | 97% |
| Hybrid | 4 (2 ranks × 2 threads) | 0.1486 | 2.07 | 52% |
| Hybrid | 8 (4 ranks × 2 threads) | 0.2371 | 1.29 | 16% |

OpenMP was best on this single shared-memory laptop. It reached 3.56× speedup on the four physical cores. Moving from four to eight threads added only a small improvement because the extra workers are simultaneous-multithreading threads, not additional physical cores, and the filter puts pressure on memory bandwidth.

Pure MPI did not scale well on this machine. All ranks competed for the same four cores and memory system, while the timed section also paid for data distribution, halo messages, and collection. The four-rank timing was slower than serial, illustrating that more parallel workers do not guarantee better performance.

Hybrid performance was strong with two workers but declined as MPI ranks were added. The four-worker hybrid result was still faster than serial, but slower than four-thread OpenMP. This is a defensible result: hybrid designs are most useful across multiple cluster nodes; on one laptop, OpenMP has lower communication overhead.

## Weak-scaling summary

Normalized time should remain near 1.0 when the work per worker stays constant.

| Implementation | 2 workers | 4 workers | 8 workers |
|---|---:|---:|---:|
| OpenMP | 1.07 | 1.11 | 2.07 |
| MPI | 1.75 | 9.22 | 8.00 |
| Hybrid | 0.99 | 1.87 | 9.69 |

OpenMP stayed close to ideal through four workers and then took about twice as long at eight workers. MPI and hybrid weak scaling were limited by communication and resource contention on one node. A multi-node cluster may change the relative results, so the conclusion is limited to this test environment.

## Optimization insights

- Static OpenMP scheduling works well because every output row has almost identical cost.
- Contiguous row ownership makes scatter/gather simple and keeps pixel access cache-friendly.
- Only two rows are communicated per rank per iteration; interior pixels require no messages.
- Using two image buffers prevents data races and ensures one iteration is fully complete before the next.
- A future version could overlap nonblocking halo communication with computation of interior rows. That is a reasonable extension, but it is intentionally omitted here to keep this version easy to explain and verify.

The complete values are in `results/benchmark.csv`; the presentation-ready graphs are the SVG files in `results/`.
