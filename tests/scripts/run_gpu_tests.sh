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

# Developers without a device get skips. CI supplies lavapipe/WARP and sets
# HIKARI_TESTS_REQUIRE_DEVICE=1 so missing device coverage fails there.
ctest --test-dir "build/$PRESET" -L gpu --no-tests=error --output-on-failure
