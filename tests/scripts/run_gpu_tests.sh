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

# Separate from run_unit_tests.sh because these need a Vulkan ICD, and CI has
# none. They skip rather than fail without one, so precommit runs them and a
# developer with no GPU is not blocked — but that same silence is why CI does
# not run them: a machine with no ICD would report a green run of nothing.
# Anything relying on these having actually executed must check that they did.
ctest --test-dir "build/$PRESET" -L gpu --output-on-failure
