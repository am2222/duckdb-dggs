# Grid Statistics

These functions return grid statistics for a given resolution without requiring a cell identifier.

## dggs_n_cells

### Signatures

```sql
UBIGINT dggs_n_cells (res INTEGER)
UBIGINT dggs_n_cells (res INTEGER, params STRUCT)
```

### Description

Returns the total number of cells at the given resolution.

### Example

```sql
SELECT dggs_n_cells(5);
```
```
┌─────────────────┐
│ dggs_n_cells(5) │
│     uint64      │
├─────────────────┤
│           10242 │
└─────────────────┘
```

## dggs_cell_area_km

### Signatures

```sql
DOUBLE dggs_cell_area_km (res INTEGER)
DOUBLE dggs_cell_area_km (res INTEGER, params STRUCT)
```

### Description

Returns the average cell area in km² at the given resolution.

### Example

```sql
SELECT dggs_cell_area_km(5);
```
```
┌──────────────────────┐
│ dggs_cell_area_km(5) │
│        double        │
├──────────────────────┤
│   49811.09587149303  │
└──────────────────────┘
```

## dggs_cell_dist_km

### Signatures

```sql
DOUBLE dggs_cell_dist_km (res INTEGER)
DOUBLE dggs_cell_dist_km (res INTEGER, params STRUCT)
```

### Description

Returns the average cell spacing (distance between cell centres) in km at the given resolution.

### Example

```sql
SELECT dggs_cell_dist_km(5);
```
```
┌──────────────────────┐
│ dggs_cell_dist_km(5) │
│        double        │
├──────────────────────┤
│   220.4266384815885  │
└──────────────────────┘
```

## dggs_cls_km

### Signatures

```sql
DOUBLE dggs_cls_km (res INTEGER)
DOUBLE dggs_cls_km (res INTEGER, params STRUCT)
```

### Description

Returns the characteristic length scale (CLS) in km at the given resolution.

### Example

```sql
SELECT dggs_cls_km(5);
```
```
┌────────────────────┐
│   dggs_cls_km(5)   │
│       double       │
├────────────────────┤
│ 251.84027008853553 │
└────────────────────┘
```

## dggs_res_info

### Signatures

```sql
STRUCT dggs_res_info (res INTEGER)
STRUCT dggs_res_info (res INTEGER, params STRUCT)
```

Return type: `STRUCT(res INTEGER, cells UBIGINT, area_km DOUBLE, spacing_km DOUBLE, cls_km DOUBLE)`

### Description

Returns all grid statistics for the given resolution as a single struct.

### Example

```sql
SELECT dggs_res_info(5);
```
```
┌────────────────────────────────────────────────────────────────────────────────────┐
│                                 dggs_res_info(5)                                   │
│      struct(res integer, cells ubigint, area_km double, ...)                       │
├────────────────────────────────────────────────────────────────────────────────────┤
│ {res: 5, cells: 10242, area_km: 49811.1, spacing_km: 220.4, cls_km: 251.8}        │
└────────────────────────────────────────────────────────────────────────────────────┘
```

## seqnum_cell_area_km

### Signatures

```sql
DOUBLE seqnum_cell_area_km (seqnum UBIGINT, res INTEGER)
DOUBLE seqnum_cell_area_km (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the true spherical area in km² of **one** cell, measured on the WGS84 authalic sphere. `dggs_cell_area_km` gives the grid average; individual cells differ from it, most visibly the 12 pentagons, and on non-equal-area projections such as `FULLER`. The areas of all cells at a resolution add up to the area of the sphere.

### Example

```sql
-- a hexagon, a pentagon, and the grid average at resolution 5
SELECT seqnum_cell_area_km(2380::UBIGINT, 5) AS hexagon,
       seqnum_cell_area_km(1::UBIGINT, 5)    AS pentagon,
       dggs_cell_area_km(5)                  AS average;
```
```
┌───────────────────┬────────────────────┬───────────────────┐
│      hexagon      │      pentagon      │      average      │
│      double       │       double       │      double       │
├───────────────────┼────────────────────┼───────────────────┤
│ 49815.21462915404 │ 42984.471856754506 │ 49811.09587149303 │
└───────────────────┴────────────────────┴───────────────────┘
```
