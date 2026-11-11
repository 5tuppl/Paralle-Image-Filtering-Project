#include "runtime.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

void mpi_check(int status)
{
    if (status != MPI_SUCCESS) MPI_Abort(MPI_COMM_WORLD, status);
}

static void execution_partition(Execution &execution, const Options &options,
                                std::uint32_t height)
{
    const auto size = options.filter.filter == Filter::pixel ? options.filter.block_size : 1;
    const auto units = divide_ceil(height, size);
    const auto ranks = static_cast<std::uint32_t>(execution.ranks);
    if (ranks > units) throw std::runtime_error("Ranks exceed owned rows or pixel block rows");
    execution.counts.resize(ranks);
    execution.displacements.resize(ranks);
    std::uint32_t begin = 0;
    for (std::uint32_t rank = 0; rank < ranks; ++rank) {
        const std::uint32_t count = units / ranks + (rank < units % ranks);
        const auto first = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(begin) * size, height);
        const auto last = std::min<std::uint64_t>(static_cast<std::uint64_t>(begin + count) *
                                                size, height);
        execution.counts[rank] = static_cast<int>((last - first) * execution.width * 3);
        execution.displacements[rank] = static_cast<int>(first * execution.width * 3);
        if (rank == static_cast<std::uint32_t>(execution.rank)) {
            execution.rows = static_cast<std::uint32_t>(last - first);
            execution.row_offset = static_cast<std::uint32_t>(first);
        }
        begin += count;
    }
    assert(begin == units);
    assert(execution.rows > 0);
}

void execution_prepare(Execution &execution, const Options &options,
                       std::uint32_t width, std::uint32_t height)
{
    image_byte_count(width, height);
    execution.width = width;
    execution.distributed = options.mode == Mode::mpi || options.mode == Mode::hybrid;
    execution.parallel = options.mode == Mode::openmp || options.mode == Mode::hybrid;
    if (!execution.distributed && execution.ranks != 1) {
        throw std::runtime_error("Serial/OpenMP requires exactly one rank");
    }
    execution_partition(execution, options, height);
    const std::uint32_t halos = options.filter.filter == Filter::blur ? 2 : 0;
    const auto bytes = static_cast<std::uint64_t>(execution.rows + halos) * width * 3;
    execution.source.resize(bytes);
    execution.target.resize(bytes);
    if (options.filter.filter == Filter::blur) execution.blur.horizontal.resize(bytes);
    else pixel_prepare(execution.pixel, width, execution.rows, execution.row_offset,
                        options.threads, options.filter);
    assert(execution.source.size() == execution.target.size());
    assert(execution.counts[static_cast<std::uint32_t>(execution.rank)] > 0);
}

static void execution_exchange(Execution &execution)
{
    const int top = execution.rank == 0 ? MPI_PROC_NULL : execution.rank - 1;
    const int bottom = execution.rank + 1 == execution.ranks ? MPI_PROC_NULL : execution.rank + 1;
    const auto stride = static_cast<std::uint64_t>(execution.width) * 3;
    auto *source = execution.source.data();
    blur_edges(source, execution.width, execution.rows,
               top == MPI_PROC_NULL, bottom == MPI_PROC_NULL);
    assert(execution.rows > 0);
    assert(stride <= image_bytes_max);
    if (!execution.distributed) return;
    mpi_check(MPI_Sendrecv(source + stride, static_cast<int>(stride), MPI_BYTE, top, 100,
        source + (execution.rows + 1) * stride, static_cast<int>(stride), MPI_BYTE, bottom, 100,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    mpi_check(MPI_Sendrecv(source + execution.rows * stride, static_cast<int>(stride),
        MPI_BYTE, bottom, 101, source, static_cast<int>(stride), MPI_BYTE, top, 101,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE));
}

static void execution_palette(Execution &execution, const FilterOptions &options)
{
    auto &workspace = execution.pixel;
    assert(workspace.histogram.size() == histogram_bins * histogram_fields);
    assert(workspace.global_histogram.size() == workspace.histogram.size());
    if (execution.distributed) {
        mpi_check(MPI_Reduce(workspace.histogram.data(), workspace.global_histogram.data(),
            histogram_bins * histogram_fields, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD));
    } else {
        std::copy(workspace.histogram.begin(), workspace.histogram.end(),
                  workspace.global_histogram.begin());
    }
    if (execution.rank == 0) pixel_palette(workspace, options);
    if (execution.distributed) {
        auto &palette = workspace.palette;
        mpi_check(MPI_Bcast(&palette.count, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD));
        mpi_check(MPI_Bcast(palette.red.data(), palette_capacity, MPI_UINT8_T, 0, MPI_COMM_WORLD));
        mpi_check(MPI_Bcast(palette.green.data(), palette_capacity,
                            MPI_UINT8_T, 0, MPI_COMM_WORLD));
        mpi_check(MPI_Bcast(palette.blue.data(), palette_capacity, MPI_UINT8_T, 0, MPI_COMM_WORLD));
    }
}

static void execution_compute(Execution &execution, const FilterOptions &options, Timings &timings)
{
    assert(execution.width > 0);
    assert(execution.rows > 0);
    const double start = MPI_Wtime();
    if (options.filter == Filter::blur) {
        for (std::uint32_t pass = 0; pass < options.iterations; ++pass) {
            execution_exchange(execution);
            blur_pass(execution.source.data(), execution.target.data(), execution.width,
                       execution.rows, execution.blur, execution.parallel);
            execution.source.swap(execution.target);
        }
        timings.compute = MPI_Wtime() - start;
    } else {
        pixel_average({execution.source.data(), execution.width, execution.rows}, execution.pixel,
                       options, execution.parallel);
        pixel_histogram(execution.pixel, execution.parallel);
        timings.compute = MPI_Wtime() - start;
        const double palette_start = MPI_Wtime();
        execution_palette(execution, options);
        timings.palette = MPI_Wtime() - palette_start;
        const double expand_start = MPI_Wtime();
        pixel_expand(execution.target.data(), execution.width, execution.rows, execution.pixel,
                      options, execution.parallel);
        timings.compute += MPI_Wtime() - expand_start;
    }
}

static Timings timings_max(const Timings &local)
{
    double values[5] = {local.total, local.distribute, local.compute, local.palette, local.collect};
    double maximum[5]{};
    mpi_check(MPI_Reduce(values, maximum, 5, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD));
    return {maximum[0], maximum[1], maximum[2], maximum[3], maximum[4]};
}

Timings execution_run(Execution &execution, const Image &input, Image &output,
                      const Options &options)
{
    assert(execution.source.size() == execution.target.size());
    assert(execution.rows > 0);
    const auto offset = options.filter.filter == Filter::blur ? execution.width * 3 : 0;
    auto *source = execution.source.data() + offset;
    const int bytes = execution.counts[static_cast<std::uint32_t>(execution.rank)];
    mpi_check(MPI_Barrier(MPI_COMM_WORLD));
    Timings timings;
    const double start = MPI_Wtime();
    if (execution.distributed) {
        mpi_check(MPI_Scatterv(input.pixels.data(), execution.counts.data(),
            execution.displacements.data(), MPI_BYTE, source, bytes, MPI_BYTE, 0, MPI_COMM_WORLD));
    } else std::memcpy(source, input.pixels.data(), static_cast<std::uint64_t>(bytes));
    timings.distribute = MPI_Wtime() - start;
    execution_compute(execution, options.filter, timings);
    const double collect_start = MPI_Wtime();
    const auto *result = options.filter.filter == Filter::blur ?
        execution.source.data() + offset : execution.target.data();
    if (execution.distributed) {
        mpi_check(MPI_Gatherv(result, bytes, MPI_BYTE, output.pixels.data(),
            execution.counts.data(),
            execution.displacements.data(), MPI_BYTE, 0, MPI_COMM_WORLD));
    } else std::memcpy(output.pixels.data(), result, static_cast<std::uint64_t>(bytes));
    timings.collect = MPI_Wtime() - collect_start;
    timings.total = MPI_Wtime() - start;
    return timings_max(timings);
}
