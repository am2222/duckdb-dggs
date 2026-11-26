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

