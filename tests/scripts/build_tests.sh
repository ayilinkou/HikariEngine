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

cmake --build "build/$PRESET" --target core_tests platform_tests rhi_tests rhi_gpu_tests
