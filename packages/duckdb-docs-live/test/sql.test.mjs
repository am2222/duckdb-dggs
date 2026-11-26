import test from 'node:test';
import assert from 'node:assert/strict';
import { render, numericallyEqual, stripSemicolon, isEngineCrash, runQuery } from '../src/sql.mjs';

test('render', () => {
  assert.equal(render(null), 'NULL');
  assert.equal(render(''), '(empty)');
  assert.equal(render(12n), '12');
});

test('numericallyEqual', () => {
  assert.equal(numericallyEqual('1.0000000001', '1'), true);
  assert.equal(numericallyEqual('1.1', '1'), false);
  assert.equal(numericallyEqual('abc', 'abc'), false);
});

test('stripSemicolon', () => {
  assert.equal(stripSemicolon(' SELECT 1; '), 'SELECT 1');
});

test('isEngineCrash', () => {
  assert.equal(isEngineCrash('Program terminated with exit(1)'), true);
});

function fakeConn() {
  const seen = [];
  return {
    seen,
    async query(sql) {
      seen.push(sql);
      return { schema: { fields: [{ name: 'a' }] }, toArray: () => [{ toJSON: () => ({ a: '1' }) }] };
    },
  };
}

test('runQuery wraps SELECT and returns names', async () => {
  const conn = fakeConn();
  const r = await runQuery(conn, 'SELECT 1');
  assert.ok(conn.seen[0].startsWith('SELECT COLUMNS(*)::VARCHAR FROM ('));
  assert.deepEqual(r, { cols: 1, names: ['a'], rows: [['1']] });
});

test('runQuery does not wrap PRAGMA', async () => {
  const conn = fakeConn();
  await runQuery(conn, 'PRAGMA version');
  assert.equal(conn.seen[0], 'PRAGMA version');
});
