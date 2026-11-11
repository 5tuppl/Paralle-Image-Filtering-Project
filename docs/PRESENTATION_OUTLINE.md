# Presentation outline: parallel blur and pixel art

1. **Problem and motivation.** Two RGB operations on large images; preserve exact results while
   distributing computation. Run `make demo` and show input, blur, and pixel-art images from
   `data/demo/`. The local deck uses optional reference artwork instead.
2. **Precise algorithms.** Edge-clamped 3×3 blur with one integer division; block averaging,
   histogram median-cut palette, Bayer dithering, and expansion. State that Pixelz inspired the look.
3. **Sequential baselines.** Optimized serial implementations for speedup; independent scalar/Python
   oracles for correctness. Explain why using a slower naive baseline would exaggerate speedup.
4. **Data-oriented design.** Draw RGB bands, ping-pong buffers, blur scratch, thread-private
   histograms, and palette arrays. Explain initialization versus processing allocations.
5. **OpenMP.** Static row/block scheduling, immutable sources, disjoint writes, private histogram
   accumulation, and barriers. Identify the loops in the actual implementation.
6. **MPI blur ownership.** Draw quotient/remainder row bands and halo rows. Show scatter → repeated
   exchange/compute → gather. Explain partial ownership and why one halo row suffices.
7. **MPI pixel ownership and hybrid.** Draw whole block-row bands and global histogram reduction →
   palette construction → broadcast. Contrast neighbor communication with a global dependency.
8. **Correctness and portability.** Show `make test`, independent expected-value checks, byte
   equality, edge cases, failure timeouts, codec tests, and sanitizer validation. State the tested OS.
9. **Measurement method.** Include machine/compiler/MPI, dimensions, filter settings, worker layout,
   one warm-up, seven samples, and precise timing boundaries. Mention unpinned OS scheduling.
10. **Strong scaling.** Insert time, speedup, and efficiency graphs for both filters at 2048² or
    4096² from `results/mac-release/`. Use `docs/RESULTS.md` for observed numbers and limitations.
11. **Weak scaling and challenges.** Insert the two weak plots. Explain fixed width/proportional
    height, fixed pixels/blocks per worker, palette overhead, bandwidth, and heterogeneous cores.
12. **Demo and conclusions.** Run the short demo. State what passed, what improved, what did not
    scale, and what future multi-node or architecture-specific experiments would test.

The optional local deck is `presentation/parallel-image-filtering.pptx` (excluded from Git).
It expands this outline into
15 slides, including a cover and separate speedup, efficiency, and weak-scaling slides. Charts
and tables remain editable, and every slide has speaker notes. Allow 12–15 minutes with the demo.
