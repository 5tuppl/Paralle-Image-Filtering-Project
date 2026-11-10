#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

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

void run(const std::string &command)
{
    const int status = std::system(command.c_str());
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("command failed: " + command);
    }
}

bool files_are_equal(const fs::path &left_path, const fs::path &right_path)
{
    if (fs::file_size(left_path) != fs::file_size(right_path)) return false;

    std::ifstream left(left_path, std::ios::binary);
    std::ifstream right(right_path, std::ios::binary);
    std::array<char, 4096> left_buffer{};
    std::array<char, 4096> right_buffer{};
    while (left && right) {
        left.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        right.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        if (left.gcount() != right.gcount() ||
            !std::equal(left_buffer.begin(), left_buffer.begin() + left.gcount(),
                        right_buffer.begin())) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        const fs::path executable = fs::absolute(argc > 0 ? argv[0] : "bin/test_correctness");
        const fs::path root = executable.parent_path().parent_path();
        const fs::path tests = root / "tests";
        const fs::path filter = root / "bin" / "parallel_filter";
        const fs::path generator = root / "bin" / "generate_image";
        fs::create_directories(tests);

        const fs::path input = tests / "input_small.ppm";
        const fs::path serial = tests / "output_serial.ppm";
        const fs::path openmp = tests / "output_openmp.ppm";
        const fs::path mpi = tests / "output_mpi.ppm";
        const fs::path hybrid = tests / "output_hybrid.ppm";

        run(shell_quote(generator.string()) + " " + shell_quote(input.string()) +
            " --width 37 --height 29");
        run(shell_quote(filter.string()) + " --input " + shell_quote(input.string()) +
            " --output " + shell_quote(serial.string()) +
            " --mode serial --iterations 3 --verify");
        run("OMP_NUM_THREADS=4 " + shell_quote(filter.string()) + " --input " +
            shell_quote(input.string()) + " --output " + shell_quote(openmp.string()) +
            " --mode openmp --threads 4 --iterations 3 --verify");
        run("mpirun --oversubscribe --bind-to none -np 2 " + shell_quote(filter.string()) +
            " --input " + shell_quote(input.string()) + " --output " +
            shell_quote(mpi.string()) + " --mode mpi --iterations 3 --verify");
        run("OMP_NUM_THREADS=2 mpirun --oversubscribe --bind-to none -np 2 " +
            shell_quote(filter.string()) + " --input " + shell_quote(input.string()) +
            " --output " + shell_quote(hybrid.string()) +
            " --mode hybrid --threads 2 --iterations 3 --verify");

        if (!files_are_equal(serial, openmp) || !files_are_equal(serial, mpi) ||
            !files_are_equal(serial, hybrid)) {
            throw std::runtime_error("the four output images are not byte-identical");
        }
        std::cout << "All four implementations produced byte-identical images.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Correctness test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
