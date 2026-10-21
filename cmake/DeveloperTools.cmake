# Python is optional for configuring and building the engine. Only explicitly
# requested developer-tool targets require it; policy and file selection live in
# Python, shared with the standalone commands and CI.
function(hikari_add_developer_tools repo_root)
  find_package(Python3 3.12 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${repo_root}/scripts/format.py"
              --check-version WORKING_DIRECTORY "${repo_root}")
  else()
    message(
      STATUS
        "Python 3.12+ not found; developer tools require it, engine builds do not"
    )
  endif()

  foreach(tool IN ITEMS format format-check)
    if(tool STREQUAL "format")
      set(script "scripts/format.py")
    else()
      set(script "tests/scripts/format_check.py")
    endif()
    if(Python3_Interpreter_FOUND)
      add_custom_target(
        ${tool}
        COMMAND "${Python3_EXECUTABLE}" "${repo_root}/${script}"
        WORKING_DIRECTORY "${repo_root}"
        USES_TERMINAL VERBATIM)
    else()
      add_custom_target(
        ${tool}
        COMMAND
          "${CMAKE_COMMAND}" -E echo
          "${tool} requires Python 3.12+. Install it and rerun CMake configuration."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM)
    endif()
  endforeach()
endfunction()
