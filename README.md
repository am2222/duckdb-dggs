# duck_dggs

A DuckDB extension for discrete global grid systems (DGGS) using [DGGRID v8](https://github.com/sahrk/DGGRID). Provides coordinate transforms between geographic coordinates and ISEA grid cell identifiers across multiple reference frames.

## Building

```sh
git submodule update --init --recursive
GEN=ninja make
```

For faster incremental builds, install `ccache` and `ninja`:
```sh
brew install ccache ninja
```

The main binaries produced:
- `./build/release/duckdb` — DuckDB shell with the extension loaded
- `./build/release/extension/duck_dggs/duck_dggs.duckdb_extension` — distributable binary

## Usage

```sh
./build/release/duckdb
```

```sql
-- lon, lat → cell ID at resolution 5
SELECT geo_to_seqnum(0.0, 0.0, 5);
-- → 2380

-- cell ID → cell centre (lon, lat)
SELECT seqnum_to_geo(2380, 5);
-- → {'lon_deg': -0.902, 'lat_deg': ~0.0}

-- unpack struct fields
SELECT r.lon_deg, r.lat_deg
FROM (SELECT seqnum_to_geo(2380, 5) AS r);
```

## Functions

All functions use the **ISEA4H** grid by default (ISEA projection, aperture 4, hexagon topology). The `res` parameter controls resolution (0–30).

### Coordinate types

| Type | Fields | Description |
|------|--------|-------------|
| `UBIGINT` | — | Sequence number — linear cell index |
| `STRUCT(lon_deg, lat_deg)` | `DOUBLE, DOUBLE` | Geographic coordinates (degrees) |
| `STRUCT(x, y)` | `DOUBLE, DOUBLE` | Plane coordinates |
| `STRUCT(tnum, x, y)` | `UBIGINT, DOUBLE, DOUBLE` | Projected triangle coordinates |
| `STRUCT(quad, x, y)` | `UBIGINT, DOUBLE, DOUBLE` | Quad (Q2DD) coordinates |
| `STRUCT(quad, i, j)` | `UBIGINT, BIGINT, BIGINT` | Discrete quad (Q2DI) coordinates |

---

### Utility

| Function | Returns | Description |
|----------|---------|-------------|
| `duck_dggs_version()` | `VARCHAR` | Extension version string |

---

### From geographic coordinates `(lon DOUBLE, lat DOUBLE, res INTEGER)`

| Function | Returns | Description |
|----------|---------|-------------|
| `geo_to_seqnum(lon, lat, res)` | `UBIGINT` | Cell sequence number containing the point |
| `geo_to_geo(lon, lat, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `geo_to_plane(lon, lat, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `geo_to_projtri(lon, lat, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `geo_to_q2dd(lon, lat, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `geo_to_q2di(lon, lat, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |

---

### From sequence number `(seqnum UBIGINT, res INTEGER)`

| Function | Returns | Description |
|----------|---------|-------------|
| `seqnum_to_geo(seqnum, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `seqnum_to_plane(seqnum, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `seqnum_to_projtri(seqnum, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `seqnum_to_q2dd(seqnum, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `seqnum_to_q2di(seqnum, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |
| `seqnum_to_seqnum(seqnum, res)` | `UBIGINT` | Round-trip validation — normalises the cell ID |

---

### From Q2DI `(quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)`

| Function | Returns | Description |
|----------|---------|-------------|
| `q2di_to_geo(quad, i, j, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `q2di_to_plane(quad, i, j, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `q2di_to_projtri(quad, i, j, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `q2di_to_q2dd(quad, i, j, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `q2di_to_q2di(quad, i, j, res)` | `STRUCT(quad, i, j)` | Round-trip validation |
| `q2di_to_seqnum(quad, i, j, res)` | `UBIGINT` | Cell sequence number |

---

### From Q2DD `(quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)`

| Function | Returns | Description |
|----------|---------|-------------|
| `q2dd_to_geo(quad, x, y, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `q2dd_to_plane(quad, x, y, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `q2dd_to_projtri(quad, x, y, res)` | `STRUCT(tnum, x, y)` | Cell centre in projected triangle coords |
| `q2dd_to_q2dd(quad, x, y, res)` | `STRUCT(quad, x, y)` | Round-trip validation |
| `q2dd_to_q2di(quad, x, y, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |
| `q2dd_to_seqnum(quad, x, y, res)` | `UBIGINT` | Cell sequence number |

---

### From projected triangle `(tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)`

| Function | Returns | Description |
|----------|---------|-------------|
| `projtri_to_geo(tnum, x, y, res)` | `STRUCT(lon_deg, lat_deg)` | Cell centre in geographic coords |
| `projtri_to_plane(tnum, x, y, res)` | `STRUCT(x, y)` | Cell centre in plane coords |
| `projtri_to_projtri(tnum, x, y, res)` | `STRUCT(tnum, x, y)` | Round-trip validation |
| `projtri_to_q2dd(tnum, x, y, res)` | `STRUCT(quad, x, y)` | Cell centre in Q2DD coords |
| `projtri_to_q2di(tnum, x, y, res)` | `STRUCT(quad, i, j)` | Cell index in Q2DI coords |
| `projtri_to_seqnum(tnum, x, y, res)` | `UBIGINT` | Cell sequence number |

---

## Running tests

```sh
make test
```
