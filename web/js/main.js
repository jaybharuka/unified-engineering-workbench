// Entry point: tab navigation, lazy view initialisation, keep-alive, health read-out.
import { apiGet, startHeartbeat } from './api.js';
import { $, fmt, setStatus } from './util.js';
import { initGeometry } from './geometry.js';
import { initThermal } from './thermal.js';
import { initPerformance } from './performance.js';
import { initHardware } from './hardware.js';
import { initBackprop } from './backprop.js';

const VIEWS = {
  geometry: { init: initGeometry },
  thermal: { init: initThermal },
  performance: { init: initPerformance },
  hardware: { init: initHardware },
  backprop: { init: initBackprop },
};
const started = new Set();

function show(name) {
  for (const key of Object.keys(VIEWS)) {
    const selected = key === name;
    const tab = $(`tab-${key}`);
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
    $(`view-${key}`).hidden = !selected;
  }
  if (!started.has(name)) {
    started.add(name);
    try { VIEWS[name].init(); } catch (e) { setStatus(`Could not start the ${name} view: ${e.message}`, 'error'); console.error(e); }
  }
  window.dispatchEvent(new Event('resize'));  // canvases that were hidden need a layout pass
  location.hash = name;
}

function initTabs() {
  const names = Object.keys(VIEWS);
  const tablist = $('tabs');
  tablist.addEventListener('click', (e) => {
    const b = e.target.closest('button[role=tab]');
    if (b) show(b.id.replace('tab-', ''));
  });
  tablist.addEventListener('keydown', (e) => {
    const cur = names.findIndex((n) => $(`tab-${n}`).getAttribute('aria-selected') === 'true');
    let next = -1;
    if (e.key === 'ArrowRight') next = (cur + 1) % names.length;
    else if (e.key === 'ArrowLeft') next = (cur + names.length - 1) % names.length;
    else if (e.key === 'Home') next = 0;
    else if (e.key === 'End') next = names.length - 1;
    if (next >= 0) { e.preventDefault(); show(names[next]); $(`tab-${names[next]}`).focus(); }
  });
}

async function showHealth() {
  try {
    const h = await apiGet('/api/health');
    const launch = h.launch_to_ready_ms !== undefined ? ` · launch→ready ${fmt(h.launch_to_ready_ms, 4)} ms` : '';
    $('health').textContent = `Backend v${h.version} ready${launch}`;
    window.__health = h;
  } catch (e) {
    $('health').textContent = 'Backend not reachable';
    setStatus(e.message, 'error');
  }
}

initTabs();
startHeartbeat();
showHealth();
const first = location.hash.replace('#', '');
show(VIEWS[first] ? first : 'geometry');
window.addEventListener('unhandledrejection', (e) => setStatus(`Unexpected error: ${e.reason && e.reason.message ? e.reason.message : e.reason}`, 'error'));
