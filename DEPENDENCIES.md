# Dependencies and versions

| Component | Version | License | Where |
|---|---|---|---|
| cpp-httplib (header only) | 0.18.3 | MIT | `third_party/httplib.h`, `third_party/LICENSE-cpp-httplib.txt` |
| C++ standard library | C++17 | - | - |
| CMake (build only) | >= 3.16 | - | - |
| GCC / MinGW-w64 (build only) | GCC 13 (posix threads, static link) | GPL runtime-exception | used for the shipped binaries |
| Python 3 + matplotlib (optional, plots) | 3.11 / 3.x | PSF / BSD-style | `tools/make_plots.py` |
| Python Playwright (optional, UI test) | any recent | Apache-2.0 | `tests/ui/ui_smoke.py` |

The web UI uses **no** third-party JavaScript, CSS, fonts or CDN: everything (plots, 3-D viewer, colour map) is in `web/`.
No network access is required at run time. No credentials or personal data are stored or transmitted; the server listens on 127.0.0.1 only.
