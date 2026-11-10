#include "filter.h"

#include <omp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void blur_rows(const uint8_t *source, uint8_t *destination,
                      int width, int height, int use_openmp)
{
    /* Each output pixel is independent, so rows can be shared by threads. */
    #pragma omp parallel for schedule(static) if(use_openmp)
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            for (int channel = 0; channel < 3; ++channel) {
                unsigned int sum = 0;
                for (int vertical = -1; vertical <= 1; ++vertical) {
                    int actual_row = row + vertical;
                    if (actual_row < 0) actual_row = 0;
                    if (actual_row >= height) actual_row = height - 1;
                    for (int horizontal = -1; horizontal <= 1; ++horizontal) {
                        int actual_column = column + horizontal;
                        if (actual_column < 0) actual_column = 0;
                        if (actual_column >= width) actual_column = width - 1;
                        sum += source[((size_t)actual_row * (size_t)width +
                                       (size_t)actual_column) * 3U +
                                      (size_t)channel];
                    }
                }
                destination[((size_t)row * (size_t)width + (size_t)column) * 3U +
                            (size_t)channel] = (uint8_t)(sum / 9U);
            }
        }
    }
}

int filter_shared(const Image *input, Image *output, int iterations, int use_openmp)
{
    const size_t byte_count = (size_t)input->width * (size_t)input->height * 3U;
    uint8_t *current;
    uint8_t *next;

    if (!image_allocate(output, input->width, input->height, 3)) {
        return 0;
    }
    current = (uint8_t *)malloc(byte_count);
    next = (uint8_t *)malloc(byte_count);
    if (current == NULL || next == NULL) {
        free(current);
        free(next);
        image_free(output);
        return 0;
    }
    memcpy(current, input->pixels, byte_count);

    for (int pass = 0; pass < iterations; ++pass) {
        uint8_t *temporary;
        blur_rows(current, next, input->width, input->height, use_openmp);
        temporary = current;
        current = next;
        next = temporary;
    }
    memcpy(output->pixels, current, byte_count);
    free(current);
    free(next);
    return 1;
}
