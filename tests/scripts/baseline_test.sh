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

# --borderless is what fixes the screenshot's size, not --resolution. A window
# size is a request the window system may refuse, and a tiling compositor
# (Hyprland, sway, i3) always does — it puts the window in whatever tile the
# layout says, so the captured frame comes out at the tile's size and differs
# between machines and between layouts. Covering the display is honoured, so it
# gives the same extent on every run.
#
# --resolution is still passed: it is what a non-tiling window system uses, and
# it is the size the window is created at before the mode change, so a
# compositor that refuses fullscreen degrades to the right size rather than to
# three quarters of the display.
#
# Both only pin the extent to *this* display's. A capture that does not depend
# on the display at all needs an offscreen render target (Part IV steps 38-39).
./build/$PRESET/VulkanApp --report --screenshot --frames --fixed-dt --scene --camera-preset 1 \
    --resolution 1920x1080 --borderless
