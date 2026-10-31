# Architecture

```
 Launch.exe (Win32 GUI, no console)
     | CreateProcess hidden, reads port from stdout pipe, polls /api/health, opens Edge --app
     v
 wb_server.exe  (cpp-httplib, 127.0.0.1 only)      wb_cli.exe  (same core, command line)
     |  api.cpp: routes, validation, JSON          |
     |  jobs.cpp: one cancellable benchmark job    |
     v                                             v
 wb_core (C++17 static library; no HTTP, no UI)
   fea1d  stl_parser  ansys_csv  batch + thread_pool  hardware (mocks)  backprop  sysinfo  json/util  samples

 web/ (HTML + CSS + ES modules, canvas plots and 3-D viewer; no framework, no CDN)
```

## Separation of concerns
* **Presentation** is only `web/`. It talks to the backend through the JSON API in `src/server/api.cpp`; it contains no engineering numerics except drawing.
* **Computation** is only `src/core`. It has no dependency on HTTP or the UI, so it is unit-tested directly (`tests/`) and reused by `wb_cli`.
* **Transport** (`src/server`) validates requests, converts results to JSON and runs long jobs on a job manager.

## API (all JSON, loopback only)
GET `/api/health`, `/api/sysinfo`, `/api/samples`, `/api/geometry/sample`, `/api/results/sample`, `/api/jobs/:id`, `/api/jobs/:id/csv`, `/api/hw/devices`, `/api/hw/events`.
POST `/api/heartbeat`, `/closing`, `/shutdown`, `/geometry/parse`, `/results/parse`, `/thermal/solve`, `/thermal/export`, `/jobs/benchmark`, `/jobs/:id/cancel`, `/hw/connect|disconnect|telemetry|fault`, `/backprop/run`.

## Lifecycle: no terminal, clean shutdown
`Launch.exe` -> hidden server (`--port 0 --auto-exit`). The page sends a heartbeat; the server exits 4 s after the `/api/closing` beacon,
180 s without heartbeat, or 300 s if the UI never loaded. So there are no orphan processes after normal use.

## Security (local-only)
* Binds `127.0.0.1`; `Host` header must be `127.0.0.1:<port>` or `localhost:<port>` (DNS-rebinding guard).
* Every POST must carry `X-WB-Client: 1` (blocks cross-site form posts). Sample file names are whitelisted (no path traversal).
* Request bodies above 300 MB return HTTP 413; parsers enforce 256 MiB / 3M triangles / 5M rows.

## Concurrency and cancellation
* One benchmark job at a time. `ThreadPool::runOnAll` is a fork-join over a bounded pool (workers <= logical CPUs, <= 4 offered in the UI), chunked atomic work distribution, per-worker workspaces (no shared mutable state in the hot loop).
* Cancel sets an atomic flag checked between chunks; a cancelled job keeps no partial results. Progress is an atomic counter polled by the UI.
* Pool threads are created outside the timed region; launch-to-ready time is measured separately (`--launch-epoch-ms`).

## Error handling
Every parser returns a structured result (`ok`, line-numbered error). The API maps these to HTTP 4xx with `{error, code, hint}`; the UI shows them inline and never crashes. Device mocks throw `DeviceError{code, recoverable, hint}`.
