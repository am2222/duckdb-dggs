# Live WASM examples in the docs — design

Date: 2026-11-25 · Branch: `feature/live-wasm-examples`

## Intent

Every SQL example in the docs carries hand-pasted output. Nothing checks that
output against the extension, and nothing checks that the extension still works
in DuckDB-WASM from a reader's browser. We want the examples to *run*:

- **Readers** click Run on any example, see the live result from DuckDB-WASM,
  and can edit the SQL and re-run it.
- **The maintainer** gets a status page that runs every example on the site and
  shows which ones still match their documented output, plus a CI check that
  runs the same examples against the freshly built WASM extension of each commit.

It must be reusable by the maintainer's other DuckDB extensions, not hard-wired
to duck_dggs.

## Goals

1. A Vue component for VitePress that turns a doc example into a runnable,
   editable example, showing ✓ match / ✗ differs / ⏭ skipped against the
   documented output.
2. A `/wasm-status` page that runs every example on the site and reports per page.
3. A CI check that runs the doc examples against the CI-built `wasm_eh` file.
4. All three reusable by another extension's repo with a few lines of config.

## Non-goals

- Publishing per-commit WASM builds to a public URL (e.g. GitHub Pages). CI tests
  the fresh build itself; `?build=` is for a build you serve yourself. Can be
  added later.
- A full SQL IDE (autocomplete, schema browser, result charts).
- Rewriting the existing examples. The current `.md` files keep working unchanged
  apart from optional markers.

## Anti-goals (failure even if it "works")

- Readers who never click Run download DuckDB-WASM anyway. The engine must load
  lazily on first Run, never on page load (except on `/wasm-status`).
- The site, the status page and CI disagree about what an example is or what
  counts as a match. One extractor and one comparator, shared by all three.
- A false green: an example that silently isn't run, or a CI job that passes when
  nothing ran or the artifact was missing.

## What exists today (facts the design builds on)

- Docs: VitePress 1.6 in `docs/`, `base: '/duckdb-dggs/'`, deployed by
  `.github/workflows/docs.yml` to GitHub Pages.
- ~180 ```` ```sql ```` fences in `docs/functions/*.md` and `docs/guide/*.md`.
  They are of three kinds:
  - **Signature blocks** (`UBIGINT seqnum_to_zorder (seqnum UBIGINT, ...)`) —
    not SQL, must never run.
  - **Examples with a DuckDB box table** in the next fence (` ``` ` block starting
    `┌`), e.g. `coverage-geodesy.md`. Exact-ish expected output.
  - **Examples with `-- → value` comments** inside the SQL fence, e.g.
    `getting-started.md`, `hierarchical-indexes.md`. Sometimes abbreviated or
    rounded (`-- → POLYGON ((...))`, `area_km: 49811.1`). A fence may hold several
    statements each with its own `-- →`.
- `.github/actions/wasm-sqllogictest/` (composite action + `run.mjs`, ~600 lines)
  already runs `test/sql` against the CI `wasm_eh` artifact in
  `@haybarn/haybarn-wasm@2.0.0-rc5` under Node, comparing via
  `SELECT COLUMNS(*)::VARCHAR FROM (<q>)`. Job `wasm-test` in
  `MainDistributionPipeline.yml`. It is already meant to be reused by other
  extensions via `am2222/duckdb-dggs/.github/actions/wasm-sqllogictest@main`.
- Published WASM extension: `https://haybarn-extensions.query.farm/community/v2.0.0/wasm_eh/duck_dggs.duckdb_extension.wasm`
  returns 200 (checked 2026-11-25). `@haybarn/haybarn-wasm` has a browser build
  (`dist/duckdb-browser.mjs`). npm `latest` is a 1.5.x engine; the 2.x engine is
  under the `rc` tag — the engine version must match the extension's DuckDB
  version (v2.0.0), so it is pinned, not `latest`.
- Haybarn has no `spatial` for v2.0.0 wasm_eh; `INSTALL spatial FROM
  'https://extensions.duckdb.org'` works in the haybarn engine.

## Approaches considered

1. **Run-button per example (A)** — lazy, cheap for readers, no overview.
2. **Auto-run every example on page load (B)** — instant overview for the
   maintainer, but every reader pays the WASM download.
3. **Editable playground (C)** — best for readers to explore, no overview.

**Chosen:** A + C for readers (Run button; once loaded, the SQL becomes editable),
and B's overview moved to a dedicated `/wasm-status` page so only the maintainer
pays for running everything.

For CI, two options:

1. **Node runner (chosen)** — extend the existing `wasm-sqllogictest` action with
   a `docs` input; extract examples from markdown and run them on the CI build.
   Fast, no browser, reuses the engine setup that already works.
2. **Headless browser** on `/wasm-status?build=…` via Playwright — tests the
   real browser path too, but slower and more moving parts. Deferred.

Build source: the site loads the **published** extension by default (what
readers can install). `?build=<url>` overrides it on any page for testing a
build you serve yourself (e.g. a local `build/wasm_eh` via a CORS-enabled static
server). CI tests the fresh artifact directly and prints the engine version,
extension source and pass/fail table in the job summary.

## Design

### Package: `packages/duckdb-docs-live/` (name is the builder's call)

A small ESM package in this repo, consumed by `docs/` via a local path
dependency. Other extensions can consume it by git URL or after an npm publish
(publishing is the maintainer's step, not part of this work). Three entry points:

- **`extract`** (pure, no DOM, no DuckDB — used by the markdown plugin, the
  status page and the CI runner):
  - Input: a markdown string. Output: a list of examples
    `{ id, page, heading, sql, statements: [{ sql, expected }], mode }`.
  - A fence is an example only if it has expected output: a following box-table
    fence, or one or more `-- →` comments. Fences with neither (signatures,
    setup-only snippets) are not examples. Info-string markers override:
    ```` ```sql run ```` (run, display result, no comparison), ```` ```sql norun ````
    (never run, e.g. `INSTALL … FROM community`).
  - `id` is stable: page path + nearest heading + index under that heading.
- **`compare`** (pure): compares a live result to the expected value.
  - Box table: parse header (column names, types) and rows; compare cell text,
    with numeric tolerance (same rule as `run.mjs` `numericallyEqual`).
  - `-- →` value: single-cell comparison of the rendered value; `...` /
    `(...)` in the expected text acts as a wildcard; numbers in the expected text
    match if the live value rounds to them (handles `49811.1`).
  - Result: `match | differs | error | skipped`, plus expected/actual for display.
- **VitePress integration**:
  - A markdown-it plugin (`config.markdown.config`) that replaces each example
    fence (and its output fence) with `<LiveSql :example="…">`, leaving the
    original code and output as the server-rendered fallback (no JS = today's page).
  - `LiveSql.vue`: shows the SQL and the documented output as today, plus a Run
    button. First Run lazily imports the engine module, starts DuckDB-WASM in a
    worker, loads the extension, then runs. After that the SQL is editable, with
    Reset. Shows the live result table, a ✓/✗ badge vs the documented output, and
    timing. One shared engine per page; examples run on a fresh connection each,
    in document order (so earlier statements in the same fence count).
  - `WasmStatus.vue`: on mount, loads the engine, runs every example from every
    page (the plugin emits a build-time index of all extracted examples), and
    renders a per-page table: counts, and an expandable diff per ✗. Header shows
    extension version (`SELECT extension_version FROM duckdb_extensions()`),
    engine version, and build source. "Run all" re-runs.
  - Config, in the consuming site's `.vitepress/config.ts`:
    `{ extension: 'duck_dggs', engine: '@haybarn/haybarn-wasm@2.0.0-rc5',
    extensionSource: 'community', extraSetup: [] }`.
  - `spatial`: examples that need it declare it (`LOAD spatial` in the SQL is
    detected); the engine installs it from `extensions.duckdb.org` on demand.
    If that fails, the example shows ⏭ with the reason.

### CI

- `wasm-sqllogictest` action gains `docs` (glob, e.g. `docs/**/*.md`). When set,
  after the sqllogictest files it runs the extracted doc examples using the
  package's `extract` + `compare`, and adds a "Doc examples" section to the job
  summary (page, example id, status, diff). Any `differs` or `error` fails the
  job. Zero examples found fails the job (no false green).
- `MainDistributionPipeline.yml` `wasm-test` job passes `docs: docs/**/*.md`.
- `docs.yml` builds the site with the package; no new deploy behaviour.

### Reuse by another extension

- Docs: add the package, two lines in `.vitepress/config.ts`, and a
  `wasm-status.md` page containing `<WasmStatus />`.
- CI: call `am2222/duckdb-dggs/.github/actions/wasm-sqllogictest@main` with
  `docs:`.

## Decisions made after review (maintainer's calls marked as such)

- **`-- →` text format (my call).** Expected values are written CLI-style
  (`{res: 5, …}`, `POINT (-0.902 -0.000)`), while `::VARCHAR` gives quoted struct
  keys and full precision. The comparator normalises both sides: it strips quotes
  around struct keys and compares numbers token by token, at the precision
  written in the expected text (`-0.902` matches `-0.90213…`, `-0.000` matches
  `-1e-17`). `...` remains a wildcard. `LiveSql` renders live results as a
  DuckDB-style box table, the same shape as the documented outputs.
- **Shape vs `-- →` (my call).** A `-- →` value matches a 1×1 result. Any other
  shape is compared as rows joined by newline, columns by `, `. Anything else is
  `differs`, never `error`.
- **Unreleased functions on the public site (my call).** The site loads the
  published extension, but docs deploy from `main`. In the browser, a
  "function does not exist" / "no function matches" error is shown as
  ⏭ *not in the published build yet*, not ✗. In CI, against the fresh build,
  the same error is a real failure.
- **`spatial` in the browser (verified 2026-11-25).** `extensions.duckdb.org`
  (v1.4.0 and v2.0.0 `wasm_eh/spatial`) and `haybarn-extensions.query.farm`
  both return `access-control-allow-origin: *`, so on-demand
  `INSTALL spatial FROM 'https://extensions.duckdb.org'` is expected to work in
  the browser too. If it fails at build time, that's a blocker to raise, not a
  reason to skip silently.
- **Truth when docs and WASM disagree (my call).** For each ✗ found on first run,
  the builder re-runs the example on a native build of the same commit. If native
  agrees with WASM, the doc is wrong: fix the expected output in this PR and list
  it. If native agrees with the doc, it's a WASM bug: list it in the PR.
- **CI on landing (maintainer's call):** known WASM bugs go in a committed
  known-failures file (example id + reason). A listed example must still fail.
  If it starts passing, CI fails with "remove it from the list", so the file
  can't go stale. Nothing else is quarantined.
- **Replace `WasmDeployedTest.yml` (maintainer's call).** Today it runs the
  third-party `haybarn-extension-wasm-tester` weekly against the published
  artifact, through `scripts/test-wasm.sh` and `scripts/wasm-tester-patch.mjs`,
  which work around three upstream bugs in that tester. We keep what it is for:
  a scheduled check that the *published* artifact still works, with no push
  needed. The tester itself goes. New `WasmDeployedTest.yml`: weekly plus
  `workflow_dispatch`. It downloads the published `wasm_eh` file from haybarn
  (hard-fails if it 404s), then runs our own `wasm-sqllogictest` action with
  `extension-file:` on `test/sql` and `docs/**/*.md` from the **released
  tag**, so it tests what users actually have. The action and runner come from
  `main`. The eh/mvp matrix goes, because the 2.x engine has a single bundle.
  Delete `scripts/test-wasm.sh` and `scripts/wasm-tester-patch.mjs`. Point the
  `make test_wasm` / `test_wasm_local` targets at the action's `run.mjs`, run
  locally on the published file.

## Left to the builder

- Package name, internal file layout, and how the action finds the package code
  (vendor a built copy into the action vs. relative path in this repo) — must
  still work when the action is used from another repo.
- Exact engine bootstrap in the browser (worker/bundle URLs from jsDelivr vs.
  bundling via Vite) and handling the `?build=` URL (needs
  `allowUnsignedExtensions` for non-catalog builds).
- Visual styling, within VitePress's default theme variables (light/dark).
- Fixing doc examples that turn out to be wrong when first run — list them in
  the PR rather than silently changing the expected output.

## Engine decision (2026-11-25, maintainer: "use something safer than haybarn, don't reinvent")

Use the **official `@duckdb/duckdb-wasm`**, pinned to `1.33.1-dev119.0`, for the
site, the status page and the CI action. That release runs DuckDB v2.0.0. With
it, `INSTALL duck_dggs FROM community` loads v0.2.2 from
`community-extensions.duckdb.org` (official DuckDB now serves community WASM
builds for v2.0.x), `spatial` installs from the default repo, and a raw unsigned
v2.0.0 `wasm_eh` build loads with `allowUnsignedExtensions`. Haybarn is dropped
everywhere, including the existing `wasm-test` job's engine.

Constraint: an extension built for v2.0.0 refuses to load in v2.0.1
("built specifically for DuckDB version 'v2.0.0'"). The engine's DuckDB
version must equal CI's `duckdb_version`, so the two are bumped together.
`latest` on npm is a v1.5.4 engine and `next` is already v2.0.1, so the version
is always pinned exactly. "Published" in this doc now means
`community-extensions.duckdb.org`, not haybarn.
