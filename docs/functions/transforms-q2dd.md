# Transforms -- from Q2DD

All `q2dd_to_*` functions take a quad number (`UBIGINT`), continuous x and y coordinates (`DOUBLE`), and a resolution integer.

## q2dd_to_geo

### Signatures

```sql
GEOMETRY q2dd_to_geo (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
GEOMETRY q2dd_to_geo (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Returns the geographic centre of the nearest DGGS cell as a `GEOMETRY` POINT.

### Example

```sql
SELECT q2dd_to_geo(3, 0.15625, 0.27063, 5);
-- → POINT (-0.902 -0.000)
```

## q2dd_to_seqnum

### Signatures

```sql
UBIGINT q2dd_to_seqnum (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT q2dd_to_seqnum (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DD coordinate to the corresponding sequential cell index.

### Example

```sql
SELECT q2dd_to_seqnum(3, 0.15625, 0.27063, 5);
-- → 2380
```

## q2dd_to_q2di

### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2dd_to_q2di (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2dd_to_q2di (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DD coordinate to Q2DI (integer cell indices).

### Example

```sql
SELECT q2dd_to_q2di(3, 0.15625, 0.27063, 5);
-- → {'quad': 3, 'i': 10, 'j': 10}
```

## q2dd_to_q2dd

### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_q2dd (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_q2dd (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Re-encodes a Q2DD coordinate through a full round-trip.

### Example

```sql
SELECT q2dd_to_q2dd(3, 0.15625, 0.27063, 5);
-- → {'quad': 3, 'x': 0.156, 'y': 0.271}
```

## q2dd_to_projtri

### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_projtri (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_projtri (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DD coordinate to PROJTRI (icosahedron triangle + projected x/y).

### Example

```sql
SELECT q2dd_to_projtri(3, 0.15625, 0.27063, 5);
-- → {'tnum': 7, 'x': 0.688, 'y': 0.0}
```

## q2dd_to_plane

### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) q2dd_to_plane (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) q2dd_to_plane (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

### Description

Converts a Q2DD coordinate to unfolded icosahedron plane coordinates.

### Example

```sql
SELECT q2dd_to_plane(3, 0.15625, 0.27063, 5);
-- → {'x': 2.312, 'y': 1.732}
```
