# Live WASM Examples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every documented SQL example runnable in DuckDB-WASM (Run button + editable SQL on the site, a `/wasm-status` page, and a CI check on the fresh build), reusable by other extensions, and replace the third-party deployed-WASM tester with our own action.

**Architecture:** One dependency-free ESM core (`extract`, `compare`, `sql`, `examples`) in `packages/duckdb-docs-live/src/`, imported directly by Node (the `wasm-sqllogictest` action's `run.mjs`) and by Vite (the VitePress plugin and Vue components). The site, the status page and CI therefore share one extractor and one comparator.

**Tech Stack:** Node 20 (`node --test`), plain ESM with JSDoc types (no build step), VitePress 1.6 / Vue 3 / markdown-it, `@duckdb/duckdb-wasm@1.33.1-dev119.0` (official engine, DuckDB v2.0.0; browser `getJsDelivrBundles`/`selectBundle`; Node `duckdb-node.cjs`), GitHub Actions composite action.

**Spec:** `docs/superpowers/specs/2026-11-25-live-wasm-examples-design.md`

## Global Constraints

- Worktree `/Users/majid/orca/workspaces/duckdb-dggs/live-wasm-examples`, branch `feature/live-wasm-examples`. **Never run `git commit`/push**: the maintainer commits. Each task ends with a checkpoint (`git status` + `git diff --stat`) instead of a commit.
- Core files under `packages/duckdb-docs-live/src/` are plain `.mjs` with **zero runtime dependencies** and no DOM or Node-only APIs, because the action imports them by relative path from `${{ github.action_path }}/../../../packages/duckdb-docs-live/src/` (remote `uses:` of this action downloads the whole repo, so this works from other repos).
- Engine: official `@duckdb/duckdb-wasm@1.33.1-dev119.0` everywhere (site, status page, CI action default). It runs DuckDB **v2.0.0**, which must equal the DuckDB version CI builds against (`duckdb_version: v2.0.0` in `MainDistributionPipeline.yml`) — a v2.0.0 extension refuses to load in v2.0.1 (verified 2026-11-25: dev119 = v2.0.0 and loads both the published extension and a raw unsigned v2.0.0 file; dev121+ = v2.0.1; `latest` = v1.5.4). Pin exactly; bump together with the CI DuckDB version. Haybarn is no longer used anywhere.
- Published extension: `INSTALL duck_dggs FROM community; LOAD duck_dggs;` (the official engine resolves `community` to `community-extensions.duckdb.org`). File: `https://community-extensions.duckdb.org/v2.0.0/wasm_eh/duck_dggs.duckdb_extension.wasm`.
- Extra repository for `spatial`: `https://extensions.duckdb.org`.
- The engine loads lazily on the first Run, never on page load (except on `/wasm-status`).
- Fence markers: ```` ```sql run ```` (run + show, no comparison), ```` ```sql norun ```` (never run).
- Statuses (one enum everywhere): `match | differs | error | skipped | shown | unreleased`.
- Known failures file: `docs/wasm-known-failures.json`, `[{ "id": string, "reason": string }]`.
- Results are always compared as `SELECT COLUMNS(*)::VARCHAR FROM (<q>)` strings, exactly as `run.mjs` does today.

## Review Focus

1. **Truncated CLI box output** (wide cells ending in `…`, elided rows `│ · │`, footer `N rows (M shown)`): expect `…` to act as a wildcard inside a cell and elided tables to be `shown`, never ✗. → Task 3 tests `truncated cell` and `elided rows`.
2. **A fence with setup statements and one box table after it** (e.g. `CREATE TABLE …; SELECT …;`): only the last statement is compared; earlier ones just run. → Task 2 test `box applies to last statement`.
3. **Duplicate headings on a page** (several `### Example`): every example id stays unique across the whole site. → Task 2 test `ids unique across docs`.
4. **The engine dies mid-page** (`exit()`/trap, as the DgAuthalic bug did): later examples report `error: engine stopped` instead of hanging, and the status page starts a fresh engine per page. → Task 4 test `crash marks the rest`.
5. **`?build=` URL unreachable or blocked by CORS**: one clear banner ("could not load build <url>: …"), not a ✗ on every example. → Task 7 manual check, step 6.

---

### Task 1: Package skeleton + shared `sql.mjs` (move from `run.mjs`)

**Files:**
- Create: `packages/duckdb-docs-live/package.json` (`"name": "duckdb-docs-live"`, `"type": "module"`, `"exports"`: `"."` → `src/index.mjs`, `"./vitepress"` → `vitepress/index.mjs`, `"./status"` → `vitepress/WasmStatus.vue`; `"peerDependencies"`: `vitepress ^1.6`, `vue ^3`, `@duckdb/duckdb-wasm 1.33.1-dev119.0`; `"scripts": {"test": "node --test test/"}`)
- Create: `packages/duckdb-docs-live/src/sql.mjs`, `src/index.mjs` (re-exports everything from `src/*.mjs`)
- Create: `packages/duckdb-docs-live/test/sql.test.mjs`
- Modify: `.github/actions/wasm-sqllogictest/action.yml`: `engine-package` default → `@duckdb/duckdb-wasm`, `engine-version` default → `1.33.1-dev119.0` (the existing `wasm-test` CI job thereby moves off haybarn too); in `run.mjs` `parseArgs` default `enginePackage` → `'@duckdb/duckdb-wasm'`
- Modify: `.github/actions/wasm-sqllogictest/run.mjs` (delete `stripSemicolon`, `errorMessage`, `isEngineCrash`, `render`, `rowsOf`, `runQuery`, `numericallyEqual`; import them)

**Interfaces:**
- Produces (from `src/sql.mjs`): `stripSemicolon(sql: string): string`, `errorMessage(e: unknown): string`, `isEngineCrash(message: string): boolean`, `render(v: unknown): string`, `rowsOf(table: ArrowTable): {cols: number, names: string[], rows: string[][]}` (now also returns `names`), `runQuery(conn: {query(sql): Promise<ArrowTable>}, sql: string): Promise<{cols, names, rows}>`, `numericallyEqual(a: string, b: string): boolean`. Bodies are moved verbatim; only `names` is added.

- [ ] **Step 1: Write tests** in `test/sql.test.mjs`: `render(null) === 'NULL'`, `render('') === '(empty)'`, `render(12n) === '12'`; `numericallyEqual('1.0000000001', '1')` true, `numericallyEqual('1.1', '1')` false, `numericallyEqual('abc', 'abc')` false; `stripSemicolon(' SELECT 1; ') === 'SELECT 1'`; `isEngineCrash('Program terminated with exit(1)')` true; `runQuery` with a fake conn whose `query` records the SQL: asserts the wrapped SQL starts with `SELECT COLUMNS(*)::VARCHAR FROM (` for `SELECT 1`, and is unwrapped for `PRAGMA version`.
- [ ] **Step 2:** `cd packages/duckdb-docs-live && npm test` → FAIL (module not found).
- [ ] **Step 3:** Move the functions into `src/sql.mjs`; in `run.mjs` add `import { … } from '../../../packages/duckdb-docs-live/src/sql.mjs';`.
- [ ] **Step 4:** `npm test` → PASS.
- [ ] **Step 5: Regression check against the published build.**
  ```bash
  mkdir -p /tmp/ddl && curl -sfo /tmp/ddl/duck_dggs.duckdb_extension.wasm https://community-extensions.duckdb.org/v2.0.0/wasm_eh/duck_dggs.duckdb_extension.wasm
  cd .github/actions/wasm-sqllogictest && npm install --no-save web-worker@1.2.0 @duckdb/duckdb-wasm@1.33.1-dev119.0
  node run.mjs --extension duck_dggs --extension-file /tmp/ddl/duck_dggs.duckdb_extension.wasm --tests ../../../test/sql
  ```
  Run both the copy and the new file with the same installed engine (`--engine-package @duckdb/duckdb-wasm`).
  Expected: the same pass/fail counts as running the unmodified `run.mjs` (`git stash` is NOT allowed — compare against a copy of the original file in `/tmp`). Failures for functions newer than the published ref are expected and identical in both.
- [ ] **Step 6: Checkpoint** — `git status -sb && git diff --stat`.

### Task 2: `extract.mjs`

**Files:**
- Create: `packages/duckdb-docs-live/src/extract.mjs`, `test/extract.test.mjs`, `test/fixtures/` (small `.md` snippets copied from real pages)

**Interfaces:**
- Produces: `extractExamples(markdown: string, page: string): Example[]` where
  `Example = { id: string, page: string, heading: string, line: number, mode: 'compare'|'run'|'norun', statements: Statement[], needs: string[] }`,
  `Statement = { sql: string, expected: Expected|null }`,
  `Expected = { kind: 'arrow', text: string } | { kind: 'box', text: string }`.
  `needs` contains `'spatial'` when any statement matches `/\bLOAD\s+spatial\b/i`; that `LOAD spatial` statement itself is kept (the engine installs first, Task 4).

Rules (the algorithm the tests pin):
- Only ```` ```sql ```` fences. Info string words after `sql`: `run` / `norun` set `mode`; default `compare`.
- Split fence into statements on `;` at end of line (outside quotes). A `-- → value` line (or consecutive `-- ` continuation lines right after it) is the expected value of the statement immediately before it.
- A box table is the next fence (any info string, empty allowed) if its first non-blank line starts with `┌`, and only blank lines separate it from the sql fence. It is the expected value of the **last** statement.
- A fence with `mode: 'compare'` and no expected values anywhere is **not an example** (signatures, setup). `run` fences are always examples; `norun` fences are returned with `mode: 'norun'` so the status page can count them.
- `heading` = text of the nearest preceding `##`/`###` heading; `id = ${page}#${slug(heading)}/${n}` where `n` counts examples under that heading from 1. `slug` lowercases and replaces runs of non-`[a-z0-9_]` with `-`.

- [ ] **Step 1: Write tests:**
  - `signature fence is not an example` — `UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER)` fence → `[]`.
  - `arrow comments` — fence `SELECT dggs_n_cells(5);\n-- → 10242\n\nSELECT dggs_res_info(5);\n-- → {res: 5, cells: 10242}` → 2 statements, expected `{kind:'arrow', text:'10242'}` and `'{res: 5, cells: 10242}'`.
  - `box table` — the `geo_distance_km` example from `docs/functions/coverage-geodesy.md` → one statement, `expected.kind === 'box'`, text starts with `┌`.
  - `box applies to last statement` — `CREATE TABLE t AS SELECT 1 x;\nSELECT x FROM t;` + box → statement 0 expected `null`, statement 1 box.
  - `markers` — ```` ```sql norun ```` → `mode 'norun'`; ```` ```sql run ```` without output → example with `mode 'run'`.
  - `needs spatial` — fence containing `LOAD spatial;` → `needs: ['spatial']`.
  - `ids unique across docs` — run over every `docs/**/*.md` in the repo except `node_modules/`, `.vitepress/` and `superpowers/` (read from disk in the test), assert no duplicate `id` and total examples `> 100`.
- [ ] **Step 2:** `npm test` → FAIL.
- [ ] **Step 3:** Implement `extractExamples` in `src/extract.mjs` (line-based scanner; no markdown library).
- [ ] **Step 4:** `npm test` → PASS.
- [ ] **Step 5: Checkpoint.**

### Task 3: `compare.mjs`

**Files:**
- Create: `packages/duckdb-docs-live/src/compare.mjs`, `test/compare.test.mjs`

**Interfaces:**
- Consumes: `numericallyEqual` (Task 1), `Expected` (Task 2).
- Produces: `parseBox(text: string): { names: string[], types: string[], rows: string[][], elided: boolean }`;
  `compareResult(expected: Expected, actual: {names: string[], rows: string[][]}): { status: 'match'|'differs'|'shown', detail?: string }`.

Rules:
- **Box:** line 2 = names, line 3 = types, rows between `├` and `└`; cells split on `│`, trimmed. `elided = true` if any row is all `·`, or a footer line matches `/\d+ rows/`. Elided → `shown`. Otherwise compare row count, then each cell with `cellsMatch`.
- **Arrow:** actual rendered as rows joined by `\n`, columns by `, ` (1×1 → the single value). Compare with `cellsMatch`.
- `cellsMatch(expected, actual)`: normalise both (strip `'` or `"` around struct keys: `/(['"])(\w+)\1\s*:/g → '$2:'`, collapse whitespace). Then tokenise on numbers (`/-?\d+(\.\d+)?(e[-+]?\d+)?/i`). Non-number segments must be equal, except that `...`, `(...)` and `…` in the expected text match any text (including empty). A number token matches if `numericallyEqual`, or if the actual rounds to the expected at the expected token's decimal places (`-0.000` matches `-1e-17`; `49811.1` matches `49811.0923`).

- [ ] **Step 1: Write tests:**
  - `struct keys quoted vs unquoted` — expected arrow `{res: 5, cells: 10242, area_km: 49811.1}` vs actual `{'res': 5, 'cells': 10242, 'area_km': 49811.0923}` → `match`.
  - `rounded point` — `POINT (-0.902 -0.000)` vs `POINT (-0.9021342 -1.2e-17)` → `match`; vs `POINT (-0.95 0)` → `differs` with a `detail` naming both values.
  - `wildcard` — `POLYGON ((...))` vs a full polygon string → `match`.
  - `list` — `[2412, 2413, 2381]` vs `[2412, 2413, 2380]` → `differs`.
  - `box exact` — the `geo_distance_km` box vs `{names:['…'], rows:[['5364.66413006686']]}` → `match` (the official engine prints one digit fewer than the docs' `5364.664130066861`, observed 2026-11-25, so this is the real case).
  - `box row count` — box with 1 row vs 2 actual rows → `differs`, detail `expected 1 row(s), got 2`.
  - `truncated cell` — box cell `[600, 601, 617, …` vs full list → `match`.
  - `elided rows` — box containing a `│ · │` row → `shown`.
  - `multi-row vs arrow` — arrow `1, a\n2, b` vs rows `[['1','a'],['2','b']]` → `match`.
- [ ] **Step 2:** `npm test` → FAIL. **Step 3:** implement. **Step 4:** `npm test` → PASS.
- [ ] **Step 5: Checkpoint.**

### Task 4: `examples.mjs` — run examples, apply known failures

**Files:**
- Create: `packages/duckdb-docs-live/src/examples.mjs`, `test/examples.test.mjs`

**Interfaces:**
- Consumes: `runQuery`, `errorMessage`, `isEngineCrash` (Task 1), `Example` (Task 2), `compareResult` (Task 3).
- Produces:
  - `runExample(session: Session, ex: Example, opts: {published: boolean}): Promise<ExampleResult>`;
    `Session = { connect(): Promise<Conn>, ensure(ext: string): Promise<void>, dead: boolean }` (provided by the Node runner in Task 5 and the browser engine in Task 7);
    `ExampleResult = { id, page, status, statements: [{sql, status, expected?, actual?: {names, rows}, detail?, ms}] }`. The example's status is the worst of its statements (`error > differs > unreleased > shown > match`), `skipped` for `norun`, `shown` for `run`.
  - `applyKnownFailures(results: ExampleResult[], known: {id, reason}[]): { results: ExampleResult[], problems: string[] }` — a known id that `differs`/`error`s becomes `skipped` with `detail: 'known failure: <reason>'`; a known id that `match`es adds the problem `"<id> passes now — remove it from docs/wasm-known-failures.json"`; an unknown id in the list adds `"<id> is in the known-failures list but no such example exists"`.

Behaviour:
- Calls `session.ensure(n)` for each `ex.needs`; if it throws → `skipped`, detail `needs <n>: <message>`.
- New connection per example; statements run in order; a statement with `expected: null` only has to not error.
- Error matching `/does not exist|No function matches/i` with `opts.published` → `unreleased`; otherwise `error`.
- If an error `isEngineCrash`, set `session.dead = true`; any later `runExample` on that session returns `error` with detail `engine stopped`, without calling `connect`.

- [ ] **Step 1: Write tests** with a fake `Session` whose `connect()` returns a conn answering from a map of SQL → rows / thrown error:
  - `match`, `differs`, `setup statement error → error`.
  - `unreleased only when published` — `Catalog Error: Scalar Function with name foo does not exist` → `unreleased` with `{published:true}`, `error` with `{published:false}`.
  - `crash marks the rest` — first example throws `Program terminated with exit(1)`; second example → `error`, detail `engine stopped`, `connect` called once in total.
  - `needs spatial fails → skipped`.
  - `known failure` — three cases above for `applyKnownFailures`.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS.
- [ ] **Step 5: Checkpoint.**

### Task 5: Doc examples in `run.mjs` and the action

**Files:**
- Modify: `.github/actions/wasm-sqllogictest/run.mjs` (new options + docs phase + summary section)
- Modify: `.github/actions/wasm-sqllogictest/action.yml` (inputs `docs`, `known-failures`, `published`; pass through)
- Modify: `.github/actions/wasm-sqllogictest/README.md` (document the inputs)
- Modify: `.github/workflows/MainDistributionPipeline.yml` `wasm-test` job: add `docs: docs/**/*.md` and `known-failures: docs/wasm-known-failures.json`
- Create: `docs/wasm-known-failures.json` → `[]`

**Interfaces:**
- Consumes: `extractExamples`, `runExample`, `applyKnownFailures`.
- Produces: CLI `--docs <file|dir>` (repeatable; dirs walked for `.md`, `node_modules`, `.vitepress` and `superpowers` skipped — the same exclusions as the status page loader in Task 8), `--known-failures <file>`, `--published` (boolean; default off in CI, on in the deployed test); `--tests` becomes optional when `--docs` is given. Page name = path relative to the docs root given, without `.md` (e.g. `functions/coverage-geodesy`). Node `Session`: one `openDb(engine)` per page, then `LOAD '<extension-file>'`; `ensure('spatial')` tries `INSTALL spatial` then each `--repo` as `INSTALL spatial FROM '<repo>'`, then `LOAD spatial`.
- Action inputs: `docs` (newline list, default `''`), `known-failures` (default `''`), `published` (default `'false'`).

Exit / summary rules:
- Job summary gets a `### Doc examples` section: header line with engine, DuckDB version, extension file **or URL**; then `| | Page | Example | Detail |` rows for every non-`match` result, and a final count line `N match · N differs · N error · N skipped · N shown · N unreleased`.
- Fail (exit 1) if any `differs`/`error`, any `problems` from `applyKnownFailures`, or zero examples extracted while `--docs` was given.

- [ ] **Step 1:** Run against the published file (from Task 1 Step 5): `node run.mjs --extension duck_dggs --extension-file /tmp/ddl/duck_dggs.duckdb_extension.wasm --docs ../../../docs --published` → the run fails with "unknown option --docs" (proves the test drives new behaviour).
- [ ] **Step 2:** Implement the options, the docs phase, and the summary in `run.mjs`; wire `action.yml`.
- [ ] **Step 3:** Re-run Step 1's command. Expected: a doc-example table on stdout, a count line, total examples `> 100`, and `GITHUB_STEP_SUMMARY=/tmp/sum.md` produces the markdown section. Record the non-match list for Task 6.
- [ ] **Step 4:** `node run.mjs … --docs /tmp/empty-dir` → exit 1, message `no doc examples found`.
- [ ] **Step 5:** `actionlint` (if installed) on the workflow; otherwise `npx --yes yaml-lint` style parse check of both YAML files.
- [ ] **Step 6: Checkpoint.**

### Task 6: First-run triage

**Files:**
- Modify: `docs/**/*.md` (only expected outputs that are wrong, and `norun` markers on fences like `INSTALL duck_dggs FROM community`)
- Modify: `docs/wasm-known-failures.json`
- Create: `docs/superpowers/plans/2026-11-25-triage-notes.md` (the list that goes in the PR description)

The CI target is the fresh build, so triage against **this commit's** WASM build, not the published one: download the `duck_dggs-v2.0.0-extension-wasm_eh` artifact from the latest green `MainDistributionPipeline` run on `main` (`gh run download <id> -n duck_dggs-v2.0.0-extension-wasm_eh -D /tmp/ddl-main`), or build it locally if that run is older than `HEAD`.

- [ ] **Step 1:** `node run.mjs --extension duck_dggs --extension-file /tmp/ddl-main/…wasm --docs ../../../docs` (no `--published`). Save the output.
- [ ] **Step 2:** For each `differs`/`error`, run the same SQL in the native build of the same commit (`build/release/duckdb -unsigned -c "LOAD 'build/release/extension/duck_dggs/duck_dggs.duckdb_extension'; <sql>"` in the main checkout `/Users/majid/duckdb-dggs` — read only, do not rebuild there; if no native build of this commit exists, build in this worktree with `GEN=ninja make release` after `git submodule update --init --recursive`).
  - Native == WASM → the doc is wrong: fix the expected output in the `.md`.
  - Native == doc → WASM bug: add `{id, reason}` to `docs/wasm-known-failures.json`, reason starting `WASM bug:`.
  - Neither (e.g. the example needs a file or network) → mark the fence `norun` with a short `<!-- why -->` comment above it.
- [ ] **Step 3:** Re-run Step 1. Expected: exit 0, no `problems`.
- [ ] **Step 4:** Write the triage notes file: one line per change, with category (doc fixed / WASM bug / norun) and the id.
- [ ] **Step 5: Checkpoint.**

### Task 7: Browser engine, markdown plugin, `LiveSql.vue`

**Files:**
- Create: `packages/duckdb-docs-live/vitepress/engine.mjs`, `vitepress/plugin.mjs`, `vitepress/LiveSql.vue`, `vitepress/index.mjs`, `vitepress/style.css`
- Modify: `docs/package.json` (add `"duckdb-docs-live": "file:../packages/duckdb-docs-live"`, `"@duckdb/duckdb-wasm": "1.33.1-dev119.0"`)
- Modify: `docs/.vitepress/config.ts` (add `markdown: { config: (md) => md.use(liveSql) }` and `vite.optimizeDeps.exclude: ['@duckdb/duckdb-wasm']`)
- Create: `docs/.vitepress/theme/index.ts` (extends `DefaultTheme`, `enhanceApp` registers `LiveSql`, `WasmStatus`, and calls `provideLiveSqlConfig(app, config)`)
- Create: `docs/.vitepress/live-sql.config.ts`: `{ extension: 'duck_dggs', engineVersion: '1.33.1-dev119.0', repositories: ['https://extensions.duckdb.org'] }`

**Interfaces:**
- Consumes: `extractExamples`, `runExample`, `compareResult`, `parseBox`.
- Produces:
  - `vitepress/engine.mjs`: `getSession(config, buildUrl: string|null): Promise<Session & {info: {duckdb: string, engine: string, extension: string, source: string}}>` — a memoised singleton per `buildUrl`. It dynamically imports `@duckdb/duckdb-wasm`, picks the bundle with `selectBundle(getJsDelivrBundles())`, and creates the worker from a `Blob` that `importScripts` the jsDelivr worker URL. It opens with `allowUnsignedExtensions: !!buildUrl`. Setup: published → `INSTALL <ext> FROM community; LOAD <ext>`; build → `LOAD '<buildUrl>'`. `info.extension` comes from `SELECT extension_version FROM duckdb_extensions() WHERE extension_name = '<ext>'`. `ensure()` behaves as in Task 5. Also `buildUrlFromLocation(): string|null` reads `?build=` and keeps it in `sessionStorage` (inside a try/catch), so it survives navigation within the site.
  - `vitepress/plugin.mjs`: `liveSql(md)` — a markdown-it core rule that runs `extractExamples` on `env.relativePath`'s source. For each example fence it wraps the existing rendered fence (and its box fence) in `<LiveSql :example="<json, attribute-escaped>">…original html…</LiveSql>` (the original markup goes in the default slot, so without JS the page is exactly today's).
  - `LiveSql.vue` props: `example: Example`. UI: the slot, plus a toolbar `[▶ Run]` (`[Reset]` after the first edit), status badge, and timing. After the first Run, the SQL becomes an editable `<textarea>`, auto-sized and monospace, styled to match VitePress code blocks. The result renders as a box-style table with `<table>` and VitePress CSS vars (light/dark). An edited example shows its result without a badge (no expected value).
  - `provideLiveSqlConfig(app, config)` / `useLiveSqlConfig()` (Vue provide/inject).

- [ ] **Step 1:** `cd docs && npm install && npm run build` → PASS (the build must still succeed with the plugin; the pages render the fallback).
- [ ] **Step 2:** `npm run dev`; with the Playwright MCP tools open `http://localhost:5173/duckdb-dggs/functions/coverage-geodesy`. Check that **no request to jsdelivr or community-extensions.duckdb.org happens on load** (`browser_network_requests`).
- [ ] **Step 3:** Click Run on `geo_distance_km` → ✓ badge, value `5364.664130066861`, and the network shows the engine and `duck_dggs.duckdb_extension.wasm` loaded once.
- [ ] **Step 4:** Edit the SQL to `SELECT dggs_n_cells(3)`, Run → table, no badge; Reset → original SQL and ✓ again.
- [ ] **Step 5:** `getting-started` → the `seqnum_to_boundary` example (needs spatial) runs, or shows ⏭ with the install error. Either way, record which in the triage notes.
- [ ] **Step 6 (Review Focus 5):** open `?build=http://localhost:9/nope.wasm`, Run → one banner, `could not load build http://localhost:9/nope.wasm: …`, and the example shows `error`, not a ✗ badge on every example.
- [ ] **Step 7:** Toggle dark mode, and check at a 375px viewport that the result table scrolls horizontally inside its box, not the page.
- [ ] **Step 8: Checkpoint.**

### Task 8: Status page

**Files:**
- Create: `packages/duckdb-docs-live/vitepress/WasmStatus.vue`, `vitepress/examples.data.mjs` (factory)
- Create: `docs/.vitepress/examples.data.mts` (`export default createExamplesLoader('**/*.md', { exclude: ['superpowers/**', 'node_modules/**'] })`)
- Create: `docs/wasm-status.md` (frontmatter `title: WASM status`, body `<WasmStatus />`)
- Modify: `docs/.vitepress/config.ts` (footer/sidebar link "WASM status" under Guide; add `superpowers/**` to `srcExclude`)
- Copy `docs/wasm-known-failures.json` into the loader output (the loader reads it), so the page applies the same list.

**Interfaces:**
- Consumes: `getSession`, `buildUrlFromLocation`, `runExample`, `applyKnownFailures`, `extractExamples`.
- Produces: `createExamplesLoader(pattern: string, opts: {exclude?: string[], knownFailures?: string}): VitePressDataLoader` → data `{ pages: {page, examples: Example[]}[], known: {id, reason}[] }`. `WasmStatus.vue` runs every page in a **fresh session** (`getSession` gets a `fresh: true` option that bypasses the memo) and shows the build-source/engine/extension header from `info`, a `[Run all]` button, and one row per page with counts per status. Each non-match row expands to show expected vs actual. A `problems` list from `applyKnownFailures` is shown in red at the top.

- [ ] **Step 1:** `npm run build` → PASS; `/wasm-status` page exists in `dist`.
- [ ] **Step 2:** In dev, open `/duckdb-dggs/wasm-status` → it auto-runs. With `?build=<url of the Task 6 artifact served by npx http-server --cors /tmp/ddl-main -p 8099>`, the counts equal Task 6 Step 3's CI counts exactly (the site/CI agreement anti-goal). Without `?build=`, differences are only `unreleased`.
- [ ] **Step 3:** Run all twice → the counts are stable and the network shows one engine download per run (cached).
- [ ] **Step 4: Checkpoint.**

### Task 9: Replace the deployed-WASM test; wiring and docs

**Files:**
- Rewrite: `.github/workflows/WasmDeployedTest.yml`
- Delete: `scripts/test-wasm.sh`, `scripts/wasm-tester-patch.mjs`
- Modify: `Makefile` (`test_wasm`, `test_wasm_local`)
- Modify: `.github/workflows/docs.yml` (`paths:` add `packages/duckdb-docs-live/**`; `npm ci` keeps working with the `file:` dep)
- Create: `packages/duckdb-docs-live/README.md` (how another extension adopts it: package dep, the config/theme lines, `wasm-status.md`, the action inputs)
- Modify: `docs/DEVELOPMENT.md` (short section: Run button, `?build=`, `/wasm-status`, known failures)

**Interfaces:**
- `WasmDeployedTest.yml`: triggers `schedule: '0 6 * * 1'`, `workflow_dispatch`, and push/PR on its own path + `.github/actions/wasm-sqllogictest/**` + `packages/duckdb-docs-live/src/**`. One job:
  1. checkout `main` (action + runner);
  2. read `ref:` from `description.yml` (`yq '.repo.ref'`) and `git worktree add`, or a second `actions/checkout` with `ref:` into `released/`;
  3. `curl -fsSLo $RUNNER_TEMP/duck_dggs.duckdb_extension.wasm https://community-extensions.duckdb.org/v2.0.0/wasm_eh/duck_dggs.duckdb_extension.wasm` (`-f` makes a 404 fail the job);
  4. `uses: ./.github/actions/wasm-sqllogictest` with `extension-file`, `tests: released/test/sql`, `docs: released/docs`, `published: 'true'`, and `known-failures: released/docs/wasm-known-failures.json` only if that file exists there.
  The job summary shows the extension **URL** (the published file) as the build source.
- `Makefile`: `test_wasm` = download the published file to `build/wasm-published/` and run `run.mjs` with `--tests test/sql --docs docs --published`; `test_wasm_local` = same with `EXT_FILE ?= build/wasm_eh/extension/duck_dggs/duck_dggs.duckdb_extension.wasm` and no `--published`. Both install the engine into the action dir first, the same way `action.yml` does.

- [ ] **Step 1:** `make test_wasm` → runs and prints both sections. Exit status: whatever the published build gives (record it).
- [ ] **Step 2:** `act` is not assumed. Validate the workflow by parsing (`actionlint` if present). Then `grep -rn "test-wasm.sh\|wasm-tester-patch" .` → no hits outside `CHANGELOG.md`.
- [ ] **Step 3:** `cd docs && npm ci && npm run build` → PASS (simulates `docs.yml`).
- [ ] **Step 4:** `cd packages/duckdb-docs-live && npm test` → PASS (everything still green).
- [ ] **Step 5: Final checkpoint** — `git status -sb` (the branch line must not show `...origin/main`), `git diff --stat origin/main`. Hand over to the maintainer with the triage notes; **do not commit**.

### Task 10: Deterministic `seqnum_all_parents` (added 2026-11-25, maintainer request)

**Why:** Triage found `seqnum_all_parents(2412, 5)` = `[599, 616, 615]` on native osx_arm64 and `[599, 598, 615]` on WASM. Measured with `spatial`: cell 2412 (res 5) shares 50.06 % of its area with 599 and 49.94 % with 615, and touches 598 and 616 at **a single point each** (zero area). DGGRID's `DgIDGGS4H::setAddParents` (third_party/DGGRID/src/lib/dglib/lib/DgIDGGS4H.cpp:63) samples edge midpoints in the projected plane, and some fall exactly on a parent edge/vertex — a mathematical tie decided by last-bit float rounding (FMA contraction, WASM's 128-bit soft `long double`). Aperture 3/7 (`DgHexIDGGS::setAddParents`, `DgIDGGS7H::setAddParents`) sample the child's vertices, which are tie-prone the same way. `DgIDGGS43H::setAddParents` is a dglib Fatal (`report(..., Fatal)` → exit).

**Decision (maintainer):** `seqnum_all_parents` returns the parents whose cells **share area** with the cell, identically on every platform: the containing parent (`seqnum_parent`) first, then the others in ascending order. For 2412 @ res 5: `[599, 615]`. Document the definition and the reason on the function's doc page.

**Files:**
- Modify: `src/dggrid_transform.cpp` `seqNumAllParents` (≈ line 1132)
- Modify: `test/sql/duck_dggs.test` (All Parents block ≈ line 503)
- Modify: `docs/functions/parent-child.md` (`## seqnum_all_parents`)
- Modify: `docs/wasm-known-failures.json` (remove the `seqnum_all_parents` entry), `docs/superpowers/plans/2026-11-25-triage-notes.md` (resolution line)

**Interfaces:** `std::vector<SeqNum> seqNumAllParents(const DggsParams &p, SeqNum seqnum)` — signature unchanged.

Algorithm (the part a signature doesn't determine):
- Do not call `idggs->setParents`. Get the child cell's vertices in the res grid's planar `backFrame()` (`grids()[res]->setVertices`, then `backFrame().getAddress(...)`), and its centre in the same frame.
- Sample points: each vertex and each edge midpoint, each moved toward the centre: `q = c + (v - c) * (1 - 1e-6)`. 1e-6 of the cell radius is far above float noise (~1e-15) and far below any real overlap, so no sample sits on a parent boundary.
- Convert each sample to the res-1 grid, take its SEQNUM, dedupe. Primary = `seqNumParent(p, seqnum)`; put it first (insert if missing); sort the rest ascending.
- Hexagon topologies only (apertures 3, 4, 7 and mixed 43 — 43 must no longer hit the dglib Fatal). For TRIANGLE / DIAMOND (and any other topology), keep today's behaviour (single congruent parent) unchanged.

- [ ] **Step 1: Tests first** (`test/sql/duck_dggs.test`, All Parents block):
  - `SELECT seqnum_all_parents(2412::UBIGINT, 5)` → `[599, 615]`
  - interior `seqnum_all_parents(2380::UBIGINT, 5)` → `[599]` (unchanged)
  - first element equals `seqnum_parent` for 2412 and 2380 (keep existing)
  - every listed parent shares area: `SELECT bool_and(…)` is not possible without spatial, so instead assert for a sample of 200 cells at res 5 (`range(0, 10242, 51)`) that `len(seqnum_all_parents(c,5)) BETWEEN 1 AND 3` and `list_sort(list_slice(l, 2, len(l))) = list_slice(l, 2, len(l))` (tail ascending).
  - aperture 7 and aperture 3 (`dggs_params('ISEA', 7, 'HEXAGON', …)`, `dggs_params('ISEA', 3, 'HEXAGON', …)`): first element = `seqnum_parent` with the same params, length ≥ 1, for a couple of cells.
  - mixed 43 hex params (see grid-configuration docs / `dggs_params` for how aperture 43 is expressed): `seqnum_all_parents` returns without crashing, first = `seqnum_parent`.
- [ ] **Step 2:** native build in this worktree: `git submodule update --init --recursive` (pass `--reference /Users/majid/duckdb-dggs/duckdb` for the duckdb submodule if it speeds things up; never modify /Users/majid/duckdb-dggs), then `GEN=ninja make release` (long; run in background). Run `make test` → the new tests FAIL (RED).
- [ ] **Step 3:** implement; `make release && make test` → PASS (GREEN), all existing tests still pass.
- [ ] **Step 4 (the cross-platform proof):** with `spatial` on the native build, for 2412 @ 5 and the 200-cell sample, assert every returned parent has `ST_Area(ST_Intersection(child, parent)) > 0` and every parent with positive overlap among the child's neighbours' parents is returned. Then WASM: build `wasm_eh` locally if the toolchain allows (user's emsdk 4.0.7 at /Users/majid/Ready/Apps/tests/personal/emsdk — do NOT change its active version; `make wasm_eh` per extension-ci-tools); if that isn't feasible in reasonable time, say so — CI's `wasm-test` job (test/sql + doc examples on the fresh wasm_eh) is the WASM proof after the maintainer pushes.
- [ ] **Step 5:** docs. In `## seqnum_all_parents`: definition = "parents whose cells share area with the cell; the containing parent (`seqnum_parent`) first, the rest ascending". Update the example to `[599, 615]` with comment "Cell 2412 straddles two parents". Add a short `::: info Why not cells that only touch at a corner?` block: aperture-4/3/7 cells can meet parent corners exactly; parents touching only at a single point are excluded because deciding them is a floating-point tie that gave different answers on different platforms (e.g. native vs WASM); sampling is moved slightly inside the cell so the result is the same everywhere. Remove the known-failures entry; update triage notes.
- [ ] **Step 6:** run the doc examples with `run.mjs --docs docs --known-failures docs/wasm-known-failures.json` against the CI wasm file **with `--published` off**: `seqnum_all_parents/1` will now `differ` on that older build (it still has the old algorithm) — expected; record it, don't add a known failure. Checkpoint.
