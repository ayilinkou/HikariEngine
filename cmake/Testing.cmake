function(engine_test test_name)
  cmake_parse_arguments(TEST "" "" "SOURCES;LIBS" ${ARGN})

  add_executable(${test_name} ${TEST_SOURCES})

  target_link_libraries(${test_name} PRIVATE Catch2::Catch2WithMain
                                             ${TEST_LIBS})

  engine_set_warnings(${test_name})

  include(Catch)
  catch_discover_tests(${test_name} PROPERTIES LABELS "unit")
endfunction()
