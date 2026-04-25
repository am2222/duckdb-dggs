#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

void RegisterIGeo7Functions(ExtensionLoader &loader);

} // namespace duckdb
