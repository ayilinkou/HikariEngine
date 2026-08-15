engine_test(core_tests SOURCES unit/core/SwapbackArrayTests.cpp
            unit/core/LogTests.cpp LIBS Engine::Core)

target_include_directories(core_tests
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

target_compile_definitions(
  core_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/")
