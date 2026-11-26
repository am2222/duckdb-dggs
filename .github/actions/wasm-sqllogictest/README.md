# wasm-sqllogictest

Runs a DuckDB extension's sqllogictest files against its **WASM build**, in a
DuckDB-WASM engine under Node. duckdb/extension-ci-tools builds the WASM
extensions but skips their tests, so a WASM-only bug (a native library calling
`exit()`, different `long double` precision, a missing symbol) otherwise ships
unnoticed.

Each test file gets a fresh database. `require <your extension>` loads the
`.wasm` file under test; other required extensions (e.g. `spatial`) are
installed from the engine's repository or `repositories`, and the file is
skipped if none has them. Values are cast to `VARCHAR` inside the engine before
comparison, so they print exactly as in the native runner.

## Use in another extension repository

Add a job after the build in the workflow that calls `_extension_distribution.yml`:

```yaml
  wasm-test:
    needs: duckdb-stable-build          # your build job
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: am2222/duckdb-dggs/.github/actions/wasm-sqllogictest@main
        with:
          extension: my_extension
          artifact: my_extension-v2.0.0-extension-wasm_eh
```

The artifact is named `<extension>-<duckdb_version>-extension-wasm_eh` by
`_extension_distribution.yml`. Pin `@main` to a commit SHA if you want the
runner to stay fixed.

| Input | Default | |
|---|---|---|
| `extension` | (required) | Name used in `require <name>`. |
| `artifact` | | Artifact of this run holding `<extension>.duckdb_extension.wasm`. |
| `extension-file` | | Path to the `.wasm` instead of `artifact`. |
| `tests` | `test/sql` | Files or directories, one per line. |
| `include-slow` | `false` | Also run `.test_slow`. |
| `docs` | | Markdown files, directories or patterns (`docs/**/*.md`), one per line, whose SQL examples are run too. See below. |
| `known-failures` | | JSON `[{ "id", "reason" }]` of doc examples expected to fail. |
| `published` | `false` | Report doc examples using functions the engine lacks as `unreleased` instead of failing. |
| `extension-source` | file path | Text shown in the summary as the build under test (e.g. a download URL). Display only. |
| `engine-package` | `@duckdb/duckdb-wasm` | npm engine package. |
| `engine-version` | `1.33.1-dev119.0` | DuckDB v2.0.0 build. Must match the DuckDB version the extension was built for. |
| `bundle` | first available | The 2.x engines (the official `@duckdb/duckdb-wasm` 1.33.x dev builds and others) ship one `base` bundle; `eh` / `mvp` are DuckDB 1.x builds. |
| `repositories` | `https://extensions.duckdb.org` | Tried, after the engine's own, for other required extensions. |
| `timeout` | `600` | Seconds per test file. |

For a DuckDB v1.5 extension, use an engine built from DuckDB 1.5 with
`bundle: eh` and the `...-v1.5.x-extension-wasm_eh` artifact (or `mvp`).

The job fails if any file fails, or if no file ran at all (which usually means
the extension never loaded). Results also go to the job summary.

## Doc examples

With `docs`, the SQL fences in the markdown pages (` ```sql `; ` ```sql run `
runs and shows without comparing, ` ```sql norun ` never runs) are executed as
well, each page in a fresh database with the extension `LOAD`ed from the `.wasm`
file. An example's `-- →` comments or a following box-drawing table are its
expected result and are compared as `VARCHAR` strings. A page that does
`LOAD spatial` gets it installed from the engine's repository or `repositories`.

A directory is walked for `.md` files (skipping `node_modules`, `.vitepress` and
`superpowers`); a pattern such as `docs/**/*.md` walks the directory before its
first wildcard. Page names are paths relative to that directory, without `.md`.
When `docs` is given and `tests` is left empty, only the doc examples run.

Each example ends as `match`, `differs`, `error`, `skipped`, `shown` or
`unreleased`. The job summary gets a `### Doc examples` section naming the
build (`extension-source`), a row per non-matching example, and a count line.
The job fails on any `differs` or `error`, on a `known-failures` problem (an
entry that now passes or no longer exists), or if no example was found. Use
`published: true` when testing an already-published extension that may lag the
docs: examples that call missing functions become `unreleased`, not failures.

## Run locally

```sh
cd .github/actions/wasm-sqllogictest && npm install --no-save web-worker@1.2.0 @duckdb/duckdb-wasm@1.33.1-dev119.0 && cd -
node .github/actions/wasm-sqllogictest/run.mjs \
  --extension my_extension \
  --extension-file path/to/my_extension.duckdb_extension.wasm \
  --repo https://extensions.duckdb.org \
  --docs docs --known-failures docs/wasm-known-failures.json
```

To test a CI build: `gh run download <run-id> -n my_extension-v2.0.0-extension-wasm_eh -D wasm`.

## Supported sqllogictest features

`statement ok|error`, `query <types> [nosort|rowsort|valuesort]` (row-per-line,
value-per-line and `N values hashing to <md5>` results), expected errors as a
substring or `<REGEX>:` / `<!REGEX>:`, `require`, `require-env`,
`skipif|onlyif <system>` (this runner is `duckdb` and `wasm`), `mode skip|unskip`,
`loop`, `foreach`, `endloop`, `restart` and `halt`. A file using anything else,
or requiring a native-only gate such as `require notwindows`, is skipped and
listed as such. Numbers that differ only in rounding (relative 1e-9) match.

If the engine dies mid-file (the WASM module ran `exit()` or trapped), the
rest of that file is reported as not run, with the engine's last output.
