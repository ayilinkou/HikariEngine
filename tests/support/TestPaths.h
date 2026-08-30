#pragma once

/**
 * TEST_DATA_DIR is injected via target_compile_definitions (see
 * tests/CMakeLists.txt) as an absolute path to tests/data/, so test data can
 * be referenced regardless of the working directory ctest is invoked from.
 */
inline constexpr const char* TestDataDir()
{
    return TEST_DATA_DIR;
}
