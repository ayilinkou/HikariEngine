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

option(ENABLE_SANITIZERS "Enable ASan (and UBSan when available)" OFF)

if(ENABLE_SANITIZERS)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address /INCREMENTAL:NO)

    # ASan is incompatible with /RTC1, which CMake's Debug config injects by
    # default
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
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
    src/Texture.cpp
    src/Cubemap.cpp
    src/Camera.cpp
    src/Material.cpp
    src/PBRMaterial.cpp
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
    src/ThreadPool.cpp
    src/CloudSystem.cpp
    src/XmlParser.cpp
    src/VMAImpl.cpp
    src/AllocatedBuffer.cpp
    src/AllocatedImage.cpp
    src/PipelineBuilder.cpp
    src/ComputePipelineBuilder.cpp)

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

find_program(SLANGC_EXECUTABLE slangc)
if(NOT SLANGC_EXECUTABLE)
  message(FATAL_ERROR "slangc not found!")
endif()

add_executable(VulkanApp)

target_precompile_headers(VulkanApp PRIVATE src/pch.h)

target_sources(VulkanApp PRIVATE ${SOURCES})

target_link_libraries(
  VulkanApp
  PRIVATE SDL3::SDL3
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

  target_compile_options(
    VulkanApp PRIVATE /W3 # Warning level
                      $<$<CONFIG:Debug>:/Od> $<$<CONFIG:Release>:/O2>)

  target_link_options(
    VulkanApp PRIVATE $<$<CONFIG:Debug>:/DEBUG> # optional but useful for crash
                                                # reports
    $<$<CONFIG:Release>:/DEBUG>)
else()
  target_compile_options(
    VulkanApp
    PRIVATE -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            $<$<CONFIG:Debug>:
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

# TODO: make shaders only depend on headers that they actually include
function(add_slang_shader_target target)
  cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})

  set(shaders_source_dir ${CMAKE_CURRENT_LIST_DIR}/src/shaders)
  set(shaders_out_dir ${CMAKE_CURRENT_LIST_DIR}/shaders)

  file(GLOB_RECURSE shader_headers CONFIGURE_DEPENDS
       ${shaders_source_dir}/*.slangh)

  set(spv_outputs "")
  foreach(shader ${SHADER_SOURCES})
    file(RELATIVE_PATH rel_path ${shaders_source_dir} ${shader})
    set(output_file ${shaders_out_dir}/${rel_path})
    string(REPLACE ".slang" ".spv" output_file ${output_file})
    get_filename_component(output_dir ${output_file} DIRECTORY)

    if(shader MATCHES "\\.comp\\.slang$")
      set(entry_points -entry main)
    else()
      set(entry_points -entry vertMain -entry fragMain)
    endif()

    add_custom_command(
      OUTPUT ${output_file}
      COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${rel_path}"
      COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
      COMMAND
        ${SLANGC_EXECUTABLE} ${shader} -target spirv -profile spirv_1_4
        -emit-spirv-directly -fvk-use-entrypoint-name ${entry_points} -o
        ${output_file} $<IF:$<CONFIG:Debug>,-g1,-g0>
        $<IF:$<CONFIG:Debug>,-O0,-O3>
      DEPENDS ${shader} ${shader_headers}
      COMMENT "Compiling shader ${rel_path}"
      VERBATIM)

    list(APPEND spv_outputs ${output_file})
  endforeach()

  add_custom_target(${target} ALL DEPENDS ${spv_outputs})
endfunction()

file(GLOB_RECURSE shader_slang_sources CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_LIST_DIR}/src/shaders/*.slang)

add_slang_shader_target(CompileShadersTarget SOURCES ${shader_slang_sources})
add_dependencies(VulkanApp CompileShadersTarget)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT
                                                            VulkanApp)
set_target_properties(VulkanApp PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                           "${CMAKE_SOURCE_DIR}")
