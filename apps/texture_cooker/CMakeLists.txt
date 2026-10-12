cmake_minimum_required(VERSION 4.0)

# This independent host-tool build is shared by every engine configuration.
set(CMAKE_BUILD_TYPE Release CACHE STRING "Cooker configuration" FORCE)
set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "Cooker configurations" FORCE)
project(HikariTextureCooker LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

get_filename_component(HIKARI_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${HIKARI_ENGINE_SOURCE_DIR}/cmake/Warnings.cmake")
include("${HIKARI_ENGINE_SOURCE_DIR}/cmake/HeaderSelfContainment.cmake")
include("${HIKARI_ENGINE_SOURCE_DIR}/cmake/EngineModule.cmake")
include("${HIKARI_ENGINE_SOURCE_DIR}/cmake/BuildOptions.cmake")
find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include("${HIKARI_ENGINE_SOURCE_DIR}/cmake/TextureTools.cmake")
engine_header_self_containment_init()
add_subdirectory("${HIKARI_ENGINE_SOURCE_DIR}/engine/core" engine/core)
add_subdirectory("${HIKARI_ENGINE_SOURCE_DIR}/engine/platform" engine/platform)
add_subdirectory("${HIKARI_ENGINE_SOURCE_DIR}/engine/asset" engine/asset)

add_library(TextureCookerCli STATIC TextureCookerCli.cpp)
target_link_libraries(TextureCookerCli PUBLIC Engine::Asset Engine::Platform)
engine_set_warnings(TextureCookerCli)

add_executable(HikariTextureCooker main.cpp)
target_link_libraries(HikariTextureCooker PRIVATE TextureCookerCli)
target_compile_definitions(HikariTextureCooker PRIVATE
  HIKARI_COOKER_CONTENT_DIR="${HIKARI_ENGINE_SOURCE_DIR}/content")
# A generator expression suppresses multi-config generators' automatic suffix.
set_target_properties(HikariTextureCooker PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<0:>")
engine_set_warnings(HikariTextureCooker)
hikari_apply_build_options(HikariTextureCooker)
