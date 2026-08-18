#!/bin/bash
set -e

# Thin wrapper: the check itself is a CMake script so that this and the .bat
# share one implementation. See cmake/RhiBoundaryCheck.cmake for what it does
# and why.
cmake -P "$(dirname "$0")/../../cmake/RhiBoundaryCheck.cmake"
