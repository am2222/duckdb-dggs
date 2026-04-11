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
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `projection` | `VARCHAR` | `'ISEA'` (Icosahedral Snyder Equal Area) or `'FULLER'`. |
| `aperture` | `INTEGER` | Cell aperture: `3`, `4`, or `7`. |
| `topology` | `VARCHAR` | `'HEXAGON'`, `'TRIANGLE'`, or `'DIAMOND'`. |
| `azimuth_deg` | `DOUBLE` | Rotation of the icosahedron around the pole axis (degrees). |
| `pole_lat_deg` | `DOUBLE` | Latitude of the icosahedron pole (degrees). Default ISEA: `58.28252559`. |
| `pole_lon_deg` | `DOUBLE` | Longitude of the icosahedron pole (degrees). Default ISEA: `11.25`. |
| `is_aperture_sequence` | `BOOLEAN` | Use a mixed-aperture sequence instead of a fixed aperture. Default: `false`. |
| `aperture_sequence` | `VARCHAR` | Aperture at each resolution as a digit string (e.g. `'3437'`). Only used when `is_aperture_sequence` is `true`. |

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
