#!/usr/bin/env bash
# End-to-end UI test (needs: python3, playwright + chromium). Run after building natively: cmake -S . -B build && cmake --build build
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/ui/ui_smoke.py build/wb_server . docs/screenshots
