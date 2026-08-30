# Guards the engine's namespace rule. Two checks:
#
#   1. Every public header under engine/<module>/include/ opens the namespace
#      its directory names — engine/core/include/core/Timer.h opens
#      Hikari::Core, engine/rhi/include/rhi/vulkan/DebugNames.h opens
#      Hikari::Rhi::Vulkan.
#   2. No header anywhere carries a using-directive.
#
# Check 1 is what makes uniform nesting worth having over promoting one module's
# vocabulary to Hikari:: directly. Both arrangements read fine; only this one is
# mechanically checkable, because "directory == target == namespace" is a rule a
# script can verify while "this type is common enough to live at the top level"
# is a judgement that can only be enforced by discipline. The rule is the reason
# the nesting is uniform, so it is enforced rather than trusted.
#
# Check 2 is the one that catches a real defect class. A using-directive in a
# header leaks into every translation unit that includes it, transitively and
# invisibly, and the resulting ambiguity surfaces in whichever unrelated file
# happens to include it next. Nothing else in this tree catches that: it
# compiles here and breaks on another compiler, or after an unrelated include is
# added months later. Using-directives in .cpp files are deliberate and fine.
#
# Run with:  cmake -P cmake/NamespaceCheck.cmake
# from anywhere — paths are resolved relative to this file, not the caller.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/StripComments.cmake")

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# Headers that declare nothing a namespace could hold. A ratchet rather than a
# ceiling: an entry that stops being needed is itself a failure, so the list
# cannot quietly outlive its reason.
set(no_namespace_allowlist "engine/core/include/core/MyMacros.h")

set(errors "")

# ---------------------------------------------------------------------------
# 1. Public engine headers open the namespace their directory names.
# ---------------------------------------------------------------------------

file(GLOB module_dirs "${repo_root}/engine/*")
set(checked_count 0)
set(allowlist_hits "")

foreach(module_dir IN LISTS module_dirs)
  if(NOT IS_DIRECTORY "${module_dir}/include")
    continue()
  endif()

  file(GLOB_RECURSE headers "${module_dir}/include/*.h")

  foreach(header IN LISTS headers)
    file(RELATIVE_PATH relative "${repo_root}" "${header}")

    # The namespace the path asks for: every directory component below
    # include/, PascalCased. core/Timer.h -> Hikari::Core,
    # rhi/vulkan/DebugNames.h -> Hikari::Rhi::Vulkan.
    file(RELATIVE_PATH under_include "${module_dir}/include" "${header}")
    get_filename_component(dirs "${under_include}" DIRECTORY)
    string(REPLACE "/" ";" components "${dirs}")

    set(expected "Hikari")
    foreach(component IN LISTS components)
      string(SUBSTRING "${component}" 0 1 first)
      string(SUBSTRING "${component}" 1 -1 remainder)
      string(TOUPPER "${first}" first)
      string(APPEND expected "::${first}${remainder}")
    endforeach()

    file(STRINGS "${header}" lines)
    set(in_block 0)
    set(found 0)
    foreach(line IN LISTS lines)
      strip_comments_from_line("${line}" in_block code)
      # Deeper nesting is fine — BarrierPresets.h groups its own contents under
      # Hikari::Rhi::BarrierPresets. What the check forbids is a header opening
      # some other module's namespace, or none at all.
      if(code MATCHES "^namespace[ \t]+${expected}(::[A-Za-z_][A-Za-z0-9_]*)*[ \t]*(\\{)?[ \t]*$")
        set(found 1)
        break()
      endif()
    endforeach()

    list(FIND no_namespace_allowlist "${relative}" allowlist_index)

    if(allowlist_index EQUAL -1)
      if(NOT found)
        list(APPEND errors "  ${relative}\n      expected: namespace ${expected}")
      endif()
      math(EXPR checked_count "${checked_count} + 1")
    else()
      list(APPEND allowlist_hits "${relative}")
      if(found)
        list(
          APPEND
          errors
          "  ${relative}\n      allowlisted as holding no namespace, but opens ${expected} — remove the allowlist entry"
        )
      endif()
    endif()
  endforeach()
endforeach()

# An allowlist entry naming a file that no longer exists is stale.
foreach(entry IN LISTS no_namespace_allowlist)
  list(FIND allowlist_hits "${entry}" hit_index)
  if(hit_index EQUAL -1)
    list(APPEND errors "  ${entry}\n      allowlisted, but no such header was scanned — remove the entry")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# 2. No header carries a using-directive.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE all_headers "${repo_root}/engine/*.h" "${repo_root}/src/*.h"
     "${repo_root}/tests/*.h")

set(using_count 0)
foreach(header IN LISTS all_headers)
  file(RELATIVE_PATH relative "${repo_root}" "${header}")
  file(STRINGS "${header}" lines)
  set(in_block 0)
  set(line_number 0)
  foreach(line IN LISTS lines)
    math(EXPR line_number "${line_number} + 1")
    strip_comments_from_line("${line}" in_block code)
    if(code MATCHES "(^|[^A-Za-z0-9_])using[ \t]+namespace[ \t]")
      list(APPEND errors
           "  ${relative}:${line_number}\n      using-directive in a header: ${code}")
    endif()
  endforeach()
  math(EXPR using_count "${using_count} + 1")
endforeach()

if(errors)
  list(JOIN errors "\n" report)
  message(FATAL_ERROR "namespace_check failed:\n\n${report}\n")
endif()

message(
  STATUS
    "namespace_check: ${checked_count} public engine header(s) open their module's namespace; ${using_count} header(s) free of using-directives."
)
