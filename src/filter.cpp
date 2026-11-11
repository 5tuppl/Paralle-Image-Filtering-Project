#include "filter.h"
#include <algorithm>
#include <cassert>
#include <cstring>

std::uint32_t divide_ceil(std::uint32_t count, std::uint32_t divisor)
{
    assert(count > 0);
    assert(divisor > 0);
    return count / divisor + (count % divisor != 0);
}

void blur_edges(std::uint8_t *source, std::uint32_t width, std::uint32_t rows,
                bool top, bool bottom)
{
    assert(source != nullptr);
    assert(rows > 0);
    const std::uint64_t stride = static_cast<std::uint64_t>(width) * 3;
    if (top) std::memcpy(source, source + stride, stride);
    if (bottom) std::memcpy(source + (rows + 1) * stride, source + rows * stride, stride);
}

void blur_pass(const std::uint8_t *source, std::uint8_t *target,
               std::uint32_t width, std::uint32_t rows, BlurWorkspace &workspace,
               bool parallel)
{
    assert(source != target);
    assert(workspace.horizontal.size() == static_cast<std::uint64_t>(rows + 2) * width * 3);
    const std::uint64_t stride = static_cast<std::uint64_t>(width) * 3;
    auto *horizontal = workspace.horizontal.data();
    // Horizontal sums retain all precision; dividing here would change the blur.
    #pragma omp parallel for schedule(static) if(parallel)
    for (std::uint32_t row = 0; row < rows + 2; ++row) {
        const std::uint64_t offset = row * stride;
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::uint32_t left = column == 0 ? 0 : column - 1;
            const std::uint32_t right = std::min(column + 1, width - 1);
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                horizontal[offset + column * 3 + channel] = static_cast<std::uint16_t>(
                    source[offset + left * 3 + channel] +
                    source[offset + column * 3 + channel] +
                    source[offset + right * 3 + channel]);
            }
        }
    }
    #pragma omp parallel for schedule(static) if(parallel)
    for (std::uint32_t row = 1; row <= rows; ++row) {
        const std::uint64_t offset = row * stride;
        for (std::uint64_t index = 0; index < stride; ++index) {
            const std::uint32_t sum = horizontal[offset - stride + index] +
                horizontal[offset + index] + horizontal[offset + stride + index];
            target[offset + index] = static_cast<std::uint8_t>(sum / 9);
        }
    }
}
