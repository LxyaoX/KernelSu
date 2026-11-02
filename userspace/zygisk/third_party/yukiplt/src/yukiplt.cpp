// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "yukiplt.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>

#include "common.hpp"
#include "elf.hpp"
#include "imports.hpp"
#include "maps_cache.hpp"
#include "memory.hpp"
#include "registry.hpp"
#include "system_images.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#include <sys/auxv.h>

#include <exception>
#endif
#include "yukiplt.h"

namespace yukiplt {

using detail::CommitMode;
using detail::CommitResult;
using detail::GroupState;
using detail::ImageIdentity;
using detail::InspectedImage;
using detail::PageResidueRecord;
using detail::Registry;
using detail::SlotOp;
using detail::SlotOutcome;
using detail::SlotRecord;
using detail::Txn;

namespace {

constexpr size_t kMaxBatch = 64;  // Upper bound on slots in one transaction.

// A local code address identifies this copy even when statically linked or
// loaded in a separate namespace. Function addresses are not HWASan data tags.
bool is_own_image(const InspectedImage& image) noexcept {
  return image.addr_in_load(reinterpret_cast<uintptr_t>(&is_own_image), 1);
}

SlotStatus to_status(SlotOutcome o) noexcept {
  switch (o) {
    case SlotOutcome::kCommitted:
      return SlotStatus::kInstalled;
    case SlotOutcome::kConflict:
      return SlotStatus::kConflict;
    case SlotOutcome::kRolledBack:
      return SlotStatus::kRolledBack;
    case SlotOutcome::kRollbackFailed:
      return SlotStatus::kRollbackFailed;
    case SlotOutcome::kUntouched:
    default:
      return SlotStatus::kUntouched;
  }
}

ImageIdentity identity_of(const ImageView& v) noexcept {
  ImageIdentity id;
  id.load_bias = v.load_bias;
  id.phdr_addr = reinterpret_cast<uintptr_t>(v.phdr_table);
  id.phdr_count = v.phdr_count;
  id.instance_token = v.instance_token;
  return id;
}

/// Run `fn(live)` on the loaded system image matching `want`, with the dynamic
/// linker's lock held (so the image cannot be unloaded during the call) and the
/// op lock acquired inside it.  If `want_path` is non-null, the live entry's
/// name (dlpi_name, or "" when absent, as find_images() reports it) must also
/// equal it; an entry with the same bias/phdr/phnum but another name is a
/// different image and does not match.
///
/// Lock order is linker -> op, never the reverse: the op lock is only ever
/// taken inside this callback for an image operation, or standalone for a
/// registry-only operation that never calls dl_*.  dl_iterate_phdr is C, so no
/// exception may unwind through it: one thrown by the lock or by `fn` is
/// captured and rethrown after the iteration returns.  Returns true if the
/// image was found (and `fn` ran).
template <typename Fn>
bool run_on_system_image(const ImageIdentity& want, Fn&& fn, const char* want_path = nullptr,
                         detail::CauseContext* cause = nullptr) {
  if (detail::process_entry()) return false;
#if defined(__linux__) || defined(__ANDROID__)
  struct Ctx {
    const ImageIdentity* want;
    const char* want_path;
    Fn* fn;
    bool matched;
    bool path_mismatch;
    std::exception_ptr error;
  } ctx{&want, want_path, &fn, false, false, nullptr};

  detail::iterate_system_phdr(
      [](struct dl_phdr_info* info, size_t /*size*/, void* p) -> int {
        auto* c = static_cast<Ctx*>(p);
        if (static_cast<uintptr_t>(info->dlpi_addr) != c->want->load_bias ||
            reinterpret_cast<uintptr_t>(info->dlpi_phdr) != c->want->phdr_addr ||
            info->dlpi_phnum != c->want->phdr_count) {
          return 0;
        }
        if (c->want_path != nullptr &&
            std::strcmp(info->dlpi_name == nullptr ? "" : info->dlpi_name, c->want_path) != 0) {
          c->path_mismatch = true;
          return 1;  // Same address, different image: the wanted one is gone.
        }
        ImageView live;
        live.load_bias = static_cast<uintptr_t>(info->dlpi_addr);
        live.phdr_table = info->dlpi_phdr;
        live.phdr_count = info->dlpi_phnum;
        live.instance_token = 0;
        c->matched = true;
        try {
          std::lock_guard<std::mutex> lock(detail::op_mutex());  // linker -> op
          if constexpr (std::is_invocable_v<Fn, const ImageView&, const char*>)
            (*c->fn)(live, info->dlpi_name == nullptr ? "" : info->dlpi_name);
          else
            (*c->fn)(live);
        } catch (...) {
          c->error = std::current_exception();
        }
        return 1;
      },
      &ctx);
  if (ctx.error) std::rethrow_exception(ctx.error);
  // Not found under the wanted identity (gone, or another path at the same
  // bias/program headers): the caller reports kImageInvalidated.
  if (!ctx.matched) detail::note_cause(ErrorCause::kIdentityMismatch, cause);
  return ctx.matched;
#else
  (void)want;
  (void)fn;
  (void)want_path;
  (void)cause;
  return false;
#endif
}

/// A resolved request: the slot and the values to install.
struct PlannedSlot {
  uintptr_t address;
  uintptr_t original;
  uintptr_t replacement;
  void** out_original;
  size_t report_index;  ///< Index into the caller's reports array.
  /// A function GLOB_DAT slot must not be installed over an unresolved (zero)
  /// value: that would turn a null weak function pointer non-null.  JUMP_SLOT
  /// and legacy paths leave this false.
  bool reject_zero_original = false;
  bool require_current = false;
  uintptr_t expected_current = 0;
};

/// Every non-null original-pointer output must be pointer-aligned (the release
/// store is an atomic access) and distinct within one transaction, so one
/// target's original can never overwrite another's.  Checked before any lock
/// or write.
template <typename Request>
Error validate_outputs(const Request* reqs, size_t count,
                       detail::CauseContext* cause = nullptr) noexcept {
  for (size_t i = 0; i < count; ++i) {
    void** out = reqs[i].out_original;
    if (out == nullptr) continue;
    if (reinterpret_cast<uintptr_t>(out) % alignof(void*) != 0)
      return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                ErrorCause::kInvalidRequest, cause);
    for (size_t j = 0; j < i; ++j) {
      if (reqs[j].out_original == out)
        return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                  ErrorCause::kOutputAlias, cause);
    }
  }
  return Error::Ok();
}

/// Resolve every request against `img` to a unique slot, enforcing one-slot
/// uniqueness and no duplicate targets within the batch.
struct NameRequests {
  const HookRequest* requests;
  size_t count;
  size_t heads[128]{};
  size_t next[kMaxBatch]{};
  uintptr_t selected[kMaxBatch]{};
  bool found[kMaxBatch]{};
  bool ambiguous[kMaxBatch]{};

  static size_t hash(std::string_view name) noexcept {
    size_t value = 2166136261u;
    for (unsigned char c : name) value = (value ^ c) * 16777619u;
    return value & 127;
  }
  void prepare() noexcept {
    if (count <= 8) return;
    for (size_t i = 0; i < count; ++i) {
      const char* name = requests[i].symbol;
      if (!name || !*name) continue;
      size_t bucket = hash(name);
      while (heads[bucket] && std::strcmp(requests[heads[bucket] - 1].symbol, name) != 0)
        bucket = (bucket + 1) & 127;
      next[i] = heads[bucket];
      heads[bucket] = i + 1;
    }
  }
  void match(size_t i, const detail::ImportView& entry) noexcept {
    if (found[i]) ambiguous[i] = true;
    found[i] = true;
    selected[i] = entry.slot_address;
  }
  void visit(const detail::ImportView& entry) noexcept {
    if (count <= 8) {
      for (size_t i = 0; i < count; ++i)
        if (requests[i].symbol && *requests[i].symbol && entry.symbol == requests[i].symbol)
          match(i, entry);
    } else {
      size_t bucket = hash(entry.symbol);
      while (heads[bucket]) {
        if (entry.symbol == requests[heads[bucket] - 1].symbol) {
          for (size_t link = heads[bucket]; link; link = next[link - 1]) match(link - 1, entry);
          break;
        }
        bucket = (bucket + 1) & 127;
      }
    }
  }
};

Error plan_requests(const InspectedImage& img, const HookRequest* reqs, size_t count,
                    detail::SmallVec<PlannedSlot, 4>& out, detail::CauseContext* cause = nullptr) {
  NameRequests matches{reqs, count};
  matches.prepare();
  size_t skipped = 0;
  if (Error error = detail::walk_imports(
          img,
          [](void* context, const detail::ImportView& entry) {
            static_cast<NameRequests*>(context)->visit(entry);
          },
          &matches, skipped))
    return error;
  // Preserve legacy request-order errors after complete ELF validation.
  for (size_t i = 0; i < count; ++i) {
    if (!reqs[i].symbol || !reqs[i].replacement) return Error::Make(ErrorCode::kNullPointer);
    if (!*reqs[i].symbol || !matches.found[i]) return Error::Make(ErrorCode::kSymbolNotFound);
    if (matches.ambiguous[i]) return Error::Make(ErrorCode::kAmbiguousSymbol);
    for (const PlannedSlot& previous : out)
      if (previous.address == matches.selected[i])
        return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                  ErrorCause::kDuplicateTarget, cause);
    out.push_back({matches.selected[i], 0, reinterpret_cast<uintptr_t>(reqs[i].replacement),
                   reqs[i].out_original, i});
  }
  return Error::Ok();
}

// Fixed request metadata is separate from decoded-record materialization.
struct SelectedRequests {
  const char* names[kMaxBatch]{};
  size_t heads[128]{};
  size_t next[kMaxBatch]{};
  size_t selected[kMaxBatch]{};
  ErrorCode errors[kMaxBatch]{};

  template <typename Name>
  void prepare(size_t count, Name name) noexcept {
    for (size_t i = 0; i < count; ++i) {
      names[i] = name(i);
      selected[i] = ~size_t{0};
      size_t bucket = NameRequests::hash(names[i]);
      while (heads[bucket] && std::strcmp(names[heads[bucket] - 1], names[i]) != 0)
        bucket = (bucket + 1) & 127;
      next[i] = heads[bucket];
      heads[bucket] = i + 1;
    }
  }
  template <typename Match>
  void visit(std::string_view name, Match match) {
    size_t bucket = NameRequests::hash(name);
    while (heads[bucket]) {
      if (name == names[heads[bucket] - 1]) {
        for (size_t link = heads[bucket]; link; link = next[link - 1]) match(link - 1);
        return;
      }
      bucket = (bucket + 1) & 127;
    }
  }
  void choose(size_t request, size_t record, bool acceptable = true) noexcept {
    if (!acceptable || errors[request] == ErrorCode::kUnsupportedRelocation) {
      errors[request] = ErrorCode::kUnsupportedRelocation;
      return;
    }
    if (selected[request] != ~size_t{0}) errors[request] = ErrorCode::kAmbiguousSymbol;
    selected[request] = record;
  }
  Result<size_t> result(size_t request) const {
    if (errors[request] != ErrorCode::kSuccess) return Error::Make(errors[request]);
    if (selected[request] == ~size_t{0}) return Error::Make(ErrorCode::kSymbolNotFound);
    return selected[request];
  }
};

Error plan_selected_requests(const InspectedImage& img, const SelectedHookRequest* reqs,
                             size_t count, detail::SmallVec<PlannedSlot, 4>& out,
                             detail::CauseContext* cause = nullptr) {
  auto snapshot = detail::snapshot_versioned_imports(img, cause);
  if (!snapshot) return snapshot.error();
  std::optional<SelectedRequests> matches;
  if (count > 8) {
    matches.emplace();
    matches->prepare(count, [&](size_t i) { return reqs[i].target.symbol; });
    for (size_t record = 0; record < snapshot.value().imports.size(); ++record) {
      const auto& entry = snapshot.value().imports[record];
      matches->visit(entry.import.symbol, [&](size_t i) {
        if (detail::import_matches(entry, reqs[i].target)) matches->choose(i, record);
      });
    }
  }
  for (size_t i = 0; i < count; ++i) {
    if (reqs[i].replacement == nullptr) return Error::Make(ErrorCode::kNullPointer);
    auto selected =
        matches ? matches->result(i) : detail::select_import(snapshot.value(), reqs[i].target);
    if (!selected) return selected.error();
    const auto address = snapshot.value().imports[selected.value()].import.slot_address;
    for (const auto& previous : out) {
      if (previous.address == address)
        return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                  ErrorCause::kDuplicateTarget, cause);
    }
    out.push_back(
        {address, 0, reinterpret_cast<uintptr_t>(reqs[i].replacement), reqs[i].out_original, i});
  }
  return Error::Ok();
}

/// Publish each planned original before the commit makes a replacement
/// reachable.  Runs from Txn::commit only after every page raise succeeded.
struct Publication {
  const detail::SmallVec<PlannedSlot, 4>* planned;
};

void publish_originals(void* context) noexcept {
  const auto& planned = *static_cast<Publication*>(context)->planned;
  for (const PlannedSlot& slot : planned) {
    if (slot.out_original == nullptr) continue;
    // The release CAS publishes this release store. The replacement's read
    // protocol is a preceding acquire fence followed by an acquire load, so
    // its earlier ordinary PLT/GOT load is ordered before reading the original.
    // Atomic access also prevents a data race with an already-running reader.
    __atomic_store_n(slot.out_original, reinterpret_cast<void*>(slot.original), __ATOMIC_RELEASE);
  }
}

void write_reports(SlotReport* reports, size_t cap, const detail::SmallVec<PlannedSlot, 4>& planned,
                   const SlotOp* ops, size_t count) {
  if (reports == nullptr) return;
  for (size_t i = 0; i < count; ++i) {
    size_t ri = planned[i].report_index;
    if (ri >= cap) continue;
    reports[ri].address = ops[i].slot;
    reports[ri].value_before = planned[i].original;
    reports[ri].value_observed = ops[i].observed;
    reports[ri].status = to_status(ops[i].outcome);
  }
}

/// Core install path for one image, run under the op lock.  `img` must be a
/// freshly inspected, currently-valid image.  `plan_fn` resolves the caller's
/// requests into `planned` (one planner per request shape: legacy name,
/// versioned selector, or function selector).
template <typename PlanFn>
HookResult install_planned(const ImageView& view, SlotReport* reports, size_t report_cap,
                           PlanFn&& plan_fn, RecoveryDiagnostic* diagnostic = nullptr,
                           const char* system_path = nullptr, detail::CauseContext* cause = nullptr,
                           MapsCache* cache = nullptr) {
  detail::SmallVec<PlannedSlot, 4> planned;
  Error plan = plan_fn(planned);
  if (plan) return {plan, detail_api::make(detail_api::invalid())};

  // Publication is an unconditional store to caller storage, never a slot
  // update. Reject aliasing a transaction target or another live owner before
  // claiming slots, touching page permissions, or publishing any original.
  Registry& reg = Registry::instance();
  for (const auto& slot : planned) {
    const uintptr_t output = reinterpret_cast<uintptr_t>(slot.out_original);
    if (output == 0) continue;
    const Error alias = detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                           ErrorCause::kOutputAlias, cause);
    if (reg.owns_slot(output)) return {alias, HookHandle{}};
    for (const auto& target : planned)
      if (output == target.address) return {alias, HookHandle{}};
  }

  if (reg.ids_exhausted()) return {Error::Make(ErrorCode::kIntegerOverflow), HookHandle{}};
  const size_t ps = detail::page_size();
  if (ps == 0)
    return {detail::with_cause(Error::Make(ErrorCode::kUnsupportedFormat), ErrorCause::kPlatform,
                               cause),
            detail_api::make(detail_api::invalid())};
  // Without the per-copy handle salt the Registry issues no ids; report the
  // random source's errno rather than falling back to a predictable salt.
  if (!reg.salt_ready())
    return {detail::with_cause(Error::Sys(ErrorCode::kUnsupportedFormat, reg.salt_errno()),
                               ErrorCause::kRandomSource, cause),
            detail_api::make(detail_api::invalid())};

  // Reserve ownership of every slot before any write (one slot, one owner).
  SlotRecord recs[kMaxBatch];
  uintptr_t slot_addrs[kMaxBatch]{};
  for (size_t i = 0; i < planned.size(); ++i) {
    recs[i].address = planned[i].address;
    recs[i].replacement = planned[i].replacement;
    slot_addrs[i] = planned[i].address;
  }
  uint64_t id = reg.claim(identity_of(view), recs, planned.size(), system_path);
  if (id == Registry::kInvalidId)
    return {Error::Make(ErrorCode::kAlreadyHooked), detail_api::make(detail_api::invalid())};

  struct ClaimGuard {
    Registry& registry;
    uint64_t id;
    bool retained = false;
    ~ClaimGuard() {
      if (!retained) registry.drop(id);
    }
  } claim{reg, id};

  for (size_t i = 0; i < planned.size(); ++i)
    if (reg.page_pending(slot_addrs[i] & ~(uintptr_t(ps) - 1)))
      return {detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                 ErrorCause::kCacheConflict, cause),
              HookHandle{}};
  auto cache_state = cache ? detail::MapsCacheAccess::get(*cache) : nullptr;
  if (cache) {
    if (!cache_state || cache_state->invalid)
      return {detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                 ErrorCause::kCacheInvalid, cause),
              HookHandle{}};
    if (Error e = reg.bind_cache(id, cache_state))
      return {detail::with_cause(e,
                                 e.code == ErrorCode::kOutOfBounds ? ErrorCause::kMapsCoverage
                                                                   : ErrorCause::kCacheConflict,
                                 cause),
              HookHandle{}};
  }
  Txn txn(detail::active_ops(), ps, cache_state.get());
  Error prep = txn.prepare(slot_addrs, planned.size(), nullptr, 0, cause);
  if (prep) {
    return {prep, detail_api::make(detail_api::invalid())};
  }

  // Read current slot values (now confirmed mapped and readable) and fill the
  // ops.  The originals are published from inside the commit, after every page
  // raise succeeded and before the first CAS makes a replacement reachable.
  // out_original is never cleared once set.
  SlotOp ops[kMaxBatch];
  for (size_t i = 0; i < planned.size(); ++i) {
    uintptr_t cur = 0;
    txn.load(planned[i].address, cur);
    planned[i].original = cur;
    if (planned[i].require_current && cur != planned[i].expected_current) {
      if (diagnostic) diagnostic->stage = OperationStage::kExpectedCurrent;
      if (reports) {
        reports[planned[i].report_index].address = planned[i].address;
        reports[planned[i].report_index].value_before = cur;
        reports[planned[i].report_index].value_observed = cur;
        reports[planned[i].report_index].status = SlotStatus::kConflict;
      }
      return {detail::with_cause(Error::Make(ErrorCode::kSlotOwnershipConflict),
                                 ErrorCause::kExpectedCurrent, cause),
              HookHandle{}};
    }
    // Reject unresolved GLOB_DAT before any write. Zero JUMP_SLOT remains valid.
    if (planned[i].reject_zero_original && cur == 0) {
      return {Error::Make(ErrorCode::kUnsupportedRelocation),
              detail_api::make(detail_api::invalid())};
    }
    if (cur == planned[i].replacement) {  // replacement == current value: ambiguous ownership
      return {detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                 ErrorCause::kReplacementIsCurrent, cause),
              detail_api::make(detail_api::invalid())};
    }
    ops[i].slot = planned[i].address;
    ops[i].expected = cur;
    ops[i].desired = planned[i].replacement;
    // Update the registry's recorded original now that it is known.
    recs[i].original = planned[i].original;
  }

  Publication publication{&planned};
  CommitResult r =
      txn.commit(ops, planned.size(), CommitMode::kAllOrNothing, publish_originals, &publication);
  if (diagnostic) {
    diagnostic->raise_errno = r.raise_errno;
    diagnostic->restore_errno = r.restore_errno;
    diagnostic->rollback_failed = r.rollback_failed;
    diagnostic->stage = r.raise_errno != 0 ? OperationStage::kRaise : OperationStage::kCommit;
  }
  write_reports(reports, report_cap, planned, ops, planned.size());

  // Pages this commit raised but could not restore.  While any such page
  // exists, the process has a page that is more permissive than we found it, so
  // we must keep a record of it and hand back a handle to recover it — never
  // "drop and claim nothing was modified".
  PageResidueRecord pr[kMaxBatch];
  detail::PageResidue res[kMaxBatch];
  size_t pn = 0;
  {
    size_t raised = txn.residue(res, kMaxBatch);
    for (size_t i = 0; i < raised && pn < kMaxBatch; ++i) {
      pr[pn].page = res[i].page;
      pr[pn].original_prot = res[i].original_prot;
      pr[pn].sys_errno = res[i].sys_errno;
      ++pn;
    }
  }
  for (size_t i = 0; i < planned.size(); ++i) {
    recs[i].original = planned[i].original;
    // The slot's contents are ours only if the CAS committed (kCommitted) or
    // committed and then failed to roll back (kRollbackFailed).  A conflict or a
    // clean rollback means the slot never held our replacement.
    SlotOutcome o = ops[i].outcome;
    recs[i].ever_written = (o == SlotOutcome::kCommitted || o == SlotOutcome::kRollbackFailed);
  }

  const bool clean_success = r.all_committed && r.pages_restored && pn == 0;
  if (clean_success) {
    if (diagnostic) diagnostic->stage = OperationStage::kComplete;
    reg.finish_restore(id, /*released=*/false, GroupState::kPrepared, recs, planned.size(), nullptr,
                       0);
    reg.mark_installed(id);
    reg.record_diagnostic(id, {OperationStage::kComplete, Error::Ok()});
    claim.retained = true;
    return {Error::Ok(), detail_api::make(id)};
  }

  // Not a clean success.  If a page is stuck or a rollback failed, recovery
  // state must be kept: Indeterminate with a valid handle.
  if (pn > 0 || r.rollback_failed) {
    reg.mark_indeterminate(id, recs, planned.size(), pr, pn);
    claim.retained = true;
    const Error error =
        detail::with_cause(Error::Sys(ErrorCode::kIndeterminate,
                                      r.restore_errno != 0 ? r.restore_errno : r.raise_errno),
                           // A page left raised is a failed protection restore; otherwise the
                           // residue is a rollback CAS that lost to another writer.
                           pn != 0 ? ErrorCause::kProtectFailed : ErrorCause::kCasConflict, cause);
    reg.record_diagnostic(id, {pn != 0 ? OperationStage::kRestorePages : OperationStage::kRollback,
                               error, r.raise_errno, r.restore_errno, r.rollback_failed});
    if (diagnostic) {
      diagnostic->stage = pn != 0 ? OperationStage::kRestorePages : OperationStage::kRollback;
      diagnostic->residual_page_count = pn;
    }
    return {error, detail_api::make(id)};
  }

  // Every raised page was restored and no rollback failed, so nothing is left
  // modified: free the group and report the first cause.
  if (r.raise_errno != 0)
    return {detail::with_cause(Error::Sys(ErrorCode::kPermissionDenied, r.raise_errno),
                               ErrorCause::kProtectFailed, cause),
            detail_api::make(detail_api::invalid())};
  return {detail::with_cause(Error::Make(ErrorCode::kSlotOwnershipConflict),
                             ErrorCause::kCasConflict, cause),
          detail_api::make(detail_api::invalid())};
}

/// Plan a function-import batch: one scan of the image's selected sources shared
/// by all requests, then unique selection per request with duplicate-target and
/// output checks.
template <typename Request>
Error plan_function_requests(const InspectedImage& img, const Request* reqs, size_t count,
                             detail::SmallVec<PlannedSlot, 4>& out,
                             detail::CauseContext* cause = nullptr) {
  uint32_t sources = 0;
  for (size_t i = 0; i < count; ++i) {
    if (reqs[i].replacement == nullptr) return Error::Make(ErrorCode::kNullPointer);
    Error invalid = detail::validate_function_selector(reqs[i].target);
    if (invalid) return invalid;
    sources |= reqs[i].target.sources;
  }
  detail::ScanBudget budget;
  budget.cause = cause;
  auto scan = detail::scan_function_imports(img, sources, &budget);
  if (!scan) return scan.error();
  std::optional<SelectedRequests> matches;
  if (count > 8) {
    matches.emplace();
    matches->prepare(count, [&](size_t i) { return reqs[i].target.base.symbol; });
    for (size_t record = 0; record < scan.value().records.size(); ++record) {
      const auto& entry = scan.value().records[record];
      matches->visit(entry.entry.base.import.symbol, [&](size_t i) {
        if (detail::function_reloc_matches(entry, reqs[i].target))
          matches->choose(i, record, detail::function_reloc_acceptable(entry));
      });
    }
  }
  for (size_t i = 0; i < count; ++i) {
    auto selected =
        matches ? matches->result(i) : detail::select_function_reloc(scan.value(), reqs[i].target);
    if (!selected) return selected.error();
    const auto& record = scan.value().records[selected.value()].entry;
    const uintptr_t address = record.base.import.slot_address;
    for (const auto& previous : out) {
      if (previous.address == address)
        return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                  ErrorCause::kDuplicateTarget, cause);
    }
    PlannedSlot ps;
    ps.address = address;
    ps.original = 0;
    ps.replacement = reinterpret_cast<uintptr_t>(reqs[i].replacement);
    ps.out_original = reqs[i].out_original;
    ps.report_index = i;
    // Only a GLOB_DAT slot must refuse a zero current value.  A JUMP_SLOT is
    // installed whatever it holds, including 0: the caller asserted eager
    // binding, and its original is reported as found (0 included).
    ps.reject_zero_original = record.relocation_type == detail::kRelGlobDat;
    if constexpr (std::is_same_v<Request, FunctionHookRequestEx>) {
      ps.require_current = reqs[i].require_current;
      ps.expected_current = reqs[i].expected_current;
    }
    out.push_back(ps);
  }
  return Error::Ok();
}

/// Legacy name batch.
HookResult install(const ImageView& view, const InspectedImage& img, const HookRequest* reqs,
                   size_t count, SlotReport* reports, size_t report_cap,
                   RecoveryDiagnostic* diagnostic = nullptr, const char* system_path = nullptr,
                   detail::CauseContext* cause = nullptr, bool require_nonnull = false,
                   MapsCache* cache = nullptr) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return install_planned(
      view, reports, report_cap,
      [&](detail::SmallVec<PlannedSlot, 4>& planned) {
        Error error = plan_requests(img, reqs, count, planned, cause);
        if (error.ok() && require_nonnull)
          for (auto& slot : planned) slot.reject_zero_original = true;
        return error;
      },
      diagnostic, system_path, cause, cache);
}

/// Versioned selector batch.
HookResult install(const ImageView& view, const InspectedImage& img,
                   const SelectedHookRequest* selected, size_t count, SlotReport* reports,
                   size_t report_cap, RecoveryDiagnostic* diagnostic = nullptr,
                   const char* system_path = nullptr, detail::CauseContext* cause = nullptr,
                   MapsCache* cache = nullptr) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return install_planned(
      view, reports, report_cap,
      [&](detail::SmallVec<PlannedSlot, 4>& planned) {
        return plan_selected_requests(img, selected, count, planned, cause);
      },
      diagnostic, system_path, cause, cache);
}

/// Function selector batch (JUMP_SLOT + function GLOB_DAT).
HookResult install(const ImageView& view, const InspectedImage& img,
                   const FunctionHookRequest* reqs, size_t count, SlotReport* reports,
                   size_t report_cap, RecoveryDiagnostic* diagnostic = nullptr,
                   const char* system_path = nullptr, detail::CauseContext* cause = nullptr,
                   MapsCache* cache = nullptr) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return install_planned(
      view, reports, report_cap,
      [&](detail::SmallVec<PlannedSlot, 4>& planned) {
        return plan_function_requests(img, reqs, count, planned, cause);
      },
      diagnostic, system_path, cause, cache);
}

}  // namespace
// HookHandle

HookHandle::~HookHandle() { warn_if_installed(); }

HookHandle& HookHandle::operator=(HookHandle&& o) noexcept {
  if (this != &o) {
    warn_if_installed();
    id_ = o.id_;
    o.id_ = kInvalid;
  }
  return *this;
}

// Best-effort diagnostics keep admission through op-lock release and logging.
// try_lock avoids waiting for an operation that already owns the op lock.
void HookHandle::warn_if_installed() const noexcept {
  if (id_ == kInvalid) return;
  detail::OperationGuard admission;
  if (admission.error()) return;
  try {
    std::unique_lock<std::mutex> lock(detail::op_mutex(), std::try_to_lock);
    if (!lock.owns_lock()) {
      std::fprintf(stderr,
                   "[YukiPLT] WARNING: HookHandle dropped while its state could not be "
                   "inspected; it may still own an installed hook\n");
      return;
    }
    auto& reg = Registry::instance();
    // Warn whenever a dropped handle still owns a live replacement — Installed,
    // or Indeterminate with a slot whose contents are ours — not just Installed.
    if (reg.owns_live_replacement(id_)) {
      std::fprintf(stderr,
                   "[YukiPLT] WARNING: HookHandle dropped while hook is still "
                   "installed (slot 0x%zx)\n",
                   static_cast<size_t>(reg.first_slot_of(id_)));
    }
  } catch (...) {
    std::fprintf(stderr, "[YukiPLT] WARNING: HookHandle dropped; state check failed\n");
  }
}
// image_from_soname

Result<ImageView> image_from_soname(const char* soname) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
#if defined(__linux__) || defined(__ANDROID__)
  struct CallbackData {
    const char* target;
    uintptr_t main_phdr;
    ImageView result;
    size_t matches;
  };

  CallbackData data;
  data.target = (soname != nullptr && soname[0] != '\0') ? soname : nullptr;
  data.main_phdr = static_cast<uintptr_t>(getauxval(AT_PHDR));
  data.result = {};
  data.matches = 0;

  if (data.target == nullptr && data.main_phdr == 0) return Error::Make(ErrorCode::kImageNotFound);

  detail::iterate_system_phdr(
      [](struct dl_phdr_info* info, size_t /*size*/, void* ctx) -> int {
        auto* d = static_cast<CallbackData*>(ctx);
        const char* name = info->dlpi_name;

        bool match = false;
        if (d->target == nullptr) {
          match = reinterpret_cast<uintptr_t>(info->dlpi_phdr) == d->main_phdr;
        } else if (name != nullptr) {
          if (std::strchr(d->target, '/') != nullptr) {
            match = std::strcmp(name, d->target) == 0;
          } else {
            size_t name_len = std::strlen(name);
            size_t target_len = std::strlen(d->target);
            match = std::strcmp(name, d->target) == 0 ||
                    (name_len > target_len && name[name_len - target_len - 1] == '/' &&
                     std::strcmp(name + name_len - target_len, d->target) == 0);
          }
        }

        if (match) {
          d->result.load_bias = static_cast<uintptr_t>(info->dlpi_addr);
          d->result.phdr_table = info->dlpi_phdr;
          d->result.phdr_count = info->dlpi_phnum;
          d->result.instance_token = 0;
          ++d->matches;
        }
        return 0;
      },
      &data);

  if (data.matches == 0) return Error::Make(ErrorCode::kImageNotFound);
  if (data.matches > 1) return Error::Make(ErrorCode::kAmbiguousImage);
  return data.result;
#else
  (void)soname;
  return Error::Make(ErrorCode::kUnsupportedFormat);
#endif
}
// hook / hook_batch

HookResult hook(const ImageView& image, const char* symbol_name, void* replacement,
                void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  HookRequest req;
  req.symbol = symbol_name;
  req.replacement = replacement;
  req.out_original = out_original;
  return hook_batch(image, &req, 1, report);
}

namespace {
HookResult hook_batch_impl(const ImageView& image, const HookRequest* reqs, size_t count,
                           SlotReport* reports, RecoveryDiagnostic* diagnostic,
                           const char* expected_path = nullptr,
                           detail::CauseContext* cause = nullptr, bool require_nonnull = false,
                           MapsCache* cache = nullptr) {
  if (Error e = detail::process_entry())
    return {detail::with_cause(e, ErrorCause::kForkProtocol, cause), HookHandle{}};
  auto fail = [](ErrorCode c) {
    return HookResult{Error::Make(c), detail_api::make(detail_api::invalid())};
  };
  if (reqs == nullptr) return fail(ErrorCode::kNullPointer);
  if (count == 0 || count > kMaxBatch) return fail(ErrorCode::kInvalidArgument);
  // Reject unusable outputs before taking any lock or touching the image.
  Error outputs = validate_outputs(reqs, count, cause);
  if (outputs) return {outputs, detail_api::make(detail_api::invalid())};

  auto apply = [&](const ImageView& live, const char* system_path = nullptr) -> HookResult {
    try {
      auto insp = detail::inspect(live.load_bias, live.phdr_table, live.phdr_count);
      if (!insp) return {insp.error(), detail_api::make(detail_api::invalid())};
      if (is_own_image(insp.value())) {
        detail::note_cause(ErrorCause::kOwnImage, cause);
        return fail(ErrorCode::kInvalidArgument);
      }
      return install(live, insp.value(), reqs, count, reports, count, diagnostic, system_path,
                     cause, require_nonnull, cache);
    } catch (...) {
      // Never unwind through the loader's C callback frame.
      return HookResult{detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause),
                        detail_api::make(detail_api::invalid())};
    }
  };
  try {
    if (image.instance_token == 0) {
      // Keep inspection and installation inside the linker callback, taking op
      // after linker so unloading cannot invalidate the image during access.
      HookResult result = fail(ErrorCode::kImageInvalidated);
      run_on_system_image(
          identity_of(image),
          [&](const ImageView& live, const char* path) { result = apply(live, path); },
          expected_path, cause);
      return result;
    }

    // Custom image: the caller guarantees it stays mapped (contract).
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return apply(image);
  } catch (...) {
    return HookResult{detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause),
                      detail_api::make(detail_api::invalid())};
  }
}
}  // namespace

HookResult hook_batch(const ImageView& image, const HookRequest* reqs, size_t count,
                      SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  return hook_batch_impl(image, reqs, count, reports, nullptr);
}
// Precisely selected imports

HookResult hook_import(const ImageView& image, const ImportSelector& selector, void* replacement,
                       void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  SelectedHookRequest request{selector, replacement, out_original};
  return hook_imports(image, &request, 1, report);
}

namespace {
HookResult hook_imports_impl(const ImageView& image, const char* expected_path,
                             const SelectedHookRequest* reqs, size_t count, SlotReport* reports,
                             RecoveryDiagnostic* diagnostic, detail::CauseContext* cause = nullptr,
                             MapsCache* cache = nullptr) {
  if (Error e = detail::process_entry())
    return {detail::with_cause(e, ErrorCause::kForkProtocol, cause), HookHandle{}};
  auto fail = [](Error error) { return HookResult{error, HookHandle{}}; };
  if (reqs == nullptr) return fail(Error::Make(ErrorCode::kNullPointer));
  if (count == 0 || count > kMaxBatch) return fail(Error::Make(ErrorCode::kInvalidArgument));
  for (size_t i = 0; i < count; ++i) {
    auto error = detail::validate_selector(reqs[i].target);
    if (error) return fail(error);
    if (reqs[i].replacement == nullptr) return fail(Error::Make(ErrorCode::kNullPointer));
  }
  // Reject unusable outputs before taking any lock or touching the image.
  Error outputs = validate_outputs(reqs, count, cause);
  if (outputs) return fail(outputs);
  auto apply = [&](const ImageView& live, const char* system_path = nullptr) -> HookResult {
    try {
      auto inspected = detail::inspect(live.load_bias, live.phdr_table, live.phdr_count);
      if (!inspected) return fail(inspected.error());
      if (is_own_image(inspected.value()))
        return fail(detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                       ErrorCause::kOwnImage, cause));
      return install(live, inspected.value(), reqs, count, reports, count, diagnostic, system_path,
                     cause, cache);
    } catch (...) {
      // Never unwind through the loader's C callback frame.
      return fail(detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause));
    }
  };
  try {
    if (image.instance_token == 0) {
      auto result = fail(Error::Make(ErrorCode::kImageInvalidated));
      run_on_system_image(
          identity_of(image),
          [&](const ImageView& live, const char* path) { result = apply(live, path); },
          expected_path, cause);
      return result;
    }
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return apply(image);
  } catch (...) {
    return fail(detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause));
  }
}
}  // namespace

HookResult hook_imports(const ImageView& image, const SelectedHookRequest* reqs, size_t count,
                        SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  return hook_imports_impl(image, nullptr, reqs, count, reports, nullptr);
}
// Function GLOB_DAT / APS2 imports (opt-in)
//
// inspect_function_imports() and find_function_import() are read-only and use
// the inspect_metadata() helper, so they live further down with the other
// snapshot entry points.  The installing entry points are here, next to
// hook_imports(), since they share its install() / run_on_system_image() path.

HookResult hook_function_import(const ImageView& image, const FunctionImportSelector& selector,
                                void* replacement, void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  FunctionHookRequest request{selector, replacement, out_original};
  return hook_function_imports(image, &request, 1, report);
}

HookResult hook_function_imports(const ImageView& image, const FunctionHookRequest* reqs,
                                 size_t count, SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  return detail::hook_function_imports_at(image, nullptr, reqs, count, reports);
}

namespace {
template <typename Request>
HookResult hook_functions_impl(const ImageView& image, const char* expected_path,
                               const Request* reqs, size_t count, SlotReport* reports,
                               RecoveryDiagnostic* diagnostic = nullptr,
                               detail::CauseContext* cause = nullptr, MapsCache* cache = nullptr) {
  if (Error e = detail::process_entry())
    return {detail::with_cause(e, ErrorCause::kForkProtocol, cause), HookHandle{}};
  auto fail = [](Error error) { return HookResult{error, HookHandle{}}; };
  if (reqs == nullptr) return fail(Error::Make(ErrorCode::kNullPointer));
  if (count == 0 || count > kMaxBatch) return fail(Error::Make(ErrorCode::kInvalidArgument));
  for (size_t i = 0; i < count; ++i) {
    auto error = detail::validate_function_selector(reqs[i].target);
    if (error) return fail(error);
    if (reqs[i].replacement == nullptr) return fail(Error::Make(ErrorCode::kNullPointer));
  }
  // Reject unusable outputs before taking any lock or touching the image.
  Error outputs = validate_outputs(reqs, count, cause);
  if (outputs) return fail(outputs);
  auto apply = [&](const ImageView& live, const char* system_path = nullptr) -> HookResult {
    try {
      auto inspected = detail::inspect(live.load_bias, live.phdr_table, live.phdr_count);
      if (!inspected) return fail(inspected.error());
      if (is_own_image(inspected.value()))
        return fail(detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                       ErrorCause::kOwnImage, cause));
      if constexpr (std::is_same_v<Request, FunctionHookRequestEx>) {
        return install_planned(
            live, reports, count,
            [&](detail::SmallVec<PlannedSlot, 4>& planned) {
              return plan_function_requests(inspected.value(), reqs, count, planned, cause);
            },
            diagnostic, system_path, cause, cache);
      } else {
        return install(live, inspected.value(), reqs, count, reports, count, diagnostic,
                       system_path, cause, cache);
      }
    } catch (...) {
      // Never unwind through the loader's C callback frame.
      return fail(detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause));
    }
  };
  try {
    if (image.instance_token == 0) {
      // Not found, or found at the same address under another name: the image
      // the caller enumerated is no longer the one loaded there.
      auto result = fail(Error::Make(ErrorCode::kImageInvalidated));
      run_on_system_image(
          identity_of(image),
          [&](const ImageView& live, const char* path) { result = apply(live, path); },
          expected_path, cause);
      return result;
    }
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return apply(image);
  } catch (...) {
    return fail(detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause));
  }
}

}  // namespace

}  // namespace yukiplt

namespace yukiplt::detail {
HookResult hook_function_imports_at(const ImageView& image, const char* expected_path,
                                    const FunctionHookRequest* reqs, size_t count,
                                    SlotReport* reports, MapsCache* cache) {
  return hook_functions_impl(image, expected_path, reqs, count, reports, nullptr, nullptr, cache);
}
}  // namespace yukiplt::detail

namespace yukiplt {

HookResult hook_function_imports_ex(const ImageView& image, const FunctionHookRequestEx* reqs,
                                    size_t count, SlotReport* reports,
                                    RecoveryDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return detail::hook_function_imports_ex_at(image, nullptr, reqs, count, reports, diagnostic);
}

HookResult detail::hook_imports_at(const ImageView& image, const char* expected_path,
                                   const SelectedHookRequest* reqs, size_t count,
                                   SlotReport* reports, MapsCache* cache) {
  return hook_imports_impl(image, expected_path, reqs, count, reports, nullptr, nullptr, cache);
}

HookResult detail::hook_function_imports_ex_at(const ImageView& image, const char* expected_path,
                                               const FunctionHookRequestEx* reqs, size_t count,
                                               SlotReport* reports, RecoveryDiagnostic* diagnostic,
                                               MapsCache* cache) {
  RecoveryDiagnostic value;
  value.stage = OperationStage::kPrepare;
  auto result =
      hook_functions_impl(image, expected_path, reqs, count, reports, &value, nullptr, cache);
  value.error = result.error;
  value.image_invalidated = result.error.code == ErrorCode::kImageInvalidated;
  if (diagnostic) *diagnostic = value;
  return result;
}
// unhook

namespace {

/// The restore transaction, run with the op lock held and (for a system image)
/// inside the linker callback.  `image_live` is true when the image is known to
/// be mapped right now: for a system image, because we are inside
/// dl_iterate_phdr; for a custom image, by the caller's contract.
Error do_restore(uint64_t id, HookHandle& handle, bool image_live, SlotReport* reports, size_t cap,
                 RecoveryDiagnostic* diagnostic = nullptr, detail::CauseContext* cause = nullptr,
                 MapsCache* cache = nullptr) {
  Registry& reg = Registry::instance();
  // Mirror each recorded stage into the caller's immediate diagnostic too.
  auto record = [&](const RecoveryDiagnostic& value) {
    reg.record_diagnostic(id, value);
    if (diagnostic) {
      *diagnostic = value;
      // Preflight failures can retain residue from an earlier attempt too.
      // Use the same allocation-free Registry snapshot as query_recovery().
      RecoveryDiagnostic recorded;
      if (reg.recovery_snapshot(id, recorded, nullptr, 0).ok()) *diagnostic = recorded;
    }
  };
  const auto previous_state =
      reg.is_installed(id) ? GroupState::kInstalled : GroupState::kIndeterminate;

  ImageIdentity image;
  SlotRecord recs[kMaxBatch];
  size_t slot_count = 0;
  PageResidueRecord pages[kMaxBatch];
  size_t page_count = 0;
  auto cache_state = cache ? detail::MapsCacheAccess::get(*cache) : nullptr;
  if (cache && image_live) {
    if (!cache_state || cache_state->invalid)
      return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument), ErrorCause::kCacheInvalid,
                                cause);
    if (Error e = reg.bind_cache(id, cache_state))
      return detail::with_cause(e,
                                e.code == ErrorCode::kOutOfBounds ? ErrorCause::kMapsCoverage
                                                                  : ErrorCause::kCacheConflict,
                                cause);
  }
  Error begin =
      reg.begin_restore(id, image, recs, kMaxBatch, &slot_count, pages, kMaxBatch, &page_count);
  if (begin) {
    if (begin.code == ErrorCode::kImageInvalidated) {
      const Error released = reg.release(id);
      if (released) return released;
      detail_api::set(handle, detail_api::invalid());
      // The old mapping's residue is no longer retained by this consumed
      // handle. Do not expose the preflight history as current recovery work.
      if (diagnostic) *diagnostic = {};
    } else if (begin.code == ErrorCode::kInvalidArgument) {
      detail::note_cause(ErrorCause::kHandleInvalid, cause);
    }
    return begin;
  }
  if (slot_count > kMaxBatch) slot_count = kMaxBatch;
  if (page_count > kMaxBatch) page_count = kMaxBatch;

  // A system image that we could not find under the linker lock is gone.
  if (!image_live) {
    reg.invalidate_cache(id);
    reg.finish_restore(id, true, GroupState::kInstalled, nullptr, 0, nullptr, 0);
    detail_api::set(handle, detail_api::invalid());
    if (diagnostic) *diagnostic = {};
    return Error::Make(ErrorCode::kImageInvalidated);
  }

  const size_t ps = detail::page_size();
  if (ps == 0) {
    reg.finish_restore(id, false, previous_state, recs, slot_count, pages, page_count);
    const Error error = detail::with_cause(Error::Make(ErrorCode::kUnsupportedFormat),
                                           ErrorCause::kPlatform, cause);
    record({OperationStage::kPrepare, error});
    return error;
  }

  // Build the page set: slot pages plus any residual raised pages.
  uintptr_t slot_addrs[kMaxBatch];
  uintptr_t extra_pages[kMaxBatch];
  for (size_t i = 0; i < slot_count; ++i) slot_addrs[i] = recs[i].address;
  for (size_t i = 0; i < page_count; ++i) extra_pages[i] = pages[i].page;

  Txn txn(detail::active_ops(), ps, cache_state.get(), true);
  Error prep;
  try {
    prep = txn.prepare(slot_count > 0 ? slot_addrs : nullptr, slot_count,
                       page_count > 0 ? extra_pages : nullptr, page_count, cause);
  } catch (...) {
    // prepare has not changed any slot or page. Restore the exact prior state
    // and record the same code the public boundary will report.
    reg.finish_restore(id, false, previous_state, nullptr, 0, nullptr, 0);
    record({OperationStage::kPrepare,
            detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause)});
    throw;
  }
  if (prep) {
    // A failed/incomplete maps read does not prove unload. Preserve recovery
    // state; custom-image invalidation requires forget_image().
    reg.finish_restore(id, false, previous_state, recs, slot_count, pages, page_count);
    record({OperationStage::kPrepare, prep});
    return prep;
  }

  if (cache_state) {
    for (size_t i = 0; i < page_count; ++i) {
      auto found = cache_state->pages.find(pages[i].page);
      if (found == cache_state->pages.end() || found->second.invalid || !found->second.unknown)
        continue;
      const int error = txn.repair_page(pages[i].page, pages[i].original_prot);
      if (error) {
        pages[i].sys_errno = error;
        reg.finish_restore(id, false, GroupState::kIndeterminate, recs, slot_count, pages,
                           page_count);
        Error failure = Error::Sys(ErrorCode::kIndeterminate, error);
        record({OperationStage::kRestorePages, failure, 0, error, false, false, page_count});
        return failure;
      }
      found->second.unknown = false;
      found->second.vma.readable = (pages[i].original_prot & detail::kProtRead) != 0;
      found->second.vma.writable = (pages[i].original_prot & detail::kProtWrite) != 0;
    }
  }
  // Never-written slots need no recovery. CAS replacement to original; accept
  // already-original values and preserve conflicting third-party values.
  SlotOp ops[kMaxBatch];
  size_t op_count = 0;
  bool any_foreign = false;
  uintptr_t before_values[kMaxBatch]{};
  for (size_t i = 0; i < slot_count; ++i) {
    txn.load(recs[i].address, before_values[i]);
    if (!recs[i].ever_written) continue;
    const uintptr_t cur = before_values[i];
    if (cur == recs[i].replacement) {
      ops[op_count].slot = recs[i].address;
      ops[op_count].expected = recs[i].replacement;
      ops[op_count].desired = recs[i].original;
      ++op_count;
    } else if (cur != recs[i].original) {
      any_foreign = true;
    }
  }

  CommitResult r{};
  if (op_count > 0) {
    r = txn.commit(ops, op_count, CommitMode::kAllOrNothing);
  }

  // Cached recovery confirms every residual target with mprotect, including
  // when a refreshed snapshot reports the original protection.
  PageResidueRecord leftover[kMaxBatch];
  size_t leftover_count = 0;
  int restore_errno = r.restore_errno;
  for (size_t i = 0; i < page_count; ++i) {
    detail::VmaInfo live{};
    if (!txn.page_vma(pages[i].page, live)) {
      pages[i].sys_errno = 0;
      leftover[leftover_count++] = pages[i];
      continue;
    }
    const int live_prot = detail::prot_of(live);
    if (live_prot == pages[i].original_prot && !cache_state) continue;
    const bool conflict = live_prot != pages[i].original_prot &&
                          live_prot != (pages[i].original_prot | detail::kProtWrite);
    const int error = conflict ? 0 : txn.protect_page(pages[i].page, pages[i].original_prot);
    if (conflict || error != 0) {
      pages[i].sys_errno = error;
      if (restore_errno == 0) restore_errno = error;
      leftover[leftover_count++] = pages[i];
    }
  }

  // Unhook CAS: Committed restores original; RolledBack reinstalls replacement;
  // Conflict/RollbackFailed preserves the third-party value.
  size_t oi = 0;
  bool all_restored = true;
  bool rollback_failed = r.rollback_failed;
  for (size_t i = 0; i < slot_count; ++i) {
    SlotStatus st;
    uintptr_t observed = recs[i].original;
    if (i < cap && reports != nullptr) {
      reports[i].address = recs[i].address;
      reports[i].value_before = before_values[i];
    }
    // Was this slot part of the CAS set?
    bool was_op = oi < op_count && ops[oi].slot == recs[i].address;
    if (!recs[i].ever_written) {
      // We never owned this slot's contents, so there is nothing to restore and
      // it cannot be a conflict that keeps the group alive.
      st = SlotStatus::kUntouched;
      observed = before_values[i];
    } else if (was_op) {
      SlotOutcome o = ops[oi].outcome;
      observed = ops[oi].observed;
      if (o == SlotOutcome::kCommitted) {
        st = SlotStatus::kRestored;
      } else if (o == SlotOutcome::kRolledBack) {
        st = SlotStatus::kRolledBack;  // our replacement is back in the slot
        all_restored = false;
      } else {
        st = to_status(o);
        all_restored = false;
      }
      ++oi;
    } else {
      // Not CAS'd: either already original (restored) or foreign (conflict).
      uintptr_t cur = 0;
      txn.load(recs[i].address, cur);
      if (cur == recs[i].original) {
        st = SlotStatus::kRestored;
      } else {
        st = SlotStatus::kConflict;
        observed = cur;
        all_restored = false;
      }
    }
    if (i < cap && reports != nullptr) {
      reports[i].value_observed = observed;
      reports[i].status = st;
    }
  }

  // Merge any still-unrestored carried-in page with the pages this attempt
  // raised but could not restore, so a page is never dropped from the record.
  PageResidueRecord new_pages[kMaxBatch];
  size_t pn = 0;
  detail::PageResidue res[kMaxBatch];
  size_t raised = txn.residue(res, kMaxBatch);
  for (size_t i = 0; i < raised && pn < kMaxBatch; ++i) {
    new_pages[pn].page = res[i].page;
    new_pages[pn].original_prot = res[i].original_prot;
    new_pages[pn].sys_errno = res[i].sys_errno;
    ++pn;
  }
  for (size_t i = 0; i < leftover_count && pn < kMaxBatch; ++i) {
    bool dup = false;
    for (size_t j = 0; j < pn; ++j) {
      if (new_pages[j].page == leftover[i].page) {
        dup = true;
        break;
      }
    }
    if (!dup) new_pages[pn++] = leftover[i];
  }
  const bool pages_clean = r.pages_restored && pn == 0;

  if (all_restored && pages_clean && !rollback_failed) {
    reg.finish_restore(id, true, GroupState::kInstalled, nullptr, 0, nullptr, 0);
    detail_api::set(handle, detail_api::invalid());
    if (diagnostic) *diagnostic = {OperationStage::kComplete, Error::Ok()};
    return Error::Ok();
  }

  // A page left writable (pn > 0) or a failed rollback is partial recovery:
  // kIndeterminate, consistent with install().  This takes precedence over a
  // raise errno, since a stuck page is the more serious residual state.
  if (rollback_failed || !pages_clean) {
    reg.finish_restore(id, false, GroupState::kIndeterminate, recs, slot_count, new_pages, pn);
    const Error error = detail::with_cause(
        Error::Sys(ErrorCode::kIndeterminate, restore_errno != 0 ? restore_errno : r.raise_errno),
        !pages_clean ? ErrorCause::kProtectFailed : ErrorCause::kCasConflict, cause);
    record({!pages_clean ? OperationStage::kRestorePages : OperationStage::kRollback, error,
            r.raise_errno, restore_errno, rollback_failed, false, pn});
    return error;
  }

  if (r.raise_errno != 0) {
    // A page could not be raised, but every page we did raise was restored, so
    // nothing is left writable.  No slot was written; keep the handle to retry.
    reg.finish_restore(id, false, GroupState::kInstalled, recs, slot_count, new_pages, 0);
    const Error error = detail::with_cause(Error::Sys(ErrorCode::kPermissionDenied, r.raise_errno),
                                           ErrorCause::kProtectFailed, cause);
    record({OperationStage::kRaise, error, r.raise_errno});
    return error;
  }

  // Slots left as a third party's value: keep ownership for a later retry.
  (void)any_foreign;
  reg.finish_restore(id, false, GroupState::kInstalled, recs, slot_count, new_pages, pn);
  record({OperationStage::kCommit, Error::Make(ErrorCode::kSlotModifiedExternally)});
  return Error::Make(ErrorCode::kSlotModifiedExternally);
}

/// Read a group's image token under the op lock, without changing its state.
/// Returns kInvalidArgument for a bad id.  Used to route unhook to the system
/// (linker-lock) or custom path before touching registry state.
Error peek_image(uint64_t id, ImageIdentity& out) {
  ImageIdentity image;
  SlotRecord recs[1];
  size_t n = 0;
  Error e = Registry::instance().snapshot(id, image, recs, 1, &n);
  if (e) return e;
  out = image;
  return Error::Ok();
}

}  // namespace

namespace {
Error unhook_impl(HookHandle& handle, SlotReport* reports, size_t cap,
                  RecoveryDiagnostic* diagnostic, detail::CauseContext* cause = nullptr,
                  MapsCache* cache = nullptr) {
  if (Error e = detail::process_entry()) return e;
  if (!handle.valid())
    return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument), ErrorCause::kHandleInvalid,
                              cause);
  uint64_t id = detail_api::id(handle);
  try {
    // Learn the image token first (brief op-lock) so we can pick the lock order.
    ImageIdentity image;
    std::string system_path;
    {
      std::lock_guard<std::mutex> lock(detail::op_mutex());
      Error peek = peek_image(id, image);
      if (peek)
        return peek.code == ErrorCode::kInvalidArgument
                   ? detail::with_cause(peek, ErrorCause::kHandleInvalid, cause)
                   : peek;
      // Path copying can fail before do_restore() gets a chance to mirror the
      // recovery record. Keep its known residue/errno in this call's diagnostic
      // without changing the Registry's historical failure. finish_diagnostic
      // supplies this attempt's final error (for example OutOfMemory).
      if (diagnostic) {
        Error recorded = Registry::instance().recovery_snapshot(id, *diagnostic, nullptr, 0);
        if (recorded) return recorded;
        diagnostic->stage = OperationStage::kPrepare;
      }
      if (image.instance_token == 0 && !image.invalidated) {
        Error path = Registry::instance().snapshot_path(id, system_path);
        if (path) return path;
      }
    }

    if (image.instance_token == 0 && !image.invalidated) {
      // System image: restore inside the linker callback (linker -> op order).
      Error result = Error::Make(ErrorCode::kImageInvalidated);
      bool matched = run_on_system_image(
          image,
          [&](const ImageView&) {
            result =
                do_restore(id, handle, /*image_live=*/true, reports, cap, diagnostic, cause, cache);
          },
          system_path.c_str(), cause);
      if (!matched) {
        // The image is gone: consume the handle without touching memory.
        std::lock_guard<std::mutex> lock(detail::op_mutex());
        return do_restore(id, handle, /*image_live=*/false, reports, cap, diagnostic, cause, cache);
      }
      return result;
    }

    // Custom image (or already-forgotten): the caller guarantees mapping.
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return do_restore(id, handle, /*image_live=*/true, reports, cap, diagnostic, cause,
                      image.invalidated ? nullptr : cache);
  } catch (...) {
    return detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause);
  }
}

/// Complete an immediate OperationDiagnostic from the operation's recorded
/// stage data, its final error and the cause recorded for this operation.
void finish_diagnostic(OperationDiagnostic* out, const RecoveryDiagnostic& recorded,
                       const Error& error, detail::CauseContext* cause = nullptr) noexcept {
  if (out == nullptr) return;
  OperationDiagnostic value;
  value.stage = recorded.stage;
  value.error = error;
  value.cause = detail::resolve_cause(error, cause == nullptr ? ErrorCause::kNone : cause->value);
  value.raise_errno = recorded.raise_errno;
  value.restore_errno = recorded.restore_errno;
  value.rollback_failed = recorded.rollback_failed;
  value.image_invalidated = error.code == ErrorCode::kImageInvalidated;
  value.residual_page_count = recorded.residual_page_count;
  if (value.stage == OperationStage::kNone && !error.ok()) value.stage = OperationStage::kPrepare;
  if (error.ok()) value.stage = OperationStage::kComplete;
  *out = value;
}

template <typename Call>
HookResult diagnosed_install(OperationDiagnostic* diagnostic, Call&& call) {
  detail::CauseContext context;
  RecoveryDiagnostic recorded;
  HookResult result = call(&recorded, &context);
  finish_diagnostic(diagnostic, recorded, result.error, &context);
  return result;
}
}  // namespace

Error unhook(HookHandle& handle, SlotReport* reports, size_t cap) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  return unhook_impl(handle, reports, cap, nullptr);
}

Error unhook_diagnosed(HookHandle& handle, SlotReport* reports, size_t cap,
                       OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return admission.error();
  }

  detail::CauseContext context;
  const bool held = handle.valid();
  RecoveryDiagnostic recorded;
  const Error error = unhook_impl(handle, reports, cap, &recorded, &context);
  finish_diagnostic(diagnostic, recorded, error, &context);
  if (diagnostic) diagnostic->handle_consumed = held && !handle.valid();
  return error;
}

HookResult hook_batch_diagnosed(const ImageView& image, const HookRequest* reqs, size_t count,
                                SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(
      diagnostic, [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
        return hook_batch_impl(image, reqs, count, reports, recorded, nullptr, cause);
      });
}

HookResult hook_imports_diagnosed(const ImageView& image, const SelectedHookRequest* reqs,
                                  size_t count, SlotReport* reports,
                                  OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(
      diagnostic, [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
        return hook_imports_impl(image, nullptr, reqs, count, reports, recorded, cause);
      });
}

HookResult hook_function_imports_diagnosed(const ImageView& image,
                                           const FunctionHookRequestEx* reqs, size_t count,
                                           SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(
      diagnostic, [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
        return hook_functions_impl(image, nullptr, reqs, count, reports, recorded, cause);
      });
}
// Import snapshots / abandon / query / forget_image

namespace {
template <typename Snapshot, typename Collect>
Result<Snapshot> inspect_metadata(const ImageView& image, Collect collect_metadata,
                                  const char* expected_path = nullptr,
                                  detail::CauseContext* cause = nullptr) {
  if (Error e = detail::process_entry()) return e;
  if (image.phdr_table == nullptr) return Error::Make(ErrorCode::kNullPointer);
  if (image.phdr_count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  // Contain every exception inside the callable as well: it runs inside a C
  // loader callback for system images. Publish only a complete owning result.
  auto collect = [&](const ImageView& live) -> Result<Snapshot> {
    try {
      auto inspected = detail::inspect(live.load_bias, live.phdr_table, live.phdr_count);
      if (!inspected) return inspected.error();
      if constexpr (std::is_invocable_v<Collect, const InspectedImage&, detail::CauseContext*>)
        return collect_metadata(inspected.value(), cause);
      else
        return collect_metadata(inspected.value());
    } catch (...) {
      return detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause);
    }
  };
  try {
    if (image.instance_token == 0) {
      Result<Snapshot> result = Error::Make(ErrorCode::kImageInvalidated);
      run_on_system_image(
          identity_of(image), [&](const ImageView& live) { result = collect(live); }, expected_path,
          cause);
      return result;
    }
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return collect(image);
  } catch (...) {
    return detail::current_exception_error(detail::ExceptionOrigin::kInternal, cause);
  }
}
}  // namespace

Result<ImportSnapshot> inspect_imports(const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  return inspect_metadata<ImportSnapshot>(image, detail::snapshot_imports);
}

Result<VersionedImportSnapshot> inspect_versioned_imports(const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  return inspect_metadata<VersionedImportSnapshot>(image, detail::snapshot_versioned_imports);
}

Result<VersionedImport> find_import(const ImageView& image, const ImportSelector& selector) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  auto invalid = detail::validate_selector(selector);
  if (invalid) return invalid;
  return inspect_metadata<VersionedImport>(
      image, [&](const InspectedImage& inspected) -> Result<VersionedImport> {
        auto snapshot = detail::snapshot_versioned_imports(inspected);
        if (!snapshot) return snapshot.error();
        auto selected = detail::select_import(snapshot.value(), selector);
        if (!selected) return selected.error();
        return std::move(snapshot.value().imports[selected.value()]);
      });
}

Result<FunctionImportSnapshot> inspect_function_imports(const ImageView& image, uint32_t sources) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  // Validate the source mask before any image access: a zero or out-of-range
  // mask is a programming error, not an image problem.
  if (sources == 0 || (sources & ~(kSourceJmprel | kSourceRela | kSourceAndroidRela | kSourceRel |
                                   kSourceAndroidRel)) != 0)
    return Error::Make(ErrorCode::kInvalidArgument);
  if (sources & ~(kSourceJmprel | kSourceNativeDynamic | kSourceNativeAndroidPacked))
    return Error::Make(ErrorCode::kUnsupportedFormat);
  return inspect_metadata<FunctionImportSnapshot>(
      image, [&](const InspectedImage& inspected) -> Result<FunctionImportSnapshot> {
        return detail::snapshot_function_imports(inspected, sources);
      });
}

Result<FunctionImport> find_function_import(const ImageView& image,
                                            const FunctionImportSelector& selector) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  auto invalid = detail::validate_function_selector(selector);
  if (invalid) return invalid;
  return inspect_metadata<FunctionImport>(
      image, [&](const InspectedImage& inspected) -> Result<FunctionImport> {
        auto scan = detail::scan_function_imports(inspected, selector.sources);
        if (!scan) return scan.error();
        auto selected = detail::select_function_reloc(scan.value(), selector);
        if (!selected) return selected.error();
        return std::move(scan.value().records[selected.value()].entry);
      });
}

namespace {
Error validate_named_image(const NamedImage& image,
                           detail::CauseContext* cause = nullptr) noexcept {
  if (image.image.instance_token != 0 || image.path.find('\0') != std::string::npos)
    return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument), ErrorCause::kInvalidRequest,
                              cause);
  if (image.image.phdr_table == nullptr) return Error::Make(ErrorCode::kNullPointer);
  if (image.image.phdr_count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  return Error::Ok();
}
}  // namespace

Result<ImportSnapshot> inspect_imports_named(const NamedImage& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error error = validate_named_image(image)) return error;
  return inspect_metadata<ImportSnapshot>(image.image, detail::snapshot_imports,
                                          image.path.c_str());
}

Result<VersionedImportSnapshot> inspect_versioned_imports_named(const NamedImage& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error error = validate_named_image(image)) return error;
  return inspect_metadata<VersionedImportSnapshot>(image.image, detail::snapshot_versioned_imports,
                                                   image.path.c_str());
}

Result<VersionedImport> find_import_named(const NamedImage& image, const ImportSelector& selector) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error error = validate_named_image(image)) return error;
  if (Error error = detail::validate_selector(selector)) return error;
  return inspect_metadata<VersionedImport>(
      image.image,
      [&](const InspectedImage& inspected) -> Result<VersionedImport> {
        auto snapshot = detail::snapshot_versioned_imports(inspected);
        if (!snapshot) return snapshot.error();
        auto selected = detail::select_import(snapshot.value(), selector);
        if (!selected) return selected.error();
        return std::move(snapshot.value().imports[selected.value()]);
      },
      image.path.c_str());
}

Result<FunctionImportSnapshot> inspect_function_imports_named(const NamedImage& image,
                                                              uint32_t sources) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error error = validate_named_image(image)) return error;
  if (sources == 0 || (sources & ~(kSourceJmprel | kSourceRela | kSourceAndroidRela | kSourceRel |
                                   kSourceAndroidRel)) != 0)
    return Error::Make(ErrorCode::kInvalidArgument);
  if (sources & ~(kSourceJmprel | kSourceNativeDynamic | kSourceNativeAndroidPacked))
    return Error::Make(ErrorCode::kUnsupportedFormat);
  return inspect_metadata<FunctionImportSnapshot>(
      image.image,
      [&](const InspectedImage& inspected) {
        return detail::snapshot_function_imports(inspected, sources);
      },
      image.path.c_str());
}

Result<FunctionImport> find_function_import_named(const NamedImage& image,
                                                  const FunctionImportSelector& selector) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error error = validate_named_image(image)) return error;
  if (Error error = detail::validate_function_selector(selector)) return error;
  return inspect_metadata<FunctionImport>(
      image.image,
      [&](const InspectedImage& inspected) -> Result<FunctionImport> {
        auto scan = detail::scan_function_imports(inspected, selector.sources);
        if (!scan) return scan.error();
        auto selected = detail::select_function_reloc(scan.value(), selector);
        if (!selected) return selected.error();
        return std::move(scan.value().records[selected.value()].entry);
      },
      image.path.c_str());
}

HookResult hook_named(const NamedImage& image, const char* symbol_name, void* replacement,
                      void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  const HookRequest request{symbol_name, replacement, out_original};
  return hook_batch_named(image, &request, 1, report);
}

HookResult hook_batch_named(const NamedImage& image, const HookRequest* reqs, size_t count,
                            SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  if (Error error = validate_named_image(image)) return {error, HookHandle{}};
  return hook_batch_impl(image.image, reqs, count, reports, nullptr, image.path.c_str());
}

HookResult hook_import_named(const NamedImage& image, const ImportSelector& selector,
                             void* replacement, void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  const SelectedHookRequest request{selector, replacement, out_original};
  return hook_imports_named(image, &request, 1, report);
}

HookResult hook_imports_named(const NamedImage& image, const SelectedHookRequest* reqs,
                              size_t count, SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  if (Error error = validate_named_image(image)) return {error, HookHandle{}};
  return hook_imports_impl(image.image, image.path.c_str(), reqs, count, reports, nullptr);
}

HookResult hook_function_import_named(const NamedImage& image,
                                      const FunctionImportSelector& selector, void* replacement,
                                      void** out_original, SlotReport* report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  const FunctionHookRequest request{selector, replacement, out_original};
  return hook_function_imports_named(image, &request, 1, report);
}

HookResult hook_function_imports_named(const NamedImage& image, const FunctionHookRequest* reqs,
                                       size_t count, SlotReport* reports) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {admission.error(), HookHandle{}};

  if (Error error = validate_named_image(image)) return {error, HookHandle{}};
  return hook_functions_impl(image.image, image.path.c_str(), reqs, count, reports);
}

HookResult hook_function_imports_ex_named(const NamedImage& image,
                                          const FunctionHookRequestEx* reqs, size_t count,
                                          SlotReport* reports, RecoveryDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  RecoveryDiagnostic value;
  value.stage = OperationStage::kPrepare;
  const Error invalid = validate_named_image(image);
  auto result =
      invalid ? HookResult{invalid, HookHandle{}}
              : hook_functions_impl(image.image, image.path.c_str(), reqs, count, reports, &value);
  value.error = result.error;
  value.image_invalidated = result.error.code == ErrorCode::kImageInvalidated;
  if (diagnostic) *diagnostic = value;
  return result;
}

HookResult hook_batch_diagnosed_named(const NamedImage& image, const HookRequest* reqs,
                                      size_t count, SlotReport* reports,
                                      OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(diagnostic, [&](RecoveryDiagnostic* recorded,
                                           detail::CauseContext* cause) {
    if (Error error = validate_named_image(image, cause)) return HookResult{error, HookHandle{}};
    return hook_batch_impl(image.image, reqs, count, reports, recorded, image.path.c_str(), cause);
  });
}

HookResult hook_batch_nonnull_diagnosed_named(const NamedImage& image, const HookRequest* reqs,
                                              size_t count, SlotReport* reports,
                                              OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             if (Error error = validate_named_image(image, cause))
                               return HookResult{error, HookHandle{}};
                             return hook_batch_impl(image.image, reqs, count, reports, recorded,
                                                    image.path.c_str(), cause, true);
                           });
}

HookResult hook_imports_diagnosed_named(const NamedImage& image, const SelectedHookRequest* reqs,
                                        size_t count, SlotReport* reports,
                                        OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             if (Error error = validate_named_image(image, cause))
                               return HookResult{error, HookHandle{}};
                             return hook_imports_impl(image.image, image.path.c_str(), reqs, count,
                                                      reports, recorded, cause);
                           });
}

HookResult hook_function_imports_diagnosed_named(const NamedImage& image,
                                                 const FunctionHookRequestEx* reqs, size_t count,
                                                 SlotReport* reports,
                                                 OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             if (Error error = validate_named_image(image, cause))
                               return HookResult{error, HookHandle{}};
                             return hook_functions_impl(image.image, image.path.c_str(), reqs,
                                                        count, reports, recorded, cause);
                           });
}

Result<detail::RawFunctionSnapshot> detail::inspect_function_relocs(const ImageView& image,
                                                                    uint32_t sources,
                                                                    ScanBudget* budget,
                                                                    const char* expected_path) {
  return inspect_metadata<detail::RawFunctionSnapshot>(
      image,
      [&](const InspectedImage& inspected) {
        return detail::scan_function_imports(inspected, sources, budget);
      },
      expected_path, budget == nullptr ? nullptr : budget->cause);
}

Result<std::string> detail::system_image_path(const ImageView& image) {
  if (image.instance_token != 0) return std::string();
  try {
    Result<std::string> path = Error::Make(ErrorCode::kImageInvalidated);
#if defined(__linux__) || defined(__ANDROID__)
    struct Ctx {
      const ImageView* want;
      Result<std::string>* out;
    } ctx{&image, &path};
    detail::iterate_system_phdr(
        [](struct dl_phdr_info* info, size_t, void* p) -> int {
          auto* c = static_cast<Ctx*>(p);
          if (static_cast<uintptr_t>(info->dlpi_addr) != c->want->load_bias ||
              info->dlpi_phdr != c->want->phdr_table || info->dlpi_phnum != c->want->phdr_count)
            return 0;
          try {
            *c->out = std::string(info->dlpi_name == nullptr ? "" : info->dlpi_name);
          } catch (...) {
            *c->out = detail::current_exception_error();
          }
          return 1;
        },
        &ctx);
#endif
    return path;
  } catch (...) {
    return detail::current_exception_error();
  }
}

Error detail::verify_installed_bindings(InstalledBindingVerification* entries, size_t count,
                                        BindingVerificationStatistics* statistics,
                                        MapsCache* cache) {
  if (statistics) *statistics = {};
  if (count == 0) return Error::Ok();
  if (entries == nullptr) return Error::Make(ErrorCode::kNullPointer);
  if (Error e = detail::process_entry()) return e;
  auto cache_state = cache ? detail::MapsCacheAccess::get(*cache) : nullptr;
  if (cache && (!cache_state || cache_state->invalid))
    return Error::Make(ErrorCode::kInvalidArgument);
  try {
    struct Work {
      size_t index;
      SlotRecord slot;
      bool eligible = false;
    };
    struct Group {
      ImageView image;
      size_t begin;
      size_t end;
    };
    const auto less_image = [](const ImageView& a, const ImageView& b) {
      if (a.load_bias != b.load_bias) return a.load_bias < b.load_bias;
      if (a.phdr_table != b.phdr_table)
        return reinterpret_cast<uintptr_t>(a.phdr_table) <
               reinterpret_cast<uintptr_t>(b.phdr_table);
      return a.phdr_count < b.phdr_count;
    };
    std::vector<Work> work;
    std::vector<Group> groups;
    std::vector<uintptr_t> addresses;
    work.reserve(count);
    groups.reserve(count);
    addresses.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      auto& entry = entries[i];
      entry.report = {};
      entry.error = Error::Make(ErrorCode::kImageInvalidated);
      if (entry.handle == nullptr || !entry.handle->valid() || entry.image.instance_token != 0 ||
          entry.bound_path == nullptr || entry.enumerated_path == nullptr) {
        entry.error = Error::Make(ErrorCode::kInvalidArgument);
        continue;
      }
      work.push_back({i, {}});
    }
    std::sort(work.begin(), work.end(), [&](const Work& a, const Work& b) {
      return less_image(entries[a.index].image, entries[b.index].image);
    });
    for (size_t begin = 0; begin < work.size();) {
      const auto& image = entries[work[begin].index].image;
      size_t end = begin + 1;
      while (end < work.size() && !less_image(image, entries[work[end].index].image)) ++end;
      groups.push_back({image, begin, end});
      begin = end;
    }
    if (groups.empty()) return Error::Ok();
#if defined(__linux__) || defined(__ANDROID__)
    auto verify_image = [&](const ImageView& live, const char* path) -> Error {
      auto found = std::lower_bound(groups.begin(), groups.end(), live,
                                    [&](const Group& group, const ImageView& image) {
                                      return less_image(group.image, image);
                                    });
      if (found == groups.end() || less_image(live, found->image)) return Error::Ok();
      // Every Registry read and slot read occurs while this image is held by
      // the linker callback. Never enumerate with op_mutex already held.
      std::lock_guard<std::mutex> lock(detail::op_mutex());
      addresses.clear();
      for (size_t i = found->begin; i < found->end; ++i) {
        auto& item = work[i];
        auto& entry = entries[item.index];
        if (std::strcmp(entry.bound_path, entry.enumerated_path) != 0 ||
            std::strcmp(entry.bound_path, path) != 0)
          continue;
        ImageIdentity recorded;
        size_t slots = 0;
        entry.error = Registry::instance().snapshot(detail_api::id(*entry.handle), recorded,
                                                    &item.slot, 1, &slots);
        if (entry.error) continue;
        if (recorded.invalidated || recorded.instance_token != 0 ||
            recorded.load_bias != live.load_bias ||
            recorded.phdr_addr != reinterpret_cast<uintptr_t>(live.phdr_table) ||
            recorded.phdr_count != live.phdr_count) {
          entry.error = Error::Make(ErrorCode::kImageInvalidated);
          continue;
        }
        if (slots != 1) {
          entry.error = Error::Make(ErrorCode::kInvalidArgument);
          continue;
        }
        if (cache) {
          entry.error =
              Registry::instance().check_cache(detail_api::id(*entry.handle), cache_state.get());
          if (entry.error) continue;
        }
        item.eligible = true;
        addresses.push_back(item.slot.address);  // capacity reserved before enumeration
      }
      if (addresses.empty()) return Error::Ok();
      std::sort(addresses.begin(), addresses.end());
      addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
      const size_t ps = detail::page_size();
      Txn transaction(detail::active_ops(), ps, cache_state.get());
      Error prepared;
      if (ps == 0) {
        prepared = Error::Make(ErrorCode::kUnsupportedFormat);
      } else {
        if (statistics && !cache) ++statistics->maps_reads;
        prepared = transaction.prepare(addresses.data(), addresses.size());
      }
      if (prepared.code == ErrorCode::kOutOfMemory) return prepared;
      for (size_t i = found->begin; i < found->end; ++i) {
        const auto& item = work[i];
        if (!item.eligible) continue;
        auto& entry = entries[item.index];
        entry.error = prepared;
        if (prepared || !item.slot.ever_written) continue;
        uintptr_t value = 0;
        transaction.load(item.slot.address, value);
        if (value != item.slot.replacement) {
          entry.error = Error::Make(ErrorCode::kSlotModifiedExternally);
          entry.report.address = item.slot.address;
          entry.report.value_before = item.slot.replacement;
          entry.report.value_observed = value;
          entry.report.status = SlotStatus::kConflict;
        }
      }
      // The read-only transaction (including its maps) dies here. Installation
      // is a separate operation and must prepare from new maps of its own.
      return Error::Ok();
    };
    struct Context {
      decltype(verify_image)* verify;
      Error error;
    } context{&verify_image, {}};
    if (statistics) ++statistics->enumerations;
    detail::iterate_system_phdr(
        [](dl_phdr_info* info, size_t, void* opaque) -> int {
          auto& ctx = *static_cast<Context*>(opaque);
          try {
            ctx.error = (*ctx.verify)(
                {static_cast<uintptr_t>(info->dlpi_addr), info->dlpi_phdr, info->dlpi_phnum, 0},
                info->dlpi_name == nullptr ? "" : info->dlpi_name);
            if (ctx.error) return 1;
          } catch (...) {
            ctx.error = detail::current_exception_error();
            return 1;
          }
          return 0;
        },
        &context);
    return context.error;
#else
    return Error::Ok();  // No system images can match on this Host platform.
#endif
  } catch (...) {
    return detail::current_exception_error();
  }
}

Error abandon(HookHandle& handle) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (!handle.valid()) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    Error e = Registry::instance().release(detail_api::id(handle));
    if (!e) detail_api::set(handle, detail_api::invalid());
    return e;
  } catch (...) {
    return detail::current_exception_error();
  }
}

namespace {
/// Read live values under the operation lock and, for system images, linker lock.
/// Try one maps read for the group; fall back to individual slots on failure.
Error fill_query(const SlotRecord* recs, size_t n, bool live_image, SlotReport* out, size_t cap,
                 detail::MapsCacheState* cache = nullptr) {
  const size_t ps = detail::page_size();
  const size_t filled = n < cap ? n : cap;
  if (out == nullptr) return Error::Ok();
  for (size_t i = 0; i < filled; ++i) {
    out[i].address = recs[i].address;
    out[i].value_before = recs[i].original;
    out[i].value_observed = 0;
    // kUntouched here means "the live value could not be read"; kInstalled
    // means value_observed holds a genuine live read.  We never invent an
    // observed value from the recorded replacement.
    out[i].status = SlotStatus::kUntouched;
  }
  if (ps == 0 || !live_image || filled == 0) return Error::Ok();

  uintptr_t addrs[kMaxBatch];
  for (size_t i = 0; i < filled; ++i) addrs[i] = recs[i].address;
  Txn all(detail::active_ops(), ps, cache);
  Error prepared = all.prepare(addrs, filled);
  if (!prepared) {
    for (size_t i = 0; i < filled; ++i) {
      uintptr_t cur = 0;
      if (all.load(recs[i].address, cur)) {
        out[i].value_observed = cur;
        out[i].status = SlotStatus::kInstalled;
      }
    }
    return Error::Ok();
  }
  if (cache) return prepared;
  for (size_t i = 0; i < filled; ++i) {
    Txn txn(detail::active_ops(), ps, cache);
    uintptr_t one = recs[i].address;
    if (!txn.prepare(&one, 1)) {
      uintptr_t cur = 0;
      if (txn.load(recs[i].address, cur)) {
        out[i].value_observed = cur;
        out[i].status = SlotStatus::kInstalled;
      }
    }
  }
  return Error::Ok();
}
}  // namespace

static Error query_impl(const HookHandle& handle, SlotReport* out, size_t cap, size_t* count,
                        MapsCache* cache = nullptr) {
  if (Error e = detail::process_entry()) return e;
  auto state = cache ? detail::MapsCacheAccess::get(*cache) : nullptr;
  if (cache && (!state || state->invalid)) return Error::Make(ErrorCode::kInvalidArgument);
  if (!handle.valid()) return Error::Make(ErrorCode::kInvalidArgument);
  const uint64_t id = detail_api::id(handle);
  try {
    // Snapshot the recorded slots, then read live values.  A system image's
    // slots are read only inside the linker callback, so a concurrent dlclose
    // cannot unmap them mid-read; an image no longer loaded reports only the
    // recorded values.
    ImageIdentity image;
    std::string system_path;
    SlotRecord recs[kMaxBatch];
    size_t n = 0;
    {
      std::lock_guard<std::mutex> lock(detail::op_mutex());
      if (cache) {
        if (Error e = Registry::instance().check_cache(id, state.get())) return e;
      }
      Error snap = Registry::instance().snapshot(id, image, recs, kMaxBatch, &n);
      if (snap) return snap;
      // Count-only and recorded recovery queries do not need a path copy or
      // a linker lookup. A live query owns its path across the lock handoff.
      if (out != nullptr && cap != 0 && image.instance_token == 0 && !image.invalidated) {
        Error path = Registry::instance().snapshot_path(id, system_path);
        if (path) return path;
      }
    }
    if (n > kMaxBatch) n = kMaxBatch;

    if (out == nullptr || cap == 0) {
      // Count-only: no live read, so no image access or transaction pages.
    } else if (image.instance_token == 0 && !image.invalidated) {
      Error result = Error::Ok();
      bool matched = run_on_system_image(
          image,
          [&](const ImageView&) {
            // Re-snapshot under the op lock: the group may have changed or been
            // released between the two lock acquisitions.
            if (cache) {
              result = Registry::instance().check_cache(id, state.get());
              if (result) return;
            }
            result = Registry::instance().snapshot(id, image, recs, kMaxBatch, &n);
            if (result) return;
            if (n > kMaxBatch) n = kMaxBatch;
            result = fill_query(recs, n, /*live_image=*/true, out, cap, state.get());
          },
          system_path.c_str());
      if (!matched) (void)fill_query(recs, n, /*live_image=*/false, out, cap, state.get());
      if (result) return result;
    } else {
      std::lock_guard<std::mutex> lock(detail::op_mutex());
      if (cache) {
        if (Error e = Registry::instance().check_cache(id, state.get())) return e;
      }
      Error snap = Registry::instance().snapshot(id, image, recs, kMaxBatch, &n);
      if (snap) return snap;
      if (n > kMaxBatch) n = kMaxBatch;
      if (Error e = fill_query(recs, n, !image.invalidated, out, cap, state.get())) return e;
    }
    if (count != nullptr) *count = n;
    return Error::Ok();
  } catch (...) {
    return detail::current_exception_error();
  }
}

Error query(const HookHandle& h, SlotReport* out, size_t cap, size_t* count) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  return query_impl(h, out, cap, count);
}
Error query_cached(MapsCache& cache, const HookHandle& h, SlotReport* out, size_t cap,
                   size_t* count) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  return query_impl(h, out, cap, count, &cache);
}

Error forget_image(const ImageView& custom_image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (custom_image.instance_token == 0) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    Registry::instance().forget_custom_image(identity_of(custom_image));
    return Error::Ok();
  } catch (...) {
    return detail::current_exception_error();
  }
}

Error query_recovery(const HookHandle& handle, RecoveryDiagnostic* diagnostic, ResidualPage* pages,
                     size_t capacity) {
  detail::OperationGuard admission;
  if (admission.error()) return admission.error();
  return detail::query_recovery_admitted(admission.context(), handle, diagnostic, pages, capacity);
}
namespace detail {
Error query_recovery_admitted(const AdmittedOperation& admission, const HookHandle& handle,
                              RecoveryDiagnostic* diagnostic, ResidualPage* pages,
                              size_t capacity) {
  if (admission.error()) return admission.error();
  if (diagnostic == nullptr || (pages == nullptr && capacity != 0))
    return Error::Make(ErrorCode::kNullPointer);
  if (!handle.valid()) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    std::lock_guard<std::mutex> lock(detail::op_mutex());
    return Registry::instance().recovery_snapshot(detail_api::id(handle), *diagnostic, pages,
                                                  capacity);
  } catch (...) {
    return detail::current_exception_error();
  }
}
}  // namespace detail

}  // namespace yukiplt

namespace yukiplt::detail {

namespace {

constexpr uint32_t bit(ErrorCause cause) noexcept {
  return uint32_t{1} << static_cast<uint32_t>(cause);
}
static_assert(static_cast<uint32_t>(ErrorCause::kSlotDrift) < 32);

struct CausePolicy {
  ErrorCause fallback = ErrorCause::kNone;
  uint32_t accepted = 0;
  ErrorCause with_errno = ErrorCause::kNone;
};

// One exhaustive table owns both compatibility and the fallback cause. A new
// ErrorCode without a policy is a -Wswitch error in the project build.
constexpr CausePolicy policy(ErrorCode code) noexcept {
  using C = ErrorCause;
  switch (code) {
    case ErrorCode::kForkInProgress:
    case ErrorCode::kForkChildUnavailable:
      return {C::kForkProtocol, bit(C::kForkProtocol)};
    case ErrorCode::kOutOfBounds:
      return {C::kMetadataBounds,
              bit(C::kMetadataBounds) | bit(C::kScanBudget) | bit(C::kMapsCoverage)};
    case ErrorCode::kInvalidArgument:
      return {C::kInvalidRequest,
              bit(C::kCacheInvalid) | bit(C::kCacheConflict) | bit(C::kForkProtocol) |
                  bit(C::kInvalidRequest) | bit(C::kHandleInvalid) | bit(C::kOwnImage) |
                  bit(C::kDuplicateTarget) | bit(C::kOutputAlias) | bit(C::kReplacementIsCurrent) |
                  bit(C::kOriginalCapacity) | bit(C::kHostCallback)};
    case ErrorCode::kNullPointer:
      return {C::kInvalidRequest, bit(C::kInvalidRequest)};
    case ErrorCode::kPermissionDenied:
      return {C::kVmaPolicy, bit(C::kVmaPolicy) | bit(C::kProtectFailed), C::kProtectFailed};
    case ErrorCode::kSlotOwnershipConflict:
      return {C::kCasConflict, bit(C::kExpectedCurrent) | bit(C::kCasConflict)};
    case ErrorCode::kUnsupportedFormat:
      return {C::kNone, bit(C::kInternalException) | bit(C::kHostCallback) | bit(C::kRandomSource) |
                            bit(C::kPlatform)};
    case ErrorCode::kVmaUnavailable:
      return {C::kMapsUnavailable, bit(C::kMapsUnavailable)};
    case ErrorCode::kIndeterminate:
      return {C::kNone, bit(C::kProtectFailed) | bit(C::kCasConflict)};
    case ErrorCode::kImageInvalidated:
      return {C::kNone, bit(C::kIdentityMismatch)};
    case ErrorCode::kSlotModifiedExternally:
      return {C::kNone, bit(C::kSlotDrift)};
    case ErrorCode::kOutOfMemory:
      return {C::kNone, bit(C::kHostCallback)};
    case ErrorCode::kSuccess:
    case ErrorCode::kInvalidImage:
    case ErrorCode::kCorruptedElf:
    case ErrorCode::kUnsupportedRelocation:
    case ErrorCode::kSymbolNotFound:
    case ErrorCode::kNoHashTable:
    case ErrorCode::kAmbiguousSymbol:
    case ErrorCode::kAlreadyHooked:
    case ErrorCode::kNotInstalled:
    case ErrorCode::kIntegerOverflow:
    case ErrorCode::kImageNotFound:
    case ErrorCode::kAmbiguousImage:
      return {};
  }
  return {};  // Unknown external numeric value: never infer a cause.
}
}  // namespace

ErrorCause resolve_cause(const Error& error, ErrorCause noted) noexcept {
  if (error.ok()) return ErrorCause::kNone;
  const auto rule = policy(error.code);
  const auto value = static_cast<uint32_t>(noted);
  if (value < 32 && (rule.accepted & (uint32_t{1} << value))) return noted;
  return error.sys_errno != 0 && rule.with_errno != ErrorCause::kNone ? rule.with_errno
                                                                      : rule.fallback;
}

}  // namespace yukiplt::detail

namespace yukiplt {
void* load_original(void* const* storage) noexcept {
  // The plain PLT/GOT read is before this fence. A load-acquire by itself
  // orders only subsequent accesses, so it cannot replace this read fence.
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  return __atomic_load_n(storage, __ATOMIC_ACQUIRE);
}
}  // namespace yukiplt

extern "C" YUKIPLT_API void* yukiplt_c_load_original(void* const* storage) noexcept {
  return yukiplt::load_original(storage);
}

namespace yukiplt {
HookResult hook_batch_cached(MapsCache& cache, const ImageView& image, const HookRequest* reqs,
                             size_t count, SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(diagnostic, [&](RecoveryDiagnostic* recorded,
                                           detail::CauseContext* cause) {
    return hook_batch_impl(image, reqs, count, reports, recorded, nullptr, cause, false, &cache);
  });
}
HookResult hook_batch_named_cached(MapsCache& cache, const NamedImage& image,
                                   const HookRequest* reqs, size_t count, SlotReport* reports,
                                   OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  if (image.image.instance_token || image.path.find('\0') != std::string::npos) {
    Error error = Error::Make(ErrorCode::kInvalidArgument);
    finish_diagnostic(diagnostic, {}, error);
    return {error, HookHandle{}};
  }
  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             return hook_batch_impl(image.image, reqs, count, reports, recorded,
                                                    image.path.c_str(), cause, false, &cache);
                           });
}
HookResult hook_imports_cached(MapsCache& cache, const ImageView& image,
                               const SelectedHookRequest* reqs, size_t count, SlotReport* reports,
                               OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(
      diagnostic, [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
        return hook_imports_impl(image, nullptr, reqs, count, reports, recorded, cause, &cache);
      });
}
HookResult hook_imports_named_cached(MapsCache& cache, const NamedImage& image,
                                     const SelectedHookRequest* reqs, size_t count,
                                     SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  if (image.image.instance_token || image.path.find('\0') != std::string::npos) {
    Error error = Error::Make(ErrorCode::kInvalidArgument);
    finish_diagnostic(diagnostic, {}, error);
    return {error, HookHandle{}};
  }
  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             return hook_imports_impl(image.image, image.path.c_str(), reqs, count,
                                                      reports, recorded, cause, &cache);
                           });
}
HookResult hook_function_imports_cached(MapsCache& cache, const ImageView& image,
                                        const FunctionHookRequestEx* reqs, size_t count,
                                        SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  return diagnosed_install(
      diagnostic, [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
        return hook_functions_impl(image, nullptr, reqs, count, reports, recorded, cause, &cache);
      });
}
HookResult hook_function_imports_named_cached(MapsCache& cache, const NamedImage& image,
                                              const FunctionHookRequestEx* reqs, size_t count,
                                              SlotReport* reports,
                                              OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  if (image.image.instance_token || image.path.find('\0') != std::string::npos) {
    Error error = Error::Make(ErrorCode::kInvalidArgument);
    finish_diagnostic(diagnostic, {}, error);
    return {error, HookHandle{}};
  }
  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             return hook_functions_impl(image.image, image.path.c_str(), reqs,
                                                        count, reports, recorded, cause, &cache);
                           });
}
HookResult hook_batch_nonnull_named_cached(MapsCache& cache, const NamedImage& image,
                                           const HookRequest* reqs, size_t count,
                                           SlotReport* reports, OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return {admission.error(), HookHandle{}};
  }

  if (image.image.instance_token || image.path.find('\0') != std::string::npos) {
    Error error = Error::Make(ErrorCode::kInvalidArgument);
    finish_diagnostic(diagnostic, {}, error);
    return {error, HookHandle{}};
  }
  return diagnosed_install(diagnostic,
                           [&](RecoveryDiagnostic* recorded, detail::CauseContext* cause) {
                             return hook_batch_impl(image.image, reqs, count, reports, recorded,
                                                    image.path.c_str(), cause, true, &cache);
                           });
}
Error unhook_cached(MapsCache& cache, HookHandle& handle, SlotReport* reports, size_t cap,
                    OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return admission.error();
  }

  detail::CauseContext cause;
  RecoveryDiagnostic recorded;
  bool held = handle.valid();
  Error error = unhook_impl(handle, reports, cap, &recorded, &cause, &cache);
  finish_diagnostic(diagnostic, recorded, error, &cause);
  if (diagnostic) diagnostic->handle_consumed = held && !handle.valid();
  return error;
}
}  // namespace yukiplt
