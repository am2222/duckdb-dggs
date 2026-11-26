// `?build=<url>` / `#build=<url>`: run the examples against a locally served
// build instead of the published extension. Pure string helpers plus a thin
// browser layer; no engine import, so components can show the override notice
// without loading DuckDB-WASM.

export const BUILD_KEY = 'duckdb-docs-live:build';

// DuckDB-WASM only loads extension files named *.duckdb_extension.wasm.
// Returns null when the URL is usable, otherwise the reason it is not.
export function checkBuildUrl(buildUrl) {
  const path = String(buildUrl).split(/[?#]/)[0];
  return path.endsWith('.duckdb_extension.wasm')
    ? null
    : 'the file name must end in .duckdb_extension.wasm (DuckDB-WASM refuses any other name)';
}

// The build parameter in a location's search or hash: a string (possibly
// empty, meaning "clear it"), or null when neither carries one. The hash form
// exists because the Vite dev server answers 500 to any URL whose query ends
// in `.wasm`.
export function buildParam(search, hash) {
  const q = new URLSearchParams(search || '').get('build');
  if (q !== null) return q;
  const h = String(hash || '').replace(/^#/, '');
  if (!/^build=/.test(h)) return null;
  return new URLSearchParams(h).get('build');
}

// The active build URL, or null for the published extension. A parameter in
// the URL is stored in sessionStorage so it survives navigation within the
// site; an empty one (`?build=`) clears it.
export function buildUrlFromLocation() {
  if (typeof window === 'undefined') return null;
  const param = buildParam(window.location.search, window.location.hash);
  try {
    if (param !== null) {
      if (param) window.sessionStorage.setItem(BUILD_KEY, param);
      else window.sessionStorage.removeItem(BUILD_KEY);
    }
  } catch {
    // storage blocked: the parameter still works for this page
  }
  if (param !== null) return param || null;
  try {
    return window.sessionStorage.getItem(BUILD_KEY) || null;
  } catch {
    return null;
  }
}

// Forget the override: drop the stored key and remove the parameter from the
// address bar, so a reload uses the published extension too.
export function clearBuildOverride() {
  if (typeof window === 'undefined') return;
  try {
    window.sessionStorage.removeItem(BUILD_KEY);
  } catch {
    // nothing stored
  }
  const url = new URL(window.location.href);
  url.searchParams.delete('build');
  if (/^#build=/.test(url.hash)) url.hash = '';
  window.history.replaceState(window.history.state, '', url.toString());
}
