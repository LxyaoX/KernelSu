// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

#include "aps2_encoder.hpp"
#include "elf_builder.hpp"
#include "imports.hpp"
#include "memory.hpp"
#include "overrides.hpp"
#include "registry.hpp"
#include "system_images.hpp"
#include "versioned_elf.hpp"
#include "yukiplt.h"
#include "yukiplt.hpp"

namespace {
thread_local int fail_after = -1;
}

void* operator new(std::size_t size) {
  if (fail_after >= 0 && fail_after-- == 0) throw std::bad_alloc();
  if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
  return ::operator new(size, tag);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  ::operator delete(pointer);
}

namespace {
using namespace yukiplt;

size_t live_groups() {
  std::lock_guard<std::mutex> lock(detail::op_mutex());
  return detail::Registry::instance().live_count();
}

struct AllocationFixture {
  static constexpr size_t count = 9;
  std::string names[count];
  void* originals[count]{};
  HookRequest legacy[count];
  SelectedHookRequest selected[count];
  test::ElfBuilder::BuiltImage image;
  char maps[128]{};
  size_t writes = 0;
  detail::PlatformOps ops{
      [](void*, uintptr_t, size_t, int) { return 0; },
      [](void* ctx, detail::MapsCoverage& coverage) {
        auto& self = *static_cast<AllocationFixture*>(ctx);
        coverage.feed_line(self.maps, std::strlen(self.maps));
        return coverage.finish();
      },
      [](void*, uintptr_t address) {
        return __atomic_load_n(reinterpret_cast<uintptr_t*>(address), __ATOMIC_ACQUIRE);
      },
      [](void* ctx, uintptr_t address, uintptr_t* expected, uintptr_t desired) {
        ++static_cast<AllocationFixture*>(ctx)->writes;
        return __atomic_compare_exchange_n(reinterpret_cast<uintptr_t*>(address), expected, desired,
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
      },
      this};

  AllocationFixture() {
    test::ElfBuilder builder;
    for (size_t i = 0; i < count; ++i) {
      names[i] = "allocation_failure_import_" + std::to_string(i);
      builder.add_symbol(names[i].c_str(), static_cast<uint32_t>(i + 1))
          .add_jump_slot(static_cast<uint32_t>(i + 1));
      auto* replacement = reinterpret_cast<void*>(uintptr_t{0x12345678});
      legacy[i] = {names[i].c_str(), replacement, &originals[i]};
      selected[i] = {{names[i].c_str()}, replacement, &originals[i]};
    }
    image = builder.build();
    const auto ps = detail::page_size();
    const auto start = image.load_bias() & ~(ps - 1);
    const auto end = (image.load_bias() + image.buffer.size() + ps - 1) & ~(ps - 1);
    std::snprintf(maps, sizeof(maps), "%zx-%zx rw-p 00000000 00:00 0", start, end);
    detail::set_active_ops(&ops);
  }
  ~AllocationFixture() { detail::set_active_ops(nullptr); }
  ImageView view() const { return {image.load_bias(), image.phdr_table(), image.phdr_count, 41}; }
  HookResult install(bool explicit_selector) {
    return explicit_selector ? hook_imports(view(), selected, count)
                             : hook_batch(view(), legacy, count);
  }
};
}  // namespace

TEST(ImportAllocation, FailedInstallNeverStrandsOwnershipOrPublishesOriginals) {
  for (bool selected : {true, false}) {
    AllocationFixture fixture;
    const auto before = fixture.image.buffer;
    const size_t baseline = live_groups();
    int failures = 0;
    bool completed = false;
    for (int allocation = 0; allocation < 256; ++allocation) {
      fail_after = allocation;
      auto result = fixture.install(selected);
      fail_after = -1;
      if (result.ok()) {
        ASSERT_FALSE(yukiplt::unhook(result.handle));
        completed = true;
        break;
      }
      ASSERT_EQ(result.error.code, yukiplt::ErrorCode::kOutOfMemory);
      EXPECT_FALSE(result.handle.valid());
      EXPECT_EQ(fixture.image.buffer, before);
      EXPECT_EQ(fixture.writes, 0u);
      for (void* original : fixture.originals) EXPECT_EQ(original, nullptr);
      ASSERT_EQ(live_groups(), baseline) << "allocation " << allocation;
      ++failures;
    }
    EXPECT_TRUE(completed);
    EXPECT_GE(failures, 3);
    EXPECT_EQ(live_groups(), baseline);
    auto retry = fixture.install(selected);
    ASSERT_TRUE(retry.ok());
    EXPECT_FALSE(yukiplt::unhook(retry.handle));
  }
}

namespace {
struct RuleOomFixture {
  bool fail_second_cache = false;
  size_t enumerate_calls = 0;
  test::ElfBuilder::BuiltImage image;
  std::string path = "liboom.so";
  char maps[128]{};
  bool writable = false;
  static RuleOomFixture* current;
  detail::PlatformOps ops{
      [](void* p, uintptr_t, size_t, int prot) {
        static_cast<RuleOomFixture*>(p)->writable = (prot & detail::kProtWrite) != 0;
        return 0;
      },
      [](void* ctx, detail::MapsCoverage& coverage) {
        auto& self = *static_cast<RuleOomFixture*>(ctx);
        const size_t ps = detail::page_size();
        uintptr_t begin = self.image.load_bias() & ~(ps - 1);
        uintptr_t end = (self.image.load_bias() + self.image.buffer.size() + ps - 1) & ~(ps - 1);
        std::snprintf(self.maps, sizeof(self.maps), "%zx-%zx %s 00000000 00:00 0",
                      static_cast<size_t>(begin), static_cast<size_t>(end),
                      self.writable ? "rw-p" : "r--p");
        coverage.feed_line(self.maps, std::strlen(self.maps));
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

  RuleOomFixture() {
    test::ElfBuilder b;
    b.add_symbol("target", 1).set_symbol_undefined(1).add_jump_slot(1);
    image = b.build();
    __atomic_store_n(reinterpret_cast<uintptr_t*>(image.got_slot_addr(0)), uintptr_t{0x1000},
                     __ATOMIC_RELEASE);
    current = this;
    detail::set_active_ops(&ops);
    detail::set_active_image_enumerator(&RuleOomFixture::enumerate);
  }
  ~RuleOomFixture() {
    detail::set_active_image_enumerator(nullptr);
    detail::set_active_ops(nullptr);
    current = nullptr;
  }
  yukiplt::ImageView view() const {
    return {image.load_bias(), image.phdr_table(), image.phdr_count, 0xAB};
  }
  uintptr_t slot() const { return image.got_slot_addr(0); }
  static yukiplt::Result<std::vector<yukiplt::NamedImage>> enumerate(
      const yukiplt::ImageSelector&) {
    try {
      std::vector<yukiplt::NamedImage> out;
      out.push_back({current->view(), current->path});
      if (++current->enumerate_calls == 2 && current->fail_second_cache) fail_after = 0;
      return out;
    } catch (const std::bad_alloc&) {
      return yukiplt::Error::Make(yukiplt::ErrorCode::kOutOfMemory);
    }
  }
};
RuleOomFixture* RuleOomFixture::current = nullptr;
}  // namespace

TEST(ImportAllocation, RefreshCachePreparationFailureNeverPartiallyInstallsEitherOverload) {
  for (bool detailed : {false, true}) {
    RuleOomFixture fixture;
    RuleSet rules;
    void* originals[3]{};
    size_t i = 0;
    for (const char* name : {"a.so", "b.so", "c.so"}) {
      ASSERT_TRUE(rules.add_rule({ImageMatch::kBasename, name}, ImportSelector{"target"},
                                 reinterpret_cast<void*>(uintptr_t{0xa000}), &originals[i++], 1));
    }
    fixture.fail_second_cache = true;
    RuleOperationReport report;
    auto result = detailed ? rules.refresh(report) : rules.refresh();
    fail_after = -1;
    EXPECT_EQ(result.attempted, 0u);
    EXPECT_EQ(result.failed, 1u);
    EXPECT_EQ(result.retained, 0u);
    EXPECT_TRUE(rules.bindings().empty());
    EXPECT_EQ(fixture.enumerate_calls, 2u);
    for (void* original : originals) EXPECT_EQ(original, nullptr);
    EXPECT_EQ(__atomic_load_n(reinterpret_cast<uintptr_t*>(fixture.slot()), __ATOMIC_ACQUIRE),
              0x1000u);
    if (detailed) EXPECT_EQ(report.preparation_error.code, ErrorCode::kOutOfMemory);
  }
}

TEST(ImportAllocation, CPlanAndSessionPreparationPreserveOutputsAndOwnership) {
  AllocationFixture fixture;
  auto v = fixture.view();
  yukiplt_c_hook_target target{sizeof(target),
                               {v.load_bias, v.phdr_table, v.phdr_count, v.instance_token},
                               {YUKIPLT_C_SELECTOR(fixture.names[0].c_str()),
                                reinterpret_cast<void*>(uintptr_t{0xa000}), &fixture.originals[0]}};
  const auto baseline = live_groups();
  bool completed = false;
  size_t failures = 0;
  for (int allocation = 0; allocation < 256; ++allocation) {
    yukiplt_c_plan plan{};
    yukiplt_c_session session{};
    fail_after = allocation;
    auto error = yukiplt_c_build_plan(&target, 1, &plan, nullptr);
    if (!error.code) error = yukiplt_c_prepare_session(&plan, &session);
    fail_after = -1;
    EXPECT_EQ(live_groups(), baseline);
    EXPECT_EQ(fixture.writes, 0u);
    EXPECT_EQ(fixture.originals[0], nullptr);
    if (error.code) {
      EXPECT_EQ(error.code, YUKIPLT_C_OUT_OF_MEMORY);
      EXPECT_EQ(session.token, 0u);
      ++failures;
    } else {
      completed = true;
      EXPECT_NE(session.token, 0u);
    }
    EXPECT_EQ(yukiplt_c_plan_destroy(&plan).code, 0u);
    EXPECT_EQ(yukiplt_c_session_destroy(&session).code, 0u);
    if (completed) break;
  }
  EXPECT_TRUE(completed);
  EXPECT_GT(failures, 4u);
}

TEST(ImportAllocation, CachedInstallAndSnapshotReplacementAreFailureAtomic) {
  AllocationFixture fixture;
  MapsCache cache;
  ASSERT_TRUE(cache.import_text(fixture.maps, std::strlen(fixture.maps)).ok());
  size_t baseline = live_groups();
  bool success = false;
  for (int allocation = 0; allocation < 256; ++allocation) {
    fail_after = allocation;
    auto result = hook_batch_cached(cache, fixture.view(), fixture.legacy, fixture.count);
    fail_after = -1;
    if (result.ok()) {
      ASSERT_TRUE(unhook_cached(cache, result.handle).ok());
      success = true;
      break;
    }
    ASSERT_EQ(result.error.code, ErrorCode::kOutOfMemory);
    EXPECT_FALSE(result.handle.valid());
    EXPECT_EQ(live_groups(), baseline);
    EXPECT_EQ(fixture.writes, 0u);
  }
  EXPECT_TRUE(success);
  fail_after = 0;
  auto error = cache.import_text(fixture.maps, std::strlen(fixture.maps));
  fail_after = -1;
  EXPECT_EQ(error.code, ErrorCode::kOutOfMemory);
  EXPECT_EQ(live_groups(), baseline);
}

TEST(ImportAllocation, PageIndexGrowthFailureDoesNotPublishClaim) {
  using namespace yukiplt::detail;
  auto& reg = Registry::instance();
  const auto ps = page_size();
  std::lock_guard<std::mutex> lock(op_mutex());
  size_t failures = 0;
  bool success = false;
  for (int allocation = 0; allocation < 32; ++allocation) {
    ASSERT_TRUE(reg.trim_idle_storage().ok());
    uint64_t existing[8];
    for (size_t i = 0; i < 8; ++i) {
      SlotRecord slot{(i + 16) * ps + 8};
      existing[i] = reg.claim({}, &slot, 1);
      ASSERT_NE(existing[i], Registry::kInvalidId);
    }
    SlotRecord next{32 * ps + 8};
    uint64_t id = Registry::kInvalidId;
    fail_after = allocation;
    try {
      id = reg.claim({}, &next, 1);
    } catch (const std::bad_alloc&) {
      ++failures;
    }
    fail_after = -1;
    if (id == Registry::kInvalidId) {
      EXPECT_EQ(reg.live_count(), 8u);
      EXPECT_FALSE(reg.owns_slot(next.address));
      EXPECT_FALSE(reg.page_pending(next.address & ~(ps - 1)));
    } else {
      reg.drop(id);
      success = true;
    }
    for (auto previous : existing) reg.drop(previous);
    if (success) break;
  }
  EXPECT_TRUE(success);
  EXPECT_GE(failures, 2u);
  EXPECT_TRUE(reg.trim_idle_storage().ok());
}
