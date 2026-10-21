"""Fast, standard-library tests for developer tooling; no engine build required."""

import contextlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/scripts"))
from hikari_tools import common, formatting
from hikari_tools.source import code_lines
import backend_compare
import baseline_test
import namespace_check
import precommit
import rhi_boundary_check as rhi


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hikari tooling & ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def put(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        return target

    def rhi_fixture(self):
        self.put("engine/rhi/include/rhi/Neutral.h", "// VkImage and ID3D12Device are fine in prose\nint neutral;\n")
        for header in rhi.TRANSITIONAL_HEADERS:
            self.put("engine/rhi/include/rhi/" + header, "// public escape hatch\n")
        for path, header in rhi.TRANSITIONAL_ALLOWLIST:
            target = self.root / path
            existing = target.read_text(encoding="utf-8") if target.exists() else ""
            self.put(path, existing + f'#include "rhi/{header}"\n')
        self.put("engine/rhi/src/Backend.cpp", '\n'.join(line for _, line in sorted(rhi.SHARED_EXEMPTIONS)))


class SourceChecks(Fixture):
    def test_missing_rhi_tree_fails_closed(self):
        with self.assertRaisesRegex(common.ToolError, "no neutral RHI headers"):
            rhi.check(self.root)

    def test_comment_state_and_line_numbers(self):
        path = self.put("comments.h", "int a; /* block //\nstill blocked */ VkImage b; // ignored\n"
                        "/* one */ int c; /* two */ int d;\n\"VkImage\";\n")
        self.assertEqual([code for _, _, code in code_lines(path)],
                         ["int a; ", " VkImage b; ", " int c;  int d;", '"VkImage";'])
        self.assertEqual([number for number, _, _ in code_lines(path)], [1, 2, 3, 4])

    def test_rhi_valid_fixture_and_comment_mentions(self):
        self.rhi_fixture()
        self.put("engine/rhi/include/rhi/Neutral.h", "/* VkImage\nID3D12Device */\n"
                 "int a; /* ignored */ int b; // vk::Device\nBackend::D3D12 backend;")
        self.assertEqual(len(rhi.check(self.root)), 5)

    def test_neutral_rhi_patterns_and_semicolon_line_numbers(self):
        self.rhi_fixture()
        for code in ('VkImage image;', 'vk::Device d;', 'VmaAllocator a;', 'VMA_FLAG',
                     '#include <vulkan/vulkan.h>', '#include "vk_mem_alloc.h"',
                     'ID3D12Device *device;', 'IDXGIAdapter *adapter;', 'D3D12_FLAG',
                     'DXGI_FORMAT', 'D3D12MA::Allocator a;', '#include <directx/d3d12.h>',
                     '#include <dxgi.h>', '"VkImage";'):
            with self.subTest(code=code):
                self.put("engine/rhi/include/rhi/Neutral.h", "// comment\nint ok;\nint x; " + code)
                with self.assertRaisesRegex(common.ToolError, r"Neutral.h:3:"):
                    rhi.check(self.root)

    def test_transitional_header_set_is_frozen_and_shrinks(self):
        self.rhi_fixture()
        extra = self.put("engine/rhi/include/rhi/vulkan/Extra.h", "int extra;")
        with self.assertRaisesRegex(common.ToolError, "unexpected header"):
            rhi.check(self.root)
        extra.unlink()
        (self.root / "engine/rhi/include/rhi/vulkan/VulkanNative.h").unlink()
        with self.assertRaisesRegex(common.ToolError, "TRANSITIONAL_HEADERS lists"):
            rhi.check(self.root)

    def test_new_external_include_and_stale_allowlist_fail(self):
        self.rhi_fixture()
        extra = self.put("apps/editor/Extra.cpp", '#include "rhi/vulkan/VulkanNative.h"\n')
        with self.assertRaisesRegex(common.ToolError, "new use.*exposes a backend"):
            rhi.check(self.root)
        extra.unlink()
        self.put("tests/unit/rhi/SwapchainUtilTests.cpp", "// include removed\n")
        with self.assertRaisesRegex(common.ToolError, "TRANSITIONAL_ALLOWLIST has entries"):
            rhi.check(self.root)

    def test_api_names_cannot_bypass_includes_or_cross_editor_allowlists(self):
        self.rhi_fixture()
        extra = self.put("apps/editor/Extra.cpp", "VkImage image;")
        with self.assertRaisesRegex(common.ToolError, "vulkan named outside"):
            rhi.check(self.root)
        extra.unlink()
        path = self.root / "engine/editor/src/VulkanUiBackend.cpp"
        path.write_text(path.read_text(encoding="utf-8") + "ID3D12Device *device;", encoding="utf-8")
        with self.assertRaisesRegex(common.ToolError, "d3d12 named outside"):
            rhi.check(self.root)

    def test_missing_naming_allowlist_file_fails(self):
        self.rhi_fixture()
        allowlists = {key: dict(value) for key, value in rhi.NAMING_ALLOWLISTS.items()}
        allowlists["vulkan"]["apps/Missing.cpp"] = "no longer exists"
        with mock.patch.object(rhi, "NAMING_ALLOWLISTS", allowlists):
            with self.assertRaisesRegex(common.ToolError, "naming allowlist has entries"):
                rhi.check(self.root)

    def test_backend_isolation_including_non_cpp_files(self):
        self.rhi_fixture()
        for path, code in (("engine/rhi/src/vulkan/Extra.inl", "ID3D12Device *d;"),
                           ("engine/rhi/src/d3d12/Extra.cpp", "VkDevice d;"),
                           ("engine/rhi/src/Shared.cpp", "vk::Device d;")):
            with self.subTest(path=path):
                target = self.put(path, code)
                with self.assertRaisesRegex(common.ToolError, "inside engine/rhi"):
                    rhi.check(self.root)
                target.unlink()

    def test_shared_exemption_is_exact_and_stale_entries_fail(self):
        self.rhi_fixture()
        dispatcher = self.root / "engine/rhi/src/Backend.cpp"
        original = dispatcher.read_text(encoding="utf-8")
        dispatcher.write_text(original + "\nVkDevice device;", encoding="utf-8")
        with self.assertRaisesRegex(common.ToolError, "inside engine/rhi"):
            rhi.check(self.root)
        dispatcher.write_text("// factory includes removed", encoding="utf-8")
        with self.assertRaisesRegex(common.ToolError, "SHARED_EXEMPTIONS has entries"):
            rhi.check(self.root)

    def namespace_fixture(self):
        self.put("engine/core/include/core/MyMacros.h", "#define MACRO 1\n")
        self.put("engine/core/include/core/Timer.h", "/* namespace Fake {} */\nnamespace Hikari::Core\n{\n}\n")
        self.put("engine/rhi/include/rhi/d3d12/Device.h", "namespace Hikari::Rhi::D3D12::Detail {\n}\n")

    def test_namespaces_follow_paths_and_acronyms(self):
        self.namespace_fixture()
        self.assertIn("2 public engine", namespace_check.check(self.root)[0])
        self.put("engine/core/include/core/Timer.h", "namespace Hikari::Wrong {\n}\n")
        with self.assertRaisesRegex(common.ToolError, "expected namespace Hikari::Core"):
            namespace_check.check(self.root)

    def test_using_directives_ignore_comments_and_preserve_lines(self):
        self.namespace_fixture()
        path = self.put("tests/support/Leak.h", "/* using namespace Fine; */\nint a; using namespace Bad;\n")
        with self.assertRaisesRegex(common.ToolError, r"Leak.h:2: using-directive"):
            namespace_check.check(self.root)
        path.write_text("/* using namespace Fine; */\n// using namespace Fine;\n", encoding="utf-8")
        namespace_check.check(self.root)

    def test_no_namespace_allowlist_cannot_outlive_reason_or_file(self):
        self.namespace_fixture()
        path = self.put("engine/core/include/core/MyMacros.h", "namespace Hikari::Core {}\n")
        # The rule recognizes a namespace opening on its own line.
        path.write_text("namespace Hikari::Core {\n}\n", encoding="utf-8")
        with self.assertRaisesRegex(common.ToolError, "remove the no-namespace"):
            namespace_check.check(self.root)
        path.unlink()
        with self.assertRaisesRegex(common.ToolError, "allowlisted header was not scanned"):
            namespace_check.check(self.root)


class Commands(Fixture):
    def parse(self, arguments, system="Windows", error_code=2):
        parser = common.Parser(error_code=error_code)
        common.add_build_arguments(parser)
        with mock.patch.object(common.platform, "system", return_value=system):
            return common.BuildContext.from_args(parser.parse_args(arguments), parser)

    def context(self, preset="msvc", config="Release"):
        context = common.BuildContext(preset, config, self.root)
        context.__dict__["environment"] = dict(os.environ)
        return context

    def test_host_defaults_and_case_insensitive_configurations(self):
        self.assertEqual(self.parse([], "Linux").preset, "ninja-debug-linux")
        self.assertEqual(self.parse([]).preset, "ninja-debug-windows")
        self.assertEqual(self.parse(["msvc"]).config, "Debug")
        for value in ("release", "Release", "RELEASE", "rElEaSe"):
            self.assertEqual(self.parse(["msvc", "--config", value]).config, "Release")
        self.assertEqual(self.parse(["msvc", "--config", "DEBUG"]).config, "Debug")

    def test_invalid_presets_platforms_and_conflicting_configs(self):
        for arguments, system in ((["missing"], "Linux"), (["msvc"], "Linux"),
                                  (["ninja-debug-windows"], "Linux"), ([], "Darwin"),
                                  (["ninja-debug-linux", "--config", "release"], "Linux"),
                                  (["msvc", "--config", "unknown"], "Windows")):
            with self.subTest(arguments=arguments, system=system), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    self.parse(arguments, system)
                self.assertEqual(error.exception.code, 2)

    def test_visual_studio_build_test_and_executable_selection(self):
        context = self.context()
        with mock.patch.object(common.platform, "system", return_value="Windows"), \
                mock.patch.object(common, "run", return_value=0) as run:
            context.build("HikariTests")
            self.assertEqual(run.call_args.args[0],
                             ["cmake", "--build", context.build_dir, "--config", "Release", "--target", "HikariTests"])
            context.ctest("scene")
            command = run.call_args.args[0]
            self.assertEqual(command[command.index("-C") + 1], "Release")
            self.assertEqual(context.executable("HikariEditor"), self.root / "build/msvc/Release/HikariEditor.exe")
            self.assertEqual(run.call_args.kwargs["cwd"], self.root)

    def test_visual_studio_workflow_configures_then_builds_and_stops_on_failure(self):
        context = self.context()
        with mock.patch.object(common, "run", side_effect=[0, 0]) as run:
            self.assertEqual(context.workflow(), 0)
            self.assertEqual(run.call_args_list[0].args[0], ["cmake", "--preset", "msvc"])
            self.assertIn("--config", run.call_args_list[1].args[0])
        with mock.patch.object(common, "run", return_value=17) as run:
            self.assertEqual(context.workflow(), 17)
            self.assertEqual(run.call_count, 1)

    def test_ninja_uses_preset_configuration_without_multi_config_paths(self):
        context = self.context("ninja-release-linux")
        with mock.patch.object(common.platform, "system", return_value="Linux"), \
                mock.patch.object(common, "run", return_value=0) as run:
            context.ctest("unit")
            self.assertNotIn("-C", run.call_args.args[0])
            self.assertEqual(context.executable("HikariEditor"), self.root / "build/ninja-release-linux/HikariEditor")
            context.workflow()
            self.assertEqual(run.call_args.args[0], [self.root / "build.sh", "ninja-release-linux"])

    def test_precommit_order_and_fail_fast(self):
        context = self.context()
        expected = ["header_check.py", "rhi_boundary_check.py", "namespace_check.py", "build_tests.py",
                    "run_unit_tests.py", "run_gpu_tests.py", "run_scene_tests.py", "format_check.py"]
        with mock.patch.object(precommit.BuildContext, "from_args", return_value=context), \
                mock.patch.object(common.BuildContext, "workflow", return_value=0), \
                mock.patch.object(precommit, "run", return_value=0) as run, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(precommit.main([]), 0)
            self.assertEqual(run.call_args_list[0].args[0][1:4], ["-m", "unittest", "discover"])
            self.assertEqual([call.args[0][1].name for call in run.call_args_list[1:]], expected)
            self.assertEqual(run.call_args_list[1].args[0][2:], ["msvc", "--config", "Release"])
        with mock.patch.object(precommit.BuildContext, "from_args", return_value=context), \
                mock.patch.object(common.BuildContext, "workflow", return_value=0), \
                mock.patch.object(precommit, "run", side_effect=[0, 0, 19]) as run, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(precommit.main([]), 19)
            self.assertEqual(run.call_count, 3)
            self.assertNotIn("Precommit succeeded", output.getvalue())
        with mock.patch.object(precommit.BuildContext, "from_args", return_value=context), \
                mock.patch.object(common.BuildContext, "workflow") as workflow, \
                mock.patch.object(precommit, "run", return_value=18), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(precommit.main([]), 18)
            workflow.assert_not_called()

    def test_baseline_preserves_flags_update_and_all_comparison_verdicts(self):
        context = self.context()
        for verdict in (0, 1, 2, 3):
            with self.subTest(verdict=verdict), \
                    mock.patch.object(baseline_test.BuildContext, "from_args", return_value=context), \
                    mock.patch.object(baseline_test, "run", side_effect=[0, verdict]) as run, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(baseline_test.main(["--update"]), verdict)
                editor, compare = [call.args[0] for call in run.call_args_list]
                for flag in ("--borderless", "--no-ui", "--fixed-dt"):
                    self.assertIn(flag, editor)
                self.assertEqual(editor[editor.index("--frames") + 1], "1000")
                self.assertEqual(editor[editor.index("--present-mode") + 1], "immediate")
                self.assertEqual(compare[-1], "--update")
                self.assertIn("tests/baseline/report.json", compare)
                self.assertEqual(run.call_args.kwargs["cwd"], self.root)
        with mock.patch.object(baseline_test.BuildContext, "from_args", return_value=context), \
                mock.patch.object(baseline_test, "run", return_value=23) as run:
            self.assertEqual(baseline_test.main([]), 23)
            self.assertEqual(run.call_count, 1)

    def test_backend_comparison_flags_verdict_and_fail_fast(self):
        context = self.context()
        with mock.patch.object(backend_compare.platform, "system", return_value="Windows"), \
                mock.patch.object(backend_compare.BuildContext, "from_args", return_value=context), \
                mock.patch.object(backend_compare, "run", side_effect=[0, 0, 2]) as run, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(backend_compare.main([]), 2)
            vulkan, d3d12, compare = [call.args[0] for call in run.call_args_list]
            self.assertIn("Vulkan", vulkan)
            self.assertNotIn("--d3d12-gpu-based-validation", vulkan)
            self.assertEqual(d3d12[-2:], ["--d3d12-gpu-based-validation", "full"])
            self.assertIn("tests/reports/backend_d3d12.json", compare)
        with mock.patch.object(backend_compare.platform, "system", return_value="Windows"), \
                mock.patch.object(backend_compare.BuildContext, "from_args", return_value=context), \
                mock.patch.object(backend_compare, "run", side_effect=[0, 21]) as run:
            self.assertEqual(backend_compare.main([]), 21)
            self.assertEqual(run.call_count, 2)

    def test_backend_comparison_rejects_linux_even_with_explicit_windows_preset(self):
        with mock.patch.object(backend_compare.platform, "system", return_value="Linux"), \
                contextlib.redirect_stderr(io.StringIO()) as output:
            with self.assertRaises(SystemExit) as error:
                backend_compare.main(["ninja-debug-windows"])
        self.assertEqual(error.exception.code, 3)
        self.assertIn("requires Windows", output.getvalue())

    def test_process_arguments_work_with_spaces_unicode_and_shell_metacharacters(self):
        output = self.root / "arguments.json"
        argument = "a space & equals= percent% snowman=☃"
        status = common.run([sys.executable, "-c",
                             "import json, os, sys; from pathlib import Path; "
                             "Path(sys.argv[1]).write_text(json.dumps([os.getcwd(), sys.argv[2]])); sys.exit(7)",
                             output, argument], cwd=self.root)
        self.assertEqual(status, 7)
        observed_root, observed_argument = json.loads(output.read_text(encoding="utf-8"))
        self.assertTrue(self.root.samefile(observed_root))
        self.assertEqual(observed_argument, argument)

    def test_missing_child_is_an_actionable_tool_error(self):
        with contextlib.redirect_stderr(io.StringIO()) as output:
            status = common.entrypoint(lambda: common.run([self.root / "missing-child"]), error_code=3)
        self.assertEqual(status, 3)
        self.assertIn("missing-child", output.getvalue())
        self.assertNotIn("Traceback", output.getvalue())

    def test_launch_errors_without_filenames_include_executable_and_working_directory(self):
        missing = self.root / "missing-child"
        for launch in (
            lambda: common.run([missing, "private-argument"], cwd=self.root),
            lambda: common.run_cmd("private-argument", cwd=self.root, env={"COMSPEC": str(missing)}),
        ):
            with self.subTest(launch=launch), \
                    mock.patch.object(common.subprocess, "run", side_effect=FileNotFoundError(2, "The system cannot find the file specified")), \
                    contextlib.redirect_stderr(io.StringIO()) as output:
                self.assertEqual(common.entrypoint(launch, error_code=3), 3)
                self.assertIn("missing-child", output.getvalue())
                self.assertIn(repr(str(self.root)), output.getvalue())
                self.assertIn("The system cannot find the file specified", output.getvalue())
                self.assertNotIn("private-argument", output.getvalue())
                self.assertNotIn("Traceback", output.getvalue())

    def test_all_entry_scripts_offer_help_from_another_directory(self):
        scripts = [ROOT / "scripts" / f"{name}.py" for name in ("format", "precommit")]
        scripts += [ROOT / "tests/scripts" / f"{name}.py" for name in (
            "build_tests", "header_check", "run_unit_tests", "run_gpu_tests", "run_scene_tests",
            "baseline_test", "backend_compare", "namespace_check", "rhi_boundary_check", "format_check")]
        for script in scripts:
            with self.subTest(script=script.name):
                result = subprocess.run([sys.executable, script, "--help"], cwd=self.root,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("usage:", result.stdout)
        result = subprocess.run([sys.executable, ROOT / "tests/scripts/baseline_test.py", "--unknown"],
                                cwd=self.root, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 3)


class Formatting(Fixture):
    def test_empty_source_selection_fails_closed(self):
        with mock.patch.object(formatting, "find_formatter", return_value=("formatter", "22.1.8")):
            with self.assertRaisesRegex(common.ToolError, "no sources found"):
                formatting.format_tree(root=self.root)

    def test_file_coverage_stays_the_same(self):
        included = ["src/Legacy.hpp", "engine/core/include/core/Timer.h", "engine/core/src/Timer.cpp"]
        for path in included + ["apps/editor/main.cpp", "tests/unit/Test.cpp", "engine/core/CMakeLists.txt",
                                "engine/core/src/shader.hlsl", "build/generated.cpp"]:
            self.put(path, "")
        self.assertEqual([path.relative_to(self.root).as_posix() for path in formatting.sources(self.root)], sorted(included))

    def test_missing_formatter_and_mismatched_version(self):
        self.put(".clang-format-version", "22.1.8\n")
        with mock.patch.object(formatting.shutil, "which", return_value=None):
            with self.assertRaisesRegex(common.ToolError, "not found on PATH"):
                formatting.find_formatter(self.root)
        version = subprocess.CompletedProcess([], 0, "clang-format version 23.0.0\n", "")
        with mock.patch.object(formatting.shutil, "which", return_value="formatter"), \
                mock.patch.object(formatting.subprocess, "run", return_value=version), \
                contextlib.redirect_stderr(io.StringIO()) as output:
            self.assertEqual(formatting.find_formatter(self.root), ("formatter", "23.0.0"))
            self.assertIn("clang-format==22.1.8", output.getvalue())

    def test_formatter_batches_windows_command_lines_and_checks_every_file_after_failure(self):
        for index in range(220):
            self.put(f"engine/core/src/{'long_name_' * 7}{index}.cpp", "int value;\n")
        files = formatting.sources(self.root)
        with mock.patch.object(formatting, "find_formatter", return_value=("formatter", "22.1.8")), \
                mock.patch.object(formatting, "run", side_effect=lambda *a, **k: 9) as run, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(formatting.format_tree(root=self.root), 9)
            self.assertGreater(run.call_count, 1)
            self.assertEqual([path for call in run.call_args_list for path in call.args[0][3:]], files)
            self.assertTrue(all(call.args[0][1:3] == ["--dry-run", "--Werror"] for call in run.call_args_list))
            self.assertTrue(all(len(subprocess.list2cmdline([str(arg) for arg in call.args[0]])) < 32767
                                for call in run.call_args_list))
        with mock.patch.object(formatting, "find_formatter", return_value=("formatter", "22.1.8")), \
                mock.patch.object(formatting, "run", return_value=0) as run, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(formatting.format_tree(fix=True, root=self.root), 0)
            self.assertTrue(all(call.args[0][1] == "-i" for call in run.call_args_list))


@unittest.skipUnless(platform.system() == "Windows", "cmd.exe and MSVC environment checks run on Windows CI")
class WindowsEnvironment(Fixture):
    def test_native_build_entrypoint_handles_spaces_and_returns_child_status(self):
        self.put("build.bat", '@echo off\n> "%~dp0preset.txt" echo %~1\nexit /b 23\n')
        context = common.BuildContext("ninja-debug-windows", "Debug", self.root)
        context.__dict__["environment"] = dict(os.environ)
        self.assertEqual(context.workflow(), 23)
        self.assertEqual((self.root / "preset.txt").read_text(encoding="utf-8").strip(), "ninja-debug-windows")

    def test_imports_batch_environment_with_spaces_metacharacters_and_unicode(self):
        self.put("scripts/envsetup.bat", '@echo off\nset "HIKARI_TEST_VALUE=space & equals=kept"\n'
                 'set "VcToolsVersion=fixture-toolset"\nexit /b 0\n')
        original = dict(os.environ) | {"HIKARI_UNICODE": "snowman ☃"}
        environment = common.windows_environment(self.root, original)
        self.assertEqual(environment["HIKARI_TEST_VALUE"], "space & equals=kept")
        self.assertEqual(environment["HIKARI_UNICODE"], "snowman ☃")
        self.assertEqual(environment["VCTOOLSVERSION"], "fixture-toolset")
        self.assertNotIn("HIKARI_ENV_FILE", environment)
        self.assertNotIn("HIKARI_TEST_VALUE", os.environ)

    def test_batch_failure_does_not_capture_an_unconfigured_environment(self):
        self.put("scripts/envsetup.bat", "@echo off\nexit /b 19\n")
        with self.assertRaisesRegex(common.ToolError, "exit 19"):
            common.windows_environment(self.root)

    def test_real_msvc_environment_smoke(self):
        environment = common.windows_environment(ROOT)
        compiler = shutil.which("cl.exe", path=environment.get("PATH"))
        self.assertIsNotNone(compiler, "envsetup.bat did not make cl.exe available")
        # JSON produces a plain dict; the child Python uppercases Windows keys.
        self.assertTrue(environment.get("VCTOOLSVERSION"))
        self.assertTrue(environment.get("INCLUDE"))
        self.assertTrue(environment.get("LIB"))
        result = subprocess.run([compiler, "/?"], env=environment, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake/Ninja required for target integration fixtures")
class CMakeIntegration(Fixture):
    def configure(self, python=True):
        script = "import pathlib, sys\nif '--check-version' not in sys.argv: pathlib.Path('tool-ran.txt').write_text('ok')\n"
        self.put("scripts/format.py", script)
        self.put("tests/scripts/format_check.py", script)
        self.put("CMakeLists.txt", 'cmake_minimum_required(VERSION 3.20)\nproject(Tooling NONE)\n'
                 f'include("{(ROOT / "cmake/DeveloperTools.cmake").as_posix()}")\n'
                 'hikari_add_developer_tools("${CMAKE_SOURCE_DIR}")\n'
                 'add_custom_target(ordinary COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/ordinary.txt" VERBATIM)\n')
        command = ["cmake", "-G", "Ninja", "-S", self.root, "-B", self.root / "build"]
        command += [f"-DPython3_EXECUTABLE={sys.executable}"] if python else ["-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def build(self, target):
        return subprocess.run(["cmake", "--build", self.root / "build", "--target", target],
                              capture_output=True, text=True, timeout=10)

    def test_normal_configuration_and_build_work_without_python(self):
        self.configure(python=False)
        ordinary = self.build("ordinary")
        self.assertEqual(ordinary.returncode, 0, ordinary.stdout + ordinary.stderr)
        result = self.build("format-check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires Python 3.12+", result.stdout + result.stderr)

    def test_format_targets_delegate_to_python_from_repository_root(self):
        self.configure()
        for target in ("format", "format-check"):
            result = self.build(target)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            marker = self.root / "tool-ran.txt"
            self.assertTrue(marker.exists())
            marker.unlink()


if __name__ == "__main__":
    unittest.main()
