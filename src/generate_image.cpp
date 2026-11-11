#include "image.h"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>

static std::uint32_t dimension(const std::string &text)
{
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        throw std::runtime_error("Invalid dimension");
    }
    return value;
}

int main(int argc, char **argv)
{
    try {
        if (argc < 2) {
            throw std::runtime_error("Usage: generate_image OUTPUT [--width N --height N]");
        }
        std::uint32_t width = 1024, height = 1024;
        for (int index = 2; index < argc; index += 2) {
            if (index + 1 == argc) throw std::runtime_error("Missing dimension");
            const std::string key(argv[index]);
            if (key == "--width") width = dimension(argv[index + 1]);
            else if (key == "--height") height = dimension(argv[index + 1]);
            else throw std::runtime_error("Unknown option");
        }
        auto image = image_create(width, height);
        for (std::uint32_t row = 0; row < height; ++row) {
            for (std::uint32_t column = 0; column < width; ++column) {
                const auto checker = ((row / 32) + (column / 32)) % 2 ? 80U : 0U;
                const auto index = (static_cast<std::uint64_t>(row) * width + column) * 3;
                image.pixels[index] = static_cast<std::uint8_t>((3 * column + checker) % 256);
                image.pixels[index + 1] = static_cast<std::uint8_t>((2 * row + checker) % 256);
                image.pixels[index + 2] = static_cast<std::uint8_t>((column + row + checker) % 256);
            }
        }
        image_write(argv[1], image);
        std::cout << "Generated " << argv[1] << " (" << width << 'x' << height << ")\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
