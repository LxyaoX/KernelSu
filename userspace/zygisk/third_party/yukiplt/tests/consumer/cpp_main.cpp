// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <dlfcn.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <thread>

#include "../cpp_abi_layout_checks.hpp"
#include "fork_dispatcher.h"
#include "identity.h"
#include "yukiplt.hpp"

namespace {
using Function = int (*)(int);
yukiplt::OriginalSlot<Function> parent_original;
yukiplt::OriginalSlot<Function> child_original;
int parent_replacement(int x) { return parent_original.load()(x) + 100; }
int child_replacement(int x) { return child_original.load()(x) + 200; }
bool deny_open() {
  // Deny every new file open in this disposable child, including maps fallbacks.
  sock_filter instructions[] = {BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#ifdef __NR_open
                                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_open, 0, 1),
                                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#endif
                                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 1),
                                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#ifdef __NR_openat2
                                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat2, 0, 1),
                                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
#endif
                                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
  sock_fprog program{static_cast<unsigned short>(sizeof(instructions) / sizeof(instructions[0])),
                     instructions};
  return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
         prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}
}  // namespace

#if defined(YUKIPLT_TEST_UNLOAD)
static bool unloaded_host() {
  if (dispatcher_init()) return false;
  for (unsigned backend = 1; backend <= 2; ++backend) {
    void* host = dlopen("./libfork_host.so", RTLD_NOW | RTLD_LOCAL);
    if (!host) return false;
    auto enable =
        reinterpret_cast<int (*)(const yukiplt_c_fork_config*)>(dlsym(host, "host_enable"));
    auto disable = reinterpret_cast<int (*)()>(dlsym(host, "host_disable"));
    auto config = dispatcher_config();
    if (backend == 1)
      config = {sizeof(config), YUKIPLT_C_FORK_NATIVE, 5000, nullptr, nullptr, nullptr};
    if (!enable || !disable || enable(&config)) return false;
    pid_t child = fork();
    if (!child) _exit(disable());
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child || status) return false;
    if (disable() || dlclose(host)) return false;
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(enable), &info)) return false;
    child = fork();
    if (!child) _exit(0);
    if (child < 0 || waitpid(child, &status, 0) != child || status) return false;
  }
  return true;
}
#endif
int main() {
  using namespace yukiplt;
  if (test_identity()) return 40;
#if defined(YUKIPLT_TEST_UNLOAD)
  if (!unloaded_host()) return 61;
#endif
  void* library = dlopen("./libhook_target.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) return 41;
  auto call = reinterpret_cast<Function>(dlsym(library, "call_import"));
  auto image = image_from_soname("libhook_target.so");
  if (!call || !image || call(1) != 11) return 42;
  HookRequest request{"sample_add", reinterpret_cast<void*>(parent_replacement),
                      parent_original.output_address()};
  auto installed = hook_batch(image.value(), &request, 1);
  if (!installed.ok() || call(1) != 111) return 43;
  MapsCache cache;
  if (cache.capture() || dispatcher_init()) return 44;
  ForkConfig native{sizeof(ForkConfig), YUKIPLT_C_FORK_NATIVE, 5000, nullptr, nullptr, nullptr};
  if (enable_atfork(native) || disable_atfork() || enable_atfork(dispatcher_config())) return 44;
  std::atomic<bool> ready{false}, stop{false};
  std::thread worker([&] {
    ready.store(true);
    while (!stop.load()) std::this_thread::yield();
  });
  while (!ready.load()) std::this_thread::yield();
  pid_t child = fork();
  if (child == 0) {
    ForkStatus state{};
    state.struct_size = sizeof(state);
    if (fork_status(state) || state.phase != YUKIPLT_C_FORK_READY) _exit(45);
    auto snapshot = inspect_imports(image.value());
    if (!snapshot) _exit(58);
    HookSession session;
    Error admitted;
    if (!session.install(admitted).ok() || admitted) _exit(59);
    RuleSet rules;
    if (!rules.refresh(admitted).ok() || admitted) _exit(60);
    if (!deny_open()) _exit(46);
    if (unhook_cached(cache, installed.handle) || call(1) != 11) _exit(47);
    HookRequest next{"sample_add", reinterpret_cast<void*>(child_replacement),
                     child_original.output_address()};
    auto result = hook_batch_cached(cache, image.value(), &next, 1);
    if (!result.ok() || call(1) != 211) _exit(48);
    SlotReport report;
    size_t count = 0;
    if (query_cached(cache, result.handle, &report, 1, &count) || count != 1) _exit(49);
    if (unhook_cached(cache, result.handle) || call(1) != 11) _exit(50);
    auto live = hook_batch(image.value(), &next, 1);
    if (live.error.code != ErrorCode::kVmaUnavailable) _exit(51);
    _exit(0);
  }
  stop.store(true);
  worker.join();
  if (disable_atfork()) return 52;
  if (child < 0) return 53;
  int status = 0;
  if (waitpid(child, &status, 0) != child) return 54;
  if (call(1) != 111 || unhook_cached(cache, installed.handle) || call(1) != 11) return 55;
  if (!WIFEXITED(status) || WEXITSTATUS(status))
    return WIFEXITED(status) ? WEXITSTATUS(status) : 56;
  return dlclose(library) == 0 ? 0 : 57;
}
