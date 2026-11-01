// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "memory.hpp"

namespace txn_checks {
using namespace yukiplt;
using namespace yukiplt::detail;

namespace {

class FakePlatform {
 public:
  explicit FakePlatform(size_t page_size = 0x1000) : ps_(page_size) {}

  void map_page(uintptr_t base, int prot, bool shared = false) { pages_[base] = {prot, shared}; }
  void set(uintptr_t slot, uintptr_t v) { mem_[slot] = v; }
  uintptr_t get(uintptr_t slot) const {
    auto it = mem_.find(slot);
    return it == mem_.end() ? 0 : it->second;
  }
  int prot(uintptr_t base) const { return pages_.at(base).prot; }

  void fail_protect_at(int n, int err = EACCES) {
    fail_protect_at_ = n;
    fail_errno_ = err;
  }

  void race_before_cas(int n, uintptr_t slot, uintptr_t value) {
    race_at_ = n;
    race_slot_ = slot;
    race_value_ = value;
  }

  int maps_reads = 0;
  int protect_calls = 0;
  int cas_calls = 0;
  int write_faults = 0;
  std::vector<std::pair<uintptr_t, int>> protect_log;

  const PlatformOps& ops() {
    ops_.protect = [](void* ctx, uintptr_t page, size_t len, int prot) {
      return static_cast<FakePlatform*>(ctx)->protect(page, len, prot);
    };
    ops_.read_maps = [](void* ctx, MapsCoverage& c) {
      return static_cast<FakePlatform*>(ctx)->read_maps(c);
    };
    ops_.load = [](void* ctx, uintptr_t slot) {
      return static_cast<FakePlatform*>(ctx)->get(slot);
    };
    ops_.cas = [](void* ctx, uintptr_t slot, uintptr_t* expected, uintptr_t desired) {
      return static_cast<FakePlatform*>(ctx)->cas(slot, expected, desired);
    };
    ops_.ctx = this;
    return ops_;
  }

 private:
  struct PageState {
    int prot;
    bool shared;
  };

  int protect(uintptr_t page, size_t len, int prot) {
    int n = protect_calls++;
    protect_log.emplace_back(page, prot);
    if (n == fail_protect_at_) return fail_errno_;
    if (len != ps_) return EINVAL;
    auto it = pages_.find(page);
    if (it == pages_.end()) return ENOMEM;
    it->second.prot = prot;
    return 0;
  }

  Error read_maps(MapsCoverage& c) {
    ++maps_reads;
    for (const auto& kv : pages_) {
      if (c.done()) break;
      const PageState& p = kv.second;
      char line[96];
      int n = std::snprintf(line, sizeof line, "%llx-%llx %c%c%c%c 00000000 00:00 0",
                            static_cast<unsigned long long>(kv.first),
                            static_cast<unsigned long long>(kv.first + ps_),
                            (p.prot & kProtRead) ? 'r' : '-', (p.prot & kProtWrite) ? 'w' : '-',
                            (p.prot & kProtExec) ? 'x' : '-', p.shared ? 's' : 'p');
      c.feed_line(line, static_cast<size_t>(n));
    }
    return c.finish();
  }

  bool cas(uintptr_t slot, uintptr_t* expected, uintptr_t desired) {
    int n = cas_calls++;
    if (n == race_at_) mem_[race_slot_] = race_value_;
    uintptr_t cur = get(slot);
    if (cur != *expected) {
      *expected = cur;
      return false;
    }
    auto it = pages_.find(slot & ~(static_cast<uintptr_t>(ps_) - 1));
    if (it == pages_.end() || (it->second.prot & kProtWrite) == 0) {
      ++write_faults;
      *expected = cur;
      return false;
    }
    mem_[slot] = desired;
    return true;
  }

  size_t ps_;
  std::map<uintptr_t, PageState> pages_;
  std::map<uintptr_t, uintptr_t> mem_;
  int fail_protect_at_ = -1;
  int fail_errno_ = 0;
  int race_at_ = -1;
  uintptr_t race_slot_ = 0;
  uintptr_t race_value_ = 0;
  PlatformOps ops_{};
};

constexpr uintptr_t kPageA = 0x10000;
constexpr uintptr_t kPageB = 0x11000;
constexpr uintptr_t kPageC = 0x12000;
constexpr int kRO = kProtRead;
constexpr int kRW = kProtRead | kProtWrite;

SlotOp op(uintptr_t slot, uintptr_t expected, uintptr_t desired) {
  SlotOp o;
  o.slot = slot;
  o.expected = expected;
  o.desired = desired;
  return o;
}

}  // namespace

TEST(Txn, PrepareAppliesVmaPolicyToEveryPage) {
  struct Case {
    int prot;
    bool shared;
    ErrorCode want;
  } cases[] = {
      {kRO, true, ErrorCode::kPermissionDenied},
      {kRW, true, ErrorCode::kPermissionDenied},
      {kRO | kProtExec, false, ErrorCode::kPermissionDenied},
      {kRW | kProtExec, false, ErrorCode::kPermissionDenied},
      {kProtNone, false, ErrorCode::kPermissionDenied},
      {kProtWrite, false, ErrorCode::kPermissionDenied},
  };
  for (const auto& c : cases) {
    FakePlatform fp;
    fp.map_page(kPageA, kRO);
    fp.map_page(kPageB, c.prot, c.shared);
    Txn t(fp.ops(), 0x1000);
    uintptr_t s[] = {kPageA + 8, kPageB + 8};
    EXPECT_EQ(t.prepare(s, 2).code, c.want) << "prot=" << c.prot << " shared=" << c.shared;
    EXPECT_EQ(fp.protect_calls, 0);
  }
}

TEST(Txn, SixteenKPageGroupsSlotsFourKApart) {
  FakePlatform fp(0x4000);
  fp.map_page(0x40000, kRO);
  fp.set(0x40008, 1);
  fp.set(0x43008, 2);
  Txn t(fp.ops(), 0x4000);
  uintptr_t s[] = {0x40008, 0x43008};
  ASSERT_TRUE(t.prepare(s, 2).ok());
  SlotOp ops[] = {op(0x40008, 1, 10), op(0x43008, 2, 20)};
  EXPECT_TRUE(t.commit(ops, 2, CommitMode::kAllOrNothing).all_committed);
  EXPECT_EQ(fp.protect_calls, 2);
}

TEST(Txn, ConflictRollsBackEarlierSlotsInReverse) {
  FakePlatform fp;
  fp.map_page(kPageA, kRO);
  fp.map_page(kPageB, kRO);
  fp.set(kPageA + 8, 1);
  fp.set(kPageA + 16, 2);
  fp.set(kPageB + 8, 999);
  fp.set(kPageB + 16, 4);
  Txn t(fp.ops(), 0x1000);
  uintptr_t s[] = {kPageA + 8, kPageA + 16, kPageB + 8, kPageB + 16};
  ASSERT_TRUE(t.prepare(s, 4).ok());
  SlotOp ops[] = {op(kPageA + 8, 1, 10), op(kPageA + 16, 2, 20), op(kPageB + 8, 3, 30),
                  op(kPageB + 16, 4, 40)};
  CommitResult r = t.commit(ops, 4, CommitMode::kAllOrNothing);
  EXPECT_FALSE(r.all_committed);
  EXPECT_FALSE(r.rollback_failed);
  EXPECT_TRUE(r.pages_restored);
  EXPECT_EQ(r.conflicts, 1u);
  EXPECT_EQ(ops[0].outcome, SlotOutcome::kRolledBack);
  EXPECT_EQ(ops[1].outcome, SlotOutcome::kRolledBack);
  EXPECT_EQ(ops[2].outcome, SlotOutcome::kConflict);
  EXPECT_EQ(ops[2].observed, 999u);
  EXPECT_EQ(ops[3].outcome, SlotOutcome::kUntouched);

  EXPECT_EQ(fp.get(kPageA + 8), 1u);
  EXPECT_EQ(fp.get(kPageA + 16), 2u);
  EXPECT_EQ(fp.get(kPageB + 8), 999u);
  EXPECT_EQ(fp.get(kPageB + 16), 4u);

  EXPECT_EQ(fp.cas_calls, 5);
  EXPECT_EQ(fp.prot(kPageA), kRO);
  EXPECT_EQ(fp.prot(kPageB), kRO);
}

TEST(Txn, RollbackFailureIsReportedPerSlot) {
  FakePlatform fp;
  fp.map_page(kPageA, kRO);
  fp.set(kPageA + 8, 1);
  fp.set(kPageA + 16, 777);
  fp.race_before_cas(2, kPageA + 8, 0x3333);
  Txn t(fp.ops(), 0x1000);
  uintptr_t s[] = {kPageA + 8, kPageA + 16};
  ASSERT_TRUE(t.prepare(s, 2).ok());
  SlotOp ops[] = {op(kPageA + 8, 1, 10), op(kPageA + 16, 2, 20)};
  CommitResult r = t.commit(ops, 2, CommitMode::kAllOrNothing);
  EXPECT_TRUE(r.rollback_failed);
  EXPECT_TRUE(r.pages_restored);
  EXPECT_EQ(ops[0].outcome, SlotOutcome::kRollbackFailed);
  EXPECT_EQ(ops[0].observed, 0x3333u);
  EXPECT_EQ(ops[1].outcome, SlotOutcome::kConflict);
  EXPECT_EQ(fp.get(kPageA + 8), 0x3333u);
  EXPECT_EQ(fp.prot(kPageA), kRO);
}

TEST(Txn, RaiseFailureOnSecondPageRestoresFirst) {
  FakePlatform fp;
  fp.map_page(kPageA, kRO);
  fp.map_page(kPageB, kRO);
  fp.map_page(kPageC, kRO);
  fp.set(kPageA + 8, 1);
  fp.set(kPageB + 8, 2);
  fp.set(kPageC + 8, 3);
  fp.fail_protect_at(1, EPERM);
  Txn t(fp.ops(), 0x1000);
  uintptr_t s[] = {kPageA + 8, kPageB + 8, kPageC + 8};
  ASSERT_TRUE(t.prepare(s, 3).ok());
  SlotOp ops[] = {op(kPageA + 8, 1, 10), op(kPageB + 8, 2, 20), op(kPageC + 8, 3, 30)};
  CommitResult r = t.commit(ops, 3, CommitMode::kAllOrNothing);
  EXPECT_EQ(r.raise_errno, EPERM);
  EXPECT_TRUE(r.pages_restored);
  EXPECT_EQ(fp.cas_calls, 0);

  ASSERT_EQ(fp.protect_calls, 3);
  EXPECT_EQ(fp.protect_log[2], std::make_pair(kPageA, kRO));
  EXPECT_EQ(fp.prot(kPageA), kRO);
  EXPECT_EQ(fp.prot(kPageB), kRO);
  EXPECT_EQ(fp.prot(kPageC), kRO);
  for (const auto& o : ops) EXPECT_EQ(o.outcome, SlotOutcome::kUntouched);
}

TEST(Txn, RestoreFailureLeavesResidueButKeepsCommit) {
  FakePlatform fp;
  fp.map_page(kPageA, kRO);
  fp.map_page(kPageB, kRO);
  fp.set(kPageA + 8, 1);
  fp.set(kPageB + 8, 2);

  fp.fail_protect_at(2, ENOMEM);
  Txn t(fp.ops(), 0x1000);
  uintptr_t s[] = {kPageA + 8, kPageB + 8};
  ASSERT_TRUE(t.prepare(s, 2).ok());
  SlotOp ops[] = {op(kPageA + 8, 1, 10), op(kPageB + 8, 2, 20)};
  CommitResult r = t.commit(ops, 2, CommitMode::kAllOrNothing);
  EXPECT_TRUE(r.all_committed);
  EXPECT_FALSE(r.pages_restored);
  EXPECT_EQ(r.restore_errno, ENOMEM);
  EXPECT_EQ(fp.protect_calls, 4);
  EXPECT_EQ(fp.prot(kPageA), kRO);
  EXPECT_EQ(fp.prot(kPageB), kRW);

  PageResidue res[4];
  ASSERT_EQ(t.residue(res, 4), 1u);
  EXPECT_EQ(res[0].page, kPageB);
  EXPECT_EQ(res[0].original_prot, kRO);
  EXPECT_EQ(res[0].raised_prot, kRW);
}

}  // namespace txn_checks
