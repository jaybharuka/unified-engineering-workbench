// Minimal 2-D plotting on <canvas>: axes with units, grid, legend, hover read-out, PNG export.
import { downloadBlob, fmt } from './util.js';

const PALETTE = ['#1f77b4', '#d95f02', '#1b9e77', '#7570b3', '#e7298a', '#66a61e', '#a6761d'];
export const paletteColor = (i) => PALETTE[i % PALETTE.length];

function niceStep(range, target) {
  const raw = range / Math.max(1, target);
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const n = raw / mag;
  return (n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10) * mag;
}

function linearTicks(min, max, target = 6) {
  if (!(max > min)) { max = min + 1; }
  const step = niceStep(max - min, target);
  const start = Math.ceil(min / step - 1e-9) * step;
  const out = [];
  for (let v = start; v <= max + step * 1e-9; v += step) out.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  return out;
}

function logTicks(min, max) {
  const out = [];
  if (!(min > 0) || !(max >= min) || !Number.isFinite(max)) return out;
  for (let e = Math.floor(Math.log10(min)); e <= Math.ceil(Math.log10(max)); e++) out.push(Math.pow(10, e));
  return out;
}

export class Plot {
  /** opts: {title, xLabel, yLabel, xScale, yScale, yMin, yMax, xMin, xMax, legend} */
  constructor(canvas, opts = {}) {
    this.canvas = canvas;
    this.opts = { xScale: 'linear', yScale: 'linear', legend: true, ...opts };
    this.series = [];
    this.hover = null;
    canvas.style.touchAction = 'pan-y';
    new ResizeObserver(() => this.draw()).observe(canvas);
    canvas.addEventListener('pointermove', (e) => this.onMove(e));
    canvas.addEventListener('pointerleave', () => { this.hover = null; this.draw(); });
  }

  setOptions(o) { Object.assign(this.opts, o); this.draw(); }

  /** series: [{name, x[], y[], color, width, dash:[], markers:bool, line:bool, markerSize}] */
  setSeries(series) {
    this.series = series.map((s, i) => ({ color: paletteColor(i), width: 2, line: true, markers: false, markerSize: 3.5, dash: [], ...s }));
    this.draw();
    this.canvas.setAttribute('aria-label',
      `${this.opts.title || 'Plot'}: ${this.series.map((s) => s.name).join(', ')}. x axis ${this.opts.xLabel || ''}, y axis ${this.opts.yLabel || ''}.`);
  }

  css(name, fallback) { return getComputedStyle(this.canvas).getPropertyValue(name).trim() || fallback; }

  extent() {
    let xmin = Infinity, xmax = -Infinity, ymin = Infinity, ymax = -Infinity;
    const lx = this.opts.xScale === 'log', ly = this.opts.yScale === 'log';
    for (const s of this.series) {
      for (let i = 0; i < s.x.length; i++) {
        const x = s.x[i], y = s.y[i];
        if (!Number.isFinite(x) || !Number.isFinite(y) || (lx && x <= 0) || (ly && y <= 0)) continue;
        xmin = Math.min(xmin, x); xmax = Math.max(xmax, x);
        ymin = Math.min(ymin, y); ymax = Math.max(ymax, y);
      }
    }
    if (!Number.isFinite(xmin)) {  // no data yet: a valid empty frame for both linear and log axes
      xmin = lx ? 1 : 0; xmax = lx ? 10 : 1; ymin = ly ? 1 : 0; ymax = ly ? 10 : 1;
    }
    const o = this.opts;
    if (o.xMin !== undefined) xmin = o.xMin;
    if (o.xMax !== undefined) xmax = o.xMax;
    if (o.yMin !== undefined) ymin = o.yMin;
    if (o.yMax !== undefined) ymax = o.yMax;
    if (ly) { ymin = Math.pow(10, Math.floor(Math.log10(ymin))); ymax = Math.pow(10, Math.ceil(Math.log10(ymax))); }
    else if (o.yMin === undefined || o.yMax === undefined) {
      const pad = (ymax - ymin || Math.abs(ymax) || 1) * 0.06;
      if (o.yMin === undefined) ymin -= pad;
      if (o.yMax === undefined) ymax += pad;
    }
    if (lx) { xmin = Math.pow(10, Math.floor(Math.log10(xmin))); xmax = Math.pow(10, Math.ceil(Math.log10(xmax))); }
    if (xmax === xmin) xmax = xmin + 1;
    if (ymax === ymin) ymax = ymin + 1;
    return { xmin, xmax, ymin, ymax };
  }

  measure(text) {
    if (!this.mctx) this.mctx = document.createElement('canvas').getContext('2d');
    this.mctx.font = '12px system-ui, sans-serif';
    return this.mctx.measureText(text).width;
  }

  // Legend entries are laid out in wrapped rows above the plot area so they never cover data.
  legendLayout(pw) {
    const named = this.opts.legend ? this.series.filter((s) => s.name) : [];
    const entries = [];
    let x = 0, row = 0;
    for (const s of named) {
      const wEntry = this.measure(s.name) + 40;
      if (x > 0 && x + wEntry > pw) { row++; x = 0; }
      entries.push({ s, x, row });
      x += wEntry;
    }
    return { entries, rows: named.length ? row + 1 : 0 };
  }

  layout(w, h) {
    const l = 68, r = 16, b = 48;
    const pw = Math.max(10, w - l - r);
    const lg = this.legendLayout(pw);
    const t = (this.opts.title ? 34 : 14) + (lg.rows ? lg.rows * 18 + 4 : 0);
    return { m: { l, r, t, b }, pw, ph: Math.max(10, h - t - b), lg };
  }

  mapper(ext, L) {
    const lx = this.opts.xScale === 'log', ly = this.opts.yScale === 'log';
    const fx = (x) => (lx ? (Math.log10(x) - Math.log10(ext.xmin)) / (Math.log10(ext.xmax) - Math.log10(ext.xmin))
                          : (x - ext.xmin) / (ext.xmax - ext.xmin));
    const fy = (y) => (ly ? (Math.log10(y) - Math.log10(ext.ymin)) / (Math.log10(ext.ymax) - Math.log10(ext.ymin))
                          : (y - ext.ymin) / (ext.ymax - ext.ymin));
    return {
      px: (x) => L.m.l + fx(x) * L.pw,
      py: (y) => L.m.t + (1 - fy(y)) * L.ph,
    };
  }

  draw() {
    const c = this.canvas;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    if (!w || !h) return;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const fg = this.css('--plot-fg', '#222'), grid = this.css('--plot-grid', '#ddd'), bg = this.css('--plot-bg', '#fff');
    g.fillStyle = bg; g.fillRect(0, 0, w, h);
    const ext = this.extent();
    const L = this.layout(w, h);
    const map = this.mapper(ext, L);
    const lx = this.opts.xScale === 'log', ly = this.opts.yScale === 'log';
    const xt = this.opts.xTicks || (lx ? logTicks(ext.xmin, ext.xmax) : linearTicks(ext.xmin, ext.xmax, Math.max(3, Math.floor(L.pw / 90))));
    const yt = ly ? logTicks(ext.ymin, ext.ymax) : linearTicks(ext.ymin, ext.ymax, Math.max(3, Math.floor(L.ph / 50)));

    g.font = '12px system-ui, sans-serif';
    g.lineWidth = 1;
    g.strokeStyle = grid; g.fillStyle = fg;
    g.textAlign = 'center'; g.textBaseline = 'top';
    for (const t of xt) {
      if (t < ext.xmin * (1 - 1e-9) || t > ext.xmax * (1 + 1e-9)) continue;
      const x = Math.round(map.px(t)) + 0.5;
      g.beginPath(); g.moveTo(x, L.m.t); g.lineTo(x, L.m.t + L.ph); g.stroke();
      g.fillText(fmt(t, 4), x, L.m.t + L.ph + 6);
    }
    g.textAlign = 'right'; g.textBaseline = 'middle';
    for (const t of yt) {
      if (t < ext.ymin * (ly ? 1 - 1e-9 : 1) - (ly ? 0 : 1e-9) || t > ext.ymax * (ly ? 1 + 1e-9 : 1) + (ly ? 0 : 1e-9)) continue;
      const y = Math.round(map.py(t)) + 0.5;
      g.beginPath(); g.moveTo(L.m.l, y); g.lineTo(L.m.l + L.pw, y); g.stroke();
      g.fillText(fmt(t, 4), L.m.l - 6, y);
    }
    g.strokeStyle = fg; g.strokeRect(L.m.l + 0.5, L.m.t + 0.5, L.pw, L.ph);

    // series (clipped to plot area)
    g.save();
    g.beginPath(); g.rect(L.m.l, L.m.t, L.pw, L.ph); g.clip();
    for (const s of this.series) {
      g.strokeStyle = s.color; g.fillStyle = s.color; g.lineWidth = s.width; g.setLineDash(s.dash);
      if (s.line) {
        g.beginPath();
        let pen = false;
        for (let i = 0; i < s.x.length; i++) {
          if (!Number.isFinite(s.x[i]) || !Number.isFinite(s.y[i]) || (lx && s.x[i] <= 0) || (ly && s.y[i] <= 0)) { pen = false; continue; }
          const x = map.px(s.x[i]), y = map.py(s.y[i]);
          if (pen) g.lineTo(x, y); else { g.moveTo(x, y); pen = true; }
        }
        g.stroke();
      }
      g.setLineDash([]);
      if (s.markers) {
        for (let i = 0; i < s.x.length; i++) {
          if (!Number.isFinite(s.x[i]) || !Number.isFinite(s.y[i]) || (lx && s.x[i] <= 0) || (ly && s.y[i] <= 0)) continue;
          g.beginPath(); g.arc(map.px(s.x[i]), map.py(s.y[i]), s.markerSize, 0, Math.PI * 2); g.fill();
        }
      }
    }
    g.restore();

    // labels
    g.fillStyle = fg; g.textAlign = 'center'; g.textBaseline = 'alphabetic';
    if (this.opts.title) { g.font = '600 14px system-ui, sans-serif'; g.fillText(this.opts.title, L.m.l + L.pw / 2, 20); }
    g.font = '12px system-ui, sans-serif';
    if (this.opts.xLabel) g.fillText(this.opts.xLabel, L.m.l + L.pw / 2, h - 8);
    if (this.opts.yLabel) {
      g.save(); g.translate(14, L.m.t + L.ph / 2); g.rotate(-Math.PI / 2); g.fillText(this.opts.yLabel, 0, 0); g.restore();
    }

    // legend (row(s) above the plot area)
    if (L.lg.rows) {
      g.font = '12px system-ui, sans-serif'; g.textAlign = 'left'; g.textBaseline = 'middle';
      const top = (this.opts.title ? 34 : 14);
      for (const { s, x, row } of L.lg.entries) {
        const xx = L.m.l + x, yy = top + row * 18 + 7;
        g.strokeStyle = s.color; g.fillStyle = s.color; g.lineWidth = 2; g.setLineDash(s.dash);
        g.beginPath(); g.moveTo(xx, yy); g.lineTo(xx + 20, yy); g.stroke(); g.setLineDash([]);
        if (s.markers) { g.beginPath(); g.arc(xx + 10, yy, 3, 0, Math.PI * 2); g.fill(); }
        g.fillStyle = fg; g.fillText(s.name, xx + 25, yy);
      }
    }

    // hover read-out
    if (this.hover) {
      const hv = this.hover;
      g.strokeStyle = fg; g.setLineDash([3, 3]); g.lineWidth = 1;
      g.beginPath(); g.moveTo(hv.px + 0.5, L.m.t); g.lineTo(hv.px + 0.5, L.m.t + L.ph); g.stroke(); g.setLineDash([]);
      g.fillStyle = hv.color; g.beginPath(); g.arc(hv.px, hv.py, 5, 0, Math.PI * 2); g.fill();
      const text = `${hv.name}: x=${fmt(hv.x, 5)}, y=${fmt(hv.y, 6)}`;
      g.font = '12px system-ui, sans-serif'; g.textAlign = 'left'; g.textBaseline = 'middle';
      const tw = g.measureText(text).width + 12;
      let bx = hv.px + 10; if (bx + tw > w - 4) bx = hv.px - 10 - tw;
      const by = Math.min(Math.max(hv.py - 12, L.m.t + 2), L.m.t + L.ph - 24);
      g.fillStyle = bg; g.fillRect(bx, by, tw, 22); g.strokeStyle = fg; g.strokeRect(bx + 0.5, by + 0.5, tw, 22);
      g.fillStyle = fg; g.fillText(text, bx + 6, by + 11);
    }
    this.last = { ext, L, map };
  }

  onMove(e) {
    if (!this.last) return;
    const r = this.canvas.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    let best = null, bd = 18 * 18;
    for (const s of this.series) {
      for (let i = 0; i < s.x.length; i++) {
        const px = this.last.map.px(s.x[i]), py = this.last.map.py(s.y[i]);
        if (!Number.isFinite(px) || !Number.isFinite(py)) continue;
        const d = (px - mx) ** 2 + (py - my) ** 2;
        if (d < bd) { bd = d; best = { px, py, x: s.x[i], y: s.y[i], name: s.name || 'series', color: s.color }; }
      }
    }
    this.hover = best;
    this.draw();
  }

  exportPng(filename) {
    this.hover = null;
    this.draw();
    this.canvas.toBlob((b) => b && downloadBlob(b, filename), 'image/png');
  }
}
