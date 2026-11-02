// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <algorithm>
#include <array>
#include <variant>

#include "c_api.hpp"
#include "yukiplt.hpp"

namespace {
using namespace yukiplt;
using namespace yukiplt::detail::c_objects;
using namespace yukiplt::detail::c_copy;
static_assert(YUKIPLT_C_FEATURE_PLANS == 12 && YUKIPLT_C_FEATURE_SESSIONS == 13);
static_assert(YUKIPLT_C_SESSION_PREPARED == 0 &&
              static_cast<uint32_t>(SessionState::kPrepared) == 0);
static_assert(YUKIPLT_C_SESSION_INSTALLED == 1 &&
              static_cast<uint32_t>(SessionState::kInstalled) == 1);
static_assert(YUKIPLT_C_SESSION_INSTALL_FAILED == 2 &&
              static_cast<uint32_t>(SessionState::kInstallFailed) == 2);
static_assert(YUKIPLT_C_SESSION_INDETERMINATE == 3 &&
              static_cast<uint32_t>(SessionState::kIndeterminate) == 3);
static_assert(YUKIPLT_C_SESSION_RESTORE_FAILED == 4 &&
              static_cast<uint32_t>(SessionState::kRestoreFailed) == 4);
static_assert(YUKIPLT_C_SESSION_RELEASED == 5 &&
              static_cast<uint32_t>(SessionState::kReleased) == 5);
static_assert(YUKIPLT_C_SESSION_INVALIDATED == 6 &&
              static_cast<uint32_t>(SessionState::kInvalidated) == 6);
static_assert(YUKIPLT_C_SESSION_ABANDONED == 7 &&
              static_cast<uint32_t>(SessionState::kAbandoned) == 7);
struct Plan final : Object {
  Plan() : Object(Kind::kPlan) {}
  std::variant<HookPlan, FunctionHookPlan> value;
};
struct Recovery {
  RecoveryDiagnostic diagnostic;
  std::vector<ResidualPage> pages;
};
struct Session final : Object {
  Session() : Object(Kind::kSession) {}
  std::variant<HookSession, FunctionHookSession> value;
  std::vector<Recovery> recovery;
  std::vector<uint8_t> attempted;
  bool can_destroy() const noexcept override {
    const auto ready = [](const auto* session) noexcept {
      if (!session) return false;
      for (const auto& group : session->images())
        if (group.handle.valid()) return false;
      return true;
    };
    return ready(std::get_if<HookSession>(&value)) ||
           ready(std::get_if<FunctionHookSession>(&value));
  }
};
template <typename T, typename H>
std::shared_ptr<T> get(const H* handle, Kind kind) {
  if (!handle) return {};
  return std::static_pointer_cast<T>(acquire(handle->token, kind));
}
template <typename F>
yukiplt_c_error with_session(const yukiplt_c_session* h, F&& f) noexcept {
  return boundary([&]() -> Error {
    if (!h) return Error::Make(ErrorCode::kNullPointer);
    auto value = get<Session>(h, Kind::kSession);
    if (!value) return invalid();
    return f(*value);
  });
}
yukiplt_c_slot_report report(const SlotReport& r) noexcept {
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
template <typename T>
Error target_copy(const T& target, yukiplt_c_target_record* out, char* text, size_t capacity,
                  size_t* required, void* original = nullptr, SlotReport installed = {},
                  SlotReport restored = {}) {
  auto selected = record(target.selected);
  size_t bytes = 0;
  const auto empty = empty_text();
  if (!append_size(bytes, selected.import.symbol) ||
      !append_size(bytes, selected.version ? selected.version->name : empty) ||
      !append_size(bytes, selected.version ? selected.version->dependency : empty))
    return Error::Make(ErrorCode::kIntegerOverflow);
  size_t count = 0;
  bool sizes_only = false;
  if (auto e = buffers(1, bytes, out, out ? 1 : 0, text, capacity, &count, required, sizes_only))
    return e;
  if (sizes_only) return Error::Ok();
  yukiplt_c_target_record result{};
  result.image = image(target.image);
  size_t offset = 0;
  result.selected = copy_import(selected, text, offset);
  result.replacement = target.replacement;
  result.out_original = target.out_original;
  if constexpr (std::is_same_v<T, FunctionPlannedTarget>) {
    result.require_current = target.require_current;
    result.expected_current = target.expected_current;
  }
  result.original = original;
  result.install_report = report(installed);
  result.restore_report = report(restored);
  *out = result;
  return Error::Ok();
}
enum class Action { kInstall, kUnhook, kAbandon };
Error operate(Session& owned, Action action, yukiplt_c_summary* out, MapsCache* cache = nullptr) {
  if (!out) return Error::Make(ErrorCode::kNullPointer);
  return std::visit(
      [&](auto& session) -> Error {
        const auto& groups = session.images();
        for (size_t i = 0; i < groups.size(); ++i) {
          const auto& g = groups[i];
          owned.attempted[i] = action == Action::kInstall
                                   ? (g.state == SessionState::kPrepared ||
                                      g.state == SessionState::kInstallFailed ||
                                      g.state == SessionState::kIndeterminate ||
                                      g.state == SessionState::kRestoreFailed)
                                   : g.handle.valid();
        }
        // All snapshot storage was allocated by prepare. A failed read precedes
        // every abandonment, so the owning object still exposes all live handles.
        if (action == Action::kAbandon) {
          for (size_t i = 0; i < groups.size(); ++i) {
            if (!groups[i].handle.valid()) continue;
            auto& r = owned.recovery[i];
            if (auto e =
                    query_recovery(groups[i].handle, &r.diagnostic, r.pages.data(), r.pages.size()))
              return e;
            if (r.diagnostic.residual_page_count > r.pages.size())
              return Error::Make(ErrorCode::kOutOfBounds);
          }
        }
        const auto result = action == Action::kInstall
                                ? (cache ? session.install_cached(*cache) : session.install())
                            : action == Action::kUnhook
                                ? (cache ? session.unhook_cached(*cache) : session.unhook())
                                : session.abandon();
        *out = {result.attempted, result.succeeded, result.failed, result.retained};
        if (!result.failed) return Error::Ok();
        for (size_t n = 0; n < groups.size(); ++n) {
          const size_t i = action == Action::kUnhook ? groups.size() - n - 1 : n;
          if (!owned.attempted[i]) continue;
          const auto& g = groups[i];
          auto error = action == Action::kInstall && g.state != SessionState::kRestoreFailed
                           ? g.install_error
                           : g.restore_error;
          if (error) return error;
        }
        return Error::Make(ErrorCode::kIndeterminate);
      },
      owned.value);
}
template <typename Input, typename Target, typename Builder>
Error build(const Input* input, size_t count, yukiplt_c_plan* out, size_t* failed,
            Builder&& builder) {
  if (failed) *failed = SIZE_MAX;
  if (!input || !out) return Error::Make(ErrorCode::kNullPointer);
  if (out->token || !count) return invalid();
  std::vector<Target> targets;
  targets.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (failed) *failed = i;
    if (input[i].struct_size != sizeof(Input)) return invalid();
    const auto& r = input[i].request;
    if constexpr (std::is_same_v<Target, HookTarget>) {
      if (!valid(r.target)) return invalid();
      targets.push_back({image(input[i].image), selector(r.target), r.replacement, r.out_original});
    } else {
      if (r.struct_size != sizeof(r) || r.require_current > 1 || !valid(r.base.target))
        return invalid();
      targets.push_back({image(input[i].image),
                         {selector(r.base.target), r.base.replacement, r.base.out_original,
                          r.require_current != 0, r.expected_current}});
    }
  }
  auto result = builder(targets.data(), targets.size(), failed);
  if (!result) return result.error();
  if (failed) *failed = SIZE_MAX;
  auto value = std::make_shared<Plan>();
  value->value = std::move(result.value());
  auto token = publish(value);
  if (!token) return token.error();
  out->token = token.value();
  return Error::Ok();
}
}  // namespace

extern "C" {
yukiplt_c_error yukiplt_c_build_plan(const yukiplt_c_hook_target* input, size_t count,
                                     yukiplt_c_plan* out, size_t* failed) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return build<yukiplt_c_hook_target, HookTarget>(input, count, out, failed, build_hook_plan);
  });
}
yukiplt_c_error yukiplt_c_build_function_plan(const yukiplt_c_function_target* input, size_t count,
                                              yukiplt_c_plan* out, size_t* failed) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&] {
    return build<yukiplt_c_function_target, FunctionHookTarget>(input, count, out, failed,
                                                                build_function_hook_plan);
  });
}
yukiplt_c_error yukiplt_c_plan_destroy(yukiplt_c_plan* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary(
      [&] { return h ? destroy(h->token, Kind::kPlan) : Error::Make(ErrorCode::kNullPointer); });
}
yukiplt_c_error yukiplt_c_plan_size(const yukiplt_c_plan* h, size_t* size) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !size) return Error::Make(ErrorCode::kNullPointer);
    auto p = get<Plan>(h, Kind::kPlan);
    if (!p) return invalid();
    *size = std::visit([](const auto& v) { return v.targets().size(); }, p->value);
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_plan_target(const yukiplt_c_plan* h, size_t index,
                                      yukiplt_c_target_record* out, char* text, size_t capacity,
                                      size_t* required) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h) return Error::Make(ErrorCode::kNullPointer);
    auto p = get<Plan>(h, Kind::kPlan);
    if (!p) return invalid();
    return std::visit(
        [&](const auto& v) -> Error {
          if (index >= v.targets().size()) return Error::Make(ErrorCode::kOutOfBounds);
          return target_copy(v.targets()[index], out, text, capacity, required);
        },
        p->value);
  });
}
yukiplt_c_error yukiplt_c_prepare_session(const yukiplt_c_plan* h,
                                          yukiplt_c_session* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary([&]() -> Error {
    if (!h || !out) return Error::Make(ErrorCode::kNullPointer);
    if (out->token) return invalid();
    auto p = get<Plan>(h, Kind::kPlan);
    if (!p) return invalid();
    auto owned = std::make_shared<Session>();
    auto error = std::visit(
        [&](const auto& plan) -> Error {
          auto result = [&] {
            if constexpr (std::is_same_v<std::decay_t<decltype(plan)>, HookPlan>)
              return prepare_session(plan);
            else
              return prepare_function_session(plan);
          }();
          if (!result) return result.error();
          owned->recovery.resize(result.value().images().size());
          owned->attempted.resize(result.value().images().size());
          for (size_t i = 0; i < owned->recovery.size(); ++i)
            owned->recovery[i].pages.resize(result.value().images()[i].target_indices.size());
          owned->value = std::move(result.value());
          return Error::Ok();
        },
        p->value);
    if (error) return error;
    auto token = publish(owned);
    if (!token) return token.error();
    out->token = token.value();
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_session_destroy(yukiplt_c_session* h) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return boundary(
      [&] { return h ? destroy(h->token, Kind::kSession) : Error::Make(ErrorCode::kNullPointer); });
}
yukiplt_c_error yukiplt_c_session_install(yukiplt_c_session* h, yukiplt_c_summary* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) { return operate(s, Action::kInstall, out); });
}
yukiplt_c_error yukiplt_c_session_unhook(yukiplt_c_session* h, yukiplt_c_summary* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) { return operate(s, Action::kUnhook, out); });
}
yukiplt_c_error yukiplt_c_session_abandon(yukiplt_c_session* h, yukiplt_c_summary* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) { return operate(s, Action::kAbandon, out); });
}
yukiplt_c_error yukiplt_c_session_size(const yukiplt_c_session* h, size_t* targets,
                                       size_t* groups) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) -> Error {
    if (!targets || !groups) return Error::Make(ErrorCode::kNullPointer);
    std::visit(
        [&](const auto& v) {
          *targets = v.targets().size();
          *groups = v.images().size();
        },
        s.value);
    return Error::Ok();
  });
}
yukiplt_c_error yukiplt_c_session_target(const yukiplt_c_session* h, size_t index,
                                         yukiplt_c_target_record* out, char* text, size_t capacity,
                                         size_t* required) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) -> Error {
    return std::visit(
        [&](const auto& v) -> Error {
          if (index >= v.targets().size()) return Error::Make(ErrorCode::kOutOfBounds);
          const auto& t = v.targets()[index];
          return target_copy(t.target, out, text, capacity, required, t.original, t.install_report,
                             t.restore_report);
        },
        s.value);
  });
}
yukiplt_c_error yukiplt_c_session_group_copy(const yukiplt_c_session* h, size_t index,
                                             yukiplt_c_session_group* out, size_t* indices,
                                             size_t capacity, size_t* required) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) -> Error {
    if (!out || !required) return Error::Make(ErrorCode::kNullPointer);
    if (!indices && capacity) return invalid();
    return std::visit(
        [&](const auto& v) -> Error {
          if (index >= v.images().size()) return Error::Make(ErrorCode::kOutOfBounds);
          const auto& g = v.images()[index];
          *required = g.target_indices.size();
          if (indices && capacity < *required) return Error::Make(ErrorCode::kOutOfBounds);
          if (indices) std::copy(g.target_indices.begin(), g.target_indices.end(), indices);
          *out = {image(g.image), static_cast<uint32_t>(g.state), g.handle.valid(),
                  converted(g.install_error), converted(g.restore_error)};
          return Error::Ok();
        },
        s.value);
  });
}
yukiplt_c_error yukiplt_c_session_recovery(const yukiplt_c_session* h, size_t index,
                                           yukiplt_c_recovery_diagnostic* out,
                                           yukiplt_c_residual_page* pages, size_t capacity,
                                           size_t* required) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& s) -> Error {
    if (!out || !required) return Error::Make(ErrorCode::kNullPointer);
    if (!pages && capacity) return invalid();
    return std::visit(
        [&](const auto& v) -> Error {
          if (index >= v.images().size()) return Error::Make(ErrorCode::kOutOfBounds);
          const auto& g = v.images()[index];
          std::array<ResidualPage, 64> live_pages{};
          RecoveryDiagnostic d{};
          const ResidualPage* source = live_pages.data();
          if (g.handle.valid()) {
            if (auto e = query_recovery(g.handle, &d, live_pages.data(), live_pages.size()))
              return e;
            if (d.residual_page_count > live_pages.size())
              return Error::Make(ErrorCode::kOutOfBounds);
          } else if (g.state == SessionState::kAbandoned) {
            d = s.recovery[index].diagnostic;
            source = s.recovery[index].pages.data();
          } else {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, FunctionHookSession>)
              d = g.operation;
            else {
              d.error = g.state == SessionState::kReleased ? g.restore_error
                        : g.state == SessionState::kInvalidated && g.restore_error
                            ? g.restore_error
                            : g.install_error;
              d.image_invalidated = g.state == SessionState::kInvalidated;
            }
            d.residual_page_count = 0;
          }
          *required = d.residual_page_count;
          if (pages && capacity < *required) return Error::Make(ErrorCode::kOutOfBounds);
          if (pages)
            for (size_t i = 0; i < *required; ++i) {
              const auto& p = source[i];
              pages[i] = {p.address, static_cast<uint32_t>(p.original_protection),
                          static_cast<uint32_t>(p.raised_protection), p.sys_errno};
            }
          *out = diagnostic(d);
          return Error::Ok();
        },
        s.value);
  });
}
}  // extern "C"

extern "C" {
yukiplt_c_error yukiplt_c_session_install_cached(const yukiplt_c_maps_cache* cache,
                                                 yukiplt_c_session* h,
                                                 yukiplt_c_summary* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& session) {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    return operate(session, Action::kInstall, out, &lease->value);
  });
}
yukiplt_c_error yukiplt_c_session_unhook_cached(const yukiplt_c_maps_cache* cache,
                                                yukiplt_c_session* h,
                                                yukiplt_c_summary* out) noexcept {
  yukiplt::detail::OperationGuard admission;
  if (admission.error())
    return {static_cast<uint32_t>(admission.error().code), admission.error().sys_errno};

  return with_session(h, [&](Session& session) {
    auto lease = get_cache(cache);
    if (!lease) return invalid();
    return operate(session, Action::kUnhook, out, &lease->value);
  });
}
}
