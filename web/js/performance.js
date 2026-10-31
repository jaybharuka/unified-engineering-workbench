// Performance view: serial vs CPU-parallel batch of thermal solves, run as a cancellable background job.
import { apiGet, apiPost, downloadFrom } from './api.js';
import { Plot } from './plot.js';
import { $, clear, el, fmt, hideError, setBusy, setStatus, showError } from './util.js';

export async function initPerformance() {
  const plot = new Plot($('plot-speedup'), { title: 'Speedup vs workers', xLabel: 'Workers p', yLabel: 'Speedup T₁/Tₚ (×)', yMin: 0 });
  let jobId = null, pollTimer = null, sys = null;

  function renderSys(s) {
    const dl = $('sys-info');
    clear(dl);
    const rows = [
      ['CPU', s.cpu], ['Logical CPUs', String(s.logical_cores)],
      ['Physical cores', s.physical_cores_known ? String(s.physical_cores) : `${s.physical_cores} (not detectable; assumed = logical)`],
      ['RAM', `${fmt(s.ram_gib, 4)} GiB`], ['OS', s.os], ['Compiler', s.compiler], ['Flags', `${s.build_type}: ${s.optimisation_flags || '(none)'}`],
    ];
    for (const [k, v] of rows) dl.append(el('dt', { text: k }), el('dd', { text: v }));
  }

  function renderWorkers(s) {
    const box = $('b-workers');
    clear(box);
    const logical = s.logical_cores;
    for (const w of [1, 2, 4]) {
      const disabled = w > logical && !(logical === 1 && w === 2);
      const cb = el('input', { type: 'checkbox', id: `w-${w}`, value: String(w), checked: !disabled, disabled });
      box.append(el('label', {}, cb, ` ${w}${disabled ? ` (> ${logical} logical CPU${logical > 1 ? 's' : ''})` : ''}`));
    }
    if (logical === 1) box.append(el('p', { class: 'hint', text: 'Single logical CPU: the 2-worker run demonstrates the parallel path; speedup cannot be assessed here.' }));
  }

  function readForm() {
    const num = (id) => Number($(id).value);
    const workers = [...$('b-workers').querySelectorAll('input:checked')].map((c) => Number(c.value));
    return { cases: num('b-cases'), elements: num('b-elements'), seed: num('b-seed'), warmup: num('b-warmup'), repetitions: num('b-reps'), workers };
  }

  function setRunning(running) {
    $('perf-run').disabled = running; $('perf-cancel').disabled = !running;
    for (const id of ['b-cases', 'b-elements', 'b-seed', 'b-warmup', 'b-reps']) $(id).disabled = running;
    $('perf-progress').hidden = !running;
    setBusy(running);
  }

  function renderReport(report) {
    renderSys(report.system);
    const tb = $('perf-table').querySelector('tbody');
    clear(tb);
    for (const r of report.rows) {
      tb.append(el('tr', {},
        el('td', { text: r.implementation }), el('td', { text: String(r.workers) + (r.oversubscribed ? ' (oversubscribed)' : '') }),
        el('td', { text: fmt(r.median_s, 5) }), el('td', { text: fmt(r.speedup, 4) }), el('td', { text: fmt(r.efficiency, 4) }),
        el('td', { text: fmt(r.max_abs_diff_C, 3) }), el('td', { class: r.agrees ? 'good' : 'bad', text: r.agrees ? 'yes' : 'NO' })));
    }
    const notes = $('perf-notes');
    clear(notes);
    for (const n of report.notes) notes.append(el('li', { text: n }));
    const pool = report.rows.filter((r) => r.implementation === 'pool');
    const ps = pool.map((r) => r.workers);
    plot.setOptions({ xTicks: ps, xMin: Math.min(...ps) - 0.25, xMax: Math.max(...ps) + 0.25 });
    plot.setSeries([
      { name: 'Measured (pool)', x: ps, y: pool.map((r) => r.speedup), markers: true, markerSize: 5 },
      { name: 'Ideal (= p)', x: ps, y: ps, dash: [6, 4], color: '#888', width: 1.5 },
    ]);
    for (const id of ['perf-csv', 'perf-csv2', 'perf-png']) $(id).disabled = false;
    const h = window.__health;
    $('perf-launch').textContent = h
      ? `Launch-to-ready: ${h.launch_to_ready_ms !== undefined ? `${fmt(h.launch_to_ready_ms, 4)} ms (Launch.exe start → server listening)` : 'n/a (not started through Launch.exe)'}; server process start → listening: ${fmt(h.ready_ms_since_process_start, 3)} ms. Reported separately from compute time.`
      : '';
  }

  async function poll() {
    try {
      const j = await apiGet(`/api/jobs/${jobId}`);
      $('perf-bar').style.width = `${Math.round(j.progress.fraction * 100)}%`;
      $('perf-stage').textContent = `${j.stage} — ${Math.round(j.progress.fraction * 100)}%`;
      if (j.state === 'running') { pollTimer = setTimeout(poll, 250); return; }
      setRunning(false);
      if (j.state === 'done') { renderReport(j.report); setStatus(`Benchmark finished in ${fmt(j.elapsed_s, 3)} s`, 'ok'); }
      else if (j.state === 'cancelled') { $('perf-stage').textContent = 'Cancelled. No partial results were kept.'; setStatus('Benchmark cancelled', 'info'); }
      else { showError($('perf-error'), { message: j.error || 'Benchmark failed.' }); setStatus('Benchmark failed', 'error'); }
    } catch (e) { setRunning(false); showError($('perf-error'), e); setStatus('Lost contact with the backend', 'error'); }
  }

  $('perf-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    hideError($('perf-error'));
    const body = readForm();
    if (!body.workers.length) { showError($('perf-error'), { message: 'Select at least one worker count.' }); return; }
    for (const id of ['perf-csv', 'perf-csv2', 'perf-png']) $(id).disabled = true;
    try {
      setRunning(true); $('perf-bar').style.width = '0%'; $('perf-stage').textContent = 'starting …'; setStatus('Benchmark running …');
      jobId = (await apiPost('/api/jobs/benchmark', body)).job_id;
      poll();
    } catch (err) { setRunning(false); showError($('perf-error'), err); setStatus('Could not start the benchmark', 'error'); }
  });
  $('perf-cancel').addEventListener('click', async () => {
    try { await apiPost(`/api/jobs/${jobId}/cancel`, {}); $('perf-stage').textContent = 'cancelling …'; } catch (e) { showError($('perf-error'), e); }
  });
  $('perf-csv').addEventListener('click', () => downloadFrom(`/api/jobs/${jobId}/csv?kind=summary`, { filename: 'performance_summary.csv' }).catch((e) => showError($('perf-error'), e)));
  $('perf-csv2').addEventListener('click', () => downloadFrom(`/api/jobs/${jobId}/csv?kind=repetitions`, { filename: 'performance_repetitions.csv' }).catch((e) => showError($('perf-error'), e)));
  $('perf-png').addEventListener('click', () => plot.exportPng('plot_speedup.png'));
  window.addEventListener('resize', () => plot.draw());

  try { sys = await apiGet('/api/sysinfo'); renderSys(sys); renderWorkers(sys); }
  catch (e) { showError($('perf-error'), e); }
}
