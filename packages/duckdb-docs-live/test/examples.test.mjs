import test from 'node:test';
import assert from 'node:assert/strict';
import { runExample, applyKnownFailures } from '../src/examples.mjs';

// answers: SQL -> array of rows (each row an array of strings) | Error
function fakeSession(answers, { ensure } = {}) {
  const s = {
    dead: false,
    connects: 0,
    closed: 0,
    async connect() {
      s.connects++;
      return {
        async query(sql) {
          const m = /^SELECT COLUMNS\(\*\)::VARCHAR FROM \(([\s\S]*)\n\) AS __sqllogictest$/.exec(sql);
          const key = m ? m[1] : sql;
          const a = answers[key];
          if (a === undefined) throw new Error(`unexpected sql: ${key}`);
          if (a instanceof Error) throw a;
          return {
            schema: { fields: [{ name: 'x' }] },
            toArray: () => a.map((r) => ({ toJSON: () => ({ x: r[0] }) })),
          };
        },
        async close() {
          s.closed++;
          throw new Error('close failed'); // must be ignored
        },
      };
    },
    async ensure(n) {
      if (ensure) await ensure(n);
    },
  };
  return s;
}

const ex = (statements, extra = {}) => ({
  id: 'p-1', page: 'p', heading: 'h', line: 1, mode: 'compare', needs: [], statements, ...extra,
});
const arrow = (text) => ({ kind: 'arrow', text });

test('match', async () => {
  const s = fakeSession({ 'SELECT 1': [['1']] });
  const r = await runExample(s, ex([{ sql: 'SELECT 1;', expected: arrow('1') }]), { published: false });
  assert.equal(r.status, 'match');
  assert.equal(r.statements.length, 1);
  assert.equal(typeof r.statements[0].ms, 'number');
  assert.equal(s.closed, 1);
});

test('differs', async () => {
  const s = fakeSession({ 'SELECT 1': [['2']] });
  const r = await runExample(s, ex([{ sql: 'SELECT 1', expected: arrow('1') }]), { published: false });
  assert.equal(r.status, 'differs');
});

test('setup statement error -> error', async () => {
  const s = fakeSession({ 'CREATE TABLE t(a int)': new Error('Parser Error: boom'), 'SELECT 1': [['1']] });
  const r = await runExample(s, ex([
    { sql: 'CREATE TABLE t(a int)', expected: null },
    { sql: 'SELECT 1', expected: arrow('1') },
  ]), { published: false });
  assert.equal(r.status, 'error');
  assert.equal(r.statements.length, 1);
  assert.match(r.statements[0].detail, /boom/);
});

test('comment-only statements are skipped', async () => {
  const s = fakeSession({ 'SELECT 1': [['1']] });
  const r = await runExample(s, ex([
    { sql: '-- just a note', expected: null },
    { sql: 'SELECT 1', expected: arrow('1') },
  ]), { published: false });
  assert.equal(r.statements.length, 1);
  assert.equal(r.status, 'match');
});

test('run mode shows without comparing', async () => {
  const s = fakeSession({ 'SELECT 1': [['9']] });
  const r = await runExample(s, ex([{ sql: 'SELECT 1', expected: arrow('1') }], { mode: 'run' }), { published: false });
  assert.equal(r.status, 'shown');
  assert.deepEqual(r.statements[0].actual.rows, [['9']]);
});

test('norun is skipped without connecting', async () => {
  const s = fakeSession({});
  const r = await runExample(s, ex([{ sql: 'SELECT 1', expected: null }], { mode: 'norun' }), { published: false });
  assert.equal(r.status, 'skipped');
  assert.equal(s.connects, 0);
});

test('unreleased only when published', async () => {
  const err = new Error('Catalog Error: Scalar Function with name foo does not exist');
  const e = ex([{ sql: 'SELECT foo()', expected: null }]);
  assert.equal((await runExample(fakeSession({ 'SELECT foo()': err }), e, { published: true })).status, 'unreleased');
  assert.equal((await runExample(fakeSession({ 'SELECT foo()': err }), e, { published: false })).status, 'error');
});

test('only missing functions or macros are unreleased', async () => {
  const cases = [
    ['Catalog Error: Table with name x does not exist!', 'error'],
    ['Binder Error: Referenced column y does not exist', 'error'],
    ['Catalog Error: Macro Function with name m does not exist', 'unreleased'],
    ['Binder Error: No function matches the given name and argument types', 'unreleased'],
  ];
  const e = ex([{ sql: 'SELECT q', expected: null }]);
  for (const [msg, want] of cases) {
    const r = await runExample(fakeSession({ 'SELECT q': new Error(msg) }), e, { published: true });
    assert.equal(r.status, want, msg);
  }
});

test('crash marks the rest', async () => {
  const s = fakeSession({ 'SELECT 1': new Error('Program terminated with exit(1)') });
  const e = ex([{ sql: 'SELECT 1', expected: null }]);
  assert.equal((await runExample(s, e, { published: false })).status, 'error');
  assert.equal(s.dead, true);
  const r2 = await runExample(s, e, { published: false });
  assert.equal(r2.status, 'error');
  assert.equal(r2.detail, 'engine stopped');
  assert.equal(s.connects, 1);
});

test('needs spatial fails -> skipped', async () => {
  const s = fakeSession({}, { ensure: async () => { throw new Error('no network'); } });
  const r = await runExample(s, ex([{ sql: 'SELECT 1', expected: null }], { needs: ['spatial'] }), { published: false });
  assert.equal(r.status, 'skipped');
  assert.equal(r.detail, 'needs spatial: no network');
  assert.equal(s.connects, 0);
});

test('known failure', () => {
  const res = (id, status) => ({ id, page: 'p', status, statements: [] });
  const out = applyKnownFailures(
    [res('a', 'differs'), res('b', 'error'), res('c', 'match'), res('d', 'match')],
    [{ id: 'a', reason: 'r1' }, { id: 'b', reason: 'r2' }, { id: 'c', reason: 'r3' }, { id: 'zzz', reason: 'r4' }],
  );
  assert.equal(out.results[0].status, 'skipped');
  assert.equal(out.results[0].detail, 'known failure: r1');
  assert.equal(out.results[1].status, 'skipped');
  assert.equal(out.results[2].status, 'match');
  assert.equal(out.results[3].status, 'match');
  assert.deepEqual(out.problems, [
    'c passes now — remove it from docs/wasm-known-failures.json',
    'zzz is in the known-failures list but no such example exists',
  ]);
});
