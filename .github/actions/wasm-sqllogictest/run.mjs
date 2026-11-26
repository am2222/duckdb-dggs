#!/usr/bin/env node
// Runs a DuckDB extension's sqllogictest files against a locally built WASM
// extension, inside a DuckDB-WASM engine running in Node.
//
//   node run.mjs --extension duck_dggs \
//                --extension-file build/duck_dggs.duckdb_extension.wasm \
//                [--tests test/sql] [--include-slow] [--bundle base|eh|mvp]
//                [--repo URL ...] [--timeout 600] [--json report.json] [--verbose]
//                [--docs <file|dir|dir/**/*.md> ...] [--known-failures docs/wasm-known-failures.json]
//                [--published] [--extension-source <text>]
//
// Each test file gets a fresh database. `require <extension>` LOADs the file
// under test; any other required extension is installed from the engine's own
// repository or one of the --repo URLs, and the file is skipped if none has it.
//
// --docs also runs the SQL examples in markdown pages (one fresh database per
// page, the extension LOADed from --extension-file). With --docs and no
// --tests, only the doc examples run. --published marks examples that use
// functions the engine does not know as `unreleased` instead of `error`.
//
// Results are compared as DuckDB's native runner does: every value is cast to
// VARCHAR inside the engine, so lists, structs, geometries and doubles print
// exactly as they do natively.

import { createHash } from 'node:crypto';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  errorMessage, isEngineCrash, numericallyEqual, runQuery,
} from '../../../packages/duckdb-docs-live/src/sql.mjs';
import { extractExamples } from '../../../packages/duckdb-docs-live/src/extract.mjs';
import { runExample, applyKnownFailures } from '../../../packages/duckdb-docs-live/src/examples.mjs';

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
    enginePackage: '@duckdb/duckdb-wasm',
    docs: [],
    knownFailures: null,
    published: false,
    extensionSource: '',
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
      case '--docs': opts.docs.push(next()); break;
      case '--known-failures': opts.knownFailures = next(); break;
      case '--published': opts.published = true; break;
      case '--extension-source': opts.extensionSource = next(); break;
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
  if (opts.tests.length === 0 && opts.docs.length === 0) opts.tests.push('test/sql');
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

  if (await installExtension(conn, name, opts.repos)) return 'ok';
  return { skip: `require ${name}: not available for this engine` };
}

// Installs and LOADs `name` from the engine's default repository, then from
// each repo in turn. Returns whether one of them had it.
async function installExtension(conn, name, repos) {
  for (const repo of [null, ...repos]) {
    try {
      const from = repo ? ` FROM '${quote(repo)}'` : '';
      await conn.query(`FORCE INSTALL '${quote(name)}'${from}`);
      await conn.query(`LOAD '${quote(name)}'`);
      return true;
    } catch { /* next repository */ }
  }
  return false;
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

// ---------------------------------------------------------------------------
// Doc examples
// ---------------------------------------------------------------------------

const DOC_SKIP_DIRS = new Set(['node_modules', '.vitepress', 'superpowers']);
const GLOB_CHARS = /[*?[\]{}]/;

// Returns [{ file, page }]. A directory is walked for .md files; a pattern such
// as docs/**/*.md walks the directory before its first wildcard segment. The page
// name is the path relative to that root, without .md, with forward slashes.
function collectDocs(inputs) {
  const found = new Map();
  const walk = (dir, root) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      if (DOC_SKIP_DIRS.has(e.name)) continue;
      const full = path.join(dir, e.name);
      if (e.isDirectory()) walk(full, root);
      else if (e.name.endsWith('.md')) add(full, root);
    }
  };
  const add = (file, root) => {
    const page = path.relative(root, file).split(path.sep).join('/').replace(/\.md$/, '');
    if (!found.has(file)) found.set(file, page);
  };
  for (const input of inputs) {
    let root = input;
    if (GLOB_CHARS.test(input)) {
      const segs = input.split('/');
      const i = segs.findIndex((s) => GLOB_CHARS.test(s));
      root = segs.slice(0, i).join('/') || '.';
    }
    if (!fs.existsSync(root)) throw new Error(`no such docs path: ${root}`);
    if (fs.statSync(root).isDirectory()) walk(root, root);
    else add(root, path.dirname(root));
  }
  return [...found].map(([file, page]) => ({ file, page }));
}

async function runDocs(engine, opts) {
  const pages = collectDocs(opts.docs);
  const examples = [];
  for (const { file, page } of pages) {
    examples.push(...extractExamples(fs.readFileSync(file, 'utf8'), page));
  }
  if (examples.length === 0) {
    console.log('\nno doc examples found');
    return { failed: true, results: [], problems: [] };
  }

  const byPage = new Map();
  for (const ex of examples) {
    if (!byPage.has(ex.page)) byPage.set(ex.page, []);
    byPage.get(ex.page).push(ex);
  }

  let results = [];
  for (const [page, exs] of byPage) {
    const handle = await openDb(engine);
    const session = {
      dead: false,
      connect: () => handle.db.connect(),
      async ensure(name) {
        if (!(await installExtension(handle.conn, name, opts.repos))) {
          throw new Error(`${name} is not available for this engine`);
        }
      },
    };
    try {
      await handle.conn.query(`LOAD '${quote(opts.extensionFile)}'`);
    } catch (e) {
      const detail = `could not load ${opts.extensionFile}: ${errorMessage(e)}`;
      for (const ex of exs) results.push({ id: ex.id, page, status: 'error', detail, statements: [] });
      await handle.close();
      continue;
    }
    console.log(`docs ${page}: ${exs.length} example(s)`);
    try {
      for (const ex of exs) {
        results.push(await withTimeout(
          runExample(session, ex, { published: opts.published }), opts.timeout, ex.id)
          .then((r) => (r.failures ? { id: ex.id, page, status: 'error', detail: r.failures[0].message, statements: [] } : r)));
      }
    } finally {
      await handle.close();
    }
  }

  let problems = [];
  if (opts.knownFailures) {
    const known = JSON.parse(fs.readFileSync(opts.knownFailures, 'utf8'));
    ({ results, problems } = applyKnownFailures(results, known));
  }
  const byId = new Map(examples.map((e) => [e.id, e]));
  return { failed: false, results, problems, byId };
}

const STATUS_ORDER = ['match', 'differs', 'error', 'skipped', 'shown', 'unreleased'];
const STATUS_ICON = { match: '✅', differs: '❌', error: '❌', skipped: '⏭️', shown: '👁️', unreleased: '🕓' };

const oneLine = (s, max = 200) => {
  const t = String(s ?? '').replace(/\s+/g, ' ').trim();
  return t.length > max ? `${t.slice(0, max - 1)}…` : t;
};

function docDetail(r) {
  if (r.detail) return r.detail;
  const bad = r.statements.find((s) => s.status !== 'match' && s.status !== 'shown') ?? r.statements.find((s) => s.detail);
  if (!bad) return '';
  return bad.detail ?? '';
}

function docCounts(results) {
  const n = Object.fromEntries(STATUS_ORDER.map((s) => [s, 0]));
  for (const r of results) n[r.status] = (n[r.status] ?? 0) + 1;
  return n;
}

const countLine = (n) => STATUS_ORDER.map((s) => `${n[s]} ${s}`).join(' · ');

function docsSummaryMarkdown(docs, info) {
  const lines = [
    '### Doc examples',
    '',
    `Engine \`${info.engine}\` (${info.version}, ${info.platform}), extension \`${info.extensionSource}\``,
    '',
    '| | Page | Example | Detail |',
    '|---|---|---|---|',
  ];
  for (const r of docs.results) {
    if (r.status === 'match') continue;
    const ex = docs.byId.get(r.id);
    const name = ex ? `${ex.heading} (line ${ex.line})` : r.id;
    const detail = oneLine(docDetail(r)).replace(/\|/g, '\\|');
    lines.push(`| ${STATUS_ICON[r.status]} ${r.status} | \`${r.page}\` | ${name} | ${detail} |`);
  }
  for (const p of docs.problems) lines.push(`| ❌ problem | | | ${oneLine(p)} |`);
  lines.push('', countLine(docCounts(docs.results)), '');
  return lines.join('\n');
}

function printDocs(docs) {
  console.log('\nDoc examples');
  for (const r of docs.results) {
    if (r.status === 'match') continue;
    const ex = docs.byId.get(r.id);
    console.log(`${r.status.padEnd(10)} ${r.id}${ex ? ` (${ex.heading}, line ${ex.line})` : ''}`);
    const d = oneLine(docDetail(r), 300);
    if (d) console.log(`           ${d}`);
  }
  for (const p of docs.problems) console.log(`problem    ${p}`);
  console.log(`\n${countLine(docCounts(docs.results))}  (${docs.results.length} examples)`);
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

  const files = opts.tests.length ? collectTests(opts.tests, opts.includeSlow) : [];
  if (opts.tests.length && files.length === 0) throw new Error(`no test files under ${opts.tests.join(', ')}`);

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
  if (opts.tests.length) console.log(`\n${count('pass')} passed, ${count('fail')} failed, ${count('skip')} skipped`);

  const meta = { extension: opts.extension, file: opts.extensionFile, engine: `${opts.enginePackage}@${engineVersion}`, ...info };
  const extensionSource = opts.extensionSource || opts.extensionFile;

  let docs = null;
  if (opts.docs.length) {
    docs = await runDocs(engine, opts);
    if (docs.results.length) printDocs(docs);
  }

  if (opts.json) {
    fs.writeFileSync(opts.json, JSON.stringify({ ...meta, results, ...(docs ? { docs: docs.results, docsProblems: docs.problems } : {}) }, null, 2));
  }
  if (process.env.GITHUB_STEP_SUMMARY) {
    if (opts.tests.length) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, summaryMarkdown(results, meta));
    if (docs && docs.results.length) {
      fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, docsSummaryMarkdown(docs, { ...info, engine: meta.engine, extensionSource }));
    }
  }

  // All-skipped is not a pass: it usually means the extension never loaded.
  // (Only applies when the sqllogictest part ran.)
  const testsFailed = opts.tests.length > 0 && (count('fail') > 0 || count('pass') === 0);
  let docsFailed = false;
  if (docs) {
    const n = docCounts(docs.results);
    docsFailed = docs.failed || n.differs > 0 || n.error > 0 || docs.problems.length > 0;
  }
  if (testsFailed || docsFailed) process.exit(1);
}

main().catch((e) => {
  console.error(`error: ${e.message}`);
  process.exit(2);
});
