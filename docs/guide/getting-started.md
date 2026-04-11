# Getting Started

## Installation

```sql
INSTALL duck_dggs FROM community;
LOAD duck_dggs;
```

## Quick Examples

### Geographic point to cell ID

Convert a geographic point to a cell ID at resolution 5 (default ISEA4H grid):

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → 2380
```

### Cell ID to cell centre

```sql
SELECT seqnum_to_geo(2380, 5);
-- → POINT (-0.902 -0.000)
```

### Cell boundary polygon

Requires the `spatial` extension:

```sql
LOAD spatial;
SELECT seqnum_to_boundary(2380, 5);
-- → POLYGON ((...))
```

### Grid statistics

```sql
SELECT dggs_n_cells(5);
-- → 10242

SELECT dggs_res_info(5);
-- → {res: 5, cells: 10242, area_km: 49811.1, spacing_km: 220.4, cls_km: 251.8}
```

### Neighbors

```sql
SELECT seqnum_neighbors(2380::UBIGINT, 5);
-- → [2412, 2413, 2381, 2348, 2347, 2379]
```

### Parent / Child

```sql
SELECT seqnum_parent(2380::UBIGINT, 5);
-- → 599

SELECT seqnum_children(599::UBIGINT, 4);
-- → [2380, 2412, 2413, 2381, 2348, 2347, 2379]
```

### Custom grid configuration

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 572
```

## Coordinate Systems

| Symbol | Type | Description |
|--------|------|-------------|
| **GEO** | `GEOMETRY` (POINT) | Geographic lon/lat as a WKB point. |
| **SEQNUM** | `UBIGINT` | Global sequential cell index. |
| **Q2DI** | `STRUCT(quad UBIGINT, i BIGINT, j BIGINT)` | Quad number + integer cell indices. |
| **Q2DD** | `STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE)` | Quad number + continuous coordinates. |
| **PROJTRI** | `STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE)` | Icosahedron triangle + projected coordinates. |
| **PLANE** | `STRUCT(x DOUBLE, y DOUBLE)` | Unfolded icosahedron plane coordinates. |
| **VERTEX2DD** | `STRUCT(keep BOOLEAN, vert_num INTEGER, tri_num INTEGER, x DOUBLE, y DOUBLE)` | Vertex-based 2D coordinate. |
