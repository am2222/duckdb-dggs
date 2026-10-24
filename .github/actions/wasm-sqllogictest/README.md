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
| `engine-package` | `@haybarn/haybarn-wasm` | npm engine package. |
| `engine-version` | `2.0.0-rc5` | Must match the DuckDB version the extension was built for. |
| `bundle` | first available | `base` (2.x, wasm_eh), or `eh` / `mvp` (1.x). |
| `repositories` | `https://extensions.duckdb.org` | Tried, after the engine's own, for other required extensions. |
| `timeout` | `600` | Seconds per test file. |

For a DuckDB v1.5 extension, use e.g. `engine-version: 1.5.6-rc6` with
`bundle: eh` and the `...-v1.5.6-extension-wasm_eh` artifact (or `mvp`).

The job fails if any file fails, or if no file ran at all (which usually means
the extension never loaded). Results also go to the job summary.

## Run locally

```sh
cd .github/actions/wasm-sqllogictest && npm install && cd -
node .github/actions/wasm-sqllogictest/run.mjs \
  --extension my_extension \
  --extension-file path/to/my_extension.duckdb_extension.wasm \
  --repo https://extensions.duckdb.org
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
