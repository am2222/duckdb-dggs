<script setup>
// A documented SQL example that can be run in the browser. The default slot is
// the server-rendered code block and output, so without JavaScript the page is
// unchanged. The engine is imported on the first Run, never before.
import { computed, getCurrentInstance, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { detectNeeds, splitStatements } from '../src/extract.mjs';
import { applyKnownFailures, runExample } from '../src/examples.mjs';
import { errorMessage } from '../src/sql.mjs';
import BuildNotice from './BuildNotice.vue';
import {
  dismissBanner, liveState, registerInstance, showBanner, unregisterInstance, useLiveSqlConfig,
} from './config.mjs';

const props = defineProps({
  example: { type: Object, required: true },
});

const config = useLiveSqlConfig();
const uid = getCurrentInstance().uid;
const original = props.example.source ?? props.example.statements.map((s) => s.sql).join('\n\n');
const MAX_ROWS = 200;

const mounted = ref(false);
const active = ref(false); // after the first Run the SQL is editable
const sql = ref(original);
const running = ref(false);
const phase = ref('');
const result = ref(null);
const ranEdited = ref(false);
const timing = ref(null);
const textarea = ref(null);

const edited = computed(() => sql.value !== original);

onMounted(() => {
  mounted.value = true;
  registerInstance(uid);
});

// The banner belongs to the page: it goes when its owner does (SPA navigation).
onBeforeUnmount(() => unregisterInstance(uid));

// The first LiveSql on the page renders the build-override notice.
const noticeOwner = computed(() => liveState.instances[0] === uid);

// Results from a source the reader has left are no longer meaningful.
watch(() => liveState.epoch, () => {
  result.value = null;
  timing.value = null;
});

const BADGES = {
  match: { icon: '✓', text: 'matches the documented output', cls: 'ok' },
  differs: { icon: '✗', text: 'differs from the documented output', cls: 'bad' },
  error: { icon: '⚠', text: 'error', cls: 'bad' },
  skipped: { icon: '⏭', text: 'skipped', cls: 'muted' },
  unreleased: { icon: '🕓', text: 'not in the published build yet', cls: 'muted' },
};

// The known-failures list, applied exactly as CI and /wasm-status apply it: a
// listed example that differs or errors is a known issue, not a ✗; one that
// matches again shows ✓ (and /wasm-status and CI list it as a problem).
const known = (config.knownFailures ?? []).find((k) => k.id === props.example.id) ?? null;

const badge = computed(() => {
  if (!result.value || ranEdited.value) return null;
  if (known) {
    const [judged] = applyKnownFailures([result.value], [known]).results;
    if (judged.status === 'skipped' && result.value.status !== 'skipped') {
      return { icon: 'ⓘ', text: `known issue: ${known.reason}`, cls: 'muted known' };
    }
  }
  return BADGES[result.value.status] ?? null;
});

const detail = computed(() => {
  const r = result.value;
  if (!r) return '';
  if (r.detail) return r.detail;
  const bad = r.statements.find((s) => s.status !== 'match' && s.status !== 'shown' && s.detail);
  return bad?.detail ?? '';
});

const tables = computed(() => {
  const r = result.value;
  if (!r) return [];
  return r.statements
    .filter((s) => s.actual && s.actual.names.length > 0)
    // LOAD, SET, INSTALL, ... return an empty "Success" column: nothing to show
    .filter((s) => !(s.actual.rows.length === 0 && s.actual.names.join() === 'Success'))
    .map((s) => ({
      sql: s.sql,
      names: s.actual.names,
      rows: s.actual.rows.slice(0, MAX_ROWS),
      more: Math.max(0, s.actual.rows.length - MAX_ROWS),
    }));
});

const isNumber = (v) => /^-?\d+(\.\d+)?(e[-+]?\d+)?$/i.test(v);

const fmtMs = (ms) => (ms >= 1000 ? `${(ms / 1000).toFixed(1)} s` : `${Math.round(ms)} ms`);

// Edited SQL runs as shown (no expected values). Built directly, not by
// re-parsing markdown, so a ``` typed in the editor can't cut it short.
function editedExample(text) {
  const statements = splitStatements(text.split(/\r?\n/));
  return {
    id: props.example.id,
    page: props.example.page,
    heading: props.example.heading,
    line: props.example.line,
    mode: 'run',
    statements,
    needs: detectNeeds(statements),
  };
}

function autosize() {
  const el = textarea.value;
  if (!el) return;
  el.style.height = 'auto';
  el.style.height = `${el.scrollHeight + 2}px`;
}

async function run() {
  if (running.value) return;
  running.value = true;
  const isEdited = edited.value;
  const t0 = performance.now();
  let queryMs = 0;
  try {
    phase.value = 'Starting DuckDB…';
    const { getSession, buildUrlFromLocation } = await import('./engine.mjs');
    liveState.engineLoaded = true;
    const buildUrl = buildUrlFromLocation();
    liveState.build = buildUrl;
    const session = await getSession(config, buildUrl);
    if (session.setupError) {
      showBanner(session.setupError, uid);
      result.value = { id: props.example.id, page: props.example.page, status: 'error', detail: session.setupError, statements: [] };
      ranEdited.value = isEdited;
      return;
    }
    phase.value = 'Running…';
    const ex = isEdited ? editedExample(sql.value) : props.example;
    const r = await runExample(session, ex, { published: !buildUrl });
    queryMs = r.statements.reduce((n, s) => n + (s.ms ?? 0), 0);
    result.value = r;
    ranEdited.value = isEdited;
  } catch (e) {
    const message = `could not start DuckDB-WASM: ${errorMessage(e)}`;
    showBanner(message, uid);
    result.value = { id: props.example.id, page: props.example.page, status: 'error', detail: message, statements: [] };
    ranEdited.value = isEdited;
  } finally {
    const total = performance.now() - t0;
    const ran = (result.value?.statements.length ?? 0) > 0;
    timing.value = ran ? { query: queryMs, total, boot: total - queryMs > 500 } : null;
    running.value = false;
    phase.value = '';
    active.value = true;
    nextTick(autosize);
  }
}

function reset() {
  sql.value = original;
  nextTick(autosize);
  run();
}

function onKeydown(e) {
  if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
    e.preventDefault();
    run();
  }
}
</script>

<template>
  <div class="live-sql" :class="{ 'is-active': active }">
    <textarea
      v-if="active"
      ref="textarea"
      v-model="sql"
      class="live-sql-editor"
      spellcheck="false"
      autocapitalize="off"
      autocomplete="off"
      aria-label="SQL (editable)"
      @input="autosize"
      @keydown="onKeydown"
    />
    <div class="live-sql-slot"><slot /></div>

    <div v-if="mounted" class="live-sql-toolbar">
      <button type="button" class="live-sql-btn primary" :disabled="running" @click="run">
        {{ running ? phase || 'Running…' : '▶ Run' }}
      </button>
      <button v-if="active && edited" type="button" class="live-sql-btn" :disabled="running" @click="reset">
        Reset
      </button>
      <span v-if="badge" class="live-sql-badge" :class="badge.cls" :title="detail || badge.text">
        {{ badge.icon }} {{ badge.text }}
      </span>
      <span v-if="timing && !running" class="live-sql-timing">
        {{ fmtMs(timing.query) }}<template v-if="timing.boot"> · {{ fmtMs(timing.total) }} incl. engine start</template>
      </span>
    </div>

    <div v-if="result && !running" class="live-sql-result">
      <pre v-if="detail && result.status !== 'match'" class="live-sql-detail" :class="badge?.cls.includes('known') ? 'known' : result.status">{{ detail }}</pre>
      <div v-for="(t, i) in tables" :key="i" class="live-sql-table-block">
        <div v-if="tables.length > 1" class="live-sql-table-sql">{{ t.sql.split('\n').filter((l) => !l.startsWith('--')).join(' ') }}</div>
        <div class="live-sql-table-wrap">
          <table class="live-sql-table">
            <thead>
              <tr><th v-for="(n, j) in t.names" :key="j">{{ n }}</th></tr>
            </thead>
            <tbody>
              <tr v-for="(row, k) in t.rows" :key="k">
                <td v-for="(cell, j) in row" :key="j" :class="{ num: isNumber(cell) }">{{ cell }}</td>
              </tr>
              <tr v-if="t.rows.length === 0"><td :colspan="t.names.length" class="empty">0 rows</td></tr>
            </tbody>
          </table>
        </div>
        <div v-if="t.more" class="live-sql-more">… {{ t.more }} more rows</div>
      </div>
    </div>

    <Teleport v-if="mounted && noticeOwner && liveState.build" to="body">
      <BuildNotice />
    </Teleport>

    <Teleport v-if="mounted && liveState.banner && liveState.owner === uid" to="body">
      <div class="live-sql-banner" role="alert">
        <span>{{ liveState.banner }}</span>
        <button type="button" aria-label="Dismiss" @click="dismissBanner">×</button>
      </div>
    </Teleport>
  </div>
</template>
