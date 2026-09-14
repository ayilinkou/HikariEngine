#!/bin/bash
set -e

PRESET=""

for arg in "$@"; do
  case "$arg" in
    -*)
      echo "Unknown option: $arg" >&2
      echo "Usage: backend_compare.sh [preset]" >&2
      exit 3
      ;;
    *) PRESET="$arg" ;;
  esac
done

# D3D12 is built only on Windows, so this runs where both backends exist: under
# Git Bash there, or as backend_compare.bat.
if [ -z "$PRESET" ]; then
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PRESET="ninja-debug-windows" ;;
    *)
      echo "backend_compare.sh needs a build with both backends, and D3D12 is built only on Windows." >&2
      exit 3
      ;;
  esac
fi

# Fixed names in the gitignored reports directory, because the runs and the
# comparison that follows have to agree on where the files went.
VULKAN_REPORT="tests/reports/backend_vulkan.json"
D3D12_REPORT="tests/reports/backend_d3d12.json"

# The same run twice, one per backend, on each backend's default adapter. Headless,
# so no present mode or window gates anything, and without captures: across
# backends the pixels are skipped until D3D12 reaches parity, and what this checks is
# that both backends made the same decisions — the counters, held exact across
# backends (plan D26). The frame count only needs to be past the first frames' one-off
# work; the counters describe the last frame drawn.
#
# D3D12 validates on the GPU in full rather than at its default: the counters are
# compared across backends only when each backend's own validation sub-mode is at
# its strongest, and full is where the resource-state checks run.
run_backend() {
  ./build/$PRESET/HikariHeadless --backend "$1" --report "$2" "${@:3}" \
      --frames 100 --fixed-dt --scene scenes/test_scene.map --camera-preset 1 \
      --resolution 1920x1080 --no-ui
}

run_backend Vulkan "$VULKAN_REPORT"
run_backend D3D12 "$D3D12_REPORT" --d3d12-gpu-based-validation full

echo

# Exit 2 is the expected verdict until parity: nothing moved, and the pixels were
# not compared. 1 means a counter differs between the backends, which is a bug in
# one of them. The comparison decides the exit status, so set -e must not swallow
# it.
set +e
./build/$PRESET/HikariCompare --actual-report "$D3D12_REPORT" --expected-report "$VULKAN_REPORT"
status=$?
set -e

exit $status
