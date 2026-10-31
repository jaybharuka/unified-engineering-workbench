// Hardware view: block diagram plus SIMULATED device adapters (discover, connect, telemetry, recoverable timeout).
import { apiGet, apiPost } from './api.js';
import { $, clear, el, setStatus, showError, hideError } from './util.js';

export async function initHardware() {
  const telemetryLines = [];
  let retry = null;

  async function loadDiagram() {
    try {
      const res = await fetch('assets/embedded_block_diagram.svg');
      $('block-diagram').innerHTML = await res.text();
    } catch (_) { $('block-diagram').textContent = 'Diagram could not be loaded (assets/embedded_block_diagram.svg).'; }
  }

  function deviceCard(d) {
    const connected = d.state === 'connected';
    const act = (label, fn, attrs = {}) => el('button', { class: 'btn', type: 'button', onclick: fn, ...attrs }, label);
    return el('article', { class: 'device', 'aria-label': `${d.id} ${d.type}` },
      el('h4', {}, `${d.id} — ${d.type.replace('_', ' ')} `, el('span', { class: 'badge sim', text: 'SIMULATED' }), ' ',
        el('span', { class: `badge ${connected ? 'ok' : 'off'}`, text: d.state })),
      el('div', { class: 'meta', text: d.name }),
      el('div', { class: 'meta', text: `Host link: ${d.host_link}` }),
      el('div', { class: 'meta', text: `Connector: ${d.connector}` }),
      el('div', { class: 'meta', text: d.compute_capable ? 'General compute board' : 'Debug interface only — not a compute board' }),
      el('div', { class: 'meta', text: `Voltage domains: ${d.voltage_domains.map((v) => `${v.name} ${v.volts} V`).join(' · ')}` }),
      el('div', { class: 'caps' }, d.capabilities.map((c) => el('span', { class: 'chip', text: c }))),
      el('div', { class: 'btn-row' },
        act('Connect', () => call(d.id, 'connect'), { disabled: connected }),
        act('Disconnect', () => call(d.id, 'disconnect'), { disabled: !connected }),
        act('Get telemetry', () => call(d.id, 'telemetry'), { disabled: !connected }),
        act(d.armed_timeouts ? `Timeout armed (${d.armed_timeouts})` : 'Inject timeout fault', () => arm(d.id), { disabled: d.armed_timeouts > 0 })));
  }

  async function refresh() {
    const list = (await apiGet('/api/hw/devices')).devices;
    const box = $('hw-devices');
    clear(box);
    for (const d of list) box.append(deviceCard(d));
    $('hw-events').textContent = (await apiGet('/api/hw/events')).join('\n');
    $('hw-events').scrollTop = $('hw-events').scrollHeight;
  }

  function logTelemetry(msg) {
    telemetryLines.unshift(`${new Date().toLocaleTimeString()}  ${JSON.stringify(msg)}`);
    telemetryLines.length = Math.min(telemetryLines.length, 12);
    $('hw-telemetry').textContent = telemetryLines.join('\n');
  }

  async function call(id, action) {
    hideError($('hw-error'));
    try {
      const r = await apiPost(`/api/hw/${action}`, { id });
      if (action === 'telemetry') logTelemetry(r);
      setStatus(`${id}: ${action} OK (simulated)`, 'ok');
      retry = null;
    } catch (e) {
      showError($('hw-error'), e);
      if (e.recoverable) {
        // Recoverable failure: offer a retry that reconnects first when the link was dropped.
        const again = el('button', { class: 'btn primary', type: 'button', text: 'Retry' });
        again.addEventListener('click', async () => {
          if (e.code === 'NOT_CONNECTED' || action === 'telemetry') await call(id, 'connect');
          else await call(id, action);
        });
        $('hw-error').append(el('div', { class: 'btn-row' }, again));
      }
      setStatus(`${id}: ${action} failed (${e.code || 'error'})`, 'error');
    }
    await refresh().catch(() => {});
  }

  async function arm(id) {
    try { await apiPost('/api/hw/fault', { id, kind: 'timeout', count: 1 }); setStatus(`${id}: timeout armed — the next connect/telemetry will fail once`, 'info'); }
    catch (e) { showError($('hw-error'), e); }
    await refresh().catch(() => {});
  }

  $('hw-refresh').addEventListener('click', () => refresh().then(() => setStatus('Discovery complete (simulated)', 'ok')).catch((e) => showError($('hw-error'), e)));
  loadDiagram();
  try { await refresh(); } catch (e) { showError($('hw-error'), e); }
}
