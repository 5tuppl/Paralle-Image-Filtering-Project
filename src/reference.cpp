#include "filter.h"
#include <algorithm>
#include <cassert>

static Image blur_reference(const Image &input, std::uint32_t iterations)
{
    assert(iterations > 0);
    assert(input.pixels.size() == image_byte_count(input.width, input.height));
    Image current = input;
    Image next = image_create(input.width, input.height);
    for (std::uint32_t pass = 0; pass < iterations; ++pass) {
        for (std::uint32_t y = 0; y < input.height; ++y) {
            for (std::uint32_t x = 0; x < input.width; ++x) {
                for (std::uint32_t channel = 0; channel < 3; ++channel) {
                    std::uint32_t sum = 0;
                    for (std::int32_t dy = -1; dy <= 1; ++dy) {
                        for (std::int32_t dx = -1; dx <= 1; ++dx) {
                            const auto row = std::clamp<std::int64_t>(
                                static_cast<std::int64_t>(y) + dy, 0, input.height - 1);
                            const auto col = std::clamp<std::int64_t>(
                                static_cast<std::int64_t>(x) + dx, 0, input.width - 1);
                            const auto offset = static_cast<std::uint64_t>(
                                (row * input.width + col) * 3) + channel;
                            sum += current.pixels[offset];
                        }
                    }
                    next.pixels[(static_cast<std::uint64_t>(y) * input.width + x) * 3 + channel] =
                        static_cast<std::uint8_t>(sum / 9);
                }
            }
        }
        current.pixels.swap(next.pixels);
    }
    return current;
}

Image filter_reference(const Image &input, const FilterOptions &options)
{
    assert(input.width > 0);
    assert(input.height > 0);
    if (options.filter == Filter::blur) return blur_reference(input, options.iterations);
    auto output = image_create(input.width, input.height);
    PixelWorkspace workspace;
    pixel_prepare(workspace, input.width, input.height, 0, 1, options);
    pixel_average(image_view(input), workspace, options, false);
    pixel_histogram(workspace, false);
    workspace.global_histogram = workspace.histogram;
    pixel_palette(workspace, options);
    pixel_expand(output.pixels.data(), output.width, output.height, workspace, options, false);
    return output;
}
