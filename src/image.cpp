#include "image.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_token(FILE *file, char *token, size_t capacity)
{
    int character;
    size_t length = 0;

    do {
        character = fgetc(file);
        if (character == '#') {
            do {
                character = fgetc(file);
            } while (character != '\n' && character != EOF);
        }
    } while (isspace(character));

    if (character == EOF) {
        return 0;
    }

    do {
        if (length + 1 >= capacity) {
            return 0;
        }
        token[length++] = (char)character;
        character = fgetc(file);
    } while (character != EOF && !isspace(character) && character != '#');

    if (character == '#') {
        do {
            character = fgetc(file);
        } while (character != '\n' && character != EOF);
    }

    token[length] = '\0';
    return 1;
}

int image_allocate(Image *image, int width, int height, int channels)
{
    size_t byte_count;

    if (image == NULL || width <= 0 || height <= 0 || channels <= 0) {
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > SIZE_MAX / (size_t)channels) {
        return 0;
    }

    byte_count = (size_t)width * (size_t)height * (size_t)channels;
    image->pixels = (uint8_t *)malloc(byte_count);
    if (image->pixels == NULL) {
        return 0;
    }
    image->width = width;
    image->height = height;
    image->channels = channels;
    return 1;
}

void image_free(Image *image)
{
    if (image == NULL) {
        return;
    }
    free(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
    image->channels = 0;
}

int image_read_ppm(const char *path, Image *image, char *error, size_t error_size)
{
    FILE *file = NULL;
    char token[64];
    long width;
    long height;
    long maximum;
    size_t byte_count;
    char *end = NULL;
    int ok = 0;

    memset(image, 0, sizeof(*image));
    file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "cannot open '%s': %s", path, strerror(errno));
        return 0;
    }

    if (!read_token(file, token, sizeof(token)) || strcmp(token, "P6") != 0) {
        snprintf(error, error_size, "'%s' is not a binary PPM (P6) file", path);
        goto cleanup;
    }
    if (!read_token(file, token, sizeof(token))) {
        snprintf(error, error_size, "missing image width");
        goto cleanup;
    }
    width = strtol(token, &end, 10);
    if (*end != '\0' || width <= 0 || width > 1000000L) {
        snprintf(error, error_size, "invalid image width");
        goto cleanup;
    }
    if (!read_token(file, token, sizeof(token))) {
        snprintf(error, error_size, "missing image height");
        goto cleanup;
    }
    height = strtol(token, &end, 10);
    if (*end != '\0' || height <= 0 || height > 1000000L) {
        snprintf(error, error_size, "invalid image height");
        goto cleanup;
    }
    if (!read_token(file, token, sizeof(token))) {
        snprintf(error, error_size, "missing maximum color value");
        goto cleanup;
    }
    maximum = strtol(token, &end, 10);
    if (*end != '\0' || maximum != 255) {
        snprintf(error, error_size, "only 8-bit PPM files (maximum 255) are supported");
        goto cleanup;
    }

    if (!image_allocate(image, (int)width, (int)height, 3)) {
        snprintf(error, error_size, "image is too large or memory allocation failed");
        goto cleanup;
    }
    byte_count = (size_t)width * (size_t)height * 3U;
    if (fread(image->pixels, 1, byte_count, file) != byte_count) {
        snprintf(error, error_size, "pixel data in '%s' is incomplete", path);
        image_free(image);
        goto cleanup;
    }
    ok = 1;

cleanup:
    fclose(file);
    return ok;
}

int image_write_ppm(const char *path, const Image *image, char *error, size_t error_size)
{
    FILE *file;
    size_t byte_count;

    if (image == NULL || image->pixels == NULL || image->channels != 3) {
        snprintf(error, error_size, "invalid image supplied for writing");
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "cannot create '%s': %s", path, strerror(errno));
        return 0;
    }

    if (fprintf(file, "P6\n%d %d\n255\n", image->width, image->height) < 0) {
        snprintf(error, error_size, "failed to write PPM header to '%s'", path);
        fclose(file);
        return 0;
    }
    byte_count = (size_t)image->width * (size_t)image->height * 3U;
    if (fwrite(image->pixels, 1, byte_count, file) != byte_count) {
        snprintf(error, error_size, "failed to write all pixels to '%s'", path);
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        snprintf(error, error_size, "failed to close '%s' after writing", path);
        return 0;
    }
    return 1;
}

uint64_t image_checksum(const Image *image)
{
    /* FNV-1a: a small deterministic fingerprint, not a cryptographic hash. */
    uint64_t hash = UINT64_C(1469598103934665603);
    const size_t byte_count = (size_t)image->width * (size_t)image->height *
                              (size_t)image->channels;

    for (size_t index = 0; index < byte_count; ++index) {
        hash ^= (uint64_t)image->pixels[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

size_t image_difference_count(const Image *a, const Image *b, int *max_difference)
{
    size_t differences = 0;
    int maximum = 0;

    if (a->width != b->width || a->height != b->height ||
        a->channels != b->channels) {
        if (max_difference != NULL) {
            *max_difference = 255;
        }
        return SIZE_MAX;
    }

    const size_t byte_count = (size_t)a->width * (size_t)a->height *
                              (size_t)a->channels;
    for (size_t index = 0; index < byte_count; ++index) {
        int difference = abs((int)a->pixels[index] - (int)b->pixels[index]);
        if (difference != 0) {
            ++differences;
            if (difference > maximum) {
                maximum = difference;
            }
        }
    }
    if (max_difference != NULL) {
        *max_difference = maximum;
    }
    return differences;
}
