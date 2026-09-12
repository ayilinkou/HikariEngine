# Both shader tools come from vcpkg, which is what keeps the Vulkan SDK off the
# list of things a build needs. vcpkg's toolchain appends its tools directories
# to CMAKE_PROGRAM_PATH, and find_program searches CMake variables ahead of the
# environment's PATH, so these resolve to the versions vcpkg.json pins rather
# than to whichever copy a developer happens to have installed.
find_program(SLANGC_EXE slangc)
if(NOT SLANGC_EXE)
  message(FATAL_ERROR "slangc not found! It is provided by the shader-slang vcpkg port.")
endif()

# The Vulkan specification makes valid SPIR-V the application's responsibility,
# not the driver's — VUID-VkShaderModuleCreateInfo-pCode-08736 — so a driver is
# free to assume validity and skip checking. Invalid SPIR-V is then undefined
# behaviour that renders correctly on the driver it was written against and
# fails somewhere else.
#
# The validation layers do call spirv-val at runtime, but only on the modules a
# run actually creates, and only where a Vulkan ICD exists. CI has none, so the
# GPU tests skip there and the layers never load: this is the only shader
# correctness check CI can run. slangc does not stand in for it — it accepts
# modules spirv-val rejects.
#
# Fatal rather than optional, because a check that silently disappears on one of
# the six CI configurations is worse than no check: the build stays green and
# the coverage is imaginary. Depending on the spirv-tools port's "tools" feature
# rather than on an SDK install is what makes it present everywhere.
find_program(SPIRV_VAL_EXE spirv-val)
if(NOT SPIRV_VAL_EXE)
  message(FATAL_ERROR "spirv-val not found! It is provided by the spirv-tools vcpkg port.")
endif()

# Compiles .slang sources to SPIR-V beside the executable that loads them.
#
# The output directory is HIKARI_EXE_DIR/shaders — set next to
# add_executable(HikariEngine), so the two cannot drift — rather than a fixed
# path in the source tree, and that is load-bearing. Debug and Release
# compile the same sources with different flags (-O0 -g1 against -O3 -g0), so a
# shared output directory means the two configurations overwrite each other's
# work. Worse, they do it silently: each build directory judges the .spv
# up to date from its own records, and once both have built once, neither
# rebuilds — leaving whichever configuration ran last in place and every
# subsequent build a no-op. A debug session then runs optimized, stripped
# shaders while reporting success.
#
# It is deliberately <exe dir>/shaders and not <exe dir>/content/shaders.
# Paths tries <exe dir>/content as a content root candidate before the source
# tree, so creating that directory here would make the build directory look
# like a content root — an incomplete one, with no models, scenes or textures —
# and asset loading would resolve to it and fail.
function(add_slang_shader_target target)
  cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})

  # Emitted on every platform, not only on Windows (plan D27). Slang and DXC both
  # come from vcpkg as host dependencies, so a Linux build compiles the DXIL a
  # Windows build will run — which means a shader that cannot be expressed in
  # both is caught by whichever CI job runs first, rather than by the person
  # writing the D3D12 backend. The shader model is the lowest every current
  # shader compiles at; raising it lifts the minimum hardware the D3D12 backend
  # will run on, so it waits until a shader needs it.
  set(dxil_profile sm_6_0)

  # Reflection rides on the compiles that ship rather than on a pass of its own
  # (plan §4.4), so the JSON describes exactly the blob that was produced, under
  # exactly the flags it was produced with. A separate invocation would drift the
  # first time a flag was added to one and not the other, and the test would go
  # on agreeing with a compile nobody runs.
  #
  # Build bookkeeping, so it stays in the build tree rather than in the directory
  # deployed beside the executable — the same argument the depfiles carry.
  # Nothing at run time reads it; the layout test does.
  set(reflection_dir ${CMAKE_CURRENT_BINARY_DIR}/shader_reflection/$<CONFIG>)
  set(HIKARI_SHADER_REFLECTION_DIR ${reflection_dir} PARENT_SCOPE)

  # What makes a single `: register(tN, spaceM)` annotation serve both APIs
  # (plan D29). slangc's own help: "For a resource attached with :register(bX,
  # <space>) but not [vk::binding(...)], sets its Vulkan descriptor set to
  # <space> and binding number to X + N." A shift of zero therefore makes the
  # Vulkan set the register space and the Vulkan binding the register index, so
  # the SPIR-V comes out with exactly the sets and bindings the attributes used
  # to spell — one annotation per declaration instead of two, in the vocabulary
  # D13 already chose, and the only one of the two that can express a space at
  # all. "all" applies the shift to every space rather than one.
  set(vulkan_register_shifts
      -fvk-b-shift 0 all
      -fvk-t-shift 0 all
      -fvk-s-shift 0 all
      -fvk-u-shift 0 all)

  set(shaders_source_dir ${CMAKE_SOURCE_DIR}/engine/engine/src/shaders)
  set(shaders_out_dir ${HIKARI_EXE_DIR}/shaders)

  set(spv_outputs "")
  foreach(shader ${SHADER_SOURCES})
    file(RELATIVE_PATH rel_path ${shaders_source_dir} ${shader})

    # One blob per stage, each holding exactly one entry point named main (plan
    # D24 and D33). The stage is part of the output name rather than of the
    # module's contents, so resolving a stage to a file is the same question on
    # both backends — D3D12's DXIL container cannot hold two entry points at
    # all, and its bytecode description is a pointer and a length with nowhere
    # to name one.
    #
    # A compute source already carries its stage in its own name, so stripping
    # and re-appending leaves clouds.comp.spv exactly where it was.
    if(shader MATCHES "\\.comp\\.slang$")
      string(REGEX REPLACE "\\.comp\\.slang$" "" base_path ${rel_path})
      set(stage_entries main)
      set(stage_suffixes .comp)
    else()
      string(REGEX REPLACE "\\.slang$" "" base_path ${rel_path})
      set(stage_entries vertMain fragMain)
      set(stage_suffixes .vert .frag)
    endif()

    list(LENGTH stage_entries stage_count)
    math(EXPR last_stage "${stage_count} - 1")

    foreach(stage_index RANGE ${last_stage})
      list(GET stage_entries ${stage_index} entry_point)
      list(GET stage_suffixes ${stage_index} stage_suffix)

      set(output_rel ${base_path}${stage_suffix}.spv)
      set(output_file ${shaders_out_dir}/${output_rel})

      # Depfiles are build bookkeeping, so they stay in the build tree rather
      # than in the directory that gets deployed next to the executable. Keyed
      # by configuration for the same reason the SPIR-V is: the multi-config
      # generators build every configuration out of one build directory. One per
      # output, since the two stages of a surface shader are separate compiles.
      set(depfile ${CMAKE_CURRENT_BINARY_DIR}/shader_deps/$<CONFIG>/${output_rel}.d)

      # -fvk-use-entrypoint-name is deliberately absent: it is what carries the
      # source's name into the SPIR-V, and without it the entry point is named
      # main. That is what lets the seam stop spelling a name that D3D12 could
      # not read and Vulkan would only ever accept one value for.
      #
      # slangc reports exactly the files each shader pulled in — including the
      # C++ headers shared with the engine, which a *.slangh glob would miss —
      # so editing one header rebuilds only the shaders that include it.
      add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${output_rel}"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shaders_out_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_BINARY_DIR}/shader_deps/$<CONFIG>
        COMMAND ${CMAKE_COMMAND} -E make_directory ${reflection_dir}
        COMMAND
          ${SLANGC_EXE} ${shader} -target spirv -profile spirv_1_4
          -emit-spirv-directly -warnings-as-errors all -entry ${entry_point}
          ${vulkan_register_shifts} -o
          ${output_file} -reflection-json ${reflection_dir}/${output_rel}.json
          -depfile ${depfile} $<IF:$<CONFIG:Debug>,-g1,-g0>
          $<IF:$<CONFIG:Debug>,-O0,-O3>
        # Same command as the compile, so validation runs exactly when a shader
        # recompiles and a failure fails the build. The target environment is
        # stated rather than left at spirv-val's universal default, which would
        # miss the Vulkan-specific rules; it matches VulkanDevice's kApiVersion.
        COMMAND ${SPIRV_VAL_EXE} --target-env vulkan1.4 ${output_file}
        DEPENDS ${shader}
        DEPFILE ${depfile}
        COMMENT "Compiling shader ${output_rel}"
        VERBATIM)

      list(APPEND spv_outputs ${output_file})

      # The same source and the same entry point, to the other target. The
      # Vulkan register shifts are deliberately absent: the register annotations
      # are already what D3D12 reads, and the shifts exist only to derive a
      # Vulkan set and binding from them.
      set(dxil_file ${shaders_out_dir}/${base_path}${stage_suffix}.dxil)
      set(dxil_depfile
          ${CMAKE_CURRENT_BINARY_DIR}/shader_deps/$<CONFIG>/${base_path}${stage_suffix}.dxil.d)

      add_custom_command(
        OUTPUT ${dxil_file}
        COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${base_path}${stage_suffix}.dxil"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shaders_out_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_BINARY_DIR}/shader_deps/$<CONFIG>
        COMMAND ${CMAKE_COMMAND} -E make_directory ${reflection_dir}
        COMMAND
          ${SLANGC_EXE} ${shader} -target dxil -profile ${dxil_profile}
          -warnings-as-errors all -entry ${entry_point} -o ${dxil_file}
          -reflection-json ${reflection_dir}/${base_path}${stage_suffix}.dxil.json
          -depfile ${dxil_depfile} $<IF:$<CONFIG:Debug>,-g1,-g0>
          $<IF:$<CONFIG:Debug>,-O0,-O3>
        # Same placement as spirv-val, and the same argument: a check that
        # runs on its own schedule is one that can silently stop covering
        # something. DXC validates and signs every compile, so this proves the
        # validation happened rather than repeating it.
        COMMAND ${CMAKE_COMMAND} -DDXIL_FILE=${dxil_file} -P
          ${CMAKE_SOURCE_DIR}/cmake/CheckDxilSignature.cmake
        DEPENDS ${shader} ${CMAKE_SOURCE_DIR}/cmake/CheckDxilSignature.cmake
        DEPFILE ${dxil_depfile}
        COMMENT "Compiling shader ${base_path}${stage_suffix}.dxil"
        VERBATIM)

      list(APPEND spv_outputs ${dxil_file})
    endforeach()
  endforeach()

  add_custom_target(${target} ALL DEPENDS ${spv_outputs})
endfunction()

file(GLOB_RECURSE shader_slang_sources CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/engine/engine/src/shaders/*.slang)

add_slang_shader_target(CompileShadersTarget SOURCES ${shader_slang_sources})
