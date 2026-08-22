# engine_test(<name>
#   SOURCES <s1> [<s2> ...]
#   [LIBS <lib> ...]
#   [LABEL <label>])
#
# LABEL is the CTest label every case in the binary is registered under, and
# defaults to "unit". It exists so that tests needing a GPU can be labelled
# "gpu" and left out of the run CI performs: a machine with no Vulkan ICD would
# report every one of them as skipped, which is indistinguishable from a run
# where the tests were silently doing nothing.
function(engine_test test_name)
  cmake_parse_arguments(TEST "" "LABEL" "SOURCES;LIBS" ${ARGN})

  if(NOT TEST_LABEL)
    set(TEST_LABEL "unit")
  endif()

  add_executable(${test_name} ${TEST_SOURCES})

  target_link_libraries(${test_name} PRIVATE Catch2::Catch2WithMain
                                             ${TEST_LIBS})

  engine_set_warnings(${test_name})

  include(Catch)

  # Catch2 returns 4 from a run in which every case skipped, and nothing else
  # returns it — a filter matching no test at all is 2, a failure is 42. Telling
  # CTest that makes a GPU test on a machine with no ICD report as skipped
  # rather than as passed, which is the difference between a run that proved
  # something and one that could not.
  catch_discover_tests(${test_name} PROPERTIES LABELS "${TEST_LABEL}"
                       SKIP_RETURN_CODE 4)
endfunction()
