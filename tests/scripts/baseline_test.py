#!/usr/bin/env python3
"""Capture the baseline scene, then compare or update its committed reference."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import BuildContext, Parser, add_build_arguments, entrypoint, run


def main(argv=None):
    parser = Parser(description=__doc__, error_code=3)
    add_build_arguments(parser)
    parser.add_argument("--update", action="store_true", help="Promote the capture through HikariCompare")
    args = parser.parse_args(argv)
    context = BuildContext.from_args(args, parser)
    report = "tests/reports/baseline.json"
    capture = "tests/screenshots/baseline.png"
    # Borderless pins the capture to the display even on tiling compositors;
    # --resolution is the fallback request. --no-ui removes cursor-dependent
    # ImGui hover states. These flags and frame count define the baseline.
    status = run([
        context.executable("HikariEditor"), "--report", report, "--screenshot", capture,
        "--frames", "1000", "--fixed-dt", "--scene", "scenes/test_scene.map",
        "--camera-preset", "1", "--resolution", "1920x1080", "--borderless", "--no-ui",
        "--present-mode", "immediate",
    ], cwd=context.root)
    if status:
        return status
    print(flush=True)
    # Preserve comparison verdicts verbatim: 1, 2 and 3 have distinct meanings.
    return run([
        context.executable("HikariCompare"),
        "--actual-report", report, "--expected-report", "tests/baseline/report.json",
        "--actual-image", capture, "--expected-image", "tests/baseline/screenshot.png",
        *(["--update"] if args.update else []),
    ], cwd=context.root)


if __name__ == "__main__":
    raise SystemExit(entrypoint(main, error_code=3))
