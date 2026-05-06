# VulkanApp

## Requirements
This project requires CMake, vcpkg and the Vulkan SDK to be installed. Please make sure the `VULKAN_SDK` environment variable is set to the correct path.

## Build
`CMakePresets.json` contains presets for building on Windows and Linux. These presets set the `CMAKE_TOOLCHAIN_FILE` variable using a `VCPKG_ROOT` environment variable. If you're using these presets, please make sure that the `VCPKG_ROOT` environment variable is set correctly. Otherwise you can manually set the `CMAKE_TOOLCHAIN_FILE` variable yourself like this:

```Terminal
cmake --preset msvc -DCMAKE_TOOLCHAIN_FILE=your/path/here/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Windows

You can generate a Visual Studio solution into the `build` folder by running the `GENERATE_SLN.bat` script or using the following preset:
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
