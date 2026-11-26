// Query helpers shared by the sqllogictest runner and the docs runner.
// Plain ESM, no dependencies, no Node/DOM APIs.

export function stripSemicolon(sql) {
  return sql.trim().replace(/;\s*$/, '');
}

export function errorMessage(e) {
  return String(e?.message ?? e).replace(/\n\s+at .*$/gs, '').trim();
}

// The WASM module stops for good on exit(), abort() or a trap; every later
// query on it fails or hangs, so the rest of the file can't be run.
export function isEngineCrash(message) {
  return /Program terminated with exit|Aborted\(|RuntimeError: |ExitStatus/.test(message);
}

// DuckDB's runner prints NULL as "NULL" and the empty string as "(empty)".
export function render(v) {
  if (v === null || v === undefined) return 'NULL';
  const s = typeof v === 'bigint' ? v.toString() : String(v);
  return s === '' ? '(empty)' : s;
}

export function rowsOf(table) {
  const cols = table.schema.fields.map((f) => f.name);
  const rows = [];
  for (const row of table.toArray()) {
    const obj = row.toJSON();
    rows.push(cols.map((c) => render(obj[c])));
  }
  return { cols: cols.length, names: cols, rows };
}

// Runs a query and returns its rows as strings. Plain SELECTs are wrapped so
// the engine renders every value; anything that can't be wrapped (PRAGMA,
// DESCRIBE, multiple statements, ...) falls back to rendering in JavaScript.
export async function runQuery(conn, sql) {
  const body = stripSemicolon(sql);
  if (/^\s*(select|with|from|values|table|\()/i.test(body)) {
    try {
      return rowsOf(await conn.query(`SELECT COLUMNS(*)::VARCHAR FROM (${body}\n) AS __sqllogictest`));
    } catch (e) {
      if (isEngineCrash(errorMessage(e))) throw e;
      // otherwise fall through: report the unwrapped query's own error, if any
    }
  }
  return rowsOf(await conn.query(sql));
}

export function numericallyEqual(a, b) {
  if (!/^-?[\d.]+(e[-+]?\d+)?$/i.test(a) || !/^-?[\d.]+(e[-+]?\d+)?$/i.test(b)) return false;
  const x = Number(a);
  const y = Number(b);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return false;
  return Math.abs(x - y) <= 1e-9 * Math.max(1, Math.abs(x), Math.abs(y));
}
