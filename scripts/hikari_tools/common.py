"""Paths, command execution, presets and Windows toolchain setup."""

import argparse
from dataclasses import dataclass
from functools import cached_property
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


class ToolError(Exception):
    """An actionable tool failure, reported without a Python traceback."""


class Parser(argparse.ArgumentParser):
    def __init__(self, *args, error_code=2, **kwargs):
        super().__init__(*args, allow_abbrev=False, **kwargs)
        self.error_code = error_code

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(self.error_code, f"{self.prog}: error: {message}\n")


def entrypoint(main, error_code=1):
    try:
        return main()
    except (OSError, ToolError) as error:
        print(f"error: {error}", file=sys.stderr)
        return error_code
    except KeyboardInterrupt:
        return 130


def exit_status(status):
    return 128 - status if status < 0 else status


def _run_process(command, *, executable, cwd, env):
    try:
        return exit_status(subprocess.run(command, cwd=cwd, env=env).returncode)
    except OSError as error:
        # Windows launch errors can omit the filename entirely. Supply our own
        # executable and working directory, without exposing command arguments.
        raise ToolError(f"Could not start {executable!r} in {str(cwd)!r}: {error}") from error


def run(command, *, cwd=ROOT, env=None):
    """Pass arguments directly to the child; inherit its output and exit status."""
    arguments = [str(arg) for arg in command]
    return _run_process(arguments, executable=arguments[0], cwd=cwd, env=env)


def run_cmd(command, *, cwd=ROOT, env=None):
    # cmd.exe does not use the C argument quoting rules used by list2cmdline.
    # Quote the entire /c expression explicitly; its contents are fixed code,
    # with paths passed through environment variables rather than interpolated.
    comspec = (os.environ if env is None else env).get("COMSPEC", "cmd.exe")
    command_line = f'{subprocess.list2cmdline([comspec])} /d /s /v:off /c "{command}"'
    return _run_process(command_line, executable=comspec, cwd=cwd, env=env)


def windows_environment(root=ROOT, base_env=None):
    """Import envsetup.bat's environment with Windows' uppercase variable names.

    The child Python normalizes names before JSON serialization. The returned
    plain dictionary preserves Unicode and embedded '=' but has case-sensitive
    lookups, so callers must use uppercase keys.
    """
    environment = dict(os.environ if base_env is None else base_env)
    with tempfile.TemporaryDirectory(prefix="hikari env ") as temporary:
        output = Path(temporary) / "environment.json"
        helpers = {
            "HIKARI_ENVSETUP": str(root / "scripts" / "envsetup.bat"),
            "HIKARI_PYTHON": sys.executable,
            "HIKARI_ENV_FILE": str(output),
        }
        environment.update(helpers)
        capture = (
            "import json, os; from pathlib import Path; "
            "Path(os.environ['HIKARI_ENV_FILE']).write_text("
            "json.dumps(dict(os.environ)), encoding='utf-8')"
        )
        status = run_cmd(
            'call "%HIKARI_ENVSETUP%" && "%HIKARI_PYTHON%" -c "' + capture + '"',
            cwd=root, env=environment,
        )
        if status:
            raise ToolError(f"Windows toolchain setup failed (exit {status}).")
        try:
            configured = json.loads(output.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ToolError(f"Could not read the Windows toolchain environment: {error}") from error
    original = os.environ if base_env is None else base_env
    for key in helpers:
        if key in original:
            configured[key] = original[key]
        else:
            configured.pop(key, None)
    return configured


def configuration(value):
    names = {"debug": "Debug", "release": "Release"}
    try:
        return names[value.casefold()]
    except KeyError:
        raise argparse.ArgumentTypeError("configuration must be Debug or Release (case-insensitive)") from None


def add_build_arguments(parser):
    parser.add_argument("preset", nargs="?", help="CMake preset (default: host Ninja Debug)")
    parser.add_argument("--config", type=configuration, help="Debug or Release; msvc defaults to Debug")


@dataclass(frozen=True)
class BuildContext:
    preset: str
    config: str
    root: Path = ROOT

    @classmethod
    def from_args(cls, args, parser, root=ROOT):
        system = platform.system()
        if system not in ("Linux", "Windows"):
            parser.error(f"unsupported OS: {system}; supported platforms are Linux and Windows")
        preset = args.preset or f"ninja-debug-{system.lower()}"
        with (root / "CMakePresets.json").open(encoding="utf-8") as stream:
            presets = json.load(stream)["configurePresets"]
        available = {item["name"] for item in presets if not item.get("hidden", False)}
        if preset not in available:
            parser.error(f"unknown preset {preset!r}; choose one of: {', '.join(sorted(available))}")
        if preset == "msvc":
            if system != "Windows":
                parser.error("the msvc preset requires Windows")
            config = args.config or "Debug"
        else:
            if not preset.endswith(f"-{system.lower()}"):
                parser.error(f"preset {preset!r} cannot run on {system}")
            config = "Release" if "-release-" in preset else "Debug"
            if args.config and args.config != config:
                parser.error(f"{preset} selects {config}; choose a matching preset instead of --config {args.config}")
        return cls(preset, config, root)

    @property
    def build_dir(self):
        return self.root / "build" / self.preset

    @property
    def config_args(self):
        return ["--config", self.config] if self.preset == "msvc" else []

    @cached_property
    def environment(self):
        # The Visual Studio generator locates MSVC through MSBuild itself.
        if platform.system() == "Windows" and self.preset != "msvc":
            return windows_environment(self.root)
        return os.environ.copy()

    def build(self, target=None):
        command = ["cmake", "--build", self.build_dir, *self.config_args]
        if target:
            command += ["--target", target]
        return run(command, cwd=self.root, env=self.environment)

    def workflow(self):
        if self.preset == "msvc":
            # There is no msvc workflow preset: GENERATE_SLN.bat configures only.
            status = run(["cmake", "--preset", self.preset], cwd=self.root, env=self.environment)
            return status or self.build()
        if platform.system() == "Windows":
            environment = self.environment | {
                "HIKARI_BUILD_SCRIPT": str(self.root / "build.bat"),
                "HIKARI_BUILD_PRESET": self.preset,
            }
            return run_cmd('call "%HIKARI_BUILD_SCRIPT%" "%HIKARI_BUILD_PRESET%"',
                           cwd=self.root, env=environment)
        return run([self.root / "build.sh", self.preset], cwd=self.root, env=self.environment)

    def ctest(self, label):
        command = ["ctest", "--test-dir", self.build_dir]
        if self.preset == "msvc":
            command += ["-C", self.config]
        command += ["-L", label, "--no-tests=error", "--output-on-failure"]
        return run(command, cwd=self.root, env=self.environment)

    def executable(self, name):
        directory = self.build_dir / self.config if self.preset == "msvc" else self.build_dir
        return directory / (name + (".exe" if platform.system() == "Windows" else ""))


def build_tool(description, *, target=None, label=None, argv=None):
    parser = Parser(description=description)
    add_build_arguments(parser)
    context = BuildContext.from_args(parser.parse_args(argv), parser)
    return context.ctest(label) if label else context.build(target)
