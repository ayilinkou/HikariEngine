# Formats the tree, or checks that it is formatted.
#
#   cmake -P cmake/Format.cmake              # check, non-zero exit on a diff
#   cmake -DFIX=ON -P cmake/Format.cmake     # rewrite in place
#
# A standalone script rather than only a build target, because checking
# formatting needs nothing but clang-format and the sources: no configure, no
# vcpkg, no Vulkan SDK, no compiler. As a target it could only run inside a
# configured tree, which in CI meant waiting for a full build of a matrix
# configuration to learn that a file needed reindenting. The build still exposes
# `format` and `format-check`; both delegate here, so there is one file list and
# one invocation rather than two that drift.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/ClangFormatUtil.cmake")

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

hikari_find_clang_format(clang_format_exe clang_format_version)
if(NOT clang_format_exe)
  message(FATAL_ERROR "format: clang-format not found on PATH.")
endif()

file(
  GLOB_RECURSE
  sources
  "${repo_root}/src/*.cpp"
  "${repo_root}/src/*.h"
  "${repo_root}/src/*.hpp"
  "${repo_root}/engine/*/include/*.h"
  "${repo_root}/engine/*/include/*.hpp"
  "${repo_root}/engine/*/src/*.cpp"
  "${repo_root}/engine/*/src/*.h"
  "${repo_root}/engine/*/src/*.hpp")

if(NOT sources)
  message(FATAL_ERROR "format: no sources found under ${repo_root}.")
endif()

list(LENGTH sources source_count)

if(FIX)
  execute_process(
    COMMAND ${clang_format_exe} -i ${sources}
    WORKING_DIRECTORY "${repo_root}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "format: clang-format failed over ${source_count} file(s).")
  endif()
  message(STATUS "format: reformatted ${source_count} file(s) with clang-format ${clang_format_version}.")
else()
  execute_process(
    COMMAND ${clang_format_exe} --dry-run --Werror ${sources}
    WORKING_DIRECTORY "${repo_root}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(
      FATAL_ERROR
        "format_check: ${source_count} file(s) checked with clang-format ${clang_format_version}; the diffs above are what `scripts/format.sh` would write."
    )
  endif()
  message(STATUS "format_check: ${source_count} file(s) match clang-format ${clang_format_version}.")
endif()
