// Backprop view: runs the C++ implementation, shows gradient check and the loss curve.
import { apiPost } from './api.js';
import { Plot } from './plot.js';
import { $, clear, downloadText, el, fmt, hideError, setBusy, setStatus, showError } from './util.js';

export function initBackprop() {
  const plot = new Plot($('plot-loss'), { title: 'Training loss', xLabel: 'Update step (0 = initial, 1 = hand-calculated update)', yLabel: 'Loss J = ½(ŷ − y)²', yScale: 'log', legend: false });
  let loss = null;

  async function run(e) {
    e.preventDefault();
    hideError($('bp-error'));
    const body = {};
    const ids = { x: 'bp-x', y: 'bp-y', w: 'bp-w', b: 'bp-b', v: 'bp-v', c: 'bp-c', lr: 'bp-lr', steps: 'bp-steps' };
    for (const [k, id] of Object.entries(ids)) {
      const v = Number($(id).value);
      if ($(id).value.trim() === '' || !Number.isFinite(v)) { showError($('bp-error'), { message: `${k} must be a number.` }); return; }
      body[k] = v;
    }
    setBusy(true); setStatus('Running …');
    try {
      const r = await apiPost('/api/backprop/run', body);
      loss = r.loss;
      const tb = $('bp-table').querySelector('tbody');
      clear(tb);
      for (const [name, key, errKey] of [['w', 'dJ_dw', 'w'], ['b', 'dJ_db', 'b'], ['v', 'dJ_dv', 'v'], ['c', 'dJ_dc', 'c']]) {
        tb.append(el('tr', {}, el('td', { text: `∂J/∂${name}` }), el('td', { text: fmt(r.analytic_gradient[key], 10) }), el('td', { text: fmt(r.numeric_gradient[key], 10) }), el('td', { text: fmt(r.abs_error[errKey], 3) })));
      }
      $('bp-verdict').textContent = `${r.gradient_check_pass ? 'PASS' : 'FAIL'}: max |analytic − numeric| = ${fmt(r.abs_error.max, 3)} (tolerance ${r.tolerance}). `
        + `After the first update: w=${fmt(r.after_one_update.w, 8)}, b=${fmt(r.after_one_update.b, 8)}, v=${fmt(r.after_one_update.v, 8)}, c=${fmt(r.after_one_update.c, 8)}, J=${fmt(r.after_one_update.loss, 8)}. `
        + `Loss after ${loss.length - 1} updates: ${fmt(loss[loss.length - 1], 4)}.`;
      plot.setSeries([{ name: 'loss', x: loss.map((_, i) => i), y: loss.map((v) => Math.max(v, 1e-300)), markers: loss.length <= 60 }]);
      $('bp-csv').disabled = false;
      setStatus('Backprop run complete', 'ok');
    } catch (err) { showError($('bp-error'), err); setStatus('Run failed', 'error'); }
    finally { setBusy(false); }
  }

  $('bp-form').addEventListener('submit', run);
  $('bp-csv').addEventListener('click', () => downloadText(`step,loss\n${loss.map((v, i) => `${i},${v}`).join('\n')}\n`, 'backprop_loss.csv'));
  window.addEventListener('resize', () => plot.draw());
  run({ preventDefault() {} });
}
