#include "image.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include "stb_image.h"
#include "stb_image_write.h"

std::uint64_t image_byte_count(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) throw std::runtime_error("Image dimensions must be positive");
    const auto count = static_cast<std::uint64_t>(width) * height;
    if (count > image_bytes_max / 3) throw std::runtime_error("Image exceeds INT_MAX RGB bytes");
    assert(count > 0);
    assert(count * 3 <= image_bytes_max);
    return count * 3;
}

Image image_create(std::uint32_t width, std::uint32_t height)
{
    Image result;
    result.pixels.resize(image_byte_count(width, height));
    result.width = width;
    result.height = height;
    assert(result.width > 0);
    assert(result.pixels.size() == image_byte_count(width, height));
    return result;
}

static std::string image_extension(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : path.substr(dot);
    for (char &character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

static std::string ppm_token(std::istream &stream)
{
    std::string token;
    for (std::uint32_t read = 0; read < 65536; ++read) {
        const int character = stream.peek();
        if (character == EOF) throw std::runtime_error("Incomplete PPM header");
        if (character == '#') {
            for (std::uint32_t length = 0; length < 65536; ++length) {
                const int next = stream.get();
                if (next == '\n' || next == EOF) break;
                if (length == 65535) throw std::runtime_error("PPM comment too long");
            }
        } else if (std::isspace(character)) {
            stream.get();
        } else {
            break;
        }
        if (read == 65535) throw std::runtime_error("PPM header too long");
    }
    for (std::uint32_t length = 0; length < 64; ++length) {
        const int character = stream.peek();
        if (character == EOF || std::isspace(character) || character == '#') return token;
        token.push_back(static_cast<char>(stream.get()));
    }
    throw std::runtime_error("PPM token too long");
}

static std::uint32_t ppm_number(const std::string &token)
{
    if (token.empty()) throw std::runtime_error("Missing PPM dimension");
    std::uint64_t value = 0;
    for (const char character : token) {
        if (character < '0' || character > '9') throw std::runtime_error("Invalid PPM number");
        value = value * 10 + static_cast<std::uint32_t>(character - '0');
        if (value > image_bytes_max) throw std::runtime_error("PPM number too large");
    }
    return static_cast<std::uint32_t>(value);
}

static Image image_read_ppm(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open input: " + path);
    if (ppm_token(stream) != "P6") throw std::runtime_error("Expected binary P6 PPM");
    const auto width = ppm_number(ppm_token(stream));
    const auto height = ppm_number(ppm_token(stream));
    if (ppm_token(stream) != "255") throw std::runtime_error("PPM requires maximum 255");
    const int separator = stream.get();
    if (separator == EOF || !std::isspace(separator)) {
        throw std::runtime_error("Missing PPM raster separator");
    }
    // Treat CRLF as one header delimiter; do not skip whitespace-valued pixel bytes.
    if (separator == '\r' && stream.peek() == '\n') stream.get();
    auto image = image_create(width, height);
    stream.read(reinterpret_cast<char *>(image.pixels.data()),
                static_cast<std::streamsize>(image.pixels.size()));
    if (!stream) throw std::runtime_error("Incomplete PPM raster");
    return image;
}

Image image_read(const std::string &path)
{
    const auto extension = image_extension(path);
    if (extension == ".ppm") return image_read_ppm(path);
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
        throw std::runtime_error("Input must be PNG, JPEG, or PPM");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(path.c_str(), &width, &height, &channels)) {
        throw std::runtime_error("Cannot inspect input: " + path);
    }
    auto image = image_create(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height));
    using Decoded = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    Decoded decoded(stbi_load(path.c_str(), &width, &height, &channels, 3), stbi_image_free);
    if (!decoded) throw std::runtime_error("Cannot decode input: " + path);
    if (static_cast<std::uint32_t>(width) != image.width ||
        static_cast<std::uint32_t>(height) != image.height) {
        throw std::runtime_error("Image dimensions changed during decoding");
    }
    std::copy_n(decoded.get(), image.pixels.size(), image.pixels.data());
    return image;
}

void image_write(const std::string &path, const Image &image)
{
    assert(image.width > 0);
    assert(image.pixels.size() == image_byte_count(image.width, image.height));
    const auto extension = image_extension(path);
    if (extension == ".png") {
        std::ofstream stream(path, std::ios::binary);
        const auto write_bytes = [](void *context, void *bytes, int count) {
            auto &target = *static_cast<std::ofstream *>(context);
            target.write(static_cast<const char *>(bytes), count);
        };
        const int encoded = stbi_write_png_to_func(write_bytes, &stream,
            static_cast<int>(image.width), static_cast<int>(image.height), 3,
            image.pixels.data(), static_cast<int>(image.width * 3));
        stream.close();
        if (!encoded || !stream) throw std::runtime_error("Cannot write PNG: " + path);
    } else if (extension == ".ppm") {
        std::ofstream stream(path, std::ios::binary);
        stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        stream.write(reinterpret_cast<const char *>(image.pixels.data()),
                     static_cast<std::streamsize>(image.pixels.size()));
        stream.close();
        if (!stream) throw std::runtime_error("Cannot write PPM: " + path);
    } else {
        throw std::runtime_error("Output must be PNG or PPM");
    }
}

std::uint64_t image_checksum(const Image &image)
{
    assert(image.width > 0);
    assert(image.pixels.size() == image_byte_count(image.width, image.height));
    // Preserve the original project's fingerprint for baseline comparisons.
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (const std::uint8_t value : image.pixels) hash = (hash ^ value) * UINT64_C(1099511628211);
    return hash;
}

ImageView image_view(const Image &image)
{
    assert(image.width > 0);
    assert(image.pixels.size() == image_byte_count(image.width, image.height));
    return {image.pixels.data(), image.width, image.height};
}
