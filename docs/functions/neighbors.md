# Neighbors

## seqnum_neighbors

### Signatures

```sql
UBIGINT[] seqnum_neighbors (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_neighbors (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the sequential indices of all topologically adjacent cells. For hexagon grids, interior cells have 6 neighbors; pentagon cells (at icosahedron vertices) have 5. The result does not include the input cell itself.

The neighbor relationship is symmetric: if B is in `seqnum_neighbors(A)`, then A is in `seqnum_neighbors(B)`.

::: info
Not supported for `TRIANGLE` topology grids.
:::

### Example

```sql
SELECT seqnum_neighbors(2380::UBIGINT, 5);
```
```
┌──────────────────────────────────────────────────┐
│ seqnum_neighbors(CAST(2380 AS "UBIGINT"), 5)     │
│                   uint64[]                       │
├──────────────────────────────────────────────────┤
│ [2412, 2413, 2381, 2348, 2347, 2379]             │
└──────────────────────────────────────────────────┘
```

### Vectorised usage

The function is fully vectorised -- use it over a table column:

```sql
SELECT seqnum, seqnum_neighbors(seqnum, 5) AS neighbors
FROM (VALUES (2380::UBIGINT), (2381::UBIGINT), (100::UBIGINT)) t(seqnum);
```
