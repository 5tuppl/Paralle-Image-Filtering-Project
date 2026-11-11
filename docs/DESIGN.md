# Design, invariants, and TigerStyle adaptation

## Ownership and interfaces

`Image` owns one row-major RGB byte vector; `ImageView` borrows immutable pixels and dimensions.
`FilterOptions` describes the operation. `Execution` owns partition counts/displacements, local
source/target buffers, and a filter-specific workspace. `Palette` stores three separate channel
arrays. Histograms store separate arrays of counts, red sums, green sums, and blue sums.

The C++ image API now uses explicit owners and exceptions for setup/I/O failures; kernels accept
views/pointers and preallocated workspaces. The executable CLI remains compatible with existing
blur invocations. Errors are reported collectively so other ranks do not wait in a later collective.
MPI operation errors use `MPI_Abort`, since safely recovering a partially failed collective is
outside the project. File access is confined to rank zero except standalone utilities/tests.

Only rank zero stores the full image. Other ranks hold their assigned bands; blur adds two halo
rows. Shared-memory workers read immutable inputs and write disjoint rows or blocks. Each private
histogram belongs to one thread. Its merge is partitioned by histogram field. Palette data becomes
read-only after construction/broadcast. Rank boundaries are checked before allocation/distribution.

## Blur

Horizontal sums are computed for owned rows and halo rows, then three horizontal sums are added
vertically. All sums fit in `uint16_t`: 3×255 = 765 horizontally and 9×255 = 2295 vertically.
Only the final sum is divided by 9. This preserves the original integer stencil exactly.

Global edges are replicated. MPI exchanges raw RGB halo rows with two `MPI_Sendrecv` calls before
each pass. Threads process rows with static scheduling and an implicit barrier between the two
separable stages. Source/target buffers swap after each pass. Each measured repetition overwrites
its input band from the original image, independent of the number/parity of previous passes.

For N owned pixels, blur needs roughly 6N bytes of source/target RGB plus 6N bytes of horizontal
sums (and halos). Root input/output/reference storage is separate. Each pass communicates two
RGB rows per interior rank. The separable implementation trades scratch memory/traffic for fewer
arithmetic operations and simpler inner loops; the scalar 3×3 stencil remains a correctness oracle.

## Pixel-art definition

Blocks start at (0, 0), have side B, and clip at the right/bottom edges. Each RGB channel is the
floor of its 64-bit sum divided by the actual block area. There is one histogram vote per block,
including partial blocks. Histogram bin coordinates are `channel >> 3`, with RGB lexicographic
index `r*1024 + g*32 + b`.

Median cut uses actual channel sums/counts, not bin-center colors:

1. Start with occupied bins in ascending bin order in one box.
2. Select a splittable box with largest channel range; then largest population; then earliest
   box index. Choose the largest-range channel, breaking ties R, G, B.
3. Sort that box by selected-channel bin coordinate, then full bin index. Split at the first
   cumulative count reaching `ceil(population/2)`, ensuring both children contain bins.
4. Keep the lower child in the selected slot, append the upper child, and repeat until reaching
   the requested palette limit or the number of occupied bins.
5. Each palette color is the floor of its box's RGB sums divided by its population.

All working arrays are prepared before filtering. `std::sort` operates in place; the median-cut
loop is bounded by 256 boxes. Empty boxes cannot be created. Nearest-color search uses squared RGB
distance with first-entry tie-breaking, so duplicated palette colors are harmless.

Ordered dithering uses this Bayer matrix at **global block coordinates**:

```
 0  8  2 10
12  4 14  6
 3 11  1  9
15  7 13  5
```

Add `2*matrix_value - 15` to each block-mean channel, clamp to [0,255], choose the nearest palette
entry, and fill the original block. Disabling dithering sets the offset to zero. The output has
exactly the input dimensions and at most the requested number of distinct RGB colors.

MPI distributes whole block-row bands using quotient/remainder ownership. Each rank computes local
means and private thread histograms. Local integer merging precedes `MPI_Reduce` to rank zero;
rank zero builds the palette, broadcasts its count/channel arrays, and each rank expands its own
blocks. No pixel halos are needed and no rank-dependent dither phase exists.

Each thread histogram is 32768×4×8 bytes = 1 MiB. The local/global histograms need another 2 MiB per
rank. Block means need approximately 3N/B² bytes. Palette search is O(KN/B²) for K≤256; block averaging
and expansion are O(N). Histogram zeroing/reduction and serial palette construction are fixed costs
that can dominate small images. For B=8, a 2048² image has 65536 block means; searching 64 colors
requires about 4.2 million color-distance evaluations.

## Practical TigerStyle

The TigerStyle adaptation guides project-owned code: explicit control flow, fixed-width data,
checked bounds, meaningful invariants, descriptive snake_case, short functions, and 100-column
source lines. There is no project-defined recursion, virtual dispatch, per-pixel allocation, or
shared mutable pixel accumulator. Assertions remain enabled in release builds. External inputs
produce errors instead of relying on assertions.

Allocations occur during setup, decoding, reference construction, and reporting; all processing
workspaces are reused. MPI/OpenMP and standard-library internals are outside the no-allocation
contract. The standard library's in-place sort is allowed. Container sizes, standard I/O, and MPI
interfaces use their required native types with checked conversions; image dimensions, pixels,
counts, and arithmetic use explicit widths.

The original zero-dependency/Zig-only rules are intentionally adapted: C++17, OpenMP, and MPI are
course requirements; pinned stb is isolated in `codecs.cpp`; Python standard-library scripts handle
orchestration, independent oracles, CSV, and SVG instead of embedding shell commands and drawing
code in a large C++ benchmark. Python does not process timed image kernels. Library source is
excluded from project formatting rules; a documented unsigned-bit-buffer fix is applied to stb's
JPEG fixture writer after a UBSan finding.

## Validation and measurement

Tests combine hand-calculated cases, an independent scalar blur, separate Python implementations
of both algorithms, cross-mode byte comparisons, codec checks, and bounded failure tests. MPI
subprocesses have timeouts and process-group cleanup. Sanitizers fail on findings. Checksum matching
is supplementary evidence; correctness comparisons use every byte.

The primary timer starts immediately before input copy/scatter and ends immediately after completed
output copy/gather. It includes all histogram clearing, computation, and required communication.
All modes exclude setup/allocation and I/O. Rank maxima are reduced after the timer. Separate stage
maxima help locate overhead, but cannot be added to reconstruct total time.

Release benchmarks use the same optimized algorithm for serial and parallel measurements. They
verify every output outside the timer, retain seven samples after one warm-up, and report medians
and variability. Single-machine MPI runs have no explicit binding and OpenMP binding is disabled;
this is recorded in the environment metadata, not represented as a controlled affinity experiment.
