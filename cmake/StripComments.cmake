# Removes // and /* */ comments from one line, carrying the "inside a block
# comment" state across lines. Shared by the source-level checks that run over
# this tree, so that a fix to the state machine reaches all of them: two copies
# of it would drift, and a check that silently stops seeing half its input
# reads as covered while covering nothing.
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
