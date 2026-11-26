---
title: WASM status
---

<script setup>
import { data } from './.vitepress/examples.data.mts'
</script>

# WASM status

Every SQL example in these docs, run in your browser with DuckDB-WASM and
compared with its documented output. CI runs the same examples against each
build; for the same build the counts here and in CI are the same.

Add `?build=<url of a duck_dggs.duckdb_extension.wasm>` to the address to test a
build instead of the published extension.

<WasmStatus :data="data" />
