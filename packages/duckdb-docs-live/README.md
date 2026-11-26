# duckdb-docs-live

Runnable SQL examples for a DuckDB extension's VitePress docs, executed in the
reader's browser with DuckDB-WASM, plus a `/wasm-status` page and a CI action
that run the same examples against the WASM build.

- A Run button on every ` ```sql ` fence. The engine and the extension load
  lazily on the first click, never on page load.
- Results are compared with the documented output (`-- →` comments or a
  following box-drawing table) as `VARCHAR` strings.
- `/wasm-status` runs every example and lists the ones that do not match.
- `.github/actions/wasm-sqllogictest` (this repo) runs the same examples, and
  your sqllogictest files, in Node against a built `.wasm` file.

Engine: official `@duckdb/duckdb-wasm@1.33.1-dev119.0` (DuckDB v2.0.0). The
extension must be the build for the same DuckDB version.

## Fences

| Fence | Behaviour |
|---|---|
| ` ```sql ` | Run button; the result is compared with the documented one. |
| ` ```sql run ` | Run button and shown result, no comparison. |
| ` ```sql norun ` | Plain code block, never run. |

Each example ends as `match`, `differs`, `error`, `skipped`, `shown` or
`unreleased` (it uses a function the published build does not have yet).

## Adopt it in another extension

Paths below are from a repo with `docs/` as the VitePress root, as in
duckdb-dggs.

1. **Dependency.** Add the package and its peers to `docs/package.json`. It is
   not published to npm; vendor `packages/duckdb-docs-live` or reference it with
   `file:`:

   ```json
   "dependencies": {
     "duckdb-docs-live": "file:../packages/duckdb-docs-live",
     "@duckdb/duckdb-wasm": "1.33.1-dev119.0"
   }
   ```

   `docs.yml` should also trigger on `packages/duckdb-docs-live/**` if the
   package lives in your repo.

2. **`docs/.vitepress/config.ts`.** Register the markdown plugin, keep the
   engine out of Vite's dependency pre-bundling, and exclude non-page markdown:

   ```ts
   import { defineConfig } from 'vitepress'
   import { liveSql } from 'duckdb-docs-live/vitepress/plugin'

   export default defineConfig({
     srcExclude: ['superpowers/**'],      // design notes etc.; also listed in the data loader
     markdown: { config: (md) => md.use(liveSql) },
     vite: {
       optimizeDeps: { exclude: ['@duckdb/duckdb-wasm'] },
       // needed when the package is symlinked (`file:`)
       resolve: { dedupe: ['vue', '@duckdb/duckdb-wasm'] },
     },
   })
   ```

3. **`docs/.vitepress/live-sql.config.ts`.** Which extension the pages load:

   ```ts
   export default {
     extension: 'duck_dggs',
     engineVersion: '1.33.1-dev119.0',
     repositories: ['https://extensions.duckdb.org'],   // for `LOAD spatial` etc.
   }
   ```

4. **`docs/.vitepress/theme/index.ts`.** Register the components and provide the
   config (and the known-failures list, see below):

   ```ts
   import DefaultTheme from 'vitepress/theme'
   import type { Theme } from 'vitepress'
   import { LiveSql, WasmStatus, provideLiveSqlConfig } from 'duckdb-docs-live/vitepress'
   import liveSqlConfig from '../live-sql.config'
   import knownFailures from '../../wasm-known-failures.json'

   export default {
     extends: DefaultTheme,
     enhanceApp({ app }) {
       app.component('LiveSql', LiveSql)
       app.component('WasmStatus', WasmStatus)
       provideLiveSqlConfig(app, { ...liveSqlConfig, knownFailures })
     },
   } satisfies Theme
   ```

5. **`/wasm-status` page.** A data loader and a page. The loader reads every
   page with the same extractor and page ids as the CI action:

   ```ts
   // docs/.vitepress/examples.data.mts
   import { createExamplesLoader } from 'duckdb-docs-live/vitepress/examples-loader'

   export default createExamplesLoader('**/*.md', { exclude: ['superpowers/**', 'node_modules/**'] })
   ```

   ```md
   ---
   title: WASM status
   ---

   <script setup>
   import { data } from './.vitepress/examples.data.mts'
   </script>

   # WASM status

   <WasmStatus :data="data" />
   ```

   Add it to the sidebar.

6. **`docs/wasm-known-failures.json`.** `[{ "id": "<page>#<n>", "reason": "..." }]`,
   examples expected to fail in WASM. Start with `[]`. Entries must still fail:
   one that passes, or whose example is gone, fails CI.

7. **CI.** Test your own build in the workflow that builds the extension, and
   the published one on a schedule. See
   `.github/actions/wasm-sqllogictest/README.md` for all inputs.

   ```yaml
   - uses: am2222/duckdb-dggs/.github/actions/wasm-sqllogictest@main
     with:
       extension: my_extension
       artifact: my_extension-v2.0.0-extension-wasm_eh   # or extension-file: path/to.wasm
       tests: test/sql                                    # '' for docs only
       docs: docs
       known-failures: docs/wasm-known-failures.json
       # for a published build that may lag the docs:
       # published: 'true'
       # extension-source: https://community-extensions.duckdb.org/v2.0.0/wasm_eh/my_extension.duckdb_extension.wasm
   ```

   The job fails on any `differs` or `error`, on a known-failures problem, or
   when no example was found. Results are also written to the job summary.

## Testing an unreleased build

Serve the file with CORS (`npx http-server <dir> --cors`); its URL must end in
`.duckdb_extension.wasm`, and its host must be `localhost`, `127.0.0.1` or
`[::1]`, or it must be on the docs site's own origin (a relative URL is resolved
against the page). Builds load with unsigned extensions allowed, so any other
host is refused: "only local builds (localhost) or this site's own files can be
loaded". Then open any docs page with the build parameter:

- dev server (`npm run dev`): `#build=<url>`. The dev server returns a 500 for
  `?build=...wasm`.
- built site (`npm run build && npx vitepress preview`): `?build=<url>`.

An empty `?build=` clears the remembered build.

## Layout

- `src/*.mjs`: pure, dependency-free core (extraction, SQL splitting,
  comparison, status), imported by both the CI runner and the site.
- `vitepress/`: plugin, `LiveSql` and `WasmStatus` components, engine loader,
  data loader.

`npm test` runs the Node tests.
