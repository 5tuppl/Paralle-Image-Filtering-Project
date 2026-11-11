#include "filter.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <omp.h>

void pixel_prepare(PixelWorkspace &workspace, std::uint32_t width, std::uint32_t rows,
                   std::uint32_t row_offset, std::uint32_t threads,
                   const FilterOptions &options)
{
    assert(threads > 0);
    assert(row_offset % options.block_size == 0);
    workspace.block_columns = divide_ceil(width, options.block_size);
    workspace.block_rows = divide_ceil(rows, options.block_size);
    workspace.block_row_offset = row_offset / options.block_size;
    workspace.threads = threads;
    workspace.means.resize(static_cast<std::uint64_t>(workspace.block_columns) *
                           workspace.block_rows * 3);
    const std::uint64_t fields = histogram_bins * histogram_fields;
    workspace.private_histograms.resize(fields * threads);
    workspace.histogram.resize(fields);
    workspace.global_histogram.resize(fields);
    workspace.indices.resize(histogram_bins);
}

void pixel_average(ImageView source, PixelWorkspace &workspace,
                   const FilterOptions &options, bool parallel)
{
    assert(source.pixels != nullptr);
    assert(workspace.block_columns == divide_ceil(source.width, options.block_size));
    const std::uint32_t size = options.block_size;
    #pragma omp parallel for schedule(static) if(parallel)
    for (std::uint32_t row = 0; row < workspace.block_rows; ++row) {
        for (std::uint32_t column = 0; column < workspace.block_columns; ++column) {
            std::uint64_t sums[3]{};
            const std::uint32_t begin_y = row * size;
            const std::uint32_t begin_x = column * size;
            const std::uint32_t end_y = begin_y + std::min(size, source.height - begin_y);
            const std::uint32_t end_x = begin_x + std::min(size, source.width - begin_x);
            for (std::uint32_t y = begin_y; y < end_y; ++y) {
                for (std::uint32_t x = begin_x; x < end_x; ++x) {
                    const auto index = (static_cast<std::uint64_t>(y) * source.width + x) * 3;
                    for (std::uint32_t channel = 0; channel < 3; ++channel) {
                        sums[channel] += source.pixels[index + channel];
                    }
                }
            }
            const std::uint64_t count = static_cast<std::uint64_t>(end_y - begin_y) *
                                        (end_x - begin_x);
            const auto target = (static_cast<std::uint64_t>(row) *
                                 workspace.block_columns + column) * 3;
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                workspace.means[target + channel] =
                    static_cast<std::uint8_t>(sums[channel] / count);
            }
        }
    }
}

void pixel_histogram(PixelWorkspace &workspace, bool parallel)
{
    assert(workspace.threads > 0);
    assert(workspace.histogram.size() == histogram_bins * histogram_fields);
    const std::uint64_t fields = histogram_bins * histogram_fields;
    const std::uint64_t count = workspace.means.size() / 3;
    #pragma omp parallel if(parallel)
    {
        const auto thread = static_cast<std::uint32_t>(omp_get_thread_num());
        assert(thread < workspace.threads);
        #pragma omp for schedule(static)
        for (std::uint64_t field = 0; field < fields * workspace.threads; ++field) {
            workspace.private_histograms[field] = 0;
        }
        auto *histogram = workspace.private_histograms.data() + thread * fields;
        #pragma omp for schedule(static)
        for (std::uint64_t index = 0; index < count; ++index) {
            const auto *color = workspace.means.data() + index * 3;
            const std::uint32_t bin = (color[0] >> 3) * 1024 +
                                       (color[1] >> 3) * 32 + (color[2] >> 3);
            ++histogram[bin];
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                histogram[(channel + 1) * histogram_bins + bin] += color[channel];
            }
        }
    }
    #pragma omp parallel for schedule(static) if(parallel)
    for (std::uint32_t field = 0; field < fields; ++field) {
        std::uint64_t sum = 0;
        for (std::uint32_t thread = 0; thread < workspace.threads; ++thread) {
            sum += workspace.private_histograms[thread * fields + field];
        }
        workspace.histogram[field] = sum;
    }
}

static std::uint32_t bin_channel(std::uint32_t bin, std::uint32_t channel)
{
    assert(bin < histogram_bins);
    assert(channel < 3);
    return (bin >> ((2 - channel) * 5)) & 31;
}

static void box_measure(ColorBox &box, const PixelWorkspace &workspace)
{
    assert(box.begin < box.end);
    assert(box.end <= histogram_bins);
    std::uint32_t minimum[3] = {31, 31, 31};
    std::uint32_t maximum[3]{};
    box.population = 0;
    box.range = 0;
    box.channel = 0;
    for (std::uint32_t index = box.begin; index < box.end; ++index) {
        const std::uint32_t bin = workspace.indices[index];
        box.population += workspace.global_histogram[bin];
        for (std::uint32_t channel = 0; channel < 3; ++channel) {
            minimum[channel] = std::min(minimum[channel], bin_channel(bin, channel));
            maximum[channel] = std::max(maximum[channel], bin_channel(bin, channel));
        }
    }
    for (std::uint32_t channel = 0; channel < 3; ++channel) {
        if (maximum[channel] - minimum[channel] > box.range) {
            box.range = maximum[channel] - minimum[channel];
            box.channel = channel;
        }
    }
    assert(box.population > 0);
}

static void box_split(PixelWorkspace &workspace, std::uint32_t selected,
                      std::uint32_t next)
{
    auto &box = workspace.boxes[selected];
    assert(box.end - box.begin > 1);
    assert(next < palette_capacity);
    const std::uint32_t channel = box.channel;
    std::sort(workspace.indices.begin() + box.begin, workspace.indices.begin() + box.end,
        [channel](std::uint32_t left, std::uint32_t right) {
            const auto a = bin_channel(left, channel);
            const auto b = bin_channel(right, channel);
            return a == b ? left < right : a < b;
        });
    const std::uint64_t halfway = (box.population + 1) / 2;
    std::uint64_t accumulated = 0;
    std::uint32_t split = box.begin;
    do {
        accumulated += workspace.global_histogram[workspace.indices[split++]];
    } while (accumulated < halfway && split < box.end - 1);
    workspace.boxes[next] = {split, box.end, 0, 0, 0};
    box.end = split;
    box_measure(box, workspace);
    box_measure(workspace.boxes[next], workspace);
}

static void palette_colors(PixelWorkspace &workspace)
{
    assert(workspace.palette.count > 0);
    assert(workspace.palette.count <= palette_capacity);
    for (std::uint32_t index = 0; index < workspace.palette.count; ++index) {
        const auto &box = workspace.boxes[index];
        std::uint64_t sums[3]{};
        for (std::uint32_t item = box.begin; item < box.end; ++item) {
            const std::uint32_t bin = workspace.indices[item];
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                sums[channel] += workspace.global_histogram[(channel + 1) * histogram_bins + bin];
            }
        }
        workspace.palette.red[index] = static_cast<std::uint8_t>(sums[0] / box.population);
        workspace.palette.green[index] = static_cast<std::uint8_t>(sums[1] / box.population);
        workspace.palette.blue[index] = static_cast<std::uint8_t>(sums[2] / box.population);
    }
}

void pixel_palette(PixelWorkspace &workspace, const FilterOptions &options)
{
    assert(options.colors > 0);
    assert(options.colors <= palette_capacity);
    std::uint32_t occupied = 0;
    for (std::uint32_t bin = 0; bin < histogram_bins; ++bin) {
        if (workspace.global_histogram[bin] > 0) workspace.indices[occupied++] = bin;
    }
    assert(occupied > 0);
    workspace.boxes[0] = {0, occupied, 0, 0, 0};
    box_measure(workspace.boxes[0], workspace);
    workspace.palette.count = 1;
    while (workspace.palette.count < std::min(options.colors, occupied)) {
        std::uint32_t selected = palette_capacity;
        for (std::uint32_t index = 0; index < workspace.palette.count; ++index) {
            const auto &box = workspace.boxes[index];
            if (box.end - box.begin == 1) continue;
            if (selected == palette_capacity) {
                selected = index;
            } else {
                const auto &best = workspace.boxes[selected];
                if (box.range > best.range ||
                    (box.range == best.range && box.population > best.population)) selected = index;
            }
        }
        assert(selected < palette_capacity);
        box_split(workspace, selected, workspace.palette.count++);
    }
    palette_colors(workspace);
}

std::int32_t dither_offset(std::uint32_t row, std::uint32_t column)
{
    constexpr std::int32_t bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    static_assert(sizeof(bayer) == 16 * sizeof(std::int32_t));
    const auto offset = bayer[row % 4][column % 4] * 2 - 15;
    assert(offset >= -15);
    assert(offset <= 15);
    return offset;
}

std::uint32_t palette_nearest(const Palette &palette, std::int32_t red,
                              std::int32_t green, std::int32_t blue)
{
    assert(palette.count > 0);
    assert(palette.count <= palette_capacity);
    std::int32_t distance_min = std::numeric_limits<std::int32_t>::max();
    std::uint32_t nearest = 0;
    for (std::uint32_t index = 0; index < palette.count; ++index) {
        const std::int32_t dr = red - palette.red[index];
        const std::int32_t dg = green - palette.green[index];
        const std::int32_t db = blue - palette.blue[index];
        const std::int32_t distance = dr * dr + dg * dg + db * db;
        if (distance < distance_min) {
            distance_min = distance;
            nearest = index;
        }
    }
    return nearest;
}

void pixel_expand(std::uint8_t *target, std::uint32_t width, std::uint32_t rows,
                  const PixelWorkspace &workspace, const FilterOptions &options,
                  bool parallel)
{
    assert(target != nullptr);
    assert(rows > 0);
    #pragma omp parallel for schedule(static) if(parallel)
    for (std::uint32_t row = 0; row < workspace.block_rows; ++row) {
        for (std::uint32_t column = 0; column < workspace.block_columns; ++column) {
            const auto index = (static_cast<std::uint64_t>(row) *
                                workspace.block_columns + column) * 3;
            const auto offset = options.dither ?
                dither_offset(row + workspace.block_row_offset, column) : 0;
            const auto nearest = palette_nearest(workspace.palette,
                std::clamp(workspace.means[index] + offset, 0, 255),
                std::clamp(workspace.means[index + 1] + offset, 0, 255),
                std::clamp(workspace.means[index + 2] + offset, 0, 255));
            const std::uint32_t begin_y = row * options.block_size;
            const std::uint32_t begin_x = column * options.block_size;
            const auto end_y = begin_y + std::min(options.block_size, rows - begin_y);
            const auto end_x = begin_x + std::min(options.block_size, width - begin_x);
            for (std::uint32_t y = begin_y; y < end_y; ++y) {
                auto *output = target + (static_cast<std::uint64_t>(y) * width + begin_x) * 3;
                for (std::uint32_t x = begin_x; x < end_x; ++x) {
                    *output++ = workspace.palette.red[nearest];
                    *output++ = workspace.palette.green[nearest];
                    *output++ = workspace.palette.blue[nearest];
                }
            }
        }
    }
}
