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
#include "auth/authalic.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
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

// ── igeo7_geo_to_authalic / igeo7_authalic_to_geo (GEOMETRY) → GEOMETRY ─────
// Walk a DuckDB GEOMETRY (little-endian WKB) and remap every vertex's latitude
// (Y coordinate) using the WGS84 authalic-latitude transform from
// auth/authalic.hpp (Karney 2022, polynomial order 6).
//
// The geodetic↔authalic mapping is purely 1-D (φ → ξ), so X/Z/M components are
// copied through unchanged; only the Y double of each coordinate tuple is
// rewritten. Output WKB has identical layout and size to the input.
//
// Supports POINT, LINESTRING, POLYGON, MULTI*, GEOMETRYCOLLECTION, plus
// XYZ/XYM/XYZM dimension flags via the EWKB-style type_id encoding
// (type % 1000 = base type, type / 1000 = dim flag: 0=XY, 1=XYZ, 2=XYM,
// 3=XYZM). Throws on byte-order != little-endian (DuckDB's internal form).
namespace authalic_wkb {

inline uint8_t ReadU8(const uint8_t *src, std::size_t &off) {
  uint8_t v = src[off];
  off += 1;
  return v;
}

inline uint32_t ReadU32(const uint8_t *src, std::size_t &off) {
  uint32_t v;
  std::memcpy(&v, src + off, sizeof(v));
  off += sizeof(v);
  return v;
}

inline double ReadF64(const uint8_t *src, std::size_t &off) {
  double v;
  std::memcpy(&v, src + off, sizeof(v));
  off += sizeof(v);
  return v;
}

inline void WriteU8(uint8_t *dst, std::size_t &off, uint8_t v) {
  dst[off] = v;
  off += 1;
}

inline void WriteU32(uint8_t *dst, std::size_t &off, uint32_t v) {
  std::memcpy(dst + off, &v, sizeof(v));
  off += sizeof(v);
}

inline void WriteF64(uint8_t *dst, std::size_t &off, double v) {
  std::memcpy(dst + off, &v, sizeof(v));
  off += sizeof(v);
}

void TransformVertices(const uint8_t *in, uint8_t *out, std::size_t &in_off,
                       std::size_t &out_off, uint32_t count, uint32_t flag,
                       bool to_authalic) {
  // stride: XY=2, XYZ/XYM=3, XYZM=4 doubles per vertex.
  const uint32_t stride = (flag == 3) ? 4 : (flag == 0 ? 2 : 3);
  for (uint32_t i = 0; i < count; i++) {
    double x = ReadF64(in, in_off);
    double y = ReadF64(in, in_off);
    double y2 = to_authalic ? auth::geodetic_to_authalic(y)
                            : auth::authalic_to_geodetic(y);
    WriteF64(out, out_off, x);
    WriteF64(out, out_off, y2);
    for (uint32_t k = 2; k < stride; k++) {
      WriteF64(out, out_off, ReadF64(in, in_off));
    }
  }
}

void TransformOne(const uint8_t *in, uint8_t *out, std::size_t &in_off,
                  std::size_t &out_off, std::size_t in_size, bool to_authalic) {
  uint8_t order = ReadU8(in, in_off);
  WriteU8(out, out_off, order);
  if (order != 0x01) {
    throw std::runtime_error(
        "igeo7_geo_to_authalic: only little-endian WKB is supported");
  }
  uint32_t meta = ReadU32(in, in_off);
  WriteU32(out, out_off, meta);
  uint32_t low = meta & 0x0000FFFFu;
  uint32_t type_id = low % 1000;
  uint32_t flag_id = low / 1000;
  switch (type_id) {
  case 1: // POINT
    TransformVertices(in, out, in_off, out_off, 1, flag_id, to_authalic);
    break;
  case 2: { // LINESTRING
    uint32_t n = ReadU32(in, in_off);
    WriteU32(out, out_off, n);
    TransformVertices(in, out, in_off, out_off, n, flag_id, to_authalic);
    break;
  }
  case 3: { // POLYGON
    uint32_t rings = ReadU32(in, in_off);
    WriteU32(out, out_off, rings);
    for (uint32_t r = 0; r < rings; r++) {
      uint32_t n = ReadU32(in, in_off);
      WriteU32(out, out_off, n);
      TransformVertices(in, out, in_off, out_off, n, flag_id, to_authalic);
    }
    break;
  }
  case 4:   // MULTIPOINT
  case 5:   // MULTILINESTRING
  case 6:   // MULTIPOLYGON
  case 7: { // GEOMETRYCOLLECTION
    uint32_t parts = ReadU32(in, in_off);
    WriteU32(out, out_off, parts);
    for (uint32_t p = 0; p < parts; p++) {
      TransformOne(in, out, in_off, out_off, in_size, to_authalic);
    }
    break;
  }
  default:
    throw std::runtime_error("igeo7_geo_to_authalic: unsupported WKB type " +
                             std::to_string(type_id));
  }
}

string_t Transform(Vector &result, const string_t &wkb, bool to_authalic) {
  const auto size = wkb.GetSize();
  auto out_str = StringVector::EmptyString(result, size);
  auto *in = reinterpret_cast<const uint8_t *>(wkb.GetData());
  auto *out = reinterpret_cast<uint8_t *>(out_str.GetDataWriteable());
  std::size_t in_off = 0, out_off = 0;
  while (in_off < size) {
    TransformOne(in, out, in_off, out_off, size, to_authalic);
  }
  out_str.Finalize();
  return out_str;
}

} // namespace authalic_wkb

void GeoToAuthalicFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<string_t, string_t>(
      args.data[0], result, args.size(), [&](string_t wkb) {
        return authalic_wkb::Transform(result, wkb, /*to_authalic=*/true);
      });
}

void AuthalicToGeoFunction(DataChunk &args, ExpressionState &, Vector &result) {
  UnaryExecutor::Execute<string_t, string_t>(
      args.data[0], result, args.size(), [&](string_t wkb) {
        return authalic_wkb::Transform(result, wkb, /*to_authalic=*/false);
      });
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
  const auto GEO = LogicalType::GEOMETRY();

  // WGS84 geodetic ↔ authalic latitude conversion (Karney 2022 Fourier series)
  // applied to every vertex Y of a GEOMETRY. Useful before/after equal-area
  // operations on lon/lat data.
  loader.RegisterFunction(ScalarFunction("igeo7_geo_to_authalic", {GEO}, GEO,
                                         GeoToAuthalicFunction));
  loader.RegisterFunction(ScalarFunction("igeo7_authalic_to_geo", {GEO}, GEO,
                                         AuthalicToGeoFunction));

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
