#pragma once

// Shims over the DuckDB C++ API so one source tree builds against both the
// v1.5 line and v2.  v2 split the per-class vector helpers out of vector.hpp,
// made the FlatVector / ListVector read accessors const and added *Mutable
// variants for writes, dropped the count argument from ToUnifiedFormat and
// Flatten, requires a count when a Vector references a constant Value, and
// replaced the fixed-arity DefaultMacro struct with a parsed
// "(params) AS body" definition string.

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"

#if __has_include("duckdb/common/vector/list_vector.hpp")
#define DUCK_DGGS_DUCKDB_V2 1
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#else
#define DUCK_DGGS_DUCKDB_V2 0
#endif

namespace duckdb {

// Writable data pointer of a flat vector.
template <class T> inline T *MutableData(Vector &vector) {
#if DUCK_DGGS_DUCKDB_V2
  return FlatVector::GetDataMutable<T>(vector);
#else
  return FlatVector::GetData<T>(vector);
#endif
}

// Writable validity mask of a flat vector.
inline ValidityMask &MutableValidity(Vector &vector) {
#if DUCK_DGGS_DUCKDB_V2
  return FlatVector::ValidityMutable(vector);
#else
  return FlatVector::Validity(vector);
#endif
}

// Child vector of a LIST vector, for writing.
inline Vector &ListChildMutable(Vector &vector) {
#if DUCK_DGGS_DUCKDB_V2
  return ListVector::GetChildMutable(vector);
#else
  return ListVector::GetEntry(vector);
#endif
}

// Writable list_entry_t array of a LIST vector (ListVector::GetData is
// deprecated in v2 in favour of the FlatVector accessor).
inline list_entry_t *ListEntriesMutable(Vector &vector) {
  return MutableData<list_entry_t>(vector);
}

// One entry of StructVector::GetEntries, regardless of whether it is stored
// by value (v2) or behind a unique_ptr (v1.5).
inline Vector &GetStructEntry(vector<Vector> &entries, idx_t i) {
  return entries[i];
}
inline Vector &GetStructEntry(vector<unique_ptr<Vector>> &entries, idx_t i) {
  return *entries[i];
}

// --- Calls whose count argument went away in v2 ---

inline void ToUnified(Vector &vector, idx_t count, UnifiedVectorFormat &data) {
#if DUCK_DGGS_DUCKDB_V2
  (void)count;
  vector.ToUnifiedFormat(data);
#else
  vector.ToUnifiedFormat(count, data);
#endif
}

inline void FlattenVector(Vector &vector, idx_t count) {
#if DUCK_DGGS_DUCKDB_V2
  (void)count;
  vector.Flatten();
#else
  vector.Flatten(count);
#endif
}

// Make `vector` a constant vector holding `value`.  v2 needs the row count.
inline void ReferenceValue(Vector &vector, const Value &value, idx_t count) {
#if DUCK_DGGS_DUCKDB_V2
  vector.Reference(value, count_t(count));
#else
  (void)count;
  vector.Reference(value);
#endif
}

// --- Function registration ---

// Functions that can throw at execution time must say so; v2 turns a runtime
// error from an unmarked function into an internal error.
inline ScalarFunction Fallible(ScalarFunction function) {
  function.SetFallible();
  return function;
}

// A single-parameter SQL macro, expressed independently of the DuckDB
// version.  v1.5's DefaultMacro takes separate parameter / body arrays; v2's
// takes one "(params) AS body" string that it hands to the parser.
struct SqlMacro {
  const char *name;
  const char *parameter;
  const char *body;
};

inline unique_ptr<CreateMacroInfo> MakeMacroInfo(const SqlMacro &macro) {
#if DUCK_DGGS_DUCKDB_V2
  // CreateInternalMacroInfo parses the definition before returning, so the
  // temporary string only has to outlive this call.
  const string definition =
      string("(") + macro.parameter + ") AS " + macro.body;
  const DefaultMacro default_macro{DEFAULT_SCHEMA, macro.name,
                                   definition.c_str()};
#else
  const DefaultMacro default_macro{DEFAULT_SCHEMA,
                                   macro.name,
                                   {macro.parameter, nullptr},
                                   {{nullptr, nullptr}},
                                   macro.body};
#endif
  return DefaultFunctionGenerator::CreateInternalMacroInfo(default_macro);
}

} // namespace duckdb
