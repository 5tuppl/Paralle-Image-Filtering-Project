#!/usr/bin/env python3
"""Small checks for project-owned source; third-party and original Zig reference are excluded."""
import ast
import pathlib
import re


def main():
    paths = [*pathlib.Path("src").glob("*.cpp"), *pathlib.Path("include").glob("*.h"),
             *pathlib.Path("scripts").glob("*.py")]
    failures = []
    for path in paths:
        source = path.read_text()
        for number, line in enumerate(source.splitlines(), 1):
            if len(line) > 100:
                failures.append(f"{path}:{number}: line exceeds 100 columns")
        if path.suffix == ".py":
            for node in ast.walk(ast.parse(source)):
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    if node.end_lineno - node.lineno + 1 > 70:
                        failures.append(f"{path}:{node.lineno}: function exceeds 70 lines")
        if path.suffix == ".cpp":
            # Functions in this project use a standalone opening brace at column zero.
            stripped = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', source)
            stripped = re.sub(r'//[^\n]*|/\*.*?\*/', '', stripped, flags=re.DOTALL)
            start, depth = None, 0
            for number, line in enumerate(stripped.splitlines(), 1):
                if line == "{" and depth == 0:
                    start = number
                depth += line.count("{") - line.count("}")
                if start is not None and depth == 0:
                    if number - start + 1 > 70:
                        failures.append(f"{path}:{start}: function body exceeds 70 lines")
                    start = None
    if failures:
        raise SystemExit("\n".join(failures))
    print("PASS: project source line lengths and function bounds")


if __name__ == "__main__":
    main()
