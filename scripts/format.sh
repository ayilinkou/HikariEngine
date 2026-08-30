#!/bin/bash
set -e

# Thin wrapper: the file list and the invocation live in cmake/Format.cmake so
# that this, the .bat and the `format` build target share one implementation.
# No preset argument — formatting needs no configured tree.
cmake -DFIX=ON -P "$(dirname "$0")/../cmake/Format.cmake"
