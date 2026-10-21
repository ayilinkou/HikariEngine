#!/usr/bin/env python3
"""Build and run the repository's checks in their established fail-fast order."""

import sys

from hikari_tools.common import BuildContext, Parser, add_build_arguments, entrypoint, run


def main(argv=None):
    parser = Parser(description=__doc__)
    add_build_arguments(parser)
    args = parser.parse_args(argv)
    context = BuildContext.from_args(args, parser)
    print(f"Running precommit for {context.preset} ({context.config})...", flush=True)
    status = run([sys.executable, "-m", "unittest", "discover", "-s", context.root / "tests/tooling", "-v"],
                 cwd=context.root, env=context.environment)
    if status:
        return status
    status = context.workflow()
    if status:
        return status
    build_args = [context.preset, "--config", context.config]
    steps = [
        ("header_check", build_args),
        ("rhi_boundary_check", []),
        ("namespace_check", []),
        ("build_tests", build_args),
        ("run_unit_tests", build_args),
        ("run_gpu_tests", build_args),
        ("run_scene_tests", build_args),
        ("format_check", []),
    ]
    for script, arguments in steps:
        status = run([sys.executable, context.root / "tests" / "scripts" / f"{script}.py", *arguments],
                     cwd=context.root, env=context.environment)
        if status:
            return status
    print("Precommit succeeded!")
    return 0


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
