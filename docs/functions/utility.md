# Utility Functions

## duck_dggs_version

### Signature

```sql
VARCHAR duck_dggs_version ()
```

### Description

Returns the version string of the loaded duck_dggs extension together with the version of the bundled DGGRID library.

### Example

```sql
SELECT duck_dggs_version();
```
```
┌───────────────────────┐
│  duck_dggs_version()  │
│        varchar        │
├───────────────────────┤
│ f7dbd3d (DGGRID 9.0b) │
└───────────────────────┘
```

## dggs_params

See [Grid Configuration](/guide/grid-configuration) for full details.

### Signatures

```sql
STRUCT dggs_params (projection VARCHAR, aperture INTEGER, topology VARCHAR,
                   azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE)

STRUCT dggs_params (projection VARCHAR, aperture INTEGER, topology VARCHAR,
                   azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE,
                   is_aperture_sequence BOOLEAN, aperture_sequence VARCHAR)

STRUCT dggs_params (projection VARCHAR, aperture INTEGER, topology VARCHAR,
                   azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE,
                   is_aperture_sequence BOOLEAN, aperture_sequence VARCHAR,
                   datum VARCHAR)
```

### Description

Constructs a grid-configuration struct that can be passed as the optional last argument to any transform function.

### Example

```sql
SELECT dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25);
```

## dggs_preset

### Signature

```sql
STRUCT dggs_preset (name VARCHAR)
```

### Description

Returns the `dggs_params` struct of one of DGGRID's named grids: `IGEO7v1`, `IGEO7v2` (alias `IGEO7`), or `<ISEA|IVEA|FULLER><3H|4H|7H|4T|4D>` with an optional `S` (authalic sphere) or `L` (WGS84) suffix. See [Presets](/guide/grid-configuration#presets).

### Example

```sql
SELECT geo_to_seqnum('POINT(10 45)'::GEOMETRY, 9, dggs_preset('IGEO7v2'));
-- → 85043108
```
