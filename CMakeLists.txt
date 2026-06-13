cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

# Only generate Debug and Release configurations
set(CMAKE_CONFIGURATION_TYPES
    "Debug;Release"
    CACHE STRING "" FORCE)

option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=undefined -g)
  add_link_options(-fsanitize=address,undefined)
endif()

project(VulkanApp VERSION 1.0)

set(SOURCES
    src/main.cpp
    src/Model.cpp
    src/ModelData.cpp
    src/Texture.cpp
    src/Camera.cpp
    src/Material.cpp
    src/PBRMaterial.cpp
    src/MaterialFactory.cpp
    src/ResourceManager.cpp
    src/TextureLoader.cpp
    src/ModelLoader.cpp
    src/ModelManager.cpp
    src/GameObject.cpp
    src/SceneComponent.cpp
    src/Transform.cpp
    src/Node.cpp
    src/Mesh.cpp)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

find_program(SLANGC_EXECUTABLE slangc)
if(NOT SLANGC_EXECUTABLE)
  message(FATAL_ERROR "slangc not found!")
endif()

add_executable(VulkanApp)

target_precompile_headers(VulkanApp PRIVATE src/pch.h)

target_sources(VulkanApp PRIVATE ${SOURCES})

target_link_libraries(
  VulkanApp PRIVATE SDL3::SDL3 Vulkan::Vulkan glm::glm-header-only
                    assimp::assimp imgui::imgui)

target_include_directories(VulkanApp PRIVATE src ${Stb_INCLUDE_DIR})

target_compile_definitions(
  VulkanApp
  PRIVATE VULKAN_HPP_NO_STRUCT_CONSTRUCTORS GLM_FORCE_DEPTH_ZERO_TO_ONE
          GLM_ENABLE_EXPERIMENTAL $<$<CONFIG:Debug>:DEBUG>)

if(MSVC)
  target_compile_options(
    VulkanApp
    PRIVATE /W3 # Warning level
            /MP # Multi-processor compilation
            /WX # Warnings as errors
            $<$<CONFIG:Debug>:
            /Od # No optimisation
            /Zi # Debug info (PDB)
            >
            $<$<CONFIG:Release>:
            /O2 # Full optimisation
            >)

  target_link_options(
    VulkanApp
    PRIVATE
    $<$<CONFIG:Debug>:
    /DEBUG
    >
    # optional but useful for crash reports
    $<$<CONFIG:Release>:
    /DEBUG
    >)
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
endif()

file(GLOB_RECURSE shader_slang_sources CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_LIST_DIR}/src/shaders/*.slang)

function(add_slang_shader_target target)
  cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})

  set(shaders_dir ${CMAKE_CURRENT_LIST_DIR}/shaders)
  set(shaders_source_dir ${CMAKE_CURRENT_LIST_DIR}/src/shaders)
  set(entry_points -entry vertMain -entry fragMain)

  set(outputs)

  foreach(shader ${SHADER_SOURCES})
    message(STATUS "Found shader: ${shader}")

    file(RELATIVE_PATH rel_path ${shaders_source_dir} ${shader})

    set(output_file ${shaders_dir}/${rel_path})
    string(REPLACE ".slang" ".spv" output_file ${output_file})

    get_filename_component(output_dir ${output_file} DIRECTORY)

    list(APPEND outputs ${output_file})

    add_custom_command(
      OUTPUT ${output_file}
      COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${rel_path}"
      COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
      COMMAND
        ${SLANGC_EXECUTABLE} ${shader} -target spirv -profile spirv_1_4
        -emit-spirv-directly -fvk-use-entrypoint-name ${entry_points} -o
        ${output_file}
      DEPENDS ${shader}
      VERBATIM)
  endforeach()

  add_custom_target(${target} DEPENDS ${outputs})
endfunction()

add_slang_shader_target(CompileShadersTarget SOURCES ${shader_slang_sources})
add_dependencies(VulkanApp CompileShadersTarget)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT
                                                            VulkanApp)
set_target_properties(VulkanApp PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                           "${CMAKE_SOURCE_DIR}")
