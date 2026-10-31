// Geometry view: ASCII STL triangles and ANSYS nodal-temperature point cloud.
import { apiGet, apiPostRaw } from './api.js';
import { cssGradient } from './colormap.js';
import { Viewer3D } from './viewer3d.js';
import { $, b64ToFloat32, b64ToInt32, clear, el, fmt, fmtBytes, hideError, setBusy, setStatus, showError } from './util.js';

const MAX_FILE_BYTES = 256 * 1024 * 1024;

function buildTools(container, viewer, { wire = false, points = false } = {}) {
  const btn = (label, fn, title) => el('button', { class: 'btn', type: 'button', title: title || label, onclick: fn }, label);
  container.append(
    btn('Fit', () => viewer.fit(), 'Fit to view (double-click also works)'),
    btn('Front', () => viewer.setView('front'), 'View from -Y'), btn('Top', () => viewer.setView('top'), 'View from +Z'),
    btn('Right', () => viewer.setView('right'), 'View from +X'), btn('Iso', () => viewer.setView('iso')));
  if (wire) {
    const cb = el('input', { type: 'checkbox', id: 'wire-toggle' });
    cb.addEventListener('change', () => { viewer.wire = cb.checked; viewer.render(); });
    container.append(el('label', {}, cb, 'Wireframe'));
  }
  if (points) {
    const range = el('input', { type: 'range', min: '1', max: '10', value: String(viewer.pointSize), 'aria-label': 'Point size' });
    range.addEventListener('input', () => { viewer.pointSize = Number(range.value); viewer.render(); });
    container.append(el('label', {}, 'Point size', range));
  }
}

async function readFileText(file) {
  if (file.size > MAX_FILE_BYTES) {
    const e = new Error(`The file is ${fmtBytes(file.size)}, above the ${fmtBytes(MAX_FILE_BYTES)} limit.`);
    e.hint = 'Use a smaller or coarser export.';
    throw e;
  }
  return file.text();
}

function setStats(dl, rows) {
  clear(dl);
  for (const [k, v] of rows) dl.append(el('dt', { text: k }), el('dd', { text: v }));
}

function setWarnings(ul, list) {
  clear(ul);
  for (const w of list || []) ul.append(el('li', { text: w }));
}

function fillSelect(select, items) {
  clear(select);
  if (!items.length) select.append(el('option', { value: '', text: '(none found)' }));
  for (const it of items) select.append(el('option', { value: it.name, text: `${it.name} (${fmtBytes(it.bytes)})` }));
}

function wireDrop(wrap, handler) {
  wrap.addEventListener('dragover', (e) => { e.preventDefault(); wrap.classList.add('drop'); });
  wrap.addEventListener('dragleave', () => wrap.classList.remove('drop'));
  wrap.addEventListener('drop', (e) => {
    e.preventDefault(); wrap.classList.remove('drop');
    const f = e.dataTransfer.files && e.dataTransfer.files[0];
    if (f) handler(f);
  });
}

export function initGeometry() {
  // ---------------- STL ----------------
  const stlViewer = new Viewer3D($('stl-canvas'));
  buildTools($('stl-tools'), stlViewer, { wire: true });

  async function showMesh(data, label) {
    const positions = b64ToFloat32(data.positions_b64);
    stlViewer.setMesh(positions, { min: data.bbox_min_m, max: data.bbox_max_m });
    const size = data.bbox_max_m.map((v, i) => v - data.bbox_min_m[i]);
    setStats($('stl-stats'), [
      ['File', label], ['Solid name', data.name || '(unnamed)'],
      ['Triangles', `${data.triangles.toLocaleString()}${data.display_triangles < data.triangles ? ` (showing ${data.display_triangles.toLocaleString()})` : ''}`],
      ['Vertices', data.vertices.toLocaleString()],
      ['Size X×Y×Z', `${fmt(size[0], 5)} × ${fmt(size[1], 5)} × ${fmt(size[2], 5)} m`],
      ['Surface area', `${fmt(data.surface_area_m2, 5)} m²`],
      ['Degenerate tris', String(data.degenerate_triangles)],
      ['Parsed in', `${fmt(data.parse_ms, 4)} ms (C++)`],
      ['Units', 'assumed metres (STL has no unit field)'],
    ]);
    setWarnings($('stl-warnings'), data.warnings);
    $('stl-canvas').setAttribute('aria-label', `3D view of ${data.triangles} STL triangles from ${label}. Use the mouse or arrow keys to change the view.`);
  }

  async function loadStlText(text, label) {
    hideError($('stl-error'));
    setBusy(true); setStatus(`Parsing ${label} …`);
    try {
      const data = await apiPostRaw('/api/geometry/parse', text);
      await showMesh(data, label);
      setStatus(`Loaded ${label}: ${data.triangles.toLocaleString()} triangles`, 'ok');
    } catch (e) {
      showError($('stl-error'), e); setStatus(`Could not load ${label}`, 'error');
    } finally { setBusy(false); }
  }

  async function loadStlFile(file) {
    try { await loadStlText(await readFileText(file), file.name); }
    catch (e) { showError($('stl-error'), e); setStatus('File rejected', 'error'); }
  }

  $('stl-file').addEventListener('change', (e) => { const f = e.target.files[0]; if (f) loadStlFile(f); });
  wireDrop($('stl-wrap'), loadStlFile);
  $('stl-load-sample').addEventListener('click', async () => {
    const name = $('stl-sample').value;
    if (!name) return;
    hideError($('stl-error')); setBusy(true); setStatus(`Loading example ${name} …`);
    try { await showMesh(await apiGet(`/api/geometry/sample?name=${encodeURIComponent(name)}`), name); setStatus(`Loaded example ${name}`, 'ok'); }
    catch (e) { showError($('stl-error'), e); setStatus('Could not load the example', 'error'); }
    finally { setBusy(false); }
  });

  // ---------------- CSV ----------------
  const csvViewer = new Viewer3D($('csv-canvas'), { onPick: (i) => showNode(i) });
  buildTools($('csv-tools'), csvViewer, { points: true });
  let cloud = null;

  function showNode(i) {
    if (!cloud) return;
    const x = cloud.xyz[i * 3], y = cloud.xyz[i * 3 + 1], z = cloud.xyz[i * 3 + 2];
    const info = $('node-info');
    clear(info);
    info.append(
      el('div', {}, el('strong', { text: `Node ${cloud.ids[i]}` })),
      el('div', { text: `T = ${fmt(cloud.t[i], 7)} °C` }),
      el('div', { text: `x = ${fmt(x, 7)} m` }), el('div', { text: `y = ${fmt(y, 7)} m` }), el('div', { text: `z = ${fmt(z, 7)} m` }));
  }

  function showLegend(tmin, tmax) {
    $('legend').hidden = false;
    $('legend-bar').style.background = cssGradient('to top');
    const ticks = $('legend-ticks');
    clear(ticks);
    for (let i = 4; i >= 0; i--) ticks.append(el('span', { text: fmt(tmin + ((tmax - tmin) * i) / 4, 5) }));
  }

  function showCloud(data, label) {
    cloud = { xyz: b64ToFloat32(data.xyz_b64), t: b64ToFloat32(data.t_b64), ids: b64ToInt32(data.id_b64), byId: null };
    csvViewer.setPoints(cloud.xyz, cloud.t, data.t_min_C, data.t_max_C, { min: data.bbox_min_m, max: data.bbox_max_m });
    showLegend(data.t_min_C, data.t_max_C);
    const size = data.bbox_max_m.map((v, i) => v - data.bbox_min_m[i]);
    setStats($('csv-stats'), [
      ['File', label], ['Nodes', `${data.nodes.toLocaleString()}${data.display_nodes < data.nodes ? ` (showing ${data.display_nodes.toLocaleString()})` : ''}`],
      ['Temperature range', `${fmt(data.t_min_C, 6)} … ${fmt(data.t_max_C, 6)} °C`],
      ['Size X×Y×Z', `${fmt(size[0], 5)} × ${fmt(size[1], 5)} × ${fmt(size[2], 5)} m`],
      ['Units', 'x, y, z in m; temperature in °C'], ['Parsed in', `${fmt(data.parse_ms, 4)} ms (C++)`],
    ]);
    setWarnings($('csv-warnings'), data.warnings);
    $('node-info').textContent = 'Click a point in the view to inspect a node.';
    $('csv-canvas').setAttribute('aria-label', `3D point cloud of ${data.nodes} nodes coloured by temperature from ${data.t_min_C} to ${data.t_max_C} degrees Celsius. Click a point to inspect it.`);
  }

  async function loadCsvText(text, label) {
    hideError($('csv-error'));
    setBusy(true); setStatus(`Parsing ${label} …`);
    try {
      const data = await apiPostRaw('/api/results/parse', text);
      showCloud(data, label);
      setStatus(`Loaded ${label}: ${data.nodes.toLocaleString()} nodes`, 'ok');
    } catch (e) {
      showError($('csv-error'), e); setStatus(`Could not load ${label}`, 'error');
    } finally { setBusy(false); }
  }

  async function loadCsvFile(file) {
    try { await loadCsvText(await readFileText(file), file.name); }
    catch (e) { showError($('csv-error'), e); setStatus('File rejected', 'error'); }
  }

  $('csv-file').addEventListener('change', (e) => { const f = e.target.files[0]; if (f) loadCsvFile(f); });
  wireDrop($('csv-wrap'), loadCsvFile);
  $('csv-load-sample').addEventListener('click', async () => {
    const name = $('csv-sample').value;
    if (!name) return;
    hideError($('csv-error')); setBusy(true); setStatus(`Loading example ${name} …`);
    try { showCloud(await apiGet(`/api/results/sample?name=${encodeURIComponent(name)}`), name); setStatus(`Loaded example ${name}`, 'ok'); }
    catch (e) { showError($('csv-error'), e); setStatus('Could not load the example', 'error'); }
    finally { setBusy(false); }
  });
  $('node-find-btn').addEventListener('click', () => {
    if (!cloud) { $('node-info').textContent = 'Load a CSV first.'; return; }
    const id = Number($('node-find').value);
    if (!Number.isInteger(id) || id < 1) { $('node-info').textContent = 'Enter a positive whole node id.'; return; }
    if (!cloud.byId) { cloud.byId = new Map(); cloud.ids.forEach((v, i) => cloud.byId.set(v, i)); }
    const i = cloud.byId.get(id);
    if (i === undefined) { $('node-info').textContent = `Node ${id} is not in the displayed data.`; return; }
    csvViewer.select(i); showNode(i);
  });

  // ---------------- segmented switch + sample lists ----------------
  const panes = { stl: $('pane-stl'), csv: $('pane-csv') };
  function switchPane(which) {
    for (const k of Object.keys(panes)) {
      panes[k].hidden = k !== which;
      $(`seg-${k}`).setAttribute('aria-pressed', String(k === which));
    }
    requestAnimationFrame(() => (which === 'stl' ? stlViewer : csvViewer).render());
  }
  $('seg-stl').addEventListener('click', () => switchPane('stl'));
  $('seg-csv').addEventListener('click', () => switchPane('csv'));
  window.addEventListener('resize', () => { stlViewer.render(); csvViewer.render(); });

  (async () => {
    try {
      const s = await apiGet('/api/samples');
      fillSelect($('stl-sample'), s.stl);
      fillSelect($('csv-sample'), s.csv);
      if (s.stl.length) $('stl-load-sample').click();  // show something useful immediately
    } catch (e) { setStatus(e.message, 'error'); }
  })();
}
