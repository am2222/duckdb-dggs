#define DUCKDB_EXTENSION_MAIN

#include "duck_dggs_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
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
