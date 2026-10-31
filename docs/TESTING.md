# Testing

## Automated
| Suite | Where | Result |
|---|---|---|
| C++ unit tests (own tiny framework) | `tests/test_*.cpp`, run by `ctest` or `build/wb_tests` | **70 tests, 0 failed** (fea 12, parsers 24, batch 12, hardware 10, backprop 8, json 4) |
| UI end-to-end (headless Chromium via Playwright) | `tests/ui/ui_smoke.py` | 43 checks, all passed; screenshots in `docs/screenshots/` |
| Mutation check | `tools/mutation_check.sh` -> `results/mutation_check.log` | 3 of 3 injected solver bugs detected |
| Windows launcher | manual on Windows 11 (i5-11320H): double-click `Launch.exe`, no console, window opens, launch-to-ready 40 ms | passed (screenshot in the walkthrough) |

Also: the C++ tests were run on Linux (GCC) and under Wine (MinGW-built `wb_tests.exe`).
**Honest scope:** the UI test and Wine runs are developer tooling in a sandbox; real-Windows verification covered launch, the Geometry view and `wb_cli all`. [Candidate: add anything else you clicked through on your PC.]

## What the tests cover
* **FEA:** boundary values; equality with an independent dense Gaussian-elimination solver (fixed and random parameters); midpoint error = q h^2/(8k); convergence order 2; zero-generation -> linear; residual; validation of bad input; CSV shape.
* **Parsers:** valid files; empty, binary, truncated, NaN/Inf, wrong header, duplicate/negative/non-integer ids, temperatures below absolute zero, many errors (capped at 10), size limits, line numbers in messages.
* **Batch:** fixed-seed generator golden values (cross-checked with an independent Python implementation), serial = parallel (<= 1e-8), cancellation, progress, invalid parameters.
* **Hardware mocks:** discovery, capabilities, connect/disconnect state machine, telemetry, injected timeout and recovery, event-log bound.
* **Backprop:** analytic vs central-difference gradients, simultaneous update, loss decreases, CSV shape.
* **JSON:** non-finite numbers serialise as `null`, control-character escaping.

## File-size limits (measured; `results/file_size_limits.csv`, produced on the development sandbox, parse time only)
| Kind | Records | File size | Parse time | Result |
|---|---|---|---|---|
| ASCII STL | 10,000 tri | 2.4 MiB | 0.03 s | ok |
| ASCII STL | 100,000 tri | 23.7 MiB | 0.19 s | ok |
| ASCII STL | 500,000 tri | 118.7 MiB | 1.0 s | ok |
| ASCII STL | 1,000,000 tri | 237.3 MiB | 2.0 s | ok |
| ANSYS CSV | 100,000 rows | 3.9 MiB | 0.07 s | ok |
| ANSYS CSV | 1,000,000 rows | 39.9 MiB | 0.73 s | ok |
| ANSYS CSV | 3,000,000 rows | 122.0 MiB | 2.2 s | ok |

Through HTTP (uploaded as the UI does): 500,000 triangles (118.7 MiB) returned in 1.6 s with peak server memory about 270 MB; the viewer displays every 2nd triangle (250,000) with a visible decimation warning while statistics use the full mesh.
A 335 MB body is rejected with HTTP 413 and a clear message; the server stays alive.
UI exercised with a 100,000-triangle STL and a 1,000,000-node CSV (`docs/screenshots/13_*`, `14_*`).
**Documented limits:** 256 MiB per file, 3,000,000 triangles, 5,000,000 CSV rows, 300 MB request body. Larger files give a clear error, not a crash. Not tested: files between 237 MiB and 256 MiB at the limit on a low-memory machine.

## Failure cases verified in the UI
Binary STL, empty file, malformed CSV row, NaN value, wrong header (`03_stl_error.png`, `05_csv_error.png`), backend benchmark cancel, simulated device timeout and recovery (`08`, `09`).

## Known gaps
No automated test of `Launch.exe` on real Windows (manual only); no fuzzing beyond the malformed cases above; MSVC build untested; no accessibility audit with a screen reader (ARIA attributes and keyboard use were implemented, not user-tested).
