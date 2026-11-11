#!/usr/bin/env python3
"""Reproducible Open MPI/OpenMP experiments and traceable CSV/SVG output."""
import argparse
import csv
import datetime
import json
import hashlib
import os
import pathlib
import platform
import statistics
import sys
from common import fields, launcher, run
from graphs import make_graphs


def configuration(mode, workers):
    if mode == "serial":
        return 1, 1
    if mode == "openmp":
        return 1, workers
    if mode == "mpi":
        return workers, 1
    return (workers // 2, 2) if workers % 2 == 0 else (workers, 1)


def probe(command):
    try:
        return run(command, timeout=10).strip()
    except (RuntimeError, OSError):
        return "unavailable"


def environment(bin_directory, arguments):
    mac = sys.platform == "darwin"
    cpu = probe(["sysctl", "-n", "machdep.cpu.brand_string"]) if mac else "unknown"
    if sys.platform != "darwin" and pathlib.Path("/proc/cpuinfo").exists():
        for line in pathlib.Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    return {"utc_time": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "platform": platform.platform(), "cpu": cpu, "logical_cpus": os.cpu_count(),
            "physical_cpus": probe(["sysctl", "-n", "hw.physicalcpu"]) if mac else
                             probe(["lscpu"]),
            "cpu_topology": probe(["sysctl", "hw.perflevel0.physicalcpu",
                                   "hw.perflevel1.physicalcpu"])
                            if mac else probe(["lscpu", "-e"]),
            "mpi": probe(["mpirun", "--version"]), "compiler": probe(["mpic++", "--version"]),
            "build_commands": probe(["make", "-n", "-B", "all"]),
            "git_revision": probe(["git", "rev-parse", "HEAD"]),
            "git_status": probe(["git", "status", "--short"]),
            "binary": os.path.relpath(bin_directory / "parallel_filter"),
            "workers": arguments.workers, "repeats": arguments.repeats, "warmup": 1,
            "affinity": "MPI --bind-to none; OMP_PROC_BIND=false; OMP_DYNAMIC=false",
            "timing": "prepared input to output; includes copies and all filter communication",
            "stage_note": "Each stage is an independent rank maximum; do not sum them as total",
            "scope": "single machine; no oversubscription; OS scheduling; "
                     "background load uncontrolled",
            "binary_sha256": hashlib.sha256((bin_directory / "parallel_filter").read_bytes())
                            .hexdigest(),
            "kernel_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in map(pathlib.Path, ["src/filter.cpp", "src/pixel.cpp",
                                                            "src/execution.cpp"])}}


def measure(bin_directory, image, filter_name, mode, workers, repeats):
    ranks, threads = configuration(mode, workers)
    command = launcher(mode, ranks) + [bin_directory / "parallel_filter", "--input", image,
        "--filter", filter_name, "--mode", mode, "--threads", threads,
        "--iterations", 10 if filter_name == "blur" else 1,
        "--block-size", 8, "--colors", 64, "--dither", "ordered",
        "--warmup", 1, "--repeats", repeats, "--verify"]
    process_environment = dict(os.environ, OMP_DYNAMIC="false", OMP_PROC_BIND="false")
    output = run(command, timeout=600, environment=process_environment)
    if "VERIFY status=PASS" not in output:
        raise RuntimeError("Benchmark verification did not pass")
    samples = [fields(line) for line in output.splitlines() if line.startswith("SAMPLE ")]
    results = [fields(line) for line in output.splitlines() if line.startswith("RESULT ")]
    if len(results) != 1 or len(samples) != repeats:
        raise RuntimeError("Incomplete benchmark output")
    result = results[0]
    if any(sample["checksum"] != result["checksum"] for sample in samples):
        raise RuntimeError("Nondeterministic benchmark output")
    times = [float(sample["seconds"]) for sample in samples]
    result["stdev_seconds"] = statistics.stdev(times) if len(times) > 1 else 0
    return result, samples


def add_metrics(rows):
    for row in rows:
        same_filter = [candidate for candidate in rows if candidate["filter"] == row["filter"]]
        seconds = float(row["seconds"])
        workers = int(row["workers"])
        if row["experiment"] == "strong":
            baseline = next(candidate for candidate in same_filter
                            if candidate["experiment"] == "strong" and candidate["mode"] == "serial"
                            and candidate["width"] == row["width"])
            row["speedup"] = float(baseline["seconds"]) / seconds
            row["efficiency"] = row["speedup"] / workers
            row["normalized_time"] = seconds / float(baseline["seconds"])
        else:
            baseline = next(candidate for candidate in same_filter
                            if candidate["experiment"] == "weak"
                            and candidate["mode"] == row["mode"]
                            and int(candidate["workers"]) == 1)
            row["speedup"] = ""
            row["efficiency"] = float(baseline["seconds"]) / seconds
            row["normalized_time"] = seconds / float(baseline["seconds"])


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def record(bin_directory, image, filter_name, mode, workers, arguments,
           experiment, summaries, samples, checksums):
    with image.open("rb") as stream:
        stream.readline()
        _, height = map(int, stream.readline().split())
    units = height if filter_name == "blur" else (height + 7) // 8
    if configuration(mode, workers)[0] > units:
        print(f"SKIP {filter_name} {mode} workers={workers}: more ranks than owned bands")
        return
    result, measurements = measure(bin_directory, image, filter_name, mode,
                                   workers, arguments.repeats)
    key = (filter_name, result["width"], result["height"])
    if key in checksums and checksums[key] != result["checksum"]:
        raise RuntimeError("Cross-mode checksum mismatch")
    checksums[key] = result["checksum"]
    result = {"experiment": experiment, **result}
    summaries.append(result)
    for sample in measurements:
        samples.append({"experiment": experiment, "filter": filter_name, "mode": mode,
                        "workers": workers, "ranks": result["ranks"], "threads": result["threads"],
                        "width": result["width"], "height": result["height"],
                        "iterations": result["iterations"], "block_size": result["block_size"],
                        "colors": result["colors"], "dither": result["dither"], **sample})
    print(f"{experiment:6} {filter_name:5} {mode:6} workers={workers:2} "
          f"{result['width']}x{result['height']} "
          f"median={float(result['seconds']):.6f}s", flush=True)
    # Checkpoint raw samples even if a later configuration fails.
    write_csv(arguments.output / "samples.csv", samples)


def experiments(bin_directory, arguments):
    summaries, samples, checksums = [], [], {}
    data = pathlib.Path("data")
    data.mkdir(exist_ok=True)
    sizes = [128, 512] if arguments.quick else [128, 1024, 2048, 4096]
    for filter_name in ("blur", "pixel"):
        for size in sizes:
            image = data / f"generated_{size}.ppm"
            run([bin_directory / "generate_image", image, "--width", size, "--height", size])
            record(bin_directory, image, filter_name, "serial", 1, arguments,
                   "strong", summaries, samples, checksums)
            for workers in arguments.workers:
                for mode in ("openmp", "mpi", "hybrid"):
                    record(bin_directory, image, filter_name, mode, workers, arguments,
                           "strong", summaries, samples, checksums)
        base = 128 if arguments.quick else 512
        for workers in arguments.workers:
            # Fixed width and proportional height gives exactly constant blocks/pixels per worker.
            image = data / f"generated_weak_{base}_{workers}.ppm"
            run([bin_directory / "generate_image", image, "--width", base,
                 "--height", base * workers])
            for mode in ("openmp", "mpi", "hybrid"):
                record(bin_directory, image, filter_name, mode, workers, arguments,
                       "weak", summaries, samples, checksums)
    add_metrics(summaries)
    write_csv(arguments.output / "benchmark.csv", summaries)
    make_graphs(arguments.output, summaries)


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default="bin/release")
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--workers", help="Comma-separated worker counts; must include 1")
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()
    capacity = os.cpu_count() or 1
    default = [value for value in (1, 2, 4, 8) if value <= capacity]
    if not arguments.quick and capacity not in default:
        default.append(capacity)
    arguments.workers = (sorted(set(map(int, arguments.workers.split(","))))
                         if arguments.workers else default)
    if (1 not in arguments.workers or min(arguments.workers) < 1 or
            max(arguments.workers) > capacity):
        parser.error("Workers must include 1 and stay within detected logical CPUs")
    if not 1 <= arguments.repeats <= 10000:
        parser.error("Repeats must be 1..10000")
    if arguments.output is None:
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        directory_name = "quick-" + stamp if arguments.quick else stamp
        arguments.output = pathlib.Path("results") / directory_name
    arguments.output.mkdir(parents=True, exist_ok=False)
    return arguments


def main():
    arguments = parse_arguments()
    bin_directory = pathlib.Path(arguments.bin).resolve()
    metadata = environment(bin_directory, arguments)
    (arguments.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    experiments(bin_directory, arguments)
    print(f"Saved samples, summaries, metadata, and SVG graphs to {arguments.output}")


if __name__ == "__main__":
    main()
