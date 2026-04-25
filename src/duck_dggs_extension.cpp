#define DUCKDB_EXTENSION_MAIN

#include "duck_dggs_extension.hpp"
#include "dggrid_transform.hpp"
#include "igeo7_functions.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/execution/expression_executor.hpp"
// StructVector moved to its own header in DuckDB v1.6+; fall back to vector.hpp
// for v1.5
#if __has_include("duckdb/common/vector/struct_vector.hpp")
#include "duckdb/common/vector/struct_vector.hpp"
#else
#include "duckdb/common/types/vector.hpp"
#endif
#if __has_include("duckdb/common/vector/list_vector.hpp")
#include "duckdb/common/vector/list_vector.hpp"
#endif
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <dglib/DgBase.h>

namespace duckdb {

// ---------------------------------------------------------------------------
// Cross-version helper: StructVector::GetEntries returns vector<Vector> in
// v1.6+ and vector<unique_ptr<Vector>> in v1.5.1.  Both overloads compile;
// only one will match given the actual return type of GetEntries.
// ---------------------------------------------------------------------------
#if __has_include("duckdb/common/vector/struct_vector.hpp")
static inline Vector &GetStructEntry(vector<Vector> &entries, idx_t i) {
  return entries[i];
}
#else
static inline Vector &GetStructEntry(vector<unique_ptr<Vector>> &entries,
                                     idx_t i) {
  return *entries[i];
}
#endif

// ===========================================================================
// Return type helpers
// ===========================================================================

// Read lon/lat from a WKB POINT. DuckDB stores GEOMETRY as little-endian WKB:
//   [byte_order:1][type:4][x:8][y:8]
static void ReadPointXY(const string_t &geom, double &lon, double &lat) {
  auto data = reinterpret_cast<const uint8_t *>(geom.GetData());
  memcpy(&lon, data + 5, sizeof(double));
  memcpy(&lat, data + 13, sizeof(double));
}

static LogicalType PlaneType() {
  child_list_t<LogicalType> c;
  c.push_back({"x", LogicalType::DOUBLE});
  c.push_back({"y", LogicalType::DOUBLE});
  return LogicalType::STRUCT(c);
}

static LogicalType ProjTriType() {
  child_list_t<LogicalType> c;
  c.push_back({"tnum", LogicalType::UBIGINT});
  c.push_back({"x", LogicalType::DOUBLE});
  c.push_back({"y", LogicalType::DOUBLE});
  return LogicalType::STRUCT(c);
}

static LogicalType Q2DDType() {
  child_list_t<LogicalType> c;
  c.push_back({"quad", LogicalType::UBIGINT});
  c.push_back({"x", LogicalType::DOUBLE});
  c.push_back({"y", LogicalType::DOUBLE});
  return LogicalType::STRUCT(c);
}

static LogicalType Q2DIType() {
  child_list_t<LogicalType> c;
  c.push_back({"quad", LogicalType::UBIGINT});
  c.push_back({"i", LogicalType::BIGINT});
  c.push_back({"j", LogicalType::BIGINT});
  return LogicalType::STRUCT(c);
}

// STRUCT(projection VARCHAR, aperture INTEGER, topology VARCHAR,
//        azimuth_deg DOUBLE, pole_lat_deg DOUBLE, pole_lon_deg DOUBLE,
//        is_aperture_sequence BOOLEAN, aperture_sequence VARCHAR)
static LogicalType DggsParamsType() {
  child_list_t<LogicalType> c;
  c.push_back({"projection", LogicalType::VARCHAR});
  c.push_back({"aperture", LogicalType::INTEGER});
  c.push_back({"topology", LogicalType::VARCHAR});
  c.push_back({"azimuth_deg", LogicalType::DOUBLE});
  c.push_back({"pole_lat_deg", LogicalType::DOUBLE});
  c.push_back({"pole_lon_deg", LogicalType::DOUBLE});
  c.push_back({"is_aperture_sequence", LogicalType::BOOLEAN});
  c.push_back({"aperture_sequence", LogicalType::VARCHAR});
  return LogicalType::STRUCT(c);
}

static LogicalType ResInfoType() {
  child_list_t<LogicalType> c;
  c.push_back({"res", LogicalType::INTEGER});
  c.push_back({"cells", LogicalType::UBIGINT});
  c.push_back({"area_km", LogicalType::DOUBLE});
  c.push_back({"spacing_km", LogicalType::DOUBLE});
  c.push_back({"cls_km", LogicalType::DOUBLE});
  return LogicalType::STRUCT(c);
}

static void WriteResInfo(Vector &result, idx_t i, const dggrid::ResInfo &r) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<int32_t>(GetStructEntry(entries, 0))[i] =
      static_cast<int32_t>(r.res);
  FlatVector::GetData<uint64_t>(GetStructEntry(entries, 1))[i] = r.cells;
  FlatVector::GetData<double>(GetStructEntry(entries, 2))[i] = r.area_km;
  FlatVector::GetData<double>(GetStructEntry(entries, 3))[i] = r.spacing_km;
  FlatVector::GetData<double>(GetStructEntry(entries, 4))[i] = r.cls_km;
}

static LogicalType Vertex2DDType() {
  child_list_t<LogicalType> c;
  c.push_back({"keep", LogicalType::BOOLEAN});
  c.push_back({"vert_num", LogicalType::INTEGER});
  c.push_back({"tri_num", LogicalType::INTEGER});
  c.push_back({"x", LogicalType::DOUBLE});
  c.push_back({"y", LogicalType::DOUBLE});
  return LogicalType::STRUCT(c);
}

static void WriteVertex2DD(Vector &result, idx_t i,
                           const dggrid::Vertex2DDCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<bool>(GetStructEntry(entries, 0))[i] = c.keep;
  FlatVector::GetData<int32_t>(GetStructEntry(entries, 1))[i] = c.vertNum;
  FlatVector::GetData<int32_t>(GetStructEntry(entries, 2))[i] = c.triNum;
  FlatVector::GetData<double>(GetStructEntry(entries, 3))[i] = c.x;
  FlatVector::GetData<double>(GetStructEntry(entries, 4))[i] = c.y;
}

// ===========================================================================
// Output writers — fill struct child vectors from a coord value
// ===========================================================================

static void WriteGeo(Vector &result, idx_t i, const dggrid::GeoCoord &c) {
  // Write a WKB POINT(lon, lat): [byte_order:1][type:4][x:8][y:8] = 21 bytes
  auto str = StringVector::EmptyString(result, 21);
  auto data = reinterpret_cast<uint8_t *>(str.GetDataWriteable());
  data[0] = 0x01; // little-endian
  constexpr uint32_t WKB_POINT = 1;
  memcpy(data + 1, &WKB_POINT, 4);
  memcpy(data + 5, &c.lon_deg, sizeof(double));
  memcpy(data + 13, &c.lat_deg, sizeof(double));
  str.Finalize();
  FlatVector::GetData<string_t>(result)[i] = str;
}

static void WritePlane(Vector &result, idx_t i, const dggrid::PlaneCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<double>(GetStructEntry(entries, 0))[i] = c.x;
  FlatVector::GetData<double>(GetStructEntry(entries, 1))[i] = c.y;
}

static void WriteProjTri(Vector &result, idx_t i,
                         const dggrid::ProjTriCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<uint64_t>(GetStructEntry(entries, 0))[i] = c.tnum;
  FlatVector::GetData<double>(GetStructEntry(entries, 1))[i] = c.x;
  FlatVector::GetData<double>(GetStructEntry(entries, 2))[i] = c.y;
}

static void WriteQ2DD(Vector &result, idx_t i, const dggrid::Q2DDCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<uint64_t>(GetStructEntry(entries, 0))[i] = c.quad;
  FlatVector::GetData<double>(GetStructEntry(entries, 1))[i] = c.x;
  FlatVector::GetData<double>(GetStructEntry(entries, 2))[i] = c.y;
}

static void WriteQ2DI(Vector &result, idx_t i, const dggrid::Q2DICoord &c) {
  auto &entries = StructVector::GetEntries(result);
  FlatVector::GetData<uint64_t>(GetStructEntry(entries, 0))[i] = c.quad;
  FlatVector::GetData<int64_t>(GetStructEntry(entries, 1))[i] = c.i;
  FlatVector::GetData<int64_t>(GetStructEntry(entries, 2))[i] = c.j;
}

// Write a WKB Polygon (single exterior ring, open vertex list — we close it).
// Layout:
// [byte_order:1][type:4][numRings:4][numPoints:4][x0:8][y0:8]...[x0:8][y0:8]
static void WriteWKBPolygon(Vector &result, idx_t i,
                            const std::vector<dggrid::GeoCoord> &verts) {
  const std::size_t n = verts.size();
  // 1 (order) + 4 (type) + 4 (numRings) + 4 (numPoints) + (n+1)*16 (points)
  const idx_t sz = static_cast<idx_t>(13 + (n + 1) * 16);
  auto str = StringVector::EmptyString(result, sz);
  auto *d = reinterpret_cast<uint8_t *>(str.GetDataWriteable());
  std::size_t off = 0;
  d[off++] = 0x01; // little-endian
  constexpr uint32_t WKB_POLYGON = 3;
  memcpy(d + off, &WKB_POLYGON, 4);
  off += 4;
  constexpr uint32_t NUM_RINGS = 1;
  memcpy(d + off, &NUM_RINGS, 4);
  off += 4;
  const uint32_t num_pts = static_cast<uint32_t>(n + 1); // closed ring
  memcpy(d + off, &num_pts, 4);
  off += 4;
  for (std::size_t j = 0; j < n; j++) {
    memcpy(d + off, &verts[j].lon_deg, 8);
    off += 8;
    memcpy(d + off, &verts[j].lat_deg, 8);
    off += 8;
  }
  // Close the ring by repeating the first vertex.
  memcpy(d + off, &verts[0].lon_deg, 8);
  off += 8;
  memcpy(d + off, &verts[0].lat_deg, 8);
  str.Finalize();
  FlatVector::GetData<string_t>(result)[i] = str;
}

// ===========================================================================
// Input readers
// ===========================================================================

template <typename T> struct ArgReader {
  UnifiedVectorFormat fmt;
  const T *data;

  ArgReader(DataChunk &args, idx_t col, idx_t count) {
    args.data[col].ToUnifiedFormat(count, fmt);
    data = UnifiedVectorFormat::GetData<T>(fmt);
  }

  T operator[](idx_t i) const { return data[fmt.sel->get_index(i)]; }
};

// Reads a DggsParams from a struct vector (one per row).
// Each child is independently unified so constant structs work correctly.
struct ParamsReader {
  UnifiedVectorFormat proj_fmt, apt_fmt, topo_fmt, az_fmt, plat_fmt, plon_fmt;
  UnifiedVectorFormat is_apseq_fmt, apseq_fmt;
  const string_t *proj_data, *topo_data, *apseq_data;
  const int32_t *apt_data;
  const double *az_data, *plat_data, *plon_data;
  const bool *is_apseq_data;

  ParamsReader(Vector &params_vec, idx_t count) {
    auto &e = StructVector::GetEntries(params_vec);
    GetStructEntry(e, 0).ToUnifiedFormat(count, proj_fmt);
    proj_data = UnifiedVectorFormat::GetData<string_t>(proj_fmt);
    GetStructEntry(e, 1).ToUnifiedFormat(count, apt_fmt);
    apt_data = UnifiedVectorFormat::GetData<int32_t>(apt_fmt);
    GetStructEntry(e, 2).ToUnifiedFormat(count, topo_fmt);
    topo_data = UnifiedVectorFormat::GetData<string_t>(topo_fmt);
    GetStructEntry(e, 3).ToUnifiedFormat(count, az_fmt);
    az_data = UnifiedVectorFormat::GetData<double>(az_fmt);
    GetStructEntry(e, 4).ToUnifiedFormat(count, plat_fmt);
    plat_data = UnifiedVectorFormat::GetData<double>(plat_fmt);
    GetStructEntry(e, 5).ToUnifiedFormat(count, plon_fmt);
    plon_data = UnifiedVectorFormat::GetData<double>(plon_fmt);
    GetStructEntry(e, 6).ToUnifiedFormat(count, is_apseq_fmt);
    is_apseq_data = UnifiedVectorFormat::GetData<bool>(is_apseq_fmt);
    GetStructEntry(e, 7).ToUnifiedFormat(count, apseq_fmt);
    apseq_data = UnifiedVectorFormat::GetData<string_t>(apseq_fmt);
  }

  dggrid::DggsParams operator[](idx_t i) const {
    dggrid::DggsParams p;
    p.projection = proj_data[proj_fmt.sel->get_index(i)].GetString();
    p.aperture = static_cast<unsigned int>(apt_data[apt_fmt.sel->get_index(i)]);
    p.topology = topo_data[topo_fmt.sel->get_index(i)].GetString();
    p.azimuth_deg = az_data[az_fmt.sel->get_index(i)];
    p.pole_lat_deg = plat_data[plat_fmt.sel->get_index(i)];
    p.pole_lon_deg = plon_data[plon_fmt.sel->get_index(i)];
    p.is_aperture_sequence = is_apseq_data[is_apseq_fmt.sel->get_index(i)];
    p.aperture_sequence = apseq_data[apseq_fmt.sel->get_index(i)].GetString();
    return p;
  }
};

static dggrid::DggsParams paramsWithRes(int32_t res) {
  dggrid::DggsParams p;
  p.res = static_cast<int>(res);
  return p;
}

// ===========================================================================
// dggs_params constructor
// dggs_params(projection, aperture, topology, azimuth_deg, pole_lat_deg,
// pole_lon_deg)
// ===========================================================================

// 6-arg overload: backward compatible (defaults is_aperture_sequence=false)
static void DggsParamsFun(DataChunk &args, ExpressionState &, Vector &result) {
  auto &entries = StructVector::GetEntries(result);
  for (idx_t i = 0; i < 6; i++) {
    GetStructEntry(entries, i).Reference(args.data[i]);
  }
  GetStructEntry(entries, 6).Reference(Value::BOOLEAN(false));
  GetStructEntry(entries, 7).Reference(Value(""));
}

// 8-arg overload: includes aperture sequence
static void DggsParamsApSeqFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  auto &entries = StructVector::GetEntries(result);
  for (idx_t i = 0; i < 8; i++) {
    GetStructEntry(entries, i).Reference(args.data[i]);
  }
}

// ===========================================================================
// version
// ===========================================================================

static void DuckDggsVersionFun(DataChunk &, ExpressionState &, Vector &result) {
  auto ext_version = DuckDggsExtension().Version();
  std::string version = (ext_version.empty() ? "unknown" : ext_version);
  version += " (DGGRID " DGGRID_VERSION ")";
  result.Reference(Value(version));
}

// ===========================================================================
// FROM GEO  (lon DOUBLE, lat DOUBLE, res INTEGER [, params])
// ===========================================================================

static void GeoToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<string_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](const string_t &geom, int32_t res) {
        double lon, lat;
        ReadPointXY(geom, lon, lat);
        return dggrid::geoToSeqNum(paramsWithRes(res), lon, lat);
      });
}
static void GeoToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::geoToSeqNum(p, lon, lat);
  }
}

static void GeoToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    WriteGeo(result, i, dggrid::geoToGeo(paramsWithRes(res[i]), lon, lat));
  }
}
static void GeoToGeoParamsFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    WriteGeo(result, i, dggrid::geoToGeo(p, lon, lat));
  }
}

static void GeoToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    WritePlane(result, i, dggrid::geoToPlane(paramsWithRes(res[i]), lon, lat));
  }
}
static void GeoToPlaneParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    WritePlane(result, i, dggrid::geoToPlane(p, lon, lat));
  }
}

static void GeoToProjTriFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    WriteProjTri(result, i,
                 dggrid::geoToProjTri(paramsWithRes(res[i]), lon, lat));
  }
}
static void GeoToProjTriParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    WriteProjTri(result, i, dggrid::geoToProjTri(p, lon, lat));
  }
}

static void GeoToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    WriteQ2DD(result, i, dggrid::geoToQ2DD(paramsWithRes(res[i]), lon, lat));
  }
}
static void GeoToQ2DDParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    WriteQ2DD(result, i, dggrid::geoToQ2DD(p, lon, lat));
  }
}

static void GeoToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    WriteQ2DI(result, i, dggrid::geoToQ2DI(paramsWithRes(res[i]), lon, lat));
  }
}
static void GeoToQ2DIParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<string_t> geom(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    double lon, lat;
    ReadPointXY(geom[i], lon, lat);
    auto p = params[i];
    p.res = res[i];
    WriteQ2DI(result, i, dggrid::geoToQ2DI(p, lon, lat));
  }
}

// ===========================================================================
// FROM PROJTRI  (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER [, params])
// ===========================================================================

static void ProjTriToGeoFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteGeo(result, i,
             dggrid::projTriToGeo(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}
static void ProjTriToGeoParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteGeo(result, i, dggrid::projTriToGeo(p, tnum[i], x[i], y[i]));
  }
}

static void ProjTriToPlaneFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WritePlane(
        result, i,
        dggrid::projTriToPlane(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}
static void ProjTriToPlaneParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WritePlane(result, i, dggrid::projTriToPlane(p, tnum[i], x[i], y[i]));
  }
}

static void ProjTriToProjTriFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteProjTri(
        result, i,
        dggrid::projTriToProjTri(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}
static void ProjTriToProjTriParamsFun(DataChunk &args, ExpressionState &,
                                      Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteProjTri(result, i, dggrid::projTriToProjTri(p, tnum[i], x[i], y[i]));
  }
}

static void ProjTriToQ2DDFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DD(
        result, i,
        dggrid::projTriToQ2DD(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}
static void ProjTriToQ2DDParamsFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DD(result, i, dggrid::projTriToQ2DD(p, tnum[i], x[i], y[i]));
  }
}

static void ProjTriToQ2DIFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DI(
        result, i,
        dggrid::projTriToQ2DI(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}
static void ProjTriToQ2DIParamsFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DI(result, i, dggrid::projTriToQ2DI(p, tnum[i], x[i], y[i]));
  }
}

static void ProjTriToSeqNumFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] =
        dggrid::projTriToSeqNum(paramsWithRes(res[i]), tnum[i], x[i], y[i]);
}
static void ProjTriToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                     Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> tnum(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::projTriToSeqNum(p, tnum[i], x[i], y[i]);
  }
}

// ===========================================================================
// FROM Q2DD  (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER [, params])
// ===========================================================================

static void Q2DDToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteGeo(result, i,
             dggrid::q2DDToGeo(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}
static void Q2DDToGeoParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteGeo(result, i, dggrid::q2DDToGeo(p, quad[i], x[i], y[i]));
  }
}

static void Q2DDToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WritePlane(result, i,
               dggrid::q2DDToPlane(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}
static void Q2DDToPlaneParamsFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WritePlane(result, i, dggrid::q2DDToPlane(p, quad[i], x[i], y[i]));
  }
}

static void Q2DDToProjTriFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteProjTri(
        result, i,
        dggrid::q2DDToProjTri(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}
static void Q2DDToProjTriParamsFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteProjTri(result, i, dggrid::q2DDToProjTri(p, quad[i], x[i], y[i]));
  }
}

static void Q2DDToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DD(result, i,
              dggrid::q2DDToQ2DD(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}
static void Q2DDToQ2DDParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DD(result, i, dggrid::q2DDToQ2DD(p, quad[i], x[i], y[i]));
  }
}

static void Q2DDToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DI(result, i,
              dggrid::q2DDToQ2DI(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}
static void Q2DDToQ2DIParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DI(result, i, dggrid::q2DDToQ2DI(p, quad[i], x[i], y[i]));
  }
}

static void Q2DDToSeqNumFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::q2DDToSeqNum(paramsWithRes(res[i]), quad[i], x[i], y[i]);
}
static void Q2DDToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<double> x(args, 1, n), y(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::q2DDToSeqNum(p, quad[i], x[i], y[i]);
  }
}

// ===========================================================================
// FROM Q2DI  (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER [, params])
// ===========================================================================

static void Q2DIToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteGeo(result, i,
             dggrid::q2DIToGeo(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}
static void Q2DIToGeoParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteGeo(result, i, dggrid::q2DIToGeo(p, quad[i], ai[i], aj[i]));
  }
}

static void Q2DIToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WritePlane(
        result, i,
        dggrid::q2DIToPlane(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}
static void Q2DIToPlaneParamsFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WritePlane(result, i, dggrid::q2DIToPlane(p, quad[i], ai[i], aj[i]));
  }
}

static void Q2DIToProjTriFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteProjTri(
        result, i,
        dggrid::q2DIToProjTri(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}
static void Q2DIToProjTriParamsFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteProjTri(result, i, dggrid::q2DIToProjTri(p, quad[i], ai[i], aj[i]));
  }
}

static void Q2DIToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DD(result, i,
              dggrid::q2DIToQ2DD(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}
static void Q2DIToQ2DDParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DD(result, i, dggrid::q2DIToQ2DD(p, quad[i], ai[i], aj[i]));
  }
}

static void Q2DIToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DI(result, i,
              dggrid::q2DIToQ2DI(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}
static void Q2DIToQ2DIParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DI(result, i, dggrid::q2DIToQ2DI(p, quad[i], ai[i], aj[i]));
  }
}

static void Q2DIToSeqNumFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::q2DIToSeqNum(paramsWithRes(res[i]), quad[i], ai[i], aj[i]);
}
static void Q2DIToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> quad(args, 0, n);
  ArgReader<int64_t> ai(args, 1, n), aj(args, 2, n);
  ArgReader<int32_t> res(args, 3, n);
  ParamsReader params(args.data[4], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::q2DIToSeqNum(p, quad[i], ai[i], aj[i]);
  }
}

// ===========================================================================
// FROM SEQNUM  (seqnum UBIGINT, res INTEGER [, params])
// ===========================================================================

static void SeqNumToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteGeo(result, i, dggrid::seqNumToGeo(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToGeoParamsFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteGeo(result, i, dggrid::seqNumToGeo(p, seqnum[i]));
  }
}

static void SeqNumToPlaneFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WritePlane(result, i,
               dggrid::seqNumToPlane(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToPlaneParamsFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WritePlane(result, i, dggrid::seqNumToPlane(p, seqnum[i]));
  }
}

static void SeqNumToProjTriFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteProjTri(result, i,
                 dggrid::seqNumToProjTri(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToProjTriParamsFun(DataChunk &args, ExpressionState &,
                                     Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteProjTri(result, i, dggrid::seqNumToProjTri(p, seqnum[i]));
  }
}

static void SeqNumToQ2DDFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DD(result, i,
              dggrid::seqNumToQ2DD(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToQ2DDParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DD(result, i, dggrid::seqNumToQ2DD(p, seqnum[i]));
  }
}

static void SeqNumToQ2DIFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteQ2DI(result, i,
              dggrid::seqNumToQ2DI(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToQ2DIParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteQ2DI(result, i, dggrid::seqNumToQ2DI(p, seqnum[i]));
  }
}

static void SeqNumToBoundaryFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteWKBPolygon(result, i,
                    dggrid::seqNumToBoundary(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToBoundaryParamsFun(DataChunk &args, ExpressionState &,
                                      Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteWKBPolygon(result, i, dggrid::seqNumToBoundary(p, seqnum[i]));
  }
}

static void SeqNumToSeqNumFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t seqnum, int32_t res) {
        return dggrid::seqNumToSeqNum(paramsWithRes(res), seqnum);
      });
}
static void SeqNumToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::seqNumToSeqNum(p, seqnum[i]);
  }
}

// ===========================================================================
// GRID STATISTICS  (res INTEGER [, params])
// ===========================================================================

static void DggsResInfoFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  for (idx_t i = 0; i < n; i++)
    WriteResInfo(result, i, dggrid::getResAt(paramsWithRes(res[i]), res[i]));
}
static void DggsResInfoParamsFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteResInfo(result, i, dggrid::getResAt(p, res[i]));
  }
}

static void DggsNCellsFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).cells;
}
static void DggsNCellsParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::getResAt(p, res[i]).cells;
  }
}

static void DggsCellAreaKMFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).area_km;
}
static void DggsCellAreaKMParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::getResAt(p, res[i]).area_km;
  }
}

static void DggsCellDistKMFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).spacing_km;
}
static void DggsCellDistKMParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::getResAt(p, res[i]).spacing_km;
  }
}

static void DggsClsKMFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).cls_km;
}
static void DggsClsKMParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = FlatVector::GetData<double>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::getResAt(p, res[i]).cls_km;
  }
}

// ===========================================================================
// NEIGHBORS  (seqnum UBIGINT, res INTEGER [, params])  → UBIGINT[]
// ===========================================================================

static void SeqNumNeighborsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    all[i] = dggrid::seqNumNeighbors(paramsWithRes(res[i]), seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto nb : all[i])
      child_data[offset++] = nb;
  }
  ListVector::SetListSize(result, total);
}
static void SeqNumNeighborsParamsFun(DataChunk &args, ExpressionState &,
                                     Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    all[i] = dggrid::seqNumNeighbors(p, seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto nb : all[i])
      child_data[offset++] = nb;
  }
  ListVector::SetListSize(result, total);
}

// ===========================================================================
// PARENT  (seqnum UBIGINT, res INTEGER [, params])  → UBIGINT
// ===========================================================================

static void SeqNumParentFun(DataChunk &args, ExpressionState &,
                            Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t seqnum, int32_t res) {
        return dggrid::seqNumParent(paramsWithRes(res), seqnum);
      });
}
static void SeqNumParentParamsFun(DataChunk &args, ExpressionState &,
                                  Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::seqNumParent(p, seqnum[i]);
  }
}

// ===========================================================================
// CHILDREN  (seqnum UBIGINT, res INTEGER [, params])  → UBIGINT[]
// ===========================================================================

static void SeqNumChildrenFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    all[i] = dggrid::seqNumChildren(paramsWithRes(res[i]), seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto c : all[i])
      child_data[offset++] = c;
  }
  ListVector::SetListSize(result, total);
}
static void SeqNumChildrenParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    all[i] = dggrid::seqNumChildren(p, seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto c : all[i])
      child_data[offset++] = c;
  }
  ListVector::SetListSize(result, total);
}

// ===========================================================================
// HIERARCHICAL ADDRESSES  (seqnum/value UBIGINT, res INTEGER [, params])
// ===========================================================================

// ── ZORDER ──────────────────────────────────────────────────────────────────

static void SeqNumToZOrderFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t seqnum, int32_t res) {
        return dggrid::seqNumToZOrder(paramsWithRes(res), seqnum).value;
      });
}
static void SeqNumToZOrderParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::seqNumToZOrder(p, seqnum[i]).value;
  }
}

static void ZOrderToSeqNumFun(DataChunk &args, ExpressionState &,
                              Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t value, int32_t res) {
        return dggrid::zOrderToSeqNum(paramsWithRes(res), value);
      });
}
static void ZOrderToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> value(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::zOrderToSeqNum(p, value[i]);
  }
}

// ── Z3 ──────────────────────────────────────────────────────────────────────

static void SeqNumToZ3Fun(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t seqnum, int32_t res) {
        return dggrid::seqNumToZ3(paramsWithRes(res), seqnum).value;
      });
}
static void SeqNumToZ3ParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::seqNumToZ3(p, seqnum[i]).value;
  }
}

static void Z3ToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t value, int32_t res) {
        return dggrid::z3ToSeqNum(paramsWithRes(res), value);
      });
}
static void Z3ToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> value(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::z3ToSeqNum(p, value[i]);
  }
}

// ── Z7 ──────────────────────────────────────────────────────────────────────

static void SeqNumToZ7Fun(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t seqnum, int32_t res) {
        return dggrid::seqNumToZ7(paramsWithRes(res), seqnum).value;
      });
}
static void SeqNumToZ7ParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::seqNumToZ7(p, seqnum[i]).value;
  }
}

static void Z7ToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t value, int32_t res) {
        return dggrid::z7ToSeqNum(paramsWithRes(res), value);
      });
}
static void Z7ToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> value(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::z7ToSeqNum(p, value[i]);
  }
}

// ===========================================================================
// ALL PARENTS  (seqnum UBIGINT, res INTEGER [, params])  → UBIGINT[]
// ===========================================================================

static void SeqNumAllParentsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    all[i] = dggrid::seqNumAllParents(paramsWithRes(res[i]), seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto v : all[i])
      child_data[offset++] = v;
  }
  ListVector::SetListSize(result, total);
}
static void SeqNumAllParentsParamsFun(DataChunk &args, ExpressionState &,
                                      Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);

  std::vector<std::vector<uint64_t>> all(n);
  idx_t total = 0;
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    all[i] = dggrid::seqNumAllParents(p, seqnum[i]);
    total += all[i].size();
  }

  ListVector::Reserve(result, total);
  auto list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < n; i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto v : all[i])
      child_data[offset++] = v;
  }
  ListVector::SetListSize(result, total);
}

// ===========================================================================
// VERTEX2DD  (seqnum UBIGINT, res INTEGER [, params])  → STRUCT
//            (keep BOOL, vert_num INT, tri_num INT, x DOUBLE, y DOUBLE,
//             res INTEGER [, params])  → UBIGINT
// ===========================================================================

static void SeqNumToVertex2DDFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  for (idx_t i = 0; i < n; i++)
    WriteVertex2DD(result, i,
                   dggrid::seqNumToVertex2DD(paramsWithRes(res[i]), seqnum[i]));
}
static void SeqNumToVertex2DDParamsFun(DataChunk &args, ExpressionState &,
                                       Vector &result) {
  idx_t n = args.size();
  ArgReader<uint64_t> seqnum(args, 0, n);
  ArgReader<int32_t> res(args, 1, n);
  ParamsReader params(args.data[2], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    WriteVertex2DD(result, i, dggrid::seqNumToVertex2DD(p, seqnum[i]));
  }
}

static void Vertex2DDToSeqNumFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  idx_t n = args.size();
  ArgReader<bool> keep(args, 0, n);
  ArgReader<int32_t> vert_num(args, 1, n);
  ArgReader<int32_t> tri_num(args, 2, n);
  ArgReader<double> x(args, 3, n), y(args, 4, n);
  ArgReader<int32_t> res(args, 5, n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::vertex2DDToSeqNum(paramsWithRes(res[i]), keep[i],
                                       vert_num[i], tri_num[i], x[i], y[i]);
}
static void Vertex2DDToSeqNumParamsFun(DataChunk &args, ExpressionState &,
                                       Vector &result) {
  idx_t n = args.size();
  ArgReader<bool> keep(args, 0, n);
  ArgReader<int32_t> vert_num(args, 1, n);
  ArgReader<int32_t> tri_num(args, 2, n);
  ArgReader<double> x(args, 3, n), y(args, 4, n);
  ArgReader<int32_t> res(args, 5, n);
  ParamsReader params(args.data[6], n);
  auto *out = FlatVector::GetData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::vertex2DDToSeqNum(p, keep[i], vert_num[i], tri_num[i],
                                       x[i], y[i]);
  }
}

// ===========================================================================
// Registration
// ===========================================================================

static void LoadInternal(ExtensionLoader &loader) {
  loader.RegisterFunction(ScalarFunction(
      "duck_dggs_version", {}, LogicalType::VARCHAR, DuckDggsVersionFun));

  // ── dggs_params constructor ───────────────────────────────────────────────
  const auto PARAMS = DggsParamsType();
  {
    const auto V = LogicalType::VARCHAR;
    const auto BI = LogicalType::BOOLEAN;
    ScalarFunctionSet params_set("dggs_params");
    // 6-arg overload (backward compatible)
    params_set.AddFunction(
        ScalarFunction("dggs_params",
                       {V, LogicalType::INTEGER, V, LogicalType::DOUBLE,
                        LogicalType::DOUBLE, LogicalType::DOUBLE},
                       PARAMS, DggsParamsFun));
    // 8-arg overload (with aperture sequence)
    params_set.AddFunction(
        ScalarFunction("dggs_params",
                       {V, LogicalType::INTEGER, V, LogicalType::DOUBLE,
                        LogicalType::DOUBLE, LogicalType::DOUBLE, BI, V},
                       PARAMS, DggsParamsApSeqFun));
    loader.RegisterFunction(params_set);
  }

  // ── arg type shorthands ──────────────────────────────────────────────────
  const auto D = LogicalType::DOUBLE;
  const auto I = LogicalType::INTEGER;
  const auto UB = LogicalType::UBIGINT;
  const auto B = LogicalType::BIGINT;

  // ── return type shorthands ───────────────────────────────────────────────
  const auto GEO = LogicalType::GEOMETRY();
  const auto PLANE = PlaneType();
  const auto PROJTRI = ProjTriType();
  const auto Q2DD = Q2DDType();
  const auto Q2DI = Q2DIType();
  const auto RESINFO = ResInfoType();
  const auto LIST_UB = LogicalType::LIST(UB);

  // Helper: register two overloads for a function (without and with params)
  auto reg = [&](const char *name, vector<LogicalType> base_args,
                 const LogicalType &ret, scalar_function_t base_fn,
                 scalar_function_t params_fn) {
    ScalarFunctionSet set(name);
    set.AddFunction(ScalarFunction(name, base_args, ret, std::move(base_fn)));
    vector<LogicalType> ext_args = base_args;
    ext_args.push_back(PARAMS);
    set.AddFunction(
        ScalarFunction(name, std::move(ext_args), ret, std::move(params_fn)));
    loader.RegisterFunction(set);
  };

  // ── FROM GEO ─────────────────────────────────────────────────────────────
  reg("geo_to_seqnum", {GEO, I}, UB, GeoToSeqNumFun, GeoToSeqNumParamsFun);
  reg("geo_to_geo", {GEO, I}, GEO, GeoToGeoFun, GeoToGeoParamsFun);
  reg("geo_to_plane", {GEO, I}, PLANE, GeoToPlaneFun, GeoToPlaneParamsFun);
  reg("geo_to_projtri", {GEO, I}, PROJTRI, GeoToProjTriFun,
      GeoToProjTriParamsFun);
  reg("geo_to_q2dd", {GEO, I}, Q2DD, GeoToQ2DDFun, GeoToQ2DDParamsFun);
  reg("geo_to_q2di", {GEO, I}, Q2DI, GeoToQ2DIFun, GeoToQ2DIParamsFun);

  // ── FROM PROJTRI ──────────────────────────────────────────────────────────
  reg("projtri_to_geo", {UB, D, D, I}, GEO, ProjTriToGeoFun,
      ProjTriToGeoParamsFun);
  reg("projtri_to_plane", {UB, D, D, I}, PLANE, ProjTriToPlaneFun,
      ProjTriToPlaneParamsFun);
  reg("projtri_to_projtri", {UB, D, D, I}, PROJTRI, ProjTriToProjTriFun,
      ProjTriToProjTriParamsFun);
  reg("projtri_to_q2dd", {UB, D, D, I}, Q2DD, ProjTriToQ2DDFun,
      ProjTriToQ2DDParamsFun);
  reg("projtri_to_q2di", {UB, D, D, I}, Q2DI, ProjTriToQ2DIFun,
      ProjTriToQ2DIParamsFun);
  reg("projtri_to_seqnum", {UB, D, D, I}, UB, ProjTriToSeqNumFun,
      ProjTriToSeqNumParamsFun);

  // ── FROM Q2DD ─────────────────────────────────────────────────────────────
  reg("q2dd_to_geo", {UB, D, D, I}, GEO, Q2DDToGeoFun, Q2DDToGeoParamsFun);
  reg("q2dd_to_plane", {UB, D, D, I}, PLANE, Q2DDToPlaneFun,
      Q2DDToPlaneParamsFun);
  reg("q2dd_to_projtri", {UB, D, D, I}, PROJTRI, Q2DDToProjTriFun,
      Q2DDToProjTriParamsFun);
  reg("q2dd_to_q2dd", {UB, D, D, I}, Q2DD, Q2DDToQ2DDFun, Q2DDToQ2DDParamsFun);
  reg("q2dd_to_q2di", {UB, D, D, I}, Q2DI, Q2DDToQ2DIFun, Q2DDToQ2DIParamsFun);
  reg("q2dd_to_seqnum", {UB, D, D, I}, UB, Q2DDToSeqNumFun,
      Q2DDToSeqNumParamsFun);

  // ── FROM Q2DI ─────────────────────────────────────────────────────────────
  reg("q2di_to_geo", {UB, B, B, I}, GEO, Q2DIToGeoFun, Q2DIToGeoParamsFun);
  reg("q2di_to_plane", {UB, B, B, I}, PLANE, Q2DIToPlaneFun,
      Q2DIToPlaneParamsFun);
  reg("q2di_to_projtri", {UB, B, B, I}, PROJTRI, Q2DIToProjTriFun,
      Q2DIToProjTriParamsFun);
  reg("q2di_to_q2dd", {UB, B, B, I}, Q2DD, Q2DIToQ2DDFun, Q2DIToQ2DDParamsFun);
  reg("q2di_to_q2di", {UB, B, B, I}, Q2DI, Q2DIToQ2DIFun, Q2DIToQ2DIParamsFun);
  reg("q2di_to_seqnum", {UB, B, B, I}, UB, Q2DIToSeqNumFun,
      Q2DIToSeqNumParamsFun);

  // ── FROM SEQNUM ───────────────────────────────────────────────────────────
  reg("seqnum_to_geo", {UB, I}, GEO, SeqNumToGeoFun, SeqNumToGeoParamsFun);
  reg("seqnum_to_plane", {UB, I}, PLANE, SeqNumToPlaneFun,
      SeqNumToPlaneParamsFun);
  reg("seqnum_to_projtri", {UB, I}, PROJTRI, SeqNumToProjTriFun,
      SeqNumToProjTriParamsFun);
  reg("seqnum_to_q2dd", {UB, I}, Q2DD, SeqNumToQ2DDFun, SeqNumToQ2DDParamsFun);
  reg("seqnum_to_q2di", {UB, I}, Q2DI, SeqNumToQ2DIFun, SeqNumToQ2DIParamsFun);
  reg("seqnum_to_seqnum", {UB, I}, UB, SeqNumToSeqNumFun,
      SeqNumToSeqNumParamsFun);
  reg("seqnum_to_boundary", {UB, I}, GEO, SeqNumToBoundaryFun,
      SeqNumToBoundaryParamsFun);

  // ── GRID STATISTICS ───────────────────────────────────────────────────────
  reg("dggs_res_info", {I}, RESINFO, DggsResInfoFun, DggsResInfoParamsFun);
  reg("dggs_n_cells", {I}, UB, DggsNCellsFun, DggsNCellsParamsFun);
  reg("dggs_cell_area_km", {I}, D, DggsCellAreaKMFun, DggsCellAreaKMParamsFun);
  reg("dggs_cell_dist_km", {I}, D, DggsCellDistKMFun, DggsCellDistKMParamsFun);
  reg("dggs_cls_km", {I}, D, DggsClsKMFun, DggsClsKMParamsFun);

  // ── NEIGHBORS ─────────────────────────────────────────────────────────────
  reg("seqnum_neighbors", {UB, I}, LIST_UB, SeqNumNeighborsFun,
      SeqNumNeighborsParamsFun);

  // ── PARENT / CHILD ────────────────────────────────────────────────────────
  reg("seqnum_parent", {UB, I}, UB, SeqNumParentFun, SeqNumParentParamsFun);
  reg("seqnum_all_parents", {UB, I}, LIST_UB, SeqNumAllParentsFun,
      SeqNumAllParentsParamsFun);
  reg("seqnum_children", {UB, I}, LIST_UB, SeqNumChildrenFun,
      SeqNumChildrenParamsFun);

  // ── HIERARCHICAL ADDRESSES ────────────────────────────────────────────────
  {
    const auto VERTEX2DD = Vertex2DDType();
    const auto BO = LogicalType::BOOLEAN;
    reg("seqnum_to_vertex2dd", {UB, I}, VERTEX2DD, SeqNumToVertex2DDFun,
        SeqNumToVertex2DDParamsFun);
    reg("vertex2dd_to_seqnum", {BO, I, I, D, D, I}, UB, Vertex2DDToSeqNumFun,
        Vertex2DDToSeqNumParamsFun);
  }
  reg("seqnum_to_zorder", {UB, I}, UB, SeqNumToZOrderFun,
      SeqNumToZOrderParamsFun);
  reg("zorder_to_seqnum", {UB, I}, UB, ZOrderToSeqNumFun,
      ZOrderToSeqNumParamsFun);
  reg("seqnum_to_z3", {UB, I}, UB, SeqNumToZ3Fun, SeqNumToZ3ParamsFun);
  reg("z3_to_seqnum", {UB, I}, UB, Z3ToSeqNumFun, Z3ToSeqNumParamsFun);
  reg("seqnum_to_z7", {UB, I}, UB, SeqNumToZ7Fun, SeqNumToZ7ParamsFun);
  reg("z7_to_seqnum", {UB, I}, UB, Z7ToSeqNumFun, Z7ToSeqNumParamsFun);

  // ── IGEO7 / Z7 bit-level index manipulation ──────────────────────────────
  RegisterIGeo7Functions(loader);
}

void DuckDggsExtension::Load(ExtensionLoader &loader) { LoadInternal(loader); }

std::string DuckDggsExtension::Name() { return "duck_dggs"; }

std::string DuckDggsExtension::Version() const {
#ifdef EXT_VERSION_DUCK_DGGS
  return EXT_VERSION_DUCK_DGGS;
#else
  return "";
#endif
}

} // namespace duckdb

extern "C" {
DUCKDB_CPP_EXTENSION_ENTRY(duck_dggs, loader) { duckdb::LoadInternal(loader); }
}
