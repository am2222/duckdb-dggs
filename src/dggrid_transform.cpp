// dggrid_transform.cpp
//
// All coordinate transforms and grid utilities implemented against the
// DGGRID v8 dglib API, verified against the actual v8 header files:
//
//   DgIDGGBase.h   — the Q2DI frame + all RF accessors
//   DgBoundedIDGG.h — SEQNUM linearisation
//   DgIDGGS.h / DgHexIDGGS.h — IDGGS factory
//   DgProjTriRF.h / DgIDGGutil.h — intermediate frame types
//   DgEllipsoidRF.h — DgGeoCoord (latDegs / lonDegs)
//   appex.cpp      — official v8 usage example
//
// ── Reference frame chain ──────────────────────────────────────────────────
//
//   DgGeoSphRF         (GEO, radians — accessed via geoRF)
//       ↕  gnomonic projection
//   DgProjTriRF        (PROJTRI — accessed via dgg.projTriRF())
//       ↕  tri→quad mapping
//   DgQ2DDRF           (Q2DD   — accessed via dgg.q2ddRF())
//       ↕  quantise
//   DgIDGGBase itself  (Q2DI   — DgIDGGBase IS DgDiscRF<DgQ2DICoord,...>)
//       ↕  linearise via DgBoundedIDGG
//   DgBoundedIDGG      (SEQNUM — accessed via dgg.bndRF())
//
//   DgPlaneTriRF       (PLANE  — accessed via dgg.planeRF())
//
// ── Key v8 corrections from v6/dggridR assumptions ────────────────────────
//
//  ❌ assumed                │ ✅ actual v8 API
//  ──────────────────────────┼──────────────────────────────────────────────
//  q2DIRF() accessor         │ DgIDGGBase IS the Q2DI RF (no accessor needed)
//  q2DDRF() (uppercase DD)   │ q2ddRF() (lowercase)
//  dgg.getAddressAsSeqNum()  │ dgg.bndRF().seqNumAddress(q2diCoord)
//  dgg.setAddressFromSeqNum()│ dgg.bndRF().addFromSeqNum(seqnum) → DgQ2DICoord
//  DgGeoCoord(lon, lat)      │ DgGeoCoord(lon, lat, false)  [false = degrees]
//  DgGeoSphDegRF::makeRF(net,│ DgGeoSphDegRF::makeRF(geoRF, name) [no net arg]
//    geoRF, name)            │
//  idggs[res]                │ idggs->idggBase(res) or idggs->idgg(res)
//  geoRF.getPoint(loc, coord)│ geoRF.getAddress(*loc)  → const DgGeoCoord*
//  dynamic_cast<DgIDGG&>(dgg)│ DgIDGGBase already has all accessors needed

#include "dggrid_transform.hpp"

// ── dglib v8 headers ──────────────────────────────────────────────────────
#include <dglib/DgRFNetwork.h>
#include <dglib/DgGeoSphRF.h>    // DgGeoSphRF, DgGeoSphDegRF
#include <dglib/DgEllipsoidRF.h> // DgGeoCoord (.latDegs / .lonDegs)
#include <dglib/DgIDGGSBase.h>   // DgIDGGSBase
#include <dglib/DgIDGGS.h>       // DgIDGGS::makeRF  (generic factory)
#include <dglib/DgHexIDGGS.h>    // DgHexIDGGS::makeRF
#include <dglib/DgIDGGBase.h>    // DgIDGGBase (the Q2DI RF + all accessors)
#include <dglib/DgBoundedIDGG.h> // DgBoundedIDGG (SEQNUM)
#include <dglib/DgApSeq.h>       // DgApSeq (aperture sequence support)
#include <dglib/DgProjTriRF.h>   // DgProjTriRF, DgProjTriCoord, DgPlaneTriRF
#include <dglib/DgIDGGutil.h> // DgQ2DDCoord, DgQ2DDRF, DgPlaneTriRF, DgVertex2DDRF
#include <dglib/DgGridTopo.h>  // Hexagon, Triangle, Diamond, D4, D6
#include <dglib/DgLocation.h>  // DgLocation
#include <dglib/DgPolygon.h>   // DgPolygon / DgLocVector (cell boundary)
#include <dglib/DgCell.h>      // DgCell
#include <dglib/DgIVec2D.h>    // DgIVec2D (.i(), .j())
#include <dglib/DgDVec2D.h>    // DgDVec2D (.x(), .y())
#include <dglib/DgConstants.h> // M_PIl, M_ZERO
#include <dglib/DgZOrderStringRF.h> // DgZOrderStringRF, DgZOrderStringCoord (must come before DgZOrderRF.h for MSVC)
#include <dglib/DgZOrderRF.h>  // DgZOrderRF, DgZOrderCoord
#include <dglib/DgZ3StringRF.h> // DgZ3StringRF, DgZ3StringCoord (must come before DgZ3RF.h for MSVC)
#include <dglib/DgZ3RF.h> // DgZ3RF, DgZ3Coord
#include <dglib/DgZ7StringRF.h> // DgZ7StringRF, DgZ7StringCoord (must come before DgZ7RF.h for MSVC)
#include <dglib/DgZ7RF.h> // DgZ7RF, DgZ7Coord

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace dggrid {

using namespace dgg::topo; // Hexagon, Triangle, Diamond, D4, D6, ...

// ===========================================================================
// DgLocation RAII wrapper
// ===========================================================================

// makeLocation() returns a raw DgLocation* that must be deleted.
// Wrap it immediately to avoid leaks.
using LocPtr = std::unique_ptr<DgLocation>;

// ===========================================================================
// Transformer
// ===========================================================================
// Owns one fully constructed DgRFNetwork for a (projection, aperture,
// topology, res) combination.  All RF pointers are non-owning views into
// the network (the network deletes them).

struct Transformer {

  std::unique_ptr<DgRFNetwork> net;

  // Reference frames — raw non-owning pointers into net
  const DgGeoSphRF *geoRF = nullptr;  // GEO (radians)
  const DgIDGGSBase *idggs = nullptr; // multi-resolution system
  const DgIDGGBase *dgg = nullptr;    // single resolution (IS the Q2DI RF)

  // Intermediate frames — obtained from dgg accessors after build
  const DgProjTriRF *projTriRF = nullptr; // dgg.projTriRF()
  const DgQ2DDRF *q2ddRF = nullptr;       // dgg.q2ddRF()
  const DgPlaneTriRF *planeRF = nullptr;  // dgg.planeRF()
  const DgBoundedIDGG *bndRF = nullptr;   // dgg.bndRF()  (SEQNUM)

  // Hierarchical address frames (may be null depending on aperture/topology)
  const DgVertex2DDRF *vertexRF = nullptr; // dgg.vertexRF() - always available
  const DgZOrderRF *zorderRF = nullptr;    // dgg.zorderRF() - aperture 3/4 only
  const DgZ3RF *z3RF = nullptr;            // dgg.z3RF() - aperture 3 only
  const DgZ7RF *z7RF = nullptr;            // dgg.z7RF() - aperture 7 only

  // ── inX: create a DgLocation in the named source frame ──────────────

  LocPtr inGEO(double lon_deg, double lat_deg) const {
    // DgGeoCoord(lon, lat, false) — third arg false = values are in degrees
    DgGeoCoord geo(static_cast<long double>(lon_deg),
                   static_cast<long double>(lat_deg), false);
    return LocPtr(geoRF->makeLocation(geo));
  }

  LocPtr inPROJTRI(uint64_t tnum, double x, double y) const {
    DgProjTriCoord pt(
        static_cast<int>(tnum),
        DgDVec2D(static_cast<long double>(x), static_cast<long double>(y)));
    return LocPtr(projTriRF->makeLocation(pt));
  }

  LocPtr inQ2DD(uint64_t quad, double x, double y) const {
    DgQ2DDCoord pt(
        static_cast<int>(quad),
        DgDVec2D(static_cast<long double>(x), static_cast<long double>(y)));
    return LocPtr(q2ddRF->makeLocation(pt));
  }

  LocPtr inQ2DI(uint64_t quad, int64_t i, int64_t j) const {
    // DgIDGGBase IS the Q2DI RF — makeLocation directly on dgg
    DgQ2DICoord pt(
        static_cast<int>(quad),
        DgIVec2D(static_cast<long long int>(i), static_cast<long long int>(j)));
    return LocPtr(dgg->makeLocation(pt));
  }

  LocPtr inSEQNUM(uint64_t seqnum) const {
    // bndRF maps seqnum → DgQ2DICoord, then we make a location in dgg
    DgQ2DICoord q2di =
        bndRF->addFromSeqNum(static_cast<unsigned long long int>(seqnum));
    return LocPtr(dgg->makeLocation(q2di));
  }

  // ── outX: convert loc to target frame and extract typed address ───────

  void outGEO(LocPtr &loc, double &out_lon, double &out_lat) const {
    geoRF->convert(loc.get());
    const DgGeoCoord *c = geoRF->getAddress(*loc);
    out_lon = static_cast<double>(c->lonDegs());
    out_lat = static_cast<double>(c->latDegs());
  }

  void outPROJTRI(LocPtr &loc, uint64_t &out_tnum, double &out_x,
                  double &out_y) const {
    projTriRF->convert(loc.get());
    const DgProjTriCoord *c = projTriRF->getAddress(*loc);
    out_tnum = static_cast<uint64_t>(c->triNum());
    out_x = static_cast<double>(c->coord().x());
    out_y = static_cast<double>(c->coord().y());
  }

  void outQ2DD(LocPtr &loc, uint64_t &out_quad, double &out_x,
               double &out_y) const {
    q2ddRF->convert(loc.get());
    const DgQ2DDCoord *c = q2ddRF->getAddress(*loc);
    out_quad = static_cast<uint64_t>(c->quadNum());
    out_x = static_cast<double>(c->coord().x());
    out_y = static_cast<double>(c->coord().y());
  }

  void outQ2DI(LocPtr &loc, uint64_t &out_quad, int64_t &out_i,
               int64_t &out_j) const {
    // dgg IS the Q2DI RF — convert into it, then getAddress
    dgg->convert(loc.get());
    const DgQ2DICoord *c = dgg->getAddress(*loc);
    out_quad = static_cast<uint64_t>(c->quadNum());
    out_i = static_cast<int64_t>(c->coord().i());
    out_j = static_cast<int64_t>(c->coord().j());
  }

  void outSEQNUM(LocPtr &loc, uint64_t &out_seqnum) const {
    // convert to Q2DI frame (dgg), extract Q2DICoord, then linearise
    dgg->convert(loc.get());
    const DgQ2DICoord *c = dgg->getAddress(*loc);
    out_seqnum = static_cast<uint64_t>(bndRF->seqNumAddress(*c));
  }

  void outPLANE(LocPtr &loc, double &out_x, double &out_y) const {
    // DgPlaneTriRF extends DgContCartRF — addresses are DgDVec2D
    planeRF->convert(loc.get());
    const DgDVec2D *c = planeRF->getAddress(*loc);
    out_x = static_cast<double>(c->x());
    out_y = static_cast<double>(c->y());
  }

  void outVERTEX2DD(LocPtr &loc, bool &out_keep, int &out_vertNum,
                    int &out_triNum, double &out_x, double &out_y) const {
    if (!vertexRF)
      throw std::runtime_error("VERTEX2DD not available");
    vertexRF->convert(loc.get());
    const DgVertex2DDCoord *c = vertexRF->getAddress(*loc);
    out_keep = c->keep();
    out_vertNum = c->vertNum();
    out_triNum = c->triNum();
    out_x = static_cast<double>(c->coord().x());
    out_y = static_cast<double>(c->coord().y());
  }

  void outZORDER(LocPtr &loc, uint64_t &out_value) const {
    if (!zorderRF)
      throw std::runtime_error(
          "ZORDER not available for this aperture (use aperture 3 or 4)");
    zorderRF->convert(loc.get());
    const DgZOrderCoord *c = zorderRF->getAddress(*loc);
    out_value = c->value();
  }

  void outZ3(LocPtr &loc, uint64_t &out_value) const {
    if (!z3RF)
      throw std::runtime_error(
          "Z3 not available (requires aperture 3 hexagon grid)");
    z3RF->convert(loc.get());
    const DgZ3Coord *c = z3RF->getAddress(*loc);
    out_value = c->value();
  }

  void outZ7(LocPtr &loc, uint64_t &out_value) const {
    if (!z7RF)
      throw std::runtime_error(
          "Z7 not available (requires aperture 7 hexagon grid)");
    z7RF->convert(loc.get());
    const DgZ7Coord *c = z7RF->getAddress(*loc);
    out_value = c->value();
  }

  // ── inX for hierarchical types ──────────────────────────────────────────

  LocPtr inVERTEX2DD(bool keep, int vertNum, int triNum, double x,
                     double y) const {
    if (!vertexRF)
      throw std::runtime_error("VERTEX2DD not available");
    DgVertex2DDCoord coord(
        keep, vertNum, triNum,
        DgDVec2D(static_cast<long double>(x), static_cast<long double>(y)));
    return LocPtr(vertexRF->makeLocation(coord));
  }

  LocPtr inZORDER(uint64_t value) const {
    if (!zorderRF)
      throw std::runtime_error(
          "ZORDER not available for this aperture (use aperture 3 or 4)");
    DgZOrderCoord coord(value);
    return LocPtr(zorderRF->makeLocation(coord));
  }

  LocPtr inZ3(uint64_t value) const {
    if (!z3RF)
      throw std::runtime_error(
          "Z3 not available (requires aperture 3 hexagon grid)");
    DgZ3Coord coord(value);
    return LocPtr(z3RF->makeLocation(coord));
  }

  LocPtr inZ7(uint64_t value) const {
    if (!z7RF)
      throw std::runtime_error(
          "Z7 not available (requires aperture 7 hexagon grid)");
    DgZ7Coord coord(value);
    return LocPtr(z7RF->makeLocation(coord));
  }
};

// ===========================================================================
// Transformer cache
// ===========================================================================

struct CacheKey {
  std::string projection;
  unsigned int aperture;
  std::string topology;
  int res;
  double azimuth_deg;
  double pole_lat_deg;
  double pole_lon_deg;
  bool is_aperture_sequence;
  std::string aperture_sequence;

  bool operator==(const CacheKey &o) const noexcept {
    return projection == o.projection && aperture == o.aperture &&
           topology == o.topology && res == o.res &&
           azimuth_deg == o.azimuth_deg && pole_lat_deg == o.pole_lat_deg &&
           pole_lon_deg == o.pole_lon_deg &&
           is_aperture_sequence == o.is_aperture_sequence &&
           aperture_sequence == o.aperture_sequence;
  }
};

struct CacheKeyHash {
  // FNV-1a inspired combining — avoids the XOR-with-shift collisions of the
  // previous version, and includes ALL seven fields (the old hash silently
  // dropped azimuth_deg / pole_lat_deg / pole_lon_deg, causing incorrect
  // transformer reuse when orientation parameters differed).
  static std::size_t mix(std::size_t h, std::size_t v) noexcept {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
  }
  std::size_t operator()(const CacheKey &k) const noexcept {
    std::size_t h = std::hash<std::string>()(k.projection);
    h = mix(h, std::hash<unsigned int>()(k.aperture));
    h = mix(h, std::hash<std::string>()(k.topology));
    h = mix(h, std::hash<int>()(k.res));
    h = mix(h, std::hash<double>()(k.azimuth_deg));
    h = mix(h, std::hash<double>()(k.pole_lat_deg));
    h = mix(h, std::hash<double>()(k.pole_lon_deg));
    h = mix(h, std::hash<bool>()(k.is_aperture_sequence));
    h = mix(h, std::hash<std::string>()(k.aperture_sequence));
    return h;
  }
};

static std::shared_mutex s_mutex;
static std::unordered_map<CacheKey, std::shared_ptr<Transformer>, CacheKeyHash>
    s_cache;

// ---------------------------------------------------------------------------
// buildTransformer — construct a fully wired Transformer from a DggsParams.
// ---------------------------------------------------------------------------
static std::shared_ptr<Transformer> buildTransformer(const DggsParams &p) {

  if (p.res < 0 || p.res > 30)
    throw std::invalid_argument("res must be 0–30, got " +
                                std::to_string(p.res));

  // Validate aperture sequence constraints
  if (p.is_aperture_sequence) {
    if (p.topology != "HEXAGON")
      throw std::invalid_argument(
          "Aperture sequences are only supported for HEXAGON topology, got " +
          p.topology);
    if (p.aperture_sequence.empty())
      throw std::invalid_argument("aperture_sequence cannot be empty when "
                                  "is_aperture_sequence is true");
    for (char c : p.aperture_sequence) {
      if (c != '3' && c != '4' && c != '7')
        throw std::invalid_argument(
            "aperture_sequence contains invalid character '" +
            std::string(1, c) + "'. Only '3', '4', and '7' are allowed.");
    }
    if (p.res > static_cast<int>(p.aperture_sequence.length()))
      throw std::invalid_argument("Resolution " + std::to_string(p.res) +
                                  " exceeds aperture sequence length " +
                                  std::to_string(p.aperture_sequence.length()));
  }

  auto t = std::make_shared<Transformer>();
  t->net = std::make_unique<DgRFNetwork>();
  auto &net = *t->net;

  // ── Geographic RF (radians) ────────────────────────────────────────────
  t->geoRF = DgGeoSphRF::makeRF(net, "GEO");

  // ── Icosahedron orientation ────────────────────────────────────────────
  const DgGeoCoord vert0(static_cast<long double>(p.pole_lon_deg),
                         static_cast<long double>(p.pole_lat_deg),
                         false); // isRadians = false

  const long double azDegs = static_cast<long double>(p.azimuth_deg);
  const int nRes = p.res + 1;

  // ── Build IDGGS ────────────────────────────────────────────────────────
  DgGridTopology topo;
  DgGridMetric metric;
  if (p.topology == "HEXAGON") {
    topo = Hexagon;
    metric = D6;
  } else if (p.topology == "TRIANGLE") {
    topo = Triangle;
    metric = D4;
  } else if (p.topology == "DIAMOND") {
    topo = Diamond;
    metric = D4;
  } else
    throw std::invalid_argument("Unknown topology '" + p.topology +
                                "'. Use HEXAGON, TRIANGLE, or DIAMOND.");

  const DgIDGGSBase *idggs = nullptr;

  if (p.is_aperture_sequence) {
    DgApSeq apSeq(p.aperture_sequence, "CustomApSeq");
    idggs = DgHexIDGGS::makeRF(net, *t->geoRF, vert0, azDegs, p.aperture, nRes,
                               "HexIDGGS", p.projection, apSeq,
                               true,  // isApSeq = true
                               false, // isMixed43
                               0,     // numAp4
                               false  // isSuperfund
    );
  } else {
    idggs = DgIDGGS::makeRF(net, *t->geoRF, vert0, azDegs, p.aperture, nRes,
                            topo, metric, "IDGGS", p.projection);
  }

  if (!idggs)
    throw std::runtime_error(
        "makeRF returned null for " + p.projection + "/" + p.topology +
        " aperture=" + std::to_string(p.aperture) +
        (p.is_aperture_sequence ? " sequence=" + p.aperture_sequence : ""));

  t->idggs = idggs;

  // ── Get the DgIDGGBase for the specific resolution ─────────────────────
  const DgIDGGBase &dgg = idggs->idggBase(p.res);
  t->dgg = &dgg;

  // ── Resolve intermediate frames from DgIDGGBase accessors ─────────────
  t->projTriRF = &dgg.projTriRF();
  t->q2ddRF = &dgg.q2ddRF();
  t->planeRF = &dgg.planeRF();
  t->bndRF = &dgg.bndRF();

  // ── Resolve hierarchical address frames (may be null) ─────────────────
  t->vertexRF = &dgg.vertexRF(); // Always available
  t->zorderRF = dgg.zorderRF();  // Null if aperture 7
  t->z3RF = dgg.z3RF();          // Null if not aperture 3
  t->z7RF = dgg.z7RF();          // Null if not aperture 7

  return t;
}

// ---------------------------------------------------------------------------
// getTransformer — returns a cached Transformer; builds on first call.
//
// Locking strategy:
//  • Thread-local single-entry cache for the fast path: when consecutive
//    calls on the same thread use the same params (the overwhelmingly
//    common case — every row in a DuckDB DataChunk), we return immediately
//    without constructing a CacheKey, computing a hash, or taking any lock.
//  • shared_lock for the global cache read path.
//  • unique_lock for the write path; re-checks under the write lock
//    (double-checked locking) to avoid duplicate builds when two threads
//    race on the same key.
// ---------------------------------------------------------------------------
static std::shared_ptr<Transformer> getTransformer(const DggsParams &p) {
  // ── Thread-local fast path ─────────────────────────────────────────────
  // Avoids CacheKey construction, hashing, locking, and atomic refcount
  // when the same params are used repeatedly (typical: all rows in a chunk).
  static thread_local DggsParams tl_last_params{};
  static thread_local std::shared_ptr<Transformer> tl_last_result;
  static thread_local bool tl_valid = false;

  if (tl_valid && p.res == tl_last_params.res &&
      p.aperture == tl_last_params.aperture &&
      p.azimuth_deg == tl_last_params.azimuth_deg &&
      p.pole_lat_deg == tl_last_params.pole_lat_deg &&
      p.pole_lon_deg == tl_last_params.pole_lon_deg &&
      p.is_aperture_sequence == tl_last_params.is_aperture_sequence &&
      p.projection == tl_last_params.projection &&
      p.topology == tl_last_params.topology &&
      p.aperture_sequence == tl_last_params.aperture_sequence) {
    return tl_last_result;
  }

  // ── Global cache lookup ────────────────────────────────────────────────
  CacheKey key{p.projection,       p.aperture,
               p.topology,         p.res,
               p.azimuth_deg,      p.pole_lat_deg,
               p.pole_lon_deg,     p.is_aperture_sequence,
               p.aperture_sequence};
  std::shared_ptr<Transformer> result;
  {
    std::shared_lock<std::shared_mutex> rl(s_mutex);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
      result = it->second;
    }
  }

  if (!result) {
    // Cache miss — build under exclusive lock with double-check.
    std::unique_lock<std::shared_mutex> wl(s_mutex);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
      result = it->second;
    } else {
      result = buildTransformer(p);
      s_cache.emplace(std::move(key), result);
    }
  }

  // Update thread-local cache
  tl_last_params = p;
  tl_last_result = result;
  tl_valid = true;
  return result;
}

// ===========================================================================
// Utility functions
// ===========================================================================

DggsParams construct(const std::string &projection, unsigned int aperture,
                     const std::string &topology, int res, double azimuth_deg,
                     double pole_lat_deg, double pole_lon_deg,
                     bool is_aperture_sequence,
                     const std::string &aperture_sequence) {
  if (res < 0 || res > 30)
    throw std::invalid_argument("res must be 0–30");
  DggsParams p;
  p.projection = projection;
  p.aperture = aperture;
  p.topology = topology;
  p.res = res;
  p.azimuth_deg = azimuth_deg;
  p.pole_lat_deg = pole_lat_deg;
  p.pole_lon_deg = pole_lon_deg;
  p.is_aperture_sequence = is_aperture_sequence;
  p.aperture_sequence = aperture_sequence;
  return p;
}

DggsParams setRes(const DggsParams &p, int res) { return p.withRes(res); }

std::vector<ResInfo> getRes(const DggsParams &p) {
  DggsParams full = p;

  int maxRes = 30;
  if (p.is_aperture_sequence && !p.aperture_sequence.empty())
    maxRes = std::min(30, static_cast<int>(p.aperture_sequence.length()));

  full.res = maxRes;
  auto t = getTransformer(full);

  std::vector<ResInfo> rows;
  rows.reserve(static_cast<std::size_t>(maxRes + 1));
  for (int r = 0; r <= maxRes; ++r) {
    const DgIDGGBase &dgg_r = t->idggs->idggBase(r);
    const DgGridStats &gs = dgg_r.gridStats();
    rows.push_back(ResInfo{r, static_cast<uint64_t>(gs.nCells()),
                           static_cast<double>(gs.cellAreaKM()),
                           static_cast<double>(gs.cellDistKM()),
                           static_cast<double>(gs.cls())});
  }
  return rows;
}

ResInfo getResAt(const DggsParams &p, int res) {
  if (res < 0 || res > 30)
    throw std::invalid_argument("res out of range 0–30");

  int maxRes = 30;
  if (p.is_aperture_sequence && !p.aperture_sequence.empty()) {
    maxRes = static_cast<int>(p.aperture_sequence.length());
    if (res > maxRes)
      throw std::invalid_argument("Resolution " + std::to_string(res) +
                                  " exceeds aperture sequence length " +
                                  std::to_string(maxRes));
  }

  DggsParams full = p;
  full.res = maxRes;
  auto t = getTransformer(full);
  const DgIDGGBase &dgg_r = t->idggs->idggBase(res);
  const DgGridStats &gs = dgg_r.gridStats();
  return ResInfo{res, static_cast<uint64_t>(gs.nCells()),
                 static_cast<double>(gs.cellAreaKM()),
                 static_cast<double>(gs.cellDistKM()),
                 static_cast<double>(gs.cls())};
}

uint64_t maxCell(const DggsParams &p, int res) {
  int target = (res == -1) ? p.res : res;
  if (target < 0 || target > 30)
    throw std::invalid_argument("res out of range 0–30");
  return getResAt(p, target).cells;
}

std::string info(const DggsParams &p) {
  auto rows = getRes(p);
  auto &row = rows[static_cast<std::size_t>(p.res)];
  std::ostringstream os;
  os << "DGGS: " << p.projection << "  aperture=" << p.aperture
     << "  topology=" << p.topology << "  res=" << p.res << "\n"
     << "  cells:      " << row.cells << "\n"
     << "  area_km²:   " << row.area_km << "\n"
     << "  spacing_km: " << row.spacing_km << "\n"
     << "  cls_km:     " << row.cls_km << "\n";
  return os.str();
}

// ===========================================================================
// FROM GEO
// ===========================================================================

GeoCoord geoToGeo(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  GeoCoord r{};
  t->outGEO(loc, r.lon_deg, r.lat_deg);
  return r;
}

PlaneCoord geoToPlane(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  PlaneCoord r{};
  t->outPLANE(loc, r.x, r.y);
  return r;
}

ProjTriCoord geoToProjTri(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  ProjTriCoord r{};
  t->outPROJTRI(loc, r.tnum, r.x, r.y);
  return r;
}

Q2DDCoord geoToQ2DD(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  Q2DDCoord r{};
  t->outQ2DD(loc, r.quad, r.x, r.y);
  return r;
}

Q2DICoord geoToQ2DI(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  Q2DICoord r{};
  t->outQ2DI(loc, r.quad, r.i, r.j);
  return r;
}

SeqNum geoToSeqNum(const DggsParams &p, double lon_deg, double lat_deg) {
  auto t = getTransformer(p);
  auto loc = t->inGEO(lon_deg, lat_deg);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

// ===========================================================================
// FROM PROJTRI
// ===========================================================================

GeoCoord projTriToGeo(const DggsParams &p, uint64_t tnum, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  GeoCoord r{};
  t->outGEO(loc, r.lon_deg, r.lat_deg);
  return r;
}

PlaneCoord projTriToPlane(const DggsParams &p, uint64_t tnum, double x,
                          double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  PlaneCoord r{};
  t->outPLANE(loc, r.x, r.y);
  return r;
}

ProjTriCoord projTriToProjTri(const DggsParams &p, uint64_t tnum, double x,
                              double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  ProjTriCoord r{};
  t->outPROJTRI(loc, r.tnum, r.x, r.y);
  return r;
}

Q2DDCoord projTriToQ2DD(const DggsParams &p, uint64_t tnum, double x,
                        double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  Q2DDCoord r{};
  t->outQ2DD(loc, r.quad, r.x, r.y);
  return r;
}

Q2DICoord projTriToQ2DI(const DggsParams &p, uint64_t tnum, double x,
                        double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  Q2DICoord r{};
  t->outQ2DI(loc, r.quad, r.i, r.j);
  return r;
}

SeqNum projTriToSeqNum(const DggsParams &p, uint64_t tnum, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inPROJTRI(tnum, x, y);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

// ===========================================================================
// FROM Q2DD
// ===========================================================================

GeoCoord q2DDToGeo(const DggsParams &p, uint64_t quad, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  GeoCoord r{};
  t->outGEO(loc, r.lon_deg, r.lat_deg);
  return r;
}

PlaneCoord q2DDToPlane(const DggsParams &p, uint64_t quad, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  PlaneCoord r{};
  t->outPLANE(loc, r.x, r.y);
  return r;
}

ProjTriCoord q2DDToProjTri(const DggsParams &p, uint64_t quad, double x,
                           double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  ProjTriCoord r{};
  t->outPROJTRI(loc, r.tnum, r.x, r.y);
  return r;
}

Q2DDCoord q2DDToQ2DD(const DggsParams &p, uint64_t quad, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  Q2DDCoord r{};
  t->outQ2DD(loc, r.quad, r.x, r.y);
  return r;
}

Q2DICoord q2DDToQ2DI(const DggsParams &p, uint64_t quad, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  Q2DICoord r{};
  t->outQ2DI(loc, r.quad, r.i, r.j);
  return r;
}

SeqNum q2DDToSeqNum(const DggsParams &p, uint64_t quad, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DD(quad, x, y);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

// ===========================================================================
// FROM Q2DI
// ===========================================================================

GeoCoord q2DIToGeo(const DggsParams &p, uint64_t quad, int64_t i, int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  GeoCoord r{};
  t->outGEO(loc, r.lon_deg, r.lat_deg);
  return r;
}

PlaneCoord q2DIToPlane(const DggsParams &p, uint64_t quad, int64_t i,
                       int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  PlaneCoord r{};
  t->outPLANE(loc, r.x, r.y);
  return r;
}

ProjTriCoord q2DIToProjTri(const DggsParams &p, uint64_t quad, int64_t i,
                           int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  ProjTriCoord r{};
  t->outPROJTRI(loc, r.tnum, r.x, r.y);
  return r;
}

Q2DDCoord q2DIToQ2DD(const DggsParams &p, uint64_t quad, int64_t i, int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  Q2DDCoord r{};
  t->outQ2DD(loc, r.quad, r.x, r.y);
  return r;
}

Q2DICoord q2DIToQ2DI(const DggsParams &p, uint64_t quad, int64_t i, int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  Q2DICoord r{};
  t->outQ2DI(loc, r.quad, r.i, r.j);
  return r;
}

SeqNum q2DIToSeqNum(const DggsParams &p, uint64_t quad, int64_t i, int64_t j) {
  auto t = getTransformer(p);
  auto loc = t->inQ2DI(quad, i, j);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

// ===========================================================================
// FROM SEQNUM
// ===========================================================================

GeoCoord seqNumToGeo(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  GeoCoord r{};
  t->outGEO(loc, r.lon_deg, r.lat_deg);
  return r;
}

PlaneCoord seqNumToPlane(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  PlaneCoord r{};
  t->outPLANE(loc, r.x, r.y);
  return r;
}

ProjTriCoord seqNumToProjTri(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  ProjTriCoord r{};
  t->outPROJTRI(loc, r.tnum, r.x, r.y);
  return r;
}

Q2DDCoord seqNumToQ2DD(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  Q2DDCoord r{};
  t->outQ2DD(loc, r.quad, r.x, r.y);
  return r;
}

Q2DICoord seqNumToQ2DI(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  Q2DICoord r{};
  t->outQ2DI(loc, r.quad, r.i, r.j);
  return r;
}

SeqNum seqNumToSeqNum(const DggsParams &p, SeqNum seqnum) {
  // Round-trip: validates and normalises the seqnum.
  // For valid inputs the output equals the input.
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

std::vector<GeoCoord> seqNumToBoundary(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);

  // Resolve Q2DI from the sequence number.
  DgQ2DICoord q2di =
      t->bndRF->addFromSeqNum(static_cast<unsigned long long int>(seqnum));

  // setAddVertices fills the polygon with boundary vertices already converted
  // to the reference frame the DgPolygon was constructed with (geoRF here).
  DgPolygon poly(*t->geoRF);
  t->dgg->setAddVertices(q2di, poly, 0 /* densify */);

  std::vector<GeoCoord> result;
  result.reserve(static_cast<std::size_t>(poly.size()));
  for (int i = 0; i < poly.size(); i++) {
    const DgGeoCoord *c = t->geoRF->getAddress(poly[i]);
    result.push_back(
        {static_cast<double>(c->lonDegs()), static_cast<double>(c->latDegs())});
  }
  return result;
}

// ===========================================================================
// NEIGHBORS
// ===========================================================================

std::vector<SeqNum> seqNumNeighbors(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);

  if (t->dgg->gridTopo() == Triangle)
    throw std::runtime_error("Neighbors not implemented for Triangle grids");

  DgLocVector neighbors(*(t->dgg));
  t->dgg->setNeighbors(*loc, neighbors);

  std::vector<SeqNum> result;
  result.reserve(static_cast<std::size_t>(neighbors.size()));
  for (int i = 0; i < neighbors.size(); i++) {
    const DgQ2DICoord *q2di = t->dgg->getAddress(neighbors[i]);
    result.push_back(static_cast<SeqNum>(t->bndRF->seqNumAddress(*q2di)));
  }
  return result;
}

// ===========================================================================
// PARENT / CHILD
// ===========================================================================

SeqNum seqNumParent(const DggsParams &p, SeqNum seqnum) {
  if (p.res <= 0)
    throw std::runtime_error("Cannot get parent: already at resolution 0");

  // Convert the child cell's center point to the parent resolution.
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  double lon_deg = 0, lat_deg = 0;
  t->outGEO(loc, lon_deg, lat_deg);

  DggsParams parentParams = p;
  parentParams.res = p.res - 1;
  return geoToSeqNum(parentParams, lon_deg, lat_deg);
}

std::vector<SeqNum> seqNumAllParents(const DggsParams &p, SeqNum seqnum) {
  if (p.res <= 0)
    throw std::runtime_error("Cannot get parents: already at resolution 0");

  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);

  DgLocVector parents(*(t->idggs));
  t->idggs->setParents(p.res, *loc, parents);

  const DgIDGGBase &parent_dgg = t->idggs->idggBase(p.res - 1);

  std::vector<SeqNum> result;
  std::set<uint64_t> seen;
  for (int i = 0; i < parents.size(); i++) {
    DgLocation parent_loc(parents[i]);
    parent_dgg.convert(&parent_loc);
    const DgQ2DICoord *parent_q2di = parent_dgg.getAddress(parent_loc);
    uint64_t parent_seqnum = parent_dgg.bndRF().seqNumAddress(*parent_q2di);
    if (seen.insert(parent_seqnum).second)
      result.push_back(static_cast<SeqNum>(parent_seqnum));
  }

  // Ensure the primary (containing) parent is first
  SeqNum primary = seqNumParent(p, seqnum);
  auto it = std::find(result.begin(), result.end(), primary);
  if (it != result.end() && it != result.begin()) {
    std::iter_swap(result.begin(), it);
  } else if (it == result.end()) {
    result.insert(result.begin(), primary);
  }

  return result;
}

std::vector<SeqNum> seqNumChildren(const DggsParams &p, SeqNum seqnum) {
  // Build the IDGGS with res+1 so the child resolution exists.
  DggsParams p_with_children = p;
  p_with_children.res = p.res + 1;
  auto t = getTransformer(p_with_children);

  const DgIDGGBase &parent_dgg = t->idggs->idggBase(p.res);
  DgQ2DICoord q2di = parent_dgg.bndRF().addFromSeqNum(
      static_cast<unsigned long long int>(seqnum));
  DgResAdd<DgQ2DICoord> q2diR(q2di, p.res);

  DgLocVector children;
  t->idggs->setAllChildren(q2diR, children);

  const DgIDGGBase &child_dgg = t->idggs->idggBase(p.res + 1);
  std::vector<SeqNum> result;
  result.reserve(static_cast<std::size_t>(children.size()));
  for (int i = 0; i < children.size(); i++) {
    DgLocation child_loc(children[i]);
    child_dgg.convert(&child_loc);
    const DgQ2DICoord *child_q2di = child_dgg.getAddress(child_loc);
    result.push_back(
        static_cast<SeqNum>(child_dgg.bndRF().seqNumAddress(*child_q2di)));
  }
  return result;
}

// ===========================================================================
// HIERARCHICAL ADDRESS TYPES — SEQNUM conversions
// ===========================================================================

Vertex2DDCoord seqNumToVertex2DD(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  Vertex2DDCoord r{};
  t->outVERTEX2DD(loc, r.keep, r.vertNum, r.triNum, r.x, r.y);
  return r;
}

SeqNum vertex2DDToSeqNum(const DggsParams &p, bool keep, int vertNum,
                         int triNum, double x, double y) {
  auto t = getTransformer(p);
  auto loc = t->inVERTEX2DD(keep, vertNum, triNum, x, y);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

ZOrderCoord seqNumToZOrder(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  ZOrderCoord r{};
  t->outZORDER(loc, r.value);
  return r;
}

SeqNum zOrderToSeqNum(const DggsParams &p, uint64_t value) {
  auto t = getTransformer(p);
  auto loc = t->inZORDER(value);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

Z3Coord seqNumToZ3(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  Z3Coord r{};
  t->outZ3(loc, r.value);
  return r;
}

SeqNum z3ToSeqNum(const DggsParams &p, uint64_t value) {
  auto t = getTransformer(p);
  auto loc = t->inZ3(value);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

Z7Coord seqNumToZ7(const DggsParams &p, SeqNum seqnum) {
  auto t = getTransformer(p);
  auto loc = t->inSEQNUM(seqnum);
  Z7Coord r{};
  t->outZ7(loc, r.value);
  return r;
}

SeqNum z7ToSeqNum(const DggsParams &p, uint64_t value) {
  auto t = getTransformer(p);
  auto loc = t->inZ7(value);
  SeqNum r = 0;
  t->outSEQNUM(loc, r);
  return r;
}

} // namespace dggrid
