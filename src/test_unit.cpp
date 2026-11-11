#include "filter.h"
#include "stb_image_write.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <omp.h>

void allocation_audit_begin();
std::uint64_t allocation_audit_end();

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static Image optimized_blur(const Image &input, std::uint32_t iterations, bool parallel)
{
    const std::uint64_t stride = input.width * 3;
    std::vector<std::uint8_t> source((input.height + 2) * stride), target(source.size());
    BlurWorkspace workspace;
    workspace.horizontal.resize(source.size());
    std::copy(input.pixels.begin(), input.pixels.end(), source.data() + stride);
    for (std::uint32_t pass = 0; pass < iterations; ++pass) {
        blur_edges(source.data(), input.width, input.height, true, true);
        blur_pass(source.data(), target.data(), input.width, input.height, workspace, parallel);
        source.swap(target);
    }
    auto result = image_create(input.width, input.height);
    std::copy_n(source.data() + stride, result.pixels.size(), result.pixels.data());
    return result;
}

static void test_blur()
{
    auto small = image_create(2, 2);
    const std::uint8_t values[] = {0, 9, 18, 27};
    for (std::uint32_t index = 0; index < 4; ++index) {
        std::fill_n(small.pixels.data() + index * 3, 3, values[index]);
    }
    const auto result = optimized_blur(small, 1, false);
    const std::uint8_t expected[] = {9, 12, 15, 18};
    for (std::uint32_t index = 0; index < 4; ++index) {
        require(result.pixels[index * 3] == expected[index], "Hand-calculated blur failed");
    }
    std::mt19937 random(8128);
    for (const auto width : {1U, 2U, 7U, 37U}) {
        for (const auto height : {1U, 3U, 29U}) {
            auto image = image_create(width, height);
            for (auto &value : image.pixels) value = static_cast<std::uint8_t>(random() % 256);
            for (const auto passes : {1U, 2U, 5U}) {
                FilterOptions options;
                options.iterations = passes;
                const auto reference = filter_reference(image, options);
                for (const bool parallel : {false, true}) {
                    require(optimized_blur(image, passes, parallel).pixels == reference.pixels,
                            "Separable blur differs from independent stencil");
                }
            }
        }
    }
}

static void test_pixel_means()
{
    auto image = image_create(3, 2);
    const std::uint8_t values[] = {0, 20, 100, 40, 60, 200};
    for (std::uint32_t index = 0; index < 6; ++index) {
        std::fill_n(image.pixels.data() + index * 3, 3, values[index]);
    }
    FilterOptions options;
    options.filter = Filter::pixel;
    options.block_size = 2;
    options.dither = false;
    PixelWorkspace workspace;
    pixel_prepare(workspace, 3, 2, 0, 1, options);
    pixel_average(image_view(image), workspace, options, false);
    require(workspace.means == std::vector<std::uint8_t>({30, 30, 30, 150, 150, 150}),
            "Partial block average failed");
    options.colors = 2;
    auto output = filter_reference(image, options);
    for (std::uint32_t index = 0; index < 6; ++index) {
        require(output.pixels[index * 3] == (index % 3 == 2 ? 150 : 30), "Block expansion failed");
    }
    options.colors = 1;
    output = filter_reference(image, options);
    require(std::all_of(output.pixels.begin(), output.pixels.end(),
                        [](auto value) { return value == 90; }),
            "Palette population weighting failed");
}

static void test_palette()
{
    Palette palette;
    palette.count = 2;
    palette.red[0] = palette.green[0] = palette.blue[0] = 0;
    palette.red[1] = palette.green[1] = palette.blue[1] = 100;
    require(palette_nearest(palette, 50, 50, 50) == 0, "Nearest-color tie is unstable");
    constexpr int expected[16] = {-15, 1, -11, 5, 9, -7, 13, -3,
                                 -9, 7, -13, 3, 15, -1, 11, -5};
    for (std::uint32_t index = 0; index < 16; ++index) {
        require(dither_offset(index / 4, index % 4) == expected[index], "Bayer threshold failed");
        require(dither_offset(index / 4 + 4, index % 4 + 8) == expected[index],
                "Bayer period failed");
    }
    FilterOptions options;
    options.filter = Filter::pixel;
    options.block_size = 1;
    std::mt19937 random(42);
    auto image = image_create(37, 29);
    for (auto &value : image.pixels) value = static_cast<std::uint8_t>(random() % 256);
    for (const auto colors : {1U, 2U, 16U, 64U, 256U}) {
        options.colors = colors;
        const auto result = filter_reference(image, options);
        std::set<std::uint32_t> unique;
        for (std::uint64_t index = 0; index < result.pixels.size(); index += 3) {
            unique.insert(result.pixels[index] * 65536U + result.pixels[index + 1] * 256U +
                          result.pixels[index + 2]);
        }
        require(unique.size() <= colors, "Palette exceeds requested limit");
        require(result.pixels == filter_reference(image, options).pixels,
                "Nondeterministic palette");
    }
}

static void test_workspace_allocations()
{
    auto image = image_create(19, 23);
    FilterOptions options;
    options.filter = Filter::pixel;
    PixelWorkspace workspace;
    pixel_prepare(workspace, image.width, image.height, 0, 4, options);
    auto output = image_create(image.width, image.height);
    // Warm the external OpenMP runtime before auditing project-owned allocations.
    pixel_average(image_view(image), workspace, options, true);
    pixel_histogram(workspace, true);
    allocation_audit_begin();
    for (std::uint32_t repeat = 0; repeat < 3; ++repeat) {
        pixel_average(image_view(image), workspace, options, true);
        pixel_histogram(workspace, true);
        std::copy(workspace.histogram.begin(), workspace.histogram.end(),
                  workspace.global_histogram.begin());
        pixel_palette(workspace, options);
        pixel_expand(output.pixels.data(), output.width, output.height, workspace, options, true);
    }
    require(allocation_audit_end() == 0, "Pixel processing allocated memory");
    const std::uint64_t bytes = (image.height + 2) * image.width * 3;
    std::vector<std::uint8_t> source(bytes), target(bytes);
    BlurWorkspace blur;
    blur.horizontal.resize(bytes);
    allocation_audit_begin();
    blur_edges(source.data(), image.width, image.height, true, true);
    blur_pass(source.data(), target.data(), image.width, image.height, blur, true);
    require(allocation_audit_end() == 0, "Blur processing allocated memory");
}

static void test_codecs(const std::filesystem::path &directory)
{
    auto image = image_create(7, 5);
    for (std::uint64_t index = 0; index < image.pixels.size(); ++index) {
        image.pixels[index] = static_cast<std::uint8_t>(index * 17 % 256);
    }
    for (const auto *extension : {".png", ".ppm"}) {
        const auto path = (directory / (std::string("roundtrip") + extension)).string();
        image_write(path, image);
        require(image_read(path).pixels == image.pixels, "Lossless round trip failed");
    }
    const auto jpeg = (directory / "input.jpg").string();
    require(stbi_write_jpg(jpeg.c_str(), 7, 5, 3, image.pixels.data(), 90) != 0,
            "JPEG fixture failed");
    const auto decoded = image_read(jpeg);
    image_write((directory / "decoded.ppm").string(), decoded);
    const std::uint8_t rgba[] = {10, 20, 30, 0, 40, 50, 60, 128};
    const auto alpha = (directory / "alpha.png").string();
    require(stbi_write_png(alpha.c_str(), 2, 1, 4, rgba, 8) != 0, "RGBA fixture failed");
    require(image_read(alpha).pixels == std::vector<std::uint8_t>({10,20,30,40,50,60}),
            "Alpha must be discarded without compositing");
    const std::uint8_t gray[] = {0, 99, 255};
    const auto grayscale = (directory / "gray.png").string();
    require(stbi_write_png(grayscale.c_str(), 3, 1, 1, gray, 3) != 0, "Gray fixture failed");
    require(image_read(grayscale).pixels == std::vector<std::uint8_t>({0,0,0,99,99,99,255,255,255}),
            "Grayscale expansion failed");
}

int main(int argc, char **argv)
{
    try {
        omp_set_dynamic(0);
        omp_set_num_threads(4);
        const auto directory = std::filesystem::path(argc > 1 ? argv[1] : "tests/generated");
        std::filesystem::create_directories(directory);
        test_blur();
        test_pixel_means();
        test_palette();
        test_workspace_allocations();
        test_codecs(directory);
        bool rejected = false;
        try { image_create(1000000, 1000000); }
        catch (const std::exception &) { rejected = true; }
        require(rejected, "Oversized image was accepted");
        std::cout << "PASS: blur oracle, block means, palette, dithering, bounds, PNG/PPM/JPEG\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
