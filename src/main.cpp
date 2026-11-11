#include "runtime.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <omp.h>
#include <stdexcept>

static int program_run(int argc, char **argv, int provided);

int main(int argc, char **argv)
{
    int provided = 0;
    const int initialized = MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (initialized != MPI_SUCCESS) return 1;
    mpi_check(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN));
    const int status = program_run(argc, argv, provided);
    mpi_check(MPI_Finalize());
    return status;
}

static bool all_ok(bool local)
{
    int value = local ? 1 : 0;
    int result = 0;
    mpi_check(MPI_Allreduce(&value, &result, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD));
    return result != 0;
}

static bool initialize_threads(const Options &options)
{
    omp_set_dynamic(0);
    omp_set_num_threads(static_cast<int>(options.threads));
    int actual = 1;
    const bool parallel = options.mode == Mode::openmp || options.mode == Mode::hybrid;
    #pragma omp parallel if(parallel) shared(actual)
    {
        #pragma omp single
        actual = omp_get_num_threads();
    }
    const bool ok = actual == static_cast<int>(options.threads);
    if (!ok) std::cerr << "Thread setup error: runtime did not provide requested threads\n";
    return all_ok(ok);
}

static bool prepare_images(Execution &execution, const Options &options,
                           Image &input, Image &output, Image &reference)
{
    bool ok = true;
    std::uint32_t dimensions[2]{};
    try {
        if (execution.rank == 0) {
            input = image_read(options.input_path);
            output = image_create(input.width, input.height);
            if (options.verify) reference = filter_reference(input, options.filter);
            dimensions[0] = input.width;
            dimensions[1] = input.height;
        }
    } catch (const std::exception &error) {
        std::cerr << "Input error: " << error.what() << '\n';
        ok = false;
    }
    if (!all_ok(ok)) return false;
    mpi_check(MPI_Bcast(dimensions, 2, MPI_UINT32_T, 0, MPI_COMM_WORLD));
    try {
        execution_prepare(execution, options, dimensions[0], dimensions[1]);
    } catch (const std::exception &error) {
        std::cerr << "Rank " << execution.rank << " setup error: " << error.what() << '\n';
        ok = false;
    }
    return all_ok(ok);
}

static void print_sample(const Timings &timings, std::uint32_t sample, const Image &output)
{
    std::cout << std::setprecision(12) << "SAMPLE index=" << sample
        << " seconds=" << timings.total << " distribute_seconds=" << timings.distribute
        << " compute_seconds=" << timings.compute << " palette_seconds=" << timings.palette
        << " collect_seconds=" << timings.collect << " checksum=" << image_checksum(output) << '\n';
}

static void print_result(const Execution &execution, const Options &options,
                         const Image &output, std::vector<double> &samples)
{
    std::sort(samples.begin(), samples.end());
    const auto middle = samples.size() / 2;
    const double seconds = samples.size() % 2 ? samples[middle] :
        (samples[middle - 1] + samples[middle]) / 2;
    const double pixels = static_cast<double>(output.width) * output.height *
                          options.filter.iterations;
    std::cout << std::setprecision(12) << "RESULT filter=" << filter_name(options.filter.filter)
        << " mode=" << mode_name(options.mode) << " ranks=" << execution.ranks
        << " threads=" << options.threads << " workers="
        << static_cast<std::uint32_t>(execution.ranks) * options.threads
        << " width=" << output.width << " height=" << output.height
        << " iterations=" << options.filter.iterations
        << " block_size=" << options.filter.block_size
        << " colors=" << options.filter.colors << " dither="
        << (options.filter.dither ? "ordered" : "none") << " repeats=" << options.repeats
        << " warmup=" << options.warmup << " seconds=" << seconds
        << " min_seconds=" << samples.front() << " max_seconds=" << samples.back()
        << " megapixels_per_second=" << pixels / seconds / 1000000.0
        << " checksum=" << image_checksum(output) << " timing=prepared_to_output\n";
}

static bool run_samples(Execution &execution, const Options &options, const Image &input,
                        Image &output, const Image &reference, std::vector<double> &samples)
{
    bool ok = true;
    for (std::uint32_t index = 0; index < options.warmup + options.repeats; ++index) {
        const auto timings = execution_run(execution, input, output, options);
        if (index < options.warmup) continue;
        const std::uint32_t sample = index - options.warmup;
        if (execution.rank == 0) {
            samples[sample] = timings.total;
            if (options.verify && output.pixels != reference.pixels) ok = false;
            print_sample(timings, sample, output);
        }
    }
    if (execution.rank == 0 && options.verify) {
        std::cout << "VERIFY status=" << (ok ? "PASS" : "FAIL")
            << " samples=" << options.repeats << '\n';
    }
    return all_ok(ok);
}

static int program_run(int argc, char **argv, int provided)
{
    Execution execution;
    mpi_check(MPI_Comm_rank(MPI_COMM_WORLD, &execution.rank));
    mpi_check(MPI_Comm_size(MPI_COMM_WORLD, &execution.ranks));
    Options options;
    bool ok = true;
    try {
        options = parse_options(argc, argv);
        if (provided < MPI_THREAD_FUNNELED) {
            throw std::runtime_error("Insufficient MPI thread support");
        }
    } catch (const std::exception &error) {
        if (execution.rank == 0) std::cerr << "Options error: " << error.what() << '\n';
        ok = false;
    }
    if (!all_ok(ok)) return 1;
    if (options.help) {
        if (execution.rank == 0) print_usage();
        return 0;
    }
    if (!initialize_threads(options)) return 1;
    Image input, output, reference;
    if (!prepare_images(execution, options, input, output, reference)) return 1;
    std::vector<double> samples;
    try { samples.resize(options.repeats); }
    catch (const std::exception &) { ok = false; }
    if (!all_ok(ok)) return 1;
    if (!run_samples(execution, options, input, output, reference, samples)) return 1;
    try {
        if (execution.rank == 0) {
            if (!options.output_path.empty()) image_write(options.output_path, output);
            print_result(execution, options, output, samples);
        }
    } catch (const std::exception &error) {
        std::cerr << "Output error: " << error.what() << '\n';
        ok = false;
    }
    return all_ok(ok) ? 0 : 1;
}
