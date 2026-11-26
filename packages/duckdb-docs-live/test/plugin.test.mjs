import { test } from 'node:test';
import assert from 'node:assert/strict';
import MarkdownIt from 'markdown-it';
import { escapeAttr, liveSql, pageId } from '../vitepress/plugin.mjs';

test('escapeAttr escapes everything that could end or break an attribute', () => {
  assert.equal(escapeAttr(`a&b"c'd<e>f`), 'a&amp;b&quot;c&#39;d&lt;e&gt;f');
  assert.equal(escapeAttr('┌─ → plain'), '┌─ → plain');
});

test('pageId is the docs-root-relative path without .md (CI ids)', () => {
  assert.equal(pageId('functions/coverage-geodesy.md'), 'functions/coverage-geodesy');
  assert.equal(pageId('guide\\getting-started.md'), 'guide/getting-started');
  assert.equal(pageId('index.md'), 'index');
  assert.equal(pageId(undefined), '');
});

const PAGE = `---
title: Example page
outline: deep
---
# Title

## first_fn

\`\`\`sql
SELECT 'it''s' AS "q<1>"; -- & stuff
-- → it's
\`\`\`

Some prose.

\`\`\`sql
SELECT 1 AS one;
\`\`\`
\`\`\`
┌───────┐
│  one  │
│ int32 │
├───────┤
│     1 │
└───────┘
\`\`\`

\`\`\`sql
-- signature only: no expected value, not an example
INT first_fn()
\`\`\`
`;

// Decode the :example attribute the way Vue's template compiler does.
const decode = (s) =>
  s.replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');

function wrapped(html) {
  const re = /<LiveSql :example="([^"]*)">\n([\s\S]*?)<\/LiveSql>/g;
  return [...html.matchAll(re)].map((m) => ({ example: JSON.parse(decode(m[1])), inner: m[2] }));
}

// VitePress strips frontmatter before markdown-it tokenises the page
// (@mdit-vue/plugin-frontmatter overrides md.parse); mirror that.
function stripFrontmatter(md) {
  const parse = md.parse.bind(md);
  md.parse = (src, env) => parse(src.replace(/^---\n[\s\S]*?\n---\n/, ''), env);
  return md;
}

for (const [name, setup] of [['frontmatter stripped (VitePress)', stripFrontmatter], ['frontmatter kept', (md) => md]]) {
  test(`fences map to examples by line: ${name}`, () => {
    const md = setup(new MarkdownIt({ html: true })).use(liveSql);
    const html = md.render(PAGE, { relativePath: 'guide/example.md' });
    const w = wrapped(html);
    assert.equal(w.length, 2);

    const [a, b] = w;
    assert.equal(a.example.id, 'guide/example#first_fn/1');
    assert.equal(a.example.statements[0].sql, `SELECT 'it''s' AS "q<1>"; -- & stuff`);
    assert.equal(a.example.source, `SELECT 'it''s' AS "q<1>"; -- & stuff\n-- → it's`);
    assert.match(a.inner, /^<pre><code class="language-sql">/);
    assert.equal((a.inner.match(/<pre>/g) ?? []).length, 1);

    // the output box is inside the second wrapper, with its SQL
    assert.equal(b.example.id, 'guide/example#first_fn/2');
    assert.equal(b.example.statements[0].expected.kind, 'box');
    assert.equal((b.inner.match(/<pre>/g) ?? []).length, 2);
    assert.match(b.inner, /┌───────┐/);

    // the signature fence and prose are untouched, outside any wrapper
    assert.match(html, /<p>Some prose.<\/p>/);
    assert.match(html, /INT first_fn\(\)/);
    assert.doesNotMatch(w.map((x) => x.inner).join(''), /INT first_fn/);
  });
}

test('pages without relativePath or examples are left alone', () => {
  const md = new MarkdownIt().use(liveSql);
  const plain = new MarkdownIt();
  assert.equal(md.render(PAGE, {}), plain.render(PAGE, {}));
  const noExamples = '# T\n\n```sql\nSELECT 1;\n```\n';
  assert.equal(md.render(noExamples, { relativePath: 'x.md' }), plain.render(noExamples, {}));
});
