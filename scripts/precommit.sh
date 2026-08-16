#!/bin/bash
set -e

if [ -n "$1" ]; then
  PRESET="$1"
else
  OS="$(uname -s)"
  case "$OS" in
    Darwin) PRESET="ninja-debug-macos" ;;
    Linux)  PRESET="ninja-debug-linux" ;;
    *)
      echo "Unsupported OS: $OS" >&2
      exit 1
      ;;
  esac
fi

echo "Running precommit for $PRESET..."

./build.sh "$PRESET" && \
tests/scripts/build_tests.sh "$PRESET" && \
tests/scripts/run_unit_tests.sh "$PRESET" && \
scripts/format_check.sh "$PRESET" && \

echo "Precommit succeeded!"
