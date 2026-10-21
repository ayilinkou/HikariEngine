"""One file list and clang-format invocation for CLI, CI and CMake targets."""

import re
import shutil
import subprocess
import sys

from .common import ROOT, ToolError, run


def find_formatter(root=ROOT):
    expected = (root / ".clang-format-version").read_text(encoding="utf-8").strip()
    executable = shutil.which("clang-format")
    if not executable:
        raise ToolError("clang-format not found on PATH.")
    version = subprocess.run([executable, "--version"], capture_output=True, text=True, check=False)
    match = re.search(r"[0-9]+\.[0-9]+\.[0-9]+", version.stdout)
    found = match.group() if match else "unknown"
    if version.returncode or found != expected:
        print(f"warning: clang-format {found} found, but this project is formatted with {expected}.\n"
              f"Install the pinned version with: pip install clang-format=={expected}", file=sys.stderr)
    return executable, found


def sources(root=ROOT):
    # Preserve the existing coverage. Expanding this to apps/ or tests/ would
    # reformat unrelated files and belongs in a separate policy change.
    directories = [root / "src"]
    for module in sorted((root / "engine").glob("*")):
        directories += [module / "include", module / "src"]
    return sorted({path for directory in directories for path in directory.rglob("*")
                   if path.is_file() and path.suffix in (".h", ".hpp", ".cpp")})


def format_tree(*, fix=False, root=ROOT):
    executable, version = find_formatter(root)
    files = sources(root)
    if not files:
        raise ToolError(f"no sources found under {root}.")
    # Keep well below Windows' command-line limit, even with a long checkout
    # path. clang-format reads each file's style normally in either mode.
    options = ["-i"] if fix else ["--dry-run", "--Werror"]
    status = 0
    batch = []
    length = len(str(executable)) + sum(len(arg) + 3 for arg in options)
    for path in files:
        cost = len(str(path)) + 3
        if batch and length + cost > 24000:
            status = run([executable, *options, *batch], cwd=root) or status
            batch = []
            length = len(str(executable)) + sum(len(arg) + 3 for arg in options)
        batch.append(path)
        length += cost
    status = run([executable, *options, *batch], cwd=root) or status
    if status:
        print(f"format: {len(files)} file(s) checked with clang-format {version}; "
              "run python scripts/format.py to apply formatting.", file=sys.stderr)
    else:
        action = "reformatted" if fix else "checked"
        print(f"format: {action} {len(files)} file(s) with clang-format {version}.")
    return status
