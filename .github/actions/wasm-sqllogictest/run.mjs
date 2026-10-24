#!/usr/bin/env node
// Runs a DuckDB extension's sqllogictest files against a locally built WASM
// extension, inside a DuckDB-WASM engine running in Node.
//
//   node run.mjs --extension duck_dggs \
//                --extension-file build/duck_dggs.duckdb_extension.wasm \
//                [--tests test/sql] [--include-slow] [--bundle base|eh|mvp]
//                [--repo URL ...] [--timeout 600] [--json report.json] [--verbose]
//
// Each test file gets a fresh database. `require <extension>` LOADs the file
// under test; any other required extension is installed from the engine's own
// repository or one of the --repo URLs, and the file is skipped if none has it.
//
// Results are compared as DuckDB's native runner does: every value is cast to
// VARCHAR inside the engine, so lists, structs, geometries and doubles print
// exactly as they do natively.

import { createHash } from 'node:crypto';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, 'package.json'));

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

function parseArgs(argv) {
  const opts = {
    extension: null,
    extensionFile: null,
    tests: [],
    includeSlow: false,
    bundle: null,
    repos: [],
    timeout: 600,
    json: null,
    verbose: false,
    enginePackage: '@haybarn/haybarn-wasm',
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      if (i + 1 >= argv.length) throw new Error(`${a} needs a value`);
      return argv[++i];
    };
    switch (a) {
      case '--extension': opts.extension = next(); break;
      case '--extension-file': opts.extensionFile = next(); break;
      case '--tests': opts.tests.push(next()); break;
      case '--include-slow': opts.includeSlow = true; break;
      case '--bundle': opts.bundle = next(); break;
      case '--repo': opts.repos.push(next()); break;
      case '--timeout': opts.timeout = Number(next()); break;
      case '--json': opts.json = next(); break;
      case '--verbose': opts.verbose = true; break;
      case '--engine-package': opts.enginePackage = next(); break;
      case '-h': case '--help':
        console.log(fs.readFileSync(fileURLToPath(import.meta.url), 'utf8').split('\n').slice(1, 10).join('\n'));
        process.exit(0);
        break;
      default: throw new Error(`unknown argument: ${a}`);
    }
  }
  if (!opts.extension) throw new Error('--extension is required');
  if (!opts.extensionFile) throw new Error('--extension-file is required');
  opts.extensionFile = path.resolve(opts.extensionFile);
  if (!fs.existsSync(opts.extensionFile)) throw new Error(`no such file: ${opts.extensionFile}`);
  if (opts.tests.length === 0) opts.tests.push('test/sql');
  return opts;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

// The DuckDB-WASM worker prints every query error (with a stack) to its own
// console, which worker_threads forwards to ours. web-worker 1.2.0 doesn't
// expose the Worker options, so capture the output at construction instead.
// The last lines are kept: when the engine dies, they say why (e.g. a native
// library's "FATAL ERROR: ..." just before it calls exit()).
const workerOutput = [];
const threads = require('node:worker_threads');
if (!threads.__sqllogictestPatched) {
  const OrigWorker = threads.Worker;
  threads.Worker = class QuietWorker extends OrigWorker {
    constructor(filename, options) {
      super(filename, { ...(options || {}), stdout: true, stderr: true });
      for (const stream of [this.stdout, this.stderr]) {
        stream.setEncoding('utf8');
        stream.on('data', (chunk) => {
          workerOutput.push(...chunk.split('\n').filter((l) => l.trim() && !/^\s+at /.test(l)));
          workerOutput.splice(0, workerOutput.length - 20);
        });
      }
    }
  };
  threads.__sqllogictestPatched = true;
}

function loadEngine(enginePackage, bundleName) {
  const Worker = require('web-worker');
  const nodeEntry = require.resolve(`${enginePackage}/dist/duckdb-node.cjs`);
  const duckdb = require(nodeEntry);
  const dist = path.dirname(nodeEntry);
  // DuckDB-WASM 2.x ships one "base" bundle (platform wasm_eh); 1.x shipped
  // separate "eh" and "mvp" bundles.
  const available = ['base', 'eh', 'mvp'].filter((b) =>
    fs.existsSync(path.join(dist, `duckdb-node-${b}.worker.cjs`)));
  const bundle = bundleName ?? available[0];
  if (!available.includes(bundle)) {
    throw new Error(`bundle '${bundle}' not in ${enginePackage} (has: ${available.join(', ')})`);
  }
  return {
    duckdb,
    Worker,
    worker: path.join(dist, `duckdb-node-${bundle}.worker.cjs`),
    module: path.join(dist, `duckdb-${bundle}.wasm`),
    bundle,
  };
}

async function openDb(engine) {
  const worker = new engine.Worker(engine.worker);
  const db = new engine.duckdb.AsyncDuckDB({ log() {} }, worker);
  await db.instantiate(engine.module, null);
  await db.open({ allowUnsignedExtensions: true });
  const conn = await db.connect();
  return {
    conn,
    db,
    async close() {
      try { await conn.close(); } catch { /* already gone */ }
      try { await db.terminate(); } catch { /* already gone */ }
      try { worker.terminate(); } catch { /* already gone */ }
    },
  };
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

// Directives that gate on the native build or its environment; a file that
// needs one cannot be judged here, so it is skipped.
const UNSUPPORTED_REQUIRES = new Set([
  'notwindows', 'windows', 'mingw', 'notmingw', 'notmusl', 'longdouble', '64bit',
  'noforcestorage', 'no_force_storage', 'nothreadsan', 'strinline', 'skip_reload',
  'no_alternative_verify', 'no_latest_storage', 'no_vector_verification',
  'no_extension_autoloading', 'vacuum_rebuild_indexes', 'ram', 'disk_space',
  'vector_size', 'exact_vector_size', 'block_size',
]);

// Directives with no effect on what this runner checks.
const IGNORED = new Set([
  'set', 'reset', 'tags', 'test-env', 'sleep', 'require-reinit',
]);

function readBlock(lines, i) {
  // Lines from i up to (not including) the first blank line.
  const out = [];
  while (i < lines.length && lines[i].trim() !== '') out.push(lines[i++]);
  return [out, i];
}

function parse(text) {
  const lines = text.split(/\r?\n/);
  const root = [];
  const stack = [root];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    const trimmed = line.trim();
    if (trimmed === '' || trimmed.startsWith('#')) { i++; continue; }
    const lineNo = i + 1;
    const tok = trimmed.split(/\s+/);
    const cmd = tok[0];
    const push = (rec) => stack[stack.length - 1].push({ ...rec, line: lineNo });
    i++;

    if (cmd === 'statement' || cmd === 'query') {
      let body;
      [body, i] = readBlock(lines, i);
      const sep = body.findIndex((l) => l.trim() === '----');
      const sql = (sep >= 0 ? body.slice(0, sep) : body).join('\n');
      // Expected results (or a statement's expected error) run to the blank line.
      const expected = sep >= 0 ? body.slice(sep + 1) : null;
      if (cmd === 'statement') {
        push({ type: 'statement', expect: tok[1] ?? 'ok', sql, error: expected ? expected.join('\n') : null });
      } else {
        push({ type: 'query', types: tok[1] ?? '', sort: tok[2] ?? 'nosort', sql, expected: expected ?? [] });
      }
    } else if (cmd === 'require') {
      push({ type: 'require', name: tok[1], args: tok.slice(2) });
    } else if (cmd === 'require-env') {
      push({ type: 'require-env', name: tok[1], value: tok[2] });
    } else if (cmd === 'mode') {
      push({ type: 'mode', mode: tok[1] });
    } else if (cmd === 'skipif' || cmd === 'onlyif') {
      push({ type: cmd, system: tok[1] });
    } else if (cmd === 'loop' || cmd === 'concurrentloop') {
      const rec = { type: 'loop', variable: tok[1], values: [], body: [], line: lineNo };
      for (let v = Number(tok[2]); v < Number(tok[3]); v++) rec.values.push(String(v));
      stack[stack.length - 1].push(rec);
      stack.push(rec.body);
    } else if (cmd === 'foreach' || cmd === 'concurrentforeach') {
      const rec = { type: 'loop', variable: tok[1], values: tok.slice(2), body: [], line: lineNo };
      stack[stack.length - 1].push(rec);
      stack.push(rec.body);
    } else if (cmd === 'endloop') {
      if (stack.length === 1) throw new Error(`line ${lineNo}: endloop without loop`);
      stack.pop();
    } else if (cmd === 'halt') {
      push({ type: 'halt' });
    } else if (cmd === 'restart') {
      push({ type: 'restart' });
    } else if (IGNORED.has(cmd)) {
      // nothing to do
    } else {
      push({ type: 'unsupported', directive: cmd });
    }
  }
  if (stack.length !== 1) throw new Error('loop without endloop');
  return root;
}

// Expands loops into a flat record list, substituting ${var} in SQL and
// expected output.
function flatten(records, vars = {}) {
  const sub = (s) => (s == null ? s : s.replace(/\$\{(\w+)\}/g, (m, v) => (v in vars ? vars[v] : m)));
  const out = [];
  for (const r of records) {
    if (r.type === 'loop') {
      for (const value of r.values) out.push(...flatten(r.body, { ...vars, [r.variable]: value }));
      continue;
    }
    const c = { ...r };
    if (c.sql != null) c.sql = sub(c.sql);
    if (c.error != null) c.error = sub(c.error);
    if (c.expected) c.expected = c.expected.map(sub);
    out.push(c);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

const quote = (s) => s.replace(/'/g, "''");

function stripSemicolon(sql) {
  return sql.trim().replace(/;\s*$/, '');
}

function errorMessage(e) {
  return String(e?.message ?? e).replace(/\n\s+at .*$/gs, '').trim();
}

// The WASM module stops for good on exit(), abort() or a trap; every later
// query on it fails or hangs, so the rest of the file can't be run.
function isEngineCrash(message) {
  return /Program terminated with exit|Aborted\(|RuntimeError: |ExitStatus/.test(message);
}

// DuckDB's runner prints NULL as "NULL" and the empty string as "(empty)".
function render(v) {
  if (v === null || v === undefined) return 'NULL';
  const s = typeof v === 'bigint' ? v.toString() : String(v);
  return s === '' ? '(empty)' : s;
}

function rowsOf(table) {
  const cols = table.schema.fields.map((f) => f.name);
  const rows = [];
  for (const row of table.toArray()) {
    const obj = row.toJSON();
    rows.push(cols.map((c) => render(obj[c])));
  }
  return { cols: cols.length, rows };
}

// Runs a query and returns its rows as strings. Plain SELECTs are wrapped so
// the engine renders every value; anything that can't be wrapped (PRAGMA,
// DESCRIBE, multiple statements, ...) falls back to rendering in JavaScript.
async function runQuery(conn, sql) {
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

function numericallyEqual(a, b) {
  if (!/^-?[\d.]+(e[-+]?\d+)?$/i.test(a) || !/^-?[\d.]+(e[-+]?\d+)?$/i.test(b)) return false;
  const x = Number(a);
  const y = Number(b);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return false;
  return Math.abs(x - y) <= 1e-9 * Math.max(1, Math.abs(x), Math.abs(y));
}

function compareQuery(rec, actual) {
  const ncols = rec.types.length || actual.cols;
  if (actual.cols !== ncols) {
    return `expected ${ncols} column(s), got ${actual.cols}`;
  }
  let rows = actual.rows;
  if (rec.sort === 'rowsort') rows = [...rows].sort((a, b) => a.join('\t').localeCompare(b.join('\t')));
  const values = rec.sort === 'valuesort' ? rows.flat().sort() : rows.flat();

  const exp = rec.expected;
  const hash = exp.length === 1 && exp[0].trim().match(/^(\d+) values hashing to ([0-9a-f]{32})$/);
  if (hash) {
    const md5 = createHash('md5').update(values.map((v) => `${v}\n`).join('')).digest('hex');
    if (Number(hash[1]) !== values.length || md5 !== hash[2]) {
      return `expected ${hash[1]} values hashing to ${hash[2]}, got ${values.length} hashing to ${md5}`;
    }
    return null;
  }

  // Results are either one row per line (tab separated) or, for multi-column
  // results, one value per line.
  let expectedValues;
  if (ncols > 1 && exp.length === values.length && exp.every((l) => !l.includes('\t'))) {
    expectedValues = exp;
  } else {
    let expRows = exp.map((l) => l.split('\t'));
    if (rec.sort === 'rowsort') expRows = [...expRows].sort((a, b) => a.join('\t').localeCompare(b.join('\t')));
    const bad = expRows.findIndex((r) => r.length !== ncols);
    if (bad >= 0) return `expected row ${bad + 1} has ${expRows[bad].length} value(s), not ${ncols}`;
    expectedValues = rec.sort === 'valuesort' ? expRows.flat().sort() : expRows.flat();
  }
  if (expectedValues.length !== values.length) {
    return `expected ${expectedValues.length / ncols} row(s), got ${values.length / ncols}`;
  }
  for (let k = 0; k < values.length; k++) {
    const e = expectedValues[k];
    const a = values[k];
    if (e !== a && !numericallyEqual(e, a)) {
      return `row ${Math.floor(k / ncols) + 1}, column ${(k % ncols) + 1}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`;
    }
  }
  return null;
}

function errorMatches(expected, message) {
  const want = expected.trim();
  if (want.startsWith('<REGEX>:')) return new RegExp(want.slice(8), 's').test(message);
  if (want.startsWith('<!REGEX>:')) return !new RegExp(want.slice(9), 's').test(message);
  return message.includes(want);
}

// Resolves `require <name>`: 'ok', or { skip: reason } / { fail: reason }.
async function satisfyRequire(conn, rec, opts) {
  const name = rec.name;
  if (name === opts.extension) {
    try {
      await conn.query(`LOAD '${quote(opts.extensionFile)}'`);
      return 'ok';
    } catch (e) {
      return { fail: `could not load ${opts.extensionFile}: ${errorMessage(e)}` };
    }
  }
  if (UNSUPPORTED_REQUIRES.has(name)) return { skip: `require ${name}` };

  const loaded = async () => {
    const r = await conn.query(
      `SELECT count(*)::INT AS n FROM duckdb_extensions() WHERE extension_name = '${quote(name)}' AND loaded`);
    return Number(r.toArray()[0].toJSON().n) > 0;
  };
  try {
    if (await loaded()) return 'ok';
  } catch { /* not an extension query we can answer; try loading */ }

  // The engine's default repository first, then each --repo.
  for (const repo of [null, ...opts.repos]) {
    try {
      const from = repo ? ` FROM '${quote(repo)}'` : '';
      await conn.query(`FORCE INSTALL '${quote(name)}'${from}`);
      await conn.query(`LOAD '${quote(name)}'`);
      return 'ok';
    } catch { /* next repository */ }
  }
  return { skip: `require ${name}: not available for this engine` };
}

async function runFile(engine, file, opts) {
  const text = fs.readFileSync(file, 'utf8');
  let records;
  try {
    records = flatten(parse(text));
  } catch (e) {
    return { file, status: 'fail', failures: [{ line: 0, message: `parse error: ${e.message}` }] };
  }

  let handle = await openDb(engine);
  const requires = [];
  const failures = [];
  let ran = 0;
  let skipping = false;
  let condition = null; // pending skipif/onlyif for the next record
  try {
    for (const rec of records) {
      if (rec.type === 'mode') {
        if (rec.mode === 'skip') skipping = true;
        if (rec.mode === 'unskip') skipping = false;
        continue;
      }
      if (skipping) continue;
      if (rec.type === 'skipif' || rec.type === 'onlyif') { condition = rec; continue; }
      if (condition) {
        // This runner identifies as the systems 'duckdb' and 'wasm'.
        const matches = ['duckdb', 'wasm'].includes(condition.system);
        const skip = condition.type === 'skipif' ? matches : !matches;
        condition = null;
        if (skip) continue;
      }

      switch (rec.type) {
        case 'require': {
          const r = await satisfyRequire(handle.conn, rec, opts);
          if (r.skip) return { file, status: 'skip', reason: r.skip };
          if (r.fail) return { file, status: 'fail', failures: [{ line: rec.line, message: r.fail }] };
          requires.push(rec);
          break;
        }
        case 'require-env': {
          const v = process.env[rec.name];
          if (v === undefined || (rec.value !== undefined && v !== rec.value)) {
            return { file, status: 'skip', reason: `require-env ${rec.name}` };
          }
          break;
        }
        case 'unsupported':
          return { file, status: 'skip', reason: `unsupported directive '${rec.directive}' (line ${rec.line})` };
        case 'halt':
          return finish();
        case 'restart':
          await handle.close();
          handle = await openDb(engine);
          for (const r of requires) await satisfyRequire(handle.conn, r, opts);
          break;
        case 'statement': {
          ran++;
          workerOutput.length = 0;
          let err = null;
          try { await handle.conn.query(rec.sql); } catch (e) { err = errorMessage(e); }
          if (err && isEngineCrash(err)) return await crashed(rec, err);
          if (rec.expect === 'ok' && err) {
            failures.push({ line: rec.line, sql: rec.sql, message: `statement failed: ${err}` });
          } else if (rec.expect === 'error') {
            if (!err) failures.push({ line: rec.line, sql: rec.sql, message: 'statement succeeded, expected an error' });
            else if (rec.error && !errorMatches(rec.error, err)) {
              failures.push({ line: rec.line, sql: rec.sql, message: `wrong error\n  expected: ${rec.error}\n  got:      ${err}` });
            }
          }
          break;
        }
        case 'query': {
          ran++;
          workerOutput.length = 0;
          let actual;
          try {
            actual = await runQuery(handle.conn, rec.sql);
          } catch (e) {
            const err = errorMessage(e);
            if (isEngineCrash(err)) return await crashed(rec, err);
            failures.push({ line: rec.line, sql: rec.sql, message: `query failed: ${err}` });
            break;
          }
          const diff = compareQuery(rec, actual);
          if (diff) {
            failures.push({
              line: rec.line,
              sql: rec.sql,
              message: diff,
              expected: rec.expected.join('\n'),
              actual: actual.rows.map((r) => r.join('\t')).join('\n'),
            });
          }
          break;
        }
        default:
          break;
      }
    }
    return finish();
  } finally {
    await handle.close();
  }

  function finish() {
    return { file, status: failures.length ? 'fail' : 'pass', ran, failures };
  }

  async function crashed(rec, err) {
    await new Promise((resolve) => setTimeout(resolve, 200)); // let the worker's output arrive
    const fatal = workerOutput.filter((l) => !l.includes(err) && !/ExitStatus|^\s*[{}]|^\s*\w+: /.test(l)).slice(-3);
    const why = fatal.length ? `\n  engine output: ${fatal.join('\n                 ')}` : '';
    failures.push({ line: rec.line, sql: rec.sql, message: `engine crashed, rest of file not run: ${err}${why}` });
    return finish();
  }
}

function withTimeout(promise, seconds, file) {
  let timer;
  const timeout = new Promise((resolve) => {
    timer = setTimeout(() => resolve({
      file, status: 'fail', failures: [{ line: 0, message: `timed out after ${seconds}s` }],
    }), seconds * 1000);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

function collectTests(paths, includeSlow) {
  const exts = includeSlow ? ['.test', '.test_slow'] : ['.test'];
  const files = [];
  const walk = (p) => {
    const st = fs.statSync(p);
    if (st.isDirectory()) {
      for (const e of fs.readdirSync(p).sort()) walk(path.join(p, e));
    } else if (exts.some((x) => p.endsWith(x))) {
      files.push(p);
    }
  };
  for (const p of paths) walk(p);
  return files;
}

function summaryMarkdown(results, info) {
  const icon = { pass: '✅', fail: '❌', skip: '⏭️' };
  const lines = [
    `### WASM sqllogictest: ${info.extension}`,
    '',
    `Engine \`${info.engine}\` (${info.version}, ${info.platform}), extension \`${path.basename(info.file)}\``,
    '',
    '| | File | Result |',
    '|---|---|---|',
  ];
  for (const r of results) {
    const detail = r.status === 'pass' ? `${r.ran} statements/queries`
      : r.status === 'skip' ? r.reason
        : `${r.failures.length} failure(s), first at line ${r.failures[0].line}`;
    lines.push(`| ${icon[r.status]} | \`${r.file}\` | ${detail} |`);
  }
  return `${lines.join('\n')}\n`;
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const engine = loadEngine(opts.enginePackage, opts.bundle);

  const probe = await openDb(engine);
  const info = (await probe.conn.query(
    "SELECT version() AS version, (SELECT platform FROM pragma_platform()) AS platform")).toArray()[0].toJSON();
  await probe.close();
  const engineVersion = JSON.parse(fs.readFileSync(
    path.join(path.dirname(require.resolve(`${opts.enginePackage}/dist/duckdb-node.cjs`)), '..', 'package.json'), 'utf8')).version;
  console.log(`engine ${opts.enginePackage}@${engineVersion}: DuckDB ${info.version}, ${info.platform} (${engine.bundle} bundle)`);
  console.log(`extension ${opts.extension}: ${opts.extensionFile}`);

  const files = collectTests(opts.tests, opts.includeSlow);
  if (files.length === 0) throw new Error(`no test files under ${opts.tests.join(', ')}`);

  const results = [];
  for (const file of files) {
    const started = Date.now();
    const r = await withTimeout(runFile(engine, file, opts), opts.timeout, file);
    const secs = ((Date.now() - started) / 1000).toFixed(1);
    results.push(r);
    if (r.status === 'pass') console.log(`PASS ${file} (${r.ran} records, ${secs}s)`);
    else if (r.status === 'skip') console.log(`SKIP ${file}: ${r.reason}`);
    else {
      console.log(`FAIL ${file} (${r.failures.length} failure(s), ${secs}s)`);
      for (const f of opts.verbose ? r.failures : r.failures.slice(0, 10)) {
        console.log(`  ${file}:${f.line}: ${f.message}`);
        if (f.sql) console.log(`    ${f.sql.split('\n').join('\n    ')}`);
        if (f.expected !== undefined) {
          console.log(`    expected:\n      ${f.expected.split('\n').join('\n      ')}`);
          console.log(`    actual:\n      ${f.actual.split('\n').join('\n      ')}`);
        }
      }
      if (!opts.verbose && r.failures.length > 10) console.log(`  ... ${r.failures.length - 10} more (use --verbose)`);
    }
  }

  const count = (s) => results.filter((r) => r.status === s).length;
  console.log(`\n${count('pass')} passed, ${count('fail')} failed, ${count('skip')} skipped`);

  const meta = { extension: opts.extension, file: opts.extensionFile, engine: `${opts.enginePackage}@${engineVersion}`, ...info };
  if (opts.json) fs.writeFileSync(opts.json, JSON.stringify({ ...meta, results }, null, 2));
  if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, summaryMarkdown(results, meta));

  // All-skipped is not a pass: it usually means the extension never loaded.
  if (count('fail') > 0 || count('pass') === 0) process.exit(1);
}

main().catch((e) => {
  console.error(`error: ${e.message}`);
  process.exit(2);
});
