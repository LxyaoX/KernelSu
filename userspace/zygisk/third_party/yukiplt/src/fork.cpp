// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <atomic>
#include <cerrno>
#include <climits>

#include "common.hpp"
#include "maps_cache.hpp"
#include "registry.hpp"
#if defined(__linux__)
#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#else
#include <thread>
#endif

namespace yukiplt::detail {
namespace {
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<unsigned> owner{0}, phase{0}, reason{0}, mode{0}, generation{0}, active{0};
std::atomic<bool> manual{false};
std::atomic<unsigned> timeout_ms{5000};
std::atomic<bool> configuring{false}, tokens_enabled{false};
std::atomic<unsigned> token_readers{0};
#if defined(__linux__)
pthread_key_t token_key;
#endif
OperationGuard* thread_token() noexcept {
  token_readers.fetch_add(1);
  OperationGuard* token = nullptr;
#if defined(__linux__)
  if (tokens_enabled.load()) token = static_cast<OperationGuard*>(pthread_getspecific(token_key));
#endif
  token_readers.fetch_sub(1);
  return token;
}

std::atomic_flag list_lock = ATOMIC_FLAG_INIT;
std::atomic_flag control_lock = ATOMIC_FLAG_INIT;
OperationGuard* operations = nullptr;
ForkConfig configuration{};
uint64_t subscription = 0;
bool native_registered = false;
unsigned cycle_backend = 0;
bool rejected_before_cycle = false;
unsigned cycle_reason = 0;
unsigned current_pid() noexcept {
#if defined(__linux__)
  return static_cast<unsigned>(getpid());
#else
  return 1;
#endif
}
unsigned current_thread() noexcept {
#if defined(__linux__)
  return static_cast<unsigned>(syscall(SYS_gettid));
#else
  return static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}
uint64_t now_ns() noexcept;
struct ListLock {
  bool held = false;
  explicit ListLock(uint64_t deadline = 0) noexcept {
    while (list_lock.test_and_set(std::memory_order_acquire)) {
      if (deadline) {
        const auto now = now_ns();
        if (!now || now >= deadline) return;
      }
    }
    held = true;
  }
  ~ListLock() {
    if (held) list_lock.clear(std::memory_order_release);
  }
};
bool admitted(unsigned thread) noexcept {
  for (auto* it = operations; it; it = it->next)
    if (it->thread == thread) return true;
  return false;
}
Error process_check() noexcept {
  unsigned empty = 0;
  const unsigned pid = current_pid();
  owner.compare_exchange_strong(empty, pid);
  if (owner.load() != pid || phase.load() == YUKIPLT_C_FORK_REJECTED)
    return Error::Make(ErrorCode::kForkChildUnavailable);
  if (manual.load()) return Error::Make(ErrorCode::kInvalidArgument);
  return Error::Ok();
}
void wake() noexcept {
#if defined(__linux__)
  syscall(SYS_futex, &active, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
#endif
}
uint64_t now_ns() noexcept {
#if defined(__linux__)
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
  return static_cast<uint64_t>(t.tv_sec) * 1000000000 + static_cast<unsigned long>(t.tv_nsec);
#else
  return 1;
#endif
}
void prepare(void* backend) noexcept {
  const int saved = errno;
  const unsigned requested = static_cast<unsigned>(reinterpret_cast<uintptr_t>(backend));
  if (mode.load() != requested) return;
  const uint64_t start = now_ns();
  const uint64_t deadline = start + static_cast<uint64_t>(timeout_ms.load()) * 1000000;
  const unsigned previous = phase.exchange(YUKIPLT_C_FORK_DRAINING);
  cycle_backend = requested;
  cycle_reason = 0;
  rejected_before_cycle = previous == YUKIPLT_C_FORK_REJECTED;
  {
    ListLock lock(deadline);
    if (mode.load() != requested) {
      cycle_backend = 0;
      phase.store(previous);
      errno = saved;
      return;
    }
    if (configuring.load()) {
      cycle_reason = YUKIPLT_C_FORK_REASON_PROTOCOL;
    } else if (!lock.held || !start) {
      cycle_reason = YUKIPLT_C_FORK_REASON_TIMEOUT;
    } else if (rejected_before_cycle) {
      cycle_reason = reason.load();
    } else if (previous == YUKIPLT_C_FORK_DRAINING) {
      cycle_reason = YUKIPLT_C_FORK_REASON_PROTOCOL;
    } else if (admitted(current_thread())) {
      cycle_reason = YUKIPLT_C_FORK_REASON_REENTRY;
    }
  }
#if defined(__linux__)
  while (!cycle_reason) {
    unsigned count = active.load(std::memory_order_acquire);
    if (!count) break;
    const uint64_t now = now_ns();
    if (!now || now >= deadline) {
      cycle_reason = YUKIPLT_C_FORK_REASON_TIMEOUT;
      break;
    }
    const uint64_t remaining = deadline - now;
    timespec timeout{static_cast<time_t>(remaining / 1000000000),
                     static_cast<long>(remaining % 1000000000)};
    syscall(SYS_futex, &active, FUTEX_WAIT_PRIVATE, count, &timeout, nullptr, 0);
  }
#endif
  if (!cycle_reason && !Registry::instance().fork_clean())
    cycle_reason = YUKIPLT_C_FORK_REASON_RECOVERY;
  reason.store(cycle_reason);
  errno = saved;
}
void parent(void* backend) noexcept {
  const int saved = errno;
  if (cycle_backend == reinterpret_cast<uintptr_t>(backend)) {
    // A rejected child stays rejected even if it attempts another fork.
    phase.store(rejected_before_cycle ? YUKIPLT_C_FORK_REJECTED : YUKIPLT_C_FORK_READY);
    cycle_backend = 0;
  }
  errno = saved;
}
void child(void* backend) noexcept {
  const int saved = errno;
  if (cycle_backend == reinterpret_cast<uintptr_t>(backend)) {
    // Only fixed control storage is accessed, including on a failed drain.
    list_lock.clear(std::memory_order_release);
    control_lock.clear(std::memory_order_release);
    configuring.store(false);
    operations = nullptr;
    active.store(0);
    token_readers.store(0);
    owner.store(current_pid());
    unsigned next = generation.load() + 1;
    if (!next) cycle_reason = YUKIPLT_C_FORK_REASON_PROTOCOL;
    generation.store(next);
    reason.store(cycle_reason);
    phase.store(cycle_reason ? YUKIPLT_C_FORK_REJECTED : YUKIPLT_C_FORK_READY);
    cycle_backend = 0;
  }
  errno = saved;
}
#if defined(__linux__)
void native_prepare() noexcept { prepare(reinterpret_cast<void*>(YUKIPLT_C_FORK_NATIVE)); }
void native_parent() noexcept { parent(reinterpret_cast<void*>(YUKIPLT_C_FORK_NATIVE)); }
void native_child() noexcept { child(reinterpret_cast<void*>(YUKIPLT_C_FORK_NATIVE)); }
#endif
struct Control {
  bool held = !control_lock.test_and_set(std::memory_order_acquire);
  ~Control() {
    if (held) control_lock.clear(std::memory_order_release);
  }
};
// Control changes close admission before inspecting the active set. Fast token
// readers have a separate barrier so deletion cannot race pthread_getspecific.
struct ConfigurationWindow {
  bool held = false;
  ConfigurationWindow() noexcept {
    ListLock lock;
    if (active.load() || phase.load() == YUKIPLT_C_FORK_DRAINING) return;
    configuring.store(true);
    held = true;
  }
  ~ConfigurationWindow() {
    if (held) configuring.store(false);
  }
};
Error pause_tokens() noexcept {
  tokens_enabled.store(false);
  const uint64_t deadline = now_ns() + static_cast<uint64_t>(timeout_ms.load()) * 1000000;
  while (token_readers.load()) {
    const auto now = now_ns();
    if (!now || now >= deadline) {
      tokens_enabled.store(true);
      return Error::Make(ErrorCode::kForkInProgress);
    }
  }
  return Error::Ok();
}
}  // namespace
OperationGuard::OperationGuard() noexcept {
  error_ = process_check();
  if (error_) return;
  if (thread_token()) return;
  if (phase.load() == YUKIPLT_C_FORK_DRAINING || configuring.load()) {
    error_ = Error::Make(ErrorCode::kForkInProgress);
    return;
  }
  thread = current_thread();
  ListLock lock;
  if (admitted(thread)) return;
  if (phase.load() == YUKIPLT_C_FORK_DRAINING || configuring.load()) {
    error_ = Error::Make(ErrorCode::kForkInProgress);
    return;
  }
  if (active.load() == UINT_MAX) {
    error_ = Error::Make(ErrorCode::kOutOfBounds);
    return;
  }
#if defined(__linux__)
  if (tokens_enabled.load()) {
    int error = pthread_setspecific(token_key, this);
    if (error) {
      error_ = Error::Sys(ErrorCode::kOutOfMemory, error);
      return;
    }
  }
#endif
  next = operations;
  operations = this;
  owns_ = true;
  active.fetch_add(1, std::memory_order_release);
}
OperationGuard::~OperationGuard() {
  if (!owns_ || owner.load() != current_pid()) return;
  {
    ListLock lock;
    auto** link = &operations;
    while (*link && *link != this) link = &(*link)->next;
    if (!*link) return;
#if defined(__linux__)
    if (tokens_enabled.load()) (void)pthread_setspecific(token_key, nullptr);
#endif
    *link = next;
    active.fetch_sub(1, std::memory_order_release);
  }
  wake();
}
Error process_entry() noexcept {
  if (auto e = process_check()) return e;
  if ((phase.load() == YUKIPLT_C_FORK_DRAINING || configuring.load()) && !thread_token())
    return Error::Make(ErrorCode::kForkInProgress);
  return Error::Ok();
}
}  // namespace yukiplt::detail

namespace yukiplt {
Error enable_atfork(const ForkConfig& input) noexcept {
  using namespace detail;
  Control control;
  if (!control.held) return Error::Make(ErrorCode::kForkInProgress);
  if (auto e = process_entry()) return e;
  if (input.struct_size != sizeof(input) ||
      (input.mode != YUKIPLT_C_FORK_NATIVE && input.mode != YUKIPLT_C_FORK_HOST_DISPATCHER) ||
      (input.mode == YUKIPLT_C_FORK_HOST_DISPATCHER &&
       (!input.subscribe || !input.unsubscribe_and_drain)))
    return Error::Make(ErrorCode::kInvalidArgument);
  ForkConfig next = input;
  if (!next.timeout_ms) next.timeout_ms = 5000;
  if (mode.load()) {
    return configuration.mode == next.mode && configuration.timeout_ms == next.timeout_ms &&
                   configuration.context == next.context &&
                   configuration.subscribe == next.subscribe &&
                   configuration.unsubscribe_and_drain == next.unsubscribe_and_drain
               ? Error::Ok()
               : Error::Make(ErrorCode::kInvalidArgument);
  }
  ConfigurationWindow window;
  if (!window.held) return Error::Make(ErrorCode::kForkInProgress);
  try {
    if (auto e = initialize_c_store()) return e;
    (void)op_mutex();
    auto& registry = Registry::instance();
    if (!page_size() || !registry.salt_ready()) return Error::Make(ErrorCode::kUnsupportedFormat);
    configuration = next;
    timeout_ms.store(next.timeout_ms);
    int error = 0;
#if defined(__linux__)
    error = pthread_key_create(&token_key, nullptr);
    if (error) return Error::Sys(ErrorCode::kOutOfMemory, error);
#endif
    struct RegistrationRollback {
      bool committed = false;
      ~RegistrationRollback() {
        if (committed) return;
        mode.store(0);
#if defined(__linux__)
        (void)pthread_key_delete(token_key);
#endif
      }
    } registration;
    // A selected callback must repair child control state even before enable returns.
    mode.store(next.mode);
    if (next.mode == YUKIPLT_C_FORK_NATIVE) {
#if defined(__linux__)
      if (!native_registered) {
        error = pthread_atfork(native_prepare, native_parent, native_child);
        if (!error) native_registered = true;
      }
#else
      return Error::Make(ErrorCode::kUnsupportedFormat);
#endif
    } else {
      yukiplt_c_fork_callbacks callbacks{prepare, parent, child,
                                         reinterpret_cast<void*>(YUKIPLT_C_FORK_HOST_DISPATCHER)};
      error = next.subscribe(next.context, &callbacks, &subscription);
    }
    if (error) return Error::Sys(ErrorCode::kInvalidArgument, error);
#if defined(__linux__)
    tokens_enabled.store(true);
#endif
    registration.committed = true;
    return Error::Ok();
  } catch (...) {
    return current_exception_error();
  }
}
Error disable_atfork() noexcept {
  using namespace detail;
  Control control;
  if (!control.held || phase.load() == YUKIPLT_C_FORK_DRAINING)
    return Error::Make(ErrorCode::kForkInProgress);
  if (manual.load() || owner.load() != current_pid())
    return Error::Make(ErrorCode::kInvalidArgument);
  if (!mode.load()) return Error::Ok();
  ConfigurationWindow window;
  if (!window.held) return Error::Make(ErrorCode::kForkInProgress);
  if (auto e = pause_tokens()) return e;
  struct ResumeTokens {
    bool removed = false;
    ~ResumeTokens() {
      if (!removed) tokens_enabled.store(true);
    }
  } resume;
  if (mode.load() == YUKIPLT_C_FORK_HOST_DISPATCHER) {
    int error;
    try {
      error = configuration.unsubscribe_and_drain(configuration.context, subscription);
    } catch (...) {
      return current_exception_error();
    }
    if (error) return Error::Sys(ErrorCode::kInvalidArgument, error);
    subscription = 0;
  }
  {
    ListLock lock;
    if (phase.load() == YUKIPLT_C_FORK_DRAINING) return Error::Make(ErrorCode::kForkInProgress);
    mode.store(0);
  }
#if defined(__linux__)
  (void)pthread_key_delete(token_key);
#endif
  resume.removed = true;
  return Error::Ok();
}
Error fork_status(ForkStatus& out) noexcept {
  if (out.struct_size != sizeof(out)) return Error::Make(ErrorCode::kInvalidArgument);
  out.mode = detail::mode.load();
  out.phase = detail::phase.load();
  out.reason = detail::reason.load();
  out.pid = detail::current_pid();
  out.generation = detail::generation.load();
  if (detail::owner.load() && detail::owner.load() != out.pid) {
    out.phase = YUKIPLT_C_FORK_REJECTED;
    out.reason = YUKIPLT_C_FORK_REASON_PROTOCOL;
  }
  return Error::Ok();
}
Error prepare_fork() noexcept {
  using namespace detail;
  if (mode.load()) return Error::Make(ErrorCode::kInvalidArgument);
  if (auto e = process_entry()) return e;
  try {
    if (auto e = initialize_c_store()) return e;
    std::lock_guard<std::mutex> lock(op_mutex());
    auto& registry = Registry::instance();
    if (!page_size() || !registry.salt_ready()) return Error::Make(ErrorCode::kUnsupportedFormat);
    if (!registry.fork_clean()) return Error::Make(ErrorCode::kIndeterminate);
    manual.store(true);
    return Error::Ok();
  } catch (...) {
    return current_exception_error();
  }
}
Error after_fork_parent() noexcept {
  if (detail::mode.load() || !detail::manual.load() ||
      detail::owner.load() != detail::current_pid())
    return Error::Make(ErrorCode::kInvalidArgument);
  detail::manual.store(false);
  return Error::Ok();
}
Error after_fork_child() noexcept {
  if (detail::mode.load() || !detail::manual.load() ||
      detail::owner.load() == detail::current_pid())
    return Error::Make(ErrorCode::kInvalidArgument);
  detail::owner.store(detail::current_pid());
  detail::manual.store(false);
  return Error::Ok();
}
}  // namespace yukiplt
extern "C" {
yukiplt_c_error yukiplt_c_enable_atfork(const yukiplt_c_fork_config* config) noexcept {
  const auto e = config ? yukiplt::enable_atfork(*config)
                        : yukiplt::Error::Make(yukiplt::ErrorCode::kNullPointer);
  return {static_cast<uint32_t>(e.code), e.sys_errno};
}
yukiplt_c_error yukiplt_c_disable_atfork() noexcept {
  const auto e = yukiplt::disable_atfork();
  return {static_cast<uint32_t>(e.code), e.sys_errno};
}
yukiplt_c_error yukiplt_c_get_fork_status(yukiplt_c_fork_status* status) noexcept {
  const auto e = status ? yukiplt::fork_status(*status)
                        : yukiplt::Error::Make(yukiplt::ErrorCode::kNullPointer);
  return {static_cast<uint32_t>(e.code), e.sys_errno};
}
}
