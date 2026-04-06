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
