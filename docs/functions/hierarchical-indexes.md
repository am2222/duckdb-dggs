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
