import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { compareResult, parseBox, cellsMatch } from '../src/compare.mjs';
import { extractExamples } from '../src/extract.mjs';

const arrow = (text) => ({ kind: 'arrow', text });
const box = (...lines) => ({ kind: 'box', text: lines.join('\n') });
const one = (v) => ({ names: ['x'], rows: [[v]] });

test('struct keys quoted vs unquoted', () => {
  const r = compareResult(
    arrow('{res: 5, cells: 10242, area_km: 49811.1}'),
    one("{'res': 5, 'cells': 10242, 'area_km': 49811.0923}"),
  );
  assert.equal(r.status, 'match');
});

test('rounded point', () => {
  const e = arrow('POINT (-0.902 -0.000)');
  assert.equal(compareResult(e, one('POINT (-0.9021342 -1.2e-17)')).status, 'match');
  const d = compareResult(e, one('POINT (-0.95 0)'));
  assert.equal(d.status, 'differs');
  assert.match(d.detail, /POINT \(-0\.902 -0\.000\)/);
  assert.match(d.detail, /POINT \(-0\.95 0\)/);
});

test('wildcard', () => {
  const poly = 'POLYGON ((1 2, 3 4, 5 6, 1 2))';
  assert.equal(compareResult(arrow('POLYGON ((...))'), one(poly)).status, 'match');
  assert.equal(compareResult(arrow('POLYGON (…'), one(poly)).status, 'match');
  assert.equal(compareResult(arrow('POINT ((...))'), one(poly)).status, 'differs');
});

test('list', () => {
  assert.equal(
    compareResult(arrow('[2412, 2413, 2381]'), one('[2412, 2413, 2380]')).status,
    'differs',
  );
  assert.equal(
    compareResult(arrow('[2412, 2413, 2381]'), one('[2412, 2413, 2381]')).status,
    'match',
  );
});

test('box exact', () => {
  const md = readFileSync(new URL('./fixtures/geo-distance.md', import.meta.url), 'utf8');
  const ex = extractExamples(md, 'geo').find((e) => e.statements.some((s) => s.expected));
  const expected = ex.statements.find((s) => s.expected).expected;
  assert.equal(expected.kind, 'box');
  assert.equal(
    compareResult(expected, { names: ['…'], rows: [['5364.66413006686']] }).status,
    'match',
  );
  assert.equal(compareResult(expected, one('5400.1')).status, 'differs');
});

const b1 = box(
  '┌─────┐',
  '│ x   │',
  '│ int │',
  '├─────┤',
  '│   1 │',
  '└─────┘',
);

test('box row count', () => {
  const r = compareResult(b1, { names: ['x'], rows: [['1'], ['2']] });
  assert.equal(r.status, 'differs');
  assert.equal(r.detail, 'expected 1 row(s), got 2');
});

test('truncated cell', () => {
  const b = box('┌──┐', '│ x │', '│ l │', '├──┤', '│ [600, 601, 617, … │', '└──┘');
  assert.equal(
    compareResult(b, one('[600, 601, 617, 618, 619]')).status,
    'match',
  );
});

test('elided rows', () => {
  const b = box('┌──┐', '│ x │', '│ l │', '├──┤', '│ 1 │', '│ · │', '│ 9 │', '└──┘');
  assert.equal(compareResult(b, one('1')).status, 'shown');
  const f = box('┌──┐', '│ x │', '│ l │', '├──┤', '│ 1 │', '│ 20 rows │', '└──┘');
  assert.equal(compareResult(f, one('1')).status, 'shown');
});

test('multi-row vs arrow', () => {
  const r = compareResult(arrow('1, a\n2, b'), {
    names: ['i', 's'],
    rows: [['1', 'a'], ['2', 'b']],
  });
  assert.equal(r.status, 'match');
});

test('multi-column box with lists (real parent-child example)', () => {
  const text = readFileSync(
    new URL('../../../docs/functions/parent-child.md', import.meta.url),
    'utf8',
  );
  const ex = extractExamples(text, 'parent-child')
    .flatMap((e) => e.statements)
    .find((s) => s.expected && s.expected.text.includes('spatial_children'));
  const p = parseBox(ex.expected.text);
  assert.deepEqual(p.names, ['index_children', 'spatial_children']);
  assert.deepEqual(p.types, ['uint64[]', 'uint64[]']);
  assert.equal(p.elided, false);
  assert.deepEqual(p.rows, [
    ['[2380, 2381, 2412, 2413]', '[2380, 2412, 2413, 2381, 2348, 2347, 2379]'],
  ]);
  const ok = compareResult(ex.expected, {
    names: ['a', 'b'],
    rows: [['[2380, 2381, 2412, 2413]', '[2380, 2412, 2413, 2381, 2348, 2347, 2379]']],
  });
  assert.equal(ok.status, 'match');
  const bad = compareResult(ex.expected, {
    names: ['a', 'b'],
    rows: [['[2380, 2381, 2412, 2413]', '[2380]']],
  });
  assert.equal(bad.status, 'differs');
  assert.match(bad.detail, /column 2/);
});

test('cellsMatch basics', () => {
  assert.equal(cellsMatch('a  b', 'a b'), true);
  assert.equal(cellsMatch('5', '5.2'), false);
  assert.equal(cellsMatch('abc', 'abd'), false);
});
