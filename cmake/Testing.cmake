# The backends a test binary can be registered against: Vulkan everywhere, and
# D3D12 where the RHI builds it — the same condition engine/rhi/CMakeLists.txt
# adds its sources under.
set(HIKARI_TEST_BACKENDS Vulkan)
if(WIN32)
  list(APPEND HIKARI_TEST_BACKENDS D3D12)
endif()

# engine_test(<name>
#   SOURCES <s1> [<s2> ...]
#   [LIBS <lib> ...]
#   [LABEL <label>]
#   [BACKEND_SPECS <backend> <test spec> [<backend> <test spec> ...]])
#
# LABEL is the CTest label every case in the binary is registered under, and
# defaults to "unit". It exists so that tests needing a GPU can be labelled
# "gpu" and left out of the run CI performs: a machine with no Vulkan ICD would
# report every one of them as skipped, which is indistinguishable from a run
# where the tests were silently doing nothing.
#
# BACKEND_SPECS registers the binary once per backend this build contains
# instead of once, each registration choosing its cases with a Catch2 test spec
# and telling the process which backend to run through HIKARI_TEST_BACKEND. One
# binary rather than one per backend, because a case that is not about a
# backend should not have to be written twice. The choice arrives from CTest
# rather than from a loop inside the binary, so a registration is one backend's
# run from start to finish, and CI can ask for one of them by label.
#
# Vulkan keeps the bare label. Every other backend's is the label suffixed with
# its lower-case name — "gpu-d3d12" — so that `ctest -L gpu`, a regular
# expression, still runs everything a machine has, and `ctest -L gpu-d3d12`
# runs one backend alone. A backend named here that the build lacks is left
# out, which is what a Linux build does with D3D12.
function(engine_test test_name)
  cmake_parse_arguments(TEST "" "LABEL" "SOURCES;LIBS;BACKEND_SPECS" ${ARGN})

  if(NOT TEST_LABEL)
    set(TEST_LABEL "unit")
  endif()

  add_executable(${test_name} ${TEST_SOURCES})

  # SanitizerShims is linked directly rather than inherited: an OBJECT
  # library's files reach only the targets that name it. See
  # engine/core/CMakeLists.txt.
  target_link_libraries(${test_name} PRIVATE Catch2::Catch2WithMain
                                             SanitizerShims ${TEST_LIBS})

  engine_set_warnings(${test_name})

  include(Catch)

  # Catch2 returns 4 from a run in which every case skipped, and nothing else
  # returns it — a filter matching no test at all is 2, a failure is 42. Telling
  # CTest that makes a GPU test on a machine with no ICD report as skipped
  # rather than as passed, which is the difference between a run that proved
  # something and one that could not.
  if(NOT TEST_BACKEND_SPECS)
    catch_discover_tests(${test_name} PROPERTIES LABELS "${TEST_LABEL}"
                         SKIP_RETURN_CODE 4)
    return()
  endif()

  list(LENGTH TEST_BACKEND_SPECS spec_count)
  math(EXPR last_pair "${spec_count} / 2 - 1")
  foreach(pair RANGE ${last_pair})
    math(EXPR backend_index "${pair} * 2")
    math(EXPR spec_index "${pair} * 2 + 1")
    list(GET TEST_BACKEND_SPECS ${backend_index} backend)
    list(GET TEST_BACKEND_SPECS ${spec_index} spec)

    if(NOT backend IN_LIST HIKARI_TEST_BACKENDS)
      continue()
    endif()

    if(backend STREQUAL "Vulkan")
      set(backend_label "${TEST_LABEL}")
    else()
      string(TOLOWER "${backend}" backend_lower)
      set(backend_label "${TEST_LABEL}-${backend_lower}")
    endif()

    # Each registration is distinguished by its spec — Catch.cmake names its
    # generated test list after a hash of it — so two backends must not share
    # one.
    catch_discover_tests(
      ${test_name}
      TEST_SPEC "${spec}"
      TEST_SUFFIX " (${backend})"
      PROPERTIES LABELS "${backend_label}" ENVIRONMENT "HIKARI_TEST_BACKEND=${backend}"
                 SKIP_RETURN_CODE 4)
  endforeach()
endfunction()
