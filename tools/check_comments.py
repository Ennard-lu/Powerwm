#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
import sys
from pathlib import Path

CJK = re.compile(r"[\u4e00-\u9fff]")
CPP_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".cxx"}
LINE_COMMENT_SUFFIXES = {".sh", ".py"}
CONFIG_SUFFIXES = {".yml", ".yaml", ".cmake", ".txt", ".conf"}
CONFIG_NAMES = {"Dockerfile", ".clang-format", ".clang-tidy", ".gitignore"}
SKIP_DIRS = {"build", ".git", ".cache"}

errors: list[str] = []


def is_spdx(text: str) -> bool:
    stripped = text.strip()
    return stripped.startswith("// SPDX-License-Identifier:") or stripped.startswith(
        "# SPDX-License-Identifier:"
    )


def is_api_comment(text: str) -> bool:
    stripped = text.strip()
    return (
        stripped.startswith("///")
        or stripped.startswith("//!")
        or stripped.startswith("/**")
        or stripped.startswith("/*!")
    )


def check_comment(path: Path, line: int, text: str) -> None:
    if is_spdx(text):
        return
    if path.suffix in {".cpp", ".cc", ".cxx"}:
        errors.append(f"{path}:{line}: comments are not allowed in implementation files")
        return
    if not is_api_comment(text):
        errors.append(f"{path}:{line}: explanatory comment is forbidden")
        return
    if CJK.search(text):
        errors.append(f"{path}:{line}: API comment must be English")


def scan_cpp(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    index = 0
    line = 1
    start_line = 1
    state = "code"
    buffer: list[str] = []

    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if char == "/" and next_char == "/":
                state = "line"
                start_line = line
                buffer = ["//"]
                index += 2
                continue
            if char == "/" and next_char == "*":
                state = "block"
                start_line = line
                buffer = ["/*"]
                index += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "\n":
                line += 1
            index += 1
            continue

        if state == "string":
            if char == "\\":
                index += 2
                continue
            if char == '"':
                state = "code"
            elif char == "\n":
                line += 1
            index += 1
            continue

        if state == "char":
            if char == "\\":
                index += 2
                continue
            if char == "'":
                state = "code"
            elif char == "\n":
                line += 1
            index += 1
            continue

        if state == "line":
            if char == "\n":
                check_comment(path, start_line, "".join(buffer))
                state = "code"
                buffer = []
                line += 1
            else:
                buffer.append(char)
            index += 1
            continue

        if state == "block":
            if char == "*" and next_char == "/":
                buffer.append("*/")
                check_comment(path, start_line, "".join(buffer))
                state = "code"
                buffer = []
                index += 2
                continue
            if char == "\n":
                line += 1
            buffer.append(char)
            index += 1
            continue

    if state in {"line", "block"}:
        check_comment(path, start_line, "".join(buffer))


def scan_line_comments(path: Path) -> None:
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.lstrip()
        if not stripped.startswith("#"):
            continue
        if stripped.startswith("#!"):
            continue
        check_comment(path, number, stripped)


def iter_files() -> list[Path]:
    files: list[Path] = []
    for root in [Path("src"), Path("include"), Path("tests"), Path("tools")]:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and not any(part in SKIP_DIRS for part in path.parts):
                files.append(path)
    for path in [Path("CMakeLists.txt"), Path(".clang-format"), Path(".clang-tidy"), Path(".gitignore")]:
        if path.exists():
            files.append(path)
    for root in [Path("containers"), Path(".github")]:
        if root.exists():
            files.extend(path for path in root.rglob("*") if path.is_file())
    return files


def main() -> int:
    for path in iter_files():
        suffix = path.suffix
        if suffix in CPP_SUFFIXES:
            scan_cpp(path)
        elif suffix in LINE_COMMENT_SUFFIXES or suffix in CONFIG_SUFFIXES or path.name in CONFIG_NAMES:
            scan_line_comments(path)

    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"comment policy failed with {len(errors)} violation(s)", file=sys.stderr)
        return 1
    print("Comment policy passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
