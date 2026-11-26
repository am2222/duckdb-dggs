// Site config (provide/inject) and page-wide state shared by LiveSql instances.
import { inject, reactive } from 'vue';
import { buildUrlFromLocation, clearBuildOverride } from './build.mjs';

export const LIVE_SQL_CONFIG = Symbol('duckdb-docs-live:config');

// knownFailures: the docs/wasm-known-failures.json list ([{id, reason}]), so a
// listed example shows "known issue" instead of ✗, as CI and /wasm-status do.
const DEFAULTS = { extension: '', engineVersion: '', repositories: [], knownFailures: [] };

export function provideLiveSqlConfig(app, config) {
  app.provide(LIVE_SQL_CONFIG, { ...DEFAULTS, ...config });
}

export function useLiveSqlConfig() {
  const config = inject(LIVE_SQL_CONFIG, null);
  if (!config || !config.extension) {
    throw new Error('duckdb-docs-live: call provideLiveSqlConfig(app, { extension: ... }) in enhanceApp');
  }
  return config;
}

// One banner per page for failures that affect every example (bad ?build=,
// engine that won't start). `owner` is the component that renders it.
// `build` is the active ?build=/#build= override (null = published); while it
// is set, the first mounted LiveSql shows a notice with [Use published].
// `epoch` changes when the engine source changes, so results from the old
// source are cleared.
export const liveState = reactive({
  banner: null,
  owner: null,
  build: null,
  instances: [],
  epoch: 0,
  engineLoaded: false,
});

export function registerInstance(uid) {
  liveState.instances.push(uid);
  liveState.build = buildUrlFromLocation();
}

export function unregisterInstance(uid) {
  const i = liveState.instances.indexOf(uid);
  if (i >= 0) liveState.instances.splice(i, 1);
  if (liveState.owner === uid) dismissBanner();
}

// Leave a build override: forget the stored URL, stop the engine that loaded
// it, and clear every result that came from it.
export async function usePublished() {
  const url = liveState.build;
  clearBuildOverride();
  liveState.build = null;
  dismissBanner();
  liveState.epoch++;
  if (url && liveState.engineLoaded) {
    const { dropSession } = await import('./engine.mjs');
    await dropSession(url);
  }
}

export function showBanner(message, owner) {
  if (liveState.banner === message) return;
  liveState.banner = message;
  liveState.owner = owner;
}

export function dismissBanner() {
  liveState.banner = null;
  liveState.owner = null;
}
