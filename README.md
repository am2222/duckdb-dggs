<div align="center">
  <img src="duck_dggs_icon.svg" width="240" alt="duck_dggs"/>
  <h1>duck_dggs</h1>
  <p>A DuckDB extension for discrete global grid systems (DGGS) powered by <a href="https://github.com/sahrk/DGGRID">DGGRID v8</a>.<br/>
  Transforms geographic coordinates to and from DGGS cell identifiers across multiple coordinate reference frames.</p>
  <p><strong><a href="https://am2222.github.io/duckdb-dggs/">Documentation</a></strong></p>
</div>

---

## Quick start
This extension exposes [DGGRID](https://github.com/sahrk/DGGRID) Discrete Global Grid System (DGGS) operations as DuckDB scalar functions.


```sql
INSTALL duck_dggs FROM community;
LOAD duck_dggs;
```

```sql
-- Geographic point → cell ID at resolution 5 (default ISEA4H grid)
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5);
-- → 2380

-- Cell ID → cell centre point
SELECT seqnum_to_geo(2380, 5);
-- → POINT (-0.9020481125846749 -1.2067949803565235e-09)

-- Cell ID → cell boundary polygon (requires the spatial extension)
LOAD spatial;
SELECT seqnum_to_boundary(2380, 5);
-- → POLYGON ((0.095 0.667, -0.922 1.336, -1.916 0.669, -1.916 -0.669, -0.922 -1.336, 0.095 -0.667, 0.095 0.667))

-- Use a custom grid (ISEA3H — aperture 3 hexagons)
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 572
```

---

## Development

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for build and test instructions.

---

# DuckDB DGGS Function Reference

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
| **VERTEX2DD** | `STRUCT(keep BOOLEAN, vert_num INTEGER, tri_num INTEGER, x DOUBLE, y DOUBLE)` | Vertex-based 2D coordinate with triangle and vertex metadata. |

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
| [`seqnum_to_boundary`](#seqnum_to_boundary) | SEQNUM, res | GEO | Sequential index → cell boundary polygon. |
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
| [`dggs_n_cells`](#dggs_n_cells) | res | `UBIGINT` | Number of cells at a given resolution. |
| [`dggs_cell_area_km`](#dggs_cell_area_km) | res | `DOUBLE` | Cell area in km² at a given resolution. |
| [`dggs_cell_dist_km`](#dggs_cell_dist_km) | res | `DOUBLE` | Cell spacing in km at a given resolution. |
| [`dggs_cls_km`](#dggs_cls_km) | res | `DOUBLE` | Characteristic length scale in km. |
| [`dggs_res_info`](#dggs_res_info) | res | `STRUCT` | All grid statistics for a resolution. |
| [`seqnum_neighbors`](#seqnum_neighbors) | SEQNUM, res | `UBIGINT[]` | Topologically adjacent cells. |
| [`seqnum_parent`](#seqnum_parent) | SEQNUM, res | `UBIGINT` | Parent cell at res-1. |
| [`seqnum_all_parents`](#seqnum_all_parents) | SEQNUM, res | `UBIGINT[]` | All touching parent cells at res-1. |
| [`seqnum_children`](#seqnum_children) | SEQNUM, res | `UBIGINT[]` | Child cells at res+1. |
| [`seqnum_to_zorder`](#seqnum_to_zorder) | SEQNUM, res | `UBIGINT` | Sequential index to Z-order index. |
| [`zorder_to_seqnum`](#zorder_to_seqnum) | ZORDER, res | `UBIGINT` | Z-order index to sequential index. |
| [`seqnum_to_z3`](#seqnum_to_z3) | SEQNUM, res | `UBIGINT` | Sequential index to Z3 index (aperture 3). |
| [`z3_to_seqnum`](#z3_to_seqnum) | Z3, res | `UBIGINT` | Z3 index to sequential index. |
| [`seqnum_to_z7`](#seqnum_to_z7) | SEQNUM, res | `UBIGINT` | Sequential index to Z7 index (aperture 7). |
| [`z7_to_seqnum`](#z7_to_seqnum) | Z7, res | `UBIGINT` | Z7 index to sequential index. |
| [`seqnum_to_vertex2dd`](#seqnum_to_vertex2dd) | SEQNUM, res | VERTEX2DD | Sequential index to vertex 2DD coords. |
| [`vertex2dd_to_seqnum`](#vertex2dd_to_seqnum) | VERTEX2DD, res | `UBIGINT` | Vertex 2DD coords to sequential index. |
| [`igeo7_from_string`](#igeo7_from_string) | VARCHAR | `UBIGINT` | Parse compact IGEO7/Z7 string → packed index. |
| [`igeo7_to_string`](#igeo7_to_string) | Z7 | `VARCHAR` | Packed index → compact string (stops before first `7`). |
| [`igeo7_encode`](#igeo7_encode) | base, d1..d20 | `UBIGINT` | Pack base cell + 20 digit slots. |
| [`igeo7_encode_at_resolution`](#igeo7_encode_at_resolution) | base, res, d1..d20 | `UBIGINT` | Encode then truncate to resolution. |
| [`igeo7_get_resolution`](#igeo7_get_resolution) | Z7 | `INTEGER` | Resolution (0–20) of a packed index. |
| [`igeo7_get_base_cell`](#igeo7_get_base_cell) | Z7 | `UTINYINT` | Base cell ID (0–11). |
| [`igeo7_get_digit`](#igeo7_get_digit) | Z7, pos | `UTINYINT` | Extract the i-th digit (1–20). |
| [`igeo7_parent`](#igeo7_parent) | Z7 | `UBIGINT` | Parent index (one level up). |
| [`igeo7_parent_at`](#igeo7_parent_at) | Z7, res | `UBIGINT` | Ancestor at specific resolution. |
| [`igeo7_get_neighbours`](#igeo7_get_neighbours) | Z7 | `UBIGINT[]` | All 6 neighbours (invalid = `UINT64_MAX`). |
| [`igeo7_get_neighbour`](#igeo7_get_neighbour) | Z7, dir | `UBIGINT` | Single neighbour in direction 1–6. |
| [`igeo7_first_non_zero`](#igeo7_first_non_zero) | Z7 | `INTEGER` | Position of first non-zero digit slot. |
| [`igeo7_is_valid`](#igeo7_is_valid) | Z7 | `BOOLEAN` | `FALSE` when index equals `UINT64_MAX` sentinel. |
| [`igeo7_decode_str`](#igeo7_decode_str) | Z7 | `VARCHAR` | Verbose `base-d1.d2…d20` form showing all 20 slots. |
| [`igeo7_string_parent`](#igeo7_string_parent) | VARCHAR | `VARCHAR` | Drop last char of a compact IGEO7 string. |
| [`igeo7_string_local_pos`](#igeo7_string_local_pos) | VARCHAR | `VARCHAR` | Last char of a compact IGEO7 string. |
| [`igeo7_string_is_center`](#igeo7_string_is_center) | VARCHAR | `BOOLEAN` | True when last digit is `'0'` (center of parent). |

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
STRUCT dggs_params (projection VARCHAR, aperture INTEGER, topology VARCHAR,
                   azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE,
                   is_aperture_sequence BOOLEAN, aperture_sequence VARCHAR)
```

Return type: `STRUCT(projection VARCHAR, aperture INTEGER, topology VARCHAR, azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE, is_aperture_sequence BOOLEAN, aperture_sequence VARCHAR)`

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
| `is_aperture_sequence` | `BOOLEAN` | When `true`, uses a mixed-aperture sequence instead of a fixed aperture. Default: `false`. |
| `aperture_sequence` | `VARCHAR` | A string of digits (`'3'`, `'4'`, `'7'`) defining the aperture at each resolution level. Only used when `is_aperture_sequence` is `true`. |

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

```sql
-- Mixed-aperture grid (aperture 3 at res 1, then 4, then 3, then 7)
SELECT dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25, true, '3437');
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

### seqnum_to_boundary

#### Signatures

```sql
GEOMETRY seqnum_to_boundary (seqnum UBIGINT, res INTEGER)
GEOMETRY seqnum_to_boundary (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the boundary of the DGGS cell identified by the given sequential index
at the specified resolution as a `GEOMETRY` POLYGON.
The exterior ring is closed (the first vertex is repeated as the last), which
is required by the WKB/WKT standard.

For ISEA4H hexagon cells the ring contains **7 points** (6 distinct vertices +
1 closing repeat). The cell centre (from `seqnum_to_geo`) is guaranteed to lie
inside the returned polygon.

Requires the `spatial` extension (`LOAD spatial;`).

#### Example

```sql
SELECT seqnum_to_boundary(2380, 5);
```
```
┌────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    seqnum_to_boundary(2380, 5)                                     │
│                                            geometry                                                │
├────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ POLYGON ((-0.451 0.781, 0.451 0.781, 0.902 0.0, 0.451 -0.781, -0.451 -0.781, -0.902 0.0, -0.451 0.781)) │
└────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

With explicit grid parameters:

```sql
SELECT seqnum_to_boundary(2380, 5,
                          dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25));
```

#### Notes

- **No holes**: DGGS cells are convex and topologically simple — the returned polygon always has exactly one ring.
- **Resolution sensitivity**: cells at coarser resolutions cover a larger area; `ST_Area(seqnum_to_boundary(seqnum, 3))` > `ST_Area(seqnum_to_boundary(seqnum, 5))`.
- **Adjacency**: adjacent cells do not contain each other's centre points; use `ST_Contains` to verify containment.

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

## Grid Statistics

### dggs_n_cells

#### Signatures

```sql
UBIGINT dggs_n_cells (res INTEGER)
UBIGINT dggs_n_cells (res INTEGER, params STRUCT)
```

#### Description

Returns the total number of cells at the given resolution.

#### Example

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

----

### dggs_cell_area_km

#### Signatures

```sql
DOUBLE dggs_cell_area_km (res INTEGER)
DOUBLE dggs_cell_area_km (res INTEGER, params STRUCT)
```

#### Description

Returns the average cell area in km² at the given resolution.

#### Example

```sql
SELECT dggs_cell_area_km(5);
```
```
┌──────────────────────┐
│ dggs_cell_area_km(5) │
│        double        │
├──────────────────────┤
│    49811.09587149303  │
└──────────────────────┘
```

----

### dggs_cell_dist_km

#### Signatures

```sql
DOUBLE dggs_cell_dist_km (res INTEGER)
DOUBLE dggs_cell_dist_km (res INTEGER, params STRUCT)
```

#### Description

Returns the average cell spacing (distance between cell centres) in km at the given resolution.

#### Example

```sql
SELECT dggs_cell_dist_km(5);
```
```
┌──────────────────────┐
│ dggs_cell_dist_km(5) │
│        double        │
├──────────────────────┤
│    220.4266384815885  │
└──────────────────────┘
```

----

### dggs_cls_km

#### Signatures

```sql
DOUBLE dggs_cls_km (res INTEGER)
DOUBLE dggs_cls_km (res INTEGER, params STRUCT)
```

#### Description

Returns the characteristic length scale (CLS) in km at the given resolution.

#### Example

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

----

### dggs_res_info

#### Signatures

```sql
STRUCT dggs_res_info (res INTEGER)
STRUCT dggs_res_info (res INTEGER, params STRUCT)
```

Return type: `STRUCT(res INTEGER, cells UBIGINT, area_km DOUBLE, spacing_km DOUBLE, cls_km DOUBLE)`

#### Description

Returns all grid statistics for the given resolution as a single struct.

#### Example

```sql
SELECT dggs_res_info(5);
```
```
┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                                    dggs_res_info(5)                                                     │
│                  struct(res integer, cells ubigint, area_km double, spacing_km double, cls_km double)                   │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ {'res': 5, 'cells': 10242, 'area_km': 49811.09587149303, 'spacing_km': 220.4266384815885, 'cls_km': 251.84027008853553} │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

----

## Neighbors

### seqnum_neighbors

#### Signatures

```sql
UBIGINT[] seqnum_neighbors (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_neighbors (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the sequential indices of all topologically adjacent cells. For hexagon grids, interior cells have 6 neighbors; pentagon cells (at icosahedron vertices) have 5. The result does not include the input cell itself.

#### Example

```sql
SELECT seqnum_neighbors(2380::UBIGINT, 5);
```
```
┌──────────────────────────────────────────────────┐
│ seqnum_neighbors(CAST(2380 AS "UBIGINT"), 5)     │
│                   uint64[]                       │
├──────────────────────────────────────────────────┤
│ [2412, 2413, 2381, 2348, 2347, 2379]             │
└──────────────────────────────────────────────────┘
```

----

## Parent / Child Hierarchy

### seqnum_parent

#### Signatures

```sql
UBIGINT seqnum_parent (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_parent (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the sequential index of the parent cell at resolution `res - 1` that contains the centre of the given cell. The resolution must be > 0.

#### Example

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

----

### seqnum_all_parents

#### Signatures

```sql
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_all_parents (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the sequential indices of all parent cells at resolution `res - 1` that touch the given cell. Interior cells typically have 1 parent; cells on a parent-cell boundary may touch 2 or 3 parents. The primary (containing) parent is always the first element.

#### Example

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

----

### seqnum_children

#### Signatures

```sql
UBIGINT[] seqnum_children (seqnum UBIGINT, res INTEGER)
UBIGINT[] seqnum_children (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Returns the sequential indices of all child cells at resolution `res + 1` that belong to the given cell. For aperture-4 hexagon grids this returns 7 children (1 interior + 6 boundary).

#### Example

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

----

## Hierarchical Index Conversions

These functions convert between the sequential cell index and alternative hierarchical indexing schemes. Z-order and Z3/Z7 indices encode spatial locality, which can be useful for range queries and spatial sorting.

### seqnum_to_zorder

#### Signatures

```sql
UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_zorder (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its Z-order (Morton code) index. Available for aperture 3 and aperture 4 grids.

#### Example

```sql
SELECT seqnum_to_zorder(2380::UBIGINT, 5);
```
```
┌──────────────────────────────────────────────┐
│ seqnum_to_zorder(CAST(2380 AS "UBIGINT"), 5) │
│                    uint64                    │
├──────────────────────────────────────────────┤
│                          3688448094816436224  │
└──────────────────────────────────────────────┘
```

----

### zorder_to_seqnum

#### Signatures

```sql
UBIGINT zorder_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT zorder_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Z-order index back to a sequential cell index.

#### Example

```sql
SELECT zorder_to_seqnum(3688448094816436224::UBIGINT, 5);
-- → 2380
```

----

### seqnum_to_z3

#### Signatures

```sql
UBIGINT seqnum_to_z3 (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_z3 (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its Z3 hierarchical index. Only available for aperture 3 hexagon grids.

#### Example

```sql
SELECT seqnum_to_z3(100::UBIGINT, 4,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
```
```
┌─────────────────────┐
│       uint64        │
├─────────────────────┤
│ 2994893752201379839 │
└─────────────────────┘
```

----

### z3_to_seqnum

#### Signatures

```sql
UBIGINT z3_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT z3_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Z3 index back to a sequential cell index.

#### Example

```sql
SELECT z3_to_seqnum(2994893752201379839::UBIGINT, 4,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 100
```

----

### seqnum_to_z7

#### Signatures

```sql
UBIGINT seqnum_to_z7 (seqnum UBIGINT, res INTEGER)
UBIGINT seqnum_to_z7 (seqnum UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a sequential cell index to its Z7 hierarchical index. Only available for aperture 7 hexagon grids.

#### Example

```sql
SELECT seqnum_to_z7(100::UBIGINT, 4,
    dggs_params('ISEA', 7, 'HEXAGON', 0.0, 58.28252559, 11.25));
```
```
┌─────────────────────┐
│       uint64        │
├─────────────────────┤
│ 1162491653815009279 │
└─────────────────────┘
```

----

### z7_to_seqnum

#### Signatures

```sql
UBIGINT z7_to_seqnum (value UBIGINT, res INTEGER)
UBIGINT z7_to_seqnum (value UBIGINT, res INTEGER, params STRUCT)
```

#### Description

Converts a Z7 index back to a sequential cell index.

#### Example

```sql
SELECT z7_to_seqnum(1162491653815009279::UBIGINT, 4,
    dggs_params('ISEA', 7, 'HEXAGON', 0.0, 58.28252559, 11.25));
-- → 100
```

----

## IGEO7 / Z7 Bit-Level Operations

Functions in this section operate directly on the packed 64-bit IGEO7/Z7
index (hexagonal DGGS with 7-fold hierarchy, aperture 7). Bit layout:

```
bits [63:60]  base cell  (4 bits, values 0–11)
bits [59:57]  digit 1    (3 bits, values 0–6; 7 = padding / unused slot)
bits [56:54]  digit 2
...
bits  [2: 0]  digit 20
```

**Compact string form** (`igeo7_to_string`) is a 2-digit zero-padded base
cell followed by the significant digits, stopping before the first `7`:
`0800432` → base `08`, digits `0,0,4,3,2`, resolution 5.

**Verbose debug form** (`igeo7_decode_str`) shows all 20 slots:
`0-0.1.6.1.6.1.2.0.6.2.4.1.3.7.7.7.7.7.7.7`.

These functions are self-contained bit manipulation — they do not require a
`dggs_params` struct and always use the canonical IGEO7 configuration. They
compose cleanly with the aperture-7 `seqnum_to_z7` / `z7_to_seqnum` DGGRID
converters above when you need to move between sequential and bit-packed
representations.

> Bit-level logic, neighbour traversal, and the companion macros are ported
> from the upstream [`allixender/igeo7_duckdb`](https://github.com/allixender/igeo7_duckdb)
> extension (MIT-licensed). The Z7 core library sits at
> `third_party/igeo7_duckdb` as a git submodule.

----

### igeo7_from_string

#### Signature

```sql
UBIGINT igeo7_from_string (s VARCHAR)
```

#### Description

Parse a compact IGEO7/Z7 string (base cell + significant digits) into the
canonical 64-bit packed index.

#### Example

```sql
SELECT igeo7_from_string('0800432');
-- → 612839406969683967
```

----

### igeo7_to_string

#### Signature

```sql
VARCHAR igeo7_to_string (idx UBIGINT)
```

#### Description

Render a packed index as a compact string. Stops at the first padding slot
(`7`), so the length reflects the cell's resolution.

#### Example

```sql
SELECT igeo7_to_string(igeo7_from_string('0800432'));
-- → '0800432'
```

----

### igeo7_encode

#### Signatures

```sql
UBIGINT igeo7_encode (base UTINYINT, d1 UTINYINT, …, d20 UTINYINT)
UBIGINT igeo7_encode (base INTEGER, d1 INTEGER, …, d20 INTEGER)
```

#### Description

Pack a base cell (0–11) and exactly 20 three-bit digits (each 0–7) into the
canonical 64-bit index. Use `7` for every slot beyond the target resolution.
Values are masked to their field widths internally — no range check
required by callers. The `INTEGER` overload accepts unadorned SQL literals.

#### Example

```sql
-- Reconstruct '0800432': base=8, resolution=5, digits 0,0,4,3,2
SELECT igeo7_to_string(
    igeo7_encode(8, 0,0,4,3,2, 7,7,7,7,7, 7,7,7,7,7, 7,7,7,7,7));
-- → '0800432'
```

----

### igeo7_encode_at_resolution

#### Signatures

```sql
UBIGINT igeo7_encode_at_resolution (base UTINYINT, res INTEGER,
                                    d1 UTINYINT, …, d20 UTINYINT)
UBIGINT igeo7_encode_at_resolution (base INTEGER, res INTEGER,
                                    d1 INTEGER, …, d20 INTEGER)
```

#### Description

Convenience wrapper: encode 20 digits then truncate to the given
resolution, filling slots `(res+1)..20` with `7` (padding). Equivalent to
`igeo7_parent_at(igeo7_encode(…), res)` in a single call.

#### Example

```sql
SELECT igeo7_to_string(
    igeo7_encode_at_resolution(8, 3,
        0,0,4,3,2, 7,7,7,7,7, 7,7,7,7,7, 7,7,7,7,7));
-- → '08004'
```

----

### igeo7_get_resolution

#### Signature

```sql
INTEGER igeo7_get_resolution (idx UBIGINT)
```

#### Description

Resolution (0–20) of a packed index — derived by scanning the digit slots
for the first padding (`7`) value.

#### Example

```sql
SELECT igeo7_get_resolution(igeo7_from_string('0800432'));
-- → 5
```

----

### igeo7_get_base_cell

#### Signature

```sql
UTINYINT igeo7_get_base_cell (idx UBIGINT)
```

#### Description

Extract the base cell ID (0–11) stored in the top 4 bits.

#### Example

```sql
SELECT igeo7_get_base_cell(igeo7_from_string('0800432'));
-- → 8
```

----

### igeo7_get_digit

#### Signature

```sql
UTINYINT igeo7_get_digit (idx UBIGINT, pos INTEGER)
```

#### Description

Extract the digit at position `pos` (1..20). Returns `7` (padding) if
`pos` is out of range or beyond the cell's resolution.

#### Example

```sql
SELECT
    igeo7_get_digit(igeo7_from_string('0800432'), 1) AS d1,  -- 0
    igeo7_get_digit(igeo7_from_string('0800432'), 3) AS d3,  -- 4
    igeo7_get_digit(igeo7_from_string('0800432'), 5) AS d5;  -- 2
```

----

### igeo7_parent

#### Signature

```sql
UBIGINT igeo7_parent (idx UBIGINT)
```

#### Description

Parent index — one level up. Resolution-0 cells return themselves.

#### Example

```sql
SELECT igeo7_to_string(igeo7_parent(igeo7_from_string('0800432')));
-- → '080043'
```

----

### igeo7_parent_at

#### Signature

```sql
UBIGINT igeo7_parent_at (idx UBIGINT, res INTEGER)
```

#### Description

Ancestor at a specific resolution. Keeps digits `1..res`, fills the rest
with padding (`7`).

#### Example

```sql
SELECT igeo7_to_string(igeo7_parent_at(igeo7_from_string('0800432'), 3));
-- → '08004'
```

----

### igeo7_get_neighbours

#### Signature

```sql
UBIGINT[] igeo7_get_neighbours (idx UBIGINT)
```

#### Description

All 6 neighbours as a fixed-length array. Pentagon-adjacent directions
return the invalid sentinel `UINT64_MAX` — use `igeo7_is_valid` to filter.

#### Example

```sql
SELECT list_transform(
    igeo7_get_neighbours(igeo7_from_string('0800432')),
    x -> igeo7_to_string(x)) AS neighbours;
-- → [0800433, 0800651, 0800064, 0800436, 0800430, 0800655]
```

----

### igeo7_get_neighbour

#### Signature

```sql
UBIGINT igeo7_get_neighbour (idx UBIGINT, direction INTEGER)
```

#### Description

Single neighbour in direction 1..6. Returns `UINT64_MAX` for out-of-range
directions or pentagon-excluded neighbours.

#### Example

```sql
SELECT igeo7_to_string(
    igeo7_get_neighbour(igeo7_from_string('0800432'), 1));
-- → '0800433'
```

----

### igeo7_first_non_zero

#### Signature

```sql
INTEGER igeo7_first_non_zero (idx UBIGINT)
```

#### Description

Position (1..20) of the first non-zero digit slot. Returns 0 when the
index is padding-only. Useful for grouping cells by their lowest-level
divergence from the base cell centre.

----

### igeo7_is_valid

#### Signature

```sql
BOOLEAN igeo7_is_valid (idx UBIGINT)
```

#### Description

`FALSE` only when the index equals the invalid sentinel `UINT64_MAX`
(typically produced by out-of-range neighbour lookups).

#### Example

```sql
SELECT igeo7_is_valid(igeo7_from_string('0800432')) AS v_ok,
       igeo7_is_valid(18446744073709551615::UBIGINT) AS v_bad;
-- → v_ok = true, v_bad = false
```

----

### igeo7_decode_str

#### Signature

```sql
VARCHAR igeo7_decode_str (idx UBIGINT)   -- SQL macro
```

#### Description

Verbose `base-d1.d2…d20` form showing **all 20** digit slots including
padding `7`s. Useful for debugging; for bulk display prefer the compact
`igeo7_to_string`.

#### Example

```sql
SELECT igeo7_decode_str(32023330408103935::UBIGINT);
-- → '0-0.1.6.1.6.1.2.0.6.2.4.1.3.7.7.7.7.7.7.7'
```

----

### igeo7_string_parent

#### Signature

```sql
VARCHAR igeo7_string_parent (s VARCHAR)   -- SQL macro
```

#### Description

Drop the last character of a compact IGEO7 string. Equivalent to
`igeo7_to_string(igeo7_parent(igeo7_from_string(s)))` but cheap — pure
string slicing.

#### Example

```sql
SELECT igeo7_string_parent('0800432');
-- → '080043'
```

----

### igeo7_string_local_pos

#### Signature

```sql
VARCHAR igeo7_string_local_pos (s VARCHAR)   -- SQL macro
```

#### Description

Last character of a compact IGEO7 string — the local position digit
within the parent cell.

#### Example

```sql
SELECT igeo7_string_local_pos('0800432');
-- → '2'
```

----

### igeo7_string_is_center

#### Signature

```sql
BOOLEAN igeo7_string_is_center (s VARCHAR)   -- SQL macro
```

#### Description

`TRUE` when the cell occupies the center position within its parent
(last digit equals `'0'`, i.e. the hex overlapping the parent centroid).

#### Example

```sql
SELECT igeo7_string_is_center('080040') AS center,
       igeo7_string_is_center('0800432') AS not_center;
-- → center = true, not_center = false
```

----

### seqnum_to_vertex2dd

#### Signatures

```sql
STRUCT seqnum_to_vertex2dd (seqnum UBIGINT, res INTEGER)
STRUCT seqnum_to_vertex2dd (seqnum UBIGINT, res INTEGER, params STRUCT)
```

Return type: `STRUCT(keep BOOLEAN, vert_num INTEGER, tri_num INTEGER, x DOUBLE, y DOUBLE)`

#### Description

Converts a sequential cell index to its VERTEX2DD representation, which encodes the cell using vertex-based coordinates with triangle metadata.

#### Example

```sql
SELECT seqnum_to_vertex2dd(2380::UBIGINT, 5);
```
```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                   seqnum_to_vertex2dd(CAST(2380 AS "UBIGINT"), 5)                   │
│     struct(keep boolean, vert_num integer, tri_num integer, x double, y double)     │
├─────────────────────────────────────────────────────────────────────────────────────┤
│ {'keep': true, 'vert_num': 3, 'tri_num': 1, 'x': 0.15625, 'y': 0.27063293868263705} │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

----

### vertex2dd_to_seqnum

#### Signatures

```sql
UBIGINT vertex2dd_to_seqnum (keep BOOLEAN, vert_num INTEGER, tri_num INTEGER,
                             x DOUBLE, y DOUBLE, res INTEGER)
UBIGINT vertex2dd_to_seqnum (keep BOOLEAN, vert_num INTEGER, tri_num INTEGER,
                             x DOUBLE, y DOUBLE, res INTEGER, params STRUCT)
```

#### Description

Converts VERTEX2DD coordinates back to a sequential cell index.

#### Example

```sql
SELECT vertex2dd_to_seqnum(true, 3, 1, 0.15625, 0.27063293868263705, 5);
-- → 2380
```

----
