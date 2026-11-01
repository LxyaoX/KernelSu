// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "common.hpp"
#include "imports.hpp"
#include "maps_cache.hpp"
#include "system_images.hpp"
#include "yukiplt.hpp"

namespace yukiplt {

namespace {

bool same_image(const ImageView& a, const ImageView& b) noexcept {
  return a.load_bias == b.load_bias && a.phdr_table == b.phdr_table &&
         a.phdr_count == b.phdr_count && a.instance_token == b.instance_token;
}

// Only borrowed filter strings and owned enumeration results, scoped to one
// refresh. Installation still validates the live instance and path under the
// linker lock. No addresses from this cache are dereferenced here.
class RefreshImages {
 public:
  const Result<std::vector<NamedImage>>& get(const ImageSelector& filter) {
    auto matches = [&](const Entry& entry) {
      if (entry.filter.match != filter.match) return false;
      if (filter.match == ImageMatch::kLoadBias && entry.filter.load_bias != filter.load_bias)
        return false;
      if ((filter.match == ImageMatch::kExactPath || filter.match == ImageMatch::kBasename) &&
          std::strcmp(entry.filter.name, filter.name) != 0)
        return false;
      return true;
    };
    if (!first_) {
      first_.emplace(Entry{filter, detail::active_image_enumerator()(filter)});
      return first_->images;
    }
    if (matches(*first_)) return first_->images;
    for (const auto& entry : entries_)
      if (matches(entry)) return entry.images;
    entries_.push_back({filter, detail::active_image_enumerator()(filter)});
    return entries_.back().images;
  }

 private:
  struct Entry {
    ImageSelector filter;
    Result<std::vector<NamedImage>> images;
  };
  std::optional<Entry> first_;
  std::vector<Entry> entries_;
};

}  // namespace

// Rebuild a rule's borrowed pointers to point into its own owned strings (needed
// after the rules_ vector reallocates).  Static member so it can touch Rule.
void RuleSet::rebind_selector(Rule& rule) noexcept {
  rule.selector.base.symbol = rule.symbol.c_str();
  rule.selector.base.version = rule.version.empty() ? nullptr : rule.version.c_str();
  rule.selector.base.dependency = rule.dependency.empty() ? nullptr : rule.dependency.c_str();
  rule.filter.name = rule.filter_name.empty() ? nullptr : rule.filter_name.c_str();
}

RuleSet::~RuleSet() {
  // Like HookSession/HookHandle: destruction does not unhook.  A live handle
  // logs the core's warning when its HookHandle is destroyed.
}

RuleSet::Rule* RuleSet::find_rule(uint64_t id) noexcept {
  Rule* rule = rule_by_id(id);
  return rule && !rule->removed ? rule : nullptr;
}
const RuleSet::Rule* RuleSet::rule_by_id(uint64_t id) const noexcept {
  const auto it =
      std::lower_bound(rules_.begin(), rules_.end(), id,
                       [](const Rule& rule, uint64_t wanted) { return rule.id < wanted; });
  return it != rules_.end() && it->id == id ? &*it : nullptr;
}
RuleSet::Rule* RuleSet::rule_by_id(uint64_t id) noexcept {
  return const_cast<Rule*>(static_cast<const RuleSet&>(*this).rule_by_id(id));
}

size_t RuleSet::BindingKeyHash::operator()(const BindingKey& k) const noexcept {
  uint64_t h = k.rule_id * 0x9E3779B97F4A7C15ull;
  auto mix = [&h](uint64_t v) { h = (h ^ v) * 0x100000001B3ull; };
  mix(k.load_bias);
  mix(reinterpret_cast<uintptr_t>(k.phdr_table));
  mix(k.phdr_count);
  mix(k.instance_token);
  return static_cast<size_t>(h ^ (h >> 32));
}

size_t RuleSet::find_binding(uint64_t rule_id, const ImageView& image) const noexcept {
  auto it = binding_index_.find(key_of(rule_id, image));
  return it == binding_index_.end() ? ~size_t{0} : it->second;
}

size_t RuleSet::retained_for(uint64_t rule_id, const ImageView* image,
                             bool active_only) const noexcept {
  size_t count = 0;
  for (size_t i = 0; i < bindings_.size(); ++i) {
    const auto& binding = bindings_[i];
    if (!handles_[i].valid() || (rule_id != 0 && binding.rule_id != rule_id) ||
        (image != nullptr && !same_image(binding.image, *image)))
      continue;
    const Rule* rule = active_only ? rule_by_id(binding.rule_id) : nullptr;
    if (active_only && (!rule || rule->removed)) continue;
    ++count;
  }
  return count;
}

Result<uint64_t> RuleSet::add_rule(const ImageSelector& filter,
                                   const FunctionImportSelector& selector, void* replacement,
                                   void** out_originals, size_t capacity) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (next_rule_id_ == 0) return Error::Make(ErrorCode::kIntegerOverflow);
  if (replacement == nullptr) return Error::Make(ErrorCode::kNullPointer);
  // Validate the filter and selector up front (same rules as find_images() and
  // hook_function_imports(), including a null symbol -> kNullPointer and an
  // empty one -> kInvalidArgument), so a malformed rule never fails later.
  if (Error e = detail::validate_image_selector(filter)) return e;
  if (Error e = detail::validate_function_selector(selector)) return e;
  if (capacity > 0 && (out_originals == nullptr ||
                       reinterpret_cast<uintptr_t>(out_originals) % alignof(void*) != 0))
    return Error::Make(ErrorCode::kInvalidArgument);
  try {
    Rule rule;
    rule.id = next_rule_id_;
    rule.filter = filter;
    rule.filter_name = (filter.name != nullptr) ? std::string(filter.name) : std::string();
    rule.selector = selector;
    rule.symbol = selector.base.symbol;
    rule.version =
        (selector.base.version != nullptr) ? std::string(selector.base.version) : std::string();
    rule.dependency = (selector.base.dependency != nullptr) ? std::string(selector.base.dependency)
                                                            : std::string();
    rule.replacement = replacement;
    rule.out_originals = out_originals;
    rule.capacity = capacity;
    rules_.push_back(std::move(rule));
    // Re-point every rule's borrowed pointers: push_back may have reallocated.
    for (auto& existing : rules_) rebind_selector(existing);
    ++next_rule_id_;
    return rules_.back().id;
  } catch (...) {
    return detail::current_exception_error();
  }
}

Result<uint64_t> RuleSet::add_rule(const ImageSelector& filter, const ImportSelector& selector,
                                   void* replacement, void** out_originals, size_t capacity) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  // Promote a plain name/version selector to a JUMP_SLOT-only function selector.
  FunctionImportSelector func;
  func.base = selector;
  func.sources = kSourceJmprel;
  func.types = kTypeJumpSlot;
  func.index_source = RelocationSource::kJmprel;
  return add_rule(filter, func, replacement, out_originals, capacity);
}

namespace {

// A pre-install invalidation is a clean retry using the same original slot.
// Only a previously installed binding confirmed gone becomes Invalidated.
RuleState install_state(const HookResult& result) noexcept {
  if (result.ok()) return RuleState::kInstalled;
  return result.handle.valid() ? RuleState::kIndeterminate : RuleState::kInstallFailed;
}

// Ensure one more push_back cannot allocate, doubling capacity when full.  May
// throw std::bad_alloc; called only while nothing is installed yet.
template <typename Vector>
void reserve_for_one_more(Vector& v) {
  if (v.size() < v.capacity()) return;
  if (v.size() == v.max_size()) throw std::length_error("RuleSet capacity");
  v.reserve(v.empty() ? 8 : v.size() > v.max_size() / 2 ? v.max_size() : v.size() * 2);
}

void read_recovery(const HookHandle& handle, RecoveryDiagnostic& diagnostic) {
  const Error error = query_recovery(handle, &diagnostic);
  if (error) {
    diagnostic = {};
    diagnostic.error = error;
  }
}

// Does one rule's image filter match this specific (image, path)?  Pure local
// test — never calls the enumerator.  A system image (token 0) is only matched by
// main-executable/load-bias filters; a custom image (token != 0) is matched by
// path filters and load-bias.
bool filter_matches(const ImageSelector& filter, const ImageView& image,
                    const char* path) noexcept {
  const char* p = path != nullptr ? path : "";
  switch (filter.match) {
    case ImageMatch::kMainExecutable:
      return false;  // Cannot confirm "main executable" without enumeration.
    case ImageMatch::kLoadBias:
      return image.load_bias == filter.load_bias;
    case ImageMatch::kExactPath:
      return filter.name != nullptr && std::strcmp(p, filter.name) == 0;
    case ImageMatch::kBasename: {
      if (filter.name == nullptr) return false;
      const char* slash = std::strrchr(p, '/');
      return std::strcmp(slash != nullptr ? slash + 1 : p, filter.name) == 0;
    }
  }
  return false;
}

}  // namespace

// Apply one rule to one already-matched image instance.  Shared by refresh()
// (enumerated images) and notify_loaded() (a caller-supplied custom image).
// `expected_path` is the enumerated name a system image must still carry under
// the linker lock (null: no path check).
void RuleSet::apply_rule_to_image(Rule& rule, const ImageView& image, const char* path,
                                  const char* expected_path, RuleSummary& summary,
                                  RuleOperationEvent* event, MapsCache* cache) {
  const size_t existing = find_binding(rule.id, image);
  if (event && existing != ~size_t{0}) {
    event->binding_index = existing;
    event->state = bindings_[existing].state;
    event->error = bindings_[existing].error;
    if (handles_[existing].valid()) read_recovery(handles_[existing], event->recovery);
  }
  // Retry clean failures in place. Reapply ended bindings with fresh originals;
  // published slots may still be read and are never reused. Keep unresolved and
  // abandoned bindings, and revalidate installed system bindings during refresh.
  bool retry_in_place = false;
  if (existing != ~size_t{0}) {
    const RuleState st = bindings_[existing].state;
    if (st == RuleState::kCapacityExhausted) {
      ++summary.failed;
      if (event) {
        event->stage = RuleOperationStage::kCapacity;
        event->attempted = false;
        event->recovery = {};
        event->recovery.error = bindings_[existing].error;
      }
      return;
    }
    if (st == RuleState::kPending || st == RuleState::kInstallFailed) {
      retry_in_place = true;
    } else if (st != RuleState::kReleased && st != RuleState::kInvalidated) {
      return;
    }
  }
  const bool new_binding = !retry_in_place;

  // Each new binding consumes one original slot permanently; clean retries reuse
  // that binding's slot. Disabled/capacity-failed bindings use the no-slot sentinel.
  const size_t candidate_index = new_binding ? rule.next_slot : bindings_[existing].original_index;
  const size_t slot_index = candidate_index < rule.capacity ? candidate_index : kNoOriginalIndex;
  void** out = slot_index != kNoOriginalIndex ? &rule.out_originals[slot_index] : nullptr;

  // Prepare the binding and index before installation can publish ownership.
  // Superseded bindings remain history but leave the current index.
  RuleBinding pending;
  std::string retry_path;
  auto record_prepare_failure = [&](Error error) {
    ++summary.attempted;
    ++summary.failed;
    if (event) {
      event->attempted = true;
      event->stage = RuleOperationStage::kBindingPrepare;
      event->error = error;
      event->state = RuleState::kInstallFailed;
      event->binding_index = ~size_t{0};
      event->recovery = {};
    }
  };
  try {
    if (new_binding) {
      pending.rule_id = rule.id;
      pending.image = image;
      if (path != nullptr) pending.path = path;
      pending.original_index = slot_index;
      // Grow geometrically: reserving exactly size()+1 would reallocate (and move
      // every binding and handle) on each new binding, making refresh quadratic.
      reserve_for_one_more(bindings_);
      reserve_for_one_more(handles_);
      binding_index_[key_of(rule.id, image)] = bindings_.size();
    } else if (path != nullptr) {
      // Prepare before install: updating a successful retry must not allocate
      // after the core has acquired ownership or published an original value.
      retry_path = path;
    }
  } catch (...) {
    record_prepare_failure(detail::current_exception_error());
    return;
  }

  ++summary.attempted;

  // Original storage exhausted: refuse before any mprotect/publish/CAS.
  const bool capacity_refused = rule.capacity != 0 && slot_index == kNoOriginalIndex;
  if (new_binding && slot_index != kNoOriginalIndex) rule.next_slot = slot_index + 1;

  HookResult result{Error::Make(ErrorCode::kInvalidArgument), HookHandle{}};
  SlotReport report{};
  if (!capacity_refused) {
    FunctionHookRequest request{rule.selector, rule.replacement, out};
    result = detail::hook_function_imports_at(image, expected_path, &request, 1, &report, cache);
  }

  if (new_binding) {
    pending.state = capacity_refused ? RuleState::kCapacityExhausted : install_state(result);
    pending.error = result.error;
    pending.install_report = report;
    bindings_.push_back(std::move(pending));  // reserved above: no throw
    handles_.push_back(std::move(result.handle));
  } else {
    if (result.ok() || result.handle.valid()) bindings_[existing].path.swap(retry_path);
    bindings_[existing].state =
        capacity_refused ? RuleState::kCapacityExhausted : install_state(result);
    bindings_[existing].error = result.error;
    bindings_[existing].install_report = report;
    handles_[existing] = std::move(result.handle);
  }

  const size_t idx = new_binding ? bindings_.size() - 1 : existing;
  if (event) {
    event->attempted = true;
    event->stage = capacity_refused ? RuleOperationStage::kCapacity : RuleOperationStage::kInstall;
    event->error = result.error;
    event->state = bindings_[idx].state;
    event->binding_index = idx;
    event->recovery = {};
    event->recovery.error = result.error;
    event->recovery.image_invalidated = result.error.code == ErrorCode::kImageInvalidated;
    if (handles_[idx].valid()) read_recovery(handles_[idx], event->recovery);
  }
  if (result.ok())
    ++summary.succeeded;
  else
    ++summary.failed;
  if (handles_[idx].valid()) ++summary.retained;
}

// Recheck retained system bindings under the linker lock without writing.
// A mismatch preserves handle and ownership; third-party values are never adopted.
void RuleSet::record_verification(size_t index, Error error, RuleSummary& summary,
                                  RuleOperationEvent& event) {
  event.binding_index = index;
  event.state = bindings_[index].state;
  event.error = error;
  event.recovery = {};
  if (handles_[index].valid()) read_recovery(handles_[index], event.recovery);
  bindings_[index].error = error;
  if (!error) return;
  ++summary.attempted;
  ++summary.failed;
  event.attempted = true;
}

RuleSummary RuleSet::refresh_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  RuleOperationReport report;
  return refresh_impl(report, cache);
}
RuleSummary RuleSet::refresh() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return refresh_impl(nullptr);
}
RuleSummary RuleSet::refresh_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return refresh_impl(&cache);
}

namespace {

// Undo one binding through the public unhook(), updating its state.  Returns
// true on a clean release.
bool undo_binding(RuleBinding& binding, HookHandle& handle, RuleSummary& summary,
                  MapsCache* cache) {
  if (!handle.valid()) return false;
  ++summary.attempted;
  SlotReport report{};
  Error error = cache ? yukiplt::unhook_cached(*cache, handle, &report, 1)
                      : yukiplt::unhook(handle, &report, 1);
  binding.restore_report = report;
  binding.error = error;
  if (!error) {
    binding.state = RuleState::kReleased;
    ++summary.succeeded;
    return true;
  }
  if (error.code == ErrorCode::kImageInvalidated) {
    binding.state = RuleState::kInvalidated;
    ++summary.succeeded;  // The image is gone; the handle is consumed.
    return true;
  }
  binding.state = RuleState::kRestoreFailed;
  ++summary.failed;
  if (handle.valid()) ++summary.retained;
  return false;
}

}  // namespace

RuleSummary RuleSet::release_image_impl(const ImageView& image, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  RuleSummary summary;
  // Reverse order so dependent installs undo last-in-first-out.
  for (size_t n = bindings_.size(); n-- > 0;) {
    if (!same_image(bindings_[n].image, image)) continue;
    undo_binding(bindings_[n], handles_[n], summary, cache);
  }
  summary.retained = retained_for(0, &image);
  return summary;
}
RuleSummary RuleSet::release_image(const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return release_image_impl(image, nullptr);
}
RuleSummary RuleSet::release_image_cached(MapsCache& cache, const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return release_image_impl(image, &cache);
}

RuleSummary RuleSet::notify_loaded_impl(const ImageView& image, const char* path,
                                        MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  RuleSummary summary;
  // A loader notifies about a specific custom instance; a system image (token 0)
  // has no stable instance identity here and is handled through refresh().
  if (image.instance_token == 0) {
    summary.attempted = 1;
    summary.failed = 1;
    return summary;
  }
  for (auto& rule : rules_) {
    if (rule.removed) continue;
    rebind_selector(rule);
    if (!filter_matches(rule.filter, image, path)) continue;
    apply_rule_to_image(rule, image, path, nullptr, summary, nullptr, cache);
  }
  summary.retained = 0;
  for (const auto& rule : rules_)
    if (!rule.removed && filter_matches(rule.filter, image, path))
      summary.retained += retained_for(rule.id, &image);
  return summary;
}
RuleSummary RuleSet::notify_loaded(const ImageView& image, const char* path) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_loaded_impl(image, path, nullptr);
}
RuleSummary RuleSet::notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                          const char* path) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_loaded_impl(image, path, &cache);
}

RuleSummary RuleSet::notify_unloading_impl(const ImageView& image, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  if (image.instance_token == 0) return {1, 0, 1, 0};
  // Undo exactly this instance's bindings; identical to release_image(), named
  // for the loader lifecycle pairing with notify_loaded().
  return release_image_impl(image, cache);
}
RuleSummary RuleSet::notify_unloading(const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_unloading_impl(image, nullptr);
}
RuleSummary RuleSet::notify_unloading_cached(MapsCache& cache, const ImageView& image) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_unloading_impl(image, &cache);
}

RuleSummary RuleSet::remove_rule_impl(uint64_t rule_id, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  RuleSummary summary;
  Rule* rule = find_rule(rule_id);
  if (rule == nullptr) {
    summary.failed = 1;  // unknown or already-removed rule
    summary.retained = rule_id == 0 ? 0 : retained_for(rule_id);
    return summary;
  }
  for (size_t n = bindings_.size(); n-- > 0;) {
    if (bindings_[n].rule_id != rule_id) continue;
    undo_binding(bindings_[n], handles_[n], summary, cache);
  }
  // Stop participating in future refreshes regardless; a retained failed handle
  // is still reachable through bindings()/unhook() for a retry.
  rule->removed = true;
  summary.retained = retained_for(rule_id);
  return summary;
}
RuleSummary RuleSet::remove_rule(uint64_t rule_id) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return remove_rule_impl(rule_id, nullptr);
}
RuleSummary RuleSet::remove_rule_cached(MapsCache& cache, uint64_t rule_id) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return remove_rule_impl(rule_id, &cache);
}

RuleSummary RuleSet::unhook_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  RuleSummary summary;
  for (size_t n = bindings_.size(); n-- > 0;)
    undo_binding(bindings_[n], handles_[n], summary, cache);
  summary.retained = retained_for();
  return summary;
}
RuleSummary RuleSet::unhook() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(nullptr);
}
RuleSummary RuleSet::unhook_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(&cache);
}

RuleSummary RuleSet::abandon() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  if (detail::process_entry()) return {0, 0, 1, 0};
  RuleSummary summary;
  for (size_t n = bindings_.size(); n-- > 0;) {
    if (!handles_[n].valid()) continue;
    ++summary.attempted;
    Error error = yukiplt::abandon(handles_[n]);
    bindings_[n].error = error;
    if (!error) {
      bindings_[n].state = RuleState::kAbandoned;
      ++summary.succeeded;
    } else {
      ++summary.failed;
      if (handles_[n].valid()) ++summary.retained;
    }
  }
  summary.retained = retained_for();
  return summary;
}

RuleSetStatistics RuleSet::statistics() const noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {};

  RuleSetStatistics result{rules_.size(), 0, bindings_.size(), retained_for()};
  for (const auto& rule : rules_)
    if (!rule.removed) ++result.active_rules;
  return result;
}

Result<size_t> RuleSet::prune_history() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  const auto removable = [&](size_t i) {
    return !handles_[i].valid() && (bindings_[i].state == RuleState::kReleased ||
                                    bindings_[i].state == RuleState::kInvalidated);
  };
  size_t removed = 0;
  for (size_t i = 0; i < bindings_.size(); ++i)
    if (removable(i)) ++removed;
  if (removed == 0) return size_t{0};
  try {
    const size_t retained = bindings_.size() - removed;
    std::vector<size_t> remap(bindings_.size(), ~size_t{0});
    size_t next = 0;
    for (size_t i = 0; i < bindings_.size(); ++i)
      if (!removable(i)) remap[i] = next++;
    std::vector<RuleBinding> bindings;
    std::vector<HookHandle> handles;
    decltype(binding_index_) index;
    bindings.reserve(retained);
    handles.reserve(retained);
    index.reserve(std::min(retained, binding_index_.size()));
    // Only current mappings may survive: never promote an older historical
    // record to replace a current mapping being removed.
    for (const auto& entry : binding_index_)
      if (remap[entry.second] != ~size_t{0}) index.emplace(entry.first, remap[entry.second]);

    // All allocation has finished. Move live handles exactly once, without
    // invoking their move-assignment/drop behavior or exposing a partial state.
    static_assert(std::is_nothrow_move_constructible_v<RuleBinding>);
    static_assert(std::is_nothrow_move_constructible_v<HookHandle>);
    static_assert(std::is_nothrow_swappable_v<decltype(binding_index_)>);
    for (size_t i = 0; i < bindings_.size(); ++i) {
      if (remap[i] == ~size_t{0}) continue;
      bindings.push_back(std::move(bindings_[i]));
      handles.push_back(std::move(handles_[i]));
    }
    bindings_.swap(bindings);
    handles_.swap(handles);
    binding_index_.swap(index);
    return removed;
  } catch (...) {
    return detail::current_exception_error();
  }
}

Result<RuleStatistics> RuleSet::statistics(uint64_t rule_id) const noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  const Rule* found = rule_by_id(rule_id);
  if (!found) return Error::Make(ErrorCode::kInvalidArgument);
  const Rule& rule = *found;
  RuleStatistics result{rule_id,
                        rule.removed,
                        rule.capacity != 0,
                        rule.capacity,
                        rule.next_slot,
                        rule.capacity - rule.next_slot,
                        0,
                        retained_for(rule_id)};
  for (const auto& binding : bindings_)
    if (binding.rule_id == rule_id) ++result.bindings;
  return result;
}

namespace {
void reset_report(RuleOperationReport& report) noexcept {
  report.preparation_error = Error::Ok();
  report.events.clear();
}
RuleSummary report_failure(RuleOperationReport& report, Error error, size_t retained) noexcept {
  report.events.clear();
  report.preparation_error = error;
  return {0, 0, 1, retained};
}
}  // namespace

RuleSummary RuleSet::refresh_impl(RuleOperationReport& report, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  reset_report(report);
  std::vector<detail::InstalledBindingVerification> verification;
  std::vector<size_t> verification_events;
  try {
    // Finish enumeration and all diagnostic string allocation before the first
    // installation. An enumeration error belongs to its rule, not to an unload.
    RefreshImages enumeration_cache;
    std::unordered_set<BindingKey, BindingKeyHash> targets;
    for (auto& rule : rules_) {
      if (rule.removed) continue;
      rebind_selector(rule);
      const auto& images = enumeration_cache.get(rule.filter);
      if (!images) {
        RuleOperationEvent event;
        event.rule_id = rule.id;
        event.stage = RuleOperationStage::kEnumerate;
        event.error = images.error();
        report.events.push_back(std::move(event));
      } else {
        for (const auto& named : images.value()) {
          if (!targets.insert(key_of(rule.id, named.image)).second) continue;
          RuleOperationEvent event;
          event.rule_id = rule.id;
          event.image = named.image;
          event.path = named.path;
          event.stage = RuleOperationStage::kInstall;
          report.events.push_back(std::move(event));
        }
      }
    }
    // No binding/handle storage changes until the batch has returned: its
    // pointers refer to the existing owning strings and handles. The batch's
    // own allocation/exception failure also precedes every installation.
    verification.reserve(report.events.size());
    verification_events.reserve(report.events.size());
    for (size_t i = 0; i < report.events.size(); ++i) {
      auto& event = report.events[i];
      if (event.stage == RuleOperationStage::kEnumerate || event.image.instance_token != 0)
        continue;
      const size_t index = find_binding(event.rule_id, event.image);
      if (index == ~size_t{0} || bindings_[index].state != RuleState::kInstalled) continue;
      event.stage = RuleOperationStage::kVerify;
      event.binding_index = index;
      verification.push_back({&handles_[index],
                              event.image,
                              bindings_[index].path.c_str(),
                              event.path.c_str(),
                              {},
                              {}});
      verification_events.push_back(i);
    }
    if (Error error = detail::verify_installed_bindings(verification.data(), verification.size(),
                                                        nullptr, cache))
      return report_failure(report, error, 0);
  } catch (...) {
    return report_failure(report, detail::current_exception_error(), 0);
  }
  RuleSummary summary;
  for (size_t i = 0; i < verification.size(); ++i) {
    auto& event = report.events[verification_events[i]];
    record_verification(event.binding_index, verification[i].error, summary, event);
  }
  for (auto& event : report.events) {
    if (event.stage == RuleOperationStage::kVerify) continue;
    if (event.stage == RuleOperationStage::kEnumerate) {
      if (event.error.code != ErrorCode::kImageNotFound) ++summary.failed;
      continue;
    }
    Rule& rule = *rule_by_id(event.rule_id);
    apply_rule_to_image(rule, event.image, event.path.c_str(), event.path.c_str(), summary, &event,
                        cache);
  }
  summary.retained = 0;
  for (const auto& event : report.events)
    if (event.binding_index < handles_.size() && handles_[event.binding_index].valid())
      ++summary.retained;
  return summary;
}
RuleSummary RuleSet::refresh(RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return refresh_impl(report, nullptr);
}
RuleSummary RuleSet::refresh_cached(MapsCache& cache, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return refresh_impl(report, &cache);
}

RuleSummary RuleSet::notify_loaded_impl(const ImageView& image, const char* path,
                                        RuleOperationReport& report, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  // Inputs may refer to the previous report's owned metadata.
  RuleOperationReport previous = std::move(report);
  report = {};
  reset_report(report);
  if (image.instance_token == 0)
    return report_failure(report, Error::Make(ErrorCode::kInvalidArgument), 0);
  try {
    for (auto& rule : rules_) {
      if (rule.removed) continue;
      rebind_selector(rule);
      if (!filter_matches(rule.filter, image, path)) continue;
      RuleOperationEvent event;
      event.rule_id = rule.id;
      event.image = image;
      if (path) event.path = path;
      event.stage = RuleOperationStage::kInstall;
      report.events.push_back(std::move(event));
    }
  } catch (...) {
    const Error error = detail::current_exception_error();
    size_t retained = 0;
    for (const auto& rule : rules_)
      if (!rule.removed && filter_matches(rule.filter, image, path))
        retained += retained_for(rule.id, &image);
    return report_failure(report, error, retained);
  }
  RuleSummary summary;
  for (auto& event : report.events)
    apply_rule_to_image(*rule_by_id(event.rule_id), image, event.path.c_str(), nullptr, summary,
                        &event, cache);
  summary.retained = 0;
  for (const auto& event : report.events) summary.retained += retained_for(event.rule_id, &image);
  return summary;
}
RuleSummary RuleSet::notify_loaded(const ImageView& image, const char* path,
                                   RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_loaded_impl(image, path, report, nullptr);
}
RuleSummary RuleSet::notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                          const char* path, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_loaded_impl(image, path, report, &cache);
}

Error RuleSet::prepare_undo_report(RuleOperationReport& report, uint64_t rule_id,
                                   const ImageView* image, RuleOperationStage stage) const {
  reset_report(report);
  try {
    for (size_t n = bindings_.size(); n-- > 0;) {
      const auto& binding = bindings_[n];
      if (!handles_[n].valid() || (rule_id != 0 && binding.rule_id != rule_id) ||
          (image && !same_image(*image, binding.image)))
        continue;
      RuleOperationEvent event;
      event.rule_id = binding.rule_id;
      event.image = binding.image;
      event.path = binding.path;
      event.stage = stage;
      event.binding_index = n;
      event.attempted = true;
      if (stage == RuleOperationStage::kAbandon) {
        if (Error error = query_recovery(handles_[n], &event.recovery)) {
          report.preparation_error = error;
          report.events.clear();
          return error;
        }
      }
      report.events.push_back(std::move(event));
    }
    return Error::Ok();
  } catch (...) {
    report.preparation_error = detail::current_exception_error();
  }
  report.events.clear();
  return report.preparation_error;
}

void RuleSet::finish_undo_report(RuleOperationReport& report) const {
  for (auto& event : report.events) {
    const auto& binding = bindings_[event.binding_index];
    event.state = binding.state;
    event.error = binding.error;
    // Abandon consumes the registry entry, not its residual memory effects.
    // Preserve the pre-operation snapshot even when relinquishing succeeded.
    if (event.stage == RuleOperationStage::kAbandon) continue;
    event.recovery = {};
    event.recovery.error = binding.error;
    event.recovery.stage =
        binding.error.ok() ? OperationStage::kComplete : OperationStage::kPrepare;
    event.recovery.image_invalidated = binding.state == RuleState::kInvalidated;
    if (handles_[event.binding_index].valid())
      read_recovery(handles_[event.binding_index], event.recovery);
  }
}

RuleSummary RuleSet::release_image_impl(const ImageView& image, RuleOperationReport& report,
                                        MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  const ImageView target = image;  // image may live in the report being replaced.
  if (Error e = prepare_undo_report(report, 0, &target, RuleOperationStage::kRestore))
    return {0, 0, 1, retained_for(0, &target)};
  auto summary = release_image_impl(target, cache);
  finish_undo_report(report);
  return summary;
}
RuleSummary RuleSet::release_image(const ImageView& image, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return release_image_impl(image, report, nullptr);
}
RuleSummary RuleSet::release_image_cached(MapsCache& cache, const ImageView& image,
                                          RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return release_image_impl(image, report, &cache);
}

RuleSummary RuleSet::notify_unloading_impl(const ImageView& image, RuleOperationReport& report,
                                           MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  if (image.instance_token == 0)
    return report_failure(report, Error::Make(ErrorCode::kInvalidArgument), 0);
  return release_image_impl(image, report, cache);
}
RuleSummary RuleSet::notify_unloading(const ImageView& image, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_unloading_impl(image, report, nullptr);
}
RuleSummary RuleSet::notify_unloading_cached(MapsCache& cache, const ImageView& image,
                                             RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return notify_unloading_impl(image, report, &cache);
}

RuleSummary RuleSet::remove_rule_impl(uint64_t rule_id, RuleOperationReport& report,
                                      MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  if (find_rule(rule_id) == nullptr)
    return report_failure(report, Error::Make(ErrorCode::kInvalidArgument),
                          rule_id == 0 ? 0 : retained_for(rule_id));
  if (Error e = prepare_undo_report(report, rule_id, nullptr, RuleOperationStage::kRestore))
    return {0, 0, 1, retained_for(rule_id)};
  auto summary = remove_rule_impl(rule_id, cache);
  finish_undo_report(report);
  return summary;
}
RuleSummary RuleSet::remove_rule(uint64_t rule_id, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return remove_rule_impl(rule_id, report, nullptr);
}
RuleSummary RuleSet::remove_rule_cached(MapsCache& cache, uint64_t rule_id,
                                        RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return remove_rule_impl(rule_id, report, &cache);
}

RuleSummary RuleSet::unhook_impl(RuleOperationReport& report, MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};

  if (Error e = prepare_undo_report(report, 0, nullptr, RuleOperationStage::kRestore))
    return {0, 0, 1, retained_for()};
  auto summary = unhook_impl(cache);
  finish_undo_report(report);
  return summary;
}
RuleSummary RuleSet::unhook(RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(report, nullptr);
}
RuleSummary RuleSet::unhook_cached(MapsCache& cache, RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return unhook_impl(report, &cache);
}

RuleSummary RuleSet::abandon(RuleOperationReport& report) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  if (detail::process_entry()) return {0, 0, 1, 0};
  if (Error e = prepare_undo_report(report, 0, nullptr, RuleOperationStage::kAbandon))
    return {0, 0, 1, retained_for()};
  auto summary = abandon();
  finish_undo_report(report);
  return summary;
}

}  // namespace yukiplt

namespace yukiplt {
RuleSummary RuleSet::refresh_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return refresh_cached(cache);
}
RuleSummary RuleSet::release_image_cached(MapsCache& cache, const ImageView& image,
                                          Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return release_image_cached(cache, image);
}
RuleSummary RuleSet::notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                          const char* path, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_loaded_cached(cache, image, path);
}
RuleSummary RuleSet::notify_unloading_cached(MapsCache& cache, const ImageView& image,
                                             Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_unloading_cached(cache, image);
}
RuleSummary RuleSet::remove_rule_cached(MapsCache& cache, uint64_t rule_id,
                                        Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return remove_rule_cached(cache, rule_id);
}
RuleSummary RuleSet::unhook_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook_cached(cache);
}
RuleSummary RuleSet::refresh_cached(MapsCache& cache, RuleOperationReport& report,
                                    Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return refresh_cached(cache, report);
}
RuleSummary RuleSet::notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                          const char* path, RuleOperationReport& report,
                                          Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_loaded_cached(cache, image, path, report);
}
RuleSummary RuleSet::release_image_cached(MapsCache& cache, const ImageView& image,
                                          RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return release_image_cached(cache, image, report);
}
RuleSummary RuleSet::notify_unloading_cached(MapsCache& cache, const ImageView& image,
                                             RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_unloading_cached(cache, image, report);
}
RuleSummary RuleSet::remove_rule_cached(MapsCache& cache, uint64_t rule_id,
                                        RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return remove_rule_cached(cache, rule_id, report);
}
RuleSummary RuleSet::unhook_cached(MapsCache& cache, RuleOperationReport& report,
                                   Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook_cached(cache, report);
}
RuleSetStatistics RuleSet::statistics(Error& admission_error) const noexcept {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {};
  return statistics();
}
RuleSummary RuleSet::abandon(RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return abandon(report);
}
RuleSummary RuleSet::unhook(RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook(report);
}
RuleSummary RuleSet::remove_rule(uint64_t rule_id, RuleOperationReport& report,
                                 Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return remove_rule(rule_id, report);
}
RuleSummary RuleSet::release_image(const ImageView& image, RuleOperationReport& report,
                                   Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return release_image(image, report);
}
RuleSummary RuleSet::notify_unloading(const ImageView& image, RuleOperationReport& report,
                                      Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_unloading(image, report);
}
RuleSummary RuleSet::notify_loaded(const ImageView& image, const char* path,
                                   RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_loaded(image, path, report);
}
RuleSummary RuleSet::refresh(RuleOperationReport& report, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return refresh(report);
}
RuleSummary RuleSet::abandon(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return abandon();
}
RuleSummary RuleSet::unhook(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook();
}
RuleSummary RuleSet::remove_rule(uint64_t rule_id, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return remove_rule(rule_id);
}
RuleSummary RuleSet::notify_unloading(const ImageView& image, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_unloading(image);
}
RuleSummary RuleSet::notify_loaded(const ImageView& image, const char* path,
                                   Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return notify_loaded(image, path);
}
RuleSummary RuleSet::release_image(const ImageView& image, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return release_image(image);
}
RuleSummary RuleSet::refresh(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return refresh();
}
}  // namespace yukiplt
