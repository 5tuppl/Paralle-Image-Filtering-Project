#include "filter.h"
#include "image.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <mpi.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MODE_INVALID,
    MODE_SERIAL,
    MODE_OPENMP,
    MODE_MPI,
    MODE_HYBRID
} ExecutionMode;

typedef struct {
    const char *input_path;
    const char *output_path;
    ExecutionMode mode;
    int iterations;
    int threads;
    int verify;
    int show_help;
} Options;

static const char *mode_name(ExecutionMode mode)
{
    switch (mode) {
        case MODE_SERIAL: return "serial";
        case MODE_OPENMP: return "openmp";
        case MODE_MPI: return "mpi";
        case MODE_HYBRID: return "hybrid";
        default: return "invalid";
    }
}

static ExecutionMode parse_mode(const char *text)
{
    if (strcmp(text, "serial") == 0) return MODE_SERIAL;
    if (strcmp(text, "openmp") == 0) return MODE_OPENMP;
    if (strcmp(text, "mpi") == 0) return MODE_MPI;
    if (strcmp(text, "hybrid") == 0) return MODE_HYBRID;
    return MODE_INVALID;
}

static int parse_positive_integer(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' ||
        parsed <= 0 || parsed > INT_MAX) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int parse_options(int argc, char **argv, Options *options,
                         char *error, size_t error_size)
{
    options->input_path = NULL;
    options->output_path = NULL;
    options->mode = MODE_INVALID;
    options->iterations = 1;
    options->threads = 1;
    options->verify = 0;
    options->show_help = 0;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--input") == 0 && index + 1 < argc) {
            options->input_path = argv[++index];
        } else if (strcmp(argv[index], "--output") == 0 && index + 1 < argc) {
            options->output_path = argv[++index];
        } else if (strcmp(argv[index], "--mode") == 0 && index + 1 < argc) {
            options->mode = parse_mode(argv[++index]);
        } else if (strcmp(argv[index], "--iterations") == 0 && index + 1 < argc) {
            if (!parse_positive_integer(argv[++index], &options->iterations)) {
                snprintf(error, error_size, "--iterations must be a positive integer");
                return 0;
            }
        } else if (strcmp(argv[index], "--threads") == 0 && index + 1 < argc) {
            if (!parse_positive_integer(argv[++index], &options->threads)) {
                snprintf(error, error_size, "--threads must be a positive integer");
                return 0;
            }
        } else if (strcmp(argv[index], "--verify") == 0) {
            options->verify = 1;
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            options->show_help = 1;
        } else {
            snprintf(error, error_size, "unknown or incomplete option: %s", argv[index]);
            return 0;
        }
    }

    if (!options->show_help && options->input_path == NULL) {
        snprintf(error, error_size, "--input is required");
        return 0;
    }
    if (!options->show_help && options->mode == MODE_INVALID) {
        snprintf(error, error_size,
                 "--mode must be serial, openmp, mpi, or hybrid");
        return 0;
    }
    return 1;
}

static void print_usage(const char *program)
{
    printf("Parallel 3x3 image blur (PPM P6 input)\n\n");
    printf("Usage: %s --input IMAGE --mode MODE [options]\n\n", program);
    printf("Modes:\n");
    printf("  serial   One process, one thread (correctness baseline)\n");
    printf("  openmp   One process, multiple shared-memory threads\n");
    printf("  mpi      Multiple processes, one thread per process\n");
    printf("  hybrid   Multiple MPI processes, OpenMP threads in each\n\n");
    printf("Options:\n");
    printf("  --output FILE       Write the filtered PPM image (optional)\n");
    printf("  --iterations N      Apply the blur N times (default: 1)\n");
    printf("  --threads N         OpenMP threads per process (default: 1)\n");
    printf("  --verify            Compare result exactly with serial output\n");
    printf("  --help              Show this message\n");
}

static void build_partition(int height, int processes, int row_bytes,
                            int *rows, int *counts, int *displacements)
{
    int row_offset = 0;

    for (int rank = 0; rank < processes; ++rank) {
        rows[rank] = height / processes + (rank < height % processes ? 1 : 0);
        counts[rank] = rows[rank] * row_bytes;
        displacements[rank] = row_offset * row_bytes;
        row_offset += rows[rank];
    }
}

static void exchange_halo_rows(uint8_t *pixels, int local_rows, int row_bytes,
                               int rank, int process_count, MPI_Comm communicator)
{
    const int top_rank = rank == 0 ? MPI_PROC_NULL : rank - 1;
    const int bottom_rank = rank + 1 == process_count ? MPI_PROC_NULL : rank + 1;

    /* At global boundaries the missing row is replaced with the edge row. */
    if (top_rank == MPI_PROC_NULL) {
        memcpy(pixels, pixels + row_bytes, (size_t)row_bytes);
    }
    if (bottom_rank == MPI_PROC_NULL) {
        memcpy(pixels + (size_t)(local_rows + 1) * (size_t)row_bytes,
               pixels + (size_t)local_rows * (size_t)row_bytes,
               (size_t)row_bytes);
    }

    /* Send first owned row upward and receive the lower neighbor's first row. */
    MPI_Sendrecv(pixels + row_bytes, row_bytes, MPI_BYTE, top_rank, 100,
                 pixels + (size_t)(local_rows + 1) * (size_t)row_bytes,
                 row_bytes, MPI_BYTE, bottom_rank, 100,
                 communicator, MPI_STATUS_IGNORE);

    /* Send last owned row downward and receive the upper neighbor's last row. */
    MPI_Sendrecv(pixels + (size_t)local_rows * (size_t)row_bytes,
                 row_bytes, MPI_BYTE, bottom_rank, 101,
                 pixels, row_bytes, MPI_BYTE, top_rank, 101,
                 communicator, MPI_STATUS_IGNORE);
}

static void blur_local_rows(const uint8_t *source, uint8_t *destination,
                            int width, int local_rows, int use_openmp)
{
    #pragma omp parallel for schedule(static) if(use_openmp)
    for (int local_row = 1; local_row <= local_rows; ++local_row) {
        for (int column = 0; column < width; ++column) {
            for (int channel = 0; channel < 3; ++channel) {
                unsigned int sum = 0;
                for (int vertical = -1; vertical <= 1; ++vertical) {
                    for (int horizontal = -1; horizontal <= 1; ++horizontal) {
                        int actual_column = column + horizontal;
                        if (actual_column < 0) actual_column = 0;
                        if (actual_column >= width) actual_column = width - 1;
                        sum += source[((size_t)(local_row + vertical) *
                                       (size_t)width + (size_t)actual_column) * 3U +
                                      (size_t)channel];
                    }
                }
                destination[((size_t)local_row * (size_t)width +
                             (size_t)column) * 3U + (size_t)channel] =
                    (uint8_t)(sum / 9U);
            }
        }
    }
}

static int filter_distributed(const Image *input, Image *output, int iterations,
                              int use_openmp, MPI_Comm communicator,
                              double *maximum_elapsed)
{
    int rank;
    int process_count;
    int width = input->width;
    int height = input->height;
    int row_bytes = width * 3;
    int *rows = NULL;
    int *counts = NULL;
    int *displacements = NULL;
    int local_rows;
    uint8_t *current = NULL;
    uint8_t *next = NULL;
    int allocation_ok;
    int all_allocations_ok;
    double start;
    double local_elapsed;

    MPI_Comm_rank(communicator, &rank);
    MPI_Comm_size(communicator, &process_count);

    rows = (int *)malloc((size_t)process_count * sizeof(*rows));
    counts = (int *)malloc((size_t)process_count * sizeof(*counts));
    displacements = (int *)malloc((size_t)process_count * sizeof(*displacements));
    allocation_ok = rows != NULL && counts != NULL && displacements != NULL;
    MPI_Allreduce(&allocation_ok, &all_allocations_ok, 1, MPI_INT, MPI_MIN,
                  communicator);
    if (!all_allocations_ok) {
        free(rows);
        free(counts);
        free(displacements);
        return 0;
    }

    build_partition(height, process_count, row_bytes, rows, counts, displacements);
    local_rows = rows[rank];
    current = (uint8_t *)calloc((size_t)(local_rows + 2), (size_t)row_bytes);
    next = (uint8_t *)calloc((size_t)(local_rows + 2), (size_t)row_bytes);
    allocation_ok = current != NULL && next != NULL;
    if (rank == 0) {
        allocation_ok = allocation_ok && image_allocate(output, width, height, 3);
    }
    MPI_Allreduce(&allocation_ok, &all_allocations_ok, 1, MPI_INT, MPI_MIN,
                  communicator);
    if (!all_allocations_ok) {
        free(rows);
        free(counts);
        free(displacements);
        free(current);
        free(next);
        if (rank == 0) image_free(output);
        return 0;
    }

    MPI_Barrier(communicator);
    start = MPI_Wtime();

    /* Root distributes contiguous horizontal strips. Each rank owns its rows. */
    MPI_Scatterv(rank == 0 ? input->pixels : NULL, counts, displacements, MPI_BYTE,
                 current + row_bytes, counts[rank], MPI_BYTE, 0, communicator);

    for (int pass = 0; pass < iterations; ++pass) {
        uint8_t *temporary;
        exchange_halo_rows(current, local_rows, row_bytes,
                           rank, process_count, communicator);
        blur_local_rows(current, next, width, local_rows, use_openmp);
        temporary = current;
        current = next;
        next = temporary;
    }

    MPI_Gatherv(current + row_bytes, counts[rank], MPI_BYTE,
                rank == 0 ? output->pixels : NULL,
                counts, displacements, MPI_BYTE, 0, communicator);

    local_elapsed = MPI_Wtime() - start;
    MPI_Reduce(&local_elapsed, maximum_elapsed, 1, MPI_DOUBLE, MPI_MAX,
               0, communicator);

    free(rows);
    free(counts);
    free(displacements);
    free(current);
    free(next);
    return 1;
}

int main(int argc, char **argv)
{
    int provided_thread_level;
    int rank;
    int process_count;
    Options options;
    char error[256] = {0};
    int options_ok;
    Image input{};
    Image output{};
    int input_ok = 1;
    int dimensions[2] = {0, 0};
    double elapsed = 0.0;
    int run_ok = 1;
    int exit_code = EXIT_SUCCESS;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided_thread_level);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &process_count);

    options_ok = parse_options(argc, argv, &options, error, sizeof(error));
    if (!options_ok) {
        if (rank == 0) {
            fprintf(stderr, "Error: %s\n\n", error);
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (options.show_help) {
        if (rank == 0) print_usage(argv[0]);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }
    if ((options.mode == MODE_SERIAL || options.mode == MODE_OPENMP) &&
        process_count != 1) {
        if (rank == 0) {
            fprintf(stderr, "Error: %s mode must be started with exactly one MPI process.\n",
                    mode_name(options.mode));
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (options.mode == MODE_HYBRID && provided_thread_level < MPI_THREAD_FUNNELED) {
        if (rank == 0) {
            fprintf(stderr, "Error: the MPI library lacks required thread support.\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank == 0) {
        input_ok = image_read_ppm(options.input_path, &input, error, sizeof(error));
        if (input_ok) {
            dimensions[0] = input.width;
            dimensions[1] = input.height;
        }
    }
    MPI_Bcast(&input_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!input_ok) {
        if (rank == 0) fprintf(stderr, "Error: %s\n", error);
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    MPI_Bcast(dimensions, 2, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) {
        input.width = dimensions[0];
        input.height = dimensions[1];
        input.channels = 3;
        input.pixels = NULL;
    }

    if (process_count > input.height) {
        if (rank == 0) {
            fprintf(stderr,
                    "Error: process count (%d) cannot exceed image rows (%d).\n",
                    process_count, input.height);
        }
        image_free(&input);
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if ((size_t)input.width * (size_t)input.height * 3U > (size_t)INT_MAX) {
        if (rank == 0) {
            fprintf(stderr, "Error: this demonstration supports images below INT_MAX bytes.\n");
        }
        image_free(&input);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    omp_set_dynamic(0);
    omp_set_num_threads(options.threads);

    if (options.mode == MODE_SERIAL || options.mode == MODE_OPENMP) {
        const double start = omp_get_wtime();
        run_ok = filter_shared(&input, &output, options.iterations,
                               options.mode == MODE_OPENMP);
        elapsed = omp_get_wtime() - start;
    } else {
        run_ok = filter_distributed(&input, &output, options.iterations,
                                    options.mode == MODE_HYBRID,
                                    MPI_COMM_WORLD, &elapsed);
    }

    if (!run_ok) {
        if (rank == 0) fprintf(stderr, "Error: memory allocation failed during filtering.\n");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    if (rank == 0 && options.verify) {
        Image reference{};
        int maximum_difference = 0;
        size_t differences;

        if (!filter_shared(&input, &reference, options.iterations, 0)) {
            fprintf(stderr, "Error: could not allocate serial verification image.\n");
            exit_code = EXIT_FAILURE;
            goto cleanup;
        }
        differences = image_difference_count(&output, &reference, &maximum_difference);
        if (differences == 0) {
            printf("VERIFY status=PASS differing_bytes=0 max_difference=0\n");
        } else {
            printf("VERIFY status=FAIL differing_bytes=%zu max_difference=%d\n",
                   differences, maximum_difference);
            exit_code = EXIT_FAILURE;
        }
        image_free(&reference);
    }

    if (rank == 0 && options.output_path != NULL) {
        if (!image_write_ppm(options.output_path, &output, error, sizeof(error))) {
            fprintf(stderr, "Error: %s\n", error);
            exit_code = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (rank == 0) {
        const int threads_used =
            (options.mode == MODE_OPENMP || options.mode == MODE_HYBRID)
                ? options.threads : 1;
        const int workers = process_count * threads_used;
        const double megapixels = (double)input.width * (double)input.height *
                                  (double)options.iterations / 1000000.0;
        printf("RESULT mode=%s ranks=%d threads=%d workers=%d width=%d height=%d "
               "iterations=%d seconds=%.9f megapixels_per_second=%.3f "
               "checksum=%" PRIu64 "\n",
               mode_name(options.mode), process_count, threads_used, workers,
               input.width, input.height, options.iterations, elapsed,
               elapsed > 0.0 ? megapixels / elapsed : 0.0,
               image_checksum(&output));
    }

cleanup:
    image_free(&output);
    image_free(&input);
    MPI_Finalize();
    return exit_code;
}
