# Guards the RHI's public seam. Five checks, in the order the boundary is built
# up (rhi_extraction_plan.md D1, enforcement mechanism 2 in its §4, and
# backend_readiness_plan.md D45 for the D3D12 half):
#
#   1. A neutral header in include/rhi/ must not depend on either backend's API.
#   2. include/rhi/vulkan/ and include/rhi/d3d12/, the areas that may expose a
#      backend's API, hold exactly the headers listed here and no others.
#   3. Outside engine/rhi/, only allowlisted sites may include those areas.
#   4. Outside engine/rhi/, only allowlisted files may name a backend's API at all.
#   5. Inside engine/rhi/, each backend names only its own API, and the module's
#      shared sources name neither.
#
# Checks 2 and 3 are ratchets rather than ceilings: the lists are allowed to
# shrink and an entry that stops matching is itself a failure, so neither can
# quietly outlive the code it excuses.
#
# Run with:  cmake -P cmake/RhiBoundaryCheck.cmake
# from anywhere — paths are resolved relative to this file, not the caller.
#
# Why check 1 exists alongside the HeaderSelfContainment_RHI_Neutral target: that
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
set(vulkan_patterns
    "vk::"
    "(^|[^A-Za-z0-9_])Vk[A-Z]"
    "(^|[^A-Za-z0-9_])Vma[A-Z]"
    "(^|[^A-Za-z0-9_])VMA_"
    "#[ \t]*include[ \t]*[<\"]vulkan/"
    "#[ \t]*include[ \t]*[<\"]vk_mem_alloc")

# D3D12's, with D3D12MA as VMA's counterpart. The bare word D3D12 stays legal:
# Backend::D3D12 is neutral vocabulary, as Backend::Vulkan is.
set(d3d12_patterns
    "(^|[^A-Za-z0-9_])ID3D12[A-Za-z]"
    "(^|[^A-Za-z0-9_])IDXGI[A-Za-z]"
    "(^|[^A-Za-z0-9_])D3D12_"
    "(^|[^A-Za-z0-9_])DXGI_"
    "(^|[^A-Za-z0-9_])D3D12MA"
    "#[ \t]*include[ \t]*[<\"](directx/|d3d12|dxgi)")

set(banned_patterns ${vulkan_patterns} ${d3d12_patterns})

# Comments are stripped rather than matched because the neutral headers are
# expected to name Vulkan and D3D12 types in prose — recording that
# PipelineStage maps onto VkPipelineStageFlags2 and D3D12_BARRIER_SYNC is
# exactly the documentation that makes the mapping reviewable. Matching raw
# lines would make that unwritable and push the rationale out of the code.
# What is banned is a dependency, not a mention.
include("${CMAKE_CURRENT_LIST_DIR}/StripComments.cmake")

# GLOB rather than GLOB_RECURSE is load-bearing: it excludes include/rhi/vulkan/
# and include/rhi/d3d12/, the areas allowed to expose a backend (plan D1, D9 and
# D45), which checks 2 and 3 govern instead.
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
      "rhi_boundary_check: neutral RHI headers must not depend on a backend's API.\n"
      "${violation_text}\n\n"
      "Backend-facing declarations belong in engine/rhi/src/<backend>/ (invisible\n"
      "outside the module) or engine/rhi/include/rhi/vulkan/ (transitional —\n"
      "exempt from this check, governed by the two below). Naming a Vulkan or\n"
      "D3D12 type in a comment is fine: comments are stripped before matching,\n"
      "so this is a real dependency.")
endif()

list(LENGTH neutral_headers header_count)
message(
  STATUS
    "rhi_boundary_check: ${header_count} neutral RHI header(s) free of Vulkan, VMA, D3D12 and D3D12MA."
)

# ---------------------------------------------------------------------------
# Check 2: the areas that may expose a backend are fixed sets of headers.
#
# include/rhi/vulkan/ and include/rhi/d3d12/ are the only places in the module
# that may expose a backend's API outside it (plan D1, D9 and D45). Both are
# frozen: a backend header added to one rather than to src/<backend>/ has to be
# argued for by editing this list, which is the point. Everything else a backend
# needs is private. Entries are relative to include/rhi/.
# ---------------------------------------------------------------------------

set(transitional_headers
    # The escape hatch itself (D9): instance/device/queue for ImGui, and the
    # VkFormat/VkPipelineCache accessors the app's pipeline creation needs.
    # The permanent residue (D9). ImGui's Vulkan backend takes raw handles and a
    # VkCommandBuffer by value, so a D3D12 build answers with a sibling file
    # rather than an edit. The one non-ImGui entry is the physical device, for a
    # depth-format query the neutral API cannot yet express.
    "vulkan/VulkanNative.h"
    # Pure functions over surface query results. Its only production caller is
    # now SwapchainTarget, inside the module, so this could move to src/vulkan/
    # and shrink the list. It is kept here deliberately: the functions are pure
    # and device-free so that they can be unit tested, and src/vulkan/ is on a
    # PRIVATE include path, which would put them permanently out of a test's
    # reach. SwapchainUtilTests.cpp is the test that reach buys — it puts a
    # surface into states a real display cannot be asked for on demand, a zero
    # extent among them. Reconsider if it grows past choosing surface
    # parameters, or if it acquires state or a device dependency.
    "vulkan/SwapchainUtil.h"
    # D3D12's escape hatch, permanent for the same reason as VulkanNative.h (D45):
    # ImGui's DX12 backend takes a raw device, queue, command list and the heap its
    # texture descriptors live in.
    "d3d12/D3D12Native.h")

file(GLOB transitional_present RELATIVE "${neutral_dir}" "${neutral_dir}/vulkan/*.h"
     "${neutral_dir}/d3d12/*.h")

set(unexpected "")
foreach(header IN LISTS transitional_present)
  if(NOT header IN_LIST transitional_headers)
    list(APPEND unexpected "  engine/rhi/include/rhi/${header}")
  endif()
endforeach()

if(unexpected)
  list(JOIN unexpected "\n" unexpected_text)
  message(
    FATAL_ERROR
      "rhi_boundary_check: unexpected header in an area that may expose a backend.\n"
      "${unexpected_text}\n\n"
      "A backend header belongs in engine/rhi/src/<backend>/, where nothing\n"
      "outside the module can reach it. Put it here only if something outside\n"
      "the module must include it, and say why by adding it to\n"
      "transitional_headers in cmake/RhiBoundaryCheck.cmake.")
endif()

foreach(header IN LISTS transitional_headers)
  if(NOT header IN_LIST transitional_present)
    message(
      FATAL_ERROR
        "rhi_boundary_check: transitional_headers lists ${header}, which no\n"
        "longer exists. Delete the entry — this list is meant to shrink.")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Check 3: who outside the module may include the transitional area.
#
# The plan's target for this step was "only the ImGui glue", which Stage 5
# cannot reach: the swapchain (Stage 6), the descriptor model (D7), pipeline
# creation (D8) and dispatch recording (Stage 8) are all explicitly out of
# scope, and each of them is a reason the application still names Vulkan. So the
# rule is a ratchet instead of a ceiling — every existing use is listed with the
# work that removes it, an unlisted one fails, and an entry that stops matching
# fails too, so the list cannot quietly outlive the code it excuses.
#
# Entries are "<path>|<header>|<why it is still here>", split on "|" because a
# CMake list is already split on ";". The header is relative to include/rhi/.
# ---------------------------------------------------------------------------

set(transitional_allowlist
    "engine/editor/src/VulkanUiBackend.cpp|vulkan/VulkanNative.h|ImGui's backend takes instance/device/queue and a VkCommandBuffer by value (D9)"
    "engine/editor/src/D3D12UiBackend.cpp|d3d12/D3D12Native.h|ImGui's DX12 backend takes a device, a queue, a command list and a descriptor heap by value (D45)"
    "tests/unit/rhi/SwapchainUtilTests.cpp|vulkan/SwapchainUtil.h|Surface states a real display cannot be put into on demand"
    "tests/gpu/rhi/DeviceTests.cpp|vulkan/VulkanNative.h|The escape hatch is what these cases assert on"
)

# Splitting by hand rather than with file(STRINGS), which would turn every
# semicolon in the source into a list separator and make line numbers useless.
function(read_lines path out_var)
  file(READ "${path}" content)
  string(REPLACE ";" "\;" content "${content}")
  string(REPLACE "\n" ";" content "${content}")
  set(${out_var} "${content}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE scanned_files
     "${repo_root}/apps/*.h" "${repo_root}/apps/*.cpp" "${repo_root}/tests/*.h"
     "${repo_root}/tests/*.cpp" "${repo_root}/engine/*.h" "${repo_root}/engine/*.cpp")

set(unlisted "")
set(matched_entries "")

foreach(scanned IN LISTS scanned_files)
  file(RELATIVE_PATH relative_path "${repo_root}" "${scanned}")

  if(relative_path MATCHES "^engine/rhi/")
    continue()
  endif()

  read_lines("${scanned}" lines)
  set(line_number 0)

  foreach(line IN LISTS lines)
    math(EXPR line_number "${line_number} + 1")

    # Anchored at the start of the line so a commented-out include does not
    # count as a use.
    if(NOT line MATCHES
       "^[ \t]*#[ \t]*include[ \t]*[<\"]rhi/((vulkan|d3d12)/[A-Za-z0-9_]+\\.h)[>\"]")
      continue()
    endif()

    set(included "${CMAKE_MATCH_1}")
    set(found FALSE)

    foreach(entry IN LISTS transitional_allowlist)
      if(entry MATCHES "^([^|]+)\\|([^|]+)\\|")
        if(CMAKE_MATCH_1 STREQUAL relative_path AND CMAKE_MATCH_2 STREQUAL included)
          set(found TRUE)
          list(APPEND matched_entries "${entry}")
          break()
        endif()
      endif()
    endforeach()

    if(NOT found)
      list(APPEND unlisted "  ${relative_path}:${line_number}: rhi/${included}")
    endif()
  endforeach()
endforeach()

if(unlisted)
  list(JOIN unlisted "\n" unlisted_text)
  message(
    FATAL_ERROR
      "rhi_boundary_check: new use of an RHI area that exposes a backend.\n"
      "${unlisted_text}\n\n"
      "Outside engine/rhi/, rhi/vulkan/ and rhi/d3d12/ may only be included by the sites\n"
      "listed in transitional_allowlist in cmake/RhiBoundaryCheck.cmake. Prefer\n"
      "the neutral API in rhi/. If there is genuinely no neutral way to say it\n"
      "yet, add an entry naming the work that removes it again.")
endif()

set(stale "")
foreach(entry IN LISTS transitional_allowlist)
  if(NOT entry IN_LIST matched_entries)
    string(REPLACE "|" " -> " readable "${entry}")
    list(APPEND stale "  ${readable}")
  endif()
endforeach()

if(stale)
  list(JOIN stale "\n" stale_text)
  message(
    FATAL_ERROR
      "rhi_boundary_check: transitional_allowlist has entries nothing matches.\n"
      "${stale_text}\n\n"
      "The include is gone, so delete the entry. The allowlist is a ratchet: it\n"
      "only means anything while it shrinks as the neutral API grows.")
endif()

list(LENGTH transitional_headers transitional_count)
list(LENGTH transitional_allowlist allowlist_count)
message(
  STATUS
    "rhi_boundary_check: transitional area is ${transitional_count} header(s), used from "
    "${allowlist_count} site(s) outside the module.")

# Check 4: who outside the module may name a backend's API at all.
#
# Checks 1-3 govern the RHI's own headers and who reaches into its transitional
# area. None of them stops engine code naming vk:: types it obtained some other
# way — and for a long time engine/engine/src/pch.h included vulkan.hpp, so
# every file in that module had the whole API in scope without an include or an
# allowlist entry to show for it. The ratchet read "2 headers from 4 sites"
# while a module quietly depended on Vulkan throughout.
#
# So this checks names rather than includes: an include can be avoided, a name
# cannot. A build without one backend has none of its names, so anything naming
# one is code that build cannot compile, and each list below is the honest count
# of it for its backend.
#
# One list per backend, and a file on one list is still held to the other's
# patterns: the ImGui glue for Vulkan has no business naming D3D12.
#
# Scope is engine/ (outside engine/rhi/) and apps/. Tests are deliberately
# exempt: several assert on the backend's own conversions and queue-family
# rules, which is what unit-testing a backend looks like, and their reach into
# the transitional area is already governed by check 3.
#
# One entry per backend, and each is permanent: the ImGui glue, whose backends
# take raw API objects by value (D9, D45). If a list ever grows a second, the
# question to ask is what neutral call is missing -- that is what the last
# temporary entry turned out to be.
# ---------------------------------------------------------------------------

set(vulkan_naming_allowlist
    "engine/editor/src/VulkanUiBackend.cpp|ImGui's Vulkan backend takes a VkFormat, a VkCommandBuffer and raw handles by value (D9). Permanent: a D3D12 build gets a sibling file, not an edit"
)
set(d3d12_naming_allowlist
    "engine/editor/src/D3D12UiBackend.cpp|ImGui's DX12 backend takes a device, a queue, a command list, a descriptor heap and DXGI formats by value (D45). Permanent, as its Vulkan sibling is"
)

# Matches every line of `path` against the patterns in the list named by
# `patterns_var`, comments stripped, appending "path:line: code" for each hit to
# the list named by `out_var`. `exempt_var` names a list of "path|line" entries
# whose exact (trimmed) line may match; an exemption used is recorded in
# `used_var`.
function(scan_file path relative_path patterns_var exempt_var out_var used_var)
  read_lines("${path}" lines)
  set(line_number 0)
  set(in_block 0)
  set(found "${${out_var}}")
  set(used "${${used_var}}")

  foreach(line IN LISTS lines)
    math(EXPR line_number "${line_number} + 1")
    strip_comments_from_line("${line}" in_block code)
    string(STRIP "${line}" trimmed)

    set(exempt FALSE)
    foreach(entry IN LISTS ${exempt_var})
      if(entry STREQUAL "${relative_path}|${trimmed}")
        set(exempt TRUE)
        list(APPEND used "${entry}")
      endif()
    endforeach()

    if(exempt)
      continue()
    endif()

    foreach(pattern IN LISTS ${patterns_var})
      if(code MATCHES "${pattern}")
        list(APPEND found "  ${relative_path}:${line_number}: ${code}")
        break()
      endif()
    endforeach()
  endforeach()

  set(${out_var} "${found}" PARENT_SCOPE)
  set(${used_var} "${used}" PARENT_SCOPE)
endfunction()

set(no_exemptions)

file(GLOB_RECURSE naming_scanned "${repo_root}/engine/*.h" "${repo_root}/engine/*.cpp"
     "${repo_root}/apps/*.h" "${repo_root}/apps/*.cpp")

foreach(backend IN ITEMS vulkan d3d12)
  set(naming_violations "")
  set(naming_matched "")
  set(unused "")

  foreach(scanned IN LISTS naming_scanned)
    file(RELATIVE_PATH relative_path "${repo_root}" "${scanned}")

    if(relative_path MATCHES "^engine/rhi/")
      continue()
    endif()

    set(allowed FALSE)
    foreach(entry IN LISTS ${backend}_naming_allowlist)
      if(entry MATCHES "^([^|]+)\\|")
        if(CMAKE_MATCH_1 STREQUAL relative_path)
          set(allowed TRUE)
          list(APPEND naming_matched "${entry}")
          break()
        endif()
      endif()
    endforeach()

    if(allowed)
      continue()
    endif()

    scan_file("${scanned}" "${relative_path}" ${backend}_patterns no_exemptions naming_violations
              unused)
  endforeach()

  if(naming_violations)
    list(JOIN naming_violations "\n" naming_violation_text)
    message(
      FATAL_ERROR
        "rhi_boundary_check: ${backend} named outside engine/rhi/.\n"
        "${naming_violation_text}\n\n"
        "Engine and application code talks to the RHI, not to a backend's API. If\n"
        "there is genuinely no neutral way to say it yet, add an entry to\n"
        "${backend}_naming_allowlist in cmake/RhiBoundaryCheck.cmake naming the work\n"
        "that removes it again — and check that a precompiled header is not the\n"
        "reason the name is available.")
  endif()

  foreach(entry IN LISTS ${backend}_naming_allowlist)
    if(NOT entry IN_LIST naming_matched)
      string(REPLACE "|" " -> " readable "${entry}")
      message(
        FATAL_ERROR
          "rhi_boundary_check: ${backend}_naming_allowlist has an entry nothing matches.\n"
          "  ${readable}\n\n"
          "The file no longer exists, so delete the entry. This list is a ratchet too.")
    endif()
  endforeach()

  list(LENGTH ${backend}_naming_allowlist naming_count)
  message(
    STATUS
      "rhi_boundary_check: ${naming_count} file(s) outside engine/rhi/ may name ${backend}.")
endforeach()

# ---------------------------------------------------------------------------
# Check 5: inside the module, the backends stay out of each other.
#
# Checks 1-4 exempt engine/rhi/ as a whole, so nothing stopped the D3D12 backend
# borrowing a Vulkan backend helper, or the reverse. One direction the build
# already catches: a D3D12 name in Vulkan or shared code breaks the Linux build.
# The other it never can — D3D12 code depending on Vulkan compiles everywhere
# D3D12 exists, because Vulkan is in every build — and a port written with the
# other backend open beside it is exactly where that happens.
#
# So src/vulkan/ and include/rhi/vulkan/ name no D3D12, src/d3d12/ and
# include/rhi/d3d12/ name no Vulkan or VMA, and the module's shared sources —
# src/ outside a backend's directory — name neither. A helper both backends need
# is neutral or duplicated. Neutral headers in include/rhi/ are check 1's.
#
# The one exemption is the dispatcher: Backend.cpp names each backend's factory
# header, which is declared apart from its backend precisely so that including
# it pulls in no API header. The include patterns cannot tell a module-internal
# "vulkan/..." from the API's, so the two lines are listed rather than the file.
# ---------------------------------------------------------------------------

set(shared_exemptions
    "engine/rhi/src/Backend.cpp|#include \"vulkan/VulkanDeviceFactory.h\""
    "engine/rhi/src/Backend.cpp|#include \"d3d12/D3D12DeviceFactory.h\"")

file(GLOB_RECURSE vulkan_side "${repo_root}/engine/rhi/src/vulkan/*"
     "${repo_root}/engine/rhi/include/rhi/vulkan/*")
file(GLOB_RECURSE d3d12_side "${repo_root}/engine/rhi/src/d3d12/*"
     "${repo_root}/engine/rhi/include/rhi/d3d12/*")
file(GLOB shared_side "${repo_root}/engine/rhi/src/*.h" "${repo_root}/engine/rhi/src/*.cpp")

set(isolation_violations "")
set(exemptions_used "")

foreach(file IN LISTS vulkan_side)
  file(RELATIVE_PATH relative_path "${repo_root}" "${file}")
  scan_file("${file}" "${relative_path}" d3d12_patterns no_exemptions isolation_violations
            exemptions_used)
endforeach()

foreach(file IN LISTS d3d12_side)
  file(RELATIVE_PATH relative_path "${repo_root}" "${file}")
  scan_file("${file}" "${relative_path}" vulkan_patterns no_exemptions isolation_violations
            exemptions_used)
endforeach()

foreach(file IN LISTS shared_side)
  file(RELATIVE_PATH relative_path "${repo_root}" "${file}")
  scan_file("${file}" "${relative_path}" banned_patterns shared_exemptions isolation_violations
            exemptions_used)
endforeach()

if(isolation_violations)
  list(JOIN isolation_violations "\n" isolation_text)
  message(
    FATAL_ERROR
      "rhi_boundary_check: a backend named where it does not belong inside engine/rhi/.\n"
      "${isolation_text}\n\n"
      "src/vulkan/ names no D3D12, src/d3d12/ names no Vulkan, and shared sources name\n"
      "neither. A helper both backends need belongs in a neutral file, or in each.")
endif()

foreach(entry IN LISTS shared_exemptions)
  if(NOT entry IN_LIST exemptions_used)
    string(REPLACE "|" " -> " readable "${entry}")
    message(
      FATAL_ERROR
        "rhi_boundary_check: shared_exemptions has an entry nothing matches.\n"
        "  ${readable}\n\n"
        "The line is gone, so delete the entry.")
  endif()
endforeach()

list(LENGTH vulkan_side vulkan_side_count)
list(LENGTH d3d12_side d3d12_side_count)
list(LENGTH shared_side shared_side_count)
message(
  STATUS
    "rhi_boundary_check: ${vulkan_side_count} Vulkan, ${d3d12_side_count} D3D12 and "
    "${shared_side_count} shared file(s) inside engine/rhi/ keep to their own API.")
