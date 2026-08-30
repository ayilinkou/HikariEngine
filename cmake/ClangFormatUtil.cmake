# Finding clang-format and checking it against the pin.
#
# Shared by the build's configure step and by cmake/Format.cmake, which runs
# without a configured tree so that CI can check formatting on a bare runner —
# no vcpkg, no Vulkan SDK, no compiler. Two copies of the version comparison
# would drift, and a check that silently accepts the wrong clang-format is worse
# than no check: the tree would be reformatted by whichever version ran last.

# Sets <exe_var> to the clang-format executable ("" when none is installed) and
# <version_var> to its version. Warns when that version is not the pinned one.
function(hikari_find_clang_format exe_var version_var)
  get_filename_component(repo_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" DIRECTORY)
  file(READ "${repo_root}/.clang-format-version" expected_version)
  string(STRIP "${expected_version}" expected_version)

  find_program(CLANG_FORMAT_EXE NAMES clang-format)
  if(NOT CLANG_FORMAT_EXE)
    set(${exe_var} "" PARENT_SCOPE)
    set(${version_var} "" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND ${CLANG_FORMAT_EXE} --version
    OUTPUT_VARIABLE version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" found_version "${version_output}")

  # A warning rather than an error: a mismatched clang-format still builds and
  # runs everything else, and refusing to proceed over a formatting tool would
  # be worse than the problem. CI installs the pin, so CI stays authoritative.
  if(NOT found_version VERSION_EQUAL expected_version)
    message(
      WARNING
        "clang-format ${found_version} found, but this project is formatted with "
        "${expected_version}. The 'format' and 'format-check' targets will disagree "
        "with CI. Install the pinned version with:\n"
        "    pip install clang-format==${expected_version}")
  endif()

  set(${exe_var} "${CLANG_FORMAT_EXE}" PARENT_SCOPE)
  set(${version_var} "${found_version}" PARENT_SCOPE)
endfunction()
