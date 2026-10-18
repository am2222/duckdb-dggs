# Parent / Child Hierarchy

Navigate the multi-resolution grid hierarchy. A cell at resolution `res` has a parent at `res - 1` and children at `res + 1`.

## seqnum_parent

### Signatures

```sql
UBIGINT seqnum_parent (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_parent (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the sequential index of the parent cell at resolution `res - 1` that contains the centre of the given cell. The resolution must be > 0.

### Example

```sql
SELECT seqnum_parent(2380::UBIGINT, 5);
```
```
┌───────────────────────────────────────────┐
│ seqnum_parent(CAST(2380 AS "UBIGINT"), 5) │
│                  uint64                   │
├───────────────────────────────────────────┤
│                                       599 │
└───────────────────────────────────────────┘
```

## seqnum_all_parents

### Signatures

```sql
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the sequential indices of **all** parent cells at resolution `res - 1` that touch the given cell. Interior cells typically have 1 parent; cells on a parent-cell boundary may touch 2 or 3 parents. The primary (containing) parent is always the first element.

This is useful for identifying boundary cells that belong to multiple parent regions.

### Example

```sql
-- Cell 2412 sits on a parent boundary, touching 3 parents
SELECT seqnum_all_parents(2412::UBIGINT, 5);
```
```
┌────────────────────────────────────────────────┐
│ seqnum_all_parents(CAST(2412 AS "UBIGINT"), 5) │
│                    uint64[]                    │
├────────────────────────────────────────────────┤
│ [615, 599, 616]                                │
└────────────────────────────────────────────────┘
```

```sql
-- Interior cell has just 1 parent
SELECT seqnum_all_parents(2380::UBIGINT, 5);
-- → [599]
```

## seqnum_children

### Signatures

```sql
UBIGINT[] seqnum_children (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_children (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the sequential indices of all child cells at resolution `res + 1` that belong to the given cell. For aperture-4 hexagon grids this returns 7 children (1 interior + 6 boundary). The interior child (whose centre is inside the parent) is listed first.

::: tip
Boundary children are shared with adjacent parent cells. To check which parent "owns" a child, use `seqnum_parent` on the child.
:::

### Example

```sql
SELECT seqnum_children(599::UBIGINT, 4);
```
```
┌────────────────────────────────────────────┐
│ seqnum_children(CAST(599 AS "UBIGINT"), 4) │
│                  uint64[]                  │
├────────────────────────────────────────────┤
│ [2380, 2412, 2413, 2381, 2348, 2347, 2379] │
└────────────────────────────────────────────┘
```

### Round-trip consistency

A cell is always among its parent's children:

```sql
SELECT list_contains(
    seqnum_children(seqnum_parent(2380::UBIGINT, 5), 4),
    2380::UBIGINT
);
-- → true
```

## seqnum_index_parent

### Signatures

```sql
UBIGINT seqnum_index_parent (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_index_parent (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the cell's parent at resolution `res - 1` in the grid's **hierarchical indexing system**: Z3 for aperture 3, ZOrder for aperture 4 and Z7 for aperture 7 (DGGRID v9's "indexing parent"). The resolution must be > 0.

The spatial `seqnum_parent` picks the coarser cell under the cell's centre, and a cell on a boundary can touch several (`seqnum_all_parents`). The index hierarchy is instead a strict tree: every cell has exactly one index parent, which is the parent its Z3/ZOrder/Z7 index is built from.

::: info
Only for single-aperture `HEXAGON` grids (not aperture sequences).
:::

### Example

```sql
SELECT seqnum_index_parent(2381::UBIGINT, 5);
```
```
┌───────────────────────────────────────────────┐
│ seqnum_index_parent(CAST(2381 AS UBIGINT), 5) │
│                    uint64                     │
├───────────────────────────────────────────────┤
│                                           599 │
└───────────────────────────────────────────────┘
```

## seqnum_index_children

### Signatures

```sql
UBIGINT[] seqnum_index_children (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_index_children (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the cell's children at resolution `res + 1` in the grid's hierarchical indexing system (the cells whose `seqnum_index_parent` is this cell): 4 for aperture 4, 3 for aperture 3 and 7 for aperture 7. Unlike `seqnum_children`, the index children of different cells never overlap, so taking them for every cell at one resolution lists each cell of the next resolution exactly once.

::: info
Only for single-aperture `HEXAGON` grids (not aperture sequences).
:::

### Example

```sql
-- the 4 index children vs the 7 spatial children on the default ISEA4H grid
SELECT seqnum_index_children(599::UBIGINT, 4) AS index_children,
       seqnum_children(599::UBIGINT, 4)       AS spatial_children;
```
```
┌──────────────────────────┬────────────────────────────────────────────┐
│      index_children      │              spatial_children              │
│         uint64[]         │                  uint64[]                  │
├──────────────────────────┼────────────────────────────────────────────┤
│ [2380, 2381, 2412, 2413] │ [2380, 2412, 2413, 2381, 2348, 2347, 2379] │
└──────────────────────────┴────────────────────────────────────────────┘
```
