// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "c_api.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>

#include "common.hpp"
#include "maps_cache.hpp"
#include "memory.hpp"
#include "registry.hpp"
#include "yukiplt.h"
#include "yukiplt.hpp"

namespace {
using namespace yukiplt;
static_assert(static_cast<uint32_t>(VersionMatch::kAny) == YUKIPLT_C_VERSION_ANY);
static_assert(static_cast<uint32_t>(VersionMatch::kUnversioned) == YUKIPLT_C_VERSION_UNVERSIONED);
static_assert(static_cast<uint32_t>(VersionMatch::kExact) == YUKIPLT_C_VERSION_EXACT);
static_assert(static_cast<uint32_t>(HiddenMatch::kAny) == YUKIPLT_C_HIDDEN_ANY);
static_assert(static_cast<uint32_t>(HiddenMatch::kNonHidden) == YUKIPLT_C_NON_HIDDEN);
static_assert(static_cast<uint32_t>(HiddenMatch::kHidden) == YUKIPLT_C_HIDDEN);
static_assert(static_cast<uint32_t>(SlotStatus::kUntouched) == YUKIPLT_C_UNTOUCHED);
static_assert(static_cast<uint32_t>(SlotStatus::kInstalled) == YUKIPLT_C_INSTALLED);
static_assert(static_cast<uint32_t>(SlotStatus::kRestored) == YUKIPLT_C_RESTORED);
static_assert(static_cast<uint32_t>(SlotStatus::kConflict) == YUKIPLT_C_CONFLICT);
static_assert(static_cast<uint32_t>(SlotStatus::kRolledBack) == YUKIPLT_C_ROLLED_BACK);
static_assert(static_cast<uint32_t>(SlotStatus::kRollbackFailed) == YUKIPLT_C_ROLLBACK_FAILED);
static_assert(static_cast<uint32_t>(OperationStage::kNone) == YUKIPLT_C_STAGE_NONE);
static_assert(static_cast<uint32_t>(OperationStage::kPrepare) == YUKIPLT_C_STAGE_PREPARE);
static_assert(static_cast<uint32_t>(OperationStage::kExpectedCurrent) ==
              YUKIPLT_C_STAGE_EXPECTED_CURRENT);
static_assert(static_cast<uint32_t>(OperationStage::kRaise) == YUKIPLT_C_STAGE_RAISE);
static_assert(static_cast<uint32_t>(OperationStage::kCommit) == YUKIPLT_C_STAGE_COMMIT);
static_assert(static_cast<uint32_t>(OperationStage::kRollback) == YUKIPLT_C_STAGE_ROLLBACK);
static_assert(static_cast<uint32_t>(OperationStage::kRestorePages) ==
              YUKIPLT_C_STAGE_RESTORE_PAGES);
static_assert(static_cast<uint32_t>(OperationStage::kComplete) == YUKIPLT_C_STAGE_COMPLETE);
static_assert(static_cast<uint32_t>(RelocationSource::kJmprel) == YUKIPLT_C_RELOC_SOURCE_JMPREL);
static_assert(static_cast<uint32_t>(RelocationSource::kRela) == YUKIPLT_C_RELOC_SOURCE_RELA);
static_assert(static_cast<uint32_t>(RelocationSource::kAndroidRela) ==
              YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA);
static_assert(static_cast<uint32_t>(RelocationSource::kRel) == YUKIPLT_C_RELOC_SOURCE_REL);
static_assert(static_cast<uint32_t>(RelocationSource::kAndroidRel) ==
              YUKIPLT_C_RELOC_SOURCE_ANDROID_REL);
constexpr size_t kLimit = 64;
// Every public C++ enum value that crosses the C boundary is pinned to its C
// literal, so renumbering either side fails to compile.
static_assert(static_cast<uint32_t>(ErrorCode::kSuccess) == YUKIPLT_C_SUCCESS);
static_assert(static_cast<uint32_t>(ErrorCode::kInvalidArgument) == YUKIPLT_C_INVALID_ARGUMENT);
static_assert(static_cast<uint32_t>(ErrorCode::kNullPointer) == YUKIPLT_C_NULL_POINTER);
static_assert(static_cast<uint32_t>(ErrorCode::kInvalidImage) == YUKIPLT_C_INVALID_IMAGE);
static_assert(static_cast<uint32_t>(ErrorCode::kCorruptedElf) == YUKIPLT_C_CORRUPTED_ELF);
static_assert(static_cast<uint32_t>(ErrorCode::kUnsupportedFormat) == YUKIPLT_C_UNSUPPORTED_FORMAT);
static_assert(static_cast<uint32_t>(ErrorCode::kUnsupportedRelocation) ==
              YUKIPLT_C_UNSUPPORTED_RELOCATION);
static_assert(static_cast<uint32_t>(ErrorCode::kSymbolNotFound) == YUKIPLT_C_SYMBOL_NOT_FOUND);
static_assert(static_cast<uint32_t>(ErrorCode::kNoHashTable) == YUKIPLT_C_NO_HASH_TABLE);
static_assert(static_cast<uint32_t>(ErrorCode::kAmbiguousSymbol) == YUKIPLT_C_AMBIGUOUS_SYMBOL);
static_assert(static_cast<uint32_t>(ErrorCode::kSlotOwnershipConflict) ==
              YUKIPLT_C_SLOT_OWNERSHIP_CONFLICT);
static_assert(static_cast<uint32_t>(ErrorCode::kAlreadyHooked) == YUKIPLT_C_ALREADY_HOOKED);
static_assert(static_cast<uint32_t>(ErrorCode::kNotInstalled) == YUKIPLT_C_NOT_INSTALLED);
static_assert(static_cast<uint32_t>(ErrorCode::kSlotModifiedExternally) ==
              YUKIPLT_C_SLOT_MODIFIED_EXTERNALLY);
static_assert(static_cast<uint32_t>(ErrorCode::kIndeterminate) == YUKIPLT_C_INDETERMINATE);
static_assert(static_cast<uint32_t>(ErrorCode::kPermissionDenied) == YUKIPLT_C_PERMISSION_DENIED);
static_assert(static_cast<uint32_t>(ErrorCode::kImageInvalidated) == YUKIPLT_C_IMAGE_INVALIDATED);
static_assert(static_cast<uint32_t>(ErrorCode::kOutOfMemory) == YUKIPLT_C_OUT_OF_MEMORY);
static_assert(static_cast<uint32_t>(ErrorCode::kIntegerOverflow) == YUKIPLT_C_INTEGER_OVERFLOW);
static_assert(static_cast<uint32_t>(ErrorCode::kOutOfBounds) == YUKIPLT_C_OUT_OF_BOUNDS);
static_assert(static_cast<uint32_t>(ErrorCode::kImageNotFound) == YUKIPLT_C_IMAGE_NOT_FOUND);
static_assert(static_cast<uint32_t>(ErrorCode::kAmbiguousImage) == YUKIPLT_C_AMBIGUOUS_IMAGE);
static_assert(static_cast<uint32_t>(ErrorCode::kVmaUnavailable) == YUKIPLT_C_VMA_UNAVAILABLE);
static_assert(static_cast<uint32_t>(SlotStatus::kUntouched) == YUKIPLT_C_UNTOUCHED);
static_assert(static_cast<uint32_t>(SlotStatus::kInstalled) == YUKIPLT_C_INSTALLED);
static_assert(static_cast<uint32_t>(SlotStatus::kRestored) == YUKIPLT_C_RESTORED);
static_assert(static_cast<uint32_t>(SlotStatus::kConflict) == YUKIPLT_C_CONFLICT);
static_assert(static_cast<uint32_t>(SlotStatus::kRolledBack) == YUKIPLT_C_ROLLED_BACK);
static_assert(static_cast<uint32_t>(SlotStatus::kRollbackFailed) == YUKIPLT_C_ROLLBACK_FAILED);
static_assert(static_cast<uint32_t>(OperationStage::kNone) == YUKIPLT_C_STAGE_NONE);
static_assert(static_cast<uint32_t>(OperationStage::kPrepare) == YUKIPLT_C_STAGE_PREPARE);
static_assert(static_cast<uint32_t>(OperationStage::kExpectedCurrent) ==
              YUKIPLT_C_STAGE_EXPECTED_CURRENT);
static_assert(static_cast<uint32_t>(OperationStage::kRaise) == YUKIPLT_C_STAGE_RAISE);
static_assert(static_cast<uint32_t>(OperationStage::kCommit) == YUKIPLT_C_STAGE_COMMIT);
static_assert(static_cast<uint32_t>(OperationStage::kRollback) == YUKIPLT_C_STAGE_ROLLBACK);
static_assert(static_cast<uint32_t>(OperationStage::kRestorePages) ==
              YUKIPLT_C_STAGE_RESTORE_PAGES);
static_assert(static_cast<uint32_t>(OperationStage::kComplete) == YUKIPLT_C_STAGE_COMPLETE);
static_assert(static_cast<uint32_t>(VersionMatch::kAny) == YUKIPLT_C_VERSION_ANY);
static_assert(static_cast<uint32_t>(VersionMatch::kUnversioned) == YUKIPLT_C_VERSION_UNVERSIONED);
static_assert(static_cast<uint32_t>(VersionMatch::kExact) == YUKIPLT_C_VERSION_EXACT);
static_assert(static_cast<uint32_t>(HiddenMatch::kAny) == YUKIPLT_C_HIDDEN_ANY);
static_assert(static_cast<uint32_t>(HiddenMatch::kNonHidden) == YUKIPLT_C_NON_HIDDEN);
static_assert(static_cast<uint32_t>(HiddenMatch::kHidden) == YUKIPLT_C_HIDDEN);
static_assert(static_cast<uint32_t>(kSourceJmprel) == YUKIPLT_C_SOURCE_JMPREL);
static_assert(static_cast<uint32_t>(kSourceRela) == YUKIPLT_C_SOURCE_RELA);
static_assert(static_cast<uint32_t>(kSourceAndroidRela) == YUKIPLT_C_SOURCE_ANDROID_RELA);
static_assert(static_cast<uint32_t>(kSourceRel) == YUKIPLT_C_SOURCE_REL);
static_assert(static_cast<uint32_t>(kSourceAndroidRel) == YUKIPLT_C_SOURCE_ANDROID_REL);
static_assert(static_cast<uint32_t>(kTypeJumpSlot) == YUKIPLT_C_TYPE_JUMP_SLOT);
static_assert(static_cast<uint32_t>(kTypeGlobDat) == YUKIPLT_C_TYPE_GLOB_DAT);
static_assert(static_cast<uint32_t>(RelocationSource::kJmprel) == YUKIPLT_C_RELOC_SOURCE_JMPREL);
static_assert(static_cast<uint32_t>(RelocationSource::kRela) == YUKIPLT_C_RELOC_SOURCE_RELA);
static_assert(static_cast<uint32_t>(RelocationSource::kAndroidRela) ==
              YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA);
static_assert(static_cast<uint32_t>(RelocationSource::kRel) == YUKIPLT_C_RELOC_SOURCE_REL);
static_assert(static_cast<uint32_t>(RelocationSource::kAndroidRel) ==
              YUKIPLT_C_RELOC_SOURCE_ANDROID_REL);
static_assert(static_cast<uint32_t>(ErrorCause::kNone) == YUKIPLT_C_CAUSE_NONE);
static_assert(static_cast<uint32_t>(ErrorCause::kInvalidRequest) ==
              YUKIPLT_C_CAUSE_INVALID_REQUEST);
static_assert(static_cast<uint32_t>(ErrorCause::kHandleInvalid) == YUKIPLT_C_CAUSE_HANDLE_INVALID);
static_assert(static_cast<uint32_t>(ErrorCause::kOwnImage) == YUKIPLT_C_CAUSE_OWN_IMAGE);
static_assert(static_cast<uint32_t>(ErrorCause::kDuplicateTarget) ==
              YUKIPLT_C_CAUSE_DUPLICATE_TARGET);
static_assert(static_cast<uint32_t>(ErrorCause::kOutputAlias) == YUKIPLT_C_CAUSE_OUTPUT_ALIAS);
static_assert(static_cast<uint32_t>(ErrorCause::kReplacementIsCurrent) ==
              YUKIPLT_C_CAUSE_REPLACEMENT_IS_CURRENT);
static_assert(static_cast<uint32_t>(ErrorCause::kOriginalCapacity) ==
              YUKIPLT_C_CAUSE_ORIGINAL_CAPACITY);
static_assert(static_cast<uint32_t>(ErrorCause::kMetadataBounds) ==
              YUKIPLT_C_CAUSE_METADATA_BOUNDS);
static_assert(static_cast<uint32_t>(ErrorCause::kScanBudget) == YUKIPLT_C_CAUSE_SCAN_BUDGET);
static_assert(static_cast<uint32_t>(ErrorCause::kMapsUnavailable) ==
              YUKIPLT_C_CAUSE_MAPS_UNAVAILABLE);
static_assert(static_cast<uint32_t>(ErrorCause::kMapsCoverage) == YUKIPLT_C_CAUSE_MAPS_COVERAGE);
static_assert(static_cast<uint32_t>(ErrorCause::kVmaPolicy) == YUKIPLT_C_CAUSE_VMA_POLICY);
static_assert(static_cast<uint32_t>(ErrorCause::kProtectFailed) == YUKIPLT_C_CAUSE_PROTECT_FAILED);
static_assert(static_cast<uint32_t>(ErrorCause::kExpectedCurrent) ==
              YUKIPLT_C_CAUSE_EXPECTED_CURRENT);
static_assert(static_cast<uint32_t>(ErrorCause::kCasConflict) == YUKIPLT_C_CAUSE_CAS_CONFLICT);
static_assert(static_cast<uint32_t>(ErrorCause::kInternalException) ==
              YUKIPLT_C_CAUSE_INTERNAL_EXCEPTION);
static_assert(static_cast<uint32_t>(ErrorCause::kHostCallback) == YUKIPLT_C_CAUSE_HOST_CALLBACK);
static_assert(static_cast<uint32_t>(ErrorCause::kRandomSource) == YUKIPLT_C_CAUSE_RANDOM_SOURCE);
static_assert(static_cast<uint32_t>(ErrorCause::kPlatform) == YUKIPLT_C_CAUSE_PLATFORM);
static_assert(static_cast<uint32_t>(ErrorCause::kIdentityMismatch) ==
              YUKIPLT_C_CAUSE_IDENTITY_MISMATCH);
static_assert(static_cast<uint32_t>(ErrorCause::kSlotDrift) == YUKIPLT_C_CAUSE_SLOT_DRIFT);
static_assert(static_cast<uint32_t>(ErrorCause::kCacheInvalid) == YUKIPLT_C_CAUSE_CACHE_INVALID);
static_assert(static_cast<uint32_t>(ErrorCause::kCacheConflict) == YUKIPLT_C_CAUSE_CACHE_CONFLICT);
static_assert(static_cast<uint32_t>(ErrorCause::kForkProtocol) == YUKIPLT_C_CAUSE_FORK_PROTOCOL);
yukiplt_c_error converted(Error error) noexcept {
  return {static_cast<uint32_t>(error.code), error.sys_errno};
}
yukiplt_c_recovery_diagnostic converted(const RecoveryDiagnostic& value) noexcept {
  return {static_cast<uint32_t>(value.stage),
          converted(value.error),
          value.raise_errno,
          value.restore_errno,
          value.rollback_failed ? 1u : 0u,
          value.image_invalidated ? 1u : 0u,
          value.residual_page_count};
}
// A C diagnostic for `final`, the code actually returned across the boundary
// (validation in the C layer may fail before the core fills `value`).
yukiplt_c_operation_diagnostic converted(const OperationDiagnostic& value,
                                         yukiplt_c_error final) noexcept {
  yukiplt_c_operation_diagnostic out{};
  out.struct_size = sizeof(out);
  const Error error = Error::Sys(static_cast<ErrorCode>(final.code), final.sys_errno);
  const bool from_core = value.error.code == error.code && value.error.sys_errno == error.sys_errno;
  out.stage = static_cast<uint32_t>(from_core ? value.stage : OperationStage::kPrepare);
  out.cause =
      static_cast<uint32_t>(from_core ? value.cause : detail::resolve_cause(error, value.cause));
  out.error = final;
  out.raise_errno = from_core ? value.raise_errno : 0;
  out.restore_errno = from_core ? value.restore_errno : 0;
  out.rollback_failed = from_core && value.rollback_failed ? 1u : 0u;
  out.image_invalidated = final.code == YUKIPLT_C_IMAGE_INVALIDATED ? 1u : 0u;
  out.handle_consumed = from_core && value.handle_consumed ? 1u : 0u;
  out.residual_page_count = from_core ? value.residual_page_count : 0;
  return out;
}
ImageView view(const yukiplt_c_image& image) noexcept {
  return {image.load_bias, static_cast<const NativeElfPhdr*>(image.program_headers),
          image.program_header_count, image.instance_token};
}
Result<NamedImage> named_view(const yukiplt_c_named_image& source) {
  if (source.struct_size != sizeof(yukiplt_c_named_image) || source.image.instance_token != 0)
    return Error::Make(ErrorCode::kInvalidArgument);
  if (source.path == nullptr) return Error::Make(ErrorCode::kNullPointer);
  return NamedImage{view(source.image), source.path};
}
ImportSelector selector(const yukiplt_c_selector& source) noexcept {
  return {
      source.symbol,     static_cast<VersionMatch>(source.version_match), source.version,
      source.dependency, static_cast<HiddenMatch>(source.hidden),         source.relocation_index};
}
bool valid_enums(const yukiplt_c_selector& source) noexcept {
  // Check before narrowing to the C++ uint8 enums; 256 must not become kAny.
  return source.version_match <= YUKIPLT_C_VERSION_EXACT && source.hidden <= YUKIPLT_C_HIDDEN;
}
RelocationSource reloc_source(uint32_t index_source) noexcept {
  switch (index_source) {
    case YUKIPLT_C_RELOC_SOURCE_RELA:
      return RelocationSource::kRela;
    case YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA:
      return RelocationSource::kAndroidRela;
    case YUKIPLT_C_RELOC_SOURCE_REL:
      return RelocationSource::kRel;
    case YUKIPLT_C_RELOC_SOURCE_ANDROID_REL:
      return RelocationSource::kAndroidRel;
    case YUKIPLT_C_RELOC_SOURCE_JMPREL:
    default:
      return RelocationSource::kJmprel;
  }
}
// Translate a C function selector, validating the struct_size tag, the index
// source enum and the base selector enums before narrowing.  The source/type
// masks are validated by the C++ core (non-zero, in range).
bool function_selector(const yukiplt_c_function_selector& source,
                       FunctionImportSelector& out) noexcept {
  if (source.struct_size != sizeof(yukiplt_c_function_selector)) return false;
  if (source.index_source > YUKIPLT_C_RELOC_SOURCE_ANDROID_REL) return false;
  if (!valid_enums(source.base)) return false;
  out.base = selector(source.base);
  out.sources = source.sources;
  out.types = source.types;
  out.index_source = reloc_source(source.index_source);
  return true;
}
void copy_reports(yukiplt_c_slot_report* out, const SlotReport* reports, size_t count) noexcept {
  if (!out) return;
  for (size_t i = 0; i < count; ++i)
    out[i] = {reports[i].address, reports[i].value_before, reports[i].value_observed,
              static_cast<uint32_t>(reports[i].status)};
}
uint64_t token(const HookHandle& handle) noexcept {
  return handle.valid() ? detail_api::id(handle) + 1 : 0;
}
struct BorrowedHandle {
  HookHandle value;
  explicit BorrowedHandle(uint64_t token) : value(detail_api::make(token - 1)) {}
  ~BorrowedHandle() { detail_api::set(value, detail_api::invalid()); }
};
template <typename Function>
yukiplt_c_error guarded(Function function, OperationDiagnostic* diagnostic = nullptr) noexcept {
  if (Error e = detail::process_entry()) {
    if (diagnostic) diagnostic->cause = ErrorCause::kForkProtocol;
    return converted(e);
  }
  try {
    return converted(function());
  } catch (...) {
    return converted(detail::current_exception_error());
  }
}
}  // namespace

extern "C" {
const char* yukiplt_c_error_name(uint32_t code) noexcept {
  return error_name(static_cast<ErrorCode>(code));
}
yukiplt_c_error yukiplt_c_query_recovery(const yukiplt_c_handle* handle,
                                         yukiplt_c_recovery_diagnostic* diagnostic,
                                         yukiplt_c_residual_page* pages, size_t capacity) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!handle || !diagnostic || (!pages && capacity != 0))
      return Error::Make(ErrorCode::kNullPointer);
    if (handle->token == 0) return Error::Make(ErrorCode::kInvalidArgument);
    BorrowedHandle borrowed(handle->token);
    RecoveryDiagnostic value;
    ResidualPage buffer[kLimit];
    Error result = query_recovery(borrowed.value, &value, buffer, std::min(capacity, kLimit));
    if (result) return result;
    *diagnostic = converted(value);
    for (size_t i = 0; i < std::min({capacity, value.residual_page_count, kLimit}); ++i)
      pages[i] = {buffer[i].address, buffer[i].original_protection, buffer[i].raised_protection,
                  buffer[i].sys_errno};
    return Error::Ok();
  });
}
uint32_t yukiplt_c_abi_version(void) noexcept { return YUKIPLT_C_ABI_VERSION; }
yukiplt_c_error yukiplt_c_image_from_soname(const char* name, yukiplt_c_image* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!out) return Error::Make(ErrorCode::kNullPointer);
    auto result = image_from_soname(name);
    if (!result) return result.error();
    const auto& image = result.value();
    *out = {image.load_bias, image.phdr_table, image.phdr_count, image.instance_token};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_hook(const yukiplt_c_image* image, const yukiplt_c_selector* target,
                               void* replacement, void** original, yukiplt_c_handle* handle,
                               yukiplt_c_slot_report* report) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!target) return converted(Error::Make(ErrorCode::kNullPointer));
  const yukiplt_c_request request{*target, replacement, original};
  return yukiplt_c_hook_batch(image, &request, 1, handle, report);
}
yukiplt_c_error yukiplt_c_hook_batch(const yukiplt_c_image* image,
                                     const yukiplt_c_request* requests, size_t count,
                                     yukiplt_c_handle* handle,
                                     yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    SelectedHookRequest converted_requests[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      if (!valid_enums(requests[i].target)) return Error::Make(ErrorCode::kInvalidArgument);
      converted_requests[i] = {selector(requests[i].target), requests[i].replacement,
                               requests[i].out_original};
    }
    auto result = hook_imports(view(*image), converted_requests, count, reports);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
    copy_reports(output, reports, count);
    return result.error;
  });
}
yukiplt_c_error yukiplt_c_unhook(yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
                                 size_t capacity) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!handle) return Error::Make(ErrorCode::kNullPointer);
    if (!handle->token) return Error::Make(ErrorCode::kInvalidArgument);
    BorrowedHandle borrowed(handle->token);
    SlotReport reports[kLimit];
    size_t count = 0;
    // Count-only query does not touch GOT or allocate transaction pages.
    Error queried = query(borrowed.value, nullptr, 0, &count);
    if (queried) return queried;
    const size_t n = std::min({count, capacity, kLimit});
    Error result = unhook(borrowed.value, reports, n);
    handle->token = token(borrowed.value);
    copy_reports(output, reports, n);
    return result;
  });
}
yukiplt_c_error yukiplt_c_abandon(yukiplt_c_handle* handle) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!handle) return Error::Make(ErrorCode::kNullPointer);
    if (!handle->token) return Error::Make(ErrorCode::kInvalidArgument);
    BorrowedHandle borrowed(handle->token);
    Error result = abandon(borrowed.value);
    handle->token = token(borrowed.value);
    return result;
  });
}
yukiplt_c_error yukiplt_c_query(const yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
                                size_t capacity, size_t* total) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!handle) return Error::Make(ErrorCode::kNullPointer);
    if (!handle->token) return Error::Make(ErrorCode::kInvalidArgument);
    BorrowedHandle borrowed(handle->token);
    SlotReport reports[kLimit];
    size_t count = 0;
    const size_t cap = std::min(capacity, kLimit);
    Error result = query(borrowed.value, output ? reports : nullptr, cap, &count);
    if (result) return result;
    if (total) *total = count;
    copy_reports(output, reports, std::min(cap, count));
    return result;
  });
}
yukiplt_c_error yukiplt_c_forget_image(const yukiplt_c_image* image) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!image) return Error::Make(ErrorCode::kNullPointer);
    return forget_image(view(*image));
  });
}
yukiplt_c_error yukiplt_c_trim_idle_storage(void) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (Error e = detail::process_entry()) return converted(e);
  static_assert(YUKIPLT_C_FEATURE_IDLE_STORAGE == 16);
  try {
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return converted(detail::Registry::instance().trim_idle_storage());
  } catch (...) {
    return converted(detail::current_exception_error());
  }
}

int yukiplt_c_has_feature(uint32_t feature_id) noexcept {
  return feature_id == YUKIPLT_C_FEATURE_CACHED_MAPS ||
         feature_id == YUKIPLT_C_FEATURE_CONTROLLED_FORK ||
         feature_id == YUKIPLT_C_FEATURE_MT_FORK ||
         feature_id == YUKIPLT_C_FEATURE_FUNCTION_IMPORTS ||
         feature_id == YUKIPLT_C_FEATURE_RECOVERY_DIAGNOSTICS ||
         feature_id == YUKIPLT_C_FEATURE_ERROR_NAMES ||
         feature_id == YUKIPLT_C_FEATURE_EXPECTED_CURRENT ||
         feature_id == YUKIPLT_C_FEATURE_ADDRESS_DISCOVERY ||
         feature_id == YUKIPLT_C_FEATURE_ERROR_CAUSES ||
         feature_id == YUKIPLT_C_FEATURE_ORIGINAL_LOAD ||
         feature_id == YUKIPLT_C_FEATURE_NAMED_IMAGE ||
         feature_id == YUKIPLT_C_FEATURE_OWNED_IMAGES ||
         feature_id == YUKIPLT_C_FEATURE_IMPORT_SNAPSHOTS ||
         feature_id == YUKIPLT_C_FEATURE_PLANS || feature_id == YUKIPLT_C_FEATURE_SESSIONS ||
         feature_id == YUKIPLT_C_FEATURE_RULES || feature_id == YUKIPLT_C_FEATURE_CANDIDATES ||
         feature_id == YUKIPLT_C_FEATURE_IDLE_STORAGE ||
         (feature_id == YUKIPLT_C_FEATURE_REL_IMPORTS && !kNativeUsesRela);
}
yukiplt_c_error yukiplt_c_hook_function(const yukiplt_c_image* image,
                                        const yukiplt_c_function_selector* target,
                                        void* replacement, void** original,
                                        yukiplt_c_handle* handle,
                                        yukiplt_c_slot_report* report) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!target) return converted(Error::Make(ErrorCode::kNullPointer));
  const yukiplt_c_function_request request{*target, replacement, original};
  return yukiplt_c_hook_functions(image, &request, 1, handle, report);
}
yukiplt_c_error yukiplt_c_hook_functions(const yukiplt_c_image* image,
                                         const yukiplt_c_function_request* requests, size_t count,
                                         yukiplt_c_handle* handle,
                                         yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    FunctionHookRequest converted_requests[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      FunctionImportSelector sel;
      if (!function_selector(requests[i].target, sel))
        return Error::Make(ErrorCode::kInvalidArgument);
      converted_requests[i] = {sel, requests[i].replacement, requests[i].out_original};
    }
    auto result = hook_function_imports(view(*image), converted_requests, count, reports);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
    copy_reports(output, reports, count);
    return result.error;
  });
}
yukiplt_c_error yukiplt_c_hook_functions_ex(const yukiplt_c_image* image,
                                            const yukiplt_c_function_request_ex* requests,
                                            size_t count, yukiplt_c_handle* handle,
                                            yukiplt_c_slot_report* output,
                                            yukiplt_c_recovery_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  RecoveryDiagnostic operation;
  operation.stage = OperationStage::kPrepare;
  const auto error = guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    FunctionHookRequestEx translated[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
          requests[i].require_current > 1 ||
          !function_selector(requests[i].base.target, translated[i].target))
        return Error::Make(ErrorCode::kInvalidArgument);
      translated[i].replacement = requests[i].base.replacement;
      translated[i].out_original = requests[i].base.out_original;
      translated[i].require_current = requests[i].require_current != 0;
      translated[i].expected_current = requests[i].expected_current;
    }
    auto result = hook_function_imports_ex(view(*image), translated, count, reports, &operation);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());
    copy_reports(output, reports, count);
    return result.error;
  });
  operation.error = Error::Sys(static_cast<ErrorCode>(error.code), error.sys_errno);
  if (diagnostic) *diagnostic = converted(operation);
  return error;
}

yukiplt_c_error yukiplt_c_hook_functions_diagnosed(
    const yukiplt_c_image* image, const yukiplt_c_function_request_ex* requests, size_t count,
    yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
    yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        FunctionHookRequestEx translated[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
              requests[i].require_current > 1 ||
              !function_selector(requests[i].base.target, translated[i].target))
            return Error::Make(ErrorCode::kInvalidArgument);
          translated[i].replacement = requests[i].base.replacement;
          translated[i].out_original = requests[i].base.out_original;
          translated[i].require_current = requests[i].require_current != 0;
          translated[i].expected_current = requests[i].expected_current;
        }
        auto result =
            hook_function_imports_diagnosed(view(*image), translated, count, reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_unhook_diagnosed(yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
                                           size_t capacity,
                                           yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        if (!handle) return Error::Make(ErrorCode::kNullPointer);
        if (!handle->token) {
          operation.cause = ErrorCause::kHandleInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        BorrowedHandle borrowed(handle->token);
        SlotReport reports[kLimit];
        size_t count = 0;
        // Count-only query does not touch GOT or allocate transaction pages.
        Error queried = query(borrowed.value, nullptr, 0, &count);
        if (queried) {
          if (queried.code == ErrorCode::kInvalidArgument)
            operation.cause = ErrorCause::kHandleInvalid;
          return queried;
        }
        const size_t n = std::min({count, capacity, kLimit});
        Error result = unhook_diagnosed(borrowed.value, reports, n, &operation);
        handle->token = token(borrowed.value);
        copy_reports(output, reports, n);
        return result;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
const char* yukiplt_c_error_cause_name(uint32_t cause) noexcept {
  return error_cause_name(static_cast<ErrorCause>(cause));
}
yukiplt_c_error yukiplt_c_image_containing(const void* address, yukiplt_c_image* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!output) return Error::Make(ErrorCode::kNullPointer);
    auto found = find_image_containing(address);
    if (!found) return found.error();
    const auto& image = found.value().image;
    *output = {image.load_bias, image.phdr_table, image.phdr_count, image.instance_token};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_hook_named(const yukiplt_c_named_image* image,
                                     const yukiplt_c_selector* target, void* replacement,
                                     void** original, yukiplt_c_handle* handle,
                                     yukiplt_c_slot_report* report) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!target) return converted(Error::Make(ErrorCode::kNullPointer));
  const yukiplt_c_request request{*target, replacement, original};
  return yukiplt_c_hook_batch_named(image, &request, 1, handle, report);
}

yukiplt_c_error yukiplt_c_hook_batch_named(const yukiplt_c_named_image* image,
                                           const yukiplt_c_request* requests, size_t count,
                                           yukiplt_c_handle* handle,
                                           yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    auto named = named_view(*image);
    if (!named) return named.error();
    SelectedHookRequest converted_requests[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      if (!valid_enums(requests[i].target)) return Error::Make(ErrorCode::kInvalidArgument);
      converted_requests[i] = {selector(requests[i].target), requests[i].replacement,
                               requests[i].out_original};
    }
    auto result = hook_imports_named(named.value(), converted_requests, count, reports);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
    copy_reports(output, reports, count);
    return result.error;
  });
}

yukiplt_c_error yukiplt_c_hook_function_named(const yukiplt_c_named_image* image,
                                              const yukiplt_c_function_selector* target,
                                              void* replacement, void** original,
                                              yukiplt_c_handle* handle,
                                              yukiplt_c_slot_report* report) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!target) return converted(Error::Make(ErrorCode::kNullPointer));
  const yukiplt_c_function_request request{*target, replacement, original};
  return yukiplt_c_hook_functions_named(image, &request, 1, handle, report);
}

yukiplt_c_error yukiplt_c_hook_functions_named(const yukiplt_c_named_image* image,
                                               const yukiplt_c_function_request* requests,
                                               size_t count, yukiplt_c_handle* handle,
                                               yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    auto named = named_view(*image);
    if (!named) return named.error();
    FunctionHookRequest converted_requests[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      FunctionImportSelector sel;
      if (!function_selector(requests[i].target, sel))
        return Error::Make(ErrorCode::kInvalidArgument);
      converted_requests[i] = {sel, requests[i].replacement, requests[i].out_original};
    }
    auto result = hook_function_imports_named(named.value(), converted_requests, count, reports);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
    copy_reports(output, reports, count);
    return result.error;
  });
}

yukiplt_c_error yukiplt_c_hook_functions_ex_named(
    const yukiplt_c_named_image* image, const yukiplt_c_function_request_ex* requests, size_t count,
    yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
    yukiplt_c_recovery_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  RecoveryDiagnostic operation;
  operation.stage = OperationStage::kPrepare;
  const auto error = guarded([&] {
    if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
    if (handle->token != 0 || count == 0 || count > kLimit)
      return Error::Make(ErrorCode::kInvalidArgument);
    auto named = named_view(*image);
    if (!named) return named.error();
    FunctionHookRequestEx translated[kLimit];
    SlotReport reports[kLimit];
    for (size_t i = 0; i < count; ++i) {
      if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
          requests[i].require_current > 1 ||
          !function_selector(requests[i].base.target, translated[i].target))
        return Error::Make(ErrorCode::kInvalidArgument);
      translated[i].replacement = requests[i].base.replacement;
      translated[i].out_original = requests[i].base.out_original;
      translated[i].require_current = requests[i].require_current != 0;
      translated[i].expected_current = requests[i].expected_current;
    }
    auto result =
        hook_function_imports_ex_named(named.value(), translated, count, reports, &operation);
    handle->token = token(result.handle);
    detail_api::set(result.handle, detail_api::invalid());
    copy_reports(output, reports, count);
    return result.error;
  });
  operation.error = Error::Sys(static_cast<ErrorCode>(error.code), error.sys_errno);
  if (diagnostic) *diagnostic = converted(operation);
  return error;
}

yukiplt_c_error yukiplt_c_hook_functions_diagnosed_named(
    const yukiplt_c_named_image* image, const yukiplt_c_function_request_ex* requests, size_t count,
    yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
    yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        auto named = named_view(*image);
        if (!named) return named.error();
        FunctionHookRequestEx translated[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
              requests[i].require_current > 1 ||
              !function_selector(requests[i].base.target, translated[i].target))
            return Error::Make(ErrorCode::kInvalidArgument);
          translated[i].replacement = requests[i].base.replacement;
          translated[i].out_original = requests[i].base.out_original;
          translated[i].require_current = requests[i].require_current != 0;
          translated[i].expected_current = requests[i].expected_current;
        }
        auto result = hook_function_imports_diagnosed_named(named.value(), translated, count,
                                                            reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
}  // extern C

namespace yukiplt::detail::c_objects {
namespace {
struct Store {
  std::mutex mutex;
  // Node-based storage releases each allocation at erase; an empty libc++
  // tree retains no bucket array across a static host unload.
  std::map<uint64_t, std::shared_ptr<Object>> objects;
  uint64_t next = 1;
  uint64_t salt;
  Store() {
    std::random_device random;
    salt = (uint64_t{random()} << 32) ^ random();
  }
};
Store& store() {
  static NoDestructor<Store> value;
  return value.get();
}
}  // namespace
Result<uint64_t> publish(std::shared_ptr<Object> object) {
  auto& s = store();
  std::lock_guard<std::mutex> lock(s.mutex);
  // IDs never repeat within a copy, including after failed allocation. Refuse
  // exhaustion instead of wrapping into a previously released generation.
  if (s.next == std::numeric_limits<uint64_t>::max()) return Error::Make(ErrorCode::kOutOfBounds);
  uint64_t token = s.next++ ^ s.salt;
  if (token == 0) {
    if (s.next == std::numeric_limits<uint64_t>::max()) return Error::Make(ErrorCode::kOutOfBounds);
    token = s.next++ ^ s.salt;
  }
  s.objects.emplace(token, std::move(object));
  return token;
}
std::shared_ptr<Object> acquire(uint64_t token, Kind kind) {
  if (!token) return {};
  auto& s = store();
  std::lock_guard<std::mutex> lock(s.mutex);
  auto found = s.objects.find(token);
  return found != s.objects.end() && found->second->kind == kind ? found->second : nullptr;
}
Error destroy(uint64_t& token, Kind kind) {
  if (!token) return Error::Ok();
  auto lease = acquire(token, kind);
  if (!lease || !lease->can_destroy()) return invalid();
  std::shared_ptr<Object> retired;
  {
    auto& s = store();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto found = s.objects.find(token);
    if (found == s.objects.end() || found->second->kind != kind) return invalid();
    retired = std::move(found->second);
    s.objects.erase(found);
    token = 0;
  }
  return Error::Ok();
}
}  // namespace yukiplt::detail::c_objects

namespace yukiplt::detail {
Error initialize_c_store() noexcept {
  try {
    (void)c_objects::store();
    return Error::Ok();
  } catch (...) {
    return current_exception_error();
  }
}
}  // namespace yukiplt::detail

extern "C" {
yukiplt_c_error yukiplt_c_hook_batch_cached(const yukiplt_c_maps_cache* cache,
                                            const yukiplt_c_image* image,
                                            const yukiplt_c_request* requests, size_t count,
                                            yukiplt_c_handle* handle,
                                            yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return yukiplt_c_hook_batch_diagnosed_cached(cache, image, requests, count, handle, output,
                                               nullptr);
}
yukiplt_c_error yukiplt_c_hook_batch_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        SelectedHookRequest converted_requests[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (!valid_enums(requests[i].target)) return Error::Make(ErrorCode::kInvalidArgument);
          converted_requests[i] = {selector(requests[i].target), requests[i].replacement,
                                   requests[i].out_original};
        }
        auto result = hook_imports_cached(lease->value, view(*image), converted_requests, count,
                                          reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_hook_batch_named_cached(const yukiplt_c_maps_cache* cache,
                                                  const yukiplt_c_named_image* image,
                                                  const yukiplt_c_request* requests, size_t count,
                                                  yukiplt_c_handle* handle,
                                                  yukiplt_c_slot_report* output) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return yukiplt_c_hook_batch_diagnosed_named_cached(cache, image, requests, count, handle, output,
                                                     nullptr);
}
yukiplt_c_error yukiplt_c_hook_batch_diagnosed_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        auto named = named_view(*image);
        if (!named) return named.error();
        SelectedHookRequest converted_requests[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (!valid_enums(requests[i].target)) return Error::Make(ErrorCode::kInvalidArgument);
          converted_requests[i] = {selector(requests[i].target), requests[i].replacement,
                                   requests[i].out_original};
        }
        auto result = hook_imports_named_cached(lease->value, named.value(), converted_requests,
                                                count, reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_hook_batch_nonnull_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        auto named = named_view(*image);
        if (!named) return named.error();
        HookRequest converted_requests[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          const auto& target = requests[i].target;
          if (target.version_match != YUKIPLT_C_VERSION_ANY || target.version ||
              target.dependency || target.hidden != YUKIPLT_C_HIDDEN_ANY ||
              target.relocation_index != SIZE_MAX)
            return Error::Make(ErrorCode::kInvalidArgument);
          converted_requests[i] = {target.symbol, requests[i].replacement,
                                   requests[i].out_original};
        }
        auto result = hook_batch_nonnull_named_cached(
            lease->value, named.value(), converted_requests, count, reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());  // transfer, do not abandon
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_hook_functions_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_image* image,
    const yukiplt_c_function_request_ex* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        FunctionHookRequestEx translated[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
              requests[i].require_current > 1 ||
              !function_selector(requests[i].base.target, translated[i].target))
            return Error::Make(ErrorCode::kInvalidArgument);
          translated[i].replacement = requests[i].base.replacement;
          translated[i].out_original = requests[i].base.out_original;
          translated[i].require_current = requests[i].require_current != 0;
          translated[i].expected_current = requests[i].expected_current;
        }
        auto result = hook_function_imports_cached(lease->value, view(*image), translated, count,
                                                   reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_hook_functions_diagnosed_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_function_request_ex* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!image || !requests || !handle) return Error::Make(ErrorCode::kNullPointer);
        if (handle->token != 0 || count == 0 || count > kLimit)
          return Error::Make(ErrorCode::kInvalidArgument);
        auto named = named_view(*image);
        if (!named) return named.error();
        FunctionHookRequestEx translated[kLimit];
        SlotReport reports[kLimit];
        for (size_t i = 0; i < count; ++i) {
          if (requests[i].struct_size != sizeof(yukiplt_c_function_request_ex) ||
              requests[i].require_current > 1 ||
              !function_selector(requests[i].base.target, translated[i].target))
            return Error::Make(ErrorCode::kInvalidArgument);
          translated[i].replacement = requests[i].base.replacement;
          translated[i].out_original = requests[i].base.out_original;
          translated[i].require_current = requests[i].require_current != 0;
          translated[i].expected_current = requests[i].expected_current;
        }
        auto result = hook_function_imports_named_cached(lease->value, named.value(), translated,
                                                         count, reports, &operation);
        handle->token = token(result.handle);
        detail_api::set(result.handle, detail_api::invalid());
        copy_reports(output, reports, count);
        return result.error;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_unhook_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
    size_t capacity, yukiplt_c_operation_diagnostic* diagnostic) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};
  }

  if (diagnostic && diagnostic->struct_size != sizeof(yukiplt_c_operation_diagnostic))
    return converted(Error::Make(ErrorCode::kInvalidArgument));
  OperationDiagnostic operation;
  operation.cause = ErrorCause::kInvalidRequest;
  const auto error = guarded(
      [&] {
        auto lease = detail::c_objects::get_cache(cache);
        if (!lease) {
          operation.cause = ErrorCause::kCacheInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        if (!handle) return Error::Make(ErrorCode::kNullPointer);
        if (!handle->token) {
          operation.cause = ErrorCause::kHandleInvalid;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
        BorrowedHandle borrowed(handle->token);
        SlotReport reports[kLimit];
        size_t count = 0;
        // Count-only query does not touch GOT or allocate transaction pages.
        Error queried = query(borrowed.value, nullptr, 0, &count);
        if (queried) {
          if (queried.code == ErrorCode::kInvalidArgument)
            operation.cause = ErrorCause::kHandleInvalid;
          return queried;
        }
        const size_t n = std::min({count, capacity, kLimit});
        Error result = unhook_cached(lease->value, borrowed.value, reports, n, &operation);
        handle->token = token(borrowed.value);
        copy_reports(output, reports, n);
        return result;
      },
      &operation);
  if (diagnostic) *diagnostic = converted(operation, error);
  return error;
}
yukiplt_c_error yukiplt_c_query_cached(const yukiplt_c_maps_cache* cache,
                                       const yukiplt_c_handle* handle,
                                       yukiplt_c_slot_report* output, size_t capacity,
                                       size_t* total) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    auto lease = detail::c_objects::get_cache(cache);
    if (!lease) return Error::Make(ErrorCode::kInvalidArgument);
    if (!handle) return Error::Make(ErrorCode::kNullPointer);
    if (!handle->token) return Error::Make(ErrorCode::kInvalidArgument);
    BorrowedHandle borrowed(handle->token);
    SlotReport reports[kLimit];
    size_t count = 0;
    const size_t cap = std::min(capacity, kLimit);
    Error result =
        query_cached(lease->value, borrowed.value, output ? reports : nullptr, cap, &count);
    if (result) return result;
    if (total) *total = count;
    copy_reports(output, reports, std::min(cap, count));
    return result;
  });
}
yukiplt_c_error yukiplt_c_maps_cache_create(yukiplt_c_maps_cache* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&]() -> Error {
    if (!out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return Error::Make(ErrorCode::kInvalidArgument);
    auto cache = std::make_shared<detail::c_objects::CacheObject>();
    auto id = detail::c_objects::publish(cache);
    if (!id) return id.error();
    out->token = id.value();
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_maps_cache_capture(yukiplt_c_maps_cache* cache) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&]() -> Error {
    auto lease = detail::c_objects::get_cache(cache);
    if (!lease) return Error::Make(ErrorCode::kInvalidArgument);
    return lease->value.capture();
  });
}
yukiplt_c_error yukiplt_c_maps_cache_import_text(yukiplt_c_maps_cache* cache, const char* text,
                                                 size_t length) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&]() -> Error {
    auto lease = detail::c_objects::get_cache(cache);
    if (!lease) return Error::Make(ErrorCode::kInvalidArgument);
    return lease->value.import_text(text, length);
  });
}
yukiplt_c_error yukiplt_c_maps_cache_invalidate(yukiplt_c_maps_cache* cache) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&]() -> Error {
    auto lease = detail::c_objects::get_cache(cache);
    if (!lease) return Error::Make(ErrorCode::kInvalidArgument);
    return lease->value.invalidate();
  });
}
yukiplt_c_error yukiplt_c_maps_cache_destroy(yukiplt_c_maps_cache* cache) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return guarded([&] {
    return cache ? detail::c_objects::destroy(cache->token, detail::c_objects::Kind::kMapsCache)
                 : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_prepare_fork(void) noexcept { return converted(yukiplt::prepare_fork()); }
yukiplt_c_error yukiplt_c_after_fork_parent(void) noexcept {
  return converted(yukiplt::after_fork_parent());
}
yukiplt_c_error yukiplt_c_after_fork_child(void) noexcept {
  return converted(yukiplt::after_fork_child());
}
}
