// Dependency-free 3-D viewer on a 2-D canvas: orbit / pan / zoom / fit, flat-shaded triangles (painter's
// algorithm) and colour-mapped point clouds with node picking. Z is "up".
import { colorAt } from './colormap.js';

const LUT = Array.from({ length: 256 }, (_, i) => {
  const [r, g, b] = colorAt(i / 255);
  return `rgb(${Math.round(r)},${Math.round(g)},${Math.round(b)})`;
});
const SHADES = Array.from({ length: 33 }, (_, i) => {
  const s = 0.28 + 0.72 * (i / 32);
  return `rgb(${Math.round(64 * s + 14)},${Math.round(120 * s + 18)},${Math.round(176 * s + 20)})`;
});

export class Viewer3D {
  constructor(canvas, { onPick } = {}) {
    this.canvas = canvas;
    this.onPick = onPick;
    this.mode = 'none';
    this.yaw = -0.9; this.pitch = 0.45; this.dist = 3; this.target = [0, 0, 0];
    this.fov = (40 * Math.PI) / 180;
    this.wire = false; this.pointSize = 3; this.selected = -1;
    this.bounds = null;
    this.interacting = false;
    this.idleTimer = null;
    this.pick = null;
    this.bindEvents();
    new ResizeObserver(() => this.render()).observe(canvas);
  }

  // ---------- data ----------
  setMesh(positions, bounds) {
    this.mode = 'mesh'; this.positions = positions; this.bounds = bounds; this.selected = -1;
    this.fit(); this.render();
  }

  setPoints(xyz, values, vmin, vmax, bounds) {
    this.mode = 'points'; this.xyz = xyz; this.values = values; this.vmin = vmin; this.vmax = vmax; this.bounds = bounds;
    this.bucket = new Uint8Array(values.length);
    const span = vmax - vmin || 1;
    for (let i = 0; i < values.length; i++) this.bucket[i] = Math.max(0, Math.min(255, Math.round(((values[i] - vmin) / span) * 255)));
    this.selected = -1;
    this.fit(); this.render();
  }

  clearData() { this.mode = 'none'; this.bounds = null; this.render(); }

  // ---------- camera ----------
  fit() {
    if (!this.bounds) return;
    const { min, max } = this.bounds;
    this.target = [(min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2];
    const radius = Math.max(1e-12, 0.5 * Math.hypot(max[0] - min[0], max[1] - min[1], max[2] - min[2]));
    const aspect = (this.canvas.clientWidth || 1) / (this.canvas.clientHeight || 1);
    const fovH = 2 * Math.atan(Math.tan(this.fov / 2) * aspect);
    this.dist = (radius / Math.sin(Math.min(this.fov, fovH) / 2)) * 1.08;
    this.render();
  }

  setView(name) {
    const views = { front: [-Math.PI / 2, 0], top: [-Math.PI / 2, Math.PI / 2 - 0.001], right: [0, 0], iso: [-0.9, 0.45] };
    const v = views[name];
    if (!v) return;
    [this.yaw, this.pitch] = v;
    this.fit();
  }

  basis() {
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch), cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
    const eye = [this.target[0] + this.dist * cp * cy, this.target[1] + this.dist * cp * sy, this.target[2] + this.dist * sp];
    let f = [this.target[0] - eye[0], this.target[1] - eye[1], this.target[2] - eye[2]];
    const fl = Math.hypot(...f); f = f.map((v) => v / fl);
    let r = [f[1], -f[0], 0];  // f x (0,0,1)
    const rl = Math.hypot(...r) || 1; r = r.map((v) => v / rl);
    const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
    return { eye, f, r, u };
  }

  // ---------- interaction ----------
  bindEvents() {
    const c = this.canvas;
    let last = null, moved = 0, button = 0, shift = false;
    c.addEventListener('contextmenu', (e) => e.preventDefault());
    c.addEventListener('pointerdown', (e) => {
      c.setPointerCapture(e.pointerId); c.focus();
      last = [e.clientX, e.clientY]; moved = 0; button = e.button; shift = e.shiftKey;
      this.beginInteraction();
    });
    c.addEventListener('pointermove', (e) => {
      if (!last) return;
      const dx = e.clientX - last[0], dy = e.clientY - last[1];
      last = [e.clientX, e.clientY]; moved += Math.abs(dx) + Math.abs(dy);
      if (button === 2 || shift || button === 1) this.pan(dx, dy);
      else { this.yaw -= dx * 0.008; this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch + dy * 0.008)); }
      this.render(true);
    });
    const end = (e) => {
      if (!last) return;
      last = null;
      this.endInteraction();
      if (moved < 4 && e.type === 'pointerup' && button === 0) this.handleClick(e);
    };
    c.addEventListener('pointerup', end);
    c.addEventListener('pointercancel', end);
    c.addEventListener('dblclick', () => this.fit());
    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.dist *= Math.exp(Math.max(-200, Math.min(200, e.deltaY)) * 0.0012);
      this.beginInteraction(); this.render(true); this.endInteraction();
    }, { passive: false });
    c.addEventListener('keydown', (e) => {
      const k = e.key;
      if (k === 'ArrowLeft') this.yaw += 0.08;
      else if (k === 'ArrowRight') this.yaw -= 0.08;
      else if (k === 'ArrowUp') this.pitch = Math.min(1.55, this.pitch + 0.08);
      else if (k === 'ArrowDown') this.pitch = Math.max(-1.55, this.pitch - 0.08);
      else if (k === '+' || k === '=') this.dist *= 0.9;
      else if (k === '-') this.dist *= 1.1;
      else if (k === 'f' || k === 'F') this.fit();
      else return;
      e.preventDefault();
      this.render(true); this.endInteraction();
    });
  }

  beginInteraction() { this.interacting = true; clearTimeout(this.idleTimer); }
  endInteraction() { clearTimeout(this.idleTimer); this.idleTimer = setTimeout(() => { this.interacting = false; this.render(); }, 160); }

  pan(dx, dy) {
    const { r, u } = this.basis();
    const k = (2 * this.dist * Math.tan(this.fov / 2)) / (this.canvas.clientHeight || 1);
    for (let i = 0; i < 3; i++) this.target[i] += -dx * k * r[i] + dy * k * u[i];
  }

  handleClick(e) {
    if (this.mode !== 'points' || !this.onPick) return;
    this.render(false);  // make sure screen positions are from a full render
    const rect = this.canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left, my = e.clientY - rect.top;
    const p = this.pick;
    if (!p) return;
    let best = -1, bd = 14 * 14;
    for (let j = 0; j < p.count; j++) {
      const d = (p.sx[j] - mx) ** 2 + (p.sy[j] - my) ** 2;
      if (d < bd) { bd = d; best = p.index[j]; }
    }
    if (best >= 0) { this.selected = best; this.render(); this.onPick(best); }
  }

  select(index) { this.selected = index; this.render(); }

  // ---------- rendering ----------
  render(fast = false) {
    const c = this.canvas;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    if (!w || !h) return;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const cs = getComputedStyle(c);
    g.fillStyle = cs.getPropertyValue('--viewer-bg').trim() || '#f4f6f8';
    g.fillRect(0, 0, w, h);
    const quick = fast || this.interacting;
    if (this.mode === 'mesh') this.renderMesh(g, w, h, quick);
    else if (this.mode === 'points') this.renderPoints(g, w, h, quick);
    else {
      g.fillStyle = cs.getPropertyValue('--plot-fg').trim() || '#555';
      g.font = '14px system-ui, sans-serif'; g.textAlign = 'center';
      g.fillText('No data loaded', w / 2, h / 2);
      return;
    }
    this.drawGizmo(g, w, h, cs);
  }

  renderMesh(g, w, h, quick) {
    const P = this.positions, n = P.length / 9;
    const stride = quick ? Math.max(1, Math.ceil(n / 20000)) : 1;
    const { eye, f, r, u } = this.basis();
    const s = (h / 2) / Math.tan(this.fov / 2), cx = w / 2, cy = h / 2, near = this.dist * 0.01;
    const m = Math.ceil(n / stride);
    const scr = new Float32Array(m * 6), depth = new Float32Array(m), shade = new Uint8Array(m);
    let cnt = 0;
    const cam = new Float64Array(9);
    for (let t = 0; t < n; t += stride) {
      const o = t * 9;
      let ok = true;
      for (let v = 0; v < 3; v++) {
        const dx = P[o + v * 3] - eye[0], dy = P[o + v * 3 + 1] - eye[1], dz = P[o + v * 3 + 2] - eye[2];
        const xc = dx * r[0] + dy * r[1] + dz * r[2];
        const yc = dx * u[0] + dy * u[1] + dz * u[2];
        const zc = dx * f[0] + dy * f[1] + dz * f[2];
        if (zc < near) { ok = false; break; }
        cam[v * 3] = xc; cam[v * 3 + 1] = yc; cam[v * 3 + 2] = zc;
      }
      if (!ok) continue;
      const ax = cam[3] - cam[0], ay = cam[4] - cam[1], az = cam[5] - cam[2];
      const bx = cam[6] - cam[0], by = cam[7] - cam[1], bz = cam[8] - cam[2];
      const nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
      const nl = Math.hypot(nx, ny, nz) || 1;
      shade[cnt] = Math.round((Math.abs(nz) / nl) * 32);
      depth[cnt] = (cam[2] + cam[5] + cam[8]) / 3;
      for (let v = 0; v < 3; v++) {
        scr[cnt * 6 + v * 2] = cx + (cam[v * 3] / cam[v * 3 + 2]) * s;
        scr[cnt * 6 + v * 2 + 1] = cy - (cam[v * 3 + 1] / cam[v * 3 + 2]) * s;
      }
      cnt++;
    }
    const order = new Uint32Array(cnt);
    for (let i = 0; i < cnt; i++) order[i] = i;
    order.sort((a, b) => depth[b] - depth[a]);  // far to near
    g.lineJoin = 'round';
    const edge = getComputedStyle(this.canvas).getPropertyValue('--plot-fg').trim() || '#222';
    for (let k = 0; k < cnt; k++) {
      const i = order[k], o = i * 6;
      g.beginPath(); g.moveTo(scr[o], scr[o + 1]); g.lineTo(scr[o + 2], scr[o + 3]); g.lineTo(scr[o + 4], scr[o + 5]); g.closePath();
      const col = SHADES[shade[i]];
      g.fillStyle = col;
      g.fill();
      if (this.wire) { g.strokeStyle = edge; g.lineWidth = 0.6; g.stroke(); }
      else { g.strokeStyle = col; g.lineWidth = 0.7; g.stroke(); }  // hides anti-aliasing seams
    }
    this.lastDrawn = { triangles: cnt, total: n, decimated: stride > 1 };
  }

  renderPoints(g, w, h, quick) {
    const X = this.xyz, n = X.length / 3;
    const stride = quick ? Math.max(1, Math.ceil(n / 100000)) : 1;
    const { eye, f, r, u } = this.basis();
    const sc = (h / 2) / Math.tan(this.fov / 2), cx = w / 2, cy = h / 2, near = this.dist * 0.01;
    const m = Math.ceil(n / stride);
    const sx = new Float32Array(m), sy = new Float32Array(m), idx = new Uint32Array(m);
    const counts = new Uint32Array(257);
    let cnt = 0;
    for (let i = 0; i < n; i += stride) {
      const dx = X[i * 3] - eye[0], dy = X[i * 3 + 1] - eye[1], dz = X[i * 3 + 2] - eye[2];
      const zc = dx * f[0] + dy * f[1] + dz * f[2];
      if (zc < near) continue;
      sx[cnt] = cx + ((dx * r[0] + dy * r[1] + dz * r[2]) / zc) * sc;
      sy[cnt] = cy - ((dx * u[0] + dy * u[1] + dz * u[2]) / zc) * sc;
      idx[cnt] = i;
      counts[this.bucket[i] + 1]++;
      cnt++;
    }
    for (let b = 0; b < 256; b++) counts[b + 1] += counts[b];
    const order = new Uint32Array(cnt), fill = counts.slice(0, 256);
    for (let j = 0; j < cnt; j++) order[fill[this.bucket[idx[j]]]++] = j;
    const sz = this.pointSize;
    let pos = 0;
    for (let b = 0; b < 256; b++) {
      const end = counts[b + 1];
      if (end === pos) continue;
      g.fillStyle = LUT[b];
      g.beginPath();
      for (; pos < end; pos++) { const j = order[pos]; g.rect(sx[j] - sz / 2, sy[j] - sz / 2, sz, sz); }
      g.fill();
    }
    if (!quick) this.pick = { sx, sy, index: idx, count: cnt };
    if (this.selected >= 0) {
      const i = this.selected;
      const dx = X[i * 3] - eye[0], dy = X[i * 3 + 1] - eye[1], dz = X[i * 3 + 2] - eye[2];
      const zc = dx * f[0] + dy * f[1] + dz * f[2];
      if (zc > near) {
        const px = cx + ((dx * r[0] + dy * r[1] + dz * r[2]) / zc) * sc, py = cy - ((dx * u[0] + dy * u[1] + dz * u[2]) / zc) * sc;
        g.lineWidth = 2.5; g.strokeStyle = '#ff2d55'; g.beginPath(); g.arc(px, py, sz + 6, 0, Math.PI * 2); g.stroke();
        g.lineWidth = 1; g.strokeStyle = '#fff'; g.beginPath(); g.arc(px, py, sz + 8.5, 0, Math.PI * 2); g.stroke();
      }
    }
    this.lastDrawn = { points: cnt, total: n, decimated: stride > 1 };
  }

  drawGizmo(g, w, h, cs) {
    const { r, u } = this.basis();
    const ox = 44, oy = h - 44, L = 26;
    const axes = [['X', [1, 0, 0], '#d62728'], ['Y', [0, 1, 0], '#2ca02c'], ['Z', [0, 0, 1], '#1f77b4']];
    g.font = '11px system-ui, sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle'; g.lineWidth = 2;
    for (const [name, a, col] of axes) {
      const px = a[0] * r[0] + a[1] * r[1] + a[2] * r[2], py = a[0] * u[0] + a[1] * u[1] + a[2] * u[2];
      g.strokeStyle = col; g.beginPath(); g.moveTo(ox, oy); g.lineTo(ox + px * L, oy - py * L); g.stroke();
      g.fillStyle = col; g.fillText(name, ox + px * (L + 9), oy - py * (L + 9));
    }
  }
}
