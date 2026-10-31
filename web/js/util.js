// Small shared helpers (DOM, number formatting, base64 typed arrays, downloads).

export const $ = (id) => document.getElementById(id);

export function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) node.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) node.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return node;
}

export function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

export function fmt(v, digits = 6) {
  if (v === null || v === undefined || Number.isNaN(v)) return '–';
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a >= 1e6 || a < 1e-3) return v.toExponential(Math.max(1, digits - 1));
  return Number(v.toPrecision(digits)).toString();
}

export function fmtBytes(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MiB`;
}

function b64ToBytes(b64) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}
export const b64ToFloat32 = (b64) => new Float32Array(b64ToBytes(b64).buffer);
export const b64ToInt32 = (b64) => new Int32Array(b64ToBytes(b64).buffer);

export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = el('a', { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

export function downloadText(text, filename, type = 'text/csv') {
  downloadBlob(new Blob([text], { type }), filename);
}

// Global status line + toast. Everything user-visible goes through here so errors are never silent.
export function setStatus(message, kind = 'info') {
  const bar = $('status-text');
  if (!bar) return;
  bar.textContent = message;
  bar.dataset.kind = kind;
}

export function setBusy(busy) {
  const spinner = $('status-spinner');
  if (spinner) spinner.hidden = !busy;
}

export function showError(container, err) {
  clear(container);
  container.hidden = false;
  container.append(el('strong', { text: err.message || String(err) }));
  if (err.hint) container.append(el('p', { class: 'hint', text: err.hint }));
  if (Array.isArray(err.details) && err.details.length > 1) {
    container.append(el('ul', {}, err.details.map((d) => el('li', { text: d }))));
  }
}

export function hideError(container) {
  container.hidden = true;
  clear(container);
}
