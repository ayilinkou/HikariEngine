#!/bin/bash
set -e

# An optional first argument can force a preset (e.g. ./test.sh linux).
OS="$(uname -s)"
case "${1:-$OS}" in
  Darwin|macos|mac)
    PRESET="ninja-debug-macos"
    ;;
  Linux|linux)
    PRESET="ninja-debug-linux"
    ;;
  *)
    echo "Unsupported OS: ${1:-$OS}" >&2
    exit 1
    ;;
esac

./build/${PRESET}/VulkanApp --report --screenshot --frames --fixed-dt --scene --camera-preset 1
