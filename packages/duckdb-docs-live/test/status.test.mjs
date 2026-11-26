import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  STATUS_ORDER, DOC_SKIP_DIRS, isSkippedDocPath, countStatuses, runPage, summarizePages,
} from '../src/status.mjs';
import { createExamplesLoader } from '../vitepress/examples.data.mjs';

test('status order and skip dirs match the CI runner', () => {
  assert.deepEqual(STATUS_ORDER, ['match', 'differs', 'error', 'skipped', 'shown', 'unreleased']);
  assert.deepEqual([...DOC_SKIP_DIRS].sort(), ['.vitepress', 'node_modules', 'superpowers']);
});

test('isSkippedDocPath skips any path segment in the skip set', () => {
  assert.equal(isSkippedDocPath('functions/utility.md'), false);
  assert.equal(isSkippedDocPath('index.md'), false);
  assert.equal(isSkippedDocPath('superpowers/plans/x.md'), true);
  assert.equal(isSkippedDocPath('a/node_modules/b.md'), true);
  assert.equal(isSkippedDocPath('.vitepress/theme/x.md'), true);
  assert.equal(isSkippedDocPath('guide\\superpowers\\x.md'), true);
  assert.equal(isSkippedDocPath('superpowersx/y.md'), false);
});

test('countStatuses counts every status, zero-filled', () => {
  const n = countStatuses([{ status: 'match' }, { status: 'match' }, { status: 'skipped' }]);
  assert.deepEqual(n, { match: 2, differs: 0, error: 0, skipped: 1, shown: 0, unreleased: 0 });
});

const ex = (id, page = 'p') => ({
  id, page, heading: 'h', line: 1, mode: 'compare', needs: [],
  statements: [{ sql: 'SELECT 1', expected: { kind: 'arrow', text: '1' } }],
});

test('runPage reports a setup error once per example, like CI', async () => {
  const session = { setupError: 'could not load build x: boom', dead: false };
  const r = await runPage(session, [ex('p#a/1'), ex('p#a/2')], { published: false });
  assert.deepEqual(r.map((x) => [x.id, x.page, x.status, x.detail]), [
    ['p#a/1', 'p', 'error', 'could not load build x: boom'],
    ['p#a/2', 'p', 'error', 'could not load build x: boom'],
  ]);
});

test('runPage runs every example through runExample with the options', async () => {
  const seen = [];
  const session = {
    dead: false,
    async connect() {
      return {
        async query(sql) {
          seen.push(sql);
          throw new Error('Catalog Error: Scalar Function with name foo does not exist!');
        },
        async close() {},
      };
    },
  };
  const r = await runPage(session, [ex('p#a/1')], { published: true });
  assert.equal(r[0].status, 'unreleased');
  assert.ok(seen.length >= 1); // runQuery may retry unwrapped
  const r2 = await runPage(session, [ex('p#a/1')], { published: false });
  assert.equal(r2[0].status, 'error');
});

test('summarizePages groups results by page in page order, with counts', () => {
  const pages = [{ page: 'b', examples: [] }, { page: 'a', examples: [] }];
  const results = [
    { id: 'a#x/1', page: 'a', status: 'match' },
    { id: 'b#x/1', page: 'b', status: 'differs' },
    { id: 'b#x/2', page: 'b', status: 'match' },
  ];
  const s = summarizePages(pages, results);
  assert.deepEqual(s.map((p) => p.page), ['b', 'a']);
  assert.equal(s[0].counts.differs, 1);
  assert.equal(s[0].counts.match, 1);
  assert.equal(s[0].results.length, 2);
  assert.equal(s[1].counts.match, 1);
});

function fixtureDocs() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ddl-loader-'));
  const write = (rel, text) => {
    fs.mkdirSync(path.dirname(path.join(dir, rel)), { recursive: true });
    fs.writeFileSync(path.join(dir, rel), text);
  };
  const fence = (q, out) => `## f\n\n\`\`\`sql\n${q}\n-- → ${out}\n\`\`\`\n`;
  write('index.md', '# Home\n');
  write('guide/start.md', `---\ntitle: Start\n---\n# Start\n\n${fence('SELECT 1;', '1')}`);
  write('functions/u.md', `# U\n\n${fence('SELECT 2;', '2')}\n${fence('SELECT 3;', '3')}`);
  write('superpowers/plan.md', `# P\n\n${fence('SELECT 4;', '4')}`);
  write('wasm-known-failures.json', JSON.stringify([{ id: 'functions/u#f/2', reason: 'r' }]));
  return dir;
}

test('createExamplesLoader: watch is srcDir-relative and load() extracts pages like CI', async () => {
  const dir = fixtureDocs();
  const prev = globalThis.VITEPRESS_CONFIG;
  globalThis.VITEPRESS_CONFIG = { srcDir: dir };
  try {
    const loader = createExamplesLoader('**/*.md', { exclude: ['superpowers/**'] });
    const norm = (p) => p.split(path.sep).join('/');
    assert.ok(loader.watch.includes(`${norm(dir)}/**/*.md`));
    assert.ok(loader.watch.includes(`!${norm(dir)}/superpowers/**`));
    assert.ok(loader.watch.includes(`${norm(dir)}/wasm-known-failures.json`));
    // VitePress passes the files its glob matched; the loader still filters
    // the skip dirs itself, so a forgotten exclude can't make it disagree with CI.
    const files = ['index.md', 'guide/start.md', 'functions/u.md', 'superpowers/plan.md',
      'wasm-known-failures.json'].map((f) => path.join(dir, f));
    const data = await loader.load(files);
    assert.deepEqual(data.pages.map((p) => p.page), ['functions/u', 'guide/start']);
    assert.deepEqual(data.pages[0].examples.map((e) => e.id), ['functions/u#f/1', 'functions/u#f/2']);
    assert.deepEqual(data.pages[1].examples.map((e) => e.id), ['guide/start#f/1']);
    assert.deepEqual(data.known, [{ id: 'functions/u#f/2', reason: 'r' }]);
  } finally {
    globalThis.VITEPRESS_CONFIG = prev;
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('createExamplesLoader: a missing known-failures file gives an empty list', async () => {
  const dir = fixtureDocs();
  fs.rmSync(path.join(dir, 'wasm-known-failures.json'));
  const prev = globalThis.VITEPRESS_CONFIG;
  globalThis.VITEPRESS_CONFIG = { srcDir: dir };
  try {
    const data = await createExamplesLoader('**/*.md').load([path.join(dir, 'index.md')]);
    assert.deepEqual(data, { pages: [], known: [] });
  } finally {
    globalThis.VITEPRESS_CONFIG = prev;
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('createExamplesLoader: throws outside a VitePress process', () => {
  const prev = globalThis.VITEPRESS_CONFIG;
  delete globalThis.VITEPRESS_CONFIG;
  try {
    assert.throws(() => createExamplesLoader('**/*.md'), /VitePress/);
  } finally {
    globalThis.VITEPRESS_CONFIG = prev;
  }
});
