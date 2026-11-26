// VitePress data-loader factory for the /wasm-status page. Runs in Node at
// build time (and in the dev server): reads every doc page the CI runner reads,
// extracts its examples with the same extractor and page ids, and adds the
// known-failures list, so the page and CI judge the same examples.
//
//   // docs/.vitepress/examples.data.mts
//   import { createExamplesLoader } from 'duckdb-docs-live/vitepress/examples-loader'
//   export default createExamplesLoader('**/*.md', { exclude: ['superpowers/**'] })
import fs from 'node:fs';
import path from 'node:path';
import { extractExamples } from '../src/extract.mjs';
import { isSkippedDocPath } from '../src/status.mjs';

const toPosix = (p) => p.split(path.sep).join('/');

export function createExamplesLoader(pattern, opts = {}) {
  const config = globalThis.VITEPRESS_CONFIG;
  if (!config) {
    throw new Error('createExamplesLoader() must run inside a VitePress process (no resolved config)');
  }
  const srcDir = toPosix(config.srcDir);
  const knownFile = toPosix(path.join(config.srcDir, opts.knownFailures ?? 'wasm-known-failures.json'));
  const patterns = Array.isArray(pattern) ? pattern : [pattern];
  const watch = [
    ...patterns.map((p) => `${srcDir}/${p}`),
    ...(opts.exclude ?? []).map((p) => `!${srcDir}/${p}`),
    knownFile,
  ];

  return {
    watch,
    async load(files) {
      const pages = [];
      const mdFiles = files
        .map((f) => toPosix(path.relative(config.srcDir, f)))
        .filter((rel) => rel.endsWith('.md') && !rel.startsWith('../') && !isSkippedDocPath(rel))
        .sort();
      for (const rel of mdFiles) {
        const page = rel.replace(/\.md$/, '');
        const examples = extractExamples(fs.readFileSync(path.join(config.srcDir, rel), 'utf8'), page);
        if (examples.length) pages.push({ page, examples });
      }
      let known = [];
      if (fs.existsSync(knownFile)) known = JSON.parse(fs.readFileSync(knownFile, 'utf8'));
      return { pages, known };
    },
  };
}

export default createExamplesLoader;
