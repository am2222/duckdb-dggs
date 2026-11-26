// Shared by the CI runner and the /wasm-status page, so both pick the same
// pages and count the same way. Plain ESM, no dependencies, no Node/DOM APIs.
import { runExample } from './examples.mjs';

export const STATUS_ORDER = ['match', 'differs', 'error', 'skipped', 'shown', 'unreleased'];

// Directories never searched for doc pages (any path segment).
export const DOC_SKIP_DIRS = new Set(['node_modules', '.vitepress', 'superpowers']);

// `rel` is a docs-root-relative path.
export function isSkippedDocPath(rel) {
  return String(rel).split(/[\\/]/).some((seg) => DOC_SKIP_DIRS.has(seg));
}

export function countStatuses(results) {
  const n = Object.fromEntries(STATUS_ORDER.map((s) => [s, 0]));
  for (const r of results) n[r.status] = (n[r.status] ?? 0) + 1;
  return n;
}

// Runs one page's examples in `session` (one fresh database per page). A
// session whose setup failed reports that error for every example, as CI does.
export async function runPage(session, examples, opts = {}) {
  if (session.setupError) {
    return examples.map((ex) => ({
      id: ex.id, page: ex.page, status: 'error', detail: session.setupError, statements: [],
    }));
  }
  const results = [];
  for (const ex of examples) results.push(await runExample(session, ex, opts));
  return results;
}

// [{page, examples}] + results → [{page, results, counts}] in page order.
export function summarizePages(pages, results) {
  const byPage = new Map(pages.map((p) => [p.page, []]));
  for (const r of results) {
    if (!byPage.has(r.page)) byPage.set(r.page, []);
    byPage.get(r.page).push(r);
  }
  return [...byPage].map(([page, rs]) => ({ page, results: rs, counts: countStatuses(rs) }));
}
