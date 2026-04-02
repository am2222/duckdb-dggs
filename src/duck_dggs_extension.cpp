#define DUCKDB_EXTENSION_MAIN

#include "duck_dggs_extension.hpp"
#include "dggrid_transform.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

// ===========================================================================
// Return type helpers
// ===========================================================================

static LogicalType GeoType() {
	child_list_t<LogicalType> c;
	c.push_back({"lon_deg", LogicalType::DOUBLE});
	c.push_back({"lat_deg", LogicalType::DOUBLE});
	return LogicalType::STRUCT(c);
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

// ===========================================================================
// Output writers — fill struct child vectors from a coord value
// ===========================================================================

static void WriteGeo(Vector &result, idx_t i, const dggrid::GeoCoord &c) {
	auto &entries = StructVector::GetEntries(result);
	FlatVector::GetData<double>(entries[0])[i] = c.lon_deg;
	FlatVector::GetData<double>(entries[1])[i] = c.lat_deg;
}

static void WritePlane(Vector &result, idx_t i, const dggrid::PlaneCoord &c) {
	auto &entries = StructVector::GetEntries(result);
	FlatVector::GetData<double>(entries[0])[i] = c.x;
	FlatVector::GetData<double>(entries[1])[i] = c.y;
}

static void WriteProjTri(Vector &result, idx_t i, const dggrid::ProjTriCoord &c) {
	auto &entries = StructVector::GetEntries(result);
	FlatVector::GetData<uint64_t>(entries[0])[i] = c.tnum;
	FlatVector::GetData<double>(entries[1])[i]   = c.x;
	FlatVector::GetData<double>(entries[2])[i]   = c.y;
}

static void WriteQ2DD(Vector &result, idx_t i, const dggrid::Q2DDCoord &c) {
	auto &entries = StructVector::GetEntries(result);
	FlatVector::GetData<uint64_t>(entries[0])[i] = c.quad;
	FlatVector::GetData<double>(entries[1])[i]   = c.x;
	FlatVector::GetData<double>(entries[2])[i]   = c.y;
}

static void WriteQ2DI(Vector &result, idx_t i, const dggrid::Q2DICoord &c) {
	auto &entries = StructVector::GetEntries(result);
	FlatVector::GetData<uint64_t>(entries[0])[i] = c.quad;
	FlatVector::GetData<int64_t>(entries[1])[i]  = c.i;
	FlatVector::GetData<int64_t>(entries[2])[i]  = c.j;
}

// ===========================================================================
// Input readers — extract typed values from UnifiedVectorFormat
// ===========================================================================

// Wrap args into UnifiedVectorFormat and provide typed getters.
// Usage:
//   ArgReader<double> lon(args, 0, count);
//   double v = lon[i];
template <typename T>
struct ArgReader {
	UnifiedVectorFormat fmt;
	const T *data;

	ArgReader(DataChunk &args, idx_t col, idx_t count) {
		args.data[col].ToUnifiedFormat(count, fmt);
		data = UnifiedVectorFormat::GetData<T>(fmt);
	}

	T operator[](idx_t i) const { return data[fmt.sel->get_index(i)]; }
};

// ===========================================================================
// DggsParams builder from the trailing `res` argument
// ===========================================================================

static dggrid::DggsParams paramsWithRes(int32_t res) {
	dggrid::DggsParams p;
	p.res = static_cast<int>(res);
	return p;
}

// ===========================================================================
// version
// ===========================================================================

static void DuckDggsVersionFun(DataChunk &, ExpressionState &, Vector &result) {
	auto version = DuckDggsExtension().Version();
	result.Reference(Value(version.empty() ? "unknown" : version));
}

// ===========================================================================
// FROM GEO  (lon DOUBLE, lat DOUBLE, res INTEGER)
// ===========================================================================

static void GeoToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
	TernaryExecutor::Execute<double, double, int32_t, uint64_t>(
	    args.data[0], args.data[1], args.data[2], result, args.size(),
	    [](double lon, double lat, int32_t res) {
		    return dggrid::geoToSeqNum(paramsWithRes(res), lon, lat);
	    });
}

static void GeoToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<double>  lon(args, 0, n), lat(args, 1, n);
	ArgReader<int32_t> res(args, 2, n);
	for (idx_t i = 0; i < n; i++)
		WriteGeo(result, i, dggrid::geoToGeo(paramsWithRes(res[i]), lon[i], lat[i]));
}

static void GeoToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<double>  lon(args, 0, n), lat(args, 1, n);
	ArgReader<int32_t> res(args, 2, n);
	for (idx_t i = 0; i < n; i++)
		WritePlane(result, i, dggrid::geoToPlane(paramsWithRes(res[i]), lon[i], lat[i]));
}

static void GeoToProjTriFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<double>  lon(args, 0, n), lat(args, 1, n);
	ArgReader<int32_t> res(args, 2, n);
	for (idx_t i = 0; i < n; i++)
		WriteProjTri(result, i, dggrid::geoToProjTri(paramsWithRes(res[i]), lon[i], lat[i]));
}

static void GeoToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<double>  lon(args, 0, n), lat(args, 1, n);
	ArgReader<int32_t> res(args, 2, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DD(result, i, dggrid::geoToQ2DD(paramsWithRes(res[i]), lon[i], lat[i]));
}

static void GeoToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<double>  lon(args, 0, n), lat(args, 1, n);
	ArgReader<int32_t> res(args, 2, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DI(result, i, dggrid::geoToQ2DI(paramsWithRes(res[i]), lon[i], lat[i]));
}

// ===========================================================================
// FROM PROJTRI  (tnum UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
// ===========================================================================

static void ProjTriToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteGeo(result, i, dggrid::projTriToGeo(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}

static void ProjTriToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WritePlane(result, i, dggrid::projTriToPlane(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}

static void ProjTriToProjTriFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteProjTri(result, i, dggrid::projTriToProjTri(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}

static void ProjTriToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DD(result, i, dggrid::projTriToQ2DD(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}

static void ProjTriToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DI(result, i, dggrid::projTriToQ2DI(paramsWithRes(res[i]), tnum[i], x[i], y[i]));
}

static void ProjTriToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> tnum(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	auto *out = FlatVector::GetData<uint64_t>(result);
	for (idx_t i = 0; i < n; i++)
		out[i] = dggrid::projTriToSeqNum(paramsWithRes(res[i]), tnum[i], x[i], y[i]);
}

// ===========================================================================
// FROM Q2DD  (quad UBIGINT, x DOUBLE, y DOUBLE, res INTEGER)
// ===========================================================================

static void Q2DDToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteGeo(result, i, dggrid::q2DDToGeo(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}

static void Q2DDToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WritePlane(result, i, dggrid::q2DDToPlane(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}

static void Q2DDToProjTriFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteProjTri(result, i, dggrid::q2DDToProjTri(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}

static void Q2DDToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DD(result, i, dggrid::q2DDToQ2DD(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}

static void Q2DDToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DI(result, i, dggrid::q2DDToQ2DI(paramsWithRes(res[i]), quad[i], x[i], y[i]));
}

static void Q2DDToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<double>   x(args, 1, n), y(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	auto *out = FlatVector::GetData<uint64_t>(result);
	for (idx_t i = 0; i < n; i++)
		out[i] = dggrid::q2DDToSeqNum(paramsWithRes(res[i]), quad[i], x[i], y[i]);
}

// ===========================================================================
// FROM Q2DI  (quad UBIGINT, i BIGINT, j BIGINT, res INTEGER)
// ===========================================================================

static void Q2DIToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteGeo(result, i, dggrid::q2DIToGeo(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}

static void Q2DIToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WritePlane(result, i, dggrid::q2DIToPlane(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}

static void Q2DIToProjTriFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteProjTri(result, i, dggrid::q2DIToProjTri(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}

static void Q2DIToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DD(result, i, dggrid::q2DIToQ2DD(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}

static void Q2DIToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DI(result, i, dggrid::q2DIToQ2DI(paramsWithRes(res[i]), quad[i], ai[i], aj[i]));
}

static void Q2DIToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> quad(args, 0, n);
	ArgReader<int64_t>  ai(args, 1, n), aj(args, 2, n);
	ArgReader<int32_t>  res(args, 3, n);
	auto *out = FlatVector::GetData<uint64_t>(result);
	for (idx_t i = 0; i < n; i++)
		out[i] = dggrid::q2DIToSeqNum(paramsWithRes(res[i]), quad[i], ai[i], aj[i]);
}

// ===========================================================================
// FROM SEQNUM  (seqnum UBIGINT, res INTEGER)
// ===========================================================================

static void SeqNumToGeoFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> seqnum(args, 0, n);
	ArgReader<int32_t>  res(args, 1, n);
	for (idx_t i = 0; i < n; i++)
		WriteGeo(result, i, dggrid::seqNumToGeo(paramsWithRes(res[i]), seqnum[i]));
}

static void SeqNumToPlaneFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> seqnum(args, 0, n);
	ArgReader<int32_t>  res(args, 1, n);
	for (idx_t i = 0; i < n; i++)
		WritePlane(result, i, dggrid::seqNumToPlane(paramsWithRes(res[i]), seqnum[i]));
}

static void SeqNumToProjTriFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> seqnum(args, 0, n);
	ArgReader<int32_t>  res(args, 1, n);
	for (idx_t i = 0; i < n; i++)
		WriteProjTri(result, i, dggrid::seqNumToProjTri(paramsWithRes(res[i]), seqnum[i]));
}

static void SeqNumToQ2DDFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> seqnum(args, 0, n);
	ArgReader<int32_t>  res(args, 1, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DD(result, i, dggrid::seqNumToQ2DD(paramsWithRes(res[i]), seqnum[i]));
}

static void SeqNumToQ2DIFun(DataChunk &args, ExpressionState &, Vector &result) {
	idx_t n = args.size();
	ArgReader<uint64_t> seqnum(args, 0, n);
	ArgReader<int32_t>  res(args, 1, n);
	for (idx_t i = 0; i < n; i++)
		WriteQ2DI(result, i, dggrid::seqNumToQ2DI(paramsWithRes(res[i]), seqnum[i]));
}

static void SeqNumToSeqNumFun(DataChunk &args, ExpressionState &, Vector &result) {
	BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
	    args.data[0], args.data[1], result, args.size(),
	    [](uint64_t seqnum, int32_t res) {
		    return dggrid::seqNumToSeqNum(paramsWithRes(res), seqnum);
	    });
}

// ===========================================================================
// Registration
// ===========================================================================

static void LoadInternal(ExtensionLoader &loader) {
	loader.RegisterFunction(ScalarFunction("duck_dggs_version", {}, LogicalType::VARCHAR, DuckDggsVersionFun));

	// ── arg type shorthands ──────────────────────────────────────────────────
	const auto D  = LogicalType::DOUBLE;
	const auto I  = LogicalType::INTEGER;
	const auto UB = LogicalType::UBIGINT;
	const auto B  = LogicalType::BIGINT;

	// ── return type shorthands ───────────────────────────────────────────────
	const auto GEO     = GeoType();
	const auto PLANE   = PlaneType();
	const auto PROJTRI = ProjTriType();
	const auto Q2DD    = Q2DDType();
	const auto Q2DI    = Q2DIType();

	// ── FROM GEO ─────────────────────────────────────────────────────────────
	loader.RegisterFunction(ScalarFunction("geo_to_seqnum",  {D, D, I}, UB,     GeoToSeqNumFun));
	loader.RegisterFunction(ScalarFunction("geo_to_geo",     {D, D, I}, GEO,    GeoToGeoFun));
	loader.RegisterFunction(ScalarFunction("geo_to_plane",   {D, D, I}, PLANE,  GeoToPlaneFun));
	loader.RegisterFunction(ScalarFunction("geo_to_projtri", {D, D, I}, PROJTRI,GeoToProjTriFun));
	loader.RegisterFunction(ScalarFunction("geo_to_q2dd",    {D, D, I}, Q2DD,   GeoToQ2DDFun));
	loader.RegisterFunction(ScalarFunction("geo_to_q2di",    {D, D, I}, Q2DI,   GeoToQ2DIFun));

	// ── FROM PROJTRI ──────────────────────────────────────────────────────────
	loader.RegisterFunction(ScalarFunction("projtri_to_geo",     {UB, D, D, I}, GEO,    ProjTriToGeoFun));
	loader.RegisterFunction(ScalarFunction("projtri_to_plane",   {UB, D, D, I}, PLANE,  ProjTriToPlaneFun));
	loader.RegisterFunction(ScalarFunction("projtri_to_projtri", {UB, D, D, I}, PROJTRI,ProjTriToProjTriFun));
	loader.RegisterFunction(ScalarFunction("projtri_to_q2dd",    {UB, D, D, I}, Q2DD,   ProjTriToQ2DDFun));
	loader.RegisterFunction(ScalarFunction("projtri_to_q2di",    {UB, D, D, I}, Q2DI,   ProjTriToQ2DIFun));
	loader.RegisterFunction(ScalarFunction("projtri_to_seqnum",  {UB, D, D, I}, UB,     ProjTriToSeqNumFun));

	// ── FROM Q2DD ─────────────────────────────────────────────────────────────
	loader.RegisterFunction(ScalarFunction("q2dd_to_geo",     {UB, D, D, I}, GEO,    Q2DDToGeoFun));
	loader.RegisterFunction(ScalarFunction("q2dd_to_plane",   {UB, D, D, I}, PLANE,  Q2DDToPlaneFun));
	loader.RegisterFunction(ScalarFunction("q2dd_to_projtri", {UB, D, D, I}, PROJTRI,Q2DDToProjTriFun));
	loader.RegisterFunction(ScalarFunction("q2dd_to_q2dd",    {UB, D, D, I}, Q2DD,   Q2DDToQ2DDFun));
	loader.RegisterFunction(ScalarFunction("q2dd_to_q2di",    {UB, D, D, I}, Q2DI,   Q2DDToQ2DIFun));
	loader.RegisterFunction(ScalarFunction("q2dd_to_seqnum",  {UB, D, D, I}, UB,     Q2DDToSeqNumFun));

	// ── FROM Q2DI ─────────────────────────────────────────────────────────────
	loader.RegisterFunction(ScalarFunction("q2di_to_geo",     {UB, B, B, I}, GEO,    Q2DIToGeoFun));
	loader.RegisterFunction(ScalarFunction("q2di_to_plane",   {UB, B, B, I}, PLANE,  Q2DIToPlaneFun));
	loader.RegisterFunction(ScalarFunction("q2di_to_projtri", {UB, B, B, I}, PROJTRI,Q2DIToProjTriFun));
	loader.RegisterFunction(ScalarFunction("q2di_to_q2dd",    {UB, B, B, I}, Q2DD,   Q2DIToQ2DDFun));
	loader.RegisterFunction(ScalarFunction("q2di_to_q2di",    {UB, B, B, I}, Q2DI,   Q2DIToQ2DIFun));
	loader.RegisterFunction(ScalarFunction("q2di_to_seqnum",  {UB, B, B, I}, UB,     Q2DIToSeqNumFun));

	// ── FROM SEQNUM ───────────────────────────────────────────────────────────
	loader.RegisterFunction(ScalarFunction("seqnum_to_geo",     {UB, I}, GEO,    SeqNumToGeoFun));
	loader.RegisterFunction(ScalarFunction("seqnum_to_plane",   {UB, I}, PLANE,  SeqNumToPlaneFun));
	loader.RegisterFunction(ScalarFunction("seqnum_to_projtri", {UB, I}, PROJTRI,SeqNumToProjTriFun));
	loader.RegisterFunction(ScalarFunction("seqnum_to_q2dd",    {UB, I}, Q2DD,   SeqNumToQ2DDFun));
	loader.RegisterFunction(ScalarFunction("seqnum_to_q2di",    {UB, I}, Q2DI,   SeqNumToQ2DIFun));
	loader.RegisterFunction(ScalarFunction("seqnum_to_seqnum",  {UB, I}, UB,     SeqNumToSeqNumFun));
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
