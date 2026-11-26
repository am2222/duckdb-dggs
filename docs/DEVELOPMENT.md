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

## Testing the WASM build

`make test` only covers the native build. WASM-only bugs (a native library
calling `exit()`, different `long double` precision, a missing symbol) need the
WASM build run in a DuckDB-WASM engine. `.github/actions/wasm-sqllogictest` does
that, for the sqllogictest files and for the SQL examples in `docs/`:

```sh
make test_wasm         # the PUBLISHED build (community-extensions), test/sql + docs
make test_wasm_local   # the build from `make wasm_eh` (EXT_FILE=<path> to override)
```

Both install the engine (`@duckdb/duckdb-wasm@1.33.1-dev124.0`, DuckDB v2.0.1)
into the action directory and run its `run.mjs`; neither builds anything.
`test_wasm` downloads the published file to `build/wasm-published/` and passes
`--published`, so examples that use functions the published build lacks are
reported as `unreleased` instead of failing. `make test_wasm` runs this tree's `test/sql` and `docs` against the
published build, so it fails when the tree is ahead of the release (new or
changed functions); use `make test_wasm_local` for unreleased work. Requires Node 20+ and network
access. `WasmDeployedTest.yml` runs the same against the published build every
Monday, using the tests and docs of the released commit (`repo.ref` in
`description.yml`).

### Run buttons in the docs

SQL fences on the docs site have a Run button (DuckDB-WASM, loaded on the first
click). To try a build that is not published yet, serve it and point the site at
it with a `build` parameter:

```sh
npx http-server <dir> --cors      # <dir> holds duck_dggs.duckdb_extension.wasm
```

- The URL must end in `.duckdb_extension.wasm` and be served with CORS.
- Only a build served on this machine (`localhost`, `127.0.0.1`, `[::1]`) or a
  file from the docs site's own origin is accepted: the build is loaded with
  unsigned extensions allowed, so a shared link must not be able to point a
  reader's browser at a binary from anywhere else. A relative URL is resolved
  against the page.
- With the dev server (`cd docs && npm run dev`) use the hash form,
  `http://localhost:5173/duckdb-dggs/functions/utility#build=http://localhost:8080/duck_dggs.duckdb_extension.wasm`.
  The dev server answers `?build=...wasm` with a 500 (it treats the `.wasm` in
  the query as a module request).
- The query form, `?build=...`, works on a built site:
  `npm run build && npx vitepress preview`.

### /wasm-status

`/wasm-status` runs every example on the site in your browser and lists the
ones that do not match their documented output. It takes the same `build`
parameter. CI counts the same examples with the same ids (`--docs docs`).

### Known failures

`docs/wasm-known-failures.json` (`[{ "id": "<page>#<n>", "reason": "..." }]`)
lists examples expected to fail in WASM. They show as "known issue" on the site
and do not fail CI. The list is strict: an entry must still fail. When a listed
example starts passing, or its page or example disappears, CI fails until the
entry is removed.

## DuckDB version

The extension targets DuckDB v2.0 only: the `duckdb` submodule and the release
build pin `v2.0.1`, the version duckdb/community-extensions builds against.
Support for the v1.5 line has been dropped, so code calls the v2 vector API
directly (`FlatVector::GetDataMutable`, `ListVector::GetChildMutable`,
`Vector::ToUnifiedFormat(format)`, ...). The few registration helpers live in
`src/include/duck_dggs_registration.hpp`: `Fallible` (functions that can throw
must be marked, or v2 turns the error into an internal error),
`WithParameterNames` (v2 reads parameter names from the function signature,
not the `FunctionDescription`) and `SqlMacro` / `MakeMacroInfo`.

CI's release build (`duckdb-stable-build` in `MainDistributionPipeline.yml`)
uses the same DuckDB version and extension-ci-tools branch as
community-extensions' `build.yml`, so a red build here is a failure that would
show up there; bump both together when community-extensions moves.

Note that DuckDB v2 rejects the single-arrow lambda syntax (`x -> expr`) by
default; tests use `lambda x: expr`.

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
   that copies this repo's `description.yml` with `repo.ref` (built against
   DuckDB v2.0) set to the release commit. `description.yml` here is the source of truth for the listing.

Steps 2 and 3 need the `RELEASE_PAT` repository secret: a classic
personal access token with the `public_repo` scope and push access to the
fork. Tags created with the default `GITHUB_TOKEN` do not trigger other
workflows, so without it the release would ship with no binaries and no
community-extensions PR.
