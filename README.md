# HikariEngine

[![Linux Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-release-linux%29&label=Linux%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-debug-linux%29&label=Linux%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-asan-linux%29&label=Linux%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-release-windows%29&label=Windows%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-debug-windows%29&label=Windows%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-asan-windows%29&label=Windows%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)

A cross-platform game engine for Windows and Linux, with two graphics backends behind one neutral RHI: **Vulkan** on both platforms, **D3D12** on Windows.

Three executables come out of a build:

| Binary | What it is |
|---|---|
| `HikariEditor` | the engine in an SDL window, with the ImGui editor panel and a free camera |
| `HikariHeadless` | the same engine with no window at all, rendering into an offscreen target |
| `HikariCompare` | compares two runs — their JSON reports and their PNG captures — and exits with a verdict |

**Contents:** [Requirements](#requirements) · [Build](#build) · [Run](#run) ·
[Options](#options) · [Tests and checks](#tests-and-checks) ·
[Regression checking](#regression-checking) · [Repository layout](#repository-layout) ·
[Documentation](#documentation)

---

## Requirements

- **CMake 4.0 or newer**, and **Ninja** for the `ninja-*` presets.
- A C++20 compiler: MSVC 2022 on Windows, GCC or Clang on Linux.
- **vcpkg**, with the `VCPKG_ROOT` environment variable pointing at your checkout. Every preset
  derives `CMAKE_TOOLCHAIN_FILE` from it.

### Linux packages

vcpkg does not supply the X11 and Wayland client libraries, and SDL3 pulls in a chain that runs
`autoreconf`. On Debian and Ubuntu:

```bash
sudo apt install libxcb1-dev libx11-dev libxext-dev libxrandr-dev libwayland-dev \
                 libxcursor-dev libxi-dev libxfixes-dev libxtst-dev \
                 autoconf autoconf-archive automake libtool libltdl-dev
```

---

## Build

Presets are `ninja-{release,debug,asan}-{linux,windows}`, plus `msvc` for a Visual Studio
solution. Artifacts land in `build/<preset>/`.

```bash
./build.sh                          # host default: ninja-debug-linux
./build.sh ninja-release-linux      # or any preset
```

```bat
build.bat                           :: host default: ninja-debug-windows
build.bat ninja-release-windows
```

Both scripts wrap one command, which works the same on either platform:

```bash
cmake --workflow --preset ninja-debug-linux
```

### Visual Studio solution

All three configure only — they write the solution into `build/msvc/`.

```bat
GENERATE_SLN.bat
cmake --workflow --preset generate-sln   :: what the script runs
cmake --preset msvc                      :: what that workflow's single step is
```

### Without `VCPKG_ROOT`

Point at the toolchain file yourself:

```bash
cmake --preset msvc -DCMAKE_TOOLCHAIN_FILE=/your/path/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### What a build produces

Shaders are a build step, not content: `slangc` compiles `engine/engine/src/shaders/*.slang`
into `build/<preset>/shaders/`, one set per configuration, and the executables find them there.

Every Ninja or Makefile configure symlinks `compile_commands.json` at the repository root to that
build's copy, on both platforms — so the preset configured last is the one clangd reads. The
`msvc` preset does not, since Visual Studio generators do not emit the file. On Windows the
symlink needs Developer Mode or an elevated shell; without either, CMake prints
`compile_commands.json link skipped: ...` at configure time and the build carries on.

---

## Run

Asset paths resolve against a **content root**, not the working directory, so the binaries run
from anywhere. `Paths` picks the root in this order: `--content` → the `HIKARI_CONTENT`
environment variable → `<exe dir>/content` → `<source dir>/content`.

### Editor

```bash
./build/ninja-debug-linux/HikariEditor
./build/ninja-debug-linux/HikariEditor --scene scenes/test_scene.map
./build/ninja-debug-linux/HikariEditor --content /path/to/content
./build/ninja-debug-linux/HikariEditor --help
```

**Without `--scene` no scene is loaded** — the engine comes up on an empty one. The scenes a
clone can load are in `content/scenes/`: `test_scene.map`, and `stress.map` for a heavier one.

| Input | What it does |
|---|---|
| `Escape` | toggle between camera control and the UI. A run starts on the UI, and movement is ignored while the cursor is visible |
| `F9` / `F10` / `F11` | windowed / borderless fullscreen / exclusive fullscreen |

### Headless

The same engine with no window, rendering into an offscreen target. The UI still draws, so a
headless capture and an editor capture of the same frame come out pixel-identical.

It needs something that can end the run — `--frames`, or an `--input` script containing `quit`
— takes `--resolution` for the target's extent rather than a window's.

```bash
./build/ninja-debug-linux/HikariHeadless --frames 100 --screenshot --report
./build/ninja-debug-linux/HikariHeadless --frames 100 --resolution 1920x1080 --fixed-dt \
                                         --camera-preset 1 --no-ui
```

`--screenshot` and `--report` take an optional path. Without one they write timestamped files
into `tests/screenshots/` and `tests/reports/`, relative to the current directory.

### Input scripts

Both binaries replay a script of key presses, resizes, captures and a quit, delivered on the
frames it names. The editor merges it with real input, so a scripted run that failed in CI can be
watched.

```bash
./build/ninja-debug-linux/HikariHeadless --input tests/data/input/orbit.txt
./build/ninja-debug-linux/HikariEditor  --input tests/data/input/orbit.txt
```

One command per line. The scripts under `tests/data/input/` are the worked
examples:

```
frame 1   key.down Escape       # release the cursor to the camera
frame 2   key.down W
frame 6   key.up W
frame 8   window.resize 320x240
frame 22  screenshot
frame 63  quit
```

---

## Options

Both binaries take the same options, except where the window is involved. `--help` on either
prints the authoritative list, including which backends this build actually contains.

### Scene and run

| Option | Meaning |
|---|---|
| `--scene <path>` | scene to load; without the flag, no scene is loaded |
| `--content <dir>` | content root |
| `--frames <N>` | exit after N frames (0 = run until closed) |
| `--fixed-dt` | a fixed 1/60 s timestep instead of wall-clock time |
| `--camera-preset <N>` | start the camera at preset 0–2 instead of free-look |
| `--no-ui` | suppress the editor panel; ImGui still initialises and its pass still runs |
| `--jobs <N>` | worker threads (0 = serial, no threads; default `hardware_concurrency() - 1`) |
| `--frames-in-flight <N>` | frames the CPU may work on at once (default 2) |
| `--input <path>` | replay an input script |

### Output

| Option | Meaning |
|---|---|
| `--screenshot [path]` | write a PNG of the final frame before exiting |
| `--report [path]` | write a JSON run report before exiting |

### Window (`HikariEditor` only)

| Option | Meaning |
|---|---|
| `--resolution <W>x<H>` | window size (default: three quarters of the display) |
| `--borderless` | start covering the display as a borderless window |
| `--fullscreen` | start in exclusive fullscreen |

`HikariHeadless` takes `--resolution` too, for the offscreen target, and has no window modes —
the binary *is* the mode.

### Backend and device

| Option | Meaning |
|---|---|
| `--backend <name>` | which backend to run on; `--help` lists what the build contains (default `Vulkan`) |
| `--gpu <name>` | first suitable adapter whose name contains `<name>`, case-insensitively |
| `--force-single-queue` | behave as though one queue served every role, as an integrated GPU does |
| `--vk-disable-extension <name>` | Vulkan only. Pretend an optional extension is missing, to exercise the fallback. Repeatable |
| `--d3d12-barriers <legacy\|enhanced\|auto>` | D3D12 only. Barrier model; `auto` takes enhanced where the adapter supports it (default `auto`) |

A flag only one backend can honour carries that backend's prefix, and the parser refuses it under
the other backend rather than ignoring it.

### Validation

| Option | Meaning |
|---|---|
| `--validation <on\|off>` | load the backend's validation layer (default: on in Debug, off in Release) |
| `--validation-policy <ignore\|count\|failfast>` | what a message means for the run; `failfast` aborts on the first error (default `count`) |
| `--strict-validation` | exit non-zero if any validation error occurred |
| `--vk-sync-validation <on\|off>` | Vulkan only. Synchronization validation — the expensive sub-mode (default on) |
| `--d3d12-gpu-based-validation <off\|descriptors\|full>` | D3D12 only. The debug layer's GPU-side checks; `full` adds resource-state tracking and costs several times more (default `descriptors`) |

Combinations that read as stricter than they are — `--validation off` alongside a policy or
`--strict-validation` — are refused at parse time.

---

## Tests and checks

Every script resolves `build/<preset>/` relative to the current directory, so run them from the
repository root, and each has a `.bat` beside it. The ones that need a configured tree take an
optional preset and default to the host's debug one; `format_check.sh`, `rhi_boundary_check.sh`
and `namespace_check.sh` take none, because they read the source and nothing else.

```bash
tests/scripts/build_tests.sh          # build every test target
tests/scripts/run_unit_tests.sh       # ctest -L unit
tests/scripts/run_gpu_tests.sh        # ctest -L gpu   — needs a device, or the cases skip
tests/scripts/run_scene_tests.sh      # ctest -L scene — real headless runs, asserting on the report
tests/scripts/header_check.sh         # compile every header standalone, with no PCH
tests/scripts/rhi_boundary_check.sh   # the RHI seam: neutral headers, and who may bypass them
tests/scripts/namespace_check.sh      # every engine header opens its module's namespace
tests/scripts/format_check.sh         # clang-format dry run, -Werror; needs no configured tree
tests/scripts/backend_compare.sh      # Windows only: the test scene under both backends, compared

scripts/format.sh                     # clang-format -i over the tree
scripts/precommit.sh                  # configure, build, all of the above
```

**`backend_compare.sh` needs a build containing both backends, and D3D12 is built only on
Windows** — so run it there as `backend_compare.bat`. Anywhere else it exits 3
with a message rather than pretending to have compared anything.

**`scripts/precommit.sh` is the one to run before calling a change done.** It is a superset of
CI: everything CI enforces, plus the GPU and scene tests, which CI's runners cannot run on Linux
without an ICD. Those skip rather than fail on a machine without one, so check whether they
actually ran before relying on a green result.

The suites are registered once per backend the build contains. Vulkan keeps the bare label and
D3D12's is suffixed, so `ctest -L gpu` runs both and `-L gpu-d3d12` runs D3D12 alone.
`HIKARI_TEST_GPU` names the adapter, as `--gpu` does for the apps.

### clang-format is pinned

clang-format's output is not stable across major versions, so the version is pinned in
`.clang-format-version` and CMake warns at configure time if yours differs:

```bash
pip install clang-format==$(cat .clang-format-version)
```

---

## Regression checking

"It still builds" is not evidence that a change preserved rendering. `baseline_test.sh` runs the
app with a fixed timestep and a fixed camera, then compares what came out against the committed
`tests/baseline/`:

```bash
tests/scripts/baseline_test.sh                       # capture, then compare
tests/scripts/baseline_test.sh --update              # capture, compare, promote the baseline
tests/scripts/baseline_test.sh ninja-release-linux   # any preset
```

Exit codes, strongest first:

| Code | Meaning |
|---|---|
| **3** | no verdict — a report would not read, or a field is missing from one of them |
| **1** | a compared signal moved |
| **2** | nothing moved, but a signal could not be compared |
| **0** | everything was compared, and it matched |

A no-verdict is not a pass: it means nothing was established.

`HikariCompare` is the tool underneath, and works on any two runs:

```bash
./build/<preset>/HikariCompare --actual-report a.json --expected-report b.json \
                               --actual-image a.png  --expected-image b.png
```

On a pixel failure it writes `comparison_actual.png`, `comparison_expected.png` and an amplified
`comparison_diff.png` beside the capture, so you can see where an image moved and by how much.

**Never byte-compare two captures** — PNG encoding is not reproducible, so `cmp` and `md5sum`
differ on a pixel-identical pair. That is what the comparison tool is for.

### Reading a report

The report's blocks are read differently:

- **`counters`** are expectations, and must match exactly. `counters.frame` (`drawCalls`,
  `batches`, `instances`, `barriers`, `barrierCalls`) describes the last frame drawn, which is
  the frame a capture shows; `counters.run` (`validationErrors`, `validationWarnings`,
  `uploadBatches`, `uploadSubmissions`) accumulates over the run.
- **`timings`** — `startupMs`, `firstFrame`, and `mean`/`p99`/`min`/`max` for `frameMs` and
  `cpuMs` — are measurements, never diffed. Read them for drift. `frameMs` is bounded below by
  the display refresh wherever the present path throttles the CPU, which is what `cpuMs` exists
  to see past. Frame 0 is reported separately, since it pays for first use of every pipeline.
- **`run`** and **`system`** describe the conditions, and decide which signals are comparable at
  all. A skip names the field that caused it.

---

## Repository layout

```
apps/editor/      HikariEditor — SDL window, UI attached
apps/headless/    HikariHeadless — no window, offscreen target
engine/core/      Engine::Core — logging, timing, jobs, handles, containers
engine/platform/  Engine::Platform — windowing, paths, filesystem, command line, input scripts
engine/asset/     Engine::Asset — asset cache, PNG encoding
engine/rhi/       Engine::RHI — the backend-neutral graphics API, and the Vulkan and D3D12
                  backends behind it in src/vulkan/ and src/d3d12/
engine/engine/    Engine::Engine — the renderer, the scene, the asset types, the frame loop
engine/editor/    Engine::Editor — the ImGui stack and each backend's glue
cmake/            module, testing, header-check and D3D12 deployment helpers
ports/            vcpkg overlay ports
content/          the runtime content root — models/ scenes/ textures/
tests/unit/       Catch2 tests, no GPU                    (label "unit")
tests/gpu/        Catch2 tests needing a real device      (label "gpu")
tests/scene/      real headless runs asserting on reports (label "scene")
tests/data/       a content root of its own, for tests
tests/support/    the shared image and report comparison
tests/tools/      HikariCompare
docs/             the architecture plan, the RHI decisions, the backlog
```

Source lists are explicit rather than globbed, so a new `.cpp` has to be added to its module's
`engine_module(...)` call or it will silently not build. Headers *are* globbed into the header
and format checks.

---

## Documentation

| Document | What it holds |
|---|---|
| `docs/architecture_plan.md` | the target architecture, the test strategy, and the incremental work order the project follows |
| `docs/rhi.md` | what the RHI's public seam is allowed to say, and the numbered decisions behind it. Read before touching `engine/rhi/include/` |
| `docs/backlog.md` | everything off the critical path, prioritised |
| `docs/suggested_work.md` | the code review that motivated the plan — the *why* behind a known defect |
| `CLAUDE.md` | the working rules, conventions and gotchas, in full |

---

## License

MIT. See `LICENSE.txt`.
