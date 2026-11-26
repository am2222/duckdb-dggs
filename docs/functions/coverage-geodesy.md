# Coverage & Geodesy

## polygon_to_seqnums

### Signatures

```sql
UBIGINT[] polygon_to_seqnums (geom GEOMETRY, res INTEGER)
UBIGINT[] polygon_to_seqnums (geom GEOMETRY, res INTEGER, params STRUCT)
```

### Description

Returns the sequential indices, in ascending order, of every cell whose **centre** lies inside a `POLYGON` or `MULTIPOLYGON`. Holes are excluded, and a `GEOMETRYCOLLECTION` of polygons is treated as their union. Z and M coordinates are ignored.

Because cells are matched on their centre, a polygon smaller than one cell can return an empty list; use a finer resolution, or `geo_to_seqnum` on a point, for tiny areas.

::: warning
Polygon edges are straight lines in longitude/latitude, so a polygon must not cross the antimeridian (split it into two parts at ±180° first). The function stops with an error once more than 10,000,000 cells are visited. Not supported for `TRIANGLE` topology grids.
:::

### Example

```sql
SELECT polygon_to_seqnums('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))'::GEOMETRY, 4);
```
```
┌───────────────────────────────────────────────────────────────────────────────────┐
│ polygon_to_seqnums(CAST('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))' AS GEOMETRY), 4) │
│                                     uint64[]                                      │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [600, 601, 617, 618, 634]                                                         │
└───────────────────────────────────────────────────────────────────────────────────┘
```

Cover an area and draw the cells (requires the `spatial` extension for display):

```sql
SELECT seqnum, seqnum_to_boundary(seqnum, 6) AS cell
FROM (SELECT unnest(polygon_to_seqnums(
        'POLYGON((-80 40, -70 38, -65 48, -75 50, -80 40))'::GEOMETRY, 6)) AS seqnum);
```

## geo_distance_km

### Signature

```sql
DOUBLE geo_distance_km (a GEOMETRY, b GEOMETRY)
DOUBLE geo_distance_km (a GEOMETRY, b GEOMETRY, datum VARCHAR)
```

### Description

Returns the distance in km between two `POINT` geometries (`x` = longitude, `y` = latitude).

- Without `datum`, or with `'AUTHALIC_SPHERE'`: the great-circle distance on the WGS84 authalic sphere (radius 6371.007 km) that DGGRID uses for its grids.
- With `'WGS84'`: the shortest geodesic on the WGS84 ellipsoid between geodetic coordinates (DGGRID 9.3's `DgGeodesic`). It agrees with GeographicLib to well under a millimetre, including nearly antipodal points.

### Example

```sql
-- Ottawa → London
SELECT geo_distance_km('POINT(-75.7 45.4)'::GEOMETRY, 'POINT(-0.1 51.5)'::GEOMETRY);
```
```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ geo_distance_km(CAST('POINT(-75.7 45.4)' AS GEOMETRY), CAST('POINT(-0.1 51.5)' AS GEOMETRY)) │
│                                            double                                            │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                             5364.66413006686 │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

```sql
-- the same pair on the WGS84 ellipsoid
SELECT geo_distance_km('POINT(-75.7 45.4)'::GEOMETRY, 'POINT(-0.1 51.5)'::GEOMETRY, 'WGS84');
-- → 5380.440419540828
```

## geo_azimuth_deg

### Signature

```sql
DOUBLE geo_azimuth_deg (a GEOMETRY, b GEOMETRY)
DOUBLE geo_azimuth_deg (a GEOMETRY, b GEOMETRY, datum VARCHAR)
```

### Description

Returns the initial bearing from point `a` to point `b`, in degrees clockwise from north in `[0, 360)` (0 = north, 90 = east): along a great circle of the authalic sphere by default, or along the WGS84 geodesic with `datum = 'WGS84'`.

::: tip
For two points almost exactly opposite each other on the equator, a north-going and a south-going geodesic are equally short, so the bearing is one of two valid values (`α` or `180° − α`).
:::

### Example

```sql
SELECT geo_azimuth_deg('POINT(-75.7 45.4)'::GEOMETRY, 'POINT(-0.1 51.5)'::GEOMETRY);
```
```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ geo_azimuth_deg(CAST('POINT(-75.7 45.4)' AS GEOMETRY), CAST('POINT(-0.1 51.5)' AS GEOMETRY)) │
│                                            double                                            │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                            53.92508306310388 │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

```sql
SELECT geo_azimuth_deg('POINT(-75.7 45.4)'::GEOMETRY, 'POINT(-0.1 51.5)'::GEOMETRY, 'WGS84');
-- → 53.93869311211036
```

## geo_destination

### Signatures

```sql
GEOMETRY geo_destination (origin GEOMETRY, azimuth_deg DOUBLE, distance_km DOUBLE)
GEOMETRY geo_destination (origin GEOMETRY, azimuth_deg DOUBLE, distance_km DOUBLE, datum VARCHAR)
```

### Description

Returns the `POINT` reached by travelling `distance_km` from `origin` along the initial bearing `azimuth_deg` (degrees clockwise from north). This is the direct geodesic problem, the inverse of `geo_distance_km` + `geo_azimuth_deg`. It follows a great circle of the authalic sphere by default, or the WGS84 geodesic with `datum = 'WGS84'`. A negative distance travels the opposite way, and the returned longitude is in `[-180, 180]`.

### Example

```sql
-- 5000 km from Ottawa, heading north-east
SELECT geo_destination('POINT(-75.7 45.4)'::GEOMETRY, 54.0, 5000.0)          AS sphere,
       geo_destination('POINT(-75.7 45.4)'::GEOMETRY, 54.0, 5000.0, 'WGS84') AS wgs84;
```
```
┌───────────────────────────────────────────────┬────────────────────────────────────────────┐
│                    sphere                     │                   wgs84                    │
│                   geometry                    │                  geometry                  │
├───────────────────────────────────────────────┼────────────────────────────────────────────┤
│ POINT (-5.072769007078589 52.696599311175746) │ POINT (-5.269411830339384 52.755536366808) │
└───────────────────────────────────────────────┴────────────────────────────────────────────┘
```

Round trip: going from `a` toward `b` for their distance lands on `b`.

```sql
WITH p AS (SELECT 'POINT(-75.7 45.4)'::GEOMETRY a, 'POINT(-0.1 51.5)'::GEOMETRY b)
SELECT geo_destination(a, geo_azimuth_deg(a, b, 'WGS84'),
                          geo_distance_km(a, b, 'WGS84'), 'WGS84')
FROM p;
-- → POINT (-0.10000000000000595 51.50000000000001)
```
