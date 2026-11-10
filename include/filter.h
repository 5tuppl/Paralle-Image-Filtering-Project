#ifndef FILTER_H
#define FILTER_H

#include "image.h"

/*
 * Apply a 3x3 box blur repeatedly. Pixels outside the image are clamped to
 * the nearest edge. Integer division makes all implementations bit-exact.
 */
int filter_shared(const Image *input, Image *output, int iterations, int use_openmp);

#endif
