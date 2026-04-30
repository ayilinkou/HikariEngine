# VulkanApp

## Requirements
This project requires CMake and the Vulkan SDK to be installed.

## Build
`CMakePresets.json` contains presets for building on Windows and Linux. These presets set the `CMAKE_TOOLCHAIN_FILE` variable using a `VCPKG_ROOT` environment variable. If you're using these presets, please make sure that the `VCPKG_ROOT` environment variable is set correctly. Otherwise you can manually set the `CMAKE_TOOLCHAIN_FILE` variable yourself like this:

```Terminal
cmake --workflow --preset msvc -DCMAKE_TOOLCHAIN_FILE=your/path/here/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Windows

You can generate a Visual Studio solution into the `build` folder using the following preset:
```Terminal
cmake --preset msvc
```

Or you can manually specify which generator you want to use.
```Terminal
cmake -B build -G "Visual Studio 17 2022"
```

### Linux

The `CMakePresets.json` file contains presets to build the project on Linux using Ninja.
```Terminal
cmake --workflow --preset ninja-debug-linux
cmake --workflow --preset ninja-release-linux
```
