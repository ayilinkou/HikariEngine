# engine_module(<name> [SOURCES <src1> <src2> ...] [LINK_LIBRARIES <lib1> <lib2>
# ...])
#
# Declares an engine module living at engine/<lowercased name>/, with public
# headers under include/<name>/ (lowercase folder), matching the include-site
# convention #include <core/Timer.h>.
#
# Header-only modules (no SOURCES) become INTERFACE libraries — nothing to
# compile, no warnings to apply, they just carry an include path and any
# LINK_LIBRARIES through to whatever links them.
#
# Modules with SOURCES become STATIC libraries and get engine_set_warnings
# applied like any other compiled target.

function(engine_module target)
  cmake_parse_arguments(MODULE "" "" "SOURCES;LINK_LIBRARIES" ${ARGN})

  if(MODULE_SOURCES)
    add_library(${target} STATIC ${MODULE_SOURCES})
    set(visibility PUBLIC)
    engine_set_warnings(${target})
  else()
    add_library(${target} INTERFACE)
    set(visibility INTERFACE)
  endif()

  target_include_directories(
    ${target} ${visibility}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

  if(MODULE_LINK_LIBRARIES)
    target_link_libraries(${target} ${visibility} ${MODULE_LINK_LIBRARIES})
  endif()

  add_library(Engine::${target} ALIAS ${target})

  # Check this module's public headers, linking nothing but the module itself —
  # so the check is bounded by exactly the dependencies declared above, and a
  # header that reaches outside its layer fails to compile.
  file(GLOB_RECURSE module_headers CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
       ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

  engine_header_self_containment(${target} HEADERS ${module_headers}
                                 LINK_LIBRARIES ${target})
endfunction()
