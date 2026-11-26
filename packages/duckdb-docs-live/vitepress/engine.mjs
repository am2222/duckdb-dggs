// Browser engine: DuckDB-WASM in a worker, the extension loaded once per page.
// Only ever loaded with import() from a Run click (or the status page), never
// on page load and never during SSR.
import { errorMessage } from '../src/sql.mjs';
import { checkBuildUrl } from './build.mjs';

const quote = (s) => String(s).replace(/'/g, "''");

export { buildUrlFromLocation, checkBuildUrl, clearBuildOverride } from './build.mjs';

let enginePromise = null;

async function loadEngine() {
  const duckdb = await import('@duckdb/duckdb-wasm');
  const bundle = await duckdb.selectBundle(duckdb.getJsDelivrBundles());
  return { duckdb, bundle };
}

async function terminate(db, worker, conn) {
  try { if (conn) await conn.close(); } catch { /* gone */ }
  try { await db.terminate(); } catch { /* gone */ }
  try { worker.terminate(); } catch { /* gone */ }
}

async function openDb(config, buildUrl) {
  if (!enginePromise) {
    enginePromise = loadEngine();
    enginePromise.catch(() => { enginePromise = null; });
  }
  const { duckdb, bundle } = await enginePromise;
  const workerUrl = URL.createObjectURL(
    new Blob([`importScripts("${bundle.mainWorker}");`], { type: 'text/javascript' }));
  const worker = new Worker(workerUrl);
  const db = new duckdb.AsyncDuckDB(new duckdb.ConsoleLogger(duckdb.LogLevel.WARNING), worker);
  try {
    await db.instantiate(bundle.mainModule, bundle.pthreadWorker);
    await db.open({ allowUnsignedExtensions: !!buildUrl });
  } catch (e) {
    await terminate(db, worker);
    throw e;
  } finally {
    URL.revokeObjectURL(workerUrl);
  }
  return { db, worker, engine: duckdb.PACKAGE_VERSION ?? config.engineVersion ?? '' };
}

const first = (table, col) => {
  const row = table.toArray()[0];
  return row ? String(row.toJSON()[col] ?? '') : '';
};

async function createSession(config, buildUrl) {
  const ext = config.extension;
  const repositories = config.repositories ?? [];
  const source = buildUrl ?? 'community';
  const info = { duckdb: '', engine: config.engineVersion ?? '', extension: '', source };

  // A bad ?build= URL fails every example the same way; report it once,
  // without downloading the engine.
  const bad = buildUrl ? checkBuildUrl(buildUrl) : null;
  if (bad) {
    return {
      dead: false, info, setupError: `could not load build ${buildUrl}: ${bad}`,
      connect: () => Promise.reject(new Error('no engine')),
      ensure: () => Promise.reject(new Error('no engine')),
    };
  }

  const { db, worker, engine } = await openDb(config, buildUrl);
  info.engine = engine;
  let conn;
  try {
    conn = await db.connect();
  } catch (e) {
    await terminate(db, worker);
    throw e;
  }
  const ensured = new Map();

  const session = {
    dead: false,
    info,
    setupError: null,
    db,
    connect: () => db.connect(),
    // Installs `name` from the engine's default repository, then from each
    // configured repository, and LOADs it (the Node runner's semantics).
    ensure(name) {
      if (!ensured.has(name)) {
        const p = (async () => {
          let last;
          for (const repo of [null, ...repositories]) {
            try {
              const from = repo ? ` FROM '${quote(repo)}'` : '';
              await conn.query(`FORCE INSTALL '${quote(name)}'${from}`);
              await conn.query(`LOAD '${quote(name)}'`);
              return;
            } catch (e) {
              last = e;
            }
          }
          throw new Error(`${name} is not available for this engine (${errorMessage(last)})`);
        })();
        ensured.set(name, p);
        p.catch(() => ensured.delete(name));
      }
      return ensured.get(name);
    },
    close: () => terminate(db, worker, conn),
  };

  try {
    info.duckdb = first(await conn.query('SELECT version() AS v'), 'v');
  } catch {
    // informational only
  }
  try {
    if (buildUrl) {
      await conn.query(`LOAD '${quote(buildUrl)}'`);
    } else {
      await conn.query(`INSTALL ${ext} FROM community`);
      await conn.query(`LOAD ${ext}`);
    }
  } catch (e) {
    session.setupError = buildUrl
      ? `could not load build ${buildUrl}: ${errorMessage(e)}`
      : `could not load ${ext} from community: ${errorMessage(e)}`;
    // The published extension may be a passing network problem: free the
    // engine so getSession() starts over on the next Run. A bad build fails
    // the same way every time, so that session stays memoised.
    if (!buildUrl) {
      session.retry = true;
      await session.close();
    }
    return session;
  }
  try {
    info.extension = first(await conn.query(
      `SELECT extension_version AS v FROM duckdb_extensions() WHERE extension_name = '${quote(ext)}'`), 'v');
  } catch {
    // informational only
  }
  return session;
}

const sessions = new Map();

// One engine per page and build URL. A session whose engine has died is
// replaced on the next call; `fresh: true` bypasses the memo.
export async function getSession(config, buildUrl = null, opts = {}) {
  if (typeof window === 'undefined') throw new Error('getSession() runs in the browser only');
  const key = buildUrl ?? '';
  const cached = sessions.get(key);
  if (!opts.fresh && cached) {
    const s = await cached.catch(() => null);
    if (s && !s.dead && !s.retry) return s;
    if (sessions.get(key) !== cached) return getSession(config, buildUrl, opts);
    sessions.delete(key);
    if (s?.close) s.close();
  }
  const p = createSession(config, buildUrl);
  if (!opts.fresh) {
    sessions.set(key, p);
    const forget = () => { if (sessions.get(key) === p) sessions.delete(key); };
    p.then((s) => { if (s.retry) forget(); }, forget);
  }
  return p;
}

// Drops (and stops) the memoised session for `buildUrl`, e.g. when the reader
// leaves a build override.
export async function dropSession(buildUrl) {
  const key = buildUrl ?? '';
  const p = sessions.get(key);
  if (!p) return;
  sessions.delete(key);
  const s = await p.catch(() => null);
  if (s?.close) await s.close();
}
