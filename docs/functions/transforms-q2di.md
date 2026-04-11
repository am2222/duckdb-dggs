# Transforms -- from Q2DI

All `q2di_to_*` functions take a quad number (`UBIGINT`), integer i and j indices (`BIGINT`), and a resolution integer.

## q2di_to_geo

### Signatures

```sql
GEOMETRY q2di_to_geo (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
GEOMETRY q2di_to_geo (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Returns the geographic centre of the cell as a `GEOMETRY` POINT.

### Example

```sql
SELECT q2di_to_geo(3, 10, 10, 5);
-- → POINT (-0.902 -0.000)
```

## q2di_to_seqnum

### Signatures

```sql
UBIGINT q2di_to_seqnum (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
UBIGINT q2di_to_seqnum (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DI cell coordinate to the corresponding sequential cell index.

### Example

```sql
SELECT q2di_to_seqnum(3, 10, 10, 5);
-- → 2380
```

## q2di_to_q2di

### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2di_to_q2di (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2di_to_q2di (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Re-encodes a Q2DI cell coordinate through a full round-trip. Useful for normalisation and validation.

### Example

```sql
SELECT q2di_to_q2di(3, 10, 10, 5);
-- → {'quad': 3, 'i': 10, 'j': 10}
```

## q2di_to_q2dd

### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2di_to_q2dd (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2di_to_q2dd (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DI cell coordinate to Q2DD (continuous coordinates within the quad).

### Example

```sql
SELECT q2di_to_q2dd(3, 10, 10, 5);
-- → {'quad': 3, 'x': 0.156, 'y': 0.271}
```

## q2di_to_projtri

### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2di_to_projtri (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2di_to_projtri (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DI cell coordinate to PROJTRI (icosahedron triangle + projected x/y).

### Example

```sql
SELECT q2di_to_projtri(3, 10, 10, 5);
-- → {'tnum': 7, 'x': 0.688, 'y': 0.0}
```

## q2di_to_plane

### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) q2di_to_plane (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) q2di_to_plane (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DI cell coordinate to unfolded icosahedron plane coordinates.

### Example

```sql
SELECT q2di_to_plane(3, 10, 10, 5);
-- → {'x': 2.313, 'y': 1.732}
```
