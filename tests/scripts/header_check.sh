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

# Aggregate target: compiles every src/ header and every engine module's public
# headers on their own, with no PCH.
cmake --build build/$PRESET --target HeaderSelfContainment
