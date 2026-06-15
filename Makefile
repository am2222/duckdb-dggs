PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=duck_dggs
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Test the *deployed* WASM build against the published @haybarn/haybarn-wasm
# engine (not covered by `make test`, which only runs the native unittest).
# Use `make test_wasm_local` to run THIS working tree's tests instead.
.PHONY: test_wasm test_wasm_local
test_wasm:
	./scripts/test-wasm.sh

test_wasm_local:
	./scripts/test-wasm.sh --local-tests
