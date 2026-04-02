<div align="center">
  <img src="duck_dggs_icon.svg" width="240" alt="duck_dggs"/>
  <h1>duck_dggs</h1>
  <p>A DuckDB extension for discrete global grid systems (DGGS) powered by <a href="https://github.com/sahrk/DGGRID">DGGRID v8</a>.<br/>
  Transforms geographic coordinates to and from ISEA grid cell identifiers across multiple reference frames.</p>
</div>

---

## Building

```sh
git submodule update --init --recursive
GEN=ninja make
```

For faster incremental builds:
```sh
brew install ccache ninja
```

The main binaries produced:
- `./build/release/duckdb` — DuckDB shell with the extension loaded
- `./build/release/extension/duck_dggs/duck_dggs.duckdb_extension` — distributable binary

## Quick start

```sh
./build/release/duckdb
```

```sql
-- lon, lat → cell ID at resolution 5 (default ISEA4H grid)
SELECT geo_to_seqnum(0.0, 0.0, 5);
-- → 2380

-- cell ID → cell centre
SELECT seqnum_to_geo(2380, 5);
-- → {'lon_deg': 0.0, 'lat_deg': 0.0}

-- unpack struct fields
SELECT r.lon_deg, r.lat_deg
FROM (SELECT seqnum_to_geo(2380, 5) AS r);

-- use a custom grid (ISEA3H — aperture 3 hexagons)
SELECT geo_to_seqnum(0.0, 0.0, 5,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
```

---

## Grid configuration

All functions use **ISEA4H** by default (ISEA projection, aperture 4, hexagon topology). Pass a `dggs_params` struct as the **last argument** to any transform function to use a different grid.

### `dggs_params(projection, aperture, topology, azimuth_deg, pole_lat_deg, pole_lon_deg)`

Constructs a grid configuration struct.

| Field | Type | Description |
|-------|------|-------------|
| `projection` | `VARCHAR` | `'ISEA'` (icosahedral Snyder equal-area) or `'FULLER'` (Dymaxion) |
| `aperture` | `INTEGER` | Subdivision factor per resolution step — `3` or `4` for hexagons; `4` for triangles/diamonds |
| `topology` | `VARCHAR` | Cell shape: `'HEXAGON'`, `'TRIANGLE'`, or `'DIAMOND'` |
| `azimuth_deg` | `DOUBLE` | Rotation of the icosahedron around vertex 0 (degrees) |
| `pole_lat_deg` | `DOUBLE` | Latitude of icosahedron vertex 0 (degrees) |
| `pole_lon_deg` | `DOUBLE` | Longitude of icosahedron vertex 0 (degrees) |

**Common presets:**

| Grid | Call |
|------|------|
| ISEA4H (default) | `dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)` |
| ISEA3H | `dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25)` |
| ISEA4T (triangles) | `dggs_params('ISEA', 4, 'TRIANGLE', 0.0, 58.28252559, 11.25)` |
| ISEA4D (diamonds) | `dggs_params('ISEA', 4, 'DIAMOND', 0.0, 58.28252559, 11.25)` |
| FULLER4H | `dggs_params('FULLER', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)` |

```sql
-- Store params in a column, use across many rows
WITH p AS (
    SELECT dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25) AS grid
)
SELECT geo_to_seqnum(lon, lat, 5, p.grid)
FROM my_points, p;
```

---

## Functions

Every transform function has two overloads:
- **`fn(…, res)`** — uses the default ISEA4H grid
- **`fn(…, res, params)`** — uses the supplied `dggs_params` struct

The `res` parameter sets the resolution (0–30).

### Coordinate types

| Type | Fields | Description |
|------|--------|-------------|
| `UBIGINT` | — | Sequence number — linear cell index |
| `STRUCT(lon_deg, lat_deg)` | `DOUBLE, DOUBLE` | Geographic coordinates (degrees) |
| `STRUCT(x, y)` | `DOUBLE, DOUBLE` | Plane coordinates |
| `STRUCT(tnum, x, y)` | `UBIGINT, DOUBLE, DOUBLE` | Projected triangle coordinates |
| `STRUCT(quad, x, y)` | `UBIGINT, DOUBLE, DOUBLE` | Continuous quad (Q2DD) coordinates |
| `STRUCT(quad, i, j)` | `UBIGINT, BIGINT, BIGINT` | Discrete quad (Q2DI) coordinates |

---

### Utility

| Function | Returns | Description |
|----------|---------|-------------|
| `duck_dggs_version()` | `VARCHAR` | Extension version string |
| `dggs_params(projection, aperture, topology, azimuth_deg, pole_lat_deg, pole_lon_deg)` | `STRUCT(…)` | Build a grid configuration |

---

### From geographic coordinates `(lon DOUBLE, lat DOUBLE, res INTEGER [, params])`

| Function | Returns | Description |
|----------|---------|-------------|
| `geo_to_seqnum(lon, lat, res)` | `UBIGINT` | Cell sequence number containing the point |
| `geo_to_geo(lon, lat, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `geo_to_plane(lon, lat, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `geo_to_projtri(lon, lat, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `geo_to_q2dd(lon, lat, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `geo_to_q2di(lon, lat, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |

---

### From sequence number `(seqnum UBIGINT, res INTEGER [, params])`

| Function | Returns | Description |
|----------|---------|-------------|
| `seqnum_to_geo(seqnum, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `seqnum_to_plane(seqnum, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `seqnum_to_projtri(seqnum, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `seqnum_to_q2dd(seqnum, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `seqnum_to_q2di(seqnum, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |
| `seqnum_to_seqnum(seqnum, res)` | `UBIGINT` | Round-trip validation — normalises the cell ID |

---

### From Q2DI `(quad UBIGINT, i BIGINT, j BIGINT, res INTEGER [, params])`

| Function | Returns | Description |
|----------|---------|-------------|
| `q2di_to_seqnum(quad, i, j, res)` | `UBIGINT` | Cell sequence number |
| `q2di_to_geo(quad, i, j, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `q2di_to_plane(quad, i, j, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `q2di_to_projtri(quad, i, j, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `q2di_to_q2dd(quad, i, j, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `q2di_to_q2di(quad, i, j, res)` | `STRUCT(quad, i, j)` | Round-trip validation |

---

### From Q2DD `(quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER [, params])`

| Function | Returns | Description |
|----------|---------|-------------|
| `q2dd_to_seqnum(quad, x, y, res)` | `UBIGINT` | Cell sequence number |
| `q2dd_to_geo(quad, x, y, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `q2dd_to_plane(quad, x, y, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `q2dd_to_projtri(quad, x, y, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `q2dd_to_q2dd(quad, x, y, res)` | `STRUCT(quad, x, y)` | Round-trip validation |
| `q2dd_to_q2di(quad, x, y, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |

---

### From projected triangle `(tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER [, params])`

| Function | Returns | Description |
|----------|---------|-------------|
| `projtri_to_seqnum(tnum, x, y, res)` | `UBIGINT` | Cell sequence number |
| `projtri_to_geo(tnum, x, y, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `projtri_to_plane(tnum, x, y, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `projtri_to_projtri(tnum, x, y, res)` | `STRUCT(tnum, x, y)` | Round-trip validation |
| `projtri_to_q2dd(tnum, x, y, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `projtri_to_q2di(tnum, x, y, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |

---

## Running tests

```sh
make test
```
