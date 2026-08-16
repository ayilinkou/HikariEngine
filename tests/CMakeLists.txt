engine_test(core_tests SOURCES unit/core/SwapbackArrayTests.cpp
  unit/core/LogTests.cpp unit/core/JobSystemTests.cpp LIBS Engine::Core)

target_include_directories(core_tests
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

target_compile_definitions(
  core_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/")

# Separate from core_tests so that Core's tests keep linking nothing but Core.
engine_test(platform_tests SOURCES unit/platform/PathsTests.cpp
  unit/platform/CommandLineTests.cpp LIBS Engine::Platform)
