#!/usr/bin/env bash
# Cross-compile the Windows x64 binaries from Linux (MinGW-w64, posix threads, static).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j"$(nproc)"
