#!/usr/bin/env python3
"""Independent integer oracles and byte comparisons, including collective failure tests."""
import argparse
import pathlib
import random
import tempfile
from common import launcher, run

BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def write_ppm(path, width, height, pixels):
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + bytes(pixels))


def read_ppm(path):
    return path.read_bytes().split(b"\n", 3)[3]


def blur_oracle(pixels, width, height, passes):
    current = pixels
    for _ in range(passes):
        target = []
        for y in range(height):
            for x in range(width):
                for channel in range(3):
                    values = [current[(min(height - 1, max(0, y + dy)) * width +
                                       min(width - 1, max(0, x + dx))) * 3 + channel]
                              for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
                    target.append(sum(values) // 9)
        current = target
    return bytes(current)


def palette_oracle(means, colors):
    histogram = {}
    for color in means:
        key = tuple(value // 8 for value in color)
        entry = histogram.setdefault(key, [0, 0, 0, 0])
        entry[0] += 1
        for channel in range(3):
            entry[channel + 1] += color[channel]
    boxes = [sorted(histogram)]
    while len(boxes) < min(colors, len(histogram)):
        choices = []
        for index, box in enumerate(boxes):
            if len(box) < 2:
                continue
            ranges = [max(key[c] for key in box) - min(key[c] for key in box) for c in range(3)]
            channel = ranges.index(max(ranges))
            population = sum(histogram[key][0] for key in box)
            choices.append((max(ranges), population, -index, channel))
        _, population, negative_index, channel = max(choices, key=lambda item: item[:3])
        index = -negative_index
        box = sorted(boxes[index], key=lambda key: (key[channel], key))
        cumulative, split = 0, 0
        while split < len(box) - 1:
            cumulative += histogram[box[split]][0]
            split += 1
            if cumulative >= (population + 1) // 2:
                break
        boxes[index] = box[:split]
        boxes.append(box[split:])
    return [tuple(sum(histogram[key][c + 1] for key in box) //
                  sum(histogram[key][0] for key in box) for c in range(3)) for box in boxes]


def pixel_oracle(pixels, width, height, block, colors, dither):
    means = []
    for y in range(0, height, block):
        for x in range(0, width, block):
            cells = [(yy * width + xx) * 3 for yy in range(y, min(y + block, height))
                     for xx in range(x, min(x + block, width))]
            means.append(tuple(sum(pixels[cell + c] for cell in cells) // len(cells)
                               for c in range(3)))
    palette = palette_oracle(means, colors)
    target = bytearray(width * height * 3)
    columns = (width + block - 1) // block
    for index, mean in enumerate(means):
        y, x = divmod(index, columns)
        offset = BAYER[y % 4][x % 4] * 2 - 15 if dither else 0
        adjusted = tuple(min(255, max(0, value + offset)) for value in mean)
        color = min(enumerate(palette),
                    key=lambda item: (sum((a - b) ** 2 for a, b in zip(adjusted, item[1])),
                                      item[0]))[1]
        for yy in range(y * block, min((y + 1) * block, height)):
            for xx in range(x * block, min((x + 1) * block, width)):
                target[(yy * width + xx) * 3:(yy * width + xx) * 3 + 3] = bytes(color)
    return bytes(target)


def command(binary, source, mode, ranks, threads, extra):
    return launcher(mode, ranks, testing=True) + [binary, "--input", source, "--mode", mode,
                                                 "--threads", threads] + extra


def check_case(binary, directory, width, height, pixels, filter_name, block=3,
               colors=16, dither=True):
    source, target = directory / "input.ppm", directory / "output.ppm"
    write_ppm(source, width, height, pixels)
    extra = ["--filter", filter_name, "--output", target, "--verify", "--warmup", 1, "--repeats", 2]
    if filter_name == "blur":
        extra += ["--iterations", 3]
        expected = blur_oracle(pixels, width, height, 3)
        units = height
    else:
        extra += ["--block-size", block, "--colors", colors,
                  "--dither", "ordered" if dither else "none"]
        expected = pixel_oracle(pixels, width, height, block, colors, dither)
        units = (height + block - 1) // block
    configurations = [("serial", 1, 1), ("openmp", 1, 4),
                      ("mpi", min(2, units), 1), ("hybrid", min(3, units), 2)]
    for mode, ranks, threads in configurations:
        output = run(command(binary, source, mode, ranks, threads, extra), timeout=30)
        if "VERIFY status=PASS" not in output or read_ppm(target) != expected:
            raise AssertionError(f"Oracle mismatch: {filter_name} {width}x{height} {mode}")
    return len(configurations)


def failure_tests(binary, directory):
    source = directory / "valid.ppm"
    write_ppm(source, 3, 2, [99] * 18)
    bad = directory / "bad.ppm"
    bad.write_bytes(b"P6\n3 2\n255\nshort")
    huge = directory / "huge.ppm"
    huge.write_bytes(b"P6\n1000000 1000000\n255\n")
    cases = [(["--colors", 0], source), (["--block-size", 0], source),
             (["--filter", "pixel", "--iterations", 2], source),
             (["--threads", "oops"], source), (["--filter", "unknown"], source),
             (["--repeats", 0], source), (["--warmup", -1], source),
             ([], bad), ([], huge), ([], directory / "missing.png"),
             (["--output", directory / "missing" / "out.png"], source),
             (["--filter", "pixel", "--block-size", 8], source)]
    for extra, path in cases:
        run(command(binary, path, "mpi", 2, 1, extra), timeout=15, expect_success=False)
    run(command(binary, source, "mpi", 3, 1, []), timeout=15, expect_success=False)
    return len(cases) + 1


def codec_tests(binary, directory, bin_directory):
    run([bin_directory / "test_unit", directory])
    for filter_name in ("blur", "pixel"):
        outputs = []
        for source in ("input.jpg", "decoded.ppm"):
            target = directory / "jpeg_result.ppm"
            run(command(binary, directory / source, "serial", 1, 1,
                        ["--filter", filter_name, "--output", target, "--verify"]))
            outputs.append(read_ppm(target))
        if outputs[0] != outputs[1]:
            raise AssertionError("JPEG filtering differs from decoded RGB input")
    special = directory / "special.ppm"
    special.write_bytes(b"P6\r\n# comment\r\n1 1\r\n255\r\n" + bytes([10, 13, 32]))
    run(command(binary, special, "serial", 1, 1,
                ["--output", directory / "special_out.ppm", "--verify"]))
    if read_ppm(directory / "special_out.ppm") != bytes([10, 13, 32]):
        raise AssertionError("PPM header consumed raster whitespace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default="bin/release")
    arguments = parser.parse_args()
    bin_directory = pathlib.Path(arguments.bin).resolve()
    binary = bin_directory / "parallel_filter"
    random_source = random.Random(1931)
    count = 0
    with tempfile.TemporaryDirectory(prefix="filter-test-") as temporary:
        directory = pathlib.Path(temporary)
        for width, height in ((1, 1), (1, 9), (9, 1), (2, 2), (7, 11), (37, 29)):
            pixels = [random_source.randrange(256) for _ in range(width * height * 3)]
            for filter_name in ("blur", "pixel"):
                count += check_case(binary, directory, width, height, pixels, filter_name)
        for kind in ("constant", "gradient", "checker"):
            width, height = 17, 13
            pixels = [77 if kind == "constant" else (index * 13 % 256 if kind == "gradient"
                       else ((index // 3 % width + index // (width * 3)) % 2) * 255)
                      for index in range(width * height * 3)]
            for filter_name in ("blur", "pixel"):
                count += check_case(binary, directory, width, height, pixels, filter_name)
        pixels = [random_source.randrange(256) for _ in range(11 * 17 * 3)]
        pixel_cases = ((1, 1, False), (2, 2, True), (4, 256, False), (100, 64, True))
        for block, colors, dither in pixel_cases:
            count += check_case(binary, directory, 11, 17, pixels, "pixel", block, colors, dither)
        count += failure_tests(binary, directory)
        codec_tests(binary, directory, bin_directory)
    print(f"PASS: {count} execution/failure cases plus codec checks; independent Python oracles")


if __name__ == "__main__":
    main()
