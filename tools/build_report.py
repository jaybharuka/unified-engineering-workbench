#!/usr/bin/env python3
"""Builds docs/report.pdf (<= 6 pages, < 1 MB) from the real result files. Needs: playwright+chromium, Pillow.
Usage: python tools/build_report.py   (edit CANDIDATE / LINK placeholders below or in the PDF source first)"""
import base64, csv, io, os, sys
from PIL import Image
from playwright.sync_api import sync_playwright

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
P = lambda *a: os.path.join(ROOT, *a)
CANDIDATE = os.environ.get("CANDIDATE", "[Candidate name / ID]")
LINK = os.environ.get("ZIP_LINK", "[link to CandidateID_Workbench.zip and Git repository - add before submitting]")

def rows(n): return list(csv.DictReader(open(P("results", n), newline="")))
def img(path, width=700, q=70, fmt="JPEG"):
    im = Image.open(path).convert("RGB")
    if im.width > width: im = im.resize((width, int(im.height * width / im.width)), Image.LANCZOS)
    b = io.BytesIO(); im.save(b, fmt, quality=q, optimize=True)
    return f"data:image/{fmt.lower()};base64," + base64.b64encode(b.getvalue()).decode()

conv = rows("thermal_convergence.csv"); perf = rows("performance_summary.csv")
env = dict(l.split(": ", 1) for l in open(P("results", "performance_environment.txt")).read().splitlines() if ": " in l)
svg = open(P("web", "assets", "embedded_block_diagram.svg")).read()
tab = lambda head, body: "<table><tr>" + "".join(f"<th>{h}</th>" for h in head) + "</tr>" + "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in body) + "</table>"

conv_t = tab(["N", "h (m)", "max nodal err (C)", "max midpoint err (C)", "order"],
             [[r["n_elements"], r["h_m"], r["max_node_error_C"], r["max_midpoint_error_C"], r["observed_order"] or "-"] for r in conv])
perf_t = tab(["impl.", "workers", "median (s)", "speedup T1/Tp", "efficiency", "max diff (C)"],
             [[r["implementation"], r["workers"], f'{float(r["median_s"]):.4f}', f'{float(r["speedup_T1_over_Tp"]):.2f}', f'{float(r["efficiency"]):.2f}', r["max_abs_diff_C"]] for r in perf])

html = f"""<!doctype html><html><head><meta charset="utf-8"><title>Unified Engineering Workbench - report</title><style>
@page {{ size: A4; margin: 13mm 14mm; }}
body {{ font: 10.4pt/1.4 Calibri, Arial, sans-serif; color:#111; }}
h1 {{ font-size: 16pt; margin: 0 0 2px; }} h2 {{ font-size: 12.5pt; margin: 12px 0 4px; break-after: avoid; border-bottom: 1px solid #999; }}
p, ul {{ margin: 3px 0; }} ul {{ padding-left: 16px; }}
table {{ border-collapse: collapse; margin: 4px 0; font-size: 9.2pt; }} th, td {{ border: 1px solid #888; padding: 1.5px 5px; text-align: left; }} th {{ background:#eee; }}
.row {{ display:flex; gap:8px; align-items:flex-start; }} .row > * {{ flex:1; min-width:0; }} img {{ max-width:100%; }} table, svg, .row {{ break-inside: avoid; }}
.small {{ font-size: 8.8pt; color:#333; }} svg {{ width:100%; height:auto; }} code {{ font-size: 8.5pt; }}
.note {{ background:#fff7d6; border:1px solid #d9b800; padding:2px 5px; }}
</style></head><body>
<h1>Unified Engineering Workbench</h1>
<p class="small">{CANDIDATE} &middot; PASS SCIENCE WORKS assignment &middot; Windows 11 x64 &middot; C++17 backend + web UI &middot; Full package: {LINK}</p>

<h2>1. Summary and design choices</h2>
<p>One application, five views (Geometry, Thermal, Performance, Hardware, Backprop). <code>Launch.exe</code> (tiny Win32 GUI program) starts <code>bin\\wb_server.exe</code> hidden on <code>127.0.0.1</code>, waits for <code>/api/health</code> and opens an Edge app window; the backend exits itself a few seconds after the window closes. No installer, admin rights, terminal or internet.</p>
<ul><li><b>Separation:</b> <code>wb_core</code> (C++17 library: FEA, STL/CSV parsers, thread pool, batch, device mocks, backprop) has no HTTP/UI code and is unit-tested directly; <code>wb_server</code> (cpp-httplib, MIT) only validates and serialises; the UI (plain ES modules + canvas, no CDN) only draws.</li>
<li><b>Why a local web UI:</b> works on a clean Windows 11 PC with no runtime to install; Edge is preinstalled; the same core is reused by <code>wb_cli</code>.</li>
<li><b>Safety:</b> loopback only, Host-header check, <code>X-WB-Client</code> header on every POST, file whitelist, strict parsers (line-numbered errors, NaN/Inf rejected), limits 256 MiB / 3M triangles / 5M rows, 300 MB body cap (HTTP 413).</li>
<li><b>Jobs:</b> one benchmark at a time with progress and <b>Cancel</b> (atomic flag, no partial results).</li></ul>

<h2>2. Geometry and result viewer (Task B)</h2>
<div class="row"><div><img src="{img(P('docs','screenshots','01_geometry_stl.png'),600)}"></div><div><img src="{img(P('docs','screenshots','04_geometry_csv.png'),600)}"></div></div>
<p class="small">Left: ASCII STL (self-made synthetic cylinder). Right: ANSYS-style nodal CSV (<code>node_id,x_m,y_m,z_m,temperature_C</code>), colour legend, units, node inspection. The CSV is <b>synthetic</b>, not a real ANSYS export (see <code>data/PROVENANCE.md</code>). Parsing is in C++. Tested sizes: STL to 1,000,000 triangles (237 MiB, 2.0 s parse), CSV to 3,000,000 rows (122 MiB, 2.2 s); UI exercised at 100k triangles and 1M nodes; 500k triangles through HTTP in 1.6 s; larger inputs are decimated for display only (stated in the UI).</p>

<h2>3. Thermal FEA and verification (Task C)</h2>
<p>-kA T'' = qA on [0,L]; L=1 m, A=1e-4 m&sup2;, k=10 W/mK, q=1e4 W/m&sup3;, T(0)=100 C, T(L)=20 C. Two-node elements, tridiagonal assembly, Dirichlet values moved to the RHS, Thomas solve. Exact: T = T0 + (TL-T0)x/L + qx(L-x)/(2k). Midpoint error uses the <b>FE interpolant</b> (T<sub>i</sub>+T<sub>i+1</sub>)/2 versus exact.</p>
{conv_t}
<div class="row"><div><img src="{img(P('results','plot_convergence.png'),520,fmt='PNG')}"></div><div><img src="{img(P('results','plot_temperature_profile.png'),520,fmt='PNG')}"></div></div>
<p>Midpoint error = q h&sup2;/(8k) = 125 h&sup2; exactly: slope 2. <b>Debugging insight:</b> nodal values are exact to round-off (1e-13), so a nodal-error check cannot reveal discretisation error or a wrong interpolation; verification therefore uses midpoint error, the convergence slope and an independent dense Gaussian-elimination solver. Three injected solver bugs (source term, two Dirichlet terms) were all caught by the tests (<code>tools/mutation_check.sh</code>).</p>

<h2>4. Performance (Task D)</h2>
<p>{env['workload_cases']} cases x {env['workload_elements']} elements, fixed seed {env['seed']}, {env['warmup_runs']} warm-up + {env['measured_repetitions']} repetitions, median; serial T1 baseline; pool of 1/2/4 workers (never above logical CPUs); pool threads created outside the timed region. Machine: {env['cpu']}, {env['physical_cores']} physical / {env['logical_cores']} logical cores, {env['ram_gib']} GiB RAM, {env['os']}, {env['compiler']}, {env['build_type']}, flags <code>{env['flags']}</code>. Launch-to-ready: 40 ms (reported separately).</p>
<div class="row"><div>{perf_t}<p>Serial and parallel results agree to 0 (limit 1e-8). Scaling is near-linear to the 4 physical cores (3.53x); efficiency 0.88 at 4 workers (memory bandwidth, scheduling, join). Efficiency 1.01 for 1 worker is timing noise (serial spread about 8 %). Workers beyond the logical CPU count are not offered.</p></div><div><img src="{img(P('results','plot_speedup.png'),480,fmt='PNG')}"></div></div>

<h2>5. Embedded design (Task E) - SIMULATED</h2>
<div>{svg}</div>
<p class="small">All device behaviour is simulated (C++ mocks: discovery, capabilities, connect/disconnect, one telemetry message, injected recoverable <code>TIMEOUT</code>, recovery by reconnect; screenshots 08/09). Example boards are assumptions.</p>
{tab(["", "MCU board", "FPGA board", "JTAG adapter"], [
["Link", "USB virtual COM", "USB (on-board JTAG/UART)", "USB; JTAG/SWD to target"],
["Power / domains", "USB 5 V -> 3.3 V VDD", "VCCINT 1.0, VCCAUX 1.8, VCCO 3.3 V", "USB 5 V; logic follows target VTref; never powers target"],
["Connectors", "Arduino/morpho; 10-pin Cortex debug", "Pmod; 14-pin FPGA JTAG", "target-side header differs per target"],
["JTAG ownership", "target: TDO, VTref, nSRST", "target: TDO, VREF", "adapter drives TCK/TMS/TDI/TRST/SRST, reads TDO/VTref"],
["Ground / shifting", "common GND via USB + debug GND pins", "same", "GND first; shifter only for 1.8 V/5 V targets"]])}
<p>A JTAG adapter is a <b>debug interface, not a compute board</b>: it runs no application, adapts to the target's VTref, and its connector, protocol (SWD/JTAG) and pin ownership differ per target. The software discovers by type and capability and assumes no shared connector, voltage or protocol. Pin numbers are left to each board's datasheet. <b>Safe plan:</b> datasheet review, bench supply with current limit, one device at a time, ground first, read-only (IDCODE, loopback) before any write, level shifter/series resistors where domains differ, timeouts and safe disconnect. <b>Limits:</b> no real drivers, wiring, firmware or FPGA programming.</p>

<h2>6. Backpropagation (Task F)</h2>
<p>x=1, y=2, w=.5, b=0, v=1, c=0, lr=.1. z=0.5, h=tanh z=0.4621, y&#770;=0.4621, e=-1.5379, J=1.1825. Gradients: dJ/dc=-1.5379, dJ/dv=-0.7107, dJ/dz=e v(1-h&sup2;)=-1.2095 = dJ/dw = dJ/db. Update: w=0.6209, b=0.1209, v=1.0711, c=0.1538, J=0.6858. C++ central differences (&epsilon;=1e-6): max error 1.1e-10 &le; 1e-5 PASS. Loss falls smoothly (1.18 &rarr; 1e-2 at step 10 &rarr; 1e-20 at step 100): one sample, four parameters, so it fits exactly; this is memorisation, not generalisation. Handwritten derivation: appendix (separate pages).</p>
<div class="row"><div><img src="{img(P('results','plot_backprop_loss.png'),460,fmt='PNG')}"></div><div></div></div>

<h2>7. Test outcomes</h2>
<p>70 C++ unit tests, 0 failed (FEA 12, parsers 24, batch 12, hardware 10, backprop 8, JSON 4); 43 UI end-to-end checks passed (headless Chromium); mutation check 3/3 caught; independent cross-checks (dense solver, Python golden values for the generator, finite differences). Manual on a real Windows 11 PC (i5-11320H): <code>Launch.exe</code> opens the window without a console; benchmark results above come from that PC.</p>

<h2>8. Limitations and AI-use disclosure</h2>
<ul><li>Unsigned <code>Launch.exe</code> (SmartScreen: More info &rarr; Run anyway); hardware is simulated; 1-D thermal model only; ASCII STL only (metres assumed); software 3-D renderer; MSVC build and other Windows versions untested; limits as above.</li>
<li>Claude Sonnet 5.5 (Claude Code) wrote most of the code and docs under my direction; defects it produced (infinite tick loop, single-recv health check, overflow risk, legend overlap, empty flags line, missing output folder) were found and fixed - see <code>docs/AI_USE_LOG.md</code>. The handwritten derivation is my own work. Ownership: <code>docs/OWNERSHIP.md</code>; third-party: cpp-httplib 0.18.3 (MIT).</li></ul>
</body></html>"""
out_html = P("docs", "report.html"); open(out_html, "w", encoding="utf-8").write(html)
with sync_playwright() as pw:
    b = pw.chromium.launch(); pg = b.new_page(); pg.goto("file://" + os.path.abspath(out_html))
    pg.pdf(path=P("docs", "report.pdf"), format="A4", print_background=True, prefer_css_page_size=True); b.close()
from pypdf import PdfReader
n = len(PdfReader(P("docs", "report.pdf")).pages); sz = os.path.getsize(P("docs", "report.pdf"))
print(f"report.pdf: {n} pages, {sz/1024:.0f} KiB"); sys.exit(0 if n <= 6 and sz < 1_000_000 else 1)
