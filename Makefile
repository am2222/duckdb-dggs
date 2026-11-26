PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=duck_dggs
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# WASM tests: sqllogictest files + the SQL examples in docs/, run by
# .github/actions/wasm-sqllogictest in the official DuckDB-WASM engine (the same
# runner CI uses). Neither target builds anything; `make test` only covers the
# native build.
#   make test_wasm        the PUBLISHED community-extensions build
#   make test_wasm_local  the build from `make wasm_eh` (EXT_FILE=... to override)
WASM_ACTION := .github/actions/wasm-sqllogictest
WASM_ENGINE := @duckdb/duckdb-wasm@1.33.1-dev119.0
PUBLISHED_WASM_URL := https://community-extensions.duckdb.org/v2.0.0/wasm_eh/duck_dggs.duckdb_extension.wasm
PUBLISHED_WASM := build/wasm-published/duck_dggs.duckdb_extension.wasm
EXT_FILE ?= build/wasm_eh/extension/duck_dggs/duck_dggs.duckdb_extension.wasm
WASM_RUN := node $(WASM_ACTION)/run.mjs --extension $(EXT_NAME) --tests test/sql --docs docs \
	--known-failures docs/wasm-known-failures.json --repo https://extensions.duckdb.org

.PHONY: test_wasm test_wasm_local wasm_engine
wasm_engine:
	cd $(WASM_ACTION) && npm install --no-audit --no-fund --no-save web-worker@1.2.0 $(WASM_ENGINE)

test_wasm: wasm_engine
	mkdir -p build/wasm-published
	curl -fsSLo $(PUBLISHED_WASM) $(PUBLISHED_WASM_URL)
	$(WASM_RUN) --extension-file $(PUBLISHED_WASM) --extension-source $(PUBLISHED_WASM_URL) --published

test_wasm_local: wasm_engine
	$(WASM_RUN) --extension-file $(EXT_FILE)
