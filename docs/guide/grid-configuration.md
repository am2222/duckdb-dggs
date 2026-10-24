# Grid Configuration

All transform functions use the default ISEA4H grid (ISEA projection, aperture 4, hexagon topology) when no explicit configuration is provided. Pass a `dggs_params` struct as the last argument to override this.

## `dggs_params`

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

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `projection` | `VARCHAR` | `'ISEA'` (Icosahedral Snyder Equal Area), `'IVEA'` (Icosahedral Vertex-oriented Equal Area), `'FULLER'` (Dymaxion) or `'GNOMONIC'` (icosahedral gnomonic, not equal area). |
| `aperture` | `INTEGER` | Cell aperture: `3`, `4`, or `7`. |
| `topology` | `VARCHAR` | `'HEXAGON'`, `'TRIANGLE'`, or `'DIAMOND'`. |
| `azimuth_deg` | `DOUBLE` | Rotation of the icosahedron around the pole axis (degrees). |
| `pole_lat_deg` | `DOUBLE` | Latitude of the icosahedron pole (degrees). Default ISEA: `58.28252559`. |
| `pole_lon_deg` | `DOUBLE` | Longitude of the icosahedron pole (degrees). Default ISEA: `11.25`. |
| `is_aperture_sequence` | `BOOLEAN` | Use a mixed-aperture sequence instead of a fixed aperture. Default: `false`. |
| `aperture_sequence` | `VARCHAR` | Aperture at each resolution as a digit string (e.g. `'3437'`). Only used when `is_aperture_sequence` is `true`. |
| `datum` | `VARCHAR` | Datum of geographic input and output: `'AUTHALIC_SPHERE'` (default) or `'WGS84'`. See [Datum](#datum). |

::: tip
The resolution is **not** part of `dggs_params` — it is always passed as a separate `INTEGER` argument.
:::

### Examples

Standard ISEA3H grid:

```sql
SELECT dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25);
```

Mixed-aperture grid (aperture 3, then 4, then 3, then 7):

```sql
SELECT dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25, true, '3437');
```

Using with a transform:

```sql
SELECT geo_to_seqnum('POINT(0.0 0.0)'::GEOMETRY, 5,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25));
```

## Presets

`dggs_preset(name)` returns the `dggs_params` of one of DGGRID's named grids (its `dggs_type` presets), so you don't have to spell out the orientation:

```sql
SELECT geo_to_seqnum('POINT(10 45)'::GEOMETRY, 9, dggs_preset('IGEO7v2'));
```

| Name | Grid |
|------|------|
| `<PROJ><GRID>` | `PROJ` is `ISEA`, `IVEA` or `FULLER`; `GRID` is `3H`, `4H`, `7H`, `4T` or `4D`. Authalic-sphere datum. |
| `<PROJ><GRID>S` | Same as above (S = sphere). |
| `<PROJ><GRID>L` | WGS84 datum with DGGRID's ISEAL orientation (pole longitude 11.20). |
| `IGEO7v1` | `ISEA7HS`: IGEO7 on the authalic sphere (DGGRID's `IGEO7` before v9.1). |
| `IGEO7v2`, `IGEO7` | `ISEA7HL`: IGEO7 with WGS84 lon/lat. |

Names are case-insensitive, and a trailing `Z` (DGGRID's hierarchically indexed variants, e.g. `ISEA7HSZ`) is accepted: the index functions work on every single-aperture hexagon grid anyway. `FULLER` presets use the DYMAXION orientation. The ISEA presets use the exact pole latitude `atan(φ)` = 58.2825255885…°, so they can differ from the rounded `58.28252559` used elsewhere in these docs for points that sit exactly on a cell edge.

## Datum

DGGRID builds every grid on the WGS84 **authalic sphere**: the sphere with the same surface area as the WGS84 ellipsoid, which it maps onto equal-area. By default, lon/lat values are taken to already be on that sphere. With `datum = 'WGS84'`, they are instead geodetic WGS84 coordinates (GPS, most GIS data): latitudes are converted to authalic latitude on the way in and back to geodetic latitude on the way out (cell centres and boundaries); longitudes are unchanged. Cells remain exactly equal-area on the ellipsoid.

```sql
-- the same point, read as geodetic WGS84 vs as authalic
SELECT geo_to_seqnum('POINT(10 45)'::GEOMETRY, 9, dggs_preset('ISEA4HL')),
       geo_to_seqnum('POINT(10 45)'::GEOMETRY, 9, dggs_preset('ISEA4HS'));
```

## Aperture Sequences

Aperture sequences allow different apertures at each resolution level. This is useful for creating grids where the refinement ratio varies — for example, coarse levels with aperture 7 and fine levels with aperture 3.

Constraints:
- Only supported for `HEXAGON` topology
- The sequence string must only contain `'3'`, `'4'`, and `'7'`
- Resolution must not exceed the sequence length

```sql
-- 4 levels: aperture 3, 4, 3, 4
SELECT dggs_n_cells(3,
    dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25, true, '3434'));
-- → 362
```
