"""End-to-end UI check (developer tooling, not part of the product).
Starts wb_server, drives the web UI in headless Chromium, asserts visible behaviour, saves screenshots.
Usage: python3 tests/ui/ui_smoke.py <path-to-wb_server> <project-root> [screenshot-dir]
"""
import os, re, subprocess, sys, tempfile, time
from playwright.sync_api import sync_playwright

server, root = sys.argv[1], sys.argv[2]
shots = sys.argv[3] if len(sys.argv) > 3 else None
if shots: os.makedirs(shots, exist_ok=True)
portfile = tempfile.mktemp()
proc = subprocess.Popen([server, "--root", root, "--port-file", portfile], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
for _ in range(100):
    if os.path.exists(portfile) and open(portfile).read().strip(): break
    time.sleep(0.05)
port = open(portfile).read().strip()
base = f"http://127.0.0.1:{port}/"
failures, console = [], []

def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond: failures.append(msg)

def shot(page, name):
    if shots: page.screenshot(path=os.path.join(shots, name + ".png"), full_page=False)

try:
    with sync_playwright() as p:
        browser = p.chromium.launch()
        ctx = browser.new_context(viewport={"width": 1440, "height": 900})
        page = ctx.new_page()
        page.on("console", lambda m: console.append((m.type, m.text)) if m.type in ("error", "warning") else None)
        page.on("pageerror", lambda e: console.append(("pageerror", str(e))))
        page.goto(base)

        print("Geometry / STL")
        page.wait_for_selector("#stl-stats dd")
        stats = page.inner_text("#stl-stats")
        check("1,008" in stats or "1008" in stats, "example STL loaded with 1008 triangles")
        check(page.is_visible("#stl-canvas"), "STL canvas visible")
        before = page.evaluate("document.getElementById('stl-canvas').toDataURL().length")
        box = page.locator("#stl-canvas").bounding_box()
        page.mouse.move(box["x"] + 300, box["y"] + 300); page.mouse.down(); page.mouse.move(box["x"] + 420, box["y"] + 260, steps=6); page.mouse.up()
        page.wait_for_timeout(400)
        after = page.evaluate("document.getElementById('stl-canvas').toDataURL().length")
        check(before != after, "dragging rotates the view (canvas changed)")
        shot(page, "01_geometry_stl")
        page.click("#stl-tools >> text=Top"); page.wait_for_timeout(300)
        page.click("#stl-tools >> text=Fit")
        page.check("#wire-toggle"); page.wait_for_timeout(300); shot(page, "02_geometry_stl_wireframe"); page.uncheck("#wire-toggle")

        for name, content, expect in [
            ("empty.stl", b"", "empty"),
            ("garbage.stl", b"hello world", "solid"),
            ("nan.stl", b"solid t\nfacet normal 0 0 1\nouter loop\nvertex nan 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid t\n", "line 4"),
        ]:
            page.set_input_files("#stl-file", files=[{"name": name, "mimeType": "text/plain", "buffer": content}])
            try:
                page.wait_for_function("([id, w]) => { const e = document.getElementById(id); return !e.hidden && e.textContent.toLowerCase().includes(w); }", arg=["stl-error", expect], timeout=5000)
            except Exception:
                pass
            txt = page.inner_text("#stl-error").lower()
            check(expect in txt, f"{name}: error mentions '{expect}' -> {txt[:90]!r}")
        shot(page, "03_stl_error")
        page.click("#stl-load-sample"); page.wait_for_selector("#stl-error", state="hidden")
        check(True, "error clears after loading a valid example")

        print("Geometry / CSV")
        page.click("#seg-csv")
        page.click("#csv-load-sample")
        page.wait_for_selector("#csv-stats dd")
        check("1,517" in page.inner_text("#csv-stats") or "1517" in page.inner_text("#csv-stats"), "example CSV loaded (1517 nodes)")
        check(page.is_visible("#legend"), "temperature legend visible")
        check("°C" in page.inner_text("#legend"), "legend shows units (°C)")
        tick_text = page.inner_text("#legend-ticks")
        check("193" in tick_text and "20" in tick_text, f"legend ticks show range ({tick_text.split()})")
        page.fill("#node-find", "100"); page.click("#node-find-btn")
        info = page.inner_text("#node-info")
        check("Node 100" in info and "°C" in info, f"node inspector by id -> {info!r}")
        shot(page, "04_geometry_csv")
        found = False
        cbox = page.locator("#csv-canvas").bounding_box()
        page.click("#csv-tools >> text=Front")
        page.wait_for_timeout(300)
        for i in range(60):
            x = cbox["x"] + cbox["width"] / 2 + 250 * (((i * 7) % 21) - 10) / 10
            y = cbox["y"] + cbox["height"] / 2 + 6 * (((i * 3) % 7) - 3)
            page.mouse.click(x, y)
            if "Node" in page.inner_text("#node-info") and "Node 100" not in page.inner_text("#node-info"):
                found = True; break
        check(found, "clicking a point selects a node")
        for name, content, expect in [
            ("bad.csv", b"node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,nan\n", "line 2"),
            ("hdr.csv", b"id,x,y,z,t\n1,0,0,0,5\n", "header"),
            ("empty.csv", b"", "empty"),
            ("dup.csv", b"node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,5\n1,1,0,0,6\n", "duplicate"),
        ]:
            page.set_input_files("#csv-file", files=[{"name": name, "mimeType": "text/csv", "buffer": content}])
            try:
                page.wait_for_function("([id, w]) => { const e = document.getElementById(id); return !e.hidden && e.textContent.toLowerCase().includes(w); }", arg=["csv-error", expect], timeout=5000)
            except Exception:
                pass
            txt = page.inner_text("#csv-error").lower()
            check(expect in txt, f"{name}: error mentions '{expect}' -> {txt[:90]!r}")
        shot(page, "05_csv_error")

        print("Thermal")
        page.click("#tab-thermal")
        page.wait_for_selector("#conv-table tbody tr")
        rows = page.eval_on_selector_all("#conv-table tbody tr", "rs => rs.map(r => [...r.cells].map(c => c.textContent))")
        check([r[0] for r in rows] == ["10", "20", "40", "80"], "convergence table has N = 10,20,40,80")
        check(abs(float(rows[0][3]) - 1.25) < 1e-6 and abs(float(rows[3][3]) - 0.01953125) < 1e-6, f"midpoint errors 1.25 ... 0.0195 ({rows[0][3]}, {rows[3][3]})")
        check(abs(float(rows[3][4]) - 2.0) < 1e-3, f"observed order ~2 ({rows[3][4]})")
        page.wait_for_timeout(300); shot(page, "06_thermal")
        page.fill("#p-k", "-1"); page.click("#thermal-run")
        check(page.is_visible("#thermal-error"), "invalid k shows an error")
        check(page.get_attribute("#p-k", "aria-invalid") == "true", "invalid field is marked aria-invalid")
        page.fill("#p-k", "10")
        page.fill("#p-meshes", "5, 15"); page.click("#thermal-run"); page.wait_for_timeout(600)
        rows = page.eval_on_selector_all("#conv-table tbody tr", "rs => rs.map(r => r.cells[0].textContent)")
        check(rows == ["5", "15"], "custom mesh list re-solves")
        page.click("#thermal-reset"); page.wait_for_timeout(600)
        with page.expect_download() as dl: page.click("#exp-conv")
        path = dl.value.path(); text = open(path).read()
        check(text.startswith("n_elements,h_m,max_node_error_C,max_midpoint_error_C,observed_order"), "convergence CSV downloads with header")
        with page.expect_download() as dl: page.click("#exp-sol")
        check(open(dl.value.path()).read().startswith("x_m,T_fe_C,T_exact_C,abs_error_C"), "solution CSV downloads")

        print("Performance")
        page.click("#tab-performance")
        page.wait_for_selector("#sys-info dd")
        check("Logical CPUs" in page.inner_text("#sys-info"), "system information shown")
        page.fill("#b-cases", "3000"); page.fill("#b-elements", "200"); page.fill("#b-reps", "3")
        page.click("#perf-run")
        check(page.is_disabled("#perf-run") and page.is_enabled("#perf-cancel"), "UI stays responsive: run disabled, cancel enabled while running")
        page.wait_for_selector("#perf-table tbody tr", timeout=60000)
        prow = page.eval_on_selector_all("#perf-table tbody tr", "rs => rs.map(r => [...r.cells].map(c => c.textContent))")
        check(prow[0][0] == "serial" and all(r[-1] == "yes" for r in prow), f"serial + pool rows, all agree ({[r[:2] for r in prow]})")
        page.wait_for_timeout(300); shot(page, "07_performance")
        page.fill("#b-cases", "400000"); page.fill("#b-elements", "50"); page.fill("#b-reps", "10")
        page.click("#perf-run"); page.wait_for_timeout(700); page.click("#perf-cancel")
        page.wait_for_function("document.getElementById('perf-stage').textContent.includes('Cancelled')", timeout=20000)
        check(page.is_enabled("#perf-run"), "cancel returns the UI to idle")
        page.fill("#b-cases", "0"); page.click("#perf-run"); page.wait_for_selector("#perf-error:not([hidden])", timeout=5000)
        check("cases" in page.inner_text("#perf-error").lower(), "invalid case count is rejected with a message")

        print("Hardware")
        page.click("#tab-hardware")
        page.wait_for_selector(".device")
        check(page.locator(".device").count() == 3, "three devices (MCU, FPGA, JTAG)")
        check(page.locator(".badge.sim").count() >= 3, "SIMULATED labels present")
        page.wait_for_selector("#block-diagram svg", timeout=5000)
        check(page.is_visible("#block-diagram svg"), "block diagram rendered")
        page.locator(".device").nth(0).get_by_text("Connect", exact=True).click()
        page.wait_for_selector(".device:first-child .badge.ok")
        page.locator(".device").nth(0).get_by_text("Get telemetry").click()
        page.wait_for_function("document.getElementById('hw-telemetry').textContent.includes('mcu_telemetry')")
        for i, t in enumerate(["fpga_telemetry", "jtag_status"], start=1):
            page.locator(".device").nth(i).get_by_text("Connect", exact=True).click()
            page.wait_for_selector(f".device:nth-child({i+1}) .badge.ok")
            page.locator(".device").nth(i).get_by_text("Get telemetry").click()
            page.wait_for_function(f"document.getElementById('hw-telemetry').textContent.includes('{t}')")
        check(True, "all three device types connect and report telemetry")
        page.locator(".device").nth(0).get_by_text("Inject timeout fault").click()
        page.locator(".device").nth(0).get_by_text("Get telemetry").click()
        page.wait_for_selector("#hw-error:not([hidden])")
        err = page.inner_text("#hw-error")
        check("timeout" in err.lower() and "Retry" in err, "timeout failure shown with actionable message and Retry")
        shot(page, "08_hardware_timeout")
        page.click("#hw-error >> text=Retry"); page.wait_for_selector("#hw-error", state="hidden")
        page.wait_for_selector(".device:first-child .badge.ok")
        check(True, "Retry recovers the device")
        shot(page, "09_hardware_recovered")

        print("Backprop")
        page.click("#tab-backprop")
        page.wait_for_selector("#bp-table tbody tr")
        check("PASS" in page.inner_text("#bp-verdict"), "gradient check PASS")
        page.wait_for_timeout(300); shot(page, "10_backprop")

        print("Accessibility / responsive")
        page.set_viewport_size({"width": 390, "height": 800})
        for tab in ["geometry", "thermal", "performance", "hardware", "backprop"]:
            page.click(f"#tab-{tab}"); page.wait_for_timeout(250)
            w = page.evaluate("[document.documentElement.scrollWidth, window.innerWidth]")
            check(w[0] <= w[1] + 2, f"{tab}: no horizontal page scroll at 390 px ({w[0]} vs {w[1]})")
        page.click("#tab-thermal"); page.wait_for_timeout(300); shot(page, "11_thermal_mobile")
        ctx.close()
        dark = browser.new_context(viewport={"width": 1280, "height": 800}, color_scheme="dark")
        dp = dark.new_page(); dp.goto(base + "#thermal"); dp.wait_for_selector("#conv-table tbody tr"); dp.wait_for_timeout(500); shot(dp, "12_thermal_dark")
        browser.close()
finally:
    proc.terminate()

# 4xx "Failed to load resource" lines are the deliberate negative tests (malformed uploads); anything else is a real error.
errs = [c for c in console if c[0] in ("error", "pageerror") and not re.search(r"status of (4\d\d|504)", c[1])]
check(not errs, f"no console errors ({errs[:3]})")
print(f"\n{len(failures)} failure(s)")
sys.exit(1 if failures else 0)
