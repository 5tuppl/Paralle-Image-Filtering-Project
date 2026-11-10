#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint8_t *pixels;
} Image;

int image_read_ppm(const char *path, Image *image, char *error, size_t error_size);
int image_write_ppm(const char *path, const Image *image, char *error, size_t error_size);
int image_allocate(Image *image, int width, int height, int channels);
void image_free(Image *image);
uint64_t image_checksum(const Image *image);
size_t image_difference_count(const Image *a, const Image *b, int *max_difference);

#endif
