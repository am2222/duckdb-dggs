// SPDX-License-Identifier: MIT
// IGEO7/Z7 bit-level scalar functions for the duck_dggs extension.
//
// These functions operate on the 64-bit packed IGEO7 / Z7 hexagonal-DGGS
// index form:
//   [63:60]  base cell  (4 bits, values 0–11)
//   [59:57]  digit 1    (3 bits, values 0–6; 7 = unused slot)
//   ...
//   [ 2: 0]  digit 20
//
// The underlying bit manipulation and neighbour logic is provided by the
// vendored Z7 core library under third_party/z7.

#include "igeo7_functions.hpp"

#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"

#include "library.h"

#include <array>
#include <limits>
#include <string>

namespace duckdb {

namespace {

// Stateless default Z7 configuration (pentagon exclusion zones, rotations, …).
const Z7::Z7Configuration DEFAULT_CONFIG{};

// ── igeo7_get_resolution(UBIGINT) → INTEGER ─────────────────────────────────
void GetResolutionFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, int32_t>(
      args.data[0], result, args.size(),
      [](uint64_t raw) { return Z7::Z7Index(raw).resolution(); });
}

// ── igeo7_get_base_cell(UBIGINT) → UTINYINT ─────────────────────────────────
void GetBaseCellFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, uint8_t>(
      args.data[0], result, args.size(), [](uint64_t raw) {
        return static_cast<uint8_t>(Z7::Z7Index(raw).hierarchy.base);
      });
}

// ── igeo7_get_digit(UBIGINT, INTEGER) → UTINYINT ────────────────────────────
void GetDigitFunction(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint8_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t raw, int32_t pos) -> uint8_t {
        if (pos < 1 || pos > 20) {
          return 7;
        }
        Z7::Z7Index idx(raw);
        return static_cast<uint8_t>(*idx[pos]);
      });
}

// ── igeo7_parent_at(UBIGINT, INTEGER) → UBIGINT ─────────────────────────────
void ParentAtFunction(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t raw, int32_t resolution) -> uint64_t {
        if (resolution < 0)
          resolution = 0;
        if (resolution > 20)
          resolution = 20;
        Z7::Z7Index idx(raw);
        Z7::Z7Index parent;
        parent.hierarchy.base = idx.hierarchy.base;
        for (int i = 1; i <= resolution; i++) {
          parent[i] = *idx[i];
        }
        for (int i = resolution + 1; i <= 20; i++) {
          parent[i] = 7;
        }
        return parent.index;
      });
}

// ── igeo7_parent(UBIGINT) → UBIGINT  (one level up) ─────────────────────────
void ParentFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, uint64_t>(
      args.data[0], result, args.size(), [](uint64_t raw) {
        Z7::Z7Index idx(raw);
        int res = idx.resolution();
        int target = res > 0 ? res - 1 : 0;
        Z7::Z7Index parent;
        parent.hierarchy.base = idx.hierarchy.base;
        for (int i = 1; i <= target; i++) {
          parent[i] = *idx[i];
        }
        for (int i = target + 1; i <= 20; i++) {
          parent[i] = 7;
        }
        return parent.index;
      });
}

// ── igeo7_to_string(UBIGINT) → VARCHAR ──────────────────────────────────────
void ToStringFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, string_t>(
      args.data[0], result, args.size(), [&](uint64_t raw) {
        Z7::Z7Index idx(raw);
        return StringVector::AddString(result, idx.str());
      });
}

// ── igeo7_from_string(VARCHAR) → UBIGINT ────────────────────────────────────
void FromStringFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<string_t, uint64_t>(
      args.data[0], result, args.size(),
      [](string_t s) { return Z7::Z7Index(s.GetString()).index; });
}

// ── igeo7_get_neighbours(UBIGINT) → UBIGINT[] (6 entries) ───────────────────
void GetNeighboursFunction(DataChunk &args, ExpressionState &, Vector &result) {
  auto &input = args.data[0];
  auto count = args.size();

  // Flatten so FlatVector::GetData is safe for any input vector shape
  // (e.g. CONSTANT_VECTOR from constant folding).
  input.Flatten(count);

  auto *list_entries = ListVector::GetData(result);
  auto &child = ListVector::GetEntry(result);
  ListVector::SetListSize(result, count * 6);

  auto input_data = FlatVector::GetData<uint64_t>(input);
  auto child_data = FlatVector::GetData<uint64_t>(child);
  auto &input_validity = FlatVector::Validity(input);

  for (idx_t i = 0; i < count; i++) {
    list_entries[i].offset = i * 6;
    list_entries[i].length = 6;

    if (!input_validity.RowIsValid(i)) {
      for (int j = 0; j < 6; j++) {
        child_data[i * 6 + j] = std::numeric_limits<uint64_t>::max();
      }
      continue;
    }

    Z7::Z7Index idx(input_data[i]);
    auto neighbours = Z7::neighbors(idx, DEFAULT_CONFIG);
    for (int j = 0; j < 6; j++) {
      child_data[i * 6 + j] = neighbours[j].index;
    }
  }
}

// ── igeo7_get_neighbour(UBIGINT, INTEGER) → UBIGINT ─────────────────────────
void GetNeighbourFunction(DataChunk &args, ExpressionState &, Vector &result) {
  BinaryExecutor::Execute<uint64_t, int32_t, uint64_t>(
      args.data[0], args.data[1], result, args.size(),
      [](uint64_t raw, int32_t direction) -> uint64_t {
        if (direction < 1 || direction > 6) {
          return std::numeric_limits<uint64_t>::max();
        }
        Z7::Z7Index idx(raw);
        auto neighbours = Z7::neighbors(idx, DEFAULT_CONFIG);
        return neighbours[direction - 1].index;
      });
}

// ── igeo7_first_non_zero(UBIGINT) → INTEGER ─────────────────────────────────
void FirstNonZeroFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, int32_t>(
      args.data[0], result, args.size(), [](uint64_t raw) {
        Z7::Z7Index idx(raw);
        return static_cast<int32_t>(Z7::first_non_zero(idx));
      });
}

// ── igeo7_is_valid(UBIGINT) → BOOLEAN ───────────────────────────────────────
// False iff the index equals the UINT64_MAX sentinel used for invalid cells.
void IsValidFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<uint64_t, bool>(
      args.data[0], result, args.size(),
      [](uint64_t raw) { return raw != std::numeric_limits<uint64_t>::max(); });
}

// ── igeo7_encode(base, d1..d20) → UBIGINT ───────────────────────────────────
// Packs a base cell (0–11) and 20 three-bit digits (each 0–7) into the
// canonical 64-bit Z7 layout. Use digit value 7 for unused slots beyond the
// desired resolution. Inputs are masked to their field widths (base & 0x0F,
// digit & 0x07) — no range check required by callers.
void EncodeFunction(DataChunk &args, ExpressionState &, Vector &result) {
  static constexpr int N_DIGITS = 20;
  static constexpr int shifts[N_DIGITS] = {57, 54, 51, 48, 45, 42, 39,
                                           36, 33, 30, 27, 24, 21, 18,
                                           15, 12, 9,  6,  3,  0};

  const idx_t count = args.size();
  result.SetVectorType(VectorType::FLAT_VECTOR);
  auto *result_data = FlatVector::GetData<uint64_t>(result);
  auto &result_validity = FlatVector::Validity(result);

  // Accept either UTINYINT (typed columns) or INTEGER (literal overload).
  auto read_val = [](const UnifiedVectorFormat &fmt, idx_t row_idx) -> int64_t {
    if (fmt.physical_type == PhysicalType::UINT8) {
      return static_cast<int64_t>(
          UnifiedVectorFormat::GetData<uint8_t>(fmt)[row_idx]);
    }
    return static_cast<int64_t>(
        UnifiedVectorFormat::GetData<int32_t>(fmt)[row_idx]);
  };

  UnifiedVectorFormat base_fmt;
  args.data[0].ToUnifiedFormat(count, base_fmt);

  UnifiedVectorFormat digit_fmt[N_DIGITS];
  for (int k = 0; k < N_DIGITS; k++) {
    args.data[k + 1].ToUnifiedFormat(count, digit_fmt[k]);
  }

  for (idx_t i = 0; i < count; i++) {
    const idx_t base_idx = base_fmt.sel->get_index(i);
    if (!base_fmt.validity.RowIsValid(base_idx)) {
      result_validity.SetInvalid(i);
      continue;
    }

    uint64_t packed =
        static_cast<uint64_t>(read_val(base_fmt, base_idx) & 0x0FL) << 60;
    bool any_null = false;

    for (int k = 0; k < N_DIGITS; k++) {
      const idx_t dk = digit_fmt[k].sel->get_index(i);
      if (!digit_fmt[k].validity.RowIsValid(dk)) {
        any_null = true;
        break;
      }
      packed |= static_cast<uint64_t>(read_val(digit_fmt[k], dk) & 0x07L)
                << shifts[k];
    }

    if (any_null) {
      result_validity.SetInvalid(i);
    } else {
      result_data[i] = packed;
    }
  }
}

// ── igeo7_encode_at_resolution(base, res, d1..d20) → UBIGINT ────────────────
// Pack base + 20 digits, then truncate to the given resolution (slots after
// `res` are overwritten with 7 = padding). Equivalent to
//   igeo7_parent_at(igeo7_encode(base, d1..d20), res)
// but registered as a single scalar so it's available whenever the extension
// is loaded (DuckDB's DefaultMacro caps parameters at 8; this has 22).
void EncodeAtResolutionFunction(DataChunk &args, ExpressionState &,
                                Vector &result) {
  static constexpr int N_DIGITS = 20;
  static constexpr int shifts[N_DIGITS] = {57, 54, 51, 48, 45, 42, 39,
                                           36, 33, 30, 27, 24, 21, 18,
                                           15, 12, 9,  6,  3,  0};

  const idx_t count = args.size();
  result.SetVectorType(VectorType::FLAT_VECTOR);
  auto *result_data = FlatVector::GetData<uint64_t>(result);
  auto &result_validity = FlatVector::Validity(result);

  auto read_val = [](const UnifiedVectorFormat &fmt, idx_t row_idx) -> int64_t {
    if (fmt.physical_type == PhysicalType::UINT8) {
      return static_cast<int64_t>(
          UnifiedVectorFormat::GetData<uint8_t>(fmt)[row_idx]);
    }
    return static_cast<int64_t>(
        UnifiedVectorFormat::GetData<int32_t>(fmt)[row_idx]);
  };

  UnifiedVectorFormat base_fmt;
  args.data[0].ToUnifiedFormat(count, base_fmt);

  UnifiedVectorFormat res_fmt;
  args.data[1].ToUnifiedFormat(count, res_fmt);

  UnifiedVectorFormat digit_fmt[N_DIGITS];
  for (int k = 0; k < N_DIGITS; k++) {
    args.data[k + 2].ToUnifiedFormat(count, digit_fmt[k]);
  }

  for (idx_t i = 0; i < count; i++) {
    const idx_t base_idx = base_fmt.sel->get_index(i);
    const idx_t res_idx = res_fmt.sel->get_index(i);
    if (!base_fmt.validity.RowIsValid(base_idx) ||
        !res_fmt.validity.RowIsValid(res_idx)) {
      result_validity.SetInvalid(i);
      continue;
    }

    int64_t resolution = read_val(res_fmt, res_idx);
    if (resolution < 0)
      resolution = 0;
    if (resolution > 20)
      resolution = 20;

    uint64_t packed =
        static_cast<uint64_t>(read_val(base_fmt, base_idx) & 0x0FL) << 60;
    bool any_null = false;

    for (int k = 0; k < N_DIGITS; k++) {
      uint64_t slot;
      if (k < resolution) {
        const idx_t dk = digit_fmt[k].sel->get_index(i);
        if (!digit_fmt[k].validity.RowIsValid(dk)) {
          any_null = true;
          break;
        }
        slot = static_cast<uint64_t>(read_val(digit_fmt[k], dk) & 0x07L);
      } else {
        slot = 7; // padding for slots beyond the target resolution
      }
      packed |= slot << shifts[k];
    }

    if (any_null) {
      result_validity.SetInvalid(i);
    } else {
      result_data[i] = packed;
    }
  }
}

} // namespace

void RegisterIGeo7Functions(ExtensionLoader &loader) {
  const auto UB = LogicalType::UBIGINT;
  const auto I = LogicalType::INTEGER;
  const auto V = LogicalType::VARCHAR;
  const auto UT = LogicalType::UTINYINT;
  const auto BO = LogicalType::BOOLEAN;

  loader.RegisterFunction(
      ScalarFunction("igeo7_get_resolution", {UB}, I, GetResolutionFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_get_base_cell", {UB}, UT, GetBaseCellFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_get_digit", {UB, I}, UT, GetDigitFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_parent_at", {UB, I}, UB, ParentAtFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_parent", {UB}, UB, ParentFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_to_string", {UB}, V, ToStringFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_from_string", {V}, UB, FromStringFunction));

  loader.RegisterFunction(ScalarFunction("igeo7_get_neighbours", {UB},
                                         LogicalType::LIST(UB),
                                         GetNeighboursFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_get_neighbour", {UB, I}, UB, GetNeighbourFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_first_non_zero", {UB}, I, FirstNonZeroFunction));

  loader.RegisterFunction(
      ScalarFunction("igeo7_is_valid", {UB}, BO, IsValidFunction));

  // igeo7_encode: two overloads so untyped SQL literals (INTEGER) and typed
  // UTINYINT columns both bind cleanly. Values are masked to field widths
  // internally.
  for (auto digit_type : {UT, I}) {
    vector<LogicalType> encode_args;
    encode_args.reserve(21);
    encode_args.push_back(digit_type); // base_cell
    for (int i = 0; i < 20; i++) {
      encode_args.push_back(digit_type); // d1..d20
    }
    loader.RegisterFunction(ScalarFunction(
        "igeo7_encode", std::move(encode_args), UB, EncodeFunction));
  }

  // igeo7_encode_at_resolution: (base, res, d1..d20). Registered as C++
  // rather than a SQL macro because DuckDB's DefaultMacro caps parameters at
  // 8 and this signature has 22.
  for (auto digit_type : {UT, I}) {
    vector<LogicalType> args_vec;
    args_vec.reserve(22);
    args_vec.push_back(digit_type); // base_cell
    args_vec.push_back(I);          // resolution
    for (int i = 0; i < 20; i++) {
      args_vec.push_back(digit_type); // d1..d20
    }
    loader.RegisterFunction(ScalarFunction("igeo7_encode_at_resolution",
                                           std::move(args_vec), UB,
                                           EncodeAtResolutionFunction));
  }

  // ── SQL companion macros ────────────────────────────────────────────────
  // These mirror the five macros shipped by the upstream igeo7_duckdb
  // repository (https://github.com/allixender/igeo7_duckdb) so the full
  // convenience surface is available without a separate .read step.
  // `igeo7_encode_at_resolution` is implemented above as a C++ scalar
  // (22 parameters; DefaultMacro caps at 8).
  static const DefaultMacro IGEO7_MACROS[] = {
      {DEFAULT_SCHEMA,
       "igeo7_decode_str",
       {"raw", nullptr},
       {{nullptr, nullptr}},
       // Verbose "B-d1.d2...d20" form (all 20 slots incl. padding 7s).
       "CONCAT("
       "  (raw >> 60::UBIGINT) & 15::UBIGINT, '-',"
       "  (raw >> 57::UBIGINT) & 7, '.', (raw >> 54::UBIGINT) & 7, '.',"
       "  (raw >> 51::UBIGINT) & 7, '.', (raw >> 48::UBIGINT) & 7, '.',"
       "  (raw >> 45::UBIGINT) & 7, '.', (raw >> 42::UBIGINT) & 7, '.',"
       "  (raw >> 39::UBIGINT) & 7, '.', (raw >> 36::UBIGINT) & 7, '.',"
       "  (raw >> 33::UBIGINT) & 7, '.', (raw >> 30::UBIGINT) & 7, '.',"
       "  (raw >> 27::UBIGINT) & 7, '.', (raw >> 24::UBIGINT) & 7, '.',"
       "  (raw >> 21::UBIGINT) & 7, '.', (raw >> 18::UBIGINT) & 7, '.',"
       "  (raw >> 15::UBIGINT) & 7, '.', (raw >> 12::UBIGINT) & 7, '.',"
       "  (raw >>  9::UBIGINT) & 7, '.', (raw >>  6::UBIGINT) & 7, '.',"
       "  (raw >>  3::UBIGINT) & 7, '.', (raw >>  0::UBIGINT) & 7)"},
      {DEFAULT_SCHEMA,
       "igeo7_string_parent",
       {"s", nullptr},
       {{nullptr, nullptr}},
       "s[1:LENGTH(s) - 1]"},
      {DEFAULT_SCHEMA,
       "igeo7_string_local_pos",
       {"s", nullptr},
       {{nullptr, nullptr}},
       "s[LENGTH(s):LENGTH(s)]"},
      {DEFAULT_SCHEMA,
       "igeo7_string_is_center",
       {"s", nullptr},
       {{nullptr, nullptr}},
       "s[LENGTH(s):LENGTH(s)] = '0'"},
      {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

  for (idx_t i = 0; IGEO7_MACROS[i].name != nullptr; i++) {
    auto info =
        DefaultFunctionGenerator::CreateInternalMacroInfo(IGEO7_MACROS[i]);
    loader.RegisterFunction(*info);
  }
}

} // namespace duckdb
