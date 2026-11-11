#include "runtime.h"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

const char *mode_name(Mode mode)
{
    switch (mode) {
        case Mode::serial: return "serial";
        case Mode::openmp: return "openmp";
        case Mode::mpi: return "mpi";
        case Mode::hybrid: return "hybrid";
    }
    throw std::runtime_error("Invalid execution mode");
}

const char *filter_name(Filter filter)
{
    return filter == Filter::blur ? "blur" : "pixel";
}

static std::uint32_t parse_number(std::string_view text, std::uint32_t maximum,
                                  bool zero = false)
{
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        (!zero && value == 0) || value > maximum) throw std::runtime_error("Invalid number");
    return value;
}

static Mode parse_mode(std::string_view text)
{
    if (text == "serial") return Mode::serial;
    if (text == "openmp") return Mode::openmp;
    if (text == "mpi") return Mode::mpi;
    if (text == "hybrid") return Mode::hybrid;
    throw std::runtime_error("Mode must be serial, openmp, mpi, or hybrid");
}

static void parse_value(Options &options, std::string_view key, std::string_view value)
{
    if (key == "--input") options.input_path = value;
    else if (key == "--output") options.output_path = value;
    else if (key == "--mode") options.mode = parse_mode(value);
    else if (key == "--threads") options.threads = parse_number(value, 1024);
    else if (key == "--iterations") options.filter.iterations = parse_number(value, 10000);
    else if (key == "--block-size") options.filter.block_size = parse_number(value, 1000000);
    else if (key == "--colors") options.filter.colors = parse_number(value, palette_capacity);
    else if (key == "--repeats") options.repeats = parse_number(value, 10000);
    else if (key == "--warmup") options.warmup = parse_number(value, 1000, true);
    else if (key == "--filter") {
        if (value != "blur" && value != "pixel") throw std::runtime_error("Unknown filter");
        options.filter.filter = value == "blur" ? Filter::blur : Filter::pixel;
    } else if (key == "--dither") {
        if (value != "none" && value != "ordered") throw std::runtime_error("Unknown dither");
        options.filter.dither = value == "ordered";
    } else throw std::runtime_error("Unknown option: " + std::string(key));
}

Options parse_options(int argc, char **argv)
{
    Options options;
    if (argc > 64) throw std::runtime_error("Too many arguments");
    for (int index = 1; index < argc; ++index) {
        const std::string_view key(argv[index]);
        if (key == "--help" || key == "-h") options.help = true;
        else if (key == "--verify") options.verify = true;
        else {
            if (index + 1 == argc) throw std::runtime_error("Missing value: " + std::string(key));
            parse_value(options, key, argv[++index]);
        }
    }
    if (options.help) return options;
    if (options.input_path.empty()) throw std::runtime_error("--input is required");
    if (options.filter.filter == Filter::pixel && options.filter.iterations != 1) {
        throw std::runtime_error(
            "Pixel filtering requires --iterations 1; use --repeats to measure");
    }
    if (options.mode == Mode::serial || options.mode == Mode::mpi) options.threads = 1;
    return options;
}

void print_usage()
{
    std::cout << "Usage: parallel_filter --input FILE --mode serial|openmp|mpi|hybrid [options]\n"
        "  --filter blur|pixel        Default: blur\n"
        "  --output FILE.png|FILE.ppm Optional output\n"
        "  --iterations N             Successive blur passes (default 1)\n"
        "  --block-size N             Pixel block edge (default 8)\n"
        "  --colors N                 Palette limit 1..256 (default 64)\n"
        "  --dither ordered|none      Default: ordered\n"
        "  --threads N                Threads per OpenMP/hybrid rank (default 1)\n"
        "  --warmup N --repeats N     Untimed/measured runs (defaults 0/1)\n"
        "  --verify                   Compare each measured output with serial reference\n";
}
