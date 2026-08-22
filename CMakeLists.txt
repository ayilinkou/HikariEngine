include(cmake/Warnings.cmake)
include(cmake/HeaderSelfContainment.cmake)
include(cmake/EngineModule.cmake)

cmake_minimum_required(VERSION 4.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

# Only generate Debug and Release configurations
set(CMAKE_CONFIGURATION_TYPES
    "Debug;Release"
    CACHE STRING "" FORCE)

if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  file(
    CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
    RESULT compile_commands_link
    SYMBOLIC)
  if(NOT compile_commands_link STREQUAL "0")
    message(
      STATUS "compile_commands.json link skipped: ${compile_commands_link}")
  endif()
endif()

project(
  VulkanApp
  VERSION 1.0
  LANGUAGES CXX)

option(ENGINE_BUILD_TESTS "Build unit tests" ON)
option(ENABLE_SANITIZERS "Enable ASan (and UBSan when available)" OFF)

if(ENABLE_SANITIZERS)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address /INCREMENTAL:NO)

    # ASan is incompatible with /RTC1, which CMake's Debug config injects by
    # default
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

    # MSVC STL auto-enables container-overflow annotations under
    # /fsanitize=address, which changes std::string/vector/optional ABI. vcpkg's
    # prebuilt libs (Catch2, etc.) aren't built with this, so leaving it on
    # causes LNK2038 ABI-mismatch errors when linking against them. Disable to
    # match their ABI.
    add_compile_definitions(_DISABLE_STL_ANNOTATION)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                        -fno-sanitize-recover=undefined -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  message(STATUS "ccache found, enabling compiler cache: ${CCACHE_PROGRAM}")
endif()

set(SOURCES
    src/main.cpp
    src/Model.cpp
    src/ModelData.cpp
    src/Camera.cpp
    src/Material.cpp
    src/PBRMaterial.cpp
    src/Texture.cpp
    src/Cubemap.cpp
    src/MaterialFactory.cpp
    src/ResourceManager.cpp
    src/TextureLoader.cpp
    src/CubemapLoader.cpp
    src/ModelLoader.cpp
    src/ModelManager.cpp
    src/Entity.cpp
    src/SceneComponent.cpp
    src/Transform.cpp
    src/Node.cpp
    src/Mesh.cpp
    src/CloudSystem.cpp
    src/XmlParser.cpp)

# On macOS the Vulkan validation layer only ships with the Vulkan SDK (vcpkg
# ships none) and is selected at runtime via VK_LAYER_PATH. vcpkg's own
# libvulkan loader is frequently a DIFFERENT version than the SDK's layer; that
# mismatch makes the loader recurse inside vkGetDeviceProcAddr (segfault during
# ImGui init). To keep the linked loader version in lock-step with the SDK
# validation layer, point find_package(Vulkan) at the SDK when VULKAN_SDK is
# set.
if(APPLE AND DEFINED ENV{VULKAN_SDK})
  set(Vulkan_LIBRARY
      "$ENV{VULKAN_SDK}/lib/libvulkan.dylib"
      CACHE FILEPATH "" FORCE)
  set(Vulkan_INCLUDE_DIR
      "$ENV{VULKAN_SDK}/include"
      CACHE PATH "" FORCE)
endif()

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
  ImGuiFileDialog
  GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
  GIT_TAG v0.6.7)
FetchContent_MakeAvailable(ImGuiFileDialog)
target_link_libraries(ImGuiFileDialog PRIVATE imgui::imgui)
set_target_properties(ImGuiFileDialog PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  target_compile_options(ImGuiFileDialog PRIVATE /w)
else()
  target_compile_options(ImGuiFileDialog PRIVATE -w)
endif()

# Must exist before any engine_module() call, since each module attaches its
# own header check to it.
engine_header_self_containment_init()

add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/rhi)

# clang-format's output is not stable across major versions, so the version is
# pinned rather than left to whatever is on PATH. Without the pin, the nine CI
# configurations run three different clang-formats (the Ubuntu image's, Homebrew's
# and the Windows image's LLVM) and disagree with each other and with developers —
# which is exactly how a commit lands green on macOS and Windows and red on Linux.
#
# The pinned version lives in .clang-format-version so that this file and the CI
# workflow read the same number. Bumping it means bumping that file and
# reformatting the tree in the same commit.
file(READ ${CMAKE_SOURCE_DIR}/.clang-format-version EXPECTED_CLANG_FORMAT_VERSION)
string(STRIP "${EXPECTED_CLANG_FORMAT_VERSION}" EXPECTED_CLANG_FORMAT_VERSION)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  execute_process(
    COMMAND ${CLANG_FORMAT_EXE} --version
    OUTPUT_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" FOUND_CLANG_FORMAT_VERSION
               "${CLANG_FORMAT_VERSION_OUTPUT}")

  # A warning rather than an error: a mismatched clang-format still builds and
  # runs everything else, and refusing to configure over a formatting tool would
  # be worse than the problem. CI installs the pin, so CI stays authoritative.
  if(NOT FOUND_CLANG_FORMAT_VERSION VERSION_EQUAL EXPECTED_CLANG_FORMAT_VERSION)
    message(
      WARNING
        "clang-format ${FOUND_CLANG_FORMAT_VERSION} found, but this project is "
        "formatted with ${EXPECTED_CLANG_FORMAT_VERSION}. The 'format' and "
        "'format-check' targets will disagree with CI. Install the pinned "
        "version with:\n"
        "    pip install clang-format==${EXPECTED_CLANG_FORMAT_VERSION}")
  endif()
  file(
    GLOB_RECURSE
    FORMAT_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/engine/*/include/*.h
    ${CMAKE_SOURCE_DIR}/engine/*/include/*.hpp
    ${CMAKE_SOURCE_DIR}/engine/*/src/*.cpp
    ${CMAKE_SOURCE_DIR}/engine/*/src/*.h
    ${CMAKE_SOURCE_DIR}/engine/*/src/*.hpp)

  add_custom_target(
    format
    COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format -i over source files..."
    VERBATIM)

  add_custom_target(
    format-check
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking clang-format compliance over source files..."
    VERBATIM)
else()
  message(
    STATUS
      "clang-format not found — 'format' and 'format-check' targets unavailable"
  )
endif()

if(ENGINE_BUILD_TESTS)
  enable_testing()
  find_package(Catch2 CONFIG REQUIRED)
  include(cmake/Testing.cmake)
  add_subdirectory(tests)
endif()

# The application's own headers. Engine modules attach their own checks from
# engine_module(), so this covers src/ only.
file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)

# pch.h is deliberately excluded — it exists specifically to be force-included
# ahead of everything else and isn't meant to stand alone.
list(REMOVE_ITEM APP_HEADERS ${CMAKE_SOURCE_DIR}/src/pch.h)

engine_header_self_containment(
  App
  HEADERS
  ${APP_HEADERS}
  LINK_LIBRARIES
  Engine::Core
  Engine::Platform
  Engine::RHI
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  glm::glm-header-only
  assimp::assimp
  imgui::imgui
  pugixml::pugixml
  ImGuiFileDialog
  INCLUDE_DIRECTORIES
  src
  ${Stb_INCLUDE_DIR}
  COMPILE_DEFINITIONS
  VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_ENABLE_EXPERIMENTAL
  $<$<CONFIG:Debug>:DEBUG>)

add_executable(VulkanApp)

target_precompile_headers(VulkanApp PRIVATE src/pch.h)

target_sources(VulkanApp PRIVATE ${SOURCES})

target_link_libraries(
  VulkanApp
  PRIVATE Engine::Core
          Engine::Platform
          Engine::RHI
          SDL3::SDL3
          Vulkan::Vulkan
          GPUOpen::VulkanMemoryAllocator
          glm::glm-header-only
          assimp::assimp
          imgui::imgui
          pugixml::pugixml
          ImGuiFileDialog)

target_include_directories(VulkanApp PRIVATE src ${Stb_INCLUDE_DIR})

target_compile_definitions(
  VulkanApp
  PRIVATE VULKAN_HPP_NO_STRUCT_CONSTRUCTORS GLM_FORCE_DEPTH_ZERO_TO_ONE
          GLM_ENABLE_EXPERIMENTAL $<$<CONFIG:Debug>:DEBUG>)

if(MSVC)
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    # MSBuild batches TUs into one cl invocation, so /MP is what parallelises.
    target_compile_options(VulkanApp PRIVATE /MP)
  else()
    # Ninja parallelises itself; /Z7 embeds debug info in each .obj and avoids
    # serialising all TUs through mspdbsrv writing a shared PDB.
    set_target_properties(VulkanApp PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT
                                               "$<$<CONFIG:Debug>:Embedded>")
  endif()

  target_compile_options(VulkanApp PRIVATE $<$<CONFIG:Debug>:/Od>
                                           $<$<CONFIG:Release>:/O2>)

  target_link_options(
    VulkanApp PRIVATE $<$<CONFIG:Debug>:/DEBUG> # optional but useful for crash
                                                # reports
    $<$<CONFIG:Release>:/DEBUG>)
else()
  target_compile_options(
    VulkanApp
    PRIVATE $<$<CONFIG:Debug>:
            -O0
            -g
            >
            $<$<CONFIG:Release>:
            -O3
            >)

  # lld links noticeably faster than bfd, which matters most for the ASan/UBSan
  # build (large instrumented runtime + debug info to link). Not used on Apple
  # platforms since Xcode's ld already handles this well and lld's Mach-O
  # support is less mature.
  if(NOT APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" LLD_LINKER_AVAILABLE)
    if(LLD_LINKER_AVAILABLE)
      target_link_options(VulkanApp PRIVATE -fuse-ld=lld)
    endif()
  endif()
endif()

engine_set_warnings(VulkanApp)

include(cmake/Shaders.cmake)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT
                                                            VulkanApp)
set_target_properties(VulkanApp PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                           "${CMAKE_SOURCE_DIR}")
