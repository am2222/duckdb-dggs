# Transforms -- from SEQNUM

All `seqnum_to_*` functions take a sequential cell index (`UBIGINT`) and a resolution integer.

## seqnum_to_geo

### Signatures

```sql
GEOMETRY seqnum_to_geo (seqnum UBIGINT, res INTEGER)
GEOMETRY seqnum_to_geo (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the geographic centre of the cell as a `GEOMETRY` POINT (longitude as `x`, latitude as `y`).

### Example

```sql
SELECT seqnum_to_geo(2380, 5);
-- → POINT (-0.902 -0.000)
```

## seqnum_to_boundary

### Signatures

```sql
GEOMETRY seqnum_to_boundary (seqnum UBIGINT, res INTEGER)
GEOMETRY seqnum_to_boundary (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the cell boundary as a `GEOMETRY` POLYGON. The exterior ring is closed (first vertex repeated). For hexagon grids, the ring contains 7 points.

Requires the `spatial` extension for display.

### Example

```sql
LOAD spatial;
SELECT seqnum_to_boundary(2380, 5);
-- → POLYGON ((...))
```

## seqnum_to_seqnum

### Signatures

```sql
UBIGINT seqnum_to_seqnum (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_seqnum (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Round-trip validation: re-encodes a cell index. For valid inputs, returns the same value.

### Example

```sql
SELECT seqnum_to_seqnum(2380, 5);
-- → 2380
```

## seqnum_to_q2di

### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) seqnum_to_q2di (seqnum UBIGINT, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) seqnum_to_q2di (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a sequential cell index to its Q2DI representation.

### Example

```sql
SELECT seqnum_to_q2di(2380, 5);
-- → {'quad': 3, 'i': 10, 'j': 10}
```

## seqnum_to_q2dd

### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_q2dd (seqnum UBIGINT, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_q2dd (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a sequential cell index to its Q2DD representation.

### Example

```sql
SELECT seqnum_to_q2dd(2380, 5);
-- → {'quad': 3, 'x': 0.156, 'y': 0.271}
```

## seqnum_to_projtri

### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_projtri (seqnum UBIGINT, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_projtri (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a sequential cell index to its PROJTRI representation.

### Example

```sql
SELECT seqnum_to_projtri(2380, 5);
-- → {'tnum': 7, 'x': 0.688, 'y': 0.0}
```

## seqnum_to_plane

### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) seqnum_to_plane (seqnum UBIGINT, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) seqnum_to_plane (seqnum UBIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a sequential cell index to unfolded icosahedron plane coordinates.

### Example

```sql
SELECT seqnum_to_plane(2380, 5);
-- → {'x': 2.313, 'y': 1.732}
```
