#include "image.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool parse_positive_integer(const char *text, int &value)
{
    try {
        const std::string input(text);
        std::size_t position = 0;
        const long parsed = std::stol(input, &position);
        if (position != input.size() || parsed <= 0 ||
            parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

void print_usage(const char *program)
{
    std::cout << "Usage: " << program
              << " OUTPUT.ppm [--width N] [--height N]\n";
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *output_path = argv[1];
    int width = 1024;
    int height = 1024;
    for (int index = 2; index < argc; ++index) {
        if (std::string(argv[index]) == "--width" && index + 1 < argc) {
            if (!parse_positive_integer(argv[++index], width)) {
                std::cerr << "Error: --width must be a positive integer.\n";
                return EXIT_FAILURE;
            }
        } else if (std::string(argv[index]) == "--height" && index + 1 < argc) {
            if (!parse_positive_integer(argv[++index], height)) {
                std::cerr << "Error: --height must be a positive integer.\n";
                return EXIT_FAILURE;
            }
        } else {
            std::cerr << "Error: unknown or incomplete option: " << argv[index] << '\n';
            return EXIT_FAILURE;
        }
    }

    Image image{};
    if (!image_allocate(&image, width, height, 3)) {
        std::cerr << "Error: could not allocate the generated image.\n";
        return EXIT_FAILURE;
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
    const bool written = image_write_ppm(output_path, &image, error, sizeof(error));
    image_free(&image);
    if (!written) {
        std::cerr << "Error: " << error << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Generated " << output_path << " (" << width << 'x' << height << ")\n";
    return EXIT_SUCCESS;
}
