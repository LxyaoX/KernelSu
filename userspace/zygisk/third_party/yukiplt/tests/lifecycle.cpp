// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <gtest/gtest.h>
#include <link.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "maps_cache.hpp"

static long dispatcher_test_syscall(long number, unsigned* address, int operation, int value,
                                    void* timeout, void* second, int third);
#define syscall dispatcher_test_syscall
#include "consumer/fork_dispatcher.h"
#undef syscall
#include "elf_builder.hpp"
#include "memory.hpp"
#include "overrides.hpp"
#include "registry.hpp"
#include "system_images.hpp"
#include "yukiplt.h"
#include "yukiplt.hpp"

static std::atomic<bool>* dispatcher_waiting = nullptr;
static long dispatcher_test_syscall(long number, unsigned* address, int operation, int value,
                                    void* timeout, void* second, int third) {
  if (dispatcher_waiting && operation == FUTEX_WAIT_PRIVATE) dispatcher_waiting->store(true);
  return syscall(number, address, operation, value, timeout, second, third);
}

namespace contracts_checks {
using namespace yukiplt;

namespace {

struct ContractFixture : testing::Test {
  test::ElfBuilder::BuiltImage image;
  uintptr_t first_page = 0;
  uintptr_t end_page = 0;

  bool writable = false;

  bool hide_image = false;
  int fail_raise = 0;
  int fail_restore = 0;
  size_t protect_calls = 0;
  size_t maps_calls = 0;
  size_t cas_calls = 0;

  size_t race_before_cas = 0;
  uintptr_t race_value = 0;
  void* outputs[2]{};

  void* output_at_first_cas = nullptr;
  detail::PlatformOps ops{
      [](void* p, uintptr_t, size_t, int prot) {
        auto& self = *static_cast<ContractFixture*>(p);
        ++self.protect_calls;
        const bool raise = (prot & detail::kProtWrite) != 0;
        if (raise && self.fail_raise != 0) return self.fail_raise;
        if (!raise && self.fail_restore != 0) return self.fail_restore;
        self.writable = raise;
        return 0;
      },
      [](void* p, detail::MapsCoverage& coverage) {
        auto& self = *static_cast<ContractFixture*>(p);
        ++self.maps_calls;
        char line[128];
        if (!self.hide_image) {
          std::snprintf(line, sizeof(line), "%zx-%zx %s 00000000 00:00 0",
                        static_cast<size_t>(self.first_page), static_cast<size_t>(self.end_page),
                        self.writable ? "rw-p" : "r--p");
          coverage.feed_line(line, std::strlen(line));
        }
        return coverage.finish();
      },
      [](void*, uintptr_t address) {
        return __atomic_load_n(reinterpret_cast<uintptr_t*>(address), __ATOMIC_ACQUIRE);
      },
      [](void* p, uintptr_t address, uintptr_t* expected, uintptr_t desired) {
        auto& self = *static_cast<ContractFixture*>(p);
        if (self.cas_calls == 0)
          self.output_at_first_cas = __atomic_load_n(&self.outputs[0], __ATOMIC_ACQUIRE);
        if (++self.cas_calls == self.race_before_cas)
          __atomic_store_n(reinterpret_cast<uintptr_t*>(self.image.got_slot_addr(1)),
                           self.race_value, __ATOMIC_RELEASE);
        return __atomic_compare_exchange_n(reinterpret_cast<uintptr_t*>(address), expected, desired,
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
      },
      this};

  void SetUp() override {
    test::ElfBuilder builder;
    builder.add_symbol("alpha", 1).add_symbol("beta", 2).add_jump_slot(1).add_jump_slot(2);
    image = builder.build();
    const size_t ps = detail::page_size();
    ASSERT_NE(ps, 0u);
    first_page = image.load_bias() & ~(ps - 1);
    end_page = (image.load_bias() + image.buffer.size() + ps - 1) & ~(ps - 1);
    set_slot(0, 0x1000);
    set_slot(1, 0x2000);
    detail::set_active_ops(&ops);
  }
  void TearDown() override {
    detail::set_active_ops(nullptr);
    detail::set_system_iterator(nullptr);
  }

  ImageView view(uint64_t token = 0x51) const {
    return {image.load_bias(), image.phdr_table(), image.phdr_count, token};
  }
  uintptr_t slot(size_t i) const {
    return __atomic_load_n(reinterpret_cast<uintptr_t*>(image.got_slot_addr(i)), __ATOMIC_ACQUIRE);
  }
  void set_slot(size_t i, uintptr_t value) {
    __atomic_store_n(reinterpret_cast<uintptr_t*>(image.got_slot_addr(i)), value, __ATOMIC_RELEASE);
  }
  void reset_counts() { protect_calls = maps_calls = cas_calls = 0; }
};

void* const kReplA = reinterpret_cast<void*>(uintptr_t{0xA000});
void* const kReplB = reinterpret_cast<void*>(uintptr_t{0xB000});

}  // namespace

TEST_F(ContractFixture, RefreshedOriginalProtectionStillRequiresConfirmedRecovery) {
  char text[160];
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  MapsCache cache;
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  HookRequest request{"alpha", kReplA, &outputs[0]};
  fail_restore = EPERM;
  auto result = hook_batch_cached(cache, view(), &request, 1);
  ASSERT_EQ(result.error.code, ErrorCode::kIndeterminate);
  set_slot(0, 0x1000);
  writable = false;
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  reset_counts();
  EXPECT_EQ(unhook_cached(cache, result.handle).code, ErrorCode::kIndeterminate);
  EXPECT_TRUE(result.handle.valid());
  EXPECT_GT(protect_calls, 0u);
  fail_restore = 0;
  EXPECT_TRUE(unhook_cached(cache, result.handle).ok());
  EXPECT_FALSE(result.handle.valid());
  EXPECT_FALSE(writable);
  EXPECT_EQ(cas_calls, 0u);
  EXPECT_EQ(maps_calls, 0u);
}

TEST_F(ContractFixture, CachedOwnersConflictPerPageAndInvalidationBlocksReads) {
  char text[160];
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  MapsCache first, second;
  ASSERT_TRUE(second.import_text(text, std::strlen(text)).ok());
  HookRequest alpha{"alpha", kReplA, &outputs[0]}, beta{"beta", kReplB, &outputs[1]};
  fail_restore = EPERM;
  auto live = hook(view(), "alpha", kReplA, nullptr);
  ASSERT_EQ(live.error.code, ErrorCode::kIndeterminate);
  ASSERT_TRUE(writable);
  std::snprintf(text, sizeof(text), "%zx-%zx rw-p 00000000 00:00 0\n", first_page, end_page);
  ASSERT_TRUE(first.import_text(text, std::strlen(text)).ok());
  ASSERT_TRUE(second.import_text(text, std::strlen(text)).ok());
  fail_restore = 0;
  ASSERT_TRUE(unhook(live.handle).ok());
  ASSERT_FALSE(writable);
  reset_counts();
  EXPECT_EQ(hook_batch_cached(first, view(), &alpha, 1).error.code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(hook_batch_cached(second, view(), &alpha, 1).error.code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(cas_calls, 0u);
  EXPECT_EQ(maps_calls, 0u);
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  ASSERT_TRUE(first.import_text(text, std::strlen(text)).ok());
  ASSERT_TRUE(second.import_text(text, std::strlen(text)).ok());
  auto a = hook_batch_cached(first, view(), &alpha, 1);
  ASSERT_TRUE(a.ok());
  auto b = hook_batch_cached(second, view(), &beta, 1);
  EXPECT_FALSE(b.ok());
  EXPECT_FALSE(b.handle.valid());
  b = hook_batch_cached(first, view(), &beta, 1);
  ASSERT_TRUE(b.ok());
  SlotReport report;
  size_t count = 0;
  EXPECT_FALSE(query_cached(second, a.handle, &report, 1, &count).ok());
  EXPECT_TRUE(query_cached(first, a.handle, &report, 1, &count).ok());
  ASSERT_TRUE(first.invalidate().ok());
  EXPECT_FALSE(query_cached(first, a.handle, &report, 1, &count).ok());
  EXPECT_FALSE(unhook_cached(first, a.handle).ok());
  EXPECT_TRUE(unhook(a.handle).ok());
  EXPECT_TRUE(unhook(b.handle).ok());
}

TEST(MapsCache, RejectsMalformedAndOversizedInput) {
  MapsCache cache;
  EXPECT_TRUE(cache.capture().ok());
  EXPECT_EQ(cache.import_text("", 0).code, ErrorCode::kVmaUnavailable);
  const char overlap[] = "1000-3000 r--p 0 00:00 0\n2000-4000 rw-p 0 00:00 0\n";
  EXPECT_EQ(cache.import_text(overlap, sizeof(overlap) - 1).code, ErrorCode::kVmaUnavailable);
  EXPECT_EQ(cache.import_text("x", 64u * 1024u * 1024u + 1).code, ErrorCode::kOutOfBounds);
  for (const char* damaged :
       {"1000-2000 r--p", "1000-2000 r--p garbage", "1000-2000 r--p 0 00:zz 0",
        "1000-2000 r--p 0 00:00 -1", "1000-2000 r--p 0 00:00 18446744073709551616",
        "00000000000001000-2000 r--p 0 00:00 0"}) {
    EXPECT_EQ(cache.import_text(damaged, std::strlen(damaged)).code, ErrorCode::kVmaUnavailable);
  }
}

TEST_F(ContractFixture, SystemCachedQueryRechecksOwnershipAfterLockHandoff) {
  char text[160];
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  MapsCache cache, other;
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  ASSERT_TRUE(other.import_text(text, std::strlen(text)).ok());
  struct Context {
    ContractFixture* fixture;
    MapsCache* other_cache;
    HookResult other_hook;
    bool interleave = false;
  } context{this, &other, {}};
  static Context* current = nullptr;
  current = &context;
  detail::set_system_iterator([](int (*callback)(dl_phdr_info*, size_t, void*), void* data) {
    auto& fixture = *current->fixture;
    if (current->interleave) {
      current->interleave = false;
      HookRequest request{"beta", kReplB, &fixture.outputs[1]};
      current->other_hook = hook_batch_cached(*current->other_cache, fixture.view(), &request, 1);
    }
    dl_phdr_info info{};
    info.dlpi_addr = fixture.view().load_bias;
    info.dlpi_phdr = fixture.view().phdr_table;
    info.dlpi_phnum = static_cast<ElfW(Half)>(fixture.view().phdr_count);
    info.dlpi_name = "fixture.so";
    return callback(&info, sizeof(info), data);
  });
  auto installed = hook(view(0), "alpha", kReplA, &outputs[0]);
  ASSERT_TRUE(installed.ok());
  context.interleave = true;
  SlotReport report;
  size_t count = 0;
  EXPECT_EQ(query_cached(cache, installed.handle, &report, 1, &count).code,
            ErrorCode::kInvalidArgument);
  ASSERT_TRUE(context.other_hook.ok());
  EXPECT_TRUE(unhook_cached(other, context.other_hook.handle).ok());
  EXPECT_TRUE(unhook(installed.handle).ok());
  current = nullptr;
}

TEST_F(ContractFixture, OldForgottenHandleCannotInvalidateNewCacheBinding) {
  char text[160];
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  MapsCache cache;
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  HookRequest first{"alpha", kReplA, &outputs[0]};
  fail_restore = EPERM;
  auto old = hook_batch_cached(cache, view(101), &first, 1);
  ASSERT_EQ(old.error.code, ErrorCode::kIndeterminate);
  ASSERT_TRUE(forget_image(view(101)).ok());
  fail_restore = 0;
  writable = false;
  set_slot(0, 0x1000);
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  HookRequest next{"alpha", kReplB, &outputs[1]};
  auto current = hook_batch_cached(cache, view(102), &next, 1);
  ASSERT_TRUE(current.ok());
  EXPECT_EQ(unhook_cached(cache, old.handle).code, ErrorCode::kImageInvalidated);
  EXPECT_FALSE(old.handle.valid());
  EXPECT_TRUE(unhook_cached(cache, current.handle).ok());
  EXPECT_EQ(slot(0), 0x1000u);
  EXPECT_EQ(maps_calls, 0u);
}

TEST_F(ContractFixture, CachedRestoreDoesNotRebindAfterConcurrentForget) {
  char text[160];
  std::snprintf(text, sizeof(text), "%zx-%zx r--p 00000000 00:00 0\n", first_page, end_page);
  MapsCache cache;
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  auto installed = hook(view(101), "alpha", kReplA, nullptr);
  ASSERT_TRUE(installed.ok());
  static ImageView forgotten;
  forgotten = view(101);
  auto* mutex = detail::op_mutex().native_handle();
  detail::set_mutex_unlock_callback(mutex, true, [] {
    detail::set_mutex_unlock_callback(nullptr, true, nullptr);
    EXPECT_TRUE(forget_image(forgotten).ok());
  });
  reset_counts();
  auto result = unhook_cached(cache, installed.handle);
  detail::set_mutex_unlock_callback(nullptr, true, nullptr);
  EXPECT_EQ(result.code, ErrorCode::kImageInvalidated);
  EXPECT_FALSE(installed.handle.valid());
  EXPECT_FALSE(cache.has_bindings());
  EXPECT_EQ(protect_calls, 0u);
  EXPECT_EQ(cas_calls, 0u);
  set_slot(0, 0x1000);
  HookRequest request{"alpha", kReplB, nullptr};
  auto next = hook_batch_cached(cache, view(102), &request, 1);
  EXPECT_EQ(next.error.code, ErrorCode::kInvalidArgument);
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  auto refreshed = hook_batch_cached(cache, view(102), &request, 1);
  ASSERT_TRUE(refreshed.ok());
  EXPECT_TRUE(unhook_cached(cache, refreshed.handle).ok());
}

TEST_F(ContractFixture, SuccessPublishesBeforeTheReplacementIsReachable) {
  output_at_first_cas = reinterpret_cast<void*>(uintptr_t{1});
  auto result = hook_import(view(), {"alpha"}, kReplA, &outputs[0]);
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(output_at_first_cas, reinterpret_cast<void*>(uintptr_t{0x1000}));
  EXPECT_EQ(slot(0), reinterpret_cast<uintptr_t>(kReplA));
  EXPECT_TRUE(unhook(result.handle).ok());
  EXPECT_EQ(slot(0), 0x1000u);
}

TEST_F(ContractFixture, CustomImageMissingFromMapsKeepsHandleUntilForgotten) {
  auto result = hook_import(view(0x61), {"alpha"}, kReplA, &outputs[0]);
  ASSERT_TRUE(result.ok());
  hide_image = true;
  EXPECT_EQ(unhook(result.handle).code, ErrorCode::kOutOfBounds);
  ASSERT_TRUE(result.handle.valid());
  EXPECT_EQ(slot(0), reinterpret_cast<uintptr_t>(kReplA));

  hide_image = false;
  EXPECT_EQ(hook_import(view(0x61), {"alpha"}, kReplB, nullptr).error.code,
            ErrorCode::kAlreadyHooked);

  EXPECT_TRUE(forget_image(view(0x62)).ok());
  hide_image = true;
  EXPECT_EQ(unhook(result.handle).code, ErrorCode::kOutOfBounds);
  ASSERT_TRUE(result.handle.valid());

  EXPECT_TRUE(forget_image(view(0x61)).ok());
  EXPECT_EQ(unhook(result.handle).code, ErrorCode::kImageInvalidated);
  EXPECT_FALSE(result.handle.valid());
  EXPECT_EQ(slot(0), reinterpret_cast<uintptr_t>(kReplA));
}

TEST_F(ContractFixture, ResidueAlreadyRestoredByThirdPartyNeedsNoProtectCall) {
  fail_restore = EPERM;
  auto result = hook_import(view(0x81), {"alpha"}, kReplA, &outputs[0]);
  ASSERT_EQ(result.error.code, ErrorCode::kIndeterminate);
  ASSERT_TRUE(result.handle.valid());
  ASSERT_TRUE(writable);

  writable = false;
  set_slot(0, 0x1000);
  fail_restore = 0;
  reset_counts();
  EXPECT_TRUE(unhook(result.handle).ok());
  EXPECT_EQ(protect_calls, 0u);
  EXPECT_FALSE(result.handle.valid());
}

TEST_F(ContractFixture, CSessionOwnsPlanMetadataAndRefusesToLoseLiveRecovery) {
  yukiplt_c_image ci{image.load_bias(), image.phdr_table(), image.phdr_count, 0x901};
  yukiplt_c_hook_target target{
      sizeof(target), ci, {YUKIPLT_C_SELECTOR("alpha"), kReplA, &outputs[0]}};
  yukiplt_c_plan plan{};
  size_t failed = 0;
  ASSERT_EQ(yukiplt_c_build_plan(&target, 1, &plan, &failed).code, 0u);
  EXPECT_EQ(failed, SIZE_MAX);
  yukiplt_c_session session{};
  ASSERT_EQ(yukiplt_c_prepare_session(&plan, &session).code, 0u);
  EXPECT_EQ(yukiplt_c_plan_destroy(&plan).code, 0u);
  yukiplt_c_summary summary{};
  ASSERT_EQ(yukiplt_c_session_install(&session, &summary).code, 0u);
  EXPECT_EQ(summary.succeeded, 1u);
  EXPECT_EQ(summary.retained, 1u);
  const auto token = session.token;
  EXPECT_EQ(yukiplt_c_session_destroy(&session).code, YUKIPLT_C_INVALID_ARGUMENT);
  EXPECT_EQ(session.token, token);
  size_t bytes = 0;
  ASSERT_EQ(yukiplt_c_session_target(&session, 0, nullptr, nullptr, 0, &bytes).code, 0u);
  std::vector<char> text(bytes);
  yukiplt_c_target_record result{};
  ASSERT_EQ(yukiplt_c_session_target(&session, 0, &result, text.data(), bytes, &bytes).code, 0u);
  EXPECT_STREQ(text.data() + result.selected.symbol.offset, "alpha");
  EXPECT_EQ(result.original, reinterpret_cast<void*>(uintptr_t{0x1000}));
  set_slot(0, 0x7777);
  EXPECT_EQ(yukiplt_c_session_unhook(&session, &summary).code, YUKIPLT_C_SLOT_MODIFIED_EXTERNALLY);
  EXPECT_EQ(summary.retained, 1u);
  EXPECT_EQ(yukiplt_c_session_destroy(&session).code, YUKIPLT_C_INVALID_ARGUMENT);
  set_slot(0, reinterpret_cast<uintptr_t>(kReplA));
  EXPECT_EQ(yukiplt_c_session_unhook(&session, &summary).code, 0u);
  EXPECT_EQ(summary.retained, 0u);
  EXPECT_EQ(slot(0), 0x1000u);
  EXPECT_EQ(yukiplt_c_session_destroy(&session).code, 0u);
}

}  // namespace contracts_checks

namespace concurrency_checks {
using namespace yukiplt;
using namespace yukiplt::detail;

namespace {

ImageIdentity img(uintptr_t bias) {
  ImageIdentity id;
  id.load_bias = bias;
  id.phdr_addr = bias + 0x40;
  id.phdr_count = 4;
  return id;
}

SlotRecord slot(uintptr_t a) {
  SlotRecord s;
  s.address = a;
  return s;
}

}  // namespace

TEST(Concurrency, ContendedClaimOneOwner) {
  Registry& reg = Registry::instance();
  size_t baseline;
  {
    std::lock_guard<std::mutex> lock(op_mutex());
    baseline = reg.live_count();
  }

  constexpr int kThreads = 8;
  constexpr int kRounds = 500;
  constexpr uintptr_t kSlot = 0x7f00;
  std::atomic<int> concurrent_owners{0};
  std::atomic<int> max_owners{0};
  std::atomic<int> bad{0};
  std::vector<std::thread> ts;

  for (int i = 0; i < kThreads; ++i) {
    ts.emplace_back([&] {
      for (int r = 0; r < kRounds; ++r) {
        uint64_t id;
        {
          std::lock_guard<std::mutex> lock(op_mutex());
          SlotRecord s[] = {slot(kSlot)};
          id = reg.claim(img(0x1000), s, 1);
        }
        if (id == Registry::kInvalidId) continue;

        int now = concurrent_owners.fetch_add(1) + 1;
        int prev_max = max_owners.load();
        while (now > prev_max && !max_owners.compare_exchange_weak(prev_max, now)) {
        }

        concurrent_owners.fetch_sub(1);

        std::lock_guard<std::mutex> lock(op_mutex());
        if (!reg.release(id).ok()) bad.fetch_add(1);
      }
    });
  }
  for (auto& t : ts) t.join();

  EXPECT_EQ(bad.load(), 0);
  EXPECT_LE(max_owners.load(), 1) << "two threads owned the same slot at once";
  std::lock_guard<std::mutex> lock(op_mutex());
  EXPECT_EQ(reg.live_count(), baseline);
}

}  // namespace concurrency_checks

namespace registry_checks {
using namespace yukiplt;
using namespace yukiplt::detail;

namespace {

ImageIdentity sys_image(uintptr_t bias = 0x1000) {
  ImageIdentity id;
  id.load_bias = bias;
  id.phdr_addr = bias + 0x40;
  id.phdr_count = 8;
  id.instance_token = 0;
  return id;
}

SlotRecord slot(uintptr_t addr, uintptr_t orig = 0, uintptr_t repl = 0) {
  SlotRecord s;
  s.address = addr;
  s.original = orig;
  s.replacement = repl;
  return s;
}

}  // namespace

TEST(Registry, GenerationRejectsStaleHandle) {
  Registry& reg = Registry::instance();
  SlotRecord s[] = {slot(0x3000)};
  uint64_t a = reg.claim(sys_image(), s, 1);
  ASSERT_NE(a, Registry::kInvalidId);
  reg.mark_installed(a);
  EXPECT_TRUE(reg.is_installed(a));
  reg.drop(a);

  uint64_t b = reg.claim(sys_image(), s, 1);
  ASSERT_NE(b, Registry::kInvalidId);
  EXPECT_NE(a, b);
  EXPECT_FALSE(reg.is_installed(a));
  ImageIdentity img;
  SlotRecord out[1];
  size_t n = 0;
  EXPECT_EQ(reg.snapshot(a, img, out, 1, &n).code, ErrorCode::kInvalidArgument);
  reg.drop(b);
}

TEST(Registry, SharedPageIndexSurvivesGrowthDeletionAndRecovery) {
  MapsCache cache;
  const char* text = test::synthetic_maps_line(false);
  ASSERT_TRUE(cache.import_text(text, std::strlen(text)).ok());
  auto state = MapsCacheAccess::get(cache);
  std::lock_guard<std::mutex> lock(op_mutex());
  auto& reg = Registry::instance();
  ASSERT_TRUE(reg.trim_idle_storage().ok());
  const size_t ps = page_size();
  std::vector<uint64_t> ids;
  std::vector<uintptr_t> pages;
  for (size_t i = 0; i < 100; ++i) {
    pages.push_back((i + 16) * (i + 16) * ps);
    SlotRecord s = slot(pages.back() + 8);
    auto id = reg.claim(sys_image(), &s, 1);
    ASSERT_NE(id, Registry::kInvalidId);
    ASSERT_TRUE(reg.bind_cache(id, state).ok());
    reg.mark_installed(id);
    ids.push_back(id);
  }
  const uintptr_t page = pages[50];
  SlotRecord second = slot(page + 16);
  auto shared = reg.claim(sys_image(), &second, 1);
  ASSERT_NE(shared, Registry::kInvalidId);
  ASSERT_TRUE(reg.bind_cache(shared, state).ok());
  reg.mark_installed(shared);
  PageResidueRecord residue{page, kProtRead, EPERM};
  SlotRecord first = slot(page + 8);
  reg.mark_indeterminate(ids[50], &first, 1, &residue, 1);
  reg.mark_indeterminate(shared, &second, 1, &residue, 1);
  ASSERT_TRUE(reg.page_pending(page));
  for (size_t i = 0; i < ids.size(); i += 2)
    if (i != 50) reg.drop(ids[i]);
  ASSERT_NE(reg.cached_page(page), nullptr);
  EXPECT_TRUE(reg.page_pending(page));
  reg.finish_restore(ids[50], false, GroupState::kInstalled, &first, 1, &residue, 0);
  EXPECT_TRUE(reg.page_pending(page));
  reg.drop(ids[50]);
  reg.finish_restore(shared, false, GroupState::kInstalled, &second, 1, &residue, 0);
  EXPECT_FALSE(reg.page_pending(page));
  reg.drop(shared);
  EXPECT_EQ(reg.cached_page(page), nullptr);
  for (size_t i = 1; i < ids.size(); i += 2) {
    EXPECT_NE(reg.cached_page(pages[i]), nullptr);
    reg.drop(ids[i]);
  }
  EXPECT_FALSE(reg.cache_bound(state.get()));
  EXPECT_EQ(reg.live_count(), 0u);
  EXPECT_TRUE(reg.trim_idle_storage().ok());
}

}  // namespace registry_checks

namespace rules_checks {
using namespace yukiplt;
using test::ElfBuilder;

namespace {

struct FakeImage {
  ElfBuilder::BuiltImage built;
  std::string path;
  uint64_t token = 0;

  ImageView view() const {
    return {built.load_bias(), built.phdr_table(), built.phdr_count, token};
  }
  uintptr_t slot() const { return built.got_slot_addr(0); }
};

struct RulesFixture : testing::Test {
  std::vector<FakeImage> images;

  uintptr_t first_page = 0;
  uintptr_t end_page = 0;
  bool writable = false;
  size_t enumeration_calls = 0;
  size_t maps_calls = 0;
  size_t protect_calls = 0;

  static RulesFixture* current;

  detail::PlatformOps ops{
      [](void* p, uintptr_t, size_t, int prot) {
        auto& self = *static_cast<RulesFixture*>(p);
        ++self.protect_calls;
        self.writable = (prot & detail::kProtWrite) != 0;
        return 0;
      },
      [](void* p, detail::MapsCoverage& coverage) {
        auto& self = *static_cast<RulesFixture*>(p);
        ++self.maps_calls;
        const size_t ps = detail::page_size();

        std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
        for (const auto& image : self.images) {
          uintptr_t begin = image.built.load_bias() & ~(ps - 1);
          uintptr_t end =
              (image.built.load_bias() + image.built.buffer.size() + ps - 1) & ~(ps - 1);
          ranges.emplace_back(begin, end);
        }
        std::sort(ranges.begin(), ranges.end());

        for (size_t i = 0; i < ranges.size(); ++i) {
          uintptr_t begin = ranges[i].first;
          uintptr_t end = ranges[i].second;
          while (i + 1 < ranges.size() && ranges[i + 1].first <= end) {
            end = std::max(end, ranges[i + 1].second);
            ++i;
          }
          char line[128];
          std::snprintf(line, sizeof(line), "%zx-%zx %s 00000000 00:00 0",
                        static_cast<size_t>(begin), static_cast<size_t>(end),
                        self.writable ? "rw-p" : "r--p");
          coverage.feed_line(line, std::strlen(line));
        }
        return coverage.finish();
      },
      [](void*, uintptr_t address) {
        return __atomic_load_n(reinterpret_cast<uintptr_t*>(address), __ATOMIC_ACQUIRE);
      },
      [](void*, uintptr_t address, uintptr_t* expected, uintptr_t desired) {
        return __atomic_compare_exchange_n(reinterpret_cast<uintptr_t*>(address), expected, desired,
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
      },
      this};

  void SetUp() override {
    current = this;
    detail::set_active_ops(&ops);
    detail::set_active_image_enumerator(&RulesFixture::enumerate);
  }
  void TearDown() override {
    detail::set_active_image_enumerator(nullptr);
    detail::set_active_ops(nullptr);
    current = nullptr;
  }

  static uint64_t next_token() {
    static uint64_t counter = 0x1000;
    return ++counter;
  }

  FakeImage& add_image(const char* path, uintptr_t slot_value) {
    ElfBuilder b;
    b.add_symbol("target", 1).set_symbol_undefined(1).add_jump_slot(1);
    FakeImage image;
    image.built = b.build();
    image.path = path;
    image.token = next_token();
    __atomic_store_n(reinterpret_cast<uintptr_t*>(image.built.got_slot_addr(0)), slot_value,
                     __ATOMIC_RELEASE);
    images.push_back(std::move(image));
    return images.back();
  }

  static Result<std::vector<NamedImage>> enumerate(const ImageSelector& selector) {
    ++current->enumeration_calls;
    if (selector.name && current->failed_filter == selector.name)
      return Error::Make(ErrorCode::kOutOfMemory);
    if (current->enumeration_error != ErrorCode::kSuccess)
      return Error::Make(current->enumeration_error);
    std::vector<NamedImage> out;
    for (const auto& image : current->images) {
      bool match = selector.match == ImageMatch::kMainExecutable ||
                   (selector.match == ImageMatch::kBasename && selector.name != nullptr &&
                    image.path == selector.name);
      if (match) out.push_back({image.view(), image.path});
    }
    if (out.empty()) return Error::Make(ErrorCode::kImageNotFound);
    return out;
  }

  uintptr_t read(uintptr_t addr) const {
    return __atomic_load_n(reinterpret_cast<uintptr_t*>(addr), __ATOMIC_ACQUIRE);
  }
  void write(uintptr_t addr, uintptr_t value) {
    __atomic_store_n(reinterpret_cast<uintptr_t*>(addr), value, __ATOMIC_RELEASE);
  }

  ErrorCode enumeration_error = ErrorCode::kSuccess;
  std::string failed_filter;
};

RulesFixture* RulesFixture::current = nullptr;

ImageSelector match_basename(const char* name) { return {ImageMatch::kBasename, name, 0}; }

void* const kRepl = reinterpret_cast<void*>(uintptr_t{0xA000});

FunctionImportSelector target_selector() {
  FunctionImportSelector s;
  s.base.symbol = "target";
  s.sources = kSourceJmprel;
  s.types = kTypeJumpSlot;
  return s;
}

}  // namespace

TEST_F(RulesFixture, NotifyUnloadingFailureKeepsHandleAndDoesNotForget) {
  auto& image = add_image("libkeep.so", 0x1000);
  RuleSet rules;
  void* originals[1]{};
  ASSERT_TRUE(rules.add_rule(match_basename("libkeep.so"), target_selector(), kRepl, originals, 1));
  ASSERT_EQ(rules.notify_loaded(image.view(), "libkeep.so").succeeded, 1u);

  write(image.slot(), 0xC000);
  auto failed = rules.notify_unloading(image.view());
  EXPECT_EQ(failed.failed, 1u);
  EXPECT_EQ(failed.retained, 1u);
  EXPECT_EQ(rules.bindings()[0].state, RuleState::kRestoreFailed);

  EXPECT_EQ(
      hook_import(image.view(), {"target"}, reinterpret_cast<void*>(uintptr_t{0xB000}), nullptr)
          .error.code,
      ErrorCode::kAlreadyHooked);

  ASSERT_TRUE(forget_image(image.view()).ok());
  auto after_forget = rules.notify_unloading(image.view());
  EXPECT_EQ(after_forget.failed, 0u);
  EXPECT_EQ(rules.bindings()[0].state, RuleState::kInvalidated);
  EXPECT_EQ(read(image.slot()), 0xC000u);
  write(image.slot(), 0x1000);
}

}  // namespace rules_checks

namespace contracts_checks {

TEST_F(ContractFixture, DiagnosedRestoreKeepsCoverageCauseAndRecoveryHandle) {
  HookRequest request{"alpha", kReplA, &outputs[0]};
  OperationDiagnostic diagnostic;
  auto installed = hook_batch_diagnosed(view(), &request, 1, nullptr, &diagnostic);
  ASSERT_TRUE(installed.ok());
  EXPECT_EQ(diagnostic.cause, ErrorCause::kNone);
  hide_image = true;
  EXPECT_EQ(unhook_diagnosed(installed.handle, nullptr, 0, &diagnostic).code,
            ErrorCode::kOutOfBounds);
  EXPECT_EQ(diagnostic.cause, ErrorCause::kMapsCoverage);
  EXPECT_TRUE(installed.handle.valid());
  hide_image = false;
  EXPECT_TRUE(unhook_diagnosed(installed.handle, nullptr, 0, &diagnostic).ok());
  EXPECT_EQ(diagnostic.cause, ErrorCause::kNone);
  EXPECT_TRUE(diagnostic.handle_consumed);
}

TEST(Diagnostics, ConcurrentPreparationFailuresStayIndependent) {
  std::atomic<bool> start{false};
  auto check = [&](bool alias) {
    void* original = nullptr;
    HookRequest requests[] = {{"one", reinterpret_cast<void*>(1), &original},
                              {"two", reinterpret_cast<void*>(2), &original}};
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 200; ++i) {
      OperationDiagnostic diagnostic;
      auto result = hook_batch_diagnosed({}, requests, alias ? 2 : 0, nullptr, &diagnostic);
      EXPECT_EQ(result.error.code, ErrorCode::kInvalidArgument);
      EXPECT_EQ(diagnostic.cause, alias ? ErrorCause::kOutputAlias : ErrorCause::kInvalidRequest);
      EXPECT_FALSE(result.handle.valid());
    }
  };
  std::thread first(check, true), second(check, false);
  start.store(true, std::memory_order_release);
  first.join();
  second.join();
}

TEST(Diagnostics, CandidateCallbackCannotLeakItsNestedFailureCause) {
  test::ElfBuilder builder;
  builder.add_symbol("alpha", 1).set_symbol_undefined(1).add_jump_slot(1);
  auto image = builder.build();
  NamedImage named{{image.load_bias(), image.phdr_table(), image.phdr_count, 0x765}, "custom"};
  CandidateSelection selection;
  selection.sources = kSourceJmprel;
  selection.types = kTypeJumpSlot;
  selection.predicate = [](const FunctionCandidate&, void*) -> Result<bool> {
    void* original = nullptr;
    HookRequest requests[] = {{"one", reinterpret_cast<void*>(1), &original},
                              {"two", reinterpret_cast<void*>(2), &original}};
    OperationDiagnostic nested;
    auto result = hook_batch_diagnosed({}, requests, 2, nullptr, &nested);
    EXPECT_EQ(result.error.code, ErrorCode::kInvalidArgument);
    EXPECT_EQ(nested.cause, ErrorCause::kOutputAlias);
    return Error::Make(ErrorCode::kInvalidArgument);
  };
  OperationDiagnostic diagnostic;
  auto result = expand_function_candidates_diagnosed(&named, 1, selection, &diagnostic);
  ASSERT_FALSE(result);
  EXPECT_EQ(diagnostic.cause, ErrorCause::kInvalidRequest);
  selection.predicate = nullptr;
  selection.limits.text_bytes = 0;
  result = expand_function_candidates_diagnosed(&named, 1, selection, &diagnostic);
  ASSERT_FALSE(result);
  EXPECT_EQ(diagnostic.cause, ErrorCause::kScanBudget);
}

}  // namespace contracts_checks

namespace {
using namespace yukiplt;
ForkConfig native_config(unsigned timeout = 5000) {
  return {sizeof(ForkConfig), YUKIPLT_C_FORK_NATIVE, timeout, nullptr, nullptr, nullptr};
}
int fork_phase(unsigned expected, unsigned why = 0) {
  ForkStatus status{};
  status.struct_size = sizeof(status);
  return fork_status(status) || status.phase != expected || status.reason != why;
}
void wait_for_fork_drain() {
  ForkStatus status{};
  status.struct_size = sizeof(status);
  for (;;) {
    ASSERT_TRUE(fork_status(status).ok());
    if (status.phase == YUKIPLT_C_FORK_DRAINING) return;
    std::this_thread::yield();
  }
}
void wait_child(pid_t child) {
  ASSERT_GT(child, 0);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST(MultithreadFork, DrainsOuterLeaseAndPreservesNestedOperation) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  std::atomic<bool> entered{false};
  std::atomic<int> result{0};
  std::thread worker([&] {
    detail::OperationGuard outer;
    entered.store(true);
    wait_for_fork_drain();
    detail::OperationGuard nested;
    if (nested.error()) result.store(1);
  });
  while (!entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) {
    yukiplt_c_maps_cache cache{};
    int failed = fork_phase(YUKIPLT_C_FORK_READY) || yukiplt_c_maps_cache_create(&cache).code ||
                 yukiplt_c_maps_cache_destroy(&cache).code;
    _exit(failed);
  }
  worker.join();
  wait_child(child);
  EXPECT_EQ(result.load(), 0);
  EXPECT_TRUE(disable_atfork().ok());
}
TEST(MultithreadFork, TimeoutRefusesChildAndParentContinues) {
  ASSERT_TRUE(enable_atfork(native_config(10)).ok());
  std::atomic<bool> entered{false}, release{false};
  std::thread worker([&] {
    detail::OperationGuard operation;
    entered.store(true);
    while (!release.load()) std::this_thread::yield();
  });
  while (!entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) {
    if (fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_TIMEOUT)) _exit(1);
    if (enable_atfork(native_config()).code != ErrorCode::kForkChildUnavailable) _exit(2);
    pid_t again = fork();
    if (!again) _exit(fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_TIMEOUT));
    int status = 0;
    if (again < 0 || waitpid(again, &status, 0) != again || status) _exit(3);
    _exit(fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_TIMEOUT));
  }
  release.store(true);
  worker.join();
  wait_child(child);
  EXPECT_TRUE(disable_atfork().ok());
}
TEST(MultithreadFork, ReentryRefusesChildWithoutWaitingForItself) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  {
    detail::OperationGuard outer;
    pid_t child = fork();
    if (!child) _exit(fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_REENTRY));
    wait_child(child);
  }
  EXPECT_TRUE(disable_atfork().ok());
}
TEST(MultithreadFork, BackendSwitchAndUnsubscribeUseOneCycle) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  ASSERT_TRUE(disable_atfork().ok());
  ASSERT_EQ(dispatcher_init(), 0);
  auto config = dispatcher_config();
  ASSERT_TRUE(enable_atfork(config).ok());
  ASSERT_TRUE(enable_atfork(config).ok());
  EXPECT_EQ(prepare_fork().code, ErrorCode::kInvalidArgument);
  ForkStatus before{};
  before.struct_size = sizeof(before);
  ASSERT_TRUE(fork_status(before).ok());
  pid_t child = fork();
  if (!child) {
    ForkStatus after{};
    after.struct_size = sizeof(after);
    if (fork_status(after) || after.generation != before.generation + 1) _exit(1);
    if (disable_atfork()) _exit(2);
    _exit(0);
  }
  wait_child(child);
  EXPECT_TRUE(disable_atfork().ok());
  for (const auto& slot : dispatcher_slots) EXPECT_EQ(slot.token, 0u);
}
TEST(MultithreadFork, RefusalPreservesSessionAndCallerReports) {
  ASSERT_EQ(dispatcher_init(), 0);
  ASSERT_TRUE(enable_atfork(dispatcher_config()).ok());
  dispatcher_prepare();
  HookSession session;
  Error error;
  auto summary = session.install(error);
  EXPECT_EQ(error.code, ErrorCode::kForkInProgress);
  EXPECT_EQ(summary.attempted, 0u);
  EXPECT_TRUE(session.images().empty());
  yukiplt_c_maps_cache cache{123};
  EXPECT_EQ(yukiplt_c_maps_cache_create(&cache).code, YUKIPLT_C_FORK_IN_PROGRESS);
  EXPECT_EQ(cache.token, 123u);
  dispatcher_parent();
  EXPECT_TRUE(disable_atfork().ok());
}
TEST(MultithreadFork, ChildCanDetachSubscriptionPublishedDuringEnable) {
  ASSERT_EQ(dispatcher_init(), 0);
  struct Context {
    std::atomic<bool> published{false}, release{false};
  } context;
  auto config = dispatcher_config();
  config.context = &context;
  config.subscribe = [](void* value, const yukiplt_c_fork_callbacks* callbacks,
                        uint64_t* token) -> int {
    int error = dispatcher_subscribe(nullptr, callbacks, token);
    if (error) return error;
    auto& state = *static_cast<Context*>(value);
    state.published.store(true);
    while (!state.release.load()) std::this_thread::yield();
    return 0;
  };
  Error enabled;
  std::thread worker([&] { enabled = enable_atfork(config); });
  while (!context.published.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) {
    if (fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_PROTOCOL)) _exit(1);
    yukiplt_c_maps_cache cache{};
    if (yukiplt_c_maps_cache_create(&cache).code != YUKIPLT_C_FORK_CHILD_UNAVAILABLE) _exit(2);
    if (disable_atfork()) _exit(3);
    for (const auto& slot : dispatcher_slots)
      if (slot.token) _exit(4);
    if (enable_atfork(config).code != ErrorCode::kForkChildUnavailable) _exit(5);
    _exit(0);
  }
  context.release.store(true);
  worker.join();
  wait_child(child);
  EXPECT_TRUE(enabled.ok());
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace

namespace contracts_checks {
TEST_F(ContractFixture, ForkRejectsInheritedRecoveryAndKeepsParentHandle) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  HookRequest request{"alpha", kReplA, &outputs[0]};
  fail_restore = EPERM;
  auto installed = hook_batch(view(), &request, 1);
  ASSERT_EQ(installed.error.code, ErrorCode::kIndeterminate);
  pid_t child = fork();
  if (!child) {
    if (fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_RECOVERY)) _exit(1);
    OperationDiagnostic diagnostic;
    if (unhook_diagnosed(installed.handle, nullptr, 0, &diagnostic).code !=
        ErrorCode::kForkChildUnavailable)
      _exit(2);
    if (diagnostic.cause != ErrorCause::kForkProtocol || !installed.handle.valid()) _exit(3);
    if (disable_atfork() || after_fork_child().ok()) _exit(4);
    _exit(0);
  }
  wait_child(child);
  fail_restore = 0;
  EXPECT_TRUE(unhook(installed.handle).ok());
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace contracts_checks
namespace {
TEST(MultithreadFork, DispatcherFailurePreservesSubscription) {
  ASSERT_EQ(dispatcher_init(), 0);
  auto config = dispatcher_config();
  config.subscribe = [](void*, const yukiplt_c_fork_callbacks*, uint64_t*) { return ENOMEM; };
  EXPECT_EQ(enable_atfork(config).sys_errno, ENOMEM);
  config.subscribe = [](void*, const yukiplt_c_fork_callbacks*, uint64_t*) -> int {
    throw std::bad_alloc();
  };
  EXPECT_EQ(enable_atfork(config).code, ErrorCode::kOutOfMemory);
  config = dispatcher_config();
  bool fail = true;
  config.context = &fail;
  config.unsubscribe_and_drain = [](void* context, uint64_t token) -> int {
    if (*static_cast<bool*>(context)) throw std::bad_alloc();
    return dispatcher_unsubscribe(nullptr, token);
  };
  ASSERT_TRUE(enable_atfork(config).ok());
  EXPECT_EQ(disable_atfork().code, ErrorCode::kOutOfMemory);
  ForkStatus status{};
  status.struct_size = sizeof(status);
  ASSERT_TRUE(fork_status(status).ok());
  EXPECT_EQ(status.mode, YUKIPLT_C_FORK_HOST_DISPATCHER);
  fail = false;
  EXPECT_TRUE(disable_atfork().ok());
}
TEST(MultithreadFork, DispatcherOrderingAndStaleToken) {
  ASSERT_EQ(dispatcher_init(), 0);
  unsigned value = 0;
  yukiplt_c_fork_callbacks cb{
      [](void* p) { *static_cast<unsigned*>(p) = *static_cast<unsigned*>(p) * 10 + 1; },
      [](void*) {}, [](void*) {}, &value};
  uint64_t old = 0, retained = 0, fresh = 0;
  ASSERT_EQ(dispatcher_subscribe(nullptr, &cb, &old), 0);
  ASSERT_EQ(dispatcher_subscribe(nullptr, &cb, &retained), 0);
  ASSERT_EQ(dispatcher_unsubscribe(nullptr, old), 0);
  cb.prepare = [](void* p) { *static_cast<unsigned*>(p) = *static_cast<unsigned*>(p) * 10 + 2; };
  ASSERT_EQ(dispatcher_subscribe(nullptr, &cb, &fresh), 0);
  EXPECT_NE(old, fresh);
  EXPECT_EQ(dispatcher_unsubscribe(nullptr, old), EINVAL);
  dispatcher_prepare();
  EXPECT_EQ(value, 21u);
  dispatcher_parent();
  EXPECT_EQ(dispatcher_unsubscribe(nullptr, retained), 0);
  EXPECT_EQ(dispatcher_unsubscribe(nullptr, fresh), 0);
}
}  // namespace

namespace {
std::atomic<bool> lookup_entered{false}, lookup_release{false};
TEST(MultithreadFork, ChildDropsVanishedTokenReaderBeforeUnsubscribe) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  detail::set_thread_lookup_callback([] {
    lookup_entered.store(true);
    while (!lookup_release.load()) std::this_thread::yield();
  });
  std::thread worker([] { detail::OperationGuard operation; });
  while (!lookup_entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) _exit(disable_atfork().ok() ? 0 : 1);
  lookup_release.store(true);
  worker.join();
  wait_child(child);
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace

namespace {
TEST(MultithreadFork, FailedForkCompletesParentAndPreservesErrno) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  pid_t child = fork();
  if (!child) {
    sock_filter filters[] = {BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
                             BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone, 0, 1),
                             BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EAGAIN),
#ifdef SYS_clone3
                             BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone3, 0, 1),
                             BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EAGAIN),
#endif
                             BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    sock_fprog filter{static_cast<unsigned short>(sizeof(filters) / sizeof(filters[0])), filters};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter))
      _exit(1);
    errno = 0;
    if (fork() != -1 || errno != EAGAIN) _exit(2);
    if (fork_phase(YUKIPLT_C_FORK_READY) || disable_atfork()) _exit(3);
    _exit(0);
  }
  wait_child(child);
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace

namespace {
std::atomic<bool> checkpoint_entered{false};
void await_fork_drain() {
  checkpoint_entered.store(true);
  wait_for_fork_drain();
}
TEST(MultithreadFork, DrainsCStoreBeforePublishingObjectToken) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  checkpoint_entered.store(false);
  yukiplt_c_maps_cache cache{};
  yukiplt_c_error result{};
  std::thread worker([&] {
    // Creation's only mutex is the Store lock. Pause before its release.
    detail::set_mutex_unlock_callback(nullptr, false, await_fork_drain);
    result = yukiplt_c_maps_cache_create(&cache);
  });
  while (!checkpoint_entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) _exit(!cache.token || yukiplt_c_maps_cache_destroy(&cache).code);
  worker.join();
  wait_child(child);
  EXPECT_EQ(result.code, YUKIPLT_C_SUCCESS);
  EXPECT_NE(cache.token, 0u);
  EXPECT_EQ(yukiplt_c_maps_cache_destroy(&cache).code, YUKIPLT_C_SUCCESS);
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace

namespace contracts_checks {
TEST_F(ContractFixture, ForkDrainsCRecoveryDiagnosticWriteback) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  auto image_view = view();
  yukiplt_c_image image{image_view.load_bias, image_view.phdr_table, image_view.phdr_count,
                        image_view.instance_token};
  yukiplt_c_request request{YUKIPLT_C_SELECTOR("alpha"), kReplA, nullptr};
  yukiplt_c_handle handle{};
  ASSERT_EQ(yukiplt_c_hook_batch(&image, &request, 1, &handle, nullptr).code, 0u);
  yukiplt_c_recovery_diagnostic diagnostic{};
  diagnostic.stage = UINT32_MAX;
  yukiplt_c_error result{};
  checkpoint_entered.store(false);
  std::thread worker([&] {
    detail::set_mutex_unlock_callback(detail::op_mutex().native_handle(), true, await_fork_drain);
    result = yukiplt_c_query_recovery(&handle, &diagnostic, nullptr, 0);
  });
  while (!checkpoint_entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) {
    if (diagnostic.stage == UINT32_MAX || diagnostic.error.code) _exit(1);
    _exit(yukiplt_c_unhook(&handle, nullptr, 0).code);
  }
  worker.join();
  wait_child(child);
  EXPECT_EQ(result.code, 0u);
  EXPECT_NE(diagnostic.stage, UINT32_MAX);
  EXPECT_EQ(yukiplt_c_unhook(&handle, nullptr, 0).code, 0u);
  EXPECT_TRUE(disable_atfork().ok());
}

TEST_F(ContractFixture, ForkDrainsHandleDiagnostics) {
  static std::atomic<bool> release{false};
  for (bool timeout : {true, false}) {
    ASSERT_TRUE(enable_atfork(native_config(timeout ? 20 : 5000)).ok());
    for (bool overwrite : {false, true}) {
      HookRequest request{"alpha", kReplA, &outputs[0]};
      auto installed = hook_batch(view(), &request, 1);
      ASSERT_TRUE(installed.ok());
      const uint64_t id = detail_api::id(installed.handle);
      checkpoint_entered.store(false);
      release.store(false);
      void (*checkpoint)() = await_fork_drain;
      if (timeout) {
        checkpoint = [] {
          checkpoint_entered.store(true);
          while (!release.load()) std::this_thread::yield();
        };
      }
      std::thread worker([&] {
        auto handle = std::move(installed.handle);
        detail::set_mutex_unlock_callback(detail::op_mutex().native_handle(), false, checkpoint);
        if (overwrite) handle = HookHandle{};
      });
      while (!checkpoint_entered.load()) std::this_thread::yield();
      pid_t child = fork();
      if (!child) {
        if (timeout) {
          yukiplt_c_maps_cache cache{};
          _exit(fork_phase(YUKIPLT_C_FORK_REJECTED, YUKIPLT_C_FORK_REASON_TIMEOUT) ||
                yukiplt_c_maps_cache_create(&cache).code != YUKIPLT_C_FORK_CHILD_UNAVAILABLE);
        }
        if (fork_phase(YUKIPLT_C_FORK_READY)) _exit(1);
        auto inherited = detail_api::make(id);
        _exit(unhook(inherited).ok() && slot(0) == 0x1000 ? 0 : 2);
      }
      release.store(true);
      worker.join();
      wait_child(child);
      // Reclaim the deliberately dropped handle so both processes clean up.
      auto retained = detail_api::make(id);
      EXPECT_TRUE(unhook(retained).ok());
    }
    EXPECT_TRUE(disable_atfork().ok());
  }
}

TEST_F(ContractFixture, ForkDrainsTransactionAndSessionReportPublication) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  // Check both a held op mutex and the interval after commit unlocks it.
  for (bool after_unlock : {false, true}) {
    HookTarget target{view(), {"alpha"}, kReplA, &outputs[0]};
    auto plan = build_hook_plan(&target, 1);
    ASSERT_TRUE(plan);
    auto prepared = prepare_session(plan.value());
    ASSERT_TRUE(prepared);
    auto& session = prepared.value();
    checkpoint_entered.store(false);
    SessionSummary result;
    std::thread worker([&] {
      detail::set_mutex_unlock_callback(detail::op_mutex().native_handle(), after_unlock,
                                        await_fork_drain);
      result = session.install();
    });
    while (!checkpoint_entered.load()) std::this_thread::yield();
    EXPECT_EQ(slot(0), reinterpret_cast<uintptr_t>(kReplA));
    EXPECT_EQ(session.images()[0].state, SessionState::kPrepared);
    pid_t child = fork();
    if (!child) {
      if (session.images()[0].state != SessionState::kInstalled ||
          session.targets()[0].install_report.status != SlotStatus::kInstalled)
        _exit(1);
      _exit(session.unhook().ok() && slot(0) == 0x1000 ? 0 : 2);
    }
    worker.join();
    wait_child(child);
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(session.unhook().ok());
  }
  EXPECT_TRUE(disable_atfork().ok());
}

TEST_F(ContractFixture, ChildRecoversItsOwnPermissionFailureIndependently) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  pid_t child = fork();
  if (!child) {
    HookRequest request{"alpha", kReplA, &outputs[0]};
    fail_restore = EPERM;
    auto installed = hook_batch(view(), &request, 1);
    if (installed.error.code != ErrorCode::kIndeterminate || !installed.handle.valid()) _exit(1);
    fail_restore = 0;
    if (unhook(installed.handle) || slot(0) != 0x1000) _exit(2);
    _exit(disable_atfork().ok() ? 0 : 3);
  }
  wait_child(child);
  EXPECT_EQ(slot(0), 0x1000u);
  EXPECT_EQ(outputs[0], nullptr);
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace contracts_checks

namespace rules_checks {
TEST_F(RulesFixture, ForkDrainsRuleEnumerationAndBindingPublication) {
  ASSERT_TRUE(enable_atfork(native_config()).ok());
  add_image("libfork.so", 0x1000);
  RuleSet rules;
  void* originals[1]{};
  ASSERT_TRUE(rules.add_rule(match_basename("libfork.so"), target_selector(), kRepl, originals, 1));
  checkpoint_entered.store(false);
  detail::set_active_image_enumerator([](const ImageSelector& selector) {
    await_fork_drain();
    return RulesFixture::enumerate(selector);
  });
  RuleSummary result;
  std::thread worker([&] { result = rules.refresh(); });
  while (!checkpoint_entered.load()) std::this_thread::yield();
  pid_t child = fork();
  if (!child) {
    if (rules.bindings().size() != 1 || rules.bindings()[0].state != RuleState::kInstalled)
      _exit(1);
    _exit(rules.unhook().ok() ? 0 : 2);
  }
  worker.join();
  detail::set_active_image_enumerator(&RulesFixture::enumerate);
  wait_child(child);
  EXPECT_EQ(result.succeeded, 1u);
  EXPECT_TRUE(rules.unhook().ok());
  EXPECT_TRUE(disable_atfork().ok());
}
}  // namespace rules_checks

namespace {
TEST(MultithreadFork, DispatcherUnsubscribeWaitsForSelectedCycle) {
  ASSERT_EQ(dispatcher_init(), 0);
  struct Context {
    std::atomic<bool> entered{false}, waiting{false}, completed{false}, removed{false};
  } context;
  dispatcher_waiting = &context.waiting;
  yukiplt_c_fork_callbacks callbacks{
      [](void* value) {
        auto& state = *static_cast<Context*>(value);
        state.entered.store(true);
        while (!state.waiting.load()) std::this_thread::yield();
      },
      [](void* value) { static_cast<Context*>(value)->completed.store(true); },
      [](void* value) { static_cast<Context*>(value)->completed.store(true); }, &context};
  uint64_t token = 0;
  ASSERT_EQ(dispatcher_subscribe(nullptr, &callbacks, &token), 0);
  int removal = -1;
  std::thread worker([&] {
    while (!context.entered.load()) std::this_thread::yield();
    removal = dispatcher_unsubscribe(nullptr, token);
    context.removed.store(context.completed.load());
  });
  pid_t child = fork();
  if (!child) _exit(!context.completed.load() || dispatcher_unsubscribe(nullptr, token));
  worker.join();
  dispatcher_waiting = nullptr;
  wait_child(child);
  EXPECT_EQ(removal, 0);
  EXPECT_TRUE(context.removed.load());
  EXPECT_EQ(dispatcher_unsubscribe(nullptr, token), EINVAL);
}
}  // namespace
