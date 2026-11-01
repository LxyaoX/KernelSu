// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "c_api.hpp"
#include "yukiplt.hpp"

namespace {
using namespace yukiplt;
using namespace yukiplt::detail::c_objects;
using namespace yukiplt::detail::c_copy;
static_assert(YUKIPLT_C_FEATURE_RULES == 14);
static_assert(YUKIPLT_C_NO_ORIGINAL_INDEX == kNoOriginalIndex);
static_assert(YUKIPLT_C_RULE_PENDING == 0 && static_cast<uint32_t>(RuleState::kPending) == 0);
static_assert(YUKIPLT_C_RULE_INSTALLED == 1 && static_cast<uint32_t>(RuleState::kInstalled) == 1);
static_assert(YUKIPLT_C_RULE_INSTALL_FAILED == 2 &&
              static_cast<uint32_t>(RuleState::kInstallFailed) == 2);
static_assert(YUKIPLT_C_RULE_INDETERMINATE == 3 &&
              static_cast<uint32_t>(RuleState::kIndeterminate) == 3);
static_assert(YUKIPLT_C_RULE_RELEASED == 4 && static_cast<uint32_t>(RuleState::kReleased) == 4);
static_assert(YUKIPLT_C_RULE_INVALIDATED == 5 &&
              static_cast<uint32_t>(RuleState::kInvalidated) == 5);
static_assert(YUKIPLT_C_RULE_RESTORE_FAILED == 6 &&
              static_cast<uint32_t>(RuleState::kRestoreFailed) == 6);
static_assert(YUKIPLT_C_RULE_ABANDONED == 7 && static_cast<uint32_t>(RuleState::kAbandoned) == 7);
static_assert(YUKIPLT_C_RULE_CAPACITY_EXHAUSTED == 8 &&
              static_cast<uint32_t>(RuleState::kCapacityExhausted) == 8);
static_assert(YUKIPLT_C_RULE_VALIDATE == 0 &&
              static_cast<uint32_t>(RuleOperationStage::kValidate) == 0);
static_assert(YUKIPLT_C_RULE_ENUMERATE == 1 &&
              static_cast<uint32_t>(RuleOperationStage::kEnumerate) == 1);
static_assert(YUKIPLT_C_RULE_BINDING_PREPARE == 2 &&
              static_cast<uint32_t>(RuleOperationStage::kBindingPrepare) == 2);
static_assert(YUKIPLT_C_RULE_CAPACITY == 3 &&
              static_cast<uint32_t>(RuleOperationStage::kCapacity) == 3);
static_assert(YUKIPLT_C_RULE_INSTALL == 4 &&
              static_cast<uint32_t>(RuleOperationStage::kInstall) == 4);
static_assert(YUKIPLT_C_RULE_RESTORE == 5 &&
              static_cast<uint32_t>(RuleOperationStage::kRestore) == 5);
static_assert(YUKIPLT_C_RULE_ABANDON == 6 &&
              static_cast<uint32_t>(RuleOperationStage::kAbandon) == 6);
static_assert(YUKIPLT_C_RULE_VERIFY == 7 &&
              static_cast<uint32_t>(RuleOperationStage::kVerify) == 7);
struct Rules final : Object {
  Rules() : Object(Kind::kRules) {}
  RuleSet value;
  bool can_destroy() const noexcept override { return value.statistics().active_handles == 0; }
};
struct Report final : Object {
  Report() : Object(Kind::kRuleReport) {}
  RuleOperationReport value;
  yukiplt_c_rule_report_info info{};
};
struct Bindings final : Object {
  Bindings() : Object(Kind::kBindings) {}
  std::vector<RuleBinding> value;
};
template <typename T, typename H>
std::shared_ptr<T> get(const H* handle, Kind kind) {
  return handle ? std::static_pointer_cast<T>(acquire(handle->token, kind)) : nullptr;
}
bool valid_filter(const yukiplt_c_image_selector& f) noexcept {
  return f.struct_size == sizeof(f) && f.match <= YUKIPLT_C_IMAGE_BIAS;
}
ImageSelector filter(const yukiplt_c_image_selector& f) noexcept {
  return {static_cast<ImageMatch>(f.match), f.name, f.load_bias};
}
yukiplt_c_slot_report slot_report(const SlotReport& r) noexcept {
  return {r.address, r.value_before, r.value_observed, static_cast<uint32_t>(r.status)};
}
yukiplt_c_recovery_diagnostic diagnostic(const RecoveryDiagnostic& d) noexcept {
  return {static_cast<uint32_t>(d.stage),
          converted(d.error),
          d.raise_errno,
          d.restore_errno,
          d.rollback_failed,
          d.image_invalidated,
          d.residual_page_count};
}
Error first_failure(const RuleSummary& summary, const RuleOperationReport& report) noexcept {
  if (report.preparation_error) return report.preparation_error;
  if (!summary.failed) return Error::Ok();
  for (const auto& event : report.events) {
    if (!event.error) continue;
    if (!event.attempted && event.stage != RuleOperationStage::kCapacity &&
        event.stage != RuleOperationStage::kEnumerate)
      continue;
    if (event.stage == RuleOperationStage::kEnumerate &&
        event.error.code == ErrorCode::kImageNotFound)
      continue;
    if (event.stage == RuleOperationStage::kRestore && event.state == RuleState::kInvalidated &&
        event.error.code == ErrorCode::kImageInvalidated)
      continue;
    return event.error;
  }
  return Error::Make(ErrorCode::kIndeterminate);
}
template <typename F>
yukiplt_c_error run(yukiplt_c_rules* h, yukiplt_c_rule_report* out, F&& f) noexcept {
  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return invalid();
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto report = std::make_shared<Report>();
    auto token = publish(report);
    if (!token) return token.error();
    // Publish the report owner before entering the effectful core. Even an
    // unexpected exception cannot make a performed operation lose its report.
    out->token = token.value();
    Error error;
    try {
      auto summary = f(rules->value, report->value);
      report->info.summary = {summary.attempted, summary.succeeded, summary.failed,
                              summary.retained};
      report->info.preparation_error = converted(report->value.preparation_error);
      error = first_failure(summary, report->value);
    } catch (...) {
      error = yukiplt::detail::current_exception_error();
      report->info.summary.failed = 1;
    }
    report->info.operation_error = converted(error);
    return error;
  });
}
template <typename S>
yukiplt_c_error add(yukiplt_c_rules* h, const yukiplt_c_image_selector* f, const S* selected,
                    void* replacement, void** originals, size_t capacity, uint64_t* id) noexcept {
  return boundary([&]() -> Error {
    if (!h || !f || !selected || !id) return Error::Make(ErrorCode::kNullPointer);
    if (!valid_filter(*f) || !valid(*selected)) return invalid();
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto result =
        rules->value.add_rule(filter(*f), selector(*selected), replacement, originals, capacity);
    if (!result) return result.error();
    *id = result.value();
    return Error::Ok();
  });
}
}  // namespace

extern "C" {
yukiplt_c_error yukiplt_c_rules_create(yukiplt_c_rules* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return invalid();
    auto token = publish(std::make_shared<Rules>());
    if (!token) return token.error();
    out->token = token.value();
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rules_destroy(yukiplt_c_rules* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary(
      [&] { return h ? destroy(h->token, Kind::kRules) : Error::Make(ErrorCode::kNullPointer); });
}
yukiplt_c_error yukiplt_c_rules_add(yukiplt_c_rules* h, const yukiplt_c_image_selector* f,
                                    const yukiplt_c_selector* s, void* replacement,
                                    void** originals, size_t capacity, uint64_t* id) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return add(h, f, s, replacement, originals, capacity, id);
}
yukiplt_c_error yukiplt_c_rules_add_function(yukiplt_c_rules* h, const yukiplt_c_image_selector* f,
                                             const yukiplt_c_function_selector* s,
                                             void* replacement, void** originals, size_t capacity,
                                             uint64_t* id) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return add(h, f, s, replacement, originals, capacity, id);
}
yukiplt_c_error yukiplt_c_rules_refresh(yukiplt_c_rules* h, yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return run(h, out, [](RuleSet& r, RuleOperationReport& p) { return r.refresh(p); });
}
yukiplt_c_error yukiplt_c_rules_release_image(yukiplt_c_rules* h, const yukiplt_c_image* v,
                                              yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v) return converted(Error::Make(ErrorCode::kNullPointer));
  return run(h, out,
             [&](RuleSet& r, RuleOperationReport& p) { return r.release_image(image(*v), p); });
}
yukiplt_c_error yukiplt_c_rules_notify_loaded(yukiplt_c_rules* h, const yukiplt_c_image* v,
                                              const char* path,
                                              yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v || !path) return converted(Error::Make(ErrorCode::kNullPointer));
  return run(h, out, [&](RuleSet& r, RuleOperationReport& p) {
    return r.notify_loaded(image(*v), path, p);
  });
}
yukiplt_c_error yukiplt_c_rules_notify_unloading(yukiplt_c_rules* h, const yukiplt_c_image* v,
                                                 yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v) return converted(Error::Make(ErrorCode::kNullPointer));
  return run(h, out,
             [&](RuleSet& r, RuleOperationReport& p) { return r.notify_unloading(image(*v), p); });
}
yukiplt_c_error yukiplt_c_rules_remove(yukiplt_c_rules* h, uint64_t id,
                                       yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return run(h, out, [&](RuleSet& r, RuleOperationReport& p) { return r.remove_rule(id, p); });
}
yukiplt_c_error yukiplt_c_rules_unhook(yukiplt_c_rules* h, yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return run(h, out, [](RuleSet& r, RuleOperationReport& p) { return r.unhook(p); });
}
yukiplt_c_error yukiplt_c_rules_abandon(yukiplt_c_rules* h, yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return run(h, out, [](RuleSet& r, RuleOperationReport& p) { return r.abandon(p); });
}
yukiplt_c_error yukiplt_c_rules_prune_history(yukiplt_c_rules* h, size_t* removed) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !removed) return Error::Make(ErrorCode::kNullPointer);
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto result = rules->value.prune_history();
    if (!result) return result.error();
    *removed = result.value();
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rules_get_statistics(const yukiplt_c_rules* h,
                                               yukiplt_c_rules_statistics* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto s = rules->value.statistics();
    *out = {s.registered_rules, s.active_rules, s.bindings, s.active_handles};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rules_get_rule_statistics(const yukiplt_c_rules* h, uint64_t id,
                                                    yukiplt_c_rule_statistics* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto result = rules->value.statistics(id);
    if (!result) return result.error();
    const auto& s = result.value();
    *out = {s.rule_id,           s.removed,       s.original_storage_enabled,
            s.original_capacity, s.original_used, s.original_remaining,
            s.bindings,          s.active_handles};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rules_snapshot_bindings(const yukiplt_c_rules* h,
                                                  yukiplt_c_bindings* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return invalid();
    auto rules = get<Rules>(h, Kind::kRules);
    if (!rules) return invalid();
    auto result = std::make_shared<Bindings>();
    result->value = rules->value.bindings();
    auto token = publish(result);
    if (!token) return token.error();
    out->token = token.value();
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_bindings_destroy(yukiplt_c_bindings* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return h ? destroy(h->token, Kind::kBindings) : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_bindings_copy(const yukiplt_c_bindings* h, yukiplt_c_binding_record* rows,
                                        size_t capacity, char* text, size_t text_capacity,
                                        size_t* required_count, size_t* required_text) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h) return Error::Make(ErrorCode::kNullPointer);
    auto bindings = get<Bindings>(h, Kind::kBindings);
    if (!bindings) return invalid();
    size_t bytes = 0;
    for (const auto& b : bindings->value)
      if (!append_size(bytes, b.path)) return Error::Make(ErrorCode::kIntegerOverflow);
    bool sizes_only = false;
    if (auto e = buffers(bindings->value.size(), bytes, rows, capacity, text, text_capacity,
                         required_count, required_text, sizes_only))
      return e;
    if (sizes_only) return Error::Ok();
    size_t offset = 0, index = 0;
    for (const auto& b : bindings->value)
      rows[index++] = {b.rule_id,
                       image(b.image),
                       copy_text(b.path, text, offset),
                       b.original_index,
                       static_cast<uint32_t>(b.state),
                       converted(b.error),
                       slot_report(b.install_report),
                       slot_report(b.restore_report)};
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rule_report_destroy(yukiplt_c_rule_report* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return h ? destroy(h->token, Kind::kRuleReport) : Error::Make(ErrorCode::kNullPointer);
  });
}
yukiplt_c_error yukiplt_c_rule_report_get_info(const yukiplt_c_rule_report* h,
                                               yukiplt_c_rule_report_info* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    auto report = get<Report>(h, Kind::kRuleReport);
    if (!report) return invalid();
    *out = report->info;
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_rule_report_copy(const yukiplt_c_rule_report* h,
                                           yukiplt_c_rule_event* rows, size_t capacity, char* text,
                                           size_t text_capacity, size_t* required_count,
                                           size_t* required_text) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h) return Error::Make(ErrorCode::kNullPointer);
    auto report = get<Report>(h, Kind::kRuleReport);
    if (!report) return invalid();
    size_t bytes = 0;
    for (const auto& e : report->value.events)
      if (!append_size(bytes, e.path)) return Error::Make(ErrorCode::kIntegerOverflow);
    bool sizes_only = false;
    if (auto e = buffers(report->value.events.size(), bytes, rows, capacity, text, text_capacity,
                         required_count, required_text, sizes_only))
      return e;
    if (sizes_only) return Error::Ok();
    size_t offset = 0, index = 0;
    for (const auto& e : report->value.events)
      rows[index++] = {e.rule_id,
                       image(e.image),
                       copy_text(e.path, text, offset),
                       static_cast<uint32_t>(e.stage),
                       converted(e.error),
                       e.attempted,
                       static_cast<uint32_t>(e.state),
                       e.binding_index,
                       diagnostic(e.recovery)};
    return Error::Ok();
  });
}
}  // extern "C"

extern "C" {
yukiplt_c_error yukiplt_c_rules_refresh_cached(const yukiplt_c_maps_cache* cache,
                                               yukiplt_c_rules* h,
                                               yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.refresh_cached(lease->value, report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
yukiplt_c_error yukiplt_c_rules_unhook_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h,
                                              yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.unhook_cached(lease->value, report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
yukiplt_c_error yukiplt_c_rules_release_image_cached(const yukiplt_c_maps_cache* cache,
                                                     yukiplt_c_rules* h, const yukiplt_c_image* v,
                                                     yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v) return converted(Error::Make(ErrorCode::kNullPointer));
  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.release_image_cached(lease->value, image(*v), report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
yukiplt_c_error yukiplt_c_rules_notify_loaded_cached(const yukiplt_c_maps_cache* cache,
                                                     yukiplt_c_rules* h, const yukiplt_c_image* v,
                                                     const char* path,
                                                     yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v) return converted(Error::Make(ErrorCode::kNullPointer));
  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.notify_loaded_cached(lease->value, image(*v), path, report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
yukiplt_c_error yukiplt_c_rules_notify_unloading_cached(const yukiplt_c_maps_cache* cache,
                                                        yukiplt_c_rules* h,
                                                        const yukiplt_c_image* v,
                                                        yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  if (!v) return converted(Error::Make(ErrorCode::kNullPointer));
  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.notify_unloading_cached(lease->value, image(*v), report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
yukiplt_c_error yukiplt_c_rules_remove_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h,
                                              uint64_t id, yukiplt_c_rule_report* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    auto result = run(h, out, [&](RuleSet& r, RuleOperationReport& report) {
      return r.remove_rule_cached(lease->value, id, report);
    });
    return Error::Sys(static_cast<ErrorCode>(result.code), result.sys_errno);
  });
}
}
