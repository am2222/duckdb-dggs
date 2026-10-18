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

## seqnum_k_ring

### Signatures

```sql
UBIGINT[] seqnum_k_ring (seqnum UBIGINT, k INTEGER, res INTEGER)
UBIGINT[] seqnum_k_ring (seqnum UBIGINT, k INTEGER, res INTEGER, params STRUCT)
```

### Description

Returns the cell and every cell within `k` neighbor steps of it. The input cell comes first, followed by ring 1, ring 2, … each sorted ascending. For a hexagon away from the pentagons the result has `1 + 3k(k+1)` cells; `k = 0` returns just the cell, and a `k` larger than the grid stops once every cell is included.

::: info
Not supported for `TRIANGLE` topology grids.
:::

### Example

```sql
SELECT seqnum_k_ring(2380::UBIGINT, 1, 5);
```
```
┌────────────────────────────────────────────┐
│ seqnum_k_ring(CAST(2380 AS UBIGINT), 1, 5) │
│                  uint64[]                  │
├────────────────────────────────────────────┤
│ [2380, 2347, 2348, 2379, 2381, 2412, 2413] │
└────────────────────────────────────────────┘
```
