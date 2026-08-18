# Fails if a neutral RHI header has acquired a dependency on Vulkan or VMA
# (rhi_extraction_plan.md D1, enforcement mechanism 2 in its §4).
#
# Run with:  cmake -P cmake/RhiBoundaryCheck.cmake
# from anywhere — paths are resolved relative to this file, not the caller.
#
# Why this exists alongside the HeaderSelfContainment_RHI_Neutral target: that
# target proves a neutral header compiles without linking Vulkan, but a
# dependency that also happens to sit on the default system include path is
# found regardless of what a target links, which on some distributions covers
# Vulkan. A textual check is immune to include paths. The two mechanisms fail
# independently, which is the point of having both.
#
# Why it is a CMake script rather than a shell one-liner: the .sh and .bat
# wrappers then share one implementation. Two hand-written copies of the same
# check drift, and a Windows-only or Linux-only hole in a boundary check is
# worse than no check, because it reads as covered.

cmake_minimum_required(VERSION 3.20)

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(neutral_dir "${repo_root}/engine/rhi/include/rhi")

if(NOT IS_DIRECTORY "${neutral_dir}")
  message(FATAL_ERROR "rhi_boundary_check: ${neutral_dir} does not exist.")
endif()

# Types and macros first, then includes — the include patterns catch a header
# being pulled in even when nothing from it is named yet.
#
# CMake's regex flavour has no \b, so word boundaries are spelled out as "start
# of line, or a character that cannot be part of an identifier".
set(banned_patterns
    "vk::"
    "(^|[^A-Za-z0-9_])Vk[A-Z]"
    "(^|[^A-Za-z0-9_])Vma[A-Z]"
    "(^|[^A-Za-z0-9_])VMA_"
    "#[ \t]*include[ \t]*[<\"]vulkan/"
    "#[ \t]*include[ \t]*[<\"]vk_mem_alloc")

# Removes // and /* */ comments from one line, carrying the "inside a block
# comment" state across lines.
#
# Comments are stripped rather than matched because the neutral headers are
# expected to name Vulkan and D3D12 types in prose — recording that
# PipelineStage maps onto VkPipelineStageFlags2 and D3D12_BARRIER_SYNC is
# exactly the documentation that makes the mapping reviewable. Matching raw
# lines would make that unwritable and push the rationale out of the code.
# What is banned is a dependency, not a mention.
#
# String literals are deliberately not special-cased: a neutral header naming a
# Vulkan type inside a string literal is a finding too.
function(strip_comments_from_line line in_block_var out_var)
  set(in_block "${${in_block_var}}")
  set(result "")
  set(rest "${line}")

  while(TRUE)
    if(in_block)
      string(FIND "${rest}" "*/" close_index)
      if(close_index EQUAL -1)
        break()
      endif()
      math(EXPR after_close "${close_index} + 2")
      string(SUBSTRING "${rest}" ${after_close} -1 rest)
      set(in_block 0)
    else()
      string(FIND "${rest}" "//" line_index)
      string(FIND "${rest}" "/*" block_index)

      if(line_index EQUAL -1 AND block_index EQUAL -1)
        string(APPEND result "${rest}")
        break()
      endif()

      # Whichever comes first wins: "/* //" opens a block, "// /*" does not.
      if(NOT block_index EQUAL -1 AND (line_index EQUAL -1 OR block_index LESS line_index))
        string(SUBSTRING "${rest}" 0 ${block_index} prefix)
        string(APPEND result "${prefix}")
        math(EXPR after_open "${block_index} + 2")
        string(SUBSTRING "${rest}" ${after_open} -1 rest)
        set(in_block 1)
      else()
        string(SUBSTRING "${rest}" 0 ${line_index} prefix)
        string(APPEND result "${prefix}")
        break()
      endif()
    endif()
  endwhile()

  set(${in_block_var} "${in_block}" PARENT_SCOPE)
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# GLOB rather than GLOB_RECURSE is load-bearing: it excludes include/rhi/vulkan/,
# the transitional area that is allowed to expose Vulkan (plan D1 and D9).
file(GLOB neutral_headers "${neutral_dir}/*.h")

if(NOT neutral_headers)
  message(FATAL_ERROR "rhi_boundary_check: no headers found in ${neutral_dir}.")
endif()

set(violations "")

foreach(header IN LISTS neutral_headers)
  file(READ "${header}" content)
  file(RELATIVE_PATH relative_header "${repo_root}" "${header}")

  set(line_number 0)
  set(in_block 0)

  # Split on newlines by hand. file(STRINGS) would turn every semicolon in the
  # source into a list separator, which would make the reported line numbers
  # meaningless.
  while(TRUE)
    string(FIND "${content}" "\n" newline_index)
    if(newline_index EQUAL -1)
      set(line "${content}")
    else()
      string(SUBSTRING "${content}" 0 ${newline_index} line)
      math(EXPR after_newline "${newline_index} + 1")
      string(SUBSTRING "${content}" ${after_newline} -1 content)
    endif()

    math(EXPR line_number "${line_number} + 1")

    strip_comments_from_line("${line}" in_block code)

    foreach(pattern IN LISTS banned_patterns)
      if(code MATCHES "${pattern}")
        list(APPEND violations "  ${relative_header}:${line_number}: ${code}")
        break()
      endif()
    endforeach()

    if(newline_index EQUAL -1)
      break()
    endif()
  endwhile()
endforeach()

if(violations)
  list(JOIN violations "\n" violation_text)
  message(
    FATAL_ERROR
      "rhi_boundary_check: neutral RHI headers must not depend on Vulkan or VMA.\n"
      "${violation_text}\n\n"
      "Backend-facing declarations belong in engine/rhi/src/vulkan/ (invisible\n"
      "outside the module) or engine/rhi/include/rhi/vulkan/ (transitional, and\n"
      "excluded from this check). Naming a Vulkan type in a comment is fine —\n"
      "comments are stripped before matching, so this is a real dependency.")
endif()

list(LENGTH neutral_headers header_count)
message(STATUS "rhi_boundary_check: ${header_count} neutral RHI header(s) free of Vulkan and VMA.")
