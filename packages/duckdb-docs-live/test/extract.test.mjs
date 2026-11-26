import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { extractExamples } from '../src/extract.mjs';

const fx = (name) =>
  readFileSync(new URL(`./fixtures/${name}`, import.meta.url), 'utf8');
const fence = (body, info = 'sql') => '```' + info + '\n' + body + '\n```\n';

test('signature fence is not an example', () => {
  const md = '## seqnum_to_zorder\n\n### Signature\n\n' +
    fence('UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER)');
  assert.deepEqual(extractExamples(md, 'p'), []);
});

test('arrow comments', () => {
  const md = '## Cells\n\n' + fence(
    'SELECT dggs_n_cells(5);\n-- → 10242\n\nSELECT dggs_res_info(5);\n-- → {res: 5, cells: 10242}');
  const ex = extractExamples(md, 'p');
  assert.equal(ex.length, 1);
  const [a, b] = ex[0].statements;
  assert.equal(ex[0].statements.length, 2);
  assert.deepEqual(a.expected, { kind: 'arrow', text: '10242' });
  assert.deepEqual(b.expected, { kind: 'arrow', text: '{res: 5, cells: 10242}' });
  assert.equal(a.sql, 'SELECT dggs_n_cells(5);');
  assert.equal(ex[0].id, 'p#cells/1');
  assert.equal(ex[0].line, 3);
  assert.equal(ex[0].mode, 'compare');
});

test('arrow continuation lines', () => {
  const md = '## H\n\n' + fence('SELECT 1;\n-- → a\n-- b\n-- c\n\nSELECT 2;');
  const ex = extractExamples(md, 'p');
  assert.equal(ex[0].statements[0].expected.text, 'a\nb\nc');
  assert.equal(ex[0].statements[1].expected, null);
});

test('box table', () => {
  const ex = extractExamples(fx('geo-distance.md'), 'coverage-geodesy');
  assert.equal(ex.length, 1);
  assert.equal(ex[0].heading, 'geo_distance_km');
  assert.equal(ex[0].statements.length, 1);
  assert.equal(ex[0].statements[0].expected.kind, 'box');
  assert.ok(ex[0].statements[0].expected.text.startsWith('┌'));
  assert.ok(ex[0].statements[0].sql.includes('-- Ottawa → London'));
});

test('ids use the ## section and ignore ### headings', () => {
  const ex = extractExamples(fx('geo-distance.md'), 'functions/coverage-geodesy');
  assert.equal(ex[0].id, 'functions/coverage-geodesy#geo_distance_km/1');
  const md = '# Title\n\n' + fence('SELECT 1;\n-- → 1') +
    '\n## A b\n\n### Example\n\n' + fence('SELECT 2;\n-- → 2') +
    '\n### Example\n\n' + fence('SELECT 3;\n-- → 3') +
    '\n## C\n\n' + fence('SELECT 4;\n-- → 4');
  const e = extractExamples(md, 'p');
  assert.deepEqual(e.map((x) => x.id), ['p#title/1', 'p#a-b/1', 'p#a-b/2', 'p#c/1']);
  assert.deepEqual(e.map((x) => x.heading), ['Title', 'A b', 'A b', 'C']);
  assert.equal(extractExamples(fence('SELECT 1;\n-- → 1'), 'p')[0].id, 'p#top/1');
});

test('box applies to last statement', () => {
  const md = '## T\n\n' + fence('CREATE TABLE t AS SELECT 1 x;\nSELECT x FROM t;') +
    '\n```\n┌───┐\n│ x │\n└───┘\n```\n';
  const ex = extractExamples(md, 'p');
  assert.equal(ex[0].statements[0].expected, null);
  assert.equal(ex[0].statements[1].expected.kind, 'box');
});

test('markers', () => {
  const md = '## M\n\n' + fence('SELECT 1;', 'sql norun') + '\n' + fence('SELECT 2;', 'sql run');
  const ex = extractExamples(md, 'p');
  assert.deepEqual(ex.map((e) => e.mode), ['norun', 'run']);
  assert.deepEqual(ex.map((e) => e.id), ['p#m/1', 'p#m/2']);
});

test('needs spatial', () => {
  const md = '## S\n\n' + fence('INSTALL spatial;\nLOAD spatial;\nSELECT 1;\n-- → 1');
  const ex = extractExamples(md, 'p');
  assert.deepEqual(ex[0].needs, ['spatial']);
  assert.ok(ex[0].statements.some((s) => /LOAD spatial/.test(s.sql)));
});

test('semicolons in quotes and trailing fragment', () => {
  const md = '## Q\n\n' + fence("SELECT 'a;\nb';\nSELECT 2\n-- → 2");
  const ex = extractExamples(md, 'p');
  assert.equal(ex[0].statements.length, 2);
  assert.equal(ex[0].statements[0].sql, "SELECT 'a;\nb';");
  assert.equal(ex[0].statements[1].expected.text, '2');
});

function walk(dir, out = []) {
  for (const n of readdirSync(dir)) {
    if (['node_modules', '.vitepress', 'superpowers'].includes(n)) continue;
    const p = join(dir, n);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (n.endsWith('.md')) out.push(p);
  }
  return out;
}

test('ids unique across docs', () => {
  const root = fileURLToPath(new URL('../../../docs/', import.meta.url));
  const ids = new Set();
  let total = 0;
  for (const f of walk(root)) {
    const page = f.slice(root.length).replace(/\.md$/, '');
    for (const e of extractExamples(readFileSync(f, 'utf8'), page)) {
      assert.ok(!ids.has(e.id), `duplicate id ${e.id}`);
      ids.add(e.id);
      total++;
    }
  }
  assert.ok(total > 80, `only ${total} examples`);
});
