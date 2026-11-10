#include "image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <sys/utsname.h>

namespace fs = std::filesystem;

namespace {

struct Result {
    std::string experiment;
    std::string mode;
    int workers = 0;
    int ranks = 0;
    int threads = 0;
    int width = 0;
    int height = 0;
    int iterations = 0;
    double seconds = 0.0;
    double megapixels_per_second = 0.0;
    double speedup = 0.0;
    double efficiency = 0.0;
    double normalized_time = 0.0;
    std::uint64_t checksum = 0;
    int repeats = 0;
};

struct Series {
    std::string label;
    std::string color;
    std::vector<std::pair<double, double>> points;
    bool dashed = false;
};

struct Options {
    bool quick = false;
    std::vector<int> workers;
    int repeats = 0;
};

std::string shell_quote(const std::string &text)
{
    std::string quoted = "'";
    for (const char character : text) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string run_and_capture(const std::string &command)
{
    const std::string command_with_errors = command + " 2>&1";
    FILE *pipe = popen(command_with_errors.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("could not start benchmark command");
    }

    std::array<char, 4096> buffer{};
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("benchmark command failed:\n" + command + "\n" + output);
    }
    return output;
}

std::string cpu_model()
{
    std::ifstream cpu_information("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpu_information, line)) {
        const std::string key = "model name";
        if (line.rfind(key, 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::size_t first_character = line.find_first_not_of(" \t", colon + 1);
                return line.substr(first_character);
            }
        }
    }
    return "unknown";
}

std::string mpi_version()
{
    const std::string output = run_and_capture("mpirun --version");
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("mpirun") != std::string::npos ||
            line.find("HYDRA") != std::string::npos) {
            return line;
        }
    }
    return "unknown";
}

std::pair<int, int> worker_configuration(const std::string &mode, int workers)
{
    if (mode == "openmp") return {1, workers};
    if (mode == "mpi") return {workers, 1};
    if (mode == "hybrid") {
        return workers >= 2 && workers % 2 == 0
                   ? std::pair<int, int>{workers / 2, 2}
                   : std::pair<int, int>{workers, 1};
    }
    if (mode == "serial") return {1, 1};
    throw std::runtime_error("unknown benchmark mode: " + mode);
}

Result parse_result(const std::string &output)
{
    const std::size_t marker = output.find("RESULT ");
    if (marker == std::string::npos) {
        throw std::runtime_error("program did not print a RESULT line:\n" + output);
    }
    const std::size_t line_end = output.find('\n', marker);
    const std::string line = output.substr(marker + 7, line_end - marker - 7);
    std::istringstream fields(line);
    std::map<std::string, std::string> values;
    std::string field;
    while (fields >> field) {
        const std::size_t equals = field.find('=');
        if (equals != std::string::npos) {
            values[field.substr(0, equals)] = field.substr(equals + 1);
        }
    }

    Result result;
    try {
        result.mode = values.at("mode");
        result.ranks = std::stoi(values.at("ranks"));
        result.threads = std::stoi(values.at("threads"));
        result.workers = std::stoi(values.at("workers"));
        result.width = std::stoi(values.at("width"));
        result.height = std::stoi(values.at("height"));
        result.iterations = std::stoi(values.at("iterations"));
        result.seconds = std::stod(values.at("seconds"));
        result.megapixels_per_second = std::stod(values.at("megapixels_per_second"));
        result.checksum = std::stoull(values.at("checksum"));
    } catch (const std::exception &) {
        throw std::runtime_error("could not parse RESULT line: " + line);
    }
    return result;
}

Result run_once(const fs::path &root, const std::string &mode,
                const fs::path &image, int iterations, int workers)
{
    const auto [ranks, threads] = worker_configuration(mode, workers);
    std::ostringstream program;
    program << "OMP_NUM_THREADS=" << threads
            << " OMP_DYNAMIC=FALSE OMP_PROC_BIND=spread OMP_PLACES=cores ";
    if (ranks > 1) {
        program << "mpirun --oversubscribe --bind-to none -np " << ranks << ' ';
    }
    program << shell_quote((root / "bin" / "parallel_filter").string())
            << " --input " << shell_quote(image.string())
            << " --mode " << mode
            << " --iterations " << iterations
            << " --threads " << threads;
    return parse_result(run_and_capture(program.str()));
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) return values[middle];
    return (values[middle - 1] + values[middle]) / 2.0;
}

Result measure(const fs::path &root, const std::string &mode,
               const fs::path &image, int iterations, int workers, int repeats)
{
    std::vector<Result> samples;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        samples.push_back(run_once(root, mode, image, iterations, workers));
    }
    for (const Result &sample : samples) {
        if (sample.checksum != samples.front().checksum) {
            throw std::runtime_error("non-deterministic checksum in " + mode);
        }
    }

    Result result = samples.front();
    std::vector<double> times;
    std::vector<double> throughputs;
    for (const Result &sample : samples) {
        times.push_back(sample.seconds);
        throughputs.push_back(sample.megapixels_per_second);
    }
    result.seconds = median(times);
    result.megapixels_per_second = median(throughputs);
    result.repeats = repeats;
    return result;
}

void generate_image(const fs::path &path, int width, int height)
{
    Image image{};
    if (!image_allocate(&image, width, height, 3)) {
        throw std::runtime_error("could not allocate generated benchmark image");
    }
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const int checker = ((row / 32) + (column / 32)) % 2 ? 80 : 0;
            const std::size_t index =
                (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(column)) * 3U;
            image.pixels[index] = static_cast<std::uint8_t>((3 * column + checker) % 256);
            image.pixels[index + 1] = static_cast<std::uint8_t>((2 * row + checker) % 256);
            image.pixels[index + 2] =
                static_cast<std::uint8_t>((column + row + checker) % 256);
        }
    }
    char error[256]{};
    const bool written = image_write_ppm(path.c_str(), &image, error, sizeof(error));
    image_free(&image);
    if (!written) throw std::runtime_error(error);
}

void add_metrics(std::vector<Result> &results)
{
    const auto serial = std::find_if(results.begin(), results.end(), [](const Result &result) {
        return result.experiment == "strong" && result.mode == "serial";
    });
    if (serial == results.end()) throw std::runtime_error("serial result is missing");

    for (Result &result : results) {
        if (result.experiment == "strong") {
            if (result.checksum != serial->checksum) {
                throw std::runtime_error("strong-scaling checksum mismatch in " + result.mode);
            }
            result.speedup = serial->seconds / result.seconds;
            result.efficiency = result.speedup / static_cast<double>(result.workers);
            result.normalized_time = result.seconds / serial->seconds;
        }
    }

    for (const std::string mode : {"openmp", "mpi", "hybrid"}) {
        const auto baseline = std::find_if(results.begin(), results.end(), [&](const Result &result) {
            return result.experiment == "weak" && result.mode == mode && result.workers == 1;
        });
        if (baseline == results.end()) throw std::runtime_error("weak baseline is missing");
        for (Result &result : results) {
            if (result.experiment == "weak" && result.mode == mode) {
                result.efficiency = baseline->seconds / result.seconds;
                result.normalized_time = result.seconds / baseline->seconds;
            }
        }
    }
}

void write_csv(const fs::path &path, const std::vector<Result> &results)
{
    std::ofstream file(path);
    if (!file) throw std::runtime_error("could not create " + path.string());
    file << "experiment,mode,workers,ranks,threads,width,height,iterations,seconds,"
            "megapixels_per_second,speedup,efficiency,normalized_time,checksum,repeats\n";
    file << std::setprecision(10);
    for (const Result &result : results) {
        file << result.experiment << ',' << result.mode << ',' << result.workers << ','
             << result.ranks << ',' << result.threads << ',' << result.width << ','
             << result.height << ',' << result.iterations << ',' << result.seconds << ','
             << result.megapixels_per_second << ',';
        if (result.experiment == "strong") file << result.speedup;
        file << ',' << result.efficiency << ',' << result.normalized_time << ','
             << result.checksum << ',' << result.repeats << '\n';
    }
}

std::string format_number(double value)
{
    std::ostringstream output;
    if (std::abs(value) >= 10.0) {
        output << std::fixed << std::setprecision(1) << value;
    } else {
        output << std::fixed << std::setprecision(2) << value;
    }
    return output.str();
}

void draw_svg(const fs::path &path, const std::string &title,
              const std::string &x_label, const std::string &y_label,
              const std::vector<int> &x_ticks, const std::vector<Series> &series)
{
    constexpr double width = 900.0;
    constexpr double height = 560.0;
    constexpr double left = 95.0;
    constexpr double right = 35.0;
    constexpr double top = 70.0;
    constexpr double bottom = 85.0;
    const double plot_width = width - left - right;
    const double plot_height = height - top - bottom;
    const double x_min = static_cast<double>(x_ticks.front());
    const double x_max = static_cast<double>(x_ticks.back());
    double y_max = 0.0;
    for (const Series &line : series) {
        for (const auto &[x, y] : line.points) {
            (void)x;
            y_max = std::max(y_max, y);
        }
    }
    y_max = y_max <= 0.0 ? 1.0 : y_max * 1.12;

    const auto x_position = [&](double value) {
        if (x_max == x_min) return left + plot_width / 2.0;
        return left + (value - x_min) / (x_max - x_min) * plot_width;
    };
    const auto y_position = [&](double value) {
        return top + plot_height - value / y_max * plot_height;
    };

    std::ofstream svg(path);
    if (!svg) throw std::runtime_error("could not create " + path.string());
    svg << std::fixed << std::setprecision(2);
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" height=\"560\" "
           "viewBox=\"0 0 900 560\">\n";
    svg << "<rect width=\"900\" height=\"560\" fill=\"white\"/>\n";
    svg << "<text x=\"450\" y=\"35\" text-anchor=\"middle\" "
           "font-family=\"sans-serif\" font-size=\"22\" font-weight=\"bold\">"
        << title << "</text>\n";

    for (int tick = 0; tick <= 5; ++tick) {
        const double value = y_max * static_cast<double>(tick) / 5.0;
        const double y = y_position(value);
        svg << "<line x1=\"" << left << "\" y1=\"" << y << "\" x2=\""
            << width - right << "\" y2=\"" << y
            << "\" stroke=\"#dddddd\" stroke-width=\"1\"/>\n";
        svg << "<text x=\"" << left - 10.0 << "\" y=\"" << y + 5.0
            << "\" text-anchor=\"end\" font-family=\"sans-serif\" font-size=\"13\">"
            << format_number(value) << "</text>\n";
    }
    for (const int tick : x_ticks) {
        const double x = x_position(static_cast<double>(tick));
        svg << "<line x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x
            << "\" y2=\"" << top + plot_height
            << "\" stroke=\"#eeeeee\" stroke-width=\"1\"/>\n";
        svg << "<text x=\"" << x << "\" y=\"" << top + plot_height + 25.0
            << "\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"13\">"
            << tick << "</text>\n";
    }
    svg << "<line x1=\"" << left << "\" y1=\"" << top + plot_height
        << "\" x2=\"" << width - right << "\" y2=\"" << top + plot_height
        << "\" stroke=\"black\" stroke-width=\"1.5\"/>\n";
    svg << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
        << "\" y2=\"" << top + plot_height
        << "\" stroke=\"black\" stroke-width=\"1.5\"/>\n";

    for (const Series &line : series) {
        svg << "<polyline fill=\"none\" stroke=\"" << line.color
            << "\" stroke-width=\"3\"";
        if (line.dashed) svg << " stroke-dasharray=\"8 6\"";
        svg << " points=\"";
        for (const auto &[x, y] : line.points) {
            svg << x_position(x) << ',' << y_position(y) << ' ';
        }
        svg << "\"/>\n";
        if (!line.dashed) {
            for (const auto &[x, y] : line.points) {
                svg << "<circle cx=\"" << x_position(x) << "\" cy=\"" << y_position(y)
                    << "\" r=\"4.5\" fill=\"" << line.color << "\"/>\n";
            }
        }
    }

    svg << "<text x=\"" << left + plot_width / 2.0 << "\" y=\"545\" "
           "text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"15\">"
        << x_label << "</text>\n";
    svg << "<text x=\"20\" y=\"" << top + plot_height / 2.0
        << "\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"15\" "
           "transform=\"rotate(-90 20 "
        << top + plot_height / 2.0 << ")\">" << y_label << "</text>\n";

    double legend_x = left + 15.0;
    const double legend_y = top + 20.0;
    for (const Series &line : series) {
        svg << "<line x1=\"" << legend_x << "\" y1=\"" << legend_y
            << "\" x2=\"" << legend_x + 25.0 << "\" y2=\"" << legend_y
            << "\" stroke=\"" << line.color << "\" stroke-width=\"3\"";
        if (line.dashed) svg << " stroke-dasharray=\"8 6\"";
        svg << "/>\n";
        svg << "<text x=\"" << legend_x + 31.0 << "\" y=\"" << legend_y + 5.0
            << "\" font-family=\"sans-serif\" font-size=\"13\">" << line.label
            << "</text>\n";
        legend_x += 115.0;
    }
    svg << "</svg>\n";
}

std::vector<std::pair<double, double>> select_points(
    const std::vector<Result> &results, const std::string &experiment,
    const std::string &mode, double Result::*value)
{
    std::vector<std::pair<double, double>> points;
    for (const Result &result : results) {
        if (result.experiment == experiment && result.mode == mode) {
            points.emplace_back(static_cast<double>(result.workers), result.*value);
        }
    }
    std::sort(points.begin(), points.end());
    return points;
}

void make_graphs(const fs::path &results_directory, const std::vector<Result> &results,
                 const std::vector<int> &workers)
{
    const std::array<std::string, 3> modes{"openmp", "mpi", "hybrid"};
    const std::array<std::string, 3> labels{"OpenMP", "MPI", "Hybrid"};
    const std::array<std::string, 3> colors{"#2E86AB", "#E07A5F", "#3D9970"};

    const auto build_series = [&](const std::string &experiment, double Result::*value) {
        std::vector<Series> lines;
        for (std::size_t index = 0; index < modes.size(); ++index) {
            lines.push_back({labels[index], colors[index],
                             select_points(results, experiment, modes[index], value), false});
        }
        return lines;
    };

    draw_svg(results_directory / "strong_scaling_time.svg",
             "Strong scaling: fixed image size", "Workers (MPI ranks x threads)",
             "Time (seconds)", workers, build_series("strong", &Result::seconds));

    std::vector<Series> speedup = build_series("strong", &Result::speedup);
    Series ideal{"Ideal", "#777777", {}, true};
    for (const int worker : workers) {
        ideal.points.emplace_back(worker, worker);
    }
    speedup.insert(speedup.begin(), ideal);
    draw_svg(results_directory / "speedup.svg", "Speedup relative to serial",
             "Workers", "Speedup S(p) = T(serial) / T(p)", workers, speedup);

    std::vector<Series> efficiency = build_series("strong", &Result::efficiency);
    Series ideal_efficiency{"Ideal", "#777777", {}, true};
    for (const int worker : workers) ideal_efficiency.points.emplace_back(worker, 1.0);
    efficiency.insert(efficiency.begin(), ideal_efficiency);
    draw_svg(results_directory / "efficiency.svg", "Parallel efficiency", "Workers",
             "Efficiency E(p) = S(p) / p", workers, efficiency);

    std::vector<Series> weak = build_series("weak", &Result::normalized_time);
    weak.insert(weak.begin(), ideal_efficiency);
    draw_svg(results_directory / "weak_scalability.svg", "Weak scalability",
             "Workers (image area grows proportionally)",
             "Normalized time T(p) / T(1)", workers, weak);
}

std::vector<int> parse_worker_list(const std::string &text)
{
    std::vector<int> workers;
    std::istringstream input(text);
    std::string value;
    while (std::getline(input, value, ',')) {
        const int worker = std::stoi(value);
        if (worker <= 0) throw std::runtime_error("workers must be positive");
        workers.push_back(worker);
    }
    std::sort(workers.begin(), workers.end());
    workers.erase(std::unique(workers.begin(), workers.end()), workers.end());
    if (workers.empty() || workers.front() != 1) {
        throw std::runtime_error("worker list must include 1");
    }
    return workers;
}

Options parse_options(int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--quick") {
            options.quick = true;
        } else if (argument == "--workers" && index + 1 < argc) {
            options.workers = parse_worker_list(argv[++index]);
        } else if (argument == "--repeats" && index + 1 < argc) {
            options.repeats = std::stoi(argv[++index]);
            if (options.repeats <= 0) throw std::runtime_error("repeats must be positive");
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--quick] [--workers 1,2,4,8] [--repeats N]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error("unknown or incomplete option: " + argument);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        Options options = parse_options(argc, argv);
        const fs::path executable = fs::absolute(argv[0]);
        const fs::path root = executable.parent_path().parent_path();
        const fs::path data_directory = root / "data";
        const fs::path results_directory = root / "results";
        fs::create_directories(data_directory);
        fs::create_directories(results_directory);

        const unsigned int detected = std::max(1U, std::thread::hardware_concurrency());
        if (options.workers.empty()) {
            for (const int worker : {1, 2, 4, 8}) {
                if (static_cast<unsigned int>(worker) <= detected) {
                    options.workers.push_back(worker);
                }
            }
        }
        if (options.repeats == 0) options.repeats = options.quick ? 1 : 3;
        if (static_cast<unsigned int>(options.workers.back()) > detected) {
            std::cerr << "Warning: requested workers exceed " << detected
                      << " detected hardware threads.\n";
        }

        const int strong_size = options.quick ? 512 : 2048;
        const int strong_iterations = options.quick ? 3 : 10;
        const int weak_base = options.quick ? 256 : 512;
        const int weak_iterations = options.quick ? 3 : 8;
        const fs::path strong_image =
            data_directory / ("generated_strong_" + std::to_string(strong_size) + ".ppm");
        std::cout << "Generating " << strong_size << 'x' << strong_size
                  << " strong-scaling image...\n";
        generate_image(strong_image, strong_size, strong_size);

        std::vector<Result> results;
        std::cout << "Strong scaling:\n";
        Result serial = measure(root, "serial", strong_image, strong_iterations,
                                1, options.repeats);
        serial.experiment = "strong";
        results.push_back(serial);
        std::cout << "  serial  workers=1  time=" << std::fixed << std::setprecision(6)
                  << serial.seconds << "s\n";

        for (const std::string mode : {"openmp", "mpi", "hybrid"}) {
            for (const int workers : options.workers) {
                Result result = measure(root, mode, strong_image, strong_iterations,
                                        workers, options.repeats);
                result.experiment = "strong";
                results.push_back(result);
                std::cout << "  " << std::left << std::setw(7) << mode << " workers="
                          << std::setw(2) << workers << " time=" << std::right
                          << std::fixed << std::setprecision(6) << result.seconds << "s\n";
            }
        }

        std::cout << "Weak scaling:\n";
        for (const int workers : options.workers) {
            const int dimension = std::max(
                16, static_cast<int>(std::lround(weak_base * std::sqrt(workers))));
            const fs::path image = data_directory /
                ("generated_weak_" + std::to_string(workers) + "_" +
                 std::to_string(dimension) + ".ppm");
            generate_image(image, dimension, dimension);
            for (const std::string mode : {"openmp", "mpi", "hybrid"}) {
                Result result = measure(root, mode, image, weak_iterations,
                                        workers, options.repeats);
                result.experiment = "weak";
                results.push_back(result);
                std::cout << "  " << std::left << std::setw(7) << mode << " workers="
                          << std::setw(2) << workers << " image=" << dimension << 'x'
                          << dimension << " time=" << std::right << std::fixed
                          << std::setprecision(6) << result.seconds << "s\n";
            }
        }

        add_metrics(results);
        write_csv(results_directory / "benchmark.csv", results);
        make_graphs(results_directory, results, options.workers);

        std::ofstream environment(results_directory / "environment.txt");
        struct utsname operating_system {};
        const bool have_uname = uname(&operating_system) == 0;
        environment << "cpu_model=" << cpu_model() << '\n'
                    << "operating_system="
                    << (have_uname ? std::string(operating_system.sysname) + " " +
                                         operating_system.release
                                   : "unknown")
                    << '\n'
                    << "mpi=" << mpi_version() << '\n'
                    << "compiler=" << __VERSION__ << '\n'
                    << "cxx_standard=17\n"
                    << "detected_hardware_threads=" << detected << '\n'
                    << "workers=";
        for (std::size_t index = 0; index < options.workers.size(); ++index) {
            if (index != 0) environment << ',';
            environment << options.workers[index];
        }
        environment << "\nrepeats=" << options.repeats << '\n';

        std::cout << "\nWrote results/benchmark.csv\n"
                     "Wrote results/strong_scaling_time.svg\n"
                     "Wrote results/speedup.svg\n"
                     "Wrote results/efficiency.svg\n"
                     "Wrote results/weak_scalability.svg\n"
                     "Wrote results/environment.txt\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Benchmark error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
