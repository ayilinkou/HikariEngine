# HikariEngine

[![Linux Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-release-linux%29&label=Linux%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-debug-linux%29&label=Linux%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-asan-linux%29&label=Linux%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-release-windows%29&label=Windows%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-debug-windows%29&label=Windows%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-asan-windows%29&label=Windows%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)

A cross-platform game engine for Windows and Linux, with two graphics backends behind one neutral RHI: **Vulkan** on both platforms, **D3D12** on Windows.

## Requirements

- **CMake 4.0 or newer**, and **Ninja** for the `ninja-*` presets.
- A C++20 compiler: MSVC 2022 on Windows, GCC or Clang on Linux.
- **vcpkg**, with the `VCPKG_ROOT` environment variable pointing at its root folder. Every preset
  derives `CMAKE_TOOLCHAIN_FILE` from it, otherwise you must pass in your toolchain file manually.

### Linux packages

vcpkg does not supply the X11 and Wayland client libraries, and SDL3 pulls in a chain that runs
`autoreconf`. On Debian and Ubuntu:

```bash
sudo apt install libxcb1-dev libx11-dev libxext-dev libxrandr-dev libwayland-dev \
                 libxcursor-dev libxi-dev libxfixes-dev libxtst-dev \
                 autoconf autoconf-archive automake libtool libltdl-dev
```

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

### Visual Studio solution

Configure only — generates the solution into `build/msvc/`.

```bat
GENERATE_SLN.bat
```

## Options

Both the editor and headless binaries take the same options, except where the window is involved.

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

### Window

| Option | Meaning |
|---|---|
| `--resolution <W>x<H>` | window size (default: three quarters of the display) |
| `--borderless` (editor only) | start covering the display as a borderless window |
| `--fullscreen` (editor only) | start in exclusive fullscreen |

### Backend and device

| Option | Meaning |
|---|---|
| `--backend <name>` | which backend to run on; `--help` lists what the build contains (default `Vulkan`) |
| `--gpu <name>` | first suitable adapter whose name contains `<name>`, case-insensitively |
| `--force-single-queue` | behave as though one queue served every role, as an integrated GPU does |
| `--present-mode <immediate\|mailbox\|fifo>` | Require this presentation mode. Without the flag, prefer mailbox, then immediate, then FIFO |
| `--vk-disable-extension <name>` | Vulkan only. Pretend an optional extension is missing, to exercise the fallback |
| `--d3d12-barriers <legacy\|enhanced\|auto>` | D3D12 only. Barrier model; `auto` takes enhanced where the adapter supports it (default `auto`) |

### Validation

| Option | Meaning |
|---|---|
| `--validation <on\|off>` | load the backend's validation layer (default: on in Debug and ASan, off in Release) |
| `--validation-policy <ignore\|count\|failfast>` | what a message means for the run; `failfast` aborts on the first error (default `count`) |
| `--strict-validation` | exit non-zero if any validation error occurred |
| `--vk-sync-validation <on\|off>` | Vulkan only. Synchronization validation (default on) |
| `--d3d12-gpu-based-validation <off\|descriptors\|full>` | D3D12 only. The debug layer's GPU-side checks; `full` adds resource-state tracking and costs several times more (default `descriptors`) |

## License

MIT. See `LICENSE.txt`.
