# Transforms -- from GEO

All `geo_to_*` functions take a `GEOMETRY` POINT (longitude as `x`, latitude as `y`) and a resolution integer.

## geo_to_seqnum

### Signatures

```sql
UBIGINT geo_to_seqnum (geom GEOMETRY, res INTEGER)
UBIGINT geo_to_seqnum (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the sequential cell index (SEQNUM) of the DGGS cell that contains the given point at the specified resolution.

### Example

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → 2380
```

## geo_to_geo

### Signatures

```sql
GEOMETRY geo_to_geo (geom GEOMETRY, res INTEGER)
GEOMETRY geo_to_geo (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Snaps the input point to the centre of the DGGS cell that contains it and returns the cell centre as a `GEOMETRY` POINT.

### Example

```sql
SELECT geo_to_geo('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → POINT (0 0)
```

## geo_to_q2di

### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) geo_to_q2di (geom GEOMETRY, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) geo_to_q2di (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the Q2DI coordinate (quad number + integer i/j indices) of the cell containing the input point.

### Example

```sql
SELECT geo_to_q2di('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → {'quad': 3, 'i': 10, 'j': 10}
```

## geo_to_q2dd

### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) geo_to_q2dd (geom GEOMETRY, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) geo_to_q2dd (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the Q2DD coordinate (quad number + continuous x/y coordinates) of the cell containing the input point.

### Example

```sql
SELECT geo_to_q2dd('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → {'quad': 3, 'x': 0.163, 'y': 0.283}
```

## geo_to_projtri

### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) geo_to_projtri (geom GEOMETRY, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) geo_to_projtri (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the PROJTRI coordinate (icosahedron triangle number + projected x/y) of the cell containing the input point.

### Example

```sql
SELECT geo_to_projtri('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → {'tnum': 7, 'x': 0.673, 'y': 0.0}
```

## geo_to_plane

### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) geo_to_plane (geom GEOMETRY, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) geo_to_plane (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the unfolded icosahedron plane coordinates of the cell containing the input point.

### Example

```sql
SELECT geo_to_plane('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → {'x': 3.327, 'y': 1.732}
```
