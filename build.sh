#!/bin/bash
cmake --workflow --preset ninja-asan-linux && \
ln -sf build/ninja-asan-linux/compile_commands.json compile_commands.json
