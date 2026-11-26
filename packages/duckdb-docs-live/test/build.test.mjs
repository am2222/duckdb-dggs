import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildParam, checkBuildUrl } from '../vitepress/build.mjs';

const SITE = 'https://am2222.github.io/duckdb-dggs/functions/parent-child.html';

test('checkBuildUrl accepts only *.duckdb_extension.wasm', () => {
  assert.equal(checkBuildUrl('http://localhost:8099/duck_dggs.duckdb_extension.wasm', SITE), null);
  assert.equal(checkBuildUrl('http://localhost:8099/x.duckdb_extension.wasm?v=2#a', SITE), null);
  assert.match(checkBuildUrl('http://localhost:9/nope.wasm', SITE), /must end in \.duckdb_extension\.wasm/);
  assert.match(checkBuildUrl('http://localhost:9/x.duckdb_extension.wasm.gz', SITE), /must end in/);
  assert.match(checkBuildUrl('', SITE), /must end in/);
});

test('checkBuildUrl accepts only local hosts or the page origin', () => {
  const ok = [
    'http://localhost:8099/duck_dggs.duckdb_extension.wasm',
    'http://127.0.0.1:8099/duck_dggs.duckdb_extension.wasm',
    'http://[::1]:8099/duck_dggs.duckdb_extension.wasm',
    'https://am2222.github.io/duckdb-dggs/builds/duck_dggs.duckdb_extension.wasm',
    '/duckdb-dggs/builds/duck_dggs.duckdb_extension.wasm', // relative: this origin
    'builds/duck_dggs.duckdb_extension.wasm',
  ];
  for (const u of ok) assert.equal(checkBuildUrl(u, SITE), null, u);
  const bad = [
    'https://evil.example/duck_dggs.duckdb_extension.wasm',
    'https://localhost.evil.example/duck_dggs.duckdb_extension.wasm',
    'http://am2222.github.io/duckdb-dggs/x.duckdb_extension.wasm', // other scheme = other origin
    '//evil.example/duck_dggs.duckdb_extension.wasm',
  ];
  for (const u of bad) assert.match(checkBuildUrl(u, SITE), /only local builds \(localhost\) or this site's own files/, u);
  // a local dev site may load from its own origin and from other local ports
  assert.equal(checkBuildUrl('/x.duckdb_extension.wasm', 'http://localhost:5173/p.html'), null);
  assert.match(checkBuildUrl('http://h:1/x.duckdb_extension.wasm', 'http://localhost:5173/p.html'), /only local builds/);
});

test('buildParam reads ?build= and #build=; empty means clear; none is null', () => {
  const url = 'http://localhost:8099/duck_dggs.duckdb_extension.wasm';
  assert.equal(buildParam(`?build=${url}`, ''), url);
  assert.equal(buildParam(`?build=${encodeURIComponent(url)}`, ''), url);
  assert.equal(buildParam('', `#build=${url}`), url);
  assert.equal(buildParam('?other=1', `#build=${url}`), url);
  assert.equal(buildParam('?build=', ''), '');
  assert.equal(buildParam('', '#build='), '');
  assert.equal(buildParam('', '#some-heading'), null);
  assert.equal(buildParam('', ''), null);
  // the query wins when both are present
  assert.equal(buildParam('?build=a.duckdb_extension.wasm', '#build=b.duckdb_extension.wasm'), 'a.duckdb_extension.wasm');
});
