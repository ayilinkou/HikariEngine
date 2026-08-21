# TODO: make shaders only depend on the headers they actually include.

find_program(SLANGC_EXE slangc)
if(NOT SLANGC_EXE)
  message(FATAL_ERROR "slangc not found!")
endif()

function(add_slang_shader_target target)
  cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})

  set(shaders_source_dir ${CMAKE_SOURCE_DIR}/src/shaders)
  set(shaders_out_dir ${CMAKE_SOURCE_DIR}/content/shaders)

  file(GLOB_RECURSE shader_headers CONFIGURE_DEPENDS
       ${shaders_source_dir}/*.slangh)

  set(spv_outputs "")
  foreach(shader ${SHADER_SOURCES})
    file(RELATIVE_PATH rel_path ${shaders_source_dir} ${shader})
    set(output_file ${shaders_out_dir}/${rel_path})
    string(REPLACE ".slang" ".spv" output_file ${output_file})
    get_filename_component(output_dir ${output_file} DIRECTORY)

    if(shader MATCHES "\\.comp\\.slang$")
      set(entry_points -entry main)
    else()
      set(entry_points -entry vertMain -entry fragMain)
    endif()

    add_custom_command(
      OUTPUT ${output_file}
      COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${rel_path}"
      COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
      COMMAND
        ${SLANGC_EXE} ${shader} -target spirv -profile spirv_1_4
        -emit-spirv-directly -warnings-as-errors all -fvk-use-entrypoint-name ${entry_points} -o
        ${output_file} $<IF:$<CONFIG:Debug>,-g1,-g0>
        $<IF:$<CONFIG:Debug>,-O0,-O3>
      DEPENDS ${shader} ${shader_headers}
      COMMENT "Compiling shader ${rel_path}"
      VERBATIM)

    list(APPEND spv_outputs ${output_file})
  endforeach()

  add_custom_target(${target} ALL DEPENDS ${spv_outputs})
endfunction()

file(GLOB_RECURSE shader_slang_sources CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/shaders/*.slang)

add_slang_shader_target(CompileShadersTarget SOURCES ${shader_slang_sources})
add_dependencies(VulkanApp CompileShadersTarget)
