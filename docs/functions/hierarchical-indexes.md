# Hierarchical Index Conversions

These functions convert between the sequential cell index (SEQNUM) and alternative hierarchical indexing schemes. Hierarchical indices encode spatial locality, which can improve range queries and spatial sorting.

## Z-Order (Morton Code)

Available for aperture 3 and aperture 4 grids.

### seqnum_to_zorder

```sql
UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER, params STRUCT)
```

Converts a sequential cell index to its Z-order (Morton code) index.

```sql
SELECT seqnum_to_zorder(2380::UBIGINT, 5);
-- → 3688448094816436224
```

### zorder_to_seqnum

```sql
UBIGINT zorder_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT zorder_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

Converts a Z-order index back to a sequential cell index.

```sql
SELECT zorder_to_seqnum(3688448094816436224::UBIGINT, 5);
-- → 2380
```

## Z3 Index

Only available for aperture 3 hexagon grids. Requires passing a `dggs_params` struct with `aperture = 3`.

### seqnum_to_z3

```sql
UBIGINT seqnum_to_z3 (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_z3 (seqnum UBIGINT, res INTEGER, params STRUCT)
```

Converts a sequential cell index to its Z3 hierarchical index.

```sql
SELECT seqnum_to_z3(100::UBIGINT, 4,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 2994893752201379839
```

### z3_to_seqnum

```sql
UBIGINT z3_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT z3_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

Converts a Z3 index back to a sequential cell index.

```sql
SELECT z3_to_seqnum(2994893752201379839::UBIGINT, 4,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 100
```

## Z7 Index

Only available for aperture 7 hexagon grids. Requires passing a `dggs_params` struct with `aperture = 7`.

### seqnum_to_z7

```sql
UBIGINT seqnum_to_z7 (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_z7 (seqnum UBIGINT, res INTEGER, params STRUCT)
```

Converts a sequential cell index to its Z7 hierarchical index.

```sql
SELECT seqnum_to_z7(100::UBIGINT, 4,
    dggs_params('ISEA', 7, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 1162491653815009279
```

### z7_to_seqnum

```sql
UBIGINT z7_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT z7_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

Converts a Z7 index back to a sequential cell index.

```sql
SELECT z7_to_seqnum(1162491653815009279::UBIGINT, 4,
    dggs_params('ISEA', 7, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 100
```

## IGEO7 / Z7 Bit-Level Operations

These functions operate directly on the 64-bit packed IGEO7/Z7 index without
going through DGGRID. They are self-contained bit manipulation — no
`dggs_params` required — and can be composed with the aperture-7
`seqnum_to_z7` / `z7_to_seqnum` converters above.

Bit layout:

```
bits [63:60]  base cell  (4 bits, 0–11)
bits [59:57]  digit 1    (3 bits, 0–6; 7 = padding)
bits [56:54]  digit 2
...
bits  [2: 0]  digit 20
```

The compact string form (`igeo7_to_string`) stops before the first `7`, so
its length reflects the resolution. The verbose form (`igeo7_decode_str`,
SQL macro) shows all 20 slots for debugging.

> Bit-level logic, neighbour traversal, and the companion macros are ported
> from [`allixender/igeo7_duckdb`](https://github.com/allixender/igeo7_duckdb)
> (MIT-licensed). The Z7 core library is vendored as a git submodule at
> `third_party/igeo7_duckdb/src/z7`.

### igeo7_from_string

```sql
UBIGINT igeo7_from_string (s VARCHAR)
```

Parse a compact IGEO7/Z7 string into the packed 64-bit index.

```sql
SELECT igeo7_from_string('0800432');
-- → 612839406969683967
```

### igeo7_to_string

```sql
VARCHAR igeo7_to_string (idx UBIGINT)
```

Render the packed index as a compact string (stops before the first `7`).

```sql
SELECT igeo7_to_string(igeo7_from_string('0800432'));
-- → '0800432'
```

### igeo7_encode

```sql
UBIGINT igeo7_encode (base UTINYINT, d1 UTINYINT, …, d20 UTINYINT)
UBIGINT igeo7_encode (base INTEGER,  d1 INTEGER,  …, d20 INTEGER)
```

Pack a base cell and 20 three-bit digits into the canonical 64-bit layout.
Use `7` for every slot beyond the target resolution. Field values are
masked internally — no range check needed.

```sql
SELECT igeo7_to_string(
    igeo7_encode(8, 0,0,4,3,2, 7,7,7,7,7, 7,7,7,7,7, 7,7,7,7,7));
-- → '0800432'
```

### igeo7_encode_at_resolution

```sql
UBIGINT igeo7_encode_at_resolution (base, res INTEGER, d1..d20)
```

Encode then truncate to `res`, filling slots `(res+1)..20` with padding.

```sql
SELECT igeo7_to_string(
    igeo7_encode_at_resolution(8, 3,
        0,0,4,3,2, 7,7,7,7,7, 7,7,7,7,7, 7,7,7,7,7));
-- → '08004'
```

### igeo7_get_resolution

```sql
INTEGER igeo7_get_resolution (idx UBIGINT)
```

Resolution (0–20) of a packed index.

```sql
SELECT igeo7_get_resolution(igeo7_from_string('0800432'));
-- → 5
```

### igeo7_get_base_cell

```sql
UTINYINT igeo7_get_base_cell (idx UBIGINT)
```

Base cell ID (0–11).

```sql
SELECT igeo7_get_base_cell(igeo7_from_string('0800432'));
-- → 8
```

### igeo7_get_digit

```sql
UTINYINT igeo7_get_digit (idx UBIGINT, pos INTEGER)
```

Extract digit at position `pos` (1..20). Returns `7` (padding) when out of
range or beyond the cell's resolution.

```sql
SELECT igeo7_get_digit(igeo7_from_string('0800432'), 3);
-- → 4
```

### igeo7_parent

```sql
UBIGINT igeo7_parent (idx UBIGINT)
```

Parent index (one level up).

```sql
SELECT igeo7_to_string(igeo7_parent(igeo7_from_string('0800432')));
-- → '080043'
```

### igeo7_parent_at

```sql
UBIGINT igeo7_parent_at (idx UBIGINT, res INTEGER)
```

Ancestor at a specific resolution.

```sql
SELECT igeo7_to_string(igeo7_parent_at(igeo7_from_string('0800432'), 3));
-- → '08004'
```

### igeo7_get_neighbours

```sql
UBIGINT[] igeo7_get_neighbours (idx UBIGINT)
```

All 6 neighbours. Pentagon-adjacent directions return `UINT64_MAX` —
filter with `igeo7_is_valid`.

```sql
SELECT list_transform(
    igeo7_get_neighbours(igeo7_from_string('0800432')),
    x -> igeo7_to_string(x));
-- → [0800433, 0800651, 0800064, 0800436, 0800430, 0800655]
```

### igeo7_get_neighbour

```sql
UBIGINT igeo7_get_neighbour (idx UBIGINT, direction INTEGER)
```

Single neighbour in direction 1..6.

```sql
SELECT igeo7_to_string(
    igeo7_get_neighbour(igeo7_from_string('0800432'), 1));
-- → '0800433'
```

### igeo7_first_non_zero

```sql
INTEGER igeo7_first_non_zero (idx UBIGINT)
```

Position (1..20) of the first non-zero digit slot; 0 when padding-only.

### igeo7_is_valid

```sql
BOOLEAN igeo7_is_valid (idx UBIGINT)
```

`FALSE` only when the index equals the `UINT64_MAX` sentinel.

```sql
SELECT igeo7_is_valid(igeo7_from_string('0800432')),
       igeo7_is_valid(18446744073709551615::UBIGINT);
-- → true, false
```

### igeo7_decode_str

```sql
VARCHAR igeo7_decode_str (idx UBIGINT)   -- SQL macro
```

Verbose `base-d1.d2…d20` form showing all 20 slots (incl. padding).

```sql
SELECT igeo7_decode_str(32023330408103935::UBIGINT);
-- → '0-0.1.6.1.6.1.2.0.6.2.4.1.3.7.7.7.7.7.7.7'
```

### igeo7_string_parent

```sql
VARCHAR igeo7_string_parent (s VARCHAR)   -- SQL macro
```

Drop the last char of a compact IGEO7 string.

```sql
SELECT igeo7_string_parent('0800432');
-- → '080043'
```

### igeo7_string_local_pos

```sql
VARCHAR igeo7_string_local_pos (s VARCHAR)   -- SQL macro
```

Last char of a compact IGEO7 string (local position within the parent).

```sql
SELECT igeo7_string_local_pos('0800432');
-- → '2'
```

### igeo7_string_is_center

```sql
BOOLEAN igeo7_string_is_center (s VARCHAR)   -- SQL macro
```

`TRUE` when the last digit is `'0'` — the cell overlaps the parent centroid.

```sql
SELECT igeo7_string_is_center('080040'),
       igeo7_string_is_center('0800432');
-- → true, false
```

## WGS84 Geodetic ↔ Authalic Latitude

Authalic latitudes preserve area when projecting onto an equal-area
sphere of the same total surface area as the WGS84 ellipsoid. These
helpers remap **every vertex's latitude** of a `GEOMETRY` between
geodetic (the usual lon/lat on WGS84) and authalic — useful as a
pre-step before equal-area binning of lon/lat points (including IGEO7
cell statistics).

The mapping is the order-6 polynomial Fourier series from Karney 2022
([_On auxiliary latitudes_](https://arxiv.org/abs/2212.05818)). Only
the `Y` component of each coordinate is rewritten; X (and any Z/M) pass
through unchanged. Both functions support POINT, LINESTRING, POLYGON,
MULTI*, and GEOMETRYCOLLECTION inputs.

> The math is vendored from
> [`allixender/igeo7_duckdb`](https://github.com/allixender/igeo7_duckdb)
> at `src/auth/authalic.cpp` (via the `third_party/igeo7_duckdb`
> submodule).

### igeo7_geo_to_authalic

```sql
GEOMETRY igeo7_geo_to_authalic (geom GEOMETRY)
```

Convert each vertex's geodetic latitude (WGS84) to authalic latitude.
Identity at the equator and at both poles.

```sql
SELECT igeo7_geo_to_authalic('POINT (0 45)'::GEOMETRY);
-- → POINT (0 44.87170287343394)

SELECT igeo7_geo_to_authalic(
    'POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))'::GEOMETRY);
-- → POLYGON ((0 0, 10 0, 10 9.956198098935733, 0 9.956198098935733, 0 0))
```

### igeo7_authalic_to_geo

```sql
GEOMETRY igeo7_authalic_to_geo (geom GEOMETRY)
```

Inverse of `igeo7_geo_to_authalic`. Round-tripping
`igeo7_authalic_to_geo(igeo7_geo_to_authalic(g))` recovers `g` to
within ~1e-12° across the full latitude range.

```sql
SELECT igeo7_authalic_to_geo(
    igeo7_geo_to_authalic('POINT (-71.5 42.3)'::GEOMETRY)
);
-- → POINT (-71.5 42.3)
```

## Vertex 2DD

Vertex-based 2D coordinates with triangle metadata. Available for all grid configurations.

### seqnum_to_vertex2dd

```sql
STRUCT seqnum_to_vertex2dd (seqnum UBIGINT, res INTEGER)
STRUCT seqnum_to_vertex2dd (seqnum UBIGINT, res INTEGER, params STRUCT)
```

Return type: `STRUCT(keep BOOLEAN, vert_num INTEGER, tri_num INTEGER, x DOUBLE, y DOUBLE)`

Converts a sequential cell index to its VERTEX2DD representation.

```sql
SELECT seqnum_to_vertex2dd(2380::UBIGINT, 5);
```
```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                   seqnum_to_vertex2dd(CAST(2380 AS "UBIGINT"), 5)                   │
│     struct(keep boolean, vert_num integer, tri_num integer, x double, y double)     │
├─────────────────────────────────────────────────────────────────────────────────────┤
│ {'keep': true, 'vert_num': 3, 'tri_num': 1, 'x': 0.15625, 'y': 0.27063293868263705} │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### vertex2dd_to_seqnum

```sql
UBIGINT vertex2dd_to_seqnum (keep BOOLEAN, vert_num INTEGER, tri_num INTEGER,
                             x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT vertex2dd_to_seqnum (keep BOOLEAN, vert_num INTEGER, tri_num INTEGER,
                             x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

Converts VERTEX2DD coordinates back to a sequential cell index.

```sql
SELECT vertex2dd_to_seqnum(true, 3, 1, 0.15625, 0.27063293868263705, 5);
-- → 2380
```
