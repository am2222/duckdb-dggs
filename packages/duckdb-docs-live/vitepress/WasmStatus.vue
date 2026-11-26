<script setup>
// /wasm-status: runs every documented example on the site in DuckDB-WASM and
// reports per page. It uses the same extractor, page ids, runner, known-
// failures list and counting as the CI runner, one fresh engine per page (as
// CI uses one fresh database per page), so its totals equal CI's for the same
// build. The engine is imported on mount: this page is the one place it loads
// without a click.
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { withBase } from 'vitepress';
import { applyKnownFailures } from '../src/examples.mjs';
import { errorMessage } from '../src/sql.mjs';
import { STATUS_ORDER, countStatuses, runPage, summarizePages } from '../src/status.mjs';
import BuildNotice from './BuildNotice.vue';
import { liveState, useLiveSqlConfig } from './config.mjs';

const props = defineProps({
  // the createExamplesLoader() data: { pages: [{page, examples}], known: [{id, reason}] }
  data: { type: Object, required: true },
});

const config = useLiveSqlConfig();
const pages = computed(() => props.data.pages ?? []);
const known = computed(() => props.data.known ?? []);
const exampleById = computed(() => new Map(pages.value.flatMap((p) => p.examples.map((e) => [e.id, e]))));
const totalExamples = computed(() => pages.value.reduce((n, p) => n + p.examples.length, 0));

const mounted = ref(false);
const running = ref(false);
const finished = ref(false);
const current = ref('');
const raw = ref([]); // runExample results, before the known-failures list
const pageMs = ref({});
const info = ref(null);
const startError = ref('');
const elapsed = ref(null);
const runs = ref(0);

const applied = computed(() => applyKnownFailures(raw.value, known.value));
// "no such example" problems only make sense once every page has run
const problems = computed(() => (finished.value ? applied.value.problems : []));
const rows = computed(() => {
  const ran = new Map(summarizePages(pages.value, applied.value.results).map((s) => [s.page, s]));
  return pages.value.map((p) => ({
    page: p.page,
    examples: p.examples.length,
    ran: ran.has(p.page),
    ms: pageMs.value[p.page],
    results: ran.get(p.page)?.results ?? [],
    counts: ran.get(p.page)?.counts ?? countStatuses([]),
  }));
});
const totals = computed(() => countStatuses(applied.value.results));
const healthy = computed(() =>
  finished.value && totals.value.differs === 0 && totals.value.error === 0 && problems.value.length === 0);

const LABEL = {
  match: '✓ match', differs: '✗ differs', error: '⚠ error', skipped: '⏭ skipped', shown: '👁 shown', unreleased: '🕓 unreleased',
};

let wanted = 0;
let done = 0;
let unmounted = false;

async function runOnce(stale) {
  raw.value = [];
  pageMs.value = {};
  info.value = null;
  startError.value = '';
  finished.value = false;
  elapsed.value = null;
  const t0 = performance.now();
  const { getSession, buildUrlFromLocation } = await import('./engine.mjs');
  const buildUrl = buildUrlFromLocation();
  liveState.build = buildUrl;
  const published = !buildUrl;
  for (const p of pages.value) {
    if (stale()) return;
    current.value = p.page;
    const p0 = performance.now();
    let session;
    try {
      // a fresh engine per page: a crash on one page can't poison the others
      session = await getSession(config, buildUrl, { fresh: true });
    } catch (e) {
      startError.value = `could not start DuckDB-WASM: ${errorMessage(e)}`;
      session = { setupError: startError.value };
    }
    if (!info.value && session.info) info.value = session.info;
    try {
      const results = await runPage(session, p.examples, { published });
      if (stale()) return;
      raw.value = [...raw.value, ...results];
    } finally {
      if (session.close) await session.close();
    }
    pageMs.value = { ...pageMs.value, [p.page]: performance.now() - p0 };
  }
  current.value = '';
  elapsed.value = performance.now() - t0;
  finished.value = true;
  runs.value++;
}

async function runAll() {
  wanted++;
  if (running.value) return; // the loop below picks the new request up
  running.value = true;
  try {
    while (done < wanted && !unmounted) {
      const my = wanted;
      try {
        await runOnce(() => unmounted || my !== wanted);
      } catch (e) {
        startError.value = `could not start DuckDB-WASM: ${errorMessage(e)}`;
      }
      done = my;
    }
  } finally {
    running.value = false;
    current.value = '';
  }
}

onMounted(() => {
  mounted.value = true;
  runAll();
});
onBeforeUnmount(() => {
  unmounted = true;
});

// [Use published] in the build notice: rerun against the published extension.
watch(() => liveState.epoch, () => runAll());

const open = ref(new Set());
function toggle(page) {
  const next = new Set(open.value);
  if (next.has(page)) next.delete(page);
  else next.add(page);
  open.value = next;
}

const pageLink = (page) => withBase(`/${page === 'index' ? '' : page.replace(/(^|\/)index$/, '$1')}`);
const fmtMs = (ms) => (ms == null ? '' : ms >= 1000 ? `${(ms / 1000).toFixed(1)} s` : `${Math.round(ms)} ms`);
const nonMatch = (row) => row.results.filter((r) => r.status !== 'match');
const exampleName = (id) => {
  const ex = exampleById.value.get(id);
  return ex ? `${ex.heading} (line ${ex.line})` : id;
};
// VitePress's heading slug (@mdit-vue), so the link lands on the example's heading
const slugify = (str) => String(str).normalize('NFKD').replace(/[\u0300-\u036F]/g, '')
  .replace(/[\u0000-\u001f]/g, '').replace(/[\s~`!@#$%^&*()\-_+=[\]{}|\\;:"'“”‘’<>,.?/]+/g, '-')
  .replace(/-{2,}/g, '-').replace(/^-+|-+$/g, '').replace(/^(\d)/, '_$1').toLowerCase();
const anchor = (id) => {
  const ex = exampleById.value.get(id);
  return ex && ex.heading !== 'top' ? `#${slugify(ex.heading)}` : '';
};
const actualText = (a) =>
  a ? [a.names.join(' | '), ...a.rows.map((r) => r.join(' | '))].join('\n') : '';
const resultDetail = (r) => {
  if (r.detail) return r.detail;
  const bad = r.statements.find((s) => s.status !== 'match' && s.status !== 'shown' && s.detail);
  return bad?.detail ?? '';
};
// the statements worth showing: the failing one(s), or all of a shown example
const shownStatements = (r) => {
  const bad = r.statements.filter((s) => s.status !== 'match' && s.status !== 'shown');
  return bad.length ? bad : r.statements;
};
</script>

<template>
  <div class="wasm-status">
    <div class="wasm-status-head">
      <button type="button" class="live-sql-btn primary" :disabled="!mounted || running" @click="runAll">
        {{ running ? 'Running…' : '▶ Run all' }}
      </button>
      <span v-if="running && current" class="wasm-status-progress">
        {{ Object.keys(pageMs).length + 1 }} / {{ pages.length }}: <code>{{ current }}</code>
      </span>
      <span v-else-if="finished" class="wasm-status-progress">
        {{ applied.results.length }} examples on {{ pages.length }} pages in {{ fmtMs(elapsed) }}
      </span>
      <span v-else-if="!mounted" class="wasm-status-progress">
        {{ totalExamples }} examples on {{ pages.length }} pages (needs JavaScript to run)
      </span>
    </div>

    <dl v-if="info" class="wasm-status-info">
      <div><dt>Extension source</dt><dd><code>{{ info.source }}</code></dd></div>
      <div><dt>Extension version</dt><dd><code>{{ info.extension || '—' }}</code></dd></div>
      <div><dt>DuckDB</dt><dd><code>{{ info.duckdb || '—' }}</code></dd></div>
      <div><dt>Engine</dt><dd><code>@duckdb/duckdb-wasm@{{ info.engine }}</code></dd></div>
    </dl>

    <div v-if="startError" class="wasm-status-problems" role="alert">
      <p>{{ startError }}</p>
    </div>
    <div v-if="problems.length" class="wasm-status-problems" role="alert">
      <p v-for="(p, i) in problems" :key="i">problem: {{ p }}</p>
    </div>
    <p v-if="healthy" class="wasm-status-ok">
      No differences or errors<template v-if="totals.unreleased"> ({{ totals.unreleased }} use functions that are not in the published build yet)</template>.
    </p>

    <div class="wasm-status-table-wrap">
      <table class="wasm-status-table">
        <thead>
          <tr>
            <th>Page</th>
            <th v-for="s in STATUS_ORDER" :key="s" :class="['n', s]">
              <span class="icon" aria-hidden="true">{{ LABEL[s].split(' ')[0] }}</span>{{ s }}
            </th>
            <th class="n">Time</th>
          </tr>
        </thead>
        <tbody>
          <template v-for="row in rows" :key="row.page">
            <tr :class="{ pending: !row.ran }">
              <td class="page">
                <button
                  v-if="row.ran && nonMatch(row).length"
                  type="button"
                  class="wasm-status-toggle"
                  :aria-expanded="open.has(row.page)"
                  :aria-label="`${open.has(row.page) ? 'Hide' : 'Show'} the examples that don't match on ${row.page}`"
                  @click="toggle(row.page)"
                >{{ open.has(row.page) ? '▾' : '▸' }}</button>
                <a :href="pageLink(row.page)">{{ row.page }}</a>
              </td>
              <td v-for="s in STATUS_ORDER" :key="s" :class="['n', s, { zero: !row.counts[s] }]">
                {{ row.ran ? row.counts[s] : '·' }}
              </td>
              <td class="n ms">{{ fmtMs(row.ms) }}</td>
            </tr>
            <tr v-if="row.ran && open.has(row.page)" class="wasm-status-detail-row">
              <td :colspan="STATUS_ORDER.length + 2">
                <div v-for="r in nonMatch(row)" :key="r.id" class="wasm-status-example">
                  <div class="wasm-status-example-head">
                    <span class="live-sql-badge" :class="r.status === 'differs' || r.status === 'error' ? 'bad' : 'muted'">
                      {{ LABEL[r.status] }}
                    </span>
                    <a :href="pageLink(row.page) + anchor(r.id)">{{ exampleName(r.id) }}</a>
                    <code class="id">{{ r.id }}</code>
                  </div>
                  <pre v-if="resultDetail(r)" class="live-sql-detail" :class="r.status">{{ resultDetail(r) }}</pre>
                  <div v-for="(st, i) in shownStatements(r)" :key="i" class="wasm-status-stmt">
                    <pre class="sql">{{ st.sql }}</pre>
                    <div class="cmp">
                      <div v-if="st.expected">
                        <div class="label">expected</div>
                        <pre>{{ st.expected.text }}</pre>
                      </div>
                      <div v-if="st.actual">
                        <div class="label">actual</div>
                        <pre>{{ actualText(st.actual) }}</pre>
                      </div>
                    </div>
                  </div>
                </div>
              </td>
            </tr>
          </template>
        </tbody>
        <tfoot>
          <tr>
            <th>Total ({{ applied.results.length }} / {{ totalExamples }})</th>
            <th v-for="s in STATUS_ORDER" :key="s" :class="['n', s, { zero: !totals[s] }]">{{ totals[s] }}</th>
            <th class="n ms">{{ fmtMs(elapsed) }}</th>
          </tr>
        </tfoot>
      </table>
    </div>

    <Teleport v-if="mounted && liveState.build" to="body">
      <BuildNotice />
    </Teleport>
  </div>
</template>
