// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_C_API_HPP_
#define YUKIPLT_C_API_HPP_

#include <cstring>
#include <memory>
#include <string_view>

#include "common.hpp"
#include "yukiplt.h"
#include "yukiplt.hpp"

namespace yukiplt::detail::c_objects {
enum class Kind {
  kImages,
  kImports,
  kPlan,
  kSession,
  kRules,
  kRuleReport,
  kBindings,
  kCandidates,
  kMapsCache
};
struct Object {
  explicit Object(Kind value) noexcept : kind(value) {}
  virtual ~Object() = default;
  virtual bool can_destroy() const noexcept { return true; }
  const Kind kind;
};
// Per-copy table. The mutex protects only lookup/publication/removal. Leases
// keep object storage alive without holding the table lock across core calls.
// Callers still serialize access to each object, including destruction.
Result<uint64_t> publish(std::shared_ptr<Object> object);
std::shared_ptr<Object> acquire(uint64_t token, Kind kind);
Error destroy(uint64_t& token, Kind kind);
struct CacheObject final : Object {
  CacheObject() : Object(Kind::kMapsCache) {}
  MapsCache value;
  bool can_destroy() const noexcept override { return !value.has_bindings(); }
};
inline std::shared_ptr<CacheObject> get_cache(const yukiplt_c_maps_cache* cache) {
  return cache ? std::static_pointer_cast<CacheObject>(acquire(cache->token, Kind::kMapsCache))
               : nullptr;
}
inline yukiplt_c_error converted(Error e) noexcept {
  return {static_cast<uint32_t>(e.code), e.sys_errno};
}
inline Error invalid() noexcept { return Error::Make(ErrorCode::kInvalidArgument); }
template <typename F>
yukiplt_c_error boundary(F&& call, CauseContext* cause = nullptr) noexcept {
  if (Error e = process_entry()) return converted(e);
  try {
    return converted(call());
  } catch (...) {
    return converted(current_exception_error(ExceptionOrigin::kInternal, cause));
  }
}
inline ImageView image(const yukiplt_c_image& v) noexcept {
  return {v.load_bias, static_cast<const NativeElfPhdr*>(v.program_headers), v.program_header_count,
          v.instance_token};
}
inline yukiplt_c_image image(const ImageView& v) noexcept {
  return {v.load_bias, v.phdr_table, v.phdr_count, v.instance_token};
}
inline ImportSelector selector(const yukiplt_c_selector& v) noexcept {
  return {v.symbol,     static_cast<VersionMatch>(v.version_match), v.version,
          v.dependency, static_cast<HiddenMatch>(v.hidden),         v.relocation_index};
}
inline bool valid(const yukiplt_c_selector& v) noexcept {
  return v.version_match <= YUKIPLT_C_VERSION_EXACT && v.hidden <= YUKIPLT_C_HIDDEN;
}
inline bool valid(const yukiplt_c_function_selector& v) noexcept {
  return v.struct_size == sizeof(v) && v.index_source <= YUKIPLT_C_RELOC_SOURCE_ANDROID_REL &&
         valid(v.base);
}
inline FunctionImportSelector selector(const yukiplt_c_function_selector& v) noexcept {
  return {selector(v.base), v.sources, v.types, static_cast<RelocationSource>(v.index_source)};
}
}  // namespace yukiplt::detail::c_objects

namespace yukiplt::detail::c_copy {
using c_objects::invalid;
inline bool append_size(size_t& bytes, std::string_view text) noexcept {
  size_t count;
  return detail::safe_add(text.size(), size_t{1}, count) && detail::safe_add(bytes, count, bytes);
}
inline yukiplt_c_text_ref copy_text(std::string_view value, char* text, size_t& offset) noexcept {
  yukiplt_c_text_ref result{offset, value.size()};
  if (!value.empty()) std::memcpy(text + offset, value.data(), value.size());
  text[offset + value.size()] = '\0';
  offset += value.size() + 1;
  return result;
}
inline Error buffers(size_t count, size_t bytes, const void* rows, size_t capacity,
                     const char* text, size_t text_capacity, size_t* required_count,
                     size_t* required_text, bool& sizes_only) noexcept {
  if (!required_count || !required_text) return Error::Make(ErrorCode::kNullPointer);
  if ((!rows && capacity) || (!text && text_capacity)) return invalid();
  sizes_only = !rows && !text && capacity == 0 && text_capacity == 0;
  *required_count = count;
  *required_text = bytes;
  if (!sizes_only &&
      (capacity < count || text_capacity < bytes || (count && !rows) || (bytes && !text)))
    return Error::Make(ErrorCode::kOutOfBounds);
  return Error::Ok();
}
struct RecordView {
  const ImportInfo& import;
  const SymbolVersion* version = nullptr;
  const FunctionImport* function = nullptr;
};
inline RecordView record(const ImportInfo& v) noexcept { return {v}; }
inline RecordView record(const VersionedImport& v) noexcept { return {v.import, &v.version}; }
inline RecordView record(const FunctionImport& v) noexcept {
  return {v.base.import, &v.base.version, &v};
}
constexpr std::string_view empty_text() noexcept { return ""; }

inline yukiplt_c_import_record copy_import(RecordView v, char* text, size_t& offset) noexcept {
  const auto empty = empty_text();
  yukiplt_c_import_record r{};
  r.symbol = copy_text(v.import.symbol, text, offset);
  r.version = copy_text(v.version ? v.version->name : empty, text, offset);
  r.dependency = copy_text(v.version ? v.version->dependency : empty, text, offset);
  r.slot_address = v.import.slot_address;
  r.relocation_index = v.import.relocation_index;
  r.symbol_index = v.import.symbol_index;
  r.version_index = v.version ? v.version->index : 1;
  if (v.version) {
    r.version_table_present = v.version->table_present;
    r.version_hidden = v.version->hidden;
    r.version_source = static_cast<uint32_t>(v.version->source);
  }
  r.relocation_source =
      v.function ? static_cast<uint32_t>(v.function->source) : YUKIPLT_C_RELOC_SOURCE_JMPREL;
  r.relocation_type = v.function ? v.function->relocation_type : YUKIPLT_RELOC_JUMP_SLOT;
  r.symbol_type = v.function ? v.function->symbol_type : 0;

  return r;
}

}  // namespace yukiplt::detail::c_copy

#endif  // YUKIPLT_C_API_HPP_
