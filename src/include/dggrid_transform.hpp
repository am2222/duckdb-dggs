#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dggrid {

// ===========================================================================
// DGGS configuration
// ===========================================================================

struct DggsParams {
    std::string  projection  = "ISEA";    // ISEA or FULLER
    unsigned int aperture    = 4;         // 3, 4, or 7
    std::string  topology    = "HEXAGON"; // HEXAGON, TRIANGLE, DIAMOND
    int          res         = 5;         // 0–30
    double       azimuth_deg = 0.0;
    double       pole_lat_deg = 58.28252559;   // ISEA default
    double       pole_lon_deg = 11.25;         // ISEA default

    DggsParams withRes(int r) const {
        DggsParams p = *this;
        p.res = r;
        return p;
    }
};

// ===========================================================================
// Coordinate types
// ===========================================================================

using SeqNum = uint64_t;

struct GeoCoord {
    double lon_deg = 0.0;
    double lat_deg = 0.0;
};

struct PlaneCoord {
    double x = 0.0;
    double y = 0.0;
};

struct ProjTriCoord {
    uint64_t tnum = 0;
    double   x    = 0.0;
    double   y    = 0.0;
};

struct Q2DDCoord {
    uint64_t quad = 0;
    double   x    = 0.0;
    double   y    = 0.0;
};

struct Q2DICoord {
    uint64_t quad = 0;
    int64_t  i    = 0;
    int64_t  j    = 0;
};

struct ResInfo {
    int      res        = 0;
    uint64_t cells      = 0;
    double   area_km    = 0.0;
    double   spacing_km = 0.0;
    double   cls_km     = 0.0;
};

// ===========================================================================
// Utility
// ===========================================================================

DggsParams           construct(const std::string& projection,
                                unsigned int       aperture,
                                const std::string& topology,
                                int                res,
                                double             azimuth_deg   = 0.0,
                                double             pole_lat_deg  = 58.28252559,
                                double             pole_lon_deg  = 11.25);

DggsParams           setRes(const DggsParams &p, int res);
std::vector<ResInfo> getRes(const DggsParams &p);
uint64_t             maxCell(const DggsParams &p, int res = -1);
std::string          info(const DggsParams &p);

// ===========================================================================
// Transforms — from GEO
// ===========================================================================

GeoCoord     geoToGeo    (const DggsParams &p, double lon_deg, double lat_deg);
PlaneCoord   geoToPlane  (const DggsParams &p, double lon_deg, double lat_deg);
ProjTriCoord geoToProjTri(const DggsParams &p, double lon_deg, double lat_deg);
Q2DDCoord    geoToQ2DD   (const DggsParams &p, double lon_deg, double lat_deg);
Q2DICoord    geoToQ2DI   (const DggsParams &p, double lon_deg, double lat_deg);
SeqNum       geoToSeqNum (const DggsParams &p, double lon_deg, double lat_deg);

// ===========================================================================
// Transforms — from PROJTRI
// ===========================================================================

GeoCoord     projTriToGeo    (const DggsParams &p, uint64_t tnum, double x, double y);
PlaneCoord   projTriToPlane  (const DggsParams &p, uint64_t tnum, double x, double y);
ProjTriCoord projTriToProjTri(const DggsParams &p, uint64_t tnum, double x, double y);
Q2DDCoord    projTriToQ2DD   (const DggsParams &p, uint64_t tnum, double x, double y);
Q2DICoord    projTriToQ2DI   (const DggsParams &p, uint64_t tnum, double x, double y);
SeqNum       projTriToSeqNum (const DggsParams &p, uint64_t tnum, double x, double y);

// ===========================================================================
// Transforms — from Q2DD
// ===========================================================================

GeoCoord     q2DDToGeo    (const DggsParams &p, uint64_t quad, double x, double y);
PlaneCoord   q2DDToPlane  (const DggsParams &p, uint64_t quad, double x, double y);
ProjTriCoord q2DDToProjTri(const DggsParams &p, uint64_t quad, double x, double y);
Q2DDCoord    q2DDToQ2DD   (const DggsParams &p, uint64_t quad, double x, double y);
Q2DICoord    q2DDToQ2DI   (const DggsParams &p, uint64_t quad, double x, double y);
SeqNum       q2DDToSeqNum (const DggsParams &p, uint64_t quad, double x, double y);

// ===========================================================================
// Transforms — from Q2DI
// ===========================================================================

GeoCoord     q2DIToGeo    (const DggsParams &p, uint64_t quad, int64_t i, int64_t j);
PlaneCoord   q2DIToPlane  (const DggsParams &p, uint64_t quad, int64_t i, int64_t j);
ProjTriCoord q2DIToProjTri(const DggsParams &p, uint64_t quad, int64_t i, int64_t j);
Q2DDCoord    q2DIToQ2DD   (const DggsParams &p, uint64_t quad, int64_t i, int64_t j);
Q2DICoord    q2DIToQ2DI   (const DggsParams &p, uint64_t quad, int64_t i, int64_t j);
SeqNum       q2DIToSeqNum (const DggsParams &p, uint64_t quad, int64_t i, int64_t j);

// ===========================================================================
// Transforms — from SEQNUM
// ===========================================================================

GeoCoord     seqNumToGeo    (const DggsParams &p, SeqNum seqnum);
PlaneCoord   seqNumToPlane  (const DggsParams &p, SeqNum seqnum);
ProjTriCoord seqNumToProjTri(const DggsParams &p, SeqNum seqnum);
Q2DDCoord    seqNumToQ2DD   (const DggsParams &p, SeqNum seqnum);
Q2DICoord    seqNumToQ2DI   (const DggsParams &p, SeqNum seqnum);
SeqNum       seqNumToSeqNum (const DggsParams &p, SeqNum seqnum);

} // namespace dggrid
