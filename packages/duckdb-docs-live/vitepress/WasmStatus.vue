<script setup>
// /wasm-status: runs every documented example on the site in DuckDB-WASM and
// reports per page. It uses the same extractor, page ids, runner, known-
// failures list and counting as the CI runner, one fresh engine per page (as
// CI uses one fresh database per page), so its totals equal CI's for the same
// build. The engine is imported on mount: this page is the one place it loads
// without a click.
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
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
const lastRun = ref(null);
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

// The overall state shown in the summary card.
const state = computed(() => {
  if (startError.value) return 'blocked';
  if (!running.value && !finished.value) return 'idle';
  if (running.value && !finished.value) return 'running';
  if (totals.value.differs || totals.value.error) return 'fail';
  if (problems.value.length) return 'attention';
  return 'pass';
});
const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;
const headline = computed(() => {
  const t = totals.value;
  switch (state.value) {
    case 'running':
      return `Running page ${Math.min(Object.keys(pageMs.value).length + 1, pages.value.length)} of ${pages.value.length}…`;
    case 'blocked': return 'Could not start';
    case 'fail': {
      const parts = [];
      if (t.differs) parts.push(`${plural(t.differs, 'example differs', 'examples differ')}`);
      if (t.error) parts.push(plural(t.error, 'error', 'errors'));
      return parts.join(', ');
    }
    case 'attention': return `Needs attention: ${plural(problems.value.length, 'problem', 'problems')} in the known-failures list`;
    case 'pass': return `All ${t.match} compared examples match`;
    default: return 'Waiting to run';
  }
});
const stateIcon = computed(() => ({
  running: '⏳', blocked: '⛔', fail: '✗', attention: '⚠', pass: '✓', idle: '·',
}[state.value]));
const barTotal = computed(() => Object.values(totals.value).reduce((a, b) => a + b, 0));
const progress = computed(() => (pages.value.length ? Object.keys(pageMs.value).length / pages.value.length : 0));
const firstFailure = computed(() => {
  for (const row of rows.value) {
    const bad = row.results.find((r) => r.status === 'differs' || r.status === 'error');
    if (bad) return { page: row.page, id: bad.id };
  }
  return null;
});
async function jumpToFirstFailure() {
  const f = firstFailure.value;
  if (!f) return;
  open.value = new Set([...open.value, f.page]);
  await nextTick();
  document.getElementById(`wasm-status-row-${f.page}`)?.scrollIntoView({ block: 'center', behavior: 'smooth' });
}
const metaLine = computed(() => {
  if (!info.value) return '';
  const parts = [];
  if (info.value.source === 'community') {
    parts.push(`Published extension${info.value.extension ? ` ${info.value.extension}` : ''}`);
  } else {
    parts.push(`Local build ${info.value.source}`);
  }
  if (finished.value && lastRun.value) {
    parts.push(`last run ${lastRun.value.toLocaleTimeString()}, took ${fmtMs(elapsed.value)}`);
  }
  return parts.join(' · ');
});

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
  lastRun.value = new Date();
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
      <span v-if="!mounted" class="wasm-status-progress">
        {{ totalExamples }} examples on {{ pages.length }} pages (needs JavaScript to run)
      </span>
    </div>

    <p class="wasm-status-sr" role="status">{{ ['pass', 'fail', 'attention', 'blocked'].includes(state) ? headline : '' }}</p>

    <section v-if="state !== 'idle'" :class="['wasm-status-card', state]" aria-label="Overall result">
      <div class="wasm-status-card-main">
        <p class="wasm-status-state">
          <span class="wasm-status-state-icon" aria-hidden="true">{{ stateIcon }}</span>
          <span>{{ headline }}</span>
        </p>
        <button
          v-if="state === 'fail' && firstFailure"
          type="button"
          class="wasm-status-jump"
          @click="jumpToFirstFailure"
        >Jump to first failure</button>
      </div>
      <div v-if="state === 'running'" class="wasm-status-meter" role="progressbar" aria-label="Pages run"
        :aria-valuemin="0" :aria-valuemax="pages.length" :aria-valuenow="Object.keys(pageMs).length">
        <div class="wasm-status-meter-fill" :style="{ width: `${progress * 100}%` }"></div>
      </div>
      <p v-if="state === 'blocked'" class="wasm-status-card-detail">{{ startError }}</p>
      <ul v-if="problems.length" class="wasm-status-card-problems">
        <li v-for="(p, i) in problems" :key="i">{{ p }}</li>
      </ul>
      <template v-if="barTotal">
        <div class="wasm-status-bar" role="img"
          :aria-label="STATUS_ORDER.filter((s) => totals[s]).map((s) => `${totals[s]} ${s}`).join(', ')">
          <span v-for="s in STATUS_ORDER" :key="s" v-show="totals[s]" :class="['seg', s]"
            :style="{ flexGrow: totals[s] }" :title="`${totals[s]} ${s}`"></span>
        </div>
        <ul class="wasm-status-chips">
          <li v-for="s in STATUS_ORDER" :key="s" :class="['chip', s, { zero: !totals[s] }]">
            <span class="dot" aria-hidden="true"></span><b>{{ totals[s] }}</b> {{ s }}
          </li>
        </ul>
      </template>
      <p v-if="metaLine" class="wasm-status-card-meta">{{ metaLine }}</p>
    </section>

    <dl v-if="info" class="wasm-status-info">
      <div><dt>Extension source</dt><dd><code>{{ info.source }}</code></dd></div>
      <div><dt>Extension version</dt><dd><code>{{ info.extension || '—' }}</code></dd></div>
      <div><dt>DuckDB</dt><dd><code>{{ info.duckdb || '—' }}</code></dd></div>
      <div><dt>Engine</dt><dd><code>@duckdb/duckdb-wasm@{{ info.engine }}</code></dd></div>
    </dl>

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
            <tr :id="`wasm-status-row-${row.page}`" :class="{ pending: !row.ran }">
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
