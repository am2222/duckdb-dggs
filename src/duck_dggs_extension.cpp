#define DUCKDB_EXTENSION_MAIN

#include "duck_dggs_extension.hpp"
#include "dggrid_transform.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

inline void DuckDggsVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto version = DuckDggsExtension().Version();
	result.Reference(Value(version.empty() ? "unknown" : version));
}

// geo_to_seqnum(lon DOUBLE, lat DOUBLE, res INTEGER) -> UBIGINT
// Returns the ISEA4H cell sequence number containing the given point.
inline void GeoToSeqNumFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &lon_vec = args.data[0];
	auto &lat_vec = args.data[1];
	auto &res_vec = args.data[2];

	TernaryExecutor::Execute<double, double, int32_t, uint64_t>(
	    lon_vec, lat_vec, res_vec, result, args.size(),
	    [&](double lon, double lat, int32_t res) {
		    dggrid::DggsParams p;
		    p.res = res;
		    return dggrid::geoToSeqNum(p, lon, lat);
	    });
}

static void LoadInternal(ExtensionLoader &loader) {
	auto version_fn = ScalarFunction("duck_dggs_version", {}, LogicalType::VARCHAR, DuckDggsVersionScalarFun);
	loader.RegisterFunction(version_fn);

	auto geo_to_seqnum_fn = ScalarFunction(
	    "geo_to_seqnum",
	    {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::INTEGER},
	    LogicalType::UBIGINT,
	    GeoToSeqNumFun);
	loader.RegisterFunction(geo_to_seqnum_fn);
}

void DuckDggsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string DuckDggsExtension::Name() {
	return "duck_dggs";
}

std::string DuckDggsExtension::Version() const {
#ifdef EXT_VERSION_DUCK_DGGS
	return EXT_VERSION_DUCK_DGGS;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duck_dggs, loader) {
	duckdb::LoadInternal(loader);
}
}
