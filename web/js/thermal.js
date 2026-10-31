// Thermal Analysis view: 1-D FE solution, error profile, mesh convergence, CSV/PNG export.
import { apiPost, downloadFrom } from './api.js';
import { Plot } from './plot.js';
import { $, clear, el, fmt, hideError, setBusy, setStatus, showError } from './util.js';

const DEFAULTS = { L: 1, A: 1e-4, k: 10, q: 1e4, T0: 100, TL: 20, meshes: '10, 20, 40, 80' };
const FIELDS = ['L', 'A', 'k', 'q', 'T0', 'TL'];
let last = null;  // last response (for export)

function readInputs() {
  const problems = [];
  const body = {};
  for (const f of FIELDS) {
    const input = $(`p-${f}`);
    const v = Number(input.value);
    const bad = input.value.trim() === '' || !Number.isFinite(v);
    input.setAttribute('aria-invalid', String(bad));
    if (bad) problems.push(`${f} must be a number.`);
    body[f] = v;
  }
  const rule = (cond, msg, ids) => { if (cond) { problems.push(msg); ids.forEach((i) => $(i).setAttribute('aria-invalid', 'true')); } };
  rule(body.L <= 0, 'Length L must be greater than 0.', ['p-L']);
  rule(body.A <= 0, 'Area A must be greater than 0.', ['p-A']);
  rule(body.k <= 0, 'Conductivity k must be greater than 0.', ['p-k']);
  rule(body.T0 < -273.15 || body.TL < -273.15, 'Temperatures must be above −273.15 °C.', ['p-T0', 'p-TL']);
  const meshInput = $('p-meshes');
  const parts = meshInput.value.split(/[ ,;]+/).filter(Boolean).map(Number);
  const meshesBad = !parts.length || parts.length > 8 || parts.some((n) => !Number.isInteger(n) || n < 1 || n > 20000);
  meshInput.setAttribute('aria-invalid', String(meshesBad));
  if (meshesBad) problems.push('Elements N must be 1–8 whole numbers between 1 and 20000, e.g. 10, 20, 40, 80.');
  body.meshes = parts;
  return { body, problems };
}

export function initThermal() {
  const plotT = new Plot($('plot-temp'), { title: 'FE solution vs exact', xLabel: 'Position x (m)', yLabel: 'Temperature T (°C)' });
  const plotE = new Plot($('plot-err'), { title: 'Error at element midpoints (FE interpolation − exact)', xLabel: 'Position x (m)', yLabel: '|T_FE − T_exact| (°C)', yMin: 0 });
  const plotC = new Plot($('plot-conv'), { title: 'Mesh convergence (maximum midpoint error)', xLabel: 'Element size h (m)', yLabel: 'Max |error| at midpoints (°C)', xScale: 'log', yScale: 'log' });

  function render(data) {
    last = data;
    const exact = { name: 'Exact', x: data.exact_curve.x_m, y: data.exact_curve.T_exact_C, color: getComputedStyle(document.documentElement).getPropertyValue('--plot-fg').trim() || '#444', width: 2.5, dash: [6, 4] };
    plotT.setSeries([exact, ...data.runs.map((r) => ({ name: `FE, N = ${r.n_elements}`, x: r.x_m, y: r.T_fe_C, markers: r.n_elements <= 40, width: 1.8 }))]);
    plotE.setSeries(data.runs.map((r) => ({ name: `N = ${r.n_elements}`, x: r.mid_x_m, y: r.mid_T_fe_C.map((v, i) => Math.abs(v - r.mid_T_exact_C[i])), markers: r.n_elements <= 40, width: 1.8 })));
    const hs = data.convergence.map((c) => c.h_m), es = data.convergence.map((c) => c.max_midpoint_error_C);
    const c0 = es[0] / (hs[0] ** 2);
    plotC.setSeries([
      { name: 'Max midpoint error', x: hs, y: es, markers: true, color: '#1f77b4' },
      { name: 'Reference slope 2 (∝ h²)', x: hs, y: hs.map((h) => c0 * h * h), color: '#888', dash: [5, 4], width: 1.5 },
    ]);
    const tb = $('conv-table').querySelector('tbody');
    clear(tb);
    for (const c of data.convergence) {
      tb.append(el('tr', {}, el('td', { text: String(c.n_elements) }), el('td', { text: fmt(c.h_m, 5) }), el('td', { text: fmt(c.max_node_error_C, 4) }),
        el('td', { text: fmt(c.max_midpoint_error_C, 6) }), el('td', { text: c.observed_order ? fmt(c.observed_order, 5) : '–' })));
    }
    const sel = $('exp-n');
    clear(sel);
    for (const r of data.runs) sel.append(el('option', { value: String(r.n_elements), text: String(r.n_elements) }));
    for (const id of ['exp-conv', 'exp-sol', 'exp-mid', 'png-t', 'png-e', 'png-c']) $(id).disabled = false;
  }

  async function run(e) {
    if (e) e.preventDefault();
    hideError($('thermal-error'));
    const { body, problems } = readInputs();
    if (problems.length) { showError($('thermal-error'), { message: problems[0], details: problems }); setStatus('Fix the highlighted inputs', 'error'); return; }
    setBusy(true); setStatus('Solving …'); $('thermal-run').disabled = true;
    try {
      render(await apiPost('/api/thermal/solve', body));
      setStatus('Thermal solution ready', 'ok');
    } catch (err) { showError($('thermal-error'), err); setStatus('Solve failed', 'error'); }
    finally { setBusy(false); $('thermal-run').disabled = false; }
  }

  async function exportCsv(kind, filename) {
    const { body, problems } = readInputs();
    if (problems.length) { setStatus(problems[0], 'error'); return; }
    body.kind = kind; body.n = Number($('exp-n').value || body.meshes[0]);
    try { await downloadFrom('/api/thermal/export', { method: 'POST', body, filename }); setStatus(`Saved ${filename}`, 'ok'); }
    catch (err) { showError($('thermal-error'), err); setStatus('Export failed', 'error'); }
  }

  $('thermal-form').addEventListener('submit', run);
  $('thermal-reset').addEventListener('click', () => {
    for (const f of FIELDS) { $(`p-${f}`).value = String(DEFAULTS[f]); $(`p-${f}`).setAttribute('aria-invalid', 'false'); }
    $('p-meshes').value = DEFAULTS.meshes; $('p-meshes').setAttribute('aria-invalid', 'false');
    run();
  });
  $('exp-conv').addEventListener('click', () => exportCsv('convergence', 'thermal_convergence.csv'));
  $('exp-sol').addEventListener('click', () => exportCsv('solution', `thermal_solution_N${$('exp-n').value}.csv`));
  $('exp-mid').addEventListener('click', () => exportCsv('midpoints', `thermal_midpoints_N${$('exp-n').value}.csv`));
  $('png-t').addEventListener('click', () => plotT.exportPng('plot_temperature_vs_x.png'));
  $('png-e').addEventListener('click', () => plotE.exportPng('plot_midpoint_error.png'));
  $('png-c').addEventListener('click', () => plotC.exportPng('plot_convergence.png'));
  window.addEventListener('resize', () => { plotT.draw(); plotE.draw(); plotC.draw(); });
  run();
}
