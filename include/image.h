#ifndef IMAGE_H
#define IMAGE_H
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint64_t image_bytes_max = 2147483647;
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};
struct ImageView {
    const std::uint8_t *pixels;
    std::uint32_t width;
    std::uint32_t height;
};
std::uint64_t image_byte_count(std::uint32_t width, std::uint32_t height);
Image image_create(std::uint32_t width, std::uint32_t height);
Image image_read(const std::string &path);
void image_write(const std::string &path, const Image &image);
std::uint64_t image_checksum(const Image &image);
ImageView image_view(const Image &image);
#endif
