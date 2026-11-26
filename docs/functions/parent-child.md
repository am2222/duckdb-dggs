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

In hexagon grids with aperture 3 or 4 steps many cells have their centre exactly on a parent's edge (aperture 4) or corner (aperture 3), so the centre is in two or three parents. Such a cell gets the parent that holds a point moved from the centre by a tiny fixed step (1e-4 of the cell radius, in a fixed direction in the grid's planar frame). The result is always one of the cell's [`seqnum_all_parents`](#seqnum_all_parents), and it is the same on every platform.

::: warning Changed in 0.3.0
Cells centred exactly on a parent edge or corner now get their parent by the fixed rule above, identically on every platform. Earlier versions could return either parent, depending on the platform (native and WASM disagreed for about 40 % of the cells of grids with aperture 3 or 4 steps). Cell 2412 at res 5, for example, has its centre on the edge between 599 and 615 and is now 615 everywhere. Cells whose centre is inside one parent are unchanged.
:::

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

```sql
-- Cell 2412's centre is on the edge between 599 and 615
SELECT seqnum_parent(2412::UBIGINT, 5);
-- → 615
```

## seqnum_all_parents

### Signatures

```sql
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the sequential indices of every parent cell at resolution `res - 1` that **touches** the cell (shares area, an edge or a corner): the containing parent (`seqnum_parent`) first, then the others in ascending order. An interior cell has 1 parent. A cell on a parent boundary has 3 in aperture 3 and 7 grids, and 4 in aperture 4 grids (it straddles 2 parents and touches 2 more at its corners).

This is useful for identifying boundary cells that belong to multiple parent regions.

::: warning Changed in 0.3.0
Results are now complete and identical on every platform. Earlier versions missed some of the parents that touch the cell only at a corner, which ones depended on the platform, and the parents came in no fixed order. For cell 2412 at res 5, for example, the 0.2.2 docs showed `[615, 599, 616]` and other platforms gave other lists, such as `[599, 616, 615]` or `[599, 598, 615]`; it is now `[615, 598, 599, 616]` everywhere. The first element follows the new tie rule of [`seqnum_parent`](#seqnum_parent).
:::

::: info Why corner contacts used to differ between platforms
In aperture 3, 4 and 7 hexagon grids a cell's corner can sit exactly on a parent's edge or corner. Cell 2412 at res 5 shares about half its area with each of 599 and 615, and touches 598 and 616 at a single corner each. Whether such a corner was counted was decided by floating-point rounding, so platforms disagreed: native gave `[599, 616, 615]` and WASM `[599, 598, 615]`. Now every parent that touches the cell is included, found by sampling a small ring of points around each corner and edge midpoint, so the result is the same everywhere.
:::

### Example

```sql
-- Cell 2412 straddles two parents and touches two more at its corners
SELECT seqnum_all_parents(2412::UBIGINT, 5);
```
```
┌────────────────────────────────────────────────┐
│ seqnum_all_parents(CAST(2412 AS "UBIGINT"), 5) │
│                    uint64[]                    │
├────────────────────────────────────────────────┤
│ [615, 598, 599, 616]                           │
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

Returns the cell's parent at resolution `res - 1` in the grid's **hierarchical indexing system** (DGGRID v9's "indexing parent"): Z3 for aperture 3 hexagons, ZORDER for aperture 4 hexagons and for diamonds, Z7 for aperture 7 hexagons and QUADTREE for triangles. The resolution must be > 0.

The spatial `seqnum_parent` picks the coarser cell under the cell's centre, and a cell on a boundary can touch several (`seqnum_all_parents`). The index hierarchy is instead a strict tree: every cell has exactly one index parent, which is the parent its hierarchical index is built from.

::: info
Not available on aperture-sequence grids. Diamond and triangle grids are supported from DGGRID 9.3; triangle grids only up to resolution 29.
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

Returns the cell's children at resolution `res + 1` in the grid's hierarchical indexing system (the cells whose `seqnum_index_parent` is this cell): 4 for aperture 4 (including diamonds and triangles), 3 for aperture 3 and 7 for aperture 7. Unlike `seqnum_children`, the index children of different cells never overlap, so taking them for every cell at one resolution lists each cell of the next resolution exactly once.

::: info
Not available on aperture-sequence grids. Diamond and triangle grids are supported from DGGRID 9.3; triangle grids only up to resolution 29.
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

On a triangle grid the QUADTREE children are the same 4 triangles as the spatial children:

```sql
SELECT seqnum_index_children(100::UBIGINT, 4, dggs_preset('ISEA4T'));
-- → [390, 391, 392, 456]
```
