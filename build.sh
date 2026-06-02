#!/bin/bash
cmake --workflow --preset ninja-debug-linux && \
ln -sf build/ninja-debug-linux/compile_commands.json compile_commands.json
