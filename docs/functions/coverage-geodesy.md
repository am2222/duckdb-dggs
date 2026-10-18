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
```

### Description

Returns the great-circle distance in km between two `POINT` geometries (`x` = longitude, `y` = latitude), on the WGS84 authalic sphere (radius 6371.007 km) that DGGRID uses for its grids.

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
│                                                                            5364.664130066861 │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

## geo_azimuth_deg

### Signature

```sql
DOUBLE geo_azimuth_deg (a GEOMETRY, b GEOMETRY)
```

### Description

Returns the initial great-circle bearing from point `a` to point `b`, in degrees clockwise from north in `[0, 360)` (0 = north, 90 = east).

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
