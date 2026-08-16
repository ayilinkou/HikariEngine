# Compiles every header on its own, with no precompiled header, to prove it
# stands alone rather than relying on whatever a .cpp happened to include first.
#
# There is one check target per layer rather than a single target compiling
# everything, because each one links only what its own layer is allowed to
# link. A single target linking the union would let a core/ header include
# assimp and still pass.
#
# That makes this a partial layering check too, but only for dependencies the
# compiler reaches through CMake. A dependency that also happens to be
# installed in the default system include path is found regardless of what a
# target links — on a typical Arch box that covers Vulkan, SDL3 and pugixml —
# so a clean run here is not proof that a layer is clean. The build-system
# layering in §8 of the architecture plan is the real enforcement; this is a
# cheap extra net.

# Creates the aggregate target the per-layer checks attach themselves to. Must
# be called after project() and before the first engine_header_self_containment.
function(engine_header_self_containment_init)
  add_custom_target(HeaderSelfContainment
                    COMMENT "Compiling every header on its own (no PCH)...")
endfunction()

# engine_header_self_containment(<name>
#   HEADERS <h1> [<h2> ...]
#   [LINK_LIBRARIES <lib> ...]
#   [INCLUDE_DIRECTORIES <dir> ...]
#   [COMPILE_DEFINITIONS <def> ...])
function(engine_header_self_containment name)
  cmake_parse_arguments(
    CHECK "" ""
    "HEADERS;LINK_LIBRARIES;INCLUDE_DIRECTORIES;COMPILE_DEFINITIONS" ${ARGN})

  if(NOT CHECK_HEADERS)
    return()
  endif()

  set(target HeaderSelfContainment_${name})
  set(stub_dir ${CMAKE_BINARY_DIR}/header_self_containment/${name})

  # One .cpp stub per header that does nothing but include it. Compiling via a
  # stub, rather than compiling the .h directly as its own TU, avoids the
  # "#pragma once in main file" warning.
  set(stubs "")
  foreach(header ${CHECK_HEADERS})
    file(RELATIVE_PATH rel_header ${CMAKE_SOURCE_DIR} ${header})
    string(REPLACE "/" "_" stub_name ${rel_header})
    set(stub_file ${stub_dir}/${stub_name}.cpp)
    set(stub_content "#include \"${header}\"\n")

    # Only write when the content actually differs. file(WRITE) always updates
    # the timestamp, and every configure would then invalidate every stub
    # object — making the whole check rebuild from scratch on each run rather
    # than only for the headers that changed.
    set(existing_content "")
    if(EXISTS ${stub_file})
      file(READ ${stub_file} existing_content)
    endif()

    if(NOT existing_content STREQUAL stub_content)
      file(WRITE ${stub_file} "${stub_content}")
    endif()

    list(APPEND stubs ${stub_file})
  endforeach()

  # Passing the stubs is load-bearing: CMake classifies a .h as a header and
  # never compiles it, so a target given the headers builds nothing at all.
  add_library(${target} OBJECT EXCLUDE_FROM_ALL ${stubs})

  # Deliberately no target_precompile_headers here — the PCH is exactly what
  # this target exists to do without.

  if(CHECK_LINK_LIBRARIES)
    target_link_libraries(${target} PRIVATE ${CHECK_LINK_LIBRARIES})
  endif()

  if(CHECK_INCLUDE_DIRECTORIES)
    target_include_directories(${target} PRIVATE ${CHECK_INCLUDE_DIRECTORIES})
  endif()

  if(CHECK_COMPILE_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${CHECK_COMPILE_DEFINITIONS})
  endif()

  add_dependencies(HeaderSelfContainment ${target})
endfunction()
