# Development

## Building

```sh
git submodule update --init --recursive
GEN=ninja make
```

For faster incremental builds, install ccache and ninja:

```sh
brew install ccache ninja
```

Main build outputs:

- `./build/release/duckdb` — DuckDB shell with the extension loaded
- `./build/release/extension/duck_dggs/duck_dggs.duckdb_extension` — distributable binary

## Running tests

```sh
make test
```

This runs the native DuckDB `unittest` binary against `test/sql/*.test`.

## Testing the deployed WASM build

`make test` only covers the native build. The WASM build produces an artifact
but doesn't verify that the **published** extension actually loads and runs in
WebAssembly. To check that, run the deployed WASM build against this repo's
sqllogictest suite via
[haybarn-extension-wasm-tester](https://github.com/Query-farm-haybarn/haybarn-extension-wasm-tester):

```sh
make test_wasm         # run the PUBLISHED tests against the live WASM artifact
make test_wasm_local   # run THIS working tree's tests against the live artifact
```

Both wrap `scripts/test-wasm.sh`, which clones the tester and
`haybarn-community-extensions` into `build/wasm-test/` (gitignored) and
auto-detects the deployed catalog version by probing R2 (no hardcoded version,
so it survives DuckDB version bumps). Requires Node 18+ and network access.

Pass extra flags to the tester after a literal `--`:

```sh
./scripts/test-wasm.sh -- --platform mvp --verbose
./scripts/test-wasm.sh -- --json report.json
```

Notes:

- `test_wasm` runs the tests **at the published community ref**, so it reports
  the state users actually get. Failures here mean the live deployment doesn't
  pass its own published tests yet.
- `test_wasm_local` runs your current `test/sql/*.test`. Because the deployed
  artifact lags your source, tests that reference not-yet-deployed functions
  will fail until the next release is published — that's expected.
