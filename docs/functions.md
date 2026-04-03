# DuckDB DGGS Function Reference

This extension exposes [DGGRID](https://github.com/sahrk/DGGRID) Discrete Global Grid System (DGGS) operations as DuckDB scalar functions.
All transform functions convert between coordinate representations of DGGS cells at a given resolution.
Every function has a second overload that accepts an explicit [`dggs_params`](#dggs_params) struct for full control over the grid configuration.

## Coordinate Systems

| Symbol | Type | Description |
|--------|------|-------------|
| **GEO** | `GEOMETRY` (POINT) | Geographic lon/lat as a WKB point. `x` = longitude°, `y` = latitude°. |
| **SEQNUM** | `UBIGINT` | Global sequential cell index. |
| **Q2DI** | `STRUCT(quad UBIGINT, i BIGINT, j BIGINT)` | Quad number + integer (i, j) cell indices within the quad. |
| **Q2DD** | `STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE)` | Quad number + continuous (x, y) coordinates within the quad. |
| **PROJTRI** | `STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE)` | Icosahedron triangle number + (x, y) within the projected triangle. |
| **PLANE** | `STRUCT(x DOUBLE, y DOUBLE)` | Unfolded icosahedron plane coordinates. |

## Function Index

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| [`duck_dggs_version`](#duck_dggs_version) | — | `VARCHAR` | Extension and DGGRID library version. |
| [`dggs_params`](#dggs_params) | config fields | `STRUCT` | Construct a grid configuration struct. |
| [`geo_to_seqnum`](#geo_to_seqnum) | GEO, res | SEQNUM | GEO → sequential cell index. |
| [`geo_to_geo`](#geo_to_geo) | GEO, res | GEO | Snap a point to its DGGS cell centre. |
| [`geo_to_q2di`](#geo_to_q2di) | GEO, res | Q2DI | GEO → quad/integer index. |
| [`geo_to_q2dd`](#geo_to_q2dd) | GEO, res | Q2DD | GEO → quad/continuous coords. |
| [`geo_to_projtri`](#geo_to_projtri) | GEO, res | PROJTRI | GEO → projected triangle coords. |
| [`geo_to_plane`](#geo_to_plane) | GEO, res | PLANE | GEO → unfolded plane coords. |
| [`seqnum_to_geo`](#seqnum_to_geo) | SEQNUM, res | GEO | Sequential index → cell centre point. |
| [`seqnum_to_seqnum`](#seqnum_to_seqnum) | SEQNUM, res | SEQNUM | Re-index a cell at the same resolution. |
| [`seqnum_to_q2di`](#seqnum_to_q2di) | SEQNUM, res | Q2DI | Sequential index → quad/integer index. |
| [`seqnum_to_q2dd`](#seqnum_to_q2dd) | SEQNUM, res | Q2DD | Sequential index → quad/continuous coords. |
| [`seqnum_to_projtri`](#seqnum_to_projtri) | SEQNUM, res | PROJTRI | Sequential index → projected triangle coords. |
| [`seqnum_to_plane`](#seqnum_to_plane) | SEQNUM, res | PLANE | Sequential index → unfolded plane coords. |
| [`q2di_to_geo`](#q2di_to_geo) | Q2DI, res | GEO | Quad/integer index → cell centre point. |
| [`q2di_to_seqnum`](#q2di_to_seqnum) | Q2DI, res | SEQNUM | Quad/integer index → sequential index. |
| [`q2di_to_q2di`](#q2di_to_q2di) | Q2DI, res | Q2DI | Re-encode a cell in quad/integer form. |
| [`q2di_to_q2dd`](#q2di_to_q2dd) | Q2DI, res | Q2DD | Quad/integer index → quad/continuous coords. |
| [`q2di_to_projtri`](#q2di_to_projtri) | Q2DI, res | PROJTRI | Quad/integer index → projected triangle coords. |
| [`q2di_to_plane`](#q2di_to_plane) | Q2DI, res | PLANE | Quad/integer index → unfolded plane coords. |
| [`q2dd_to_geo`](#q2dd_to_geo) | Q2DD, res | GEO | Quad/continuous coords → cell centre point. |
| [`q2dd_to_seqnum`](#q2dd_to_seqnum) | Q2DD, res | SEQNUM | Quad/continuous coords → sequential index. |
| [`q2dd_to_q2di`](#q2dd_to_q2di) | Q2DD, res | Q2DI | Quad/continuous coords → quad/integer index. |
| [`q2dd_to_q2dd`](#q2dd_to_q2dd) | Q2DD, res | Q2DD | Re-encode a cell in quad/continuous form. |
| [`q2dd_to_projtri`](#q2dd_to_projtri) | Q2DD, res | PROJTRI | Quad/continuous coords → projected triangle coords. |
| [`q2dd_to_plane`](#q2dd_to_plane) | Q2DD, res | PLANE | Quad/continuous coords → unfolded plane coords. |
| [`projtri_to_geo`](#projtri_to_geo) | PROJTRI, res | GEO | Projected triangle coords → cell centre point. |
| [`projtri_to_seqnum`](#projtri_to_seqnum) | PROJTRI, res | SEQNUM | Projected triangle coords → sequential index. |
| [`projtri_to_q2di`](#projtri_to_q2di) | PROJTRI, res | Q2DI | Projected triangle coords → quad/integer index. |
| [`projtri_to_q2dd`](#projtri_to_q2dd) | PROJTRI, res | Q2DD | Projected triangle coords → quad/continuous coords. |
| [`projtri_to_projtri`](#projtri_to_projtri) | PROJTRI, res | PROJTRI | Re-encode a cell in projected triangle form. |
| [`projtri_to_plane`](#projtri_to_plane) | PROJTRI, res | PLANE | Projected triangle coords → unfolded plane coords. |

----

## Utility Functions

### duck_dggs_version

#### Signature

```sql
VARCHAR duck_dggs_version ()
```

#### Description

Returns the version string of the loaded duck_dggs extension together with the
version of the bundled [DGGRID](https://github.com/sahrk/DGGRID) library.

#### Example

```sql
SELECT duck_dggs_version();
```
```
┌───────────────────────┐
│  duck_dggs_version()  │
│        varchar        │
├───────────────────────┤
│ f7dbd3d (DGGRID 8.44) │
└───────────────────────┘
```

----

### dggs_params

#### Signature

```sql
STRUCT dggs_params (projection VARCHAR, aperture INTEGER, topology VARCHAR,
                   azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE)
```

Return type: `STRUCT(projection VARCHAR, aperture INTEGER, topology VARCHAR, azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE)`

#### Description

Constructs a grid-configuration struct that can be passed as the optional last
argument to any transform function, giving full control over the DGGS
orientation and topology.

| Parameter | Type | Description |
|-----------|------|-------------|
| `projection` | `VARCHAR` | Projection type. `'ISEA'` (Icosahedral Snyder Equal Area) or `'FULLER'`. |
| `aperture` | `INTEGER` | Cell aperture (ratio of parent to child cell area). `3`, `4`, or `7`. |
| `topology` | `VARCHAR` | Cell shape. `'HEXAGON'`, `'TRIANGLE'`, or `'DIAMOND'`. |
| `azimuth_deg` | `DOUBLE` | Rotation of the icosahedron around the pole axis, in degrees. |
| `pole_lat_deg` | `DOUBLE` | Latitude of the icosahedron pole, in degrees. Default ISEA: `58.28252559`. |
| `pole_lon_deg` | `DOUBLE` | Longitude of the icosahedron pole, in degrees. Default ISEA: `11.25`. |

The resolution is **not** part of `dggs_params`; it is always passed as a
separate `INTEGER` argument to the transform function.

#### Example

```sql
SELECT dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25);
```
```
┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)                                    │
│   struct(projection varchar, aperture integer, topology varchar, azimuth_deg double, pole_lat_deg double, pole_lon_deg double)   │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ {'projection': ISEA, 'aperture': 4, 'topology': HEXAGON, 'azimuth_deg': 0.0, 'pole_lat_deg': 58.28252559, 'pole_lon_deg': 11.25} │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

----

## Transform Functions — from GEO

All `geo_to_*` functions take a `GEOMETRY` POINT (longitude as `x`, latitude as `y`) and a resolution integer.

### geo_to_seqnum

#### Signatures

```sql
UBIGINT geo_to_seqnum (geom GEOMETRY, res INTEGER)
UBIGINT geo_to_seqnum (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Returns the sequential cell index (SEQNUM) of the DGGS cell that contains the
given point at the specified resolution. The sequential index is a compact
`UBIGINT` that uniquely identifies each cell within a resolution level and is
well-suited for joins and aggregations.

#### Example

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌────────────────────────────────────────────────────────┐
│ geo_to_seqnum(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5) │
│                         uint64                         │
├────────────────────────────────────────────────────────┤
│                                                   2380 │
└────────────────────────────────────────────────────────┘
```

With explicit grid parameters:

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5,
                     dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25));
```
```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ geo_to_seqnum(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5, dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)) │
│                                                       uint64                                                       │
├────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                                               2380 │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

----

### geo_to_geo

#### Signatures

```sql
GEOMETRY geo_to_geo (geom GEOMETRY, res INTEGER)
GEOMETRY geo_to_geo (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Snaps the input point to the centre of the DGGS cell that contains it at the
given resolution and returns the cell centre as a `GEOMETRY` POINT.
This is a convenient way to canonicalise geographic coordinates to the DGGS
grid.

#### Example

```sql
SELECT geo_to_geo('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌─────────────────────────────────────────────────────┐
│ geo_to_geo(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5) │
│                      geometry                       │
├─────────────────────────────────────────────────────┤
│ POINT (0 0)                                         │
└─────────────────────────────────────────────────────┘
```

----

### geo_to_q2di

#### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) geo_to_q2di (geom GEOMETRY, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) geo_to_q2di (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Returns the Q2DI coordinate of the DGGS cell containing the input point.
Q2DI encodes the cell as an icosahedron quad number (`quad`) together with
integer row (`i`) and column (`j`) indices within that quad.
The integer indices make Q2DI convenient for grid-based spatial analysis.

#### Example

```sql
SELECT geo_to_q2di('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌──────────────────────────────────────────────────────┐
│ geo_to_q2di(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5) │
│       struct(quad ubigint, i bigint, j bigint)       │
├──────────────────────────────────────────────────────┤
│ {'quad': 3, 'i': 10, 'j': 10}                        │
└──────────────────────────────────────────────────────┘
```

----

### geo_to_q2dd

#### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) geo_to_q2dd (geom GEOMETRY, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) geo_to_q2dd (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Returns the Q2DD coordinate of the DGGS cell containing the input point.
Q2DD encodes the cell as an icosahedron quad number (`quad`) together with
continuous floating-point (`x`, `y`) coordinates within that quad, providing
sub-cell precision.

#### Example

```sql
SELECT geo_to_q2dd('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌───────────────────────────────────────────────────────────────┐
│     geo_to_q2dd(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5)      │
│           struct(quad ubigint, x double, y double)            │
├───────────────────────────────────────────────────────────────┤
│ {'quad': 3, 'x': 0.16325419438415284, 'y': 0.282764559254556} │
└───────────────────────────────────────────────────────────────┘
```

----

### geo_to_projtri

#### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) geo_to_projtri (geom GEOMETRY, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) geo_to_projtri (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Returns the PROJTRI coordinate of the DGGS cell containing the input point.
PROJTRI encodes the cell as an icosahedron triangle number (`tnum`, 1–20)
together with continuous (`x`, `y`) coordinates within the projected triangle.

#### Example

```sql
SELECT geo_to_projtri('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌────────────────────────────────────────────────────────────────────┐
│      geo_to_projtri(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5)       │
│              struct(tnum ubigint, x double, y double)              │
├────────────────────────────────────────────────────────────────────┤
│ {'tnum': 7, 'x': 0.6734916112035678, 'y': -1.6238500936416828e-11} │
└────────────────────────────────────────────────────────────────────┘
```

----

### geo_to_plane

#### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) geo_to_plane (geom GEOMETRY, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) geo_to_plane (geom GEOMETRY, res INTEGER, params STRUCT)
```

#### Description

Returns the unfolded icosahedron plane coordinates of the DGGS cell containing
the input point. The plane coordinate system lays the 20 icosahedron faces flat
in a 2D plane and can be useful for visualisation and certain spatial analyses.

#### Example

```sql
SELECT geo_to_plane('POINT(0.0 0.0)'::GEOMETRY, 5);
```
```
┌───────────────────────────────────────────────────────┐
│ geo_to_plane(CAST('POINT(0.0 0.0)' AS "GEOMETRY"), 5) │
│              struct(x double, y double)               │
├───────────────────────────────────────────────────────┤
│ {'x': 3.326508388796432, 'y': 1.7320508075851162}     │
└───────────────────────────────────────────────────────┘
```

----

## Transform Functions — from SEQNUM

All `seqnum_to_*` functions take a sequential cell index (`UBIGINT`) and a resolution integer.

### seqnum_to_geo

#### Signatures

```sql
GEOMETRY seqnum_to_geo (seqnum UBIGINT, res INTEGER)
GEOMETRY seqnum_to_geo (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the geographic centre of the DGGS cell identified by the given
sequential index at the specified resolution, as a `GEOMETRY` POINT
(longitude as `x`, latitude as `y`).

#### Example

```sql
SELECT seqnum_to_geo(2380, 5);
```
```
┌─────────────────────────────────────────────────────┐
│               seqnum_to_geo(2380, 5)                │
│                      geometry                       │
├─────────────────────────────────────────────────────┤
│ POINT (-0.9020481125846749 -1.2067949803565235e-09) │
└─────────────────────────────────────────────────────┘
```

With explicit grid parameters:

```sql
SELECT seqnum_to_geo(2380, 5, dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25));
```
```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ seqnum_to_geo(2380, 5, dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)) │
│                                      geometry                                      │
├────────────────────────────────────────────────────────────────────────────────────┤
│ POINT (-0.9020481125846749 -1.2067949803565235e-09)                                │
└────────────────────────────────────────────────────────────────────────────────────┘
```

----

### seqnum_to_seqnum

#### Signatures

```sql
UBIGINT seqnum_to_seqnum (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_seqnum (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Re-encodes a sequential cell index at the same or a different resolution.
When the input and output resolution are identical this is an identity
operation, useful for validating or normalising cell indices.

#### Example

```sql
SELECT seqnum_to_seqnum(2380, 5);
```
```
┌───────────────────────────┐
│ seqnum_to_seqnum(2380, 5) │
│          uint64           │
├───────────────────────────┤
│                      2380 │
└───────────────────────────┘
```

----

### seqnum_to_q2di

#### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) seqnum_to_q2di (seqnum UBIGINT, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) seqnum_to_q2di (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its Q2DI representation (quad number + integer i/j indices).

#### Example

```sql
SELECT seqnum_to_q2di(2380, 5);
```
```
┌──────────────────────────────────────────┐
│         seqnum_to_q2di(2380, 5)          │
│ struct(quad ubigint, i bigint, j bigint) │
├──────────────────────────────────────────┤
│ {'quad': 3, 'i': 10, 'j': 10}            │
└──────────────────────────────────────────┘
```

----

### seqnum_to_q2dd

#### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_q2dd (seqnum UBIGINT, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_q2dd (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its Q2DD representation (quad number + continuous x/y coordinates within the quad).

#### Example

```sql
SELECT seqnum_to_q2dd(2380, 5);
```
```
┌─────────────────────────────────────────────────────┐
│               seqnum_to_q2dd(2380, 5)               │
│      struct(quad ubigint, x double, y double)       │
├─────────────────────────────────────────────────────┤
│ {'quad': 3, 'x': 0.15625, 'y': 0.27063293868263705} │
└─────────────────────────────────────────────────────┘
```

----

### seqnum_to_projtri

#### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_projtri (seqnum UBIGINT, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) seqnum_to_projtri (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its PROJTRI representation (icosahedron triangle number + continuous x/y within the projected triangle).

#### Example

```sql
SELECT seqnum_to_projtri(2380, 5);
```
```
┌────────────────────────────────────────────────────────────────────┐
│                     seqnum_to_projtri(2380, 5)                     │
│              struct(tnum ubigint, x double, y double)              │
├────────────────────────────────────────────────────────────────────┤
│ {'tnum': 7, 'x': 0.6874999999999999, 'y': -1.4224732503009818e-16} │
└────────────────────────────────────────────────────────────────────┘
```

----

### seqnum_to_plane

#### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) seqnum_to_plane (seqnum UBIGINT, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) seqnum_to_plane (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to unfolded icosahedron plane coordinates.

#### Example

```sql
SELECT seqnum_to_plane(2380, 5);
```
```
┌────────────────────────────────────────┐
│        seqnum_to_plane(2380, 5)        │
│       struct(x double, y double)       │
├────────────────────────────────────────┤
│ {'x': 2.3125, 'y': 1.7320508075688774} │
└────────────────────────────────────────┘
```

----

## Transform Functions — from Q2DI

All `q2di_to_*` functions take a quad number (`UBIGINT`), integer i and j indices (`BIGINT`), and a resolution integer.

### q2di_to_geo

#### Signatures

```sql
GEOMETRY q2di_to_geo (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
GEOMETRY q2di_to_geo (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the geographic centre of the DGGS cell identified by the given Q2DI
coordinates as a `GEOMETRY` POINT (longitude as `x`, latitude as `y`).

#### Example

```sql
SELECT q2di_to_geo(3, 10, 10, 5);
```
```
┌─────────────────────────────────────────────────────┐
│              q2di_to_geo(3, 10, 10, 5)              │
│                      geometry                       │
├─────────────────────────────────────────────────────┤
│ POINT (-0.9020481125846749 -1.2067949803565235e-09) │
└─────────────────────────────────────────────────────┘
```

----

### q2di_to_seqnum

#### Signatures

```sql
UBIGINT q2di_to_seqnum (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
UBIGINT q2di_to_seqnum (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DI cell coordinate to the corresponding sequential cell index.

#### Example

```sql
SELECT q2di_to_seqnum(3, 10, 10, 5);
```
```
┌──────────────────────────────┐
│ q2di_to_seqnum(3, 10, 10, 5) │
│            uint64            │
├──────────────────────────────┤
│                         2380 │
└──────────────────────────────┘
```

----

### q2di_to_q2di

#### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2di_to_q2di (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2di_to_q2di (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Re-encodes a Q2DI cell coordinate through a full round-trip via the DGGS engine.
Useful for normalisation and validation.

#### Example

```sql
SELECT q2di_to_q2di(3, 10, 10, 5);
```
```
┌──────────────────────────────────────────┐
│        q2di_to_q2di(3, 10, 10, 5)        │
│ struct(quad ubigint, i bigint, j bigint) │
├──────────────────────────────────────────┤
│ {'quad': 3, 'i': 10, 'j': 10}            │
└──────────────────────────────────────────┘
```

----

### q2di_to_q2dd

#### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2di_to_q2dd (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2di_to_q2dd (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DI cell coordinate to Q2DD (continuous floating-point coordinates within the quad).

#### Example

```sql
SELECT q2di_to_q2dd(3, 10, 10, 5);
```
```
┌─────────────────────────────────────────────────────┐
│             q2di_to_q2dd(3, 10, 10, 5)              │
│      struct(quad ubigint, x double, y double)       │
├─────────────────────────────────────────────────────┤
│ {'quad': 3, 'x': 0.15625, 'y': 0.27063293868263705} │
└─────────────────────────────────────────────────────┘
```

----

### q2di_to_projtri

#### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2di_to_projtri (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2di_to_projtri (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DI cell coordinate to PROJTRI (icosahedron triangle + projected x/y).

#### Example

```sql
SELECT q2di_to_projtri(3, 10, 10, 5);
```
```
┌────────────────────────────────────────────────────────────────────┐
│                   q2di_to_projtri(3, 10, 10, 5)                    │
│              struct(tnum ubigint, x double, y double)              │
├────────────────────────────────────────────────────────────────────┤
│ {'tnum': 7, 'x': 0.6874999999999999, 'y': -1.4224732503009818e-16} │
└────────────────────────────────────────────────────────────────────┘
```

----

### q2di_to_plane

#### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) q2di_to_plane (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) q2di_to_plane (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DI cell coordinate to unfolded icosahedron plane coordinates.

#### Example

```sql
SELECT q2di_to_plane(3, 10, 10, 5);
```
```
┌────────────────────────────────────────┐
│      q2di_to_plane(3, 10, 10, 5)       │
│       struct(x double, y double)       │
├────────────────────────────────────────┤
│ {'x': 2.3125, 'y': 1.7320508075688774} │
└────────────────────────────────────────┘
```

----

## Transform Functions — from Q2DD

All `q2dd_to_*` functions take a quad number (`UBIGINT`), continuous x and y coordinates (`DOUBLE`), and a resolution integer.

### q2dd_to_geo

#### Signatures

```sql
GEOMETRY q2dd_to_geo (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
GEOMETRY q2dd_to_geo (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Returns the geographic centre of the DGGS cell whose Q2DD coordinate is
nearest to the given quad/x/y values, as a `GEOMETRY` POINT.

#### Example

```sql
SELECT q2dd_to_geo(3, 0.15625, 0.27063, 5);
```
```
┌─────────────────────────────────────────────────────┐
│         q2dd_to_geo(3, 0.15625, 0.27063, 5)         │
│                      geometry                       │
├─────────────────────────────────────────────────────┤
│ POINT (-0.9022133192670736 -0.00010878104058049762) │
└─────────────────────────────────────────────────────┘
```

----

### q2dd_to_seqnum

#### Signatures

```sql
UBIGINT q2dd_to_seqnum (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT q2dd_to_seqnum (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DD coordinate to the corresponding sequential cell index.

#### Example

```sql
SELECT q2dd_to_seqnum(3, 0.15625, 0.27063, 5);
```
```
┌────────────────────────────────────────┐
│ q2dd_to_seqnum(3, 0.15625, 0.27063, 5) │
│                 uint64                 │
├────────────────────────────────────────┤
│                                   2380 │
└────────────────────────────────────────┘
```

----

### q2dd_to_q2di

#### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2dd_to_q2di (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) q2dd_to_q2di (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DD coordinate to Q2DI (integer cell indices within the quad).

#### Example

```sql
SELECT q2dd_to_q2di(3, 0.15625, 0.27063, 5);
```
```
┌──────────────────────────────────────────┐
│   q2dd_to_q2di(3, 0.15625, 0.27063, 5)   │
│ struct(quad ubigint, i bigint, j bigint) │
├──────────────────────────────────────────┤
│ {'quad': 3, 'i': 10, 'j': 10}            │
└──────────────────────────────────────────┘
```

----

### q2dd_to_q2dd

#### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_q2dd (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_q2dd (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Re-encodes a Q2DD coordinate through a full round-trip via the DGGS engine.

#### Example

```sql
SELECT q2dd_to_q2dd(3, 0.15625, 0.27063, 5);
```
```
┌──────────────────────────────────────────┐
│   q2dd_to_q2dd(3, 0.15625, 0.27063, 5)   │
│ struct(quad ubigint, x double, y double) │
├──────────────────────────────────────────┤
│ {'quad': 3, 'x': 0.15625, 'y': 0.27063}  │
└──────────────────────────────────────────┘
```

----

### q2dd_to_projtri

#### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_projtri (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) q2dd_to_projtri (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DD coordinate to PROJTRI (icosahedron triangle + projected x/y).

#### Example

```sql
SELECT q2dd_to_projtri(3, 0.15625, 0.27063, 5);
```
```
┌───────────────────────────────────────────────────────────────────┐
│              q2dd_to_projtri(3, 0.15625, 0.27063, 5)              │
│             struct(tnum ubigint, x double, y double)              │
├───────────────────────────────────────────────────────────────────┤
│ {'tnum': 7, 'x': 0.6875025449738174, 'y': 1.4693413184184434e-06} │
└───────────────────────────────────────────────────────────────────┘
```

----

### q2dd_to_plane

#### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) q2dd_to_plane (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) q2dd_to_plane (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a Q2DD coordinate to unfolded icosahedron plane coordinates.

#### Example

```sql
SELECT q2dd_to_plane(3, 0.15625, 0.27063, 5);
```
```
┌────────────────────────────────────────────────────┐
│       q2dd_to_plane(3, 0.15625, 0.27063, 5)        │
│             struct(x double, y double)             │
├────────────────────────────────────────────────────┤
│ {'x': 2.3124974550261825, 'y': 1.7320493382275588} │
└────────────────────────────────────────────────────┘
```

----

## Transform Functions — from PROJTRI

All `projtri_to_*` functions take an icosahedron triangle number (`UBIGINT`), continuous x and y coordinates within the projected triangle (`DOUBLE`), and a resolution integer.

### projtri_to_geo

#### Signatures

```sql
GEOMETRY projtri_to_geo (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
GEOMETRY projtri_to_geo (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Returns the geographic centre of the DGGS cell identified by the given PROJTRI
coordinates as a `GEOMETRY` POINT (longitude as `x`, latitude as `y`).

#### Example

```sql
SELECT projtri_to_geo(7, 0.6875, 0.0, 5);
```
```
┌─────────────────────────────────────────────────────┐
│          projtri_to_geo(7, 0.6875, 0.0, 5)          │
│                      geometry                       │
├─────────────────────────────────────────────────────┤
│ POINT (-0.9020481125846892 -1.2068163334689483e-09) │
└─────────────────────────────────────────────────────┘
```

----

### projtri_to_seqnum

#### Signatures

```sql
UBIGINT projtri_to_seqnum (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT projtri_to_seqnum (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a PROJTRI coordinate to the corresponding sequential cell index.

#### Example

```sql
SELECT projtri_to_seqnum(7, 0.6875, 0.0, 5);
```
```
┌──────────────────────────────────────┐
│ projtri_to_seqnum(7, 0.6875, 0.0, 5) │
│                uint64                │
├──────────────────────────────────────┤
│                                 2380 │
└──────────────────────────────────────┘
```

----

### projtri_to_q2di

#### Signatures

```sql
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) projtri_to_q2di (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, i BIGINT, j BIGINT) projtri_to_q2di (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a PROJTRI coordinate to Q2DI (quad number + integer i/j indices).

#### Example

```sql
SELECT projtri_to_q2di(7, 0.6875, 0.0, 5);
```
```
┌──────────────────────────────────────────┐
│    projtri_to_q2di(7, 0.6875, 0.0, 5)    │
│ struct(quad ubigint, i bigint, j bigint) │
├──────────────────────────────────────────┤
│ {'quad': 3, 'i': 10, 'j': 10}            │
└──────────────────────────────────────────┘
```

----

### projtri_to_q2dd

#### Signatures

```sql
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) projtri_to_q2dd (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(quad UBIGINT, x DOUBLE, y DOUBLE) projtri_to_q2dd (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a PROJTRI coordinate to Q2DD (quad number + continuous x/y within the quad).

#### Example

```sql
SELECT projtri_to_q2dd(7, 0.6875, 0.0, 5);
```
```
┌─────────────────────────────────────────────────────────────────┐
│               projtri_to_q2dd(7, 0.6875, 0.0, 5)                │
│            struct(quad ubigint, x double, y double)             │
├─────────────────────────────────────────────────────────────────┤
│ {'quad': 3, 'x': 0.15624999999999967, 'y': 0.27063293868263716} │
└─────────────────────────────────────────────────────────────────┘
```

----

### projtri_to_projtri

#### Signatures

```sql
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) projtri_to_projtri (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(tnum UBIGINT, x DOUBLE, y DOUBLE) projtri_to_projtri (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Re-encodes a PROJTRI coordinate through a full round-trip via the DGGS engine.

#### Example

```sql
SELECT projtri_to_projtri(7, 0.6875, 0.0, 5);
```
```
┌──────────────────────────────────────────┐
│  projtri_to_projtri(7, 0.6875, 0.0, 5)   │
│ struct(tnum ubigint, x double, y double) │
├──────────────────────────────────────────┤
│ {'tnum': 7, 'x': 0.6875, 'y': 0.0}       │
└──────────────────────────────────────────┘
```

----

### projtri_to_plane

#### Signatures

```sql
STRUCT(x DOUBLE, y DOUBLE) projtri_to_plane (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
STRUCT(x DOUBLE, y DOUBLE) projtri_to_plane (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts a PROJTRI coordinate to unfolded icosahedron plane coordinates.

#### Example

```sql
SELECT projtri_to_plane(7, 0.6875, 0.0, 5);
```
```
┌────────────────────────────────────────┐
│  projtri_to_plane(7, 0.6875, 0.0, 5)   │
│       struct(x double, y double)       │
├────────────────────────────────────────┤
│ {'x': 2.3125, 'y': 1.7320508075688772} │
└────────────────────────────────────────┘
```

----
