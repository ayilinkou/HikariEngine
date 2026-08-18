engine_test(core_tests SOURCES unit/core/SwapbackArrayTests.cpp
  unit/core/LogTests.cpp unit/core/JobSystemTests.cpp
  unit/core/HandleTests.cpp LIBS Engine::Core)

target_include_directories(core_tests
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

target_compile_definitions(
  core_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/")

# Separate from core_tests so that Core's tests keep linking nothing but Core.
engine_test(platform_tests SOURCES unit/platform/PathsTests.cpp
  unit/platform/CommandLineTests.cpp LIBS Engine::Platform)

engine_test(rhi_tests SOURCES unit/rhi/ConversionTests.cpp
  unit/rhi/QueueFamilyTests.cpp LIBS Engine::RHI)

# The conversion tables and the queue-family selection live in src/vulkan/, not
# in the module's public include path — they are backend-internal by design
# (plan D1), so testing them means reaching into the module deliberately rather
# than promoting them to public headers just to make them reachable.
target_include_directories(rhi_tests
                           PRIVATE ${CMAKE_SOURCE_DIR}/engine/rhi/src)
