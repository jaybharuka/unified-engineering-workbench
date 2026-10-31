// Thin client for the C++ backend. All requests carry X-WB-Client so the server can reject cross-site posts.
import { downloadBlob } from './util.js';

export class ApiError extends Error {
  constructor(message, { code, hint, details, status, recoverable } = {}) {
    super(message);
    this.code = code;
    this.hint = hint;
    this.details = details;
    this.status = status;
    this.recoverable = recoverable;
  }
}

async function request(path, { method = 'GET', body, raw } = {}) {
  const init = { method, headers: { 'X-WB-Client': '1' } };
  if (method === 'POST') {
    init.headers['Content-Type'] = raw !== undefined ? 'text/plain' : 'application/json';
    init.body = raw !== undefined ? raw : JSON.stringify(body ?? {});  // always send a body (Content-Length)
  }
  let res;
  try {
    res = await fetch(path, init);
  } catch (e) {
    throw new ApiError('Cannot reach the local Workbench server. It may have been closed.', {
      code: 'NO_SERVER',
      hint: 'Close this window and start Launch.exe again.',
    });
  }
  return res;
}

async function parse(res) {
  const text = await res.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch (_) { /* not JSON */ }
  if (!res.ok) {
    throw new ApiError((data && data.error) || `Request failed (HTTP ${res.status}).`, {
      code: data && data.code, hint: data && data.hint, details: data && data.details,
      status: res.status, recoverable: data && data.recoverable,
    });
  }
  return data;
}

export const apiGet = async (path) => parse(await request(path));
export const apiPost = async (path, body) => parse(await request(path, { method: 'POST', body }));
export const apiPostRaw = async (path, text) => parse(await request(path, { method: 'POST', raw: text }));

export async function downloadFrom(path, { method = 'GET', body, filename }) {
  const res = await request(path, { method, body });
  if (!res.ok) await parse(res);  // throws ApiError with the server message
  downloadBlob(await res.blob(), filename);
}

// ---- keep-alive: lets the server exit when the window is closed (see wb_server --auto-exit) ----
export function startHeartbeat() {
  const beat = () => request('/api/heartbeat', { method: 'POST', body: {} }).catch(() => {});
  beat();
  setInterval(beat, 3000);
  window.addEventListener('pagehide', () => {
    fetch('/api/closing', { method: 'POST', headers: { 'X-WB-Client': '1', 'Content-Type': 'application/json' }, body: '{}', keepalive: true }).catch(() => {});
  });
}
