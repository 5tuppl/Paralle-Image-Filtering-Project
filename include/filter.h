#ifndef FILTER_H
#define FILTER_H
#include "image.h"
#include <array>

constexpr std::uint32_t histogram_bins = 32768;
constexpr std::uint32_t histogram_fields = 4;
constexpr std::uint32_t palette_capacity = 256;
enum class Filter { blur, pixel };
enum class Mode { serial, openmp, mpi, hybrid };
struct FilterOptions {
    Filter filter = Filter::blur;
    std::uint32_t iterations = 1;
    std::uint32_t block_size = 8;
    std::uint32_t colors = 64;
    bool dither = true;
};
struct Palette {
    std::array<std::uint8_t, palette_capacity> red{};
    std::array<std::uint8_t, palette_capacity> green{};
    std::array<std::uint8_t, palette_capacity> blue{};
    std::uint32_t count = 0;
};
struct ColorBox {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t channel = 0;
    std::uint32_t range = 0;
    std::uint64_t population = 0;
};
struct PixelWorkspace {
    std::vector<std::uint8_t> means;
    std::vector<std::uint64_t> private_histograms;
    std::vector<std::uint64_t> histogram;
    std::vector<std::uint64_t> global_histogram;
    std::vector<std::uint32_t> indices;
    std::array<ColorBox, palette_capacity> boxes{};
    Palette palette;
    std::uint32_t block_columns = 0;
    std::uint32_t block_rows = 0;
    std::uint32_t block_row_offset = 0;
    std::uint32_t threads = 1;
};
struct BlurWorkspace {
    std::vector<std::uint16_t> horizontal;
};
std::uint32_t divide_ceil(std::uint32_t count, std::uint32_t divisor);
void blur_edges(std::uint8_t *source, std::uint32_t width, std::uint32_t rows,
                bool top, bool bottom);
void blur_pass(const std::uint8_t *source, std::uint8_t *target,
               std::uint32_t width, std::uint32_t rows, BlurWorkspace &workspace,
               bool parallel);
void pixel_prepare(PixelWorkspace &workspace, std::uint32_t width, std::uint32_t rows,
                   std::uint32_t row_offset, std::uint32_t threads,
                   const FilterOptions &options);
void pixel_average(ImageView source, PixelWorkspace &workspace,
                   const FilterOptions &options, bool parallel);
void pixel_histogram(PixelWorkspace &workspace, bool parallel);
void pixel_palette(PixelWorkspace &workspace, const FilterOptions &options);
std::uint32_t palette_nearest(const Palette &palette, std::int32_t red,
                              std::int32_t green, std::int32_t blue);
std::int32_t dither_offset(std::uint32_t row, std::uint32_t column);
void pixel_expand(std::uint8_t *target, std::uint32_t width, std::uint32_t rows,
                  const PixelWorkspace &workspace, const FilterOptions &options,
                  bool parallel);
Image filter_reference(const Image &input, const FilterOptions &options);
#endif
