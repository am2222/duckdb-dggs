// Patch the cloned haybarn-extension-wasm-tester so a YAML-list
// `excluded_platforms` does not abort the whole run.
//
// The tester's wasmEnabled() calls .trim() on the value, which throws
// "TypeError: e.trim is not a function" for descriptors that write a list
// instead of a delimited string (e.g. mssql_ducklake). That happens while
// reading the catalog, before any extension is tested, so one bad descriptor
// takes down every run.
//
// Idempotent, and a no-op once upstream accepts both forms.
//
// Usage: node scripts/wasm-tester-patch.mjs <tester-dir>

import fs from 'node:fs';
import path from 'node:path';

const testerDir = process.argv[2];
if (!testerDir) {
  console.error('usage: node wasm-tester-patch.mjs <tester-dir>');
  process.exit(2);
}

const file = path.join(testerDir, 'src', 'catalog.mjs');
if (!fs.existsSync(file)) {
  console.error(`  (no ${file}; skipping patch)`);
  process.exit(0);
}

const before = fs.readFileSync(file, 'utf8');
if (before.includes('Array.isArray(excluded)')) {
  process.exit(0); // already patched
}

const original = `function wasmEnabled(excluded) {
  const e = excluded || '';
  if (e.trim() === 'wasm') return false;
  return !e.split(/[;,]/).map((s) => s.trim()).includes('wasm_eh');
}`;

const patched = `function wasmEnabled(excluded) {
  const tokens = Array.isArray(excluded)
    ? excluded.map((s) => String(s).trim())
    : String(excluded ?? '').split(/[;,]/).map((s) => s.trim());
  if (tokens.length === 1 && tokens[0] === 'wasm') return false;
  return !tokens.includes('wasm_eh');
}`;

if (!before.includes(original)) {
  console.error('  (wasmEnabled no longer matches; assuming upstream changed it)');
  process.exit(0);
}

fs.writeFileSync(file, before.replace(original, patched));
console.error('patched tester for list-form excluded_platforms');
