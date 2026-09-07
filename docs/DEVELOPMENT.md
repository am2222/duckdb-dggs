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

## DuckDB v2 compatibility

The sources build against both the DuckDB v1.5 line and the upcoming v2
(`v2.0-cyanoptera`). The differences are confined to
`src/include/duck_dggs_compat.hpp`, which detects the version at compile time
(`__has_include("duckdb/common/vector/list_vector.hpp")`) and provides shims
for the calls that changed: writable vector accessors (`MutableData`,
`MutableValidity`, `ListChildMutable`, `ListEntriesMutable`), the count-less
`ToUnifiedFormat` / `Flatten` (`ToUnified`, `FlattenVector`), constant
references (`ReferenceValue`), fallible-function marking (`Fallible`) and the
SQL macro table (`SqlMacro` / `MakeMacroInfo`). New code should go through
those shims rather than calling `FlatVector::GetData` etc. directly for writes.

CI builds a **v2 canary** (`duckdb-next-build` in
`MainDistributionPipeline.yml`) alongside the stable build. Its artifacts carry
a `-duckdb-next` suffix and are never attached to a GitHub release. The
community-extensions repo builds every extension against the same v2 branch,
so a red canary here is a failure that would show up there.

Note that DuckDB v2 rejects the single-arrow lambda syntax (`x -> expr`) by
default; tests use `lambda x: expr`, which both versions accept.

## Releasing

Releases are cut by [release-please](https://github.com/googleapis/release-please)
from conventional commits on `main` (`.github/workflows/release-please.yml`):

1. Merging the release PR creates the tag and GitHub release. The version is
   stamped into `CMakeLists.txt` and `description.yml` via the
   `x-release-please-version` markers.
2. The tag push triggers `MainDistributionPipeline.yml`, which builds all
   platforms and attaches the binaries to the release.
3. The same release-please run then opens a PR against
   `duckdb/community-extensions` (via the `am2222/community-extensions` fork)
   that copies this repo's `description.yml` with `repo.ref` set to the release
   commit. `description.yml` here is the source of truth for the listing.

Steps 2 and 3 need the `RELEASE_PLEASE_TOKEN` repository secret: a classic
personal access token with the `public_repo` scope and push access to the
fork. Tags created with the default `GITHUB_TOKEN` do not trigger other
workflows, so without it the release would ship with no binaries and no
community-extensions PR.
