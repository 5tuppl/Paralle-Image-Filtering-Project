"""Bounded process execution shared by tests and measurements (Python standard library only)."""
import os
import signal
import subprocess


def run(command, timeout=120, environment=None, expect_success=True):
    process = subprocess.Popen(
        [str(value) for value in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, env=environment, start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise RuntimeError(f"Timed out after {timeout}s: {command}") from None
    sanitizer_failure = any(marker in output for marker in (
        "ERROR: AddressSanitizer", "runtime error:", "UndefinedBehaviorSanitizer"))
    expected_code = 0 if expect_success else 1
    if process.returncode != expected_code or sanitizer_failure:
        raise RuntimeError(f"Unexpected exit {process.returncode}: {command}\n{output}")
    return output


def fields(line):
    return dict(item.split("=", 1) for item in line.split()[1:])


def launcher(mode, ranks, testing=False):
    if mode not in ("mpi", "hybrid"):
        return []
    # Open MPI flags are intentional: this is the validated MPI implementation.
    command = ["mpirun", "--bind-to", "none"]
    if testing:
        command.append("--oversubscribe")
    return command + ["-np", str(ranks)]
