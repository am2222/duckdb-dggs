// Engine-agnostic example runner and known-failures logic.
// Plain ESM, no dependencies, no Node/DOM APIs.
import { runQuery, errorMessage, isEngineCrash } from './sql.mjs';
import { compareResult } from './compare.mjs';

const RANK = { match: 0, shown: 1, unreleased: 2, differs: 3, error: 4 };
const UNRELEASED = /does not exist|No function matches/i;

const onlyComments = (sql) =>
  sql.replace(/\/\*[\s\S]*?\*\//g, '').replace(/--[^\n]*/g, '').trim() === '';

function worst(statuses, fallback) {
  let best = fallback;
  let rank = -1;
  for (const s of statuses) {
    if (RANK[s] > rank) {
      rank = RANK[s];
      best = s;
    }
  }
  return best;
}

export async function runExample(session, ex, opts = {}) {
  const base = { id: ex.id, page: ex.page };
  if (ex.mode === 'norun') return { ...base, status: 'skipped', statements: [] };
  if (session.dead) {
    return { ...base, status: 'error', detail: 'engine stopped', statements: [] };
  }
  for (const n of ex.needs ?? []) {
    try {
      await session.ensure(n);
    } catch (e) {
      return { ...base, status: 'skipped', detail: `needs ${n}: ${errorMessage(e)}`, statements: [] };
    }
  }

  const statements = [];
  const stmts = ex.statements.filter((s) => !onlyComments(s.sql));
  const hasExpected = ex.mode === 'compare' && stmts.some((s) => s.expected);
  let conn;
  try {
    conn = await session.connect();
    for (const st of stmts) {
      const t0 = Date.now();
      let actual;
      try {
        actual = await runQuery(conn, st.sql);
      } catch (e) {
        const message = errorMessage(e);
        if (isEngineCrash(message)) session.dead = true;
        const status = opts.published && UNRELEASED.test(message) ? 'unreleased' : 'error';
        statements.push({ sql: st.sql, status, expected: st.expected ?? undefined, detail: message, ms: Date.now() - t0 });
        break; // later statements depend on this one
      }
      const ms = Date.now() - t0;
      if (ex.mode === 'run') {
        statements.push({ sql: st.sql, status: 'shown', expected: st.expected ?? undefined, actual, ms });
      } else if (st.expected) {
        const { status, detail } = compareResult(st.expected, actual);
        const r = { sql: st.sql, status, expected: st.expected, actual, ms };
        if (detail !== undefined) r.detail = detail;
        statements.push(r);
      } else {
        statements.push({ sql: st.sql, status: hasExpected ? 'match' : 'shown', actual, ms });
      }
    }
  } catch (e) {
    // connect() itself failed
    const message = errorMessage(e);
    if (isEngineCrash(message)) session.dead = true;
    return { ...base, status: 'error', detail: message, statements };
  } finally {
    try {
      if (conn && typeof conn.close === 'function') await conn.close();
    } catch {
      // ignore close errors
    }
  }

  const fallback = ex.mode === 'run' ? 'shown' : 'match';
  return { ...base, status: worst(statements.map((s) => s.status), fallback), statements };
}

export function applyKnownFailures(results, known) {
  const problems = [];
  const byId = new Map(known.map((k) => [k.id, k]));
  const seen = new Set(results.map((r) => r.id));
  const out = results.map((r) => {
    const k = byId.get(r.id);
    if (!k) return r;
    if (r.status === 'differs' || r.status === 'error') {
      return { ...r, status: 'skipped', detail: `known failure: ${k.reason}` };
    }
    if (r.status === 'match') {
      problems.push(`${r.id} passes now — remove it from docs/wasm-known-failures.json`);
    }
    return r;
  });
  for (const k of known) {
    if (!seen.has(k.id)) {
      problems.push(`${k.id} is in the known-failures list but no such example exists`);
    }
  }
  return { results: out, problems };
}
