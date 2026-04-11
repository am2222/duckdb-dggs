# Transforms -- from PROJTRI

All `projtri_to_*` functions take an icosahedron triangle number (`UBIGINT`), continuous x and y coordinates (`DOUBLE`), and a resolution integer.

## projtri_to_geo

### Signatures

```sql
GEOMETRY projtri_to_geo (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
GEOMETRY projtri_to_geo (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Returns the geographic centre of the nearest DGGS cell as a `GEOMETRY` POINT.

### Example

```sql
SELECT projtri_to_geo(7, 0.6875, 0.0, 5);
-- → POINT (-0.902 -0.000)
```

## projtri_to_seqnum

### Signatures

```sql
UBIGINT projtri_to_seqnum (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT projtri_to_seqnum (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a PROJTRI coordinate to the corresponding sequential cell index.

### Example

```sql
SELECT projtri_to_seqnum(7, 0.6875, 0.0, 5);
-- → 2380
```

## projtri_to_q2di

### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) projtri_to_q2di (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) projtri_to_q2di (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a PROJTRI coordinate to Q2DI (quad number + integer i/j indices).

### Example

```sql
SELECT projtri_to_q2di(7, 0.6875, 0.0, 5);
-- → {'quad': 3, 'i': 10, 'j': 10}
```

## projtri_to_q2dd

### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) projtri_to_q2dd (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) projtri_to_q2dd (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a PROJTRI coordinate to Q2DD (quad number + continuous x/y).

### Example

```sql
SELECT projtri_to_q2dd(7, 0.6875, 0.0, 5);
-- → {'quad': 3, 'x': 0.156, 'y': 0.271}
```

## projtri_to_projtri

### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) projtri_to_projtri (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) projtri_to_projtri (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Re-encodes a PROJTRI coordinate through a full round-trip.

### Example

```sql
SELECT projtri_to_projtri(7, 0.6875, 0.0, 5);
-- → {'tnum': 7, 'x': 0.688, 'y': 0.0}
```

## projtri_to_plane

### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) projtri_to_plane (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) projtri_to_plane (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a PROJTRI coordinate to unfolded icosahedron plane coordinates.

### Example

```sql
SELECT projtri_to_plane(7, 0.6875, 0.0, 5);
-- → {'x': 2.313, 'y': 1.732}
```
