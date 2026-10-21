#!/usr/bin/env python3
"""Compare Vulkan and D3D12 reports and images on Windows."""

from pathlib import Path
import platform
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import BuildContext, Parser, add_build_arguments, entrypoint, run


def main(argv=None):
    parser = Parser(description=__doc__, error_code=3)
    add_build_arguments(parser)
    args = parser.parse_args(argv)
    if platform.system() != "Windows":
        parser.error("backend_compare requires Windows: D3D12 is only built on Windows")
    context = BuildContext.from_args(args, parser)
    reports = {backend: f"tests/reports/backend_{backend}.json" for backend in ("vulkan", "d3d12")}
    captures = {backend: f"tests/screenshots/backend_{backend}.png" for backend in reports}
    # Same adapter defaults, scene, extent and frame count as the measured pixel
    # tolerances. HikariCompare skips pixels if the PCI IDs differ. D3D12 uses
    # full GPU-based validation so both backends validate their strongest mode.
    for backend in ("Vulkan", "D3D12"):
        key = backend.lower()
        status = run([
            context.executable("HikariHeadless"), "--backend", backend,
            "--report", reports[key], "--screenshot", captures[key],
            "--frames", "100", "--fixed-dt", "--scene", "scenes/test_scene.map",
            "--camera-preset", "1", "--resolution", "1920x1080", "--no-ui",
            *(["--d3d12-gpu-based-validation", "full"] if backend == "D3D12" else []),
        ], cwd=context.root)
        if status:
            return status
    print(flush=True)
    return run([
        context.executable("HikariCompare"),
        "--actual-report", reports["d3d12"], "--expected-report", reports["vulkan"],
        "--actual-image", captures["d3d12"], "--expected-image", captures["vulkan"],
    ], cwd=context.root)


if __name__ == "__main__":
    raise SystemExit(entrypoint(main, error_code=3))
