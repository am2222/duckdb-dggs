#pragma once

// Helpers for registering functions with DuckDB v2.

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"

namespace duckdb {

// Functions that can throw at execution time must say so; DuckDB turns a
// runtime error from an unmarked function into an internal error.
inline ScalarFunction Fallible(ScalarFunction function) {
  function.SetFallible();
  return function;
}

// Put real parameter names on a function's signature.
//
// DuckDB builds a FunctionSignature for every function; the arguments-only
// constructors auto-name the parameters "col0", "col1", ... duckdb_functions()
// reads names from that signature and only falls back to
// FunctionDescription::parameter_names when the counts disagree, which they
// never do -- so without this the catalog reports col0/col1 and the
// descriptions' names are silently dropped.
inline ScalarFunction WithParameterNames(ScalarFunction function,
                                         const vector<string> &names) {
  auto &signature = function.GetSignature();
  const idx_t count =
      MinValue<idx_t>(signature.GetParameterCount(), names.size());
  for (idx_t i = 0; i < count; i++) {
    signature.GetParameter(i).SetName(Identifier(names[i]));
  }
  return function;
}

// A single-parameter SQL macro.
struct SqlMacro {
  const char *name;
  const char *parameter;
  const char *body;
};

inline unique_ptr<CreateMacroInfo> MakeMacroInfo(const SqlMacro &macro) {
  // DefaultMacro takes one "(params) AS body" definition that it hands to the
  // parser. CreateInternalMacroInfo parses it before returning, so the
  // temporary string only has to outlive this call.
  const string definition =
      string("(") + macro.parameter + ") AS " + macro.body;
  const DefaultMacro default_macro{DEFAULT_SCHEMA, macro.name,
                                   definition.c_str()};
  return DefaultFunctionGenerator::CreateInternalMacroInfo(default_macro);
}

} // namespace duckdb
