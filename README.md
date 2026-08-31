# HikariEngine

[![Linux Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-release-linux%29&label=Linux%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-debug-linux%29&label=Linux%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Linux ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28ubuntu-latest%2C%20ninja-asan-linux%29&label=Linux%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Release](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-release-windows%29&label=Windows%20Release)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows Debug](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-debug-windows%29&label=Windows%20Debug)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)
[![Windows ASan](https://img.shields.io/github/check-runs/ayilinkou/HikariEngine/main?nameFilter=build%20%28windows-latest%2C%20ninja-asan-windows%29&label=Windows%20ASan)](https://github.com/ayilinkou/HikariEngine/actions/workflows/ci.yml)

## Requirements
This project requires CMake and vcpkg. Make sure the `VCPKG_ROOT` environment variable points at your vcpkg checkout.

Linux needs the X11 and Wayland development packages. On Debian/Ubuntu:

```Terminal
sudo apt install libxcb1-dev libx11-dev libxrandr-dev libwayland-dev
```

## Build
`CMakePresets.json` contains presets for building on Windows and Linux. These presets set the `CMAKE_TOOLCHAIN_FILE` variable using a `VCPKG_ROOT` environment variable. If you're using these presets, please make sure that the `VCPKG_ROOT` environment variable is set correctly.

```Terminal
cmake --workflow generate-sln
cmake --workflow ninja-debug-linux
cmake --workflow ninja-release-windows
cmake --workflow ninja-asan-windows
```

Otherwise you can manually set the `CMAKE_TOOLCHAIN_FILE` variable yourself like this:

```Terminal
cmake --preset msvc -DCMAKE_TOOLCHAIN_FILE=your/path/here/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Windows

You can generate a Visual Studio solution into the `build` folder by running the `GENERATE_SLN.bat` script or using the following preset:
```Terminal
cmake --preset msvc
```

To build the debug config using ninja, just run the `build.bat` script.
```Terminal
build.bat
```

### Linux

To build the debug config, just run the `build.sh` script.
```Terminal
./build.sh
```
