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

# Fixed names in the gitignored reports and screenshots directories, because the
# runs and the comparison that follows have to agree on where the files went.
VULKAN_REPORT="tests/reports/backend_vulkan.json"
D3D12_REPORT="tests/reports/backend_d3d12.json"
VULKAN_CAPTURE="tests/screenshots/backend_vulkan.png"
D3D12_CAPTURE="tests/screenshots/backend_d3d12.png"

# The same run twice, one per backend, on each backend's default adapter. Headless,
# so no present mode or window gates anything. Both signals are compared: the
# counters exactly, since both backends must make the same decisions, and the
# pixels within the tolerance measured for the build type, since two backends round
# differently on one adapter (plan D26). The pixels are compared only when the PCI
# identifiers say both runs had the same adapter, so a machine whose backends pick
# different default adapters gets a skip naming them rather than a verdict. The
# tolerance was measured from exactly these flags: the scene, camera and extent, and
# frame 100, which is also past the first frames' one-off work.
#
# D3D12 validates on the GPU in full rather than at its default: the counters are
# compared across backends only when each backend's own validation sub-mode is at
# its strongest, and full is where the resource-state checks run.
run_backend() {
  ./build/$PRESET/HikariHeadless --backend "$1" --report "$2" --screenshot "$3" "${@:4}" \
      --frames 100 --fixed-dt --scene scenes/test_scene.map --camera-preset 1 \
      --resolution 1920x1080 --no-ui
}

run_backend Vulkan "$VULKAN_REPORT" "$VULKAN_CAPTURE"
run_backend D3D12 "$D3D12_REPORT" "$D3D12_CAPTURE" --d3d12-gpu-based-validation full

echo

# Exit 0 is the expected verdict: every counter matched, and the pixels differ no more
# than the tolerance allows. 1 means a counter differs between the backends, which is
# a bug in one of them, or the pixels moved past the tolerance, which is a rendering
# change or a driver update to measure and explain again. The comparison decides the
# exit status, so set -e must not swallow it.
set +e
./build/$PRESET/HikariCompare --actual-report "$D3D12_REPORT" --expected-report "$VULKAN_REPORT" \
    --actual-image "$D3D12_CAPTURE" --expected-image "$VULKAN_CAPTURE"
status=$?
set -e

exit $status
