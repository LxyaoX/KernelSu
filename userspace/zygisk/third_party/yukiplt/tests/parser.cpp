// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "aps2_encoder.hpp"
#include "elf_builder.hpp"
#include "elf.hpp"
#include "imports.hpp"
#include "overrides.hpp"
#include "memory.hpp"
#include "registry.hpp"
#include "versioned_elf.hpp"
#include "yukiplt.hpp"

namespace elf_inspector_checks {
using namespace yukiplt;
using namespace yukiplt::detail;
using namespace yukiplt::test;

TEST(ElfInspector, RejectsDynamicReadWithoutReadableLoadAndUnalignedEntries) {
  ElfBuilder unreadable;
  unreadable.set_load_flags(PF_W);
  auto no_read = unreadable.build();
  EXPECT_EQ(inspect(no_read.load_bias(), no_read.phdr_table(), no_read.phdr_count).error().code,
            ErrorCode::kOutOfBounds);
  ElfBuilder builder;
  auto image = builder.build();
  image.buffer.resize(image.buffer.size() + 8);
  auto* headers = const_cast<yukiplt::NativeElfPhdr*>(image.phdr_table());
  for (size_t i = 0; i < image.phdr_count; ++i) {
    auto& header = headers[i];
    if (header.p_type == PT_LOAD) header.p_memsz += 8;
    if (header.p_type == PT_DYNAMIC) {
      std::memmove(image.buffer.data() + header.p_vaddr + 1, image.buffer.data() + header.p_vaddr,
                   header.p_memsz);
      ++header.p_vaddr;
    }
  }
  EXPECT_EQ(inspect(image.load_bias(), image.phdr_table(), image.phdr_count).error().code,
            ErrorCode::kCorruptedElf);
}

TEST(ElfInspector, ProgramHeaderViewMustMatchTheElfHeader) {
  ElfBuilder b;
  b.with_elf_header().add_symbol("f", 1).add_jump_slot(1).set_relro(true);
  auto img = b.build();
  ASSERT_TRUE(inspect(img.load_bias(), img.phdr_table(), img.phdr_count).has_value());

  auto short_view = inspect(img.load_bias(), img.phdr_table(), img.phdr_count - 1);
  ASSERT_FALSE(short_view.has_value());
  EXPECT_EQ(short_view.error().code, ErrorCode::kCorruptedElf);

  std::vector<yukiplt::NativeElfPhdr> copy(img.phdr_table(), img.phdr_table() + img.phdr_count);
  EXPECT_TRUE(inspect(img.load_bias(), copy.data(), copy.size()).has_value());

  auto shifted = img;
  const size_t table_bytes = img.phdr_count * sizeof(yukiplt::NativeElfPhdr);
  const size_t moved = (img.buffer.size() + 15) & ~size_t{15};
  shifted.buffer.resize(moved + table_bytes, 0);
  std::memcpy(shifted.buffer.data() + moved, shifted.buffer.data() + img.phdr_offset, table_bytes);
  auto* load = reinterpret_cast<yukiplt::NativeElfPhdr*>(shifted.buffer.data() + moved);
  ASSERT_EQ(load[0].p_type, static_cast<uint32_t>(PT_LOAD));
  load[0].p_filesz = load[0].p_memsz = shifted.buffer.size();

  std::memcpy(shifted.buffer.data() + img.phdr_offset, load, table_bytes);
  ASSERT_TRUE(inspect(shifted.load_bias(),
                      reinterpret_cast<const yukiplt::NativeElfPhdr*>(shifted.buffer.data() +
                                                                      img.phdr_offset),
                      img.phdr_count)
                  .has_value());
  auto moved_view = inspect(shifted.load_bias(), load, img.phdr_count);
  ASSERT_FALSE(moved_view.has_value());
  EXPECT_EQ(moved_view.error().code, ErrorCode::kCorruptedElf);
}

TEST(ElfInspector, MissingDtNull) {
  ElfBuilder b;
  b.add_symbol("f", 1);
  b.add_jump_slot(1);
  b.set_gnu_hash(true);
  b.omit_dt_null();

  auto img = b.build();
  auto result = inspect(img.load_bias(), img.phdr_table(), img.phdr_count);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kCorruptedElf);
}

}  // namespace elf_inspector_checks

namespace imports_checks {
using namespace yukiplt;
using namespace yukiplt::detail;
using namespace yukiplt::test;

namespace {

ImageView view_of(const ElfBuilder::BuiltImage& image) {
  return {image.load_bias(), image.phdr_table(), image.phdr_count, 1};
}

template <typename T>
void write_at(uintptr_t address, const T& value) {
  std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

ElfBuilder builder() {
  ElfBuilder b;
  b.add_symbol("first_import_with_a_long_owned_name", 1).add_symbol("second", 2);
  b.add_jump_slot(2).add_jump_slot(1);
  return b;
}

}  // namespace

TEST(Imports, RejectsInvalidAndUnterminatedNames) {
  for (bool unterminated : {false, true}) {
    auto image = builder().build();
    auto inspected = inspect(image.load_bias(), image.phdr_table(), image.phdr_count);
    ASSERT_TRUE(inspected);
    if (unterminated) {
      std::memset(reinterpret_cast<void*>(inspected.value().strtab), 'x', inspected.value().strsz);
    } else {
      write_at<uint32_t>(inspected.value().symtab + 2 * sizeof(yukiplt::NativeElfSym),
                         static_cast<uint32_t>(inspected.value().strsz));
    }
    auto result = inspect_imports(view_of(image));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::kCorruptedElf);
  }
}

TEST(Imports, RejectsMisalignedOutOfRangeAndOverflowingSlots) {
  for (int mode : {0, 1, 2}) {
    auto image = builder().build();
    auto inspected = inspect(image.load_bias(), image.phdr_table(), image.phdr_count);
    ASSERT_TRUE(inspected);
    const uintptr_t offset = mode == 0   ? image.got_offset + 1
                             : mode == 1 ? image.buffer.size() + 8
                                         : std::numeric_limits<uintptr_t>::max();
    write_at<yukiplt::NativeElfRelocation>(
        inspected.value().jmprel,
        test::test_relocation(offset, yukiplt::native_reloc_info(2, YUKIPLT_RELOC_JUMP_SLOT)));
    auto result = inspect_imports(view_of(image));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              mode == 2 ? ErrorCode::kIntegerOverflow : ErrorCode::kCorruptedElf);
  }
}

}  // namespace imports_checks

namespace hash_checks {
using namespace yukiplt;
using namespace yukiplt::detail;
using namespace yukiplt::test;

TEST(Resolver, UniqueSlotAmbiguousTwoDistinctSlots) {
  ElfBuilder b;
  b.add_symbol("dup", 1);
  b.add_jump_slot(1);
  b.add_jump_slot(1);
  b.set_gnu_hash(true);

  auto img = b.build();
  auto insp = inspect(img.load_bias(), img.phdr_table(), img.phdr_count);
  ASSERT_TRUE(insp.has_value());

  EXPECT_NE(img.got_slot_addr(0), img.got_slot_addr(1));

  auto r = resolve_unique_slot(insp.value(), "dup");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kAmbiguousSymbol);
}

TEST(Resolver, UniqueSlotDuplicateRecordsRejected) {
  ElfBuilder b;
  b.add_symbol("aliased", 1);
  b.add_jump_slot(1);
  b.alias_jump_slot(1, 0);
  b.set_gnu_hash(true);

  auto img = b.build();
  auto insp = inspect(img.load_bias(), img.phdr_table(), img.phdr_count);
  ASSERT_TRUE(insp.has_value());

  auto raw = resolve_slots(insp.value(), "aliased");
  ASSERT_FALSE(raw.has_value());
  EXPECT_EQ(raw.error().code, ErrorCode::kCorruptedElf);

  auto r = resolve_unique_slot(insp.value(), "aliased");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::kCorruptedElf);
}

}  // namespace hash_checks

namespace function_imports_checks {
using namespace yukiplt;
using test::Aps2Group;
using test::Aps2Reloc;
using test::ElfBuilder;
using test::encode_aps2;

namespace {

size_t live_groups() {
  std::lock_guard<std::mutex> lock(detail::op_mutex());
  return detail::Registry::instance().live_count();
}

constexpr uint32_t kAll = kSourceJmprel | kSourceNativeDynamic | kSourceNativeAndroidPacked;

}  // namespace

namespace {

constexpr uint64_t kGlobDatSym1 = yukiplt::native_reloc_info(1, YUKIPLT_RELOC_GLOB_DAT);

struct RawAps2 {
  std::vector<uint8_t> bytes;
  detail::InspectedImage image;
  size_t visited = 0;

  explicit RawAps2(std::vector<uint8_t> stream, size_t padded_size = 0) : bytes(std::move(stream)) {
    if (padded_size > bytes.size()) bytes.resize(padded_size, 0);
    const uintptr_t base = reinterpret_cast<uintptr_t>(bytes.data());
    image.load_segments.push_back({base, base + bytes.size(), PF_R | PF_W});
    image.android_rela = base;
    image.android_rela_size = bytes.size();
  }

  Error decode() {
    visited = 0;
    return detail::decode_android_packed(
        image,
        [](void* context, const detail::DecodedReloc&) {
          ++*static_cast<size_t*>(context);
          return Error::Ok();
        },
        &visited);
  }
};

std::vector<uint8_t> aps2_header(int64_t total, int64_t initial_offset) {
  std::vector<uint8_t> out = {0x41, 0x50, 0x53, 0x32};
  test::append_sleb128(out, total);
  test::append_sleb128(out, initial_offset);
  return out;
}

void append_single(std::vector<uint8_t>& out, int64_t delta, uint64_t info) {
  test::append_sleb128(out, 1);
  test::append_sleb128(out, 0);
  test::append_sleb128(out, delta);
  test::append_sleb128(out, static_cast<int64_t>(info));
}

}  // namespace

TEST(FunctionImportsAps2, CorruptionAfterTheTargetFailsTheWholeDecode) {
  auto bytes = aps2_header(2, 0);
  append_single(bytes, 8, kGlobDatSym1);
  test::append_sleb128(bytes, 1);
  test::append_sleb128(bytes, 0);
  test::append_sleb128(bytes, 8);
  bytes.push_back(0x80);
  RawAps2 raw(bytes);
  EXPECT_EQ(raw.decode().code, ErrorCode::kCorruptedElf);
}

TEST(FunctionImportsAps2, OverlongOrOutOfRangeSlebRejected) {
  auto overlong = aps2_header(1, 0);
  test::append_sleb128(overlong, 1);
  test::append_sleb128(overlong, 0);
  for (int i = 0; i < 10; ++i) overlong.push_back(0x80);
  overlong.push_back(0x00);
  test::append_sleb128(overlong, static_cast<int64_t>(kGlobDatSym1));
  RawAps2 overlong_raw(overlong);
  EXPECT_EQ(overlong_raw.decode().code, ErrorCode::kCorruptedElf);

  auto wide = aps2_header(1, 0);
  test::append_sleb128(wide, 1);
  test::append_sleb128(wide, 0);
  for (int i = 0; i < 9; ++i) wide.push_back(0x80);
  wide.push_back(0x01);
  test::append_sleb128(wide, static_cast<int64_t>(kGlobDatSym1));
  RawAps2 wide_raw(wide);
  EXPECT_EQ(wide_raw.decode().code, ErrorCode::kCorruptedElf);

  for (int64_t extreme : {INT64_MAX, INT64_MIN}) {
    std::vector<uint8_t> encoded;
    test::append_sleb128(encoded, extreme);
    ASSERT_EQ(encoded.size(), 10u);
    auto bytes = aps2_header(1, 0);
    test::append_sleb128(bytes, 1);
    test::append_sleb128(bytes, 8);
    test::append_sleb128(bytes, 8);
    test::append_sleb128(bytes, static_cast<int64_t>(kGlobDatSym1));
    test::append_sleb128(bytes, extreme);
    RawAps2 raw(bytes);
    if (kNativeUsesRela)
      EXPECT_FALSE(raw.decode()) << extreme;
    else
      EXPECT_EQ(raw.decode().code, ErrorCode::kCorruptedElf);
  }
}

namespace {
FunctionImportSelector sel(const char* symbol, uint32_t sources = kAll,
                           uint32_t types = kTypeJumpSlot | kTypeGlobDat) {
  FunctionImportSelector s;
  s.base.symbol = symbol;
  s.sources = sources;
  s.types = types;
  return s;
}
}  // namespace

namespace {

struct FuncInstallFixture : testing::Test {
  ElfBuilder::BuiltImage image;
  uintptr_t first_page = 0;
  uintptr_t end_page = 0;
  std::vector<int> page_protections;
  size_t protect_calls = 0;
  uintptr_t fail_restore_page = 0;
  bool restore_failure_injected = false;
  size_t maps_calls = 0;
  size_t cas_calls = 0;

  size_t race_before_cas = 0;
  uintptr_t race_slot_addr = 0;
  uintptr_t race_value = 0;
  void* outputs[4]{};

  detail::PlatformOps ops{
      [](void* p, uintptr_t page, size_t length, int prot) {
        auto& self = *static_cast<FuncInstallFixture*>(p);
        ++self.protect_calls;
        const size_t ps = detail::page_size();
        if (page < self.first_page || page >= self.end_page || page % ps != 0 || length == 0 ||
            length % ps != 0 || length > self.end_page - page)
          return EINVAL;
        if (page == self.fail_restore_page && prot == detail::kProtRead &&
            !self.restore_failure_injected) {
          self.restore_failure_injected = true;
          return EACCES;
        }
        for (size_t offset = 0; offset < length; offset += ps)
          self.page_protections[(page - self.first_page + offset) / ps] = prot;
        return 0;
      },
      [](void* p, detail::MapsCoverage& coverage) {
        auto& self = *static_cast<FuncInstallFixture*>(p);
        ++self.maps_calls;
        char line[128];
        const size_t ps = detail::page_size();
        for (size_t i = 0; i < self.page_protections.size(); ++i) {
          const uintptr_t page = self.first_page + i * ps;
          std::snprintf(line, sizeof(line), "%zx-%zx %s 00000000 00:00 0",
                        static_cast<size_t>(page), static_cast<size_t>(page + ps),
                        (self.page_protections[i] & detail::kProtWrite) ? "rw-p" : "r--p");
          coverage.feed_line(line, std::strlen(line));
        }
        return coverage.finish();
      },
      [](void*, uintptr_t address) {
        return __atomic_load_n(reinterpret_cast<uintptr_t*>(address), __ATOMIC_ACQUIRE);
      },
      [](void* p, uintptr_t address, uintptr_t* expected, uintptr_t desired) {
        auto& self = *static_cast<FuncInstallFixture*>(p);
        EXPECT_EQ(self.page_protections[(address - self.first_page) / detail::page_size()],
                  detail::kProtRead | detail::kProtWrite);
        if (++self.cas_calls == self.race_before_cas && self.race_slot_addr != 0)
          __atomic_store_n(reinterpret_cast<uintptr_t*>(self.race_slot_addr), self.race_value,
                           __ATOMIC_RELEASE);
        return __atomic_compare_exchange_n(reinterpret_cast<uintptr_t*>(address), expected, desired,
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
      },
      this};

  void set_backing(ElfBuilder::BuiltImage&& built) {
    image = std::move(built);
    const size_t ps = detail::page_size();
    ASSERT_NE(ps, 0u);
    first_page = image.load_bias() & ~(ps - 1);
    end_page = (image.load_bias() + image.buffer.size() + ps - 1) & ~(ps - 1);
    page_protections.assign((end_page - first_page) / ps, detail::kProtRead);
  }

  void SetUp() override { detail::set_active_ops(&ops); }
  void TearDown() override { detail::set_active_ops(nullptr); }

  ImageView view(uint64_t token = 0x51) const {
    return {image.load_bias(), image.phdr_table(), image.phdr_count, token};
  }
  uintptr_t rd(uintptr_t addr) const {
    return __atomic_load_n(reinterpret_cast<uintptr_t*>(addr), __ATOMIC_ACQUIRE);
  }
  void wr(uintptr_t addr, uintptr_t value) {
    __atomic_store_n(reinterpret_cast<uintptr_t*>(addr), value, __ATOMIC_RELEASE);
  }
};

void* const kReplA = reinterpret_cast<void*>(uintptr_t{0xA000});
void* const kReplB = reinterpret_cast<void*>(uintptr_t{0xB000});

}  // namespace

TEST_F(FuncInstallFixture, ZeroValuedGlobDatSlotIsRefusedWholeBatchZeroModification) {
  ElfBuilder b;
  b.add_symbol("gfunc", 1).set_symbol_undefined(1);
  b.add_rela(1, YUKIPLT_RELOC_GLOB_DAT, 0);
  set_backing(b.build());
  wr(image.rela_slot_addr(0), 0);

  const size_t baseline = live_groups();
  auto result = hook_function_import(view(), sel("gfunc"), kReplA, &outputs[0]);
  EXPECT_EQ(result.error.code, ErrorCode::kUnsupportedRelocation);
  EXPECT_FALSE(result.handle.valid());
  EXPECT_EQ(rd(image.rela_slot_addr(0)), 0u);
  EXPECT_EQ(cas_calls, 0u);
  EXPECT_EQ(outputs[0], nullptr);
  EXPECT_EQ(live_groups(), baseline);
}

TEST_F(FuncInstallFixture, UnsupportedMatchRejectsBatchBeforeAnyWrite) {
  ElfBuilder b;
  b.add_symbol("good", 1);
  b.add_symbol("bad", 2).set_symbol_undefined(2).set_symbol_type(2, STT_OBJECT);
  b.add_jump_slot(1);
  b.add_rela(2, YUKIPLT_RELOC_GLOB_DAT, 0);
  set_backing(b.build());
  wr(image.got_slot_addr(0), 0x1111);
  wr(image.rela_slot_addr(0), 0x2222);

  const size_t baseline = live_groups();
  FunctionHookRequest reqs[] = {{sel("good"), kReplA, &outputs[0]},
                                {sel("bad"), kReplB, &outputs[1]}};
  auto result = hook_function_imports(view(), reqs, 2);
  EXPECT_EQ(result.error.code, ErrorCode::kUnsupportedRelocation);
  EXPECT_FALSE(result.handle.valid());
  EXPECT_EQ(rd(image.got_slot_addr(0)), 0x1111u);
  EXPECT_EQ(rd(image.rela_slot_addr(0)), 0x2222u);
  EXPECT_EQ(cas_calls, 0u);
  EXPECT_EQ(maps_calls, 0u);
  EXPECT_EQ(live_groups(), baseline);
}

TEST_F(FuncInstallFixture, ZeroWeakJumpSlotRemainsSupported) {
  ElfBuilder b;
  b.add_symbol("weak", 1)
      .set_symbol_type(1, STT_FUNC, STB_WEAK)
      .set_symbol_undefined(1)
      .add_jump_slot(1);
  set_backing(b.build());
  wr(image.got_slot_addr(0), 0);
  SlotReport report{};
  auto result = hook_function_import(view(), sel("weak", kSourceJmprel, kTypeJumpSlot), kReplA,
                                     &outputs[0], &report);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(outputs[0], nullptr);

  EXPECT_EQ(rd(image.got_slot_addr(0)), reinterpret_cast<uintptr_t>(kReplA));
  EXPECT_EQ(report.address, image.got_slot_addr(0));
  EXPECT_EQ(report.value_before, 0u);
  EXPECT_EQ(report.status, SlotStatus::kInstalled);
  EXPECT_FALSE(unhook(result.handle));
  EXPECT_EQ(rd(image.got_slot_addr(0)), 0u);
}

}  // namespace function_imports_checks

namespace versions_checks {
using namespace yukiplt;
using namespace yukiplt::test;

TEST(Versions, VersymCannotAliasNamedVersionRecordsOrChangeSelection) {
  for (bool definitions : {false, true}) {
    VersionedElf fixture(definitions);
    ASSERT_TRUE(inspect_versioned_imports(fixture.view()));
    fixture.change_tag(DT_VERSYM, DT_VERSYM, fixture.need - sizeof(NativeElfHalf));
    const auto before = fixture.image.buffer;
    auto snapshot = inspect_versioned_imports(fixture.view());
    ASSERT_FALSE(snapshot);
    EXPECT_EQ(snapshot.error().code, ErrorCode::kCorruptedElf);
    auto selected = find_import(fixture.view(), {"versioned", VersionMatch::kUnversioned});
    ASSERT_FALSE(selected);
    EXPECT_EQ(selected.error().code, ErrorCode::kCorruptedElf);
    EXPECT_EQ(fixture.image.buffer, before);

    EXPECT_TRUE(inspect_imports(fixture.view()));
  }
}

}  // namespace versions_checks

namespace scan_limits_checks {
using namespace yukiplt;
using namespace yukiplt::detail;

TEST(ScanLimits, SharedRecordAndMaterializationBudgetsHaveExactBoundaries) {
  test::ElfBuilder builder;
  auto image = builder.add_symbol("long_function_name_for_budget", 1)
                   .set_symbol_undefined(1)
                   .add_jump_slot(1)
                   .add_rela(1, kNativeGlobDat)
                   .build();
  auto parsed = inspect(image.load_bias(), image.phdr_table(), image.phdr_count);
  ASSERT_TRUE(parsed);
  const uint32_t sources = kSourceJmprel | kSourceNativeDynamic;
  ScanBudget baseline;
  ASSERT_TRUE(scan_function_imports(parsed.value(), sources, &baseline));
  const size_t used = ScanBudget{}.bytes - baseline.bytes;
  for (size_t size : {used - 1, used}) {
    ScanBudget budget{2, size};
    auto result = scan_function_imports(parsed.value(), sources, &budget);
    EXPECT_EQ(bool(result), size == used);
    if (!result) EXPECT_EQ(result.error().code, ErrorCode::kOutOfBounds);
  }
  ScanBudget across_images{3, ScanBudget{}.bytes};
  ASSERT_TRUE(scan_function_imports(parsed.value(), sources, &across_images));
  auto result = scan_function_imports(parsed.value(), sources, &across_images);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::kOutOfBounds);
}

}  // namespace scan_limits_checks
