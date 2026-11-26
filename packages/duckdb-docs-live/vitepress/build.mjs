// `?build=<url>` / `#build=<url>`: run the examples against a locally served
// build instead of the published extension. Pure string helpers plus a thin
// browser layer; no engine import, so components can show the override notice
// without loading DuckDB-WASM.

export const BUILD_KEY = 'duckdb-docs-live:build';

const LOCAL_HOSTS = new Set(['localhost', '127.0.0.1', '[::1]']);

// Returns null when the URL is usable, otherwise the reason it is not.
// - DuckDB-WASM only loads extension files named *.duckdb_extension.wasm.
// - The engine allows unsigned extensions for a build, so a link with
//   ?build= must not be able to make a visitor run a binary from anywhere:
//   only a build served on this machine (localhost, 127.0.0.1, [::1]) or a
//   file from this site's own origin is accepted.
// `base` is the page URL the build URL is resolved against (default: the
// current page); tests pass it so they need no DOM.
export function checkBuildUrl(buildUrl, base) {
  const page = base ?? (typeof window !== 'undefined' ? window.location.href : undefined);
  let url;
  try {
    url = new URL(String(buildUrl), page);
  } catch {
    return 'not a valid URL';
  }
  if (!url.pathname.endsWith('.duckdb_extension.wasm'))
    return 'the file name must end in .duckdb_extension.wasm (DuckDB-WASM refuses any other name)';
  const local = (url.protocol === 'http:' || url.protocol === 'https:') && LOCAL_HOSTS.has(url.hostname);
  let sameOrigin = false;
  try {
    sameOrigin = page !== undefined && url.origin === new URL(page).origin;
  } catch {
    // no usable page URL
  }
  if (!local && !sameOrigin)
    return "only local builds (localhost) or this site's own files can be loaded";
  return null;
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
