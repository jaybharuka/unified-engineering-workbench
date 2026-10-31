# Unified Engineering Workbench

One local desktop-style application with five views (Geometry, Thermal Analysis, Performance, Hardware, Backprop),
backed by a C++17 core. Windows 11 x64, no installer, no administrator rights, no terminal window.

> Submission: `CandidateID_Workbench.zip` (replace `CandidateID` with your ID). Placeholders in `docs/` marked `[...]` must be filled in by the candidate.

## Prerequisites (to RUN)
* Windows 11 x64 (Microsoft Edge is preinstalled; any other default browser is used as a fallback).
* Nothing else. No install, no admin rights, no internet connection, no Python/Node.

## One-click steps
1. Right-click the ZIP -> **Properties -> Unblock** (if offered) -> OK.
2. Right-click the ZIP -> **Extract All** (extract the *whole* ZIP; do not run from inside the ZIP viewer).
3. Double-click **`Launch.exe`**. If SmartScreen says "Windows protected your PC": **More info -> Run anyway** (the file is unsigned).
4. The workbench opens in its own window. Closing the window stops the backend automatically after ~4 s.

`Launch.exe` starts `bin\wb_server.exe` hidden, bound to `127.0.0.1` only on a free port, waits for `/api/health`,
opens an Edge `--app` window and exits. Launch-to-ready time is shown in the UI header and reported separately from benchmarks.

## Views
| View | What it does |
|---|---|
| Geometry | ASCII STL viewer (rotate / pan / zoom / fit, wireframe, presets) and ANSYS CSV point cloud (colour legend, units, click a node to inspect it). Parsing is done in C++. |
| Thermal Analysis | 1-D steady heat-conduction FEA (Task C): N = 10/20/40/80, midpoint error, convergence plot, CSV and PNG export. |
| Performance | Serial vs bounded-pool batch solve (Task D): warm-up, 5 repetitions, median, speedup T1/Tp, efficiency, environment report, progress bar, **Cancel**. |
| Hardware | **SIMULATED** microcontroller, FPGA and JTAG adapter (Task E): discover, capabilities, connect/disconnect, telemetry, one recoverable timeout. |
| Backprop | Task F gradient check (epsilon 1e-6, tolerance 1e-5) and 100-step loss plot. |

## Supported file formats
* **ASCII STL only** (`solid ... facet normal ... vertex ...`). Binary STL, empty files, NaN/Inf, truncated facets are rejected with a line number.
* **ANSYS-style nodal CSV**, header exactly `node_id,x_m,y_m,z_m,temperature_C`; ids positive and unique; numbers finite; temperature >= -273.15 C. Up to 10 problems are listed per file. `data/synthetic_rod_temperatures.csv` is **synthetic**, not a real ANSYS export.
* Export: CSV (raw results) and PNG (plots).

## Architecture (short)
`wb_core` (C++17 library, no UI/HTTP) <- `wb_server` (cpp-httplib, loopback only, JSON API, job manager) <- web UI (plain ES modules + canvas, no framework, no CDN).
`Launch.exe` is a tiny Win32 launcher. `wb_cli` runs the same core from a command line. Details: `docs/ARCHITECTURE.md`.

## Repository layout
```
Launch.exe  bin/{wb_server.exe,wb_cli.exe}  web/  data/ (+PROVENANCE.md)
src/{core,server,cli,launcher}  CMakeLists.txt  cmake/  third_party/  DEPENDENCIES.md
tests/  results/ (raw CSV + labelled PNG)  docs/  scripts/  tools/  .vscode/
```

## Run / debug from VS Code (or Antigravity)
Open the folder. Install the *C/C++* extension (`ms-vscode.cpptools`). Press **F5** -> "Launch Workbench" (starts `Launch.exe`),
or "Build + Launch" (CMake build first). Tasks: Terminal -> Run Task -> `build`, `test`.

## Build from source
Requires CMake >= 3.16 and a C++17 compiler. Tested: GCC 13 (Linux) and MinGW-w64 GCC 13 (posix threads, cross-built, static link).
MSVC is **not tested**.
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure        # 70 C++ tests
```
Windows with MinGW-w64 (MSYS2 `mingw-w64-x86_64-gcc`, `cmake`, `ninja`):
```
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```
Then copy `build-win\Launch.exe` to the package root and `wb_server.exe`, `wb_cli.exe` to `bin\`.
Cross-compiling from Linux: `scripts/build_windows.sh` (uses `cmake/mingw-w64-x86_64.cmake`).

## Regenerate results and plots
```
bin\wb_cli.exe all --out results          (or scripts\regenerate_results.bat)
python tools\make_plots.py                (needs matplotlib; the UI also exports PNGs)
```
UI end-to-end test (needs Python + Playwright): `scripts/run_ui_tests.sh`.

## Tested file-size limits
See `results/file_size_limits.csv` and `docs/TESTING.md`. Summary: parse tested to 1,000,000 triangles (237 MiB) and 3,000,000 CSV rows (122 MiB);
the UI was exercised with a 100,000-triangle STL and a 1,000,000-node CSV. Hard limits: 256 MiB per file, 3M triangles, 5M rows; request bodies above 300 MB get HTTP 413.
Large meshes are decimated for display only (statistics use the full mesh).

## Known limitations
* `Launch.exe` is unsigned (SmartScreen prompt) and not code-signed or installer-packaged.
* Hardware view is entirely **simulated**; no real device, JTAG or FPGA is touched.
* The 3-D viewer is a software painter's-algorithm renderer: no lighting model beyond simple shading, no occlusion-exact sorting for intersecting meshes, slower for >250k displayed triangles.
* Only ASCII STL; no unit field in STL, so metres are assumed (stated in the UI).
* Thermal task is 1-D; 3-D FEA / real ANSYS results are out of scope.
* Speedup is hardware dependent; on the reference machine only 4 physical cores exist, so >4 workers are not expected to help (the UI disables worker counts above the logical CPU count).
* Tested on Windows 11 (i5-11320H) plus Linux; other Windows versions untested.
