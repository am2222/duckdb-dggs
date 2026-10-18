#define DUCKDB_EXTENSION_MAIN

#include "duck_dggs_extension.hpp"
#include "dggrid_transform.hpp"
#include "duck_dggs_compat.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "igeo7_functions.hpp"
#include <dglib/DgBase.h>
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

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
  MutableData<int32_t>(GetStructEntry(entries, 0))[i] =
      static_cast<int32_t>(r.res);
  MutableData<uint64_t>(GetStructEntry(entries, 1))[i] = r.cells;
  MutableData<double>(GetStructEntry(entries, 2))[i] = r.area_km;
  MutableData<double>(GetStructEntry(entries, 3))[i] = r.spacing_km;
  MutableData<double>(GetStructEntry(entries, 4))[i] = r.cls_km;
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
  MutableData<bool>(GetStructEntry(entries, 0))[i] = c.keep;
  MutableData<int32_t>(GetStructEntry(entries, 1))[i] = c.vertNum;
  MutableData<int32_t>(GetStructEntry(entries, 2))[i] = c.triNum;
  MutableData<double>(GetStructEntry(entries, 3))[i] = c.x;
  MutableData<double>(GetStructEntry(entries, 4))[i] = c.y;
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
  MutableData<string_t>(result)[i] = str;
}

static void WritePlane(Vector &result, idx_t i, const dggrid::PlaneCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  MutableData<double>(GetStructEntry(entries, 0))[i] = c.x;
  MutableData<double>(GetStructEntry(entries, 1))[i] = c.y;
}

static void WriteProjTri(Vector &result, idx_t i,
                         const dggrid::ProjTriCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  MutableData<uint64_t>(GetStructEntry(entries, 0))[i] = c.tnum;
  MutableData<double>(GetStructEntry(entries, 1))[i] = c.x;
  MutableData<double>(GetStructEntry(entries, 2))[i] = c.y;
}

static void WriteQ2DD(Vector &result, idx_t i, const dggrid::Q2DDCoord &c) {
  auto &entries = StructVector::GetEntries(result);
  MutableData<uint64_t>(GetStructEntry(entries, 0))[i] = c.quad;
  MutableData<double>(GetStructEntry(entries, 1))[i] = c.x;
  MutableData<double>(GetStructEntry(entries, 2))[i] = c.y;
}

static void WriteQ2DI(Vector &result, idx_t i, const dggrid::Q2DICoord &c) {
  auto &entries = StructVector::GetEntries(result);
  MutableData<uint64_t>(GetStructEntry(entries, 0))[i] = c.quad;
  MutableData<int64_t>(GetStructEntry(entries, 1))[i] = c.i;
  MutableData<int64_t>(GetStructEntry(entries, 2))[i] = c.j;
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
  MutableData<string_t>(result)[i] = str;
}

// ===========================================================================
// Input readers
// ===========================================================================

template <typename T> struct ArgReader {
  UnifiedVectorFormat fmt;
  const T *data;

  ArgReader(DataChunk &args, idx_t col, idx_t count) {
    ToUnified(args.data[col], count, fmt);
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
    ToUnified(GetStructEntry(e, 0), count, proj_fmt);
    proj_data = UnifiedVectorFormat::GetData<string_t>(proj_fmt);
    ToUnified(GetStructEntry(e, 1), count, apt_fmt);
    apt_data = UnifiedVectorFormat::GetData<int32_t>(apt_fmt);
    ToUnified(GetStructEntry(e, 2), count, topo_fmt);
    topo_data = UnifiedVectorFormat::GetData<string_t>(topo_fmt);
    ToUnified(GetStructEntry(e, 3), count, az_fmt);
    az_data = UnifiedVectorFormat::GetData<double>(az_fmt);
    ToUnified(GetStructEntry(e, 4), count, plat_fmt);
    plat_data = UnifiedVectorFormat::GetData<double>(plat_fmt);
    ToUnified(GetStructEntry(e, 5), count, plon_fmt);
    plon_data = UnifiedVectorFormat::GetData<double>(plon_fmt);
    ToUnified(GetStructEntry(e, 6), count, is_apseq_fmt);
    is_apseq_data = UnifiedVectorFormat::GetData<bool>(is_apseq_fmt);
    ToUnified(GetStructEntry(e, 7), count, apseq_fmt);
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
  ReferenceValue(GetStructEntry(entries, 6), Value::BOOLEAN(false),
                 args.size());
  ReferenceValue(GetStructEntry(entries, 7), Value(""), args.size());
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

static void DuckDggsVersionFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  auto ext_version = DuckDggsExtension().Version();
  std::string version = (ext_version.empty() ? "unknown" : ext_version);
  version += " (DGGRID " DGGRID_VERSION ")";
  ReferenceValue(result, Value(version), args.size());
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).cells;
}
static void DggsNCellsParamsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).area_km;
}
static void DggsCellAreaKMParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = MutableData<double>(result);
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
  auto *out = MutableData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).spacing_km;
}
static void DggsCellDistKMParamsFun(DataChunk &args, ExpressionState &,
                                    Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = MutableData<double>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::getResAt(p, res[i]).spacing_km;
  }
}

static void DggsClsKMFun(DataChunk &args, ExpressionState &, Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  auto *out = MutableData<double>(result);
  for (idx_t i = 0; i < n; i++)
    out[i] = dggrid::getResAt(paramsWithRes(res[i]), res[i]).cls_km;
}
static void DggsClsKMParamsFun(DataChunk &args, ExpressionState &,
                               Vector &result) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, 0, n);
  ParamsReader params(args.data[1], n);
  auto *out = MutableData<double>(result);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
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
  auto *out = MutableData<uint64_t>(result);
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
  auto *out = MutableData<uint64_t>(result);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    out[i] = dggrid::vertex2DDToSeqNum(p, keep[i], vert_num[i], tri_num[i],
                                       x[i], y[i]);
  }
}

// ===========================================================================
// Shared helpers for the list-returning functions below
// ===========================================================================

// Writes one UBIGINT list per row.
static void WriteSeqNumLists(Vector &result,
                             const std::vector<std::vector<uint64_t>> &all) {
  idx_t total = 0;
  for (auto &l : all)
    total += l.size();
  ListVector::Reserve(result, total);
  auto list_entries = ListEntriesMutable(result);
  auto &child = ListChildMutable(result);
  auto child_data = MutableData<uint64_t>(child);
  idx_t offset = 0;
  for (idx_t i = 0; i < all.size(); i++) {
    list_entries[i].offset = offset;
    list_entries[i].length = all[i].size();
    for (auto v : all[i])
      child_data[offset++] = v;
  }
  ListVector::SetListSize(result, total);
}

// Runs fn(params, row) for every row of a (..., res [, params]) call, with
// res read from column res_col and params from the column after it when
// has_params is set.
template <class FN>
static void ForEachRowParams(DataChunk &args, idx_t res_col, bool has_params,
                             FN &&fn) {
  idx_t n = args.size();
  ArgReader<int32_t> res(args, res_col, n);
  if (!has_params) {
    for (idx_t i = 0; i < n; i++)
      fn(paramsWithRes(res[i]), i);
    return;
  }
  ParamsReader params(args.data[res_col + 1], n);
  for (idx_t i = 0; i < n; i++) {
    auto p = params[i];
    p.res = res[i];
    fn(p, i);
  }
}

// ===========================================================================
// INDEX PARENT / CHILDREN  (seqnum UBIGINT, res INTEGER [, params])
// ===========================================================================

template <bool PARAMS>
static void SeqNumIndexParentFun(DataChunk &args, ExpressionState &,
                                 Vector &result) {
  ArgReader<uint64_t> seqnum(args, 0, args.size());
  auto *out = MutableData<uint64_t>(result);
  ForEachRowParams(args, 1, PARAMS, [&](const dggrid::DggsParams &p, idx_t i) {
    out[i] = dggrid::seqNumIndexParent(p, seqnum[i]);
  });
}

template <bool PARAMS>
static void SeqNumIndexChildrenFun(DataChunk &args, ExpressionState &,
                                   Vector &result) {
  ArgReader<uint64_t> seqnum(args, 0, args.size());
  std::vector<std::vector<uint64_t>> all(args.size());
  ForEachRowParams(args, 1, PARAMS, [&](const dggrid::DggsParams &p, idx_t i) {
    all[i] = dggrid::seqNumIndexChildren(p, seqnum[i]);
  });
  WriteSeqNumLists(result, all);
}

// ===========================================================================
// K-RING  (seqnum UBIGINT, k INTEGER, res INTEGER [, params])  → UBIGINT[]
// ===========================================================================

template <bool PARAMS>
static void SeqNumKRingFun(DataChunk &args, ExpressionState &, Vector &result) {
  ArgReader<uint64_t> seqnum(args, 0, args.size());
  ArgReader<int32_t> k(args, 1, args.size());
  std::vector<std::vector<uint64_t>> all(args.size());
  ForEachRowParams(args, 2, PARAMS, [&](const dggrid::DggsParams &p, idx_t i) {
    all[i] = dggrid::seqNumKRing(p, seqnum[i], k[i]);
  });
  WriteSeqNumLists(result, all);
}

// ===========================================================================
// CELL AREA  (seqnum UBIGINT, res INTEGER [, params])  → DOUBLE
// ===========================================================================

template <bool PARAMS>
static void SeqNumCellAreaKMFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  ArgReader<uint64_t> seqnum(args, 0, args.size());
  auto *out = MutableData<double>(result);
  ForEachRowParams(args, 1, PARAMS, [&](const dggrid::DggsParams &p, idx_t i) {
    out[i] = dggrid::seqNumCellAreaKM(p, seqnum[i]);
  });
}

// ===========================================================================
// GEODESY  (a GEOMETRY, b GEOMETRY)  → DOUBLE
// ===========================================================================

static void GeoDistanceKMFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  BinaryExecutor::Execute<string_t, string_t, double>(
      args.data[0], args.data[1], result, args.size(),
      [](string_t a, string_t b) {
        double lon1, lat1, lon2, lat2;
        ReadPointXY(a, lon1, lat1);
        ReadPointXY(b, lon2, lat2);
        return dggrid::geoDistanceKM(lon1, lat1, lon2, lat2);
      });
}

static void GeoAzimuthDegFun(DataChunk &args, ExpressionState &,
                             Vector &result) {
  BinaryExecutor::Execute<string_t, string_t, double>(
      args.data[0], args.data[1], result, args.size(),
      [](string_t a, string_t b) {
        double lon1, lat1, lon2, lat2;
        ReadPointXY(a, lon1, lat1);
        ReadPointXY(b, lon2, lat2);
        return dggrid::geoAzimuthDeg(lon1, lat1, lon2, lat2);
      });
}

// ===========================================================================
// POLYGON COVERAGE  (geom GEOMETRY, res INTEGER [, params])  → UBIGINT[]
// ===========================================================================

// Minimal WKB reader for POLYGON / MULTIPOLYGON (and GEOMETRYCOLLECTIONs of
// them). Handles either byte order and the ISO / EWKB Z and M flags; only x
// and y are kept.
class PolygonWKBReader {
public:
  explicit PolygonWKBReader(const string_t &geom)
      : data_(reinterpret_cast<const uint8_t *>(geom.GetData())),
        size_(geom.GetSize()) {}

  std::vector<dggrid::GeoPolygon> Read() {
    std::vector<dggrid::GeoPolygon> polys;
    ReadGeometry(polys);
    return polys;
  }

private:
  struct Header {
    uint32_t type;
    uint32_t dims;
  };

  void Need(std::size_t n) const {
    if (off_ + n > size_)
      throw InvalidInputException("polygon_to_seqnums: truncated WKB");
  }
  // Copies n bytes at the cursor into out, reversing them for big-endian
  // WKB (the extension only targets little-endian hosts).
  void Take(void *out, std::size_t n) {
    Need(n);
    auto *o = static_cast<uint8_t *>(out);
    for (std::size_t i = 0; i < n; i++)
      o[i] = data_[off_ + (le_ ? i : n - 1 - i)];
    off_ += n;
  }
  uint32_t U32() {
    uint32_t v;
    Take(&v, 4);
    return v;
  }
  double F64() {
    double d;
    Take(&d, 8);
    return d;
  }

  Header ReadHeader() {
    Need(1);
    le_ = data_[off_++] == 1;
    uint32_t raw = U32();
    uint32_t dims = 2;
    if (raw & 0x80000000u) // EWKB Z
      dims++;
    if (raw & 0x40000000u) // EWKB M
      dims++;
    if (raw & 0x20000000u) // EWKB SRID
      U32();
    uint32_t type = raw & 0x0FFFFFFFu;
    // ISO: 1000s digit flags Z (1), M (2) or ZM (3).
    switch (type / 1000) {
    case 1:
    case 2:
      dims = 3;
      break;
    case 3:
      dims = 4;
      break;
    default:
      break;
    }
    return {type % 1000, dims};
  }

  void ReadPolygonBody(uint32_t dims, std::vector<dggrid::GeoPolygon> &out) {
    dggrid::GeoPolygon poly;
    const uint32_t num_rings = U32();
    for (uint32_t r = 0; r < num_rings; r++) {
      const uint32_t num_points = U32();
      Need(static_cast<std::size_t>(num_points) * dims * 8);
      std::vector<dggrid::GeoCoord> ring;
      ring.reserve(num_points);
      for (uint32_t i = 0; i < num_points; i++) {
        dggrid::GeoCoord c;
        c.lon_deg = F64();
        c.lat_deg = F64();
        for (uint32_t d = 2; d < dims; d++)
          F64();
        ring.push_back(c);
      }
      if (ring.size() >= 3)
        poly.push_back(std::move(ring));
    }
    if (!poly.empty())
      out.push_back(std::move(poly));
  }

  void ReadGeometry(std::vector<dggrid::GeoPolygon> &out) {
    const Header h = ReadHeader();
    switch (h.type) {
    case 3: // POLYGON
      ReadPolygonBody(h.dims, out);
      break;
    case 6: // MULTIPOLYGON
    case 7: // GEOMETRYCOLLECTION
    {
      const uint32_t n = U32();
      for (uint32_t i = 0; i < n; i++)
        ReadGeometry(out);
      break;
    }
    default:
      throw InvalidInputException(
          "polygon_to_seqnums expects a POLYGON or MULTIPOLYGON geometry");
    }
  }

  const uint8_t *data_;
  std::size_t size_;
  std::size_t off_ = 0;
  bool le_ = true;
};

template <bool PARAMS>
static void PolygonToSeqNumsFun(DataChunk &args, ExpressionState &,
                                Vector &result) {
  ArgReader<string_t> geom(args, 0, args.size());
  std::vector<std::vector<uint64_t>> all(args.size());
  ForEachRowParams(args, 1, PARAMS, [&](const dggrid::DggsParams &p, idx_t i) {
    all[i] = dggrid::polygonToSeqNums(p, PolygonWKBReader(geom[i]).Read());
  });
  WriteSeqNumLists(result, all);
}

// ===========================================================================
// Registration
// ===========================================================================

// The default grid (ISEA aperture-4 hexagons), spelled out. Used in the
// generated examples for the overloads that take an explicit params struct.
static constexpr const char *ISEA4H =
    "dggs_params('ISEA', 4, 'HEXAGON', 0.0, 58.28252559, 11.25)";
static constexpr const char *ISEA3H =
    "dggs_params('ISEA', 3, 'HEXAGON', 0.0, 58.28252559, 11.25)";
static constexpr const char *ISEA7H =
    "dggs_params('ISEA', 7, 'HEXAGON', 0.0, 58.28252559, 11.25)";

// Documentation for one transform function. Every transform is registered as
// a pair of overloads — the base signature, and the same signature with a
// trailing dggs_params struct — so one TransformDoc produces two
// FunctionDescriptions.
struct TransformDoc {
  //! Argument names of the base overload; "params" is appended for the
  //! second overload.
  vector<string> parameter_names;
  //! Example arguments for the base overload, e.g. "2380::UBIGINT, 5".
  const char *example_args;
  //! One sentence on what the function does.
  const char *description;
  //! Category tags.
  vector<string> categories;
  //! Grid the examples run on. nullptr means the function works on the
  //! default ISEA4H grid, so the base overload's example is a bare call.
  //! A non-null value means the function only works on that grid, and both
  //! examples pass it explicitly.
  const char *grid = nullptr;
};

static void LoadInternal(ExtensionLoader &loader) {
  {
    CreateScalarFunctionInfo info(ScalarFunction(
        "duck_dggs_version", {}, LogicalType::VARCHAR, DuckDggsVersionFun));
    // What the bare RegisterFunction(ScalarFunction) overload does
    // internally; CreateInfo itself defaults to ERROR_ON_CONFLICT.
    info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
    FunctionDescription desc;
    desc.description = "Returns the version of the loaded duck_dggs extension "
                       "together with the version of the bundled DGGRID "
                       "library.";
    desc.examples = {"duck_dggs_version()"};
    desc.categories = {"dggs", "metadata"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
  }

  // ── dggs_params constructor ───────────────────────────────────────────────
  const auto PARAMS = DggsParamsType();
  {
    const auto V = LogicalType::VARCHAR;
    const auto BI = LogicalType::BOOLEAN;
    const vector<LogicalType> six = {V,
                                     LogicalType::INTEGER,
                                     V,
                                     LogicalType::DOUBLE,
                                     LogicalType::DOUBLE,
                                     LogicalType::DOUBLE};
    vector<LogicalType> eight = six;
    eight.push_back(BI);
    eight.push_back(V);

    const vector<string> six_names = {"projection",   "aperture",
                                      "topology",     "azimuth_deg",
                                      "pole_lat_deg", "pole_lon_deg"};
    vector<string> eight_names = six_names;
    eight_names.push_back("is_aperture_sequence");
    eight_names.push_back("aperture_sequence");

    ScalarFunctionSet params_set("dggs_params");
    // 6-arg overload (backward compatible)
    params_set.AddFunction(WithParameterNames(
        ScalarFunction("dggs_params", six, PARAMS, DggsParamsFun), six_names));
    // 8-arg overload (with aperture sequence)
    params_set.AddFunction(WithParameterNames(
        ScalarFunction("dggs_params", eight, PARAMS, DggsParamsApSeqFun),
        eight_names));

    CreateScalarFunctionInfo info(std::move(params_set));
    info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;

    FunctionDescription six_desc;
    six_desc.parameter_types = six;
    six_desc.parameter_names = six_names;
    six_desc.description =
        "Builds the grid-configuration struct accepted as the optional last "
        "argument of every transform function, selecting the projection "
        "('ISEA' or 'FULLER'), the aperture (3, 4 or 7), the cell topology "
        "('HEXAGON', 'TRIANGLE' or 'DIAMOND') and the icosahedron "
        "orientation.";
    six_desc.examples = {ISEA4H};
    six_desc.categories = {"dggs", "configuration"};

    FunctionDescription eight_desc = six_desc;
    eight_desc.parameter_types = std::move(eight);
    eight_desc.parameter_names = eight_names;
    eight_desc.description =
        "Builds the grid-configuration struct accepted as the optional last "
        "argument of every transform function, with a mixed-aperture grid: "
        "when is_aperture_sequence is true, aperture_sequence gives the "
        "aperture ('3', '4' or '7') used at each resolution level.";
    eight_desc.examples = {"dggs_params('ISEA', 3, 'HEXAGON', 0.0, "
                           "58.28252559, 11.25, true, '3437')"};

    info.descriptions.push_back(std::move(six_desc));
    info.descriptions.push_back(std::move(eight_desc));
    loader.RegisterFunction(std::move(info));
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

  // Helper: register two overloads for a function (without and with params),
  // documenting both.
  auto reg = [&](const char *name, vector<LogicalType> base_args,
                 const LogicalType &ret, scalar_function_t base_fn,
                 scalar_function_t params_fn, const TransformDoc &doc) {
    // Every transform can throw (bad resolution, unknown projection, ...).
    vector<string> ext_names = doc.parameter_names;
    ext_names.push_back("params");

    ScalarFunctionSet set(name);
    set.AddFunction(Fallible(WithParameterNames(
        ScalarFunction(name, base_args, ret, std::move(base_fn)),
        doc.parameter_names)));
    vector<LogicalType> ext_args = base_args;
    ext_args.push_back(PARAMS);
    set.AddFunction(Fallible(WithParameterNames(
        ScalarFunction(name, ext_args, ret, std::move(params_fn)), ext_names)));

    CreateScalarFunctionInfo info(std::move(set));
    // What the bare RegisterFunction(ScalarFunctionSet) overload does
    // internally; CreateInfo itself defaults to ERROR_ON_CONFLICT.
    info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;

    const string grid = doc.grid ? doc.grid : ISEA4H;
    const string call = string(name) + "(" + doc.example_args;

    FunctionDescription base_desc;
    base_desc.parameter_types = std::move(base_args);
    base_desc.parameter_names = doc.parameter_names;
    base_desc.description = doc.description;
    base_desc.categories = doc.categories;
    // A function that needs a non-default grid has no runnable bare call, so
    // its first example passes the grid too.
    base_desc.examples = {doc.grid ? call + ", " + grid + ")" : call + ")"};

    FunctionDescription params_desc = base_desc;
    params_desc.parameter_types = std::move(ext_args);
    params_desc.parameter_names = std::move(ext_names);
    params_desc.examples = {call + ", " + grid + ")"};

    info.descriptions.push_back(std::move(base_desc));
    info.descriptions.push_back(std::move(params_desc));
    loader.RegisterFunction(std::move(info));
  };

  // ── shared argument names / example arguments ────────────────────────────
  const vector<string> GEO_RES = {"geom", "res"};
  const vector<string> SEQ_RES = {"seqnum", "res"};
  const vector<string> Q2DI_RES = {"quad", "i", "j", "res"};
  const vector<string> Q2DD_RES = {"quad", "x", "y", "res"};
  const vector<string> TRI_RES = {"tnum", "x", "y", "res"};
  const vector<string> RES = {"res"};
  const char *const GEO_EX = "'POINT(0.0 0.0)'::GEOMETRY, 5";
  const char *const SEQ_EX = "2380::UBIGINT, 5";
  const char *const Q2DI_EX = "3, 10, 10, 5";
  const char *const Q2DD_EX = "3, 0.15625, 0.27063, 5";
  const char *const TRI_EX = "7, 0.6875, 0.0, 5";
  const char *const RES_EX = "5";
  const vector<string> XFORM = {"dggs", "transform"};
  const vector<string> STATS = {"dggs", "grid statistics"};
  const vector<string> HIER = {"dggs", "hierarchy"};

  // ── FROM GEO ─────────────────────────────────────────────────────────────
  reg("geo_to_seqnum", {GEO, I}, UB, GeoToSeqNumFun, GeoToSeqNumParamsFun,
      {GEO_RES, GEO_EX,
       "Returns the sequential cell index (SEQNUM) of the DGGS cell that "
       "contains the given lon/lat point at the given resolution.",
       XFORM});
  reg("geo_to_geo", {GEO, I}, GEO, GeoToGeoFun, GeoToGeoParamsFun,
      {GEO_RES, GEO_EX,
       "Snaps a lon/lat point to the centre of the DGGS cell that contains it "
       "at the given resolution.",
       XFORM});
  reg("geo_to_plane", {GEO, I}, PLANE, GeoToPlaneFun, GeoToPlaneParamsFun,
      {GEO_RES, GEO_EX,
       "Returns the unfolded icosahedron plane coordinates of the DGGS cell "
       "that contains the given lon/lat point.",
       XFORM});
  reg("geo_to_projtri", {GEO, I}, PROJTRI, GeoToProjTriFun,
      GeoToProjTriParamsFun,
      {GEO_RES, GEO_EX,
       "Returns the PROJTRI coordinate (icosahedron triangle number plus x/y "
       "within the projected triangle) of the DGGS cell that contains the "
       "given lon/lat point.",
       XFORM});
  reg("geo_to_q2dd", {GEO, I}, Q2DD, GeoToQ2DDFun, GeoToQ2DDParamsFun,
      {GEO_RES, GEO_EX,
       "Returns the Q2DD coordinate (quad number plus continuous x/y within "
       "the quad) of the DGGS cell that contains the given lon/lat point.",
       XFORM});
  reg("geo_to_q2di", {GEO, I}, Q2DI, GeoToQ2DIFun, GeoToQ2DIParamsFun,
      {GEO_RES, GEO_EX,
       "Returns the Q2DI coordinate (quad number plus integer i/j indices) of "
       "the DGGS cell that contains the given lon/lat point.",
       XFORM});

  // ── FROM PROJTRI ──────────────────────────────────────────────────────────
  reg("projtri_to_geo", {UB, D, D, I}, GEO, ProjTriToGeoFun,
      ProjTriToGeoParamsFun,
      {TRI_RES, TRI_EX,
       "Returns the centre of the DGGS cell at the given PROJTRI coordinate "
       "as a lon/lat POINT.",
       XFORM});
  reg("projtri_to_plane", {UB, D, D, I}, PLANE, ProjTriToPlaneFun,
      ProjTriToPlaneParamsFun,
      {TRI_RES, TRI_EX,
       "Converts a PROJTRI coordinate to unfolded icosahedron plane "
       "coordinates.",
       XFORM});
  reg("projtri_to_projtri", {UB, D, D, I}, PROJTRI, ProjTriToProjTriFun,
      ProjTriToProjTriParamsFun,
      {TRI_RES, TRI_EX,
       "Re-encodes a PROJTRI coordinate through a full round-trip via the "
       "DGGS engine, normalising it to its cell.",
       XFORM});
  reg("projtri_to_q2dd", {UB, D, D, I}, Q2DD, ProjTriToQ2DDFun,
      ProjTriToQ2DDParamsFun,
      {TRI_RES, TRI_EX,
       "Converts a PROJTRI coordinate to Q2DD (quad number plus continuous "
       "x/y within the quad).",
       XFORM});
  reg("projtri_to_q2di", {UB, D, D, I}, Q2DI, ProjTriToQ2DIFun,
      ProjTriToQ2DIParamsFun,
      {TRI_RES, TRI_EX,
       "Converts a PROJTRI coordinate to Q2DI (quad number plus integer i/j "
       "indices).",
       XFORM});
  reg("projtri_to_seqnum", {UB, D, D, I}, UB, ProjTriToSeqNumFun,
      ProjTriToSeqNumParamsFun,
      {TRI_RES, TRI_EX,
       "Converts a PROJTRI coordinate to the sequential index of the DGGS "
       "cell that contains it.",
       XFORM});

  // ── FROM Q2DD ─────────────────────────────────────────────────────────────
  reg("q2dd_to_geo", {UB, D, D, I}, GEO, Q2DDToGeoFun, Q2DDToGeoParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Returns the centre of the DGGS cell at the given Q2DD coordinate as a "
       "lon/lat POINT.",
       XFORM});
  reg("q2dd_to_plane", {UB, D, D, I}, PLANE, Q2DDToPlaneFun,
      Q2DDToPlaneParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Converts a Q2DD coordinate to unfolded icosahedron plane coordinates.",
       XFORM});
  reg("q2dd_to_projtri", {UB, D, D, I}, PROJTRI, Q2DDToProjTriFun,
      Q2DDToProjTriParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Converts a Q2DD coordinate to PROJTRI (icosahedron triangle number "
       "plus x/y within the projected triangle).",
       XFORM});
  reg("q2dd_to_q2dd", {UB, D, D, I}, Q2DD, Q2DDToQ2DDFun, Q2DDToQ2DDParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Re-encodes a Q2DD coordinate through a full round-trip via the DGGS "
       "engine, normalising it to its cell.",
       XFORM});
  reg("q2dd_to_q2di", {UB, D, D, I}, Q2DI, Q2DDToQ2DIFun, Q2DDToQ2DIParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Converts a Q2DD coordinate to Q2DI (quad number plus integer i/j "
       "indices).",
       XFORM});
  reg("q2dd_to_seqnum", {UB, D, D, I}, UB, Q2DDToSeqNumFun,
      Q2DDToSeqNumParamsFun,
      {Q2DD_RES, Q2DD_EX,
       "Converts a Q2DD coordinate to the sequential index of the DGGS cell "
       "that contains it.",
       XFORM});

  // ── FROM Q2DI ─────────────────────────────────────────────────────────────
  reg("q2di_to_geo", {UB, B, B, I}, GEO, Q2DIToGeoFun, Q2DIToGeoParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Returns the centre of the DGGS cell at the given Q2DI coordinate as a "
       "lon/lat POINT.",
       XFORM});
  reg("q2di_to_plane", {UB, B, B, I}, PLANE, Q2DIToPlaneFun,
      Q2DIToPlaneParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Converts a Q2DI coordinate to unfolded icosahedron plane coordinates.",
       XFORM});
  reg("q2di_to_projtri", {UB, B, B, I}, PROJTRI, Q2DIToProjTriFun,
      Q2DIToProjTriParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Converts a Q2DI coordinate to PROJTRI (icosahedron triangle number "
       "plus x/y within the projected triangle).",
       XFORM});
  reg("q2di_to_q2dd", {UB, B, B, I}, Q2DD, Q2DIToQ2DDFun, Q2DIToQ2DDParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Converts a Q2DI coordinate to Q2DD (quad number plus continuous x/y "
       "within the quad).",
       XFORM});
  reg("q2di_to_q2di", {UB, B, B, I}, Q2DI, Q2DIToQ2DIFun, Q2DIToQ2DIParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Re-encodes a Q2DI coordinate through a full round-trip via the DGGS "
       "engine, normalising it to its cell.",
       XFORM});
  reg("q2di_to_seqnum", {UB, B, B, I}, UB, Q2DIToSeqNumFun,
      Q2DIToSeqNumParamsFun,
      {Q2DI_RES, Q2DI_EX,
       "Converts a Q2DI coordinate to the sequential index of the DGGS cell "
       "that contains it.",
       XFORM});

  // ── FROM SEQNUM ───────────────────────────────────────────────────────────
  reg("seqnum_to_geo", {UB, I}, GEO, SeqNumToGeoFun, SeqNumToGeoParamsFun,
      {SEQ_RES, SEQ_EX,
       "Returns the centre of the DGGS cell with the given sequential index "
       "as a lon/lat POINT.",
       XFORM});
  reg("seqnum_to_plane", {UB, I}, PLANE, SeqNumToPlaneFun,
      SeqNumToPlaneParamsFun,
      {SEQ_RES, SEQ_EX,
       "Converts a sequential cell index to unfolded icosahedron plane "
       "coordinates.",
       XFORM});
  reg("seqnum_to_projtri", {UB, I}, PROJTRI, SeqNumToProjTriFun,
      SeqNumToProjTriParamsFun,
      {SEQ_RES, SEQ_EX,
       "Converts a sequential cell index to PROJTRI (icosahedron triangle "
       "number plus x/y within the projected triangle).",
       XFORM});
  reg("seqnum_to_q2dd", {UB, I}, Q2DD, SeqNumToQ2DDFun, SeqNumToQ2DDParamsFun,
      {SEQ_RES, SEQ_EX,
       "Converts a sequential cell index to Q2DD (quad number plus continuous "
       "x/y within the quad).",
       XFORM});
  reg("seqnum_to_q2di", {UB, I}, Q2DI, SeqNumToQ2DIFun, SeqNumToQ2DIParamsFun,
      {SEQ_RES, SEQ_EX,
       "Converts a sequential cell index to Q2DI (quad number plus integer "
       "i/j indices).",
       XFORM});
  reg("seqnum_to_seqnum", {UB, I}, UB, SeqNumToSeqNumFun,
      SeqNumToSeqNumParamsFun,
      {SEQ_RES, SEQ_EX,
       "Re-encodes a sequential cell index through a full round-trip via the "
       "DGGS engine, which validates and normalises it.",
       XFORM});
  reg("seqnum_to_boundary", {UB, I}, GEO, SeqNumToBoundaryFun,
      SeqNumToBoundaryParamsFun,
      {SEQ_RES, SEQ_EX,
       "Returns the boundary of the DGGS cell with the given sequential index "
       "as a closed POLYGON; requires the spatial extension.",
       XFORM});

  // ── GRID STATISTICS ───────────────────────────────────────────────────────
  reg("dggs_res_info", {I}, RESINFO, DggsResInfoFun, DggsResInfoParamsFun,
      {RES, RES_EX,
       "Returns every grid statistic for the given resolution as a single "
       "struct of cell count, area, spacing and characteristic length scale.",
       STATS});
  reg("dggs_n_cells", {I}, UB, DggsNCellsFun, DggsNCellsParamsFun,
      {RES, RES_EX,
       "Returns the total number of cells at the given grid "
       "resolution.",
       STATS});
  reg("dggs_cell_area_km", {I}, D, DggsCellAreaKMFun, DggsCellAreaKMParamsFun,
      {RES, RES_EX,
       "Returns the average cell area in square kilometres at the given grid "
       "resolution.",
       STATS});
  reg("dggs_cell_dist_km", {I}, D, DggsCellDistKMFun, DggsCellDistKMParamsFun,
      {RES, RES_EX,
       "Returns the average distance in kilometres between the centres of "
       "adjacent cells at the given grid resolution.",
       STATS});
  reg("dggs_cls_km", {I}, D, DggsClsKMFun, DggsClsKMParamsFun,
      {RES, RES_EX,
       "Returns the characteristic length scale (CLS) in kilometres at the "
       "given grid resolution.",
       STATS});

  // ── NEIGHBORS ─────────────────────────────────────────────────────────────
  reg("seqnum_neighbors", {UB, I}, LIST_UB, SeqNumNeighborsFun,
      SeqNumNeighborsParamsFun,
      {SEQ_RES,
       SEQ_EX,
       "Returns the sequential indices of the cells topologically adjacent to "
       "the given cell, excluding the cell itself (6 for interior hexagons, 5 "
       "for the pentagons at the icosahedron vertices).",
       {"dggs", "neighbors"}});

  // ── PARENT / CHILD ────────────────────────────────────────────────────────
  reg("seqnum_parent", {UB, I}, UB, SeqNumParentFun, SeqNumParentParamsFun,
      {SEQ_RES, SEQ_EX,
       "Returns the sequential index of the cell at resolution res - 1 that "
       "contains the centre of the given cell.",
       HIER});
  reg("seqnum_all_parents", {UB, I}, LIST_UB, SeqNumAllParentsFun,
      SeqNumAllParentsParamsFun,
      {SEQ_RES, "2412::UBIGINT, 5",
       "Returns the sequential indices of every cell at resolution res - 1 "
       "that the given cell touches, with the containing parent first.",
       HIER});
  reg("seqnum_children", {UB, I}, LIST_UB, SeqNumChildrenFun,
      SeqNumChildrenParamsFun,
      {SEQ_RES, "599::UBIGINT, 4",
       "Returns the sequential indices of the cells at resolution res + 1 "
       "that belong to the given cell (7 for an aperture-4 hexagon grid).",
       HIER});

  // ── HIERARCHICAL ADDRESSES ────────────────────────────────────────────────
  {
    const auto VERTEX2DD = Vertex2DDType();
    const auto BO = LogicalType::BOOLEAN;
    reg("seqnum_to_vertex2dd", {UB, I}, VERTEX2DD, SeqNumToVertex2DDFun,
        SeqNumToVertex2DDParamsFun,
        {SEQ_RES, SEQ_EX,
         "Converts a sequential cell index to VERTEX2DD, which addresses the "
         "cell by vertex-based x/y plus its triangle and vertex numbers.",
         XFORM});
    reg("vertex2dd_to_seqnum", {BO, I, I, D, D, I}, UB, Vertex2DDToSeqNumFun,
        Vertex2DDToSeqNumParamsFun,
        {{"keep", "vert_num", "tri_num", "x", "y", "res"},
         "true, 3, 1, 0.15625, 0.27063293868263705, 5",
         "Converts a VERTEX2DD coordinate back to the sequential index of the "
         "DGGS cell that contains it.",
         XFORM});
  }
  reg("seqnum_to_zorder", {UB, I}, UB, SeqNumToZOrderFun,
      SeqNumToZOrderParamsFun,
      {SEQ_RES, SEQ_EX,
       "Converts a sequential cell index to its Z-order (Morton code) index, "
       "which preserves spatial locality; aperture 3 and 4 grids only.",
       HIER});
  reg("zorder_to_seqnum", {UB, I}, UB, ZOrderToSeqNumFun,
      ZOrderToSeqNumParamsFun,
      {{"zorder", "res"},
       "3688448094816436224::UBIGINT, 5",
       "Converts a Z-order (Morton code) index back to a sequential cell "
       "index; aperture 3 and 4 grids only.",
       HIER});
  reg("seqnum_to_z3", {UB, I}, UB, SeqNumToZ3Fun, SeqNumToZ3ParamsFun,
      {SEQ_RES, "100::UBIGINT, 4",
       "Converts a sequential cell index to its Z3 hierarchical index; "
       "aperture 3 hexagon grids only.",
       HIER, ISEA3H});
  reg("z3_to_seqnum", {UB, I}, UB, Z3ToSeqNumFun, Z3ToSeqNumParamsFun,
      {{"z3", "res"},
       "2994893752201379839::UBIGINT, 4",
       "Converts a Z3 hierarchical index back to a sequential cell index; "
       "aperture 3 hexagon grids only.",
       HIER,
       ISEA3H});
  reg("seqnum_to_z7", {UB, I}, UB, SeqNumToZ7Fun, SeqNumToZ7ParamsFun,
      {SEQ_RES, "100::UBIGINT, 4",
       "Converts a sequential cell index to its Z7 hierarchical index; "
       "aperture 7 hexagon grids only.",
       HIER, ISEA7H});
  reg("z7_to_seqnum", {UB, I}, UB, Z7ToSeqNumFun, Z7ToSeqNumParamsFun,
      {{"z7", "res"},
       "1162491653815009279::UBIGINT, 4",
       "Converts a Z7 hierarchical index back to a sequential cell index; "
       "aperture 7 hexagon grids only.",
       HIER,
       ISEA7H});

  // ── INDEX HIERARCHY (DGGRID v9 indexing parent / children) ───────────────
  reg("seqnum_index_parent", {UB, I}, UB, SeqNumIndexParentFun<false>,
      SeqNumIndexParentFun<true>,
      {SEQ_RES, SEQ_EX,
       "Returns the sequential index of the cell's parent at resolution "
       "res - 1 in the grid's hierarchical indexing system (Z3 for aperture "
       "3, ZOrder for 4, Z7 for 7). Every cell has exactly one index parent, "
       "even where it straddles several spatial parents; single-aperture "
       "hexagon grids only.",
       HIER});
  reg("seqnum_index_children", {UB, I}, LIST_UB, SeqNumIndexChildrenFun<false>,
      SeqNumIndexChildrenFun<true>,
      {SEQ_RES, "599::UBIGINT, 4",
       "Returns the sequential indices of the cell's children at resolution "
       "res + 1 in the grid's hierarchical indexing system (Z3, ZOrder or "
       "Z7). Unlike seqnum_children, the index children of different cells "
       "never overlap; single-aperture hexagon grids only.",
       HIER});

  // ── K-RING ────────────────────────────────────────────────────────────────
  reg("seqnum_k_ring", {UB, I, I}, LIST_UB, SeqNumKRingFun<false>,
      SeqNumKRingFun<true>,
      {{"seqnum", "k", "res"},
       "2380::UBIGINT, 1, 5",
       "Returns the cell and every cell within k neighbor steps of it, "
       "ordered by distance in steps (the cell itself first) and ascending "
       "within each ring.",
       {"dggs", "neighbors"}});

  // ── PER-CELL AREA ─────────────────────────────────────────────────────────
  reg("seqnum_cell_area_km", {UB, I}, D, SeqNumCellAreaKMFun<false>,
      SeqNumCellAreaKMFun<true>,
      {SEQ_RES, SEQ_EX,
       "Returns the true spherical area in km² of one cell, on the WGS84 "
       "authalic sphere. Unlike dggs_cell_area_km, which is the grid "
       "average, this differs between cells (pentagons are smaller).",
       STATS});

  // ── POLYGON COVERAGE ──────────────────────────────────────────────────────
  reg("polygon_to_seqnums", {GEO, I}, LIST_UB, PolygonToSeqNumsFun<false>,
      PolygonToSeqNumsFun<true>,
      {GEO_RES,
       "'POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))'::GEOMETRY, 5",
       "Returns the sequential indices, ascending, of every cell whose centre "
       "lies inside a POLYGON or MULTIPOLYGON (holes are excluded). Edges "
       "are straight lines in longitude/latitude, so the polygon must not "
       "cross the antimeridian.",
       {"dggs", "coverage"}});

  // ── GEODESY (grid independent) ────────────────────────────────────────────
  auto reg_geodesy = [&](const char *name, scalar_function_t fn,
                         const char *description) {
    const vector<string> names = {"a", "b"};
    CreateScalarFunctionInfo info(WithParameterNames(
        ScalarFunction(name, {GEO, GEO}, D, std::move(fn)), names));
    info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
    FunctionDescription desc;
    desc.parameter_types = {GEO, GEO};
    desc.parameter_names = names;
    desc.description = description;
    desc.examples = {string(name) + "('POINT(-75.7 45.4)'::GEOMETRY, "
                                    "'POINT(-0.1 51.5)'::GEOMETRY)"};
    desc.categories = {"dggs", "geodesy"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
  };
  reg_geodesy("geo_distance_km", GeoDistanceKMFun,
              "Returns the great-circle distance in km between two POINT "
              "geometries (x = longitude, y = latitude), on the WGS84 "
              "authalic sphere DGGRID uses.");
  reg_geodesy("geo_azimuth_deg", GeoAzimuthDegFun,
              "Returns the initial great-circle bearing from point a to "
              "point b, in degrees clockwise from north in [0, 360).");

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
