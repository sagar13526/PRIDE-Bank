import * as mock from './mock.js';

// kind: 'live' -> fetch the C++ backend, 'demo' -> in-browser mock engine.
// Both return the same JSON shapes (see src/api/mock.js).
export async function request(kind, base, method, path, body, timeoutMs = 4000) {
  if (kind === 'demo') return mock.handle(method, path, body);

  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const res = await fetch(base.replace(/\/$/, '') + path, {
      method,
      headers: body ? { 'Content-Type': 'application/json' } : undefined,
      body: body ? JSON.stringify(body) : undefined,
      signal: ctl.signal,
    });
    let json = null;
    try {
      json = await res.json();
    } catch {
      /* empty body */
    }
    if (!res.ok) throw new Error((json && json.error) || `HTTP ${res.status}`);
    return json;
  } finally {
    clearTimeout(timer);
  }
}

export async function probe(base) {
  try {
    await request('live', base, 'GET', '/api/accounts', undefined, 1500);
    return true;
  } catch {
    return false;
  }
}
