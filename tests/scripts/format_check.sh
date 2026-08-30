#!/bin/bash
set -e

# Thin wrapper: the file list and the invocation live in cmake/Format.cmake so
# that this, the .bat and the `format-check` build target share one
# implementation. No preset argument — checking formatting needs no configured
# tree, which is what lets CI run it on a bare runner.
cmake -P "$(dirname "$0")/../../cmake/Format.cmake"
