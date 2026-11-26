# First-run triage notes (2026-11-25)

Run against the CI `duck_dggs-v2.0.0-extension-wasm_eh` artifact of run 42128558257 (main, f5e9bc7, v0.2.2 / DGGRID 9.15), compared with the native `osx_arm64` artifact of the same run on DuckDB v2.0.0.

## Doc fixed (native == WASM, doc was stale)
- `functions/coverage-geodesy#polygon_to_seqnums/1`: expected list now `[600, 601, 616, 617, 618, 633, 634]`.
- `functions/grid-statistics#seqnum_cell_area_km/1`: areas updated to the DGGRID 9.15 values (49815.21450548134, 42984.47187249072, 49811.09587149302).
- `functions/hierarchical-indexes#igeo7-z7-bit-level-operations/1`: `igeo7_from_string('0800432')` is `9233329214155915263`.
- `functions/hierarchical-indexes#igeo7-z7-bit-level-operations/10`: deprecated `x -> ...` lambda rewritten to `lambda x: ...` (native v2 also rejects the arrow form; the new syntax gives the documented output).
- `functions/parent-child#seqnum_all_parents/1`: expected now `[599, 616, 615]` (native). The old text said the primary parent is first and listed `[615, 599, 616]`; the page prose may deserve a check by the maintainer.

## WASM bug
- `functions/parent-child#seqnum_all_parents/1`: WASM returns `[599,598,615]`, native `[599, 616, 615]` (598 vs 616 is a different cell, not formatting). Listed in known failures.

## norun / run
- `functions/utility#duck_dggs_version/1`: fence marked `sql run` (shown, not compared) because the version string changes every release.

## Comparator false ✗
- `functions/parent-child#seqnum_index_children/1` (`[2380, 2381, 2412, 2413]` vs WASM `[2380,2381,2412,2413]`): resolved by comparator normalising comma spacing; no known-failure entry.

## Browser (Task 7, published duck_dggs v0.2.2 from community, engine 1.33.1-dev119.0 / DuckDB v2.0.0)
- `guide/getting-started#quick-examples/3` (`LOAD spatial; SELECT seqnum_to_boundary(2380, 5)`): **runs** in the browser. `spatial` installs on demand from the default repo (`https://extensions.duckdb.org/v2.0.0/wasm_eh/spatial.duckdb_extension.wasm`, 200) and the example shows ✓.
- `functions/coverage-geodesy#geo_distance_km/1`: ✓, but the published build returns `5364.66413006686` (one digit shorter than the documented `5364.664130066861`; the engine's own `5364.664130066861::DOUBLE::VARCHAR` round-trips, so it is the published extension's value, not formatting). Equal within the comparator's 1e-9 tolerance, so no doc change.
