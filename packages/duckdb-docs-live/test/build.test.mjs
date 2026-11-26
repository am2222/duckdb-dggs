import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildParam, checkBuildUrl } from '../vitepress/build.mjs';

test('checkBuildUrl accepts only *.duckdb_extension.wasm', () => {
  assert.equal(checkBuildUrl('http://localhost:8099/duck_dggs.duckdb_extension.wasm'), null);
  assert.equal(checkBuildUrl('https://h/x.duckdb_extension.wasm?v=2#a'), null);
  assert.match(checkBuildUrl('http://localhost:9/nope.wasm'), /must end in \.duckdb_extension\.wasm/);
  assert.match(checkBuildUrl('https://h/x.duckdb_extension.wasm.gz'), /must end in/);
  assert.match(checkBuildUrl(''), /must end in/);
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
