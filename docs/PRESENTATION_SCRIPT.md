# Presentation script: parallel image filtering

Matches the 15-slide local deck in `presentation/parallel-image-filtering.pptx` (excluded from
Git). A fresh checkout can follow this script and generate its own demo using `make demo`.

**Speaking split:** Your mate presents slides 1–7, covering the introduction, shared design,
and blur. You present slides 8–14, covering the pixel-art communication pattern, validation,
and results. Both present slide 15. This is a speaking assignment, not a claim about who
wrote each part of the code.

**Target:** 12–15 minutes including a short live demo. Speak conversationally and pause at
the figures. Bracketed directions are actions, not words to read aloud. Replace “my teammate”
with each other's names if you prefer.

## Slide 1 — Parallel image filtering

**Speaker: Your mate · About 25 seconds**

“Hi everyone. Our project is parallel image filtering in C++.

We implemented two filters: blur and pixel art. Both run in serial, OpenMP, MPI, and a hybrid
mode combining MPI with OpenMP.

Our goal was to keep the output exactly the same across these modes, then measure how much
parallel execution helps and where scaling starts to become limited.”

## Slide 2 — Two filters, one parallel-computing problem

**Speaker: Your mate · About 40 seconds**

[Point to the input, then the two outputs.]

“These images show the two operations. Blur smooths the image by averaging neighboring pixels.
Pixel art groups pixels into larger blocks and reduces the number of colors.

Image filtering is useful for a parallel algorithms project because large images contain
millions of pixels, with similar work repeated across them.

The interesting part is deciding what each worker owns and what information it needs from
other workers. Blur needs neighboring rows. Pixel art needs a palette shared by the whole image.
Both filters preserve the original image dimensions.”

## Slide 3 — Filter definitions

**Speaker: Your mate · About 65 seconds**

“Our blur is a three-by-three box filter. For each color channel, we add the nine values in
the neighborhood, divide by nine, and round down. At the image edges, we repeat the nearest
valid pixel.

We optimize this by calculating horizontal sums first, then combining them vertically.
We divide only once, at the end. Dividing after each stage could change the rounding and
produce different pixels.

The pixel-art pipeline has four stages. First, average each square block. Second, build an
adaptive palette from the block colors. Third, use a fixed Bayer pattern to adjust color
selection. Finally, fill each block with its selected palette color.

Partial blocks at the image edges use only the pixels that actually exist. Integer arithmetic
and fixed tie-breaking rules make the output deterministic.”

## Slide 4 — Four execution modes

**Speaker: Your mate · About 50 seconds**

[Use the ownership column to explain the table.]

“Serial processes the entire image with one worker. This is our optimized baseline for
performance comparisons.

OpenMP uses multiple threads in one process. They share memory, but each thread writes to a
different output region.

MPI uses separate processes, called ranks. Each rank owns a contiguous band of the image,
and we explicitly communicate the data it needs.

Hybrid combines these approaches. MPI divides the image between processes, then OpenMP
threads work inside each process's band.

Only the thread that initializes MPI makes MPI calls. We request and check the corresponding
thread-support level, called MPI thread funneled.”

## Slide 5 — Data-oriented design and TigerStyle

**Speaker: Your mate · About 55 seconds**

“We organized the program around how the filters access data.

Image pixels are stored in one contiguous RGB array, in row order. Both filters use the color
channels together, so this layout fits their access pattern and makes row bands easy to distribute.

We allocate working buffers before filtering and reuse them. Blur alternates between source
and destination buffers. Pixel art gives each thread its own histogram, so threads do not
compete to update shared counters.

Our TigerStyle adaptation emphasizes explicit ownership, checked sizes, assertions, and short
functions. It is a practical adaptation for C++: setup, image codecs, and external runtimes
have documented exceptions to the allocation rules.”

## Slide 6 — OpenMP shares independent work

**Speaker: Your mate · About 50 seconds**

[Point to the loop, then “One writer per output byte.”]

“This pseudocode shows the main OpenMP pattern. Static scheduling assigns contiguous rows or
blocks to threads. Each thread reads the source and writes only its assigned output region.

For blur, separate source and destination buffers prevent one thread from reading a pixel
that another thread has already overwritten.

For pixel art, each thread builds a private histogram. We merge the histograms after local
work finishes.

Barriers separate dependent stages. For example, all horizontal blur sums must be ready
before the vertical stage uses them. This gives us parallel work while keeping the data
dependencies explicit.”

## Slide 7 — MPI blur needs neighboring rows

**Speaker: Your mate · About 65 seconds**

[Trace the middle rank's owned rows and halo copies.]

“This example divides ten image rows across three MPI ranks. The first rank owns four rows,
and the other two own three each. Uneven divisions are supported.

The middle rank owns rows four through six. To blur its first and last rows, it also needs
row three from the rank above and row seven from the rank below.

These extra read-only copies are called halo rows. They supply neighboring data, while each
rank still writes only its owned rows.

We distribute the image, exchange halos before every blur pass, and gather the completed
output. The outer image boundaries use the same clamped-edge rule as serial execution.

That covers blur's neighbor communication. My teammate will now explain pixel art's global
palette and our measured results.”

## Slide 8 — MPI pixel art uses one global palette

**Speaker: You · About 65 seconds**

“Pixel art has a different dependency. We assign whole block-row bands to ranks, so a block
never crosses a rank boundary.

Each rank averages its blocks and builds a local color histogram. We reduce the histogram
counts and RGB sums to rank zero. Rank zero constructs one palette and broadcasts it to
every rank.

Using one palette matters because independent palettes could create visible seams between
image bands.

Each rank then selects colors and expands its own blocks. The Bayer pattern uses global
block coordinates, so it continues correctly across rank boundaries.

Unlike error-diffusion dithering, this pattern does not pass error into the next block.
Once the palette is available, color selection can proceed independently.”

## Slide 9 — Correctness before speed

**Speaker: You · About 50 seconds**

“Before measuring speed, we checked that the algorithms produce the intended output.

Matching serial output alone is insufficient if serial and parallel share the same bug.
We therefore also used hand-calculated examples and separate Python reference implementations.

The suite passed 101 integration and failure cases, plus codec checks. It covers tiny images,
uneven partitions, partial blocks, and invalid inputs. We compare every output byte across
execution modes.

We also ran address and undefined-behavior sanitizers, and checked that prepared kernels do
not allocate project-owned memory. MPI failure tests use timeouts to catch hangs as well as
incorrect results.”

## Slide 10 — Measurement method

**Speaker: You · About 60 seconds**

“These measurements come from one Apple M5 Pro Mac with fifteen CPU cores.

We tested one, two, four, eight, and fifteen workers. Every configuration had one warm-up
followed by seven measured repetitions, each starting from the original input. We report
the median, with 1,106 measured samples across 158 configurations.

The timer covers prepared input through completed in-memory output. It includes filtering,
data distribution, communication, and gathering. Allocation, file input and output, and
verification are outside the timer for every mode.

For MPI, we report the slowest rank's duration. Workers were not pinned to specific cores,
so operating-system scheduling can affect the results. These are single-machine measurements.”

## Slide 11 — Strong scaling: speedup at 4096 × 4096

**Speaker: You · About 55 seconds**

[Point to the OpenMP endpoint on each graph.]

“Strong scaling keeps the image size fixed while increasing the worker count. Speedup is
optimized serial time divided by parallel time.

For this 4096-by-4096 image, OpenMP was fastest in our run. With fifteen threads, ten blur
passes fell from about 422 milliseconds to 55 milliseconds, giving a 7.72-times speedup.

Pixel art fell from about 22.9 milliseconds to 5.25 milliseconds, giving a 4.37-times speedup.

More workers did not always help. Pixel-art MPI improved up to eight workers, then slowed
at fifteen. Also, the fifteen-worker hybrid point uses fifteen ranks with one thread each,
so that particular layout is effectively MPI.”

## Slide 12 — Efficiency falls as worker count grows

**Speaker: You · About 40 seconds**

“Efficiency is speedup divided by the number of workers. Ideal linear scaling would give
an efficiency of one, or one hundred percent.

At fifteen OpenMP workers, blur achieved about 51 percent efficiency, while pixel art achieved
about 29 percent. Both were faster than serial, but neither gained fifteen times the speed.

Communication, synchronization, and serial stages contribute overhead. Memory bandwidth is
another possible limit, although we did not use hardware counters to measure its individual
contribution. The graphs show that each additional worker gives a smaller benefit.”

## Slide 13 — Weak scaling exposes growing overhead

**Speaker: You · About 55 seconds**

[Point to the ideal line at one. The two charts have different vertical scales.]

“Weak scaling grows the image with the worker count, keeping the number of pixels per worker
constant. We keep width at 512 and increase height to 512 times the worker count.

Each curve is normalized to that mode's one-worker time. Ideally, it would stay at one.

Instead, time generally increases at larger worker counts. At fifteen workers, OpenMP takes
about twice its one-worker time for blur and 2.27 times for pixel art.

The large pixel-art MPI point was especially variable between runs. It shows poor scaling
in this measurement, but we cannot treat it as a stable hardware limit. We kept the raw
samples so this variation remains visible.”

## Slide 14 — Optimization choices and remaining limits

**Speaker: You · About 50 seconds**

“Each optimization addressed a specific cost and introduced tradeoffs.

Separable blur reduces repeated arithmetic while preserving exact results, but its scratch
buffer still creates memory traffic.

Private histograms avoid shared-counter contention, but each thread's histogram uses one
mebibyte and must be cleared and merged.

A global palette prevents seams, but requires histogram communication and serial palette
construction on rank zero. Our stage measurements show that this combined palette stage
costs more in MPI than in OpenMP.

The next experiments would control CPU affinity, vary image content, and test multiple
machines. Those would help separate the algorithm's costs from laptop scheduling effects.”

## Slide 15 — Live demo and conclusions

**Speakers: Both · Allow 1½–2 minutes**

**Your mate:**

“We'll finish by showing the working program. The test suite checks output correctness and
failure handling. Then we'll run pixel art using two MPI ranks, with two OpenMP threads per
rank, for four workers in total.”

[Show the completed `make test` output. Run the hybrid command shown below.]

**You:**

“The verify option compares this result with serial output. Once it succeeds, we'll open the
generated image and compare it with the input.”

[Wait for successful verification before saying the next line. Open `data/live.png`.]

“Verification passed. This is the resulting pixel-art image, with the original dimensions
preserved. The default settings use eight-by-eight blocks and up to sixty-four colors.”

**Your mate:**

“Blur demonstrates communication between neighboring image bands. Pixel art demonstrates a
global reduction followed by independent local work.”

**You:**

“Both filters produced identical output across all four execution modes in our tests. OpenMP
gave the best large-image results on this Mac, while the scaling graphs show the growing
cost of overhead. Linux and multiple-machine execution still need separate validation.

Thank you. We're happy to take questions.”

## Demo preparation — do this before presenting

Run these commands from the repository root:

```sh
make -j4 release
make test
make demo
mkdir -p data
```

Keep the successful test output available. Re-running the whole suite during the talk may
exceed the demo slot. If you show a previous run, say “Here is the completed test run.”

For a fresh checkout, use this command. Slide 15 uses the optional local artwork at
`results/demo/input.png`; replace that path with the generated input below:

```sh
mpirun --bind-to none -np 2 bin/parallel_filter \
  --input data/demo/input.png --output data/live.png \
  --filter pixel --mode hybrid --threads 2 --verify
```

On the demo Mac, open the result with:

```sh
open data/live.png
```

Have `data/demo/input.png`, `data/demo/blur.png`, and `data/demo/pixel.png` ready as
backup examples. The generated pixel example uses 32 colors, while the displayed live command
uses the default 64. If using the backup, explicitly call it a previously generated result.

## Quick rehearsal notes

- Your mate owns the opening and blur explanation. You own the pixel-art MPI explanation
  and interpretation of the results. Rehearse the handoff after slide 7 once together.
- Say “seven point seven two times faster” for 7.72×, and “twenty-nine percent efficiency”
  for 29.1%. Avoid reading every graph point or every software version aloud.
- MPI ranks are processes. OpenMP workers are threads. In hybrid measurements, the total
  worker count is ranks multiplied by threads per rank.
- Speedup compares parallel execution with optimized serial execution on the same workload.
  Weak-scaling normalized time compares the same mode as both input size and workers grow.
- Keep causes qualified: communication has supporting stage measurements, while attributing
  a particular fraction of lost speedup to memory bandwidth would require more profiling.
- For a shorter slot, shorten slides 5, 9, and 14 and show the completed test run. Keep the
  ownership explanations, timing boundary, graph definitions, and single-machine limitation.

Script checked against the finished deck and `docs/RESULTS.md`. For technical questions,
rehearse the short answers in `docs/DEFENSE_GUIDE.md`.
