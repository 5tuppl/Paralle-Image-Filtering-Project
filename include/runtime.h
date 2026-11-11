#ifndef RUNTIME_H
#define RUNTIME_H
#include "filter.h"
#include <mpi.h>
#include <string>

struct Options {
    std::string input_path;
    std::string output_path;
    Mode mode = Mode::serial;
    FilterOptions filter;
    std::uint32_t threads = 1;
    std::uint32_t repeats = 1;
    std::uint32_t warmup = 0;
    bool verify = false;
    bool help = false;
};
struct Timings {
    double total = 0;
    double distribute = 0;
    double compute = 0;
    double palette = 0;
    double collect = 0;
};
struct Execution {
    int rank = 0;
    int ranks = 1;
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::uint32_t row_offset = 0;
    bool distributed = false;
    bool parallel = false;
    std::vector<int> counts;
    std::vector<int> displacements;
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> target;
    BlurWorkspace blur;
    PixelWorkspace pixel;
};
Options parse_options(int argc, char **argv);
void print_usage();
const char *mode_name(Mode mode);
const char *filter_name(Filter filter);
void mpi_check(int status);
void execution_prepare(Execution &execution, const Options &options,
                       std::uint32_t width, std::uint32_t height);
Timings execution_run(Execution &execution, const Image &input, Image &output,
                      const Options &options);
#endif
