#!/usr/bin/env bash
# Assemble dist/<ID>_Workbench.zip.  Usage: scripts/package_release.sh CandidateID
# Requires a finished build-win/ (scripts/build_windows.sh) and docs/report.pdf (docs/build_report.py).
set -euo pipefail
cd "$(dirname "$0")/.."
ID="${1:?usage: package_release.sh CandidateID}"
OUT="dist/${ID}_Workbench"
rm -rf dist && mkdir -p "$OUT/bin"
cp build-win/Launch.exe "$OUT/"; cp build-win/wb_server.exe build-win/wb_cli.exe "$OUT/bin/"
cp -r web data src tests results docs scripts tools cmake third_party .vscode "$OUT/"
cp CMakeLists.txt README.md DEPENDENCIES.md .gitignore "$OUT/"
rm -rf "$OUT/tests/tmp" "$OUT/tests/__pycache__" "$OUT/tests/ui/__pycache__"
[ -f "${ID}_Workbench.bundle" ] && cp "${ID}_Workbench.bundle" "$OUT/" || true
(cd dist && python3 -c "import shutil,sys;shutil.make_archive('${ID}_Workbench','zip','.','${ID}_Workbench')")
ls -la "dist/${ID}_Workbench.zip"
