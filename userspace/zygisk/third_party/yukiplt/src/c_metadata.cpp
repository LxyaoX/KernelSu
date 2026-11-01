// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <cstring>
#include <limits>
#include <string_view>
#include <variant>

#include "c_api.hpp"
#include "common.hpp"
#include "imports.hpp"
#include "yukiplt.hpp"

namespace {
using namespace yukiplt;
using namespace yukiplt::detail::c_objects;
using namespace yukiplt::detail::c_copy;
static_assert(YUKIPLT_C_FEATURE_OWNED_IMAGES == 10);
static_assert(YUKIPLT_C_FEATURE_IMPORT_SNAPSHOTS == 11);
static_assert(YUKIPLT_C_SNAPSHOT_IMPORTS == 0 && YUKIPLT_C_SNAPSHOT_VERSIONED == 1 &&
              YUKIPLT_C_SNAPSHOT_FUNCTIONS == 2);
static_assert(YUKIPLT_C_IMAGE_MAIN == 0 &&
              static_cast<uint32_t>(ImageMatch::kMainExecutable) == YUKIPLT_C_IMAGE_MAIN);
static_assert(YUKIPLT_C_IMAGE_PATH == 1 &&
              static_cast<uint32_t>(ImageMatch::kExactPath) == YUKIPLT_C_IMAGE_PATH);
static_assert(YUKIPLT_C_IMAGE_BASENAME == 2 &&
              static_cast<uint32_t>(ImageMatch::kBasename) == YUKIPLT_C_IMAGE_BASENAME);
static_assert(YUKIPLT_C_IMAGE_BIAS == 3 &&
              static_cast<uint32_t>(ImageMatch::kLoadBias) == YUKIPLT_C_IMAGE_BIAS);
static_assert(YUKIPLT_C_VERSION_SOURCE_NONE == 0 &&
              static_cast<uint32_t>(VersionSource::kNone) == YUKIPLT_C_VERSION_SOURCE_NONE);
static_assert(YUKIPLT_C_VERSION_SOURCE_NEEDED == 1 &&
              static_cast<uint32_t>(VersionSource::kNeeded) == YUKIPLT_C_VERSION_SOURCE_NEEDED);
static_assert(YUKIPLT_C_VERSION_SOURCE_DEFINED == 2 &&
              static_cast<uint32_t>(VersionSource::kDefined) == YUKIPLT_C_VERSION_SOURCE_DEFINED);
struct Images final : Object {
  Images() : Object(Kind::kImages) {}
  std::vector<NamedImage> values;
};
struct Imports final : Object {
  Imports() : Object(Kind::kImports) {}
  std::variant<ImportSnapshot, VersionedImportSnapshot, FunctionImportSnapshot> value;
};
template <typename T, typename H>
Error publish_to(std::shared_ptr<T> value, H* out) {
  auto token = publish(std::move(value));
  if (!token) return token.error();
  out->token = token.value();
  return Error::Ok();
}
template <typename T>
Error save(Result<T>&& result, yukiplt_c_import_snapshot* out) {
  if (!result) return result.error();
  auto snapshot = std::make_shared<Imports>();
  snapshot->value = std::move(result.value());
  return publish_to(std::move(snapshot), out);
}
Error inspect_snapshot(const ImageView& view, const NamedImage* named, uint32_t kind,
                       uint32_t sources, yukiplt_c_import_snapshot* out) {
  if (!out) return Error::Make(ErrorCode::kNullPointer);
  if (out->token) return invalid();
  if (kind > YUKIPLT_C_SNAPSHOT_FUNCTIONS || (kind != YUKIPLT_C_SNAPSHOT_FUNCTIONS && sources))
    return invalid();
  if (kind == YUKIPLT_C_SNAPSHOT_IMPORTS)
    return save(named ? inspect_imports_named(*named) : inspect_imports(view), out);
  if (kind == YUKIPLT_C_SNAPSHOT_VERSIONED)
    return save(named ? inspect_versioned_imports_named(*named) : inspect_versioned_imports(view),
                out);
  return save(named ? inspect_function_imports_named(*named, sources)
                    : inspect_function_imports(view, sources),
              out);
}
}  // namespace

extern "C" {
yukiplt_c_error yukiplt_c_find_images(const yukiplt_c_image_selector* selected,
                                      yukiplt_c_image_list* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!selected || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token || selected->struct_size != sizeof(*selected) ||
        selected->match > YUKIPLT_C_IMAGE_BIAS)
      return invalid();
    auto result = find_images(
        {static_cast<ImageMatch>(selected->match), selected->name, selected->load_bias});
    if (!result) return result.error();
    auto value = std::make_shared<Images>();
    value->values = std::move(result.value());
    return publish_to(std::move(value), out);
  });
}
yukiplt_c_error yukiplt_c_find_image_containing(const void* address,
                                                yukiplt_c_image_list* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return invalid();
    auto result = find_image_containing(address);
    if (!result) return result.error();
    auto value = std::make_shared<Images>();
    value->values.push_back(std::move(result.value()));
    return publish_to(std::move(value), out);
  });
}
yukiplt_c_error yukiplt_c_image_list_destroy(yukiplt_c_image_list* value) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return value ? destroy(value->token, Kind::kImages) : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_image_list_copy(const yukiplt_c_image_list* handle,
                                          yukiplt_c_image_record* rows, size_t capacity, char* text,
                                          size_t text_capacity, size_t* required_count,
                                          size_t* required_text) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!handle) return Error::Make(ErrorCode::kNullPointer);
    auto owned = acquire(handle->token, Kind::kImages);
    if (!owned) return invalid();
    auto& values = static_cast<Images&>(*owned).values;
    size_t bytes = 0;
    for (const auto& value : values)
      if (!append_size(bytes, value.path)) return Error::Make(ErrorCode::kIntegerOverflow);
    bool sizes_only = false;
    if (auto e = buffers(values.size(), bytes, rows, capacity, text, text_capacity, required_count,
                         required_text, sizes_only))
      return e;
    if (sizes_only) return Error::Ok();
    size_t offset = 0;
    for (size_t i = 0; i < values.size(); ++i)
      rows[i] = {image(values[i].image), copy_text(values[i].path, text, offset)};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_inspect_imports(const yukiplt_c_image* view, uint32_t kind,
                                          uint32_t sources,
                                          yukiplt_c_import_snapshot* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return view ? inspect_snapshot(image(*view), nullptr, kind, sources, out)
                : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_inspect_imports_named(const yukiplt_c_named_image* view, uint32_t kind,
                                                uint32_t sources,
                                                yukiplt_c_import_snapshot* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!view || !out || !view->path) return Error::Make(ErrorCode::kNullPointer);
    if (out->token || view->struct_size != sizeof(*view) || view->image.instance_token != 0)
      return invalid();
    NamedImage named{image(view->image), view->path};
    return inspect_snapshot(named.image, &named, kind, sources, out);
  });
}
yukiplt_c_error yukiplt_c_select_import(const yukiplt_c_image* view,
                                        const yukiplt_c_selector* selected,
                                        yukiplt_c_import_snapshot* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!view || !selected || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token || !valid(*selected)) return invalid();
    auto result = find_import(image(*view), selector(*selected));
    if (!result) return result.error();
    VersionedImportSnapshot snapshot;
    snapshot.imports.push_back(std::move(result.value()));
    return save(Result<VersionedImportSnapshot>{std::move(snapshot)}, out);
  });
}
yukiplt_c_error yukiplt_c_select_function_import(const yukiplt_c_image* view,
                                                 const yukiplt_c_function_selector* selected,
                                                 yukiplt_c_import_snapshot* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!view || !selected || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token || !valid(*selected)) return invalid();
    auto result = find_function_import(image(*view), selector(*selected));
    if (!result) return result.error();
    FunctionImportSnapshot snapshot;
    snapshot.imports.push_back(std::move(result.value()));
    return save(Result<FunctionImportSnapshot>{std::move(snapshot)}, out);
  });
}
yukiplt_c_error yukiplt_c_import_snapshot_destroy(yukiplt_c_import_snapshot* value) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return value ? destroy(value->token, Kind::kImports) : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_import_snapshot_copy(const yukiplt_c_import_snapshot* handle,
                                               yukiplt_c_import_record* rows, size_t capacity,
                                               char* text, size_t text_capacity,
                                               size_t* required_count, size_t* required_text,
                                               uint32_t* kind, size_t* skipped) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!handle || !kind || !skipped) return Error::Make(ErrorCode::kNullPointer);
    auto owned = acquire(handle->token, Kind::kImports);
    if (!owned) return invalid();
    auto& value = static_cast<Imports&>(*owned).value;
    return std::visit(
        [&](const auto& snapshot) -> Error {
          size_t bytes = 0;
          const auto& empty = empty_text();
          for (const auto& item : snapshot.imports) {
            auto v = record(item);
            if (!append_size(bytes, v.import.symbol) ||
                !append_size(bytes, v.version ? v.version->name : empty) ||
                !append_size(bytes, v.version ? v.version->dependency : empty))
              return Error::Make(ErrorCode::kIntegerOverflow);
          }
          bool sizes_only = false;
          if (auto e = buffers(snapshot.imports.size(), bytes, rows, capacity, text, text_capacity,
                               required_count, required_text, sizes_only))
            return e;
          if (!sizes_only) {
            size_t offset = 0, index = 0;
            for (const auto& item : snapshot.imports) {
              auto v = record(item);
              auto r = copy_import(v, text, offset);
              rows[index++] = r;
            }
          }
          *kind = static_cast<uint32_t>(value.index());
          *skipped = snapshot.skipped_relocations;
          return Error::Ok();
        },
        value);
  });
}
}  // extern "C"

namespace {
using namespace yukiplt;
using namespace yukiplt::detail::c_objects;
using namespace yukiplt::detail::c_copy;
static_assert(YUKIPLT_C_FEATURE_CANDIDATES == 15);
static_assert(YUKIPLT_C_TEXT_ANY == 0 && static_cast<uint32_t>(TextMatchKind::kAny) == 0);
static_assert(YUKIPLT_C_TEXT_EXACT == 1 && static_cast<uint32_t>(TextMatchKind::kExact) == 1);
static_assert(YUKIPLT_C_TEXT_PREFIX == 2 && static_cast<uint32_t>(TextMatchKind::kPrefix) == 2);
static_assert(YUKIPLT_C_TEXT_SUFFIX == 3 && static_cast<uint32_t>(TextMatchKind::kSuffix) == 3);
static_assert(YUKIPLT_C_TEXT_SUBSTRING == 4 &&
              static_cast<uint32_t>(TextMatchKind::kSubstring) == 4);
struct Candidates final : Object {
  Candidates() : Object(Kind::kCandidates) {}
  std::vector<FunctionCandidate> value;
};
bool valid_text(const yukiplt_c_text_match& text) noexcept {
  return text.kind <= YUKIPLT_C_TEXT_SUBSTRING && (text.text || !text.length) &&
         (text.kind != YUKIPLT_C_TEXT_ANY || !text.length);
}
bool valid_pattern(const yukiplt_c_candidate_pattern& p) noexcept {
  return valid_text(p.path) && valid_text(p.symbol);
}
TextMatch match(const yukiplt_c_text_match& text) noexcept {
  return {static_cast<TextMatchKind>(text.kind), {text.text ? text.text : "", text.length}};
}
CandidatePattern pattern(const yukiplt_c_candidate_pattern& p) noexcept {
  return {match(p.path), match(p.symbol)};
}
struct Predicate {
  yukiplt_c_candidate_predicate call;
  void* context;
  Error failure;
};
Result<bool> predicate(const FunctionCandidate& candidate, void* context) {
  auto& host = *static_cast<Predicate*>(context);
  const auto exact = candidate.selector();
  const auto& i = candidate.selected;
  yukiplt_c_candidate_view view{};
  view.image = image(candidate.image);
  view.path = candidate.path.c_str();
  view.selector = {sizeof(view.selector),
                   exact.sources,
                   exact.types,
                   static_cast<uint32_t>(exact.index_source),
                   {exact.base.symbol, static_cast<uint32_t>(exact.base.version_match),
                    exact.base.version, exact.base.dependency,
                    static_cast<uint32_t>(exact.base.hidden), exact.base.relocation_index}};
  view.slot_address = i.base.import.slot_address;
  view.symbol_index = i.base.import.symbol_index;
  view.version_index = i.base.version.index;
  view.version_table_present = i.base.version.table_present;
  view.version_source = static_cast<uint32_t>(i.base.version.source);
  view.relocation_type = i.relocation_type;
  view.symbol_type = i.symbol_type;
  uint32_t accept = 2;
  const auto result = host.call(&view, host.context, &accept);
  if (result.code > YUKIPLT_C_FORK_CHILD_UNAVAILABLE || (!result.code && accept > 1)) {
    host.failure = invalid();
    return detail::with_cause(host.failure, ErrorCause::kHostCallback);
  }
  if (result.code) {
    host.failure = Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
    return detail::with_cause(host.failure, ErrorCause::kHostCallback);
  }
  return accept != 0;
}
}  // namespace

extern "C" {
yukiplt_c_error yukiplt_c_expand_candidates(const yukiplt_c_candidate_image* input, size_t count,
                                            const yukiplt_c_candidate_selection* options,
                                            yukiplt_c_candidates* out,
                                            yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(*diagnostic)) return converted(invalid());
  detail::CauseContext context;
  OperationDiagnostic detail_result;
  auto result = boundary(
      [&]() -> Error {
        if (!options || !out || (!input && count)) return Error::Make(ErrorCode::kNullPointer);
        if (out->token || options->struct_size != sizeof(*options) ||
            !valid_pattern(options->include) || (!options->exclude && options->exclude_count))
          return invalid();
        detail::ScanBudget budget;
        budget.cause = &context;
        if (auto e = budget.take_objects<NamedImage>(count)) return e;
        std::vector<NamedImage> images;
        images.reserve(count);
        for (size_t i = 0; i < count; ++i) {
          if (input[i].struct_size != sizeof(input[i]) || !input[i].path) return invalid();
          const auto length = std::strlen(input[i].path);
          size_t bytes;
          if (!detail::safe_add(length, size_t{1}, bytes))
            return Error::Make(ErrorCode::kIntegerOverflow);
          if (auto e = budget.take_bytes(bytes)) return e;
          images.push_back({image(input[i].image), input[i].path});
        }
        if (auto e = budget.take_objects<CandidatePattern>(options->exclude_count)) return e;
        std::vector<CandidatePattern> exclusions;
        exclusions.reserve(options->exclude_count);
        for (size_t i = 0; i < options->exclude_count; ++i) {
          if (!valid_pattern(options->exclude[i])) return invalid();
          exclusions.push_back(pattern(options->exclude[i]));
        }
        Predicate host{options->predicate, options->context, {}};
        CandidateSelection selection{options->sources,
                                     options->types,
                                     pattern(options->include),
                                     exclusions.data(),
                                     exclusions.size(),
                                     options->predicate ? &predicate : nullptr,
                                     &host,
                                     {options->text_bytes, options->candidates}};
        auto expanded = detail::expand_candidates_budgeted(images.data(), images.size(), selection,
                                                           budget, &detail_result);
        if (!expanded) {
          if (host.failure && host.failure.code == expanded.error().code &&
              host.failure.sys_errno == expanded.error().sys_errno)
            detail_result.cause = ErrorCause::kHostCallback;
          return expanded.error();
        }
        auto owned = std::make_shared<Candidates>();
        owned->value = std::move(expanded.value());
        auto token = publish(owned);
        if (!token) return token.error();
        out->token = token.value();
        return Error::Ok();
      },
      &context);
  if (diagnostic) {
    const bool from_core = static_cast<uint32_t>(detail_result.error.code) == result.code &&
                           detail_result.error.sys_errno == result.sys_errno;
    yukiplt_c_operation_diagnostic d{};
    d.struct_size = sizeof(d);
    d.stage = static_cast<uint32_t>(from_core ? detail_result.stage : OperationStage::kPrepare);
    d.cause = static_cast<uint32_t>(
        from_core ? detail_result.cause
                  : detail::resolve_cause(
                        Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno),
                        context.value));
    d.error = result;
    *diagnostic = d;
  }
  return result;
}
yukiplt_c_error yukiplt_c_candidates_destroy(yukiplt_c_candidates* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return h ? destroy(h->token, Kind::kCandidates) : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_candidates_copy(const yukiplt_c_candidates* h,
                                          yukiplt_c_candidate_record* rows, size_t capacity,
                                          char* text, size_t text_capacity, size_t* required_count,
                                          size_t* required_text) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h) return Error::Make(ErrorCode::kNullPointer);
    auto base = acquire(h->token, Kind::kCandidates);
    if (!base) return invalid();
    auto& values = static_cast<Candidates&>(*base).value;
    size_t bytes = 0;
    for (const auto& c : values) {
      if (!append_size(bytes, c.path) || !append_size(bytes, c.selected.base.import.symbol) ||
          !append_size(bytes, c.selected.base.version.name) ||
          !append_size(bytes, c.selected.base.version.dependency))
        return Error::Make(ErrorCode::kIntegerOverflow);
    }
    bool sizes_only = false;
    if (auto e = buffers(values.size(), bytes, rows, capacity, text, text_capacity, required_count,
                         required_text, sizes_only))
      return e;
    if (sizes_only) return Error::Ok();
    size_t offset = 0, index = 0;
    for (const auto& c : values) {
      auto path = copy_text(c.path, text, offset);
      rows[index++] = {image(c.image), path, copy_import(record(c.selected), text, offset)};
    }
    return Error::Ok();
  });
}
}  // extern "C"
