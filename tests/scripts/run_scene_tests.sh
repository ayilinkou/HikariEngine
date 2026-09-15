#!/bin/bash
set -e

if [ -n "$1" ]; then
  PRESET="$1"
else
  OS="$(uname -s)"
  case "$OS" in
    Linux)  PRESET="ninja-debug-linux" ;;
    *)
      echo "Unsupported OS: $OS" >&2
      exit 1
      ;;
  esac
fi

# Real runs of the engine, headless, asserting on the RunReport they return.
# Separate from the gpu label so that ctest -L gpu keeps its under-a-minute
# meaning and a red step says which kind of thing broke. Registered once per
# backend the build contains, and -L is a regular expression, so scene-d3d12 runs
# too where it exists. Like the gpu tests they skip without a device — CI
# supplies lavapipe on Linux and WARP on Windows.
ctest --test-dir "build/$PRESET" -L scene --output-on-failure
