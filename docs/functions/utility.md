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
│ f7dbd3d (DGGRID 8.44) │
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
```

### Description

Constructs a grid-configuration struct that can be passed as the optional last argument to any transform function.

### Example

```sql
SELECT dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25);
```
