// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "imports.hpp"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

#include "common.hpp"
#include "elf.hpp"
#include "yukiplt.hpp"

namespace yukiplt::detail {
namespace {

bool readable(const InspectedImage& image, uintptr_t address, size_t size) {
  uintptr_t end;
  if (size == 0 || !safe_add(address, size, end)) return false;
  for (const auto& segment : image.load_segments) {
    if ((segment.flags & PF_R) && address >= segment.vaddr_begin && end <= segment.vaddr_end)
      return true;
  }
  return false;
}

template <typename T>
[[gnu::always_inline]] inline Result<T> read(const InspectedImage& image, uintptr_t base,
                                             size_t index = 0) {
  if (index > std::numeric_limits<uintptr_t>::max() / sizeof(T))
    return Error::Make(ErrorCode::kIntegerOverflow);
  uintptr_t address;
  if (!safe_add(base, index * sizeof(T), address)) return Error::Make(ErrorCode::kIntegerOverflow);
  if (!readable(image, address, sizeof(T))) return Error::Make(ErrorCode::kOutOfBounds);
  T value;
  std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
  return value;
}

bool array_readable(const InspectedImage& image, uintptr_t base, size_t count, size_t size) {
  return count <= std::numeric_limits<size_t>::max() / size && readable(image, base, count * size);
}

// Bound dynsym before interpreting an r_info index. A nonempty GNU hash's
// final bucket chain bounds the table. An empty GNU hash is different: GNU ld
// may emit symoffset=1 even when unhashed imports follow, so only the readable
// LOAD containing dynsym supplies a bound in that case (not an exact count).
Result<size_t> symbol_count(const InspectedImage& image) {
  if (image.sysv_hash != 0) {
    auto buckets = read<uint32_t>(image, image.sysv_hash);
    auto chains = read<uint32_t>(image, image.sysv_hash, 1);
    if (!buckets) return buckets.error();
    if (!chains) return chains.error();
    const uint64_t words = uint64_t{2} + buckets.value() + chains.value();
    if (buckets.value() == 0 || chains.value() == 0 || words > std::numeric_limits<size_t>::max() ||
        !array_readable(image, image.sysv_hash, static_cast<size_t>(words), sizeof(uint32_t)) ||
        !array_readable(image, image.symtab, chains.value(), sizeof(NativeElfSym)))
      return Error::Make(ErrorCode::kCorruptedElf);
    return static_cast<size_t>(chains.value());
  }
  if (image.gnu_hash == 0) return Error::Make(ErrorCode::kNoHashTable);
  auto buckets = read<uint32_t>(image, image.gnu_hash);
  auto first = read<uint32_t>(image, image.gnu_hash, 1);
  auto bloom = read<uint32_t>(image, image.gnu_hash, 2);
  if (!buckets) return buckets.error();
  if (!first) return first.error();
  if (!bloom) return bloom.error();
  if (buckets.value() == 0 || first.value() == 0 || bloom.value() == 0 ||
      (bloom.value() & (bloom.value() - 1)) != 0)
    return Error::Make(ErrorCode::kCorruptedElf);

  const uint64_t prefix = uint64_t{16} + uint64_t{bloom.value()} * sizeof(NativeElfAddr);
  const uint64_t header_size = prefix + uint64_t{buckets.value()} * 4;
  if (header_size > std::numeric_limits<size_t>::max() ||
      !readable(image, image.gnu_hash, static_cast<size_t>(header_size)))
    return Error::Make(ErrorCode::kCorruptedElf);
  // readable() above proves both additions cannot overflow.
  const uintptr_t bucket_base = image.gnu_hash + static_cast<uintptr_t>(prefix);
  const uintptr_t chain_base = image.gnu_hash + static_cast<uintptr_t>(header_size);
  uint32_t last_bucket = 0;
  for (size_t i = 0; i < buckets.value(); ++i) {
    auto bucket = read<uint32_t>(image, bucket_base, i);
    if (!bucket) return bucket.error();
    if (bucket.value() != 0 && bucket.value() < first.value())
      return Error::Make(ErrorCode::kCorruptedElf);
    last_bucket = std::max(last_bucket, bucket.value());
  }
  if (last_bucket == 0) {
    for (const auto& segment : image.load_segments) {
      if ((segment.flags & PF_R) && image.symtab >= segment.vaddr_begin &&
          image.symtab < segment.vaddr_end) {
        const size_t bound = (segment.vaddr_end - image.symtab) / sizeof(NativeElfSym);
        if (bound == 0) return Error::Make(ErrorCode::kCorruptedElf);
        return bound;
      }
    }
    return Error::Make(ErrorCode::kOutOfBounds);
  }
  size_t count = first.value();
  if (last_bucket != 0) {
    // Each iteration must have a mapped chain word AND symbol, so a missing
    // terminator cannot turn into an unbounded walk or read past PT_LOAD.
    size_t index = last_bucket;
    for (;;) {
      auto symbol = read<NativeElfSym>(image, image.symtab, index);
      auto chain = read<uint32_t>(image, chain_base, index - first.value());
      if (!symbol) return symbol.error();
      if (!chain) return chain.error();
      if (index == std::numeric_limits<uint32_t>::max())
        return Error::Make(ErrorCode::kIntegerOverflow);
      count = ++index;
      if (chain.value() & 1) break;
    }
  }
  if (!array_readable(image, image.symtab, count, sizeof(NativeElfSym)))
    return Error::Make(ErrorCode::kCorruptedElf);
  return count;
}

}  // namespace

Result<size_t> dynamic_symbol_count(const InspectedImage& image) { return symbol_count(image); }

namespace {
template <bool Own>
Result<ImportSnapshot> walk_imports_impl(const InspectedImage& image,
                                         void (*visit)(void*, const ImportView&) = nullptr,
                                         void* context = nullptr) {
  ImportSnapshot snapshot;
  if (image.jmprel_count == 0) return snapshot;
  auto count = symbol_count(image);
  if (!count) return count.error();
  if (!readable(image, image.strtab, image.strsz)) return Error::Make(ErrorCode::kOutOfBounds);

  // Every non-NONE DT_JMPREL record is a write to its destination, named or
  // not, so all of them take part in the duplicate-slot check — the same rule
  // the function-import scan applies to the tables it reads.  (This legacy
  // path reads only DT_JMPREL and has no scan budget; see yukiplt.h.)
  std::vector<uintptr_t> addresses;
  for (size_t i = 0; i < image.jmprel_count; ++i) {
    auto record = read<NativeElfRelocation>(image, image.jmprel, i);
    if (!record) return record.error();
    const auto& rela = record.value();
    const auto symbol_index = static_cast<uint32_t>(native_reloc_symbol(rela.r_info));
    const auto type = native_reloc_type(rela.r_info);
    if (type != YUKIPLT_RELOC_JUMP_SLOT || symbol_index == 0) {
      ++snapshot.skipped_relocations;
      if (type != 0) {
        uintptr_t destination;
        if (!safe_add(image.load_bias, static_cast<uintptr_t>(rela.r_offset), destination))
          return Error::Make(ErrorCode::kIntegerOverflow);
        addresses.push_back(destination);
      }
      continue;
    }
    if (symbol_index >= count.value()) return Error::Make(ErrorCode::kCorruptedElf);
    auto symbol = read<NativeElfSym>(image, image.symtab, symbol_index);
    if (!symbol) return symbol.error();
    const auto name_offset = symbol.value().st_name;
    if (name_offset >= image.strsz) return Error::Make(ErrorCode::kCorruptedElf);
    const char* name = reinterpret_cast<const char*>(image.strtab + name_offset);
    const auto* end = static_cast<const char*>(std::memchr(name, 0, image.strsz - name_offset));
    if (end == nullptr) return Error::Make(ErrorCode::kCorruptedElf);

    uintptr_t address;
    if (!safe_add(image.load_bias, static_cast<uintptr_t>(rela.r_offset), address))
      return Error::Make(ErrorCode::kIntegerOverflow);
    if (address % kNativeSlotSize != 0 || !image.addr_in_writable_load(address, kNativeSlotSize))
      return Error::Make(ErrorCode::kCorruptedElf);

    if constexpr (Own) {
      snapshot.imports.push_back(
          {std::string(name, static_cast<size_t>(end - name)), symbol_index, i, address});
    } else {
      visit(context,
            {std::string_view(name, static_cast<size_t>(end - name)), symbol_index, i, address});
    }
    addresses.push_back(address);
  }
  // Preserve table order in the snapshot; sort a separate address list so
  // thousands of imports do not require quadratic duplicate detection.
  std::sort(addresses.begin(), addresses.end());
  if (std::adjacent_find(addresses.begin(), addresses.end()) != addresses.end())
    return Error::Make(ErrorCode::kCorruptedElf);
  return snapshot;
}
}  // namespace

Error walk_imports(const InspectedImage& image, void (*visit)(void*, const ImportView&),
                   void* context, size_t& skipped) {
  auto result = walk_imports_impl<false>(image, visit, context);
  if (!result) return result.error();
  skipped = result.value().skipped_relocations;
  return Error::Ok();
}
Result<ImportSnapshot> snapshot_imports(const InspectedImage& image) {
  return walk_imports_impl<true>(image);
}

namespace {

constexpr uint16_t kVersionMask = 0x7fff;
constexpr uint16_t kHiddenBit = 0x8000;

template <typename T>
Result<T> read_version(const InspectedImage& image, uintptr_t address, size_t index = 0) {
  if (index > std::numeric_limits<uintptr_t>::max() / sizeof(T) ||
      !safe_add(address, index * sizeof(T), address))
    return Error::Make(ErrorCode::kIntegerOverflow);
  if (!readable(image, address, sizeof(T))) return Error::Make(ErrorCode::kOutOfBounds);
  T value;
  std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
  return value;
}

Result<std::string> string_at(const InspectedImage& image, uint32_t offset, ScanBudget& budget) {
  if (offset >= image.strsz || !readable(image, image.strtab, image.strsz))
    return Error::Make(ErrorCode::kCorruptedElf);
  const char* begin = reinterpret_cast<const char*>(image.strtab + offset);
  const auto* end = static_cast<const char*>(std::memchr(begin, 0, image.strsz - offset));
  if (end == nullptr || end == begin) return Error::Make(ErrorCode::kCorruptedElf);
  if (Error e = budget.take_bytes(static_cast<size_t>(end - begin) + 1)) return e;
  return std::string(begin, static_cast<size_t>(end - begin));
}

Result<uintptr_t> advance(uintptr_t base, uint32_t offset, size_t minimum) {
  if (offset < minimum) return Error::Make(ErrorCode::kCorruptedElf);
  uintptr_t address;
  if (!safe_add(base, offset, address)) return Error::Make(ErrorCode::kIntegerOverflow);
  return address;
}

struct Tags {
  uint64_t values[5]{};  // VERSYM, VERNEED, VERNEEDNUM, VERDEF, VERDEFNUM
  bool present[5]{};
};

Result<Tags> version_tags(const InspectedImage& image) {
  Tags tags;
  constexpr int64_t wanted[] = {DT_VERSYM, DT_VERNEED, DT_VERNEEDNUM, DT_VERDEF, DT_VERDEFNUM};
  for (size_t i = 0; i < image.dynamic_size / sizeof(NativeElfDyn); ++i) {
    auto entry = read_version<NativeElfDyn>(image, image.dynamic, i);
    if (!entry) return entry.error();
    if (entry.value().d_tag == DT_NULL) return tags;
    for (size_t j = 0; j < 5; ++j) {
      if (entry.value().d_tag != wanted[j]) continue;
      if (tags.present[j]) return Error::Make(ErrorCode::kCorruptedElf);
      tags.present[j] = true;
      tags.values[j] = entry.value().d_un.d_val;
    }
  }
  return Error::Make(ErrorCode::kCorruptedElf);
}

struct NamedVersion {
  uint16_t index;
  VersionSource source;
  std::string name;
  std::string dependency;
};

struct VersionScan {
  ScanBudget& budget;
  std::map<uintptr_t, uintptr_t> ranges;
  std::bitset<1u << 15> indices;

  Error claim_index(uint16_t index) {
    if (indices.test(index)) return Error::Make(ErrorCode::kCorruptedElf);
    indices.set(index);
    return budget.take_objects<NamedVersion>(1);
  }
};

template <typename T>
Result<T> read_record(const InspectedImage& image, uintptr_t address, VersionScan& scan) {
  auto value = read_version<T>(image, address);
  if (!value) return value.error();
  // read_version() proved the end cannot overflow. Check both neighbours before any
  // allocation or following a shared auxiliary chain a second time.
  const uintptr_t end = address + sizeof(T);
  const auto next = scan.ranges.lower_bound(address);
  if ((next != scan.ranges.end() && next->first < end) ||
      (next != scan.ranges.begin() && std::prev(next)->second > address))
    return Error::Make(ErrorCode::kCorruptedElf);
  // Charge a node-based map entry at its real cost: the pair plus the parent,
  // left, right links and color a red-black tree node carries (R-10).
  struct RangeNodeCost {
    void* links[3];
    uintptr_t color;
    std::pair<uintptr_t, uintptr_t> value;
  };
  if (Error e = scan.budget.take_objects<RangeNodeCost>(1)) return e;
  scan.ranges.emplace_hint(next, address, end);
  return value;
}

// Validate every linked entry, even versions that no selected import uses.
// Forward, non-overlapping relative offsets and exact terminal links bound
// traversal by both the declared counts and the readable mapping.
Error read_needs(const InspectedImage& image, uintptr_t address, uint64_t count,
                 std::vector<NamedVersion>& out, VersionScan& scan) {
  for (uint64_t i = 0; i < count; ++i) {
    auto record = read_record<NativeElfVerneed>(image, address, scan);
    if (!record) return record.error();
    const auto& need = record.value();
    if (need.vn_version != 1 || need.vn_cnt == 0 || (i + 1 == count) != (need.vn_next == 0))
      return Error::Make(ErrorCode::kCorruptedElf);
    auto file = string_at(image, need.vn_file, scan.budget);
    if (!file) return file.error();
    auto aux = advance(address, need.vn_aux, sizeof(NativeElfVerneed));
    if (!aux) return aux.error();
    uintptr_t next_record = 0;
    if (need.vn_next != 0) {
      auto next = advance(address, need.vn_next, sizeof(NativeElfVerneed));
      if (!next) return next.error();
      next_record = next.value();
    }
    for (size_t j = 0; j < need.vn_cnt; ++j) {
      auto item = read_record<NativeElfVernaux>(image, aux.value(), scan);
      if (!item) return item.error();
      const auto& value = item.value();
      const uint16_t index = value.vna_other & kVersionMask;
      if (index < 2 || (j + 1 == need.vn_cnt) != (value.vna_next == 0))
        return Error::Make(ErrorCode::kCorruptedElf);
      if (Error e = scan.claim_index(index)) return e;
      if (Error e = scan.budget.take_bytes(file.value().size() + 1)) return e;
      auto name = string_at(image, value.vna_name, scan.budget);
      if (!name) return name.error();
      if (elf_hash_value(name.value().c_str()) != value.vna_hash)
        return Error::Make(ErrorCode::kCorruptedElf);
      out.push_back({index, VersionSource::kNeeded, std::move(name.value()), file.value()});
      if (value.vna_next != 0) {
        aux = advance(aux.value(), value.vna_next, sizeof(NativeElfVernaux));
        if (!aux) return aux.error();
      }
    }
    address = next_record;
  }
  return Error::Ok();
}

Error read_definitions(const InspectedImage& image, uintptr_t address, uint64_t count,
                       std::vector<NamedVersion>& out, VersionScan& scan) {
  for (uint64_t i = 0; i < count; ++i) {
    auto record = read_record<NativeElfVerdef>(image, address, scan);
    if (!record) return record.error();
    const auto& definition = record.value();
    const uint16_t index = definition.vd_ndx & kVersionMask;
    if (definition.vd_version != 1 || definition.vd_cnt == 0 || index == 0 ||
        (index == 1) != ((definition.vd_flags & 1) != 0) ||
        (i + 1 == count) != (definition.vd_next == 0))
      return Error::Make(ErrorCode::kCorruptedElf);
    if (Error e = scan.claim_index(index)) return e;
    auto aux = advance(address, definition.vd_aux, sizeof(NativeElfVerdef));
    if (!aux) return aux.error();
    uintptr_t next_record = 0;
    if (definition.vd_next != 0) {
      auto next = advance(address, definition.vd_next, sizeof(NativeElfVerdef));
      if (!next) return next.error();
      next_record = next.value();
    }
    for (size_t j = 0; j < definition.vd_cnt; ++j) {
      auto item = read_record<NativeElfVerdaux>(image, aux.value(), scan);
      if (!item) return item.error();
      if ((j + 1 == definition.vd_cnt) != (item.value().vda_next == 0))
        return Error::Make(ErrorCode::kCorruptedElf);
      auto name = string_at(image, item.value().vda_name, scan.budget);
      if (!name) return name.error();
      if (j == 0) {
        if (elf_hash_value(name.value().c_str()) != definition.vd_hash)
          return Error::Make(ErrorCode::kCorruptedElf);
        out.push_back({index, VersionSource::kDefined, std::move(name.value()), {}});
      }
      if (item.value().vda_next != 0) {
        aux = advance(aux.value(), item.value().vda_next, sizeof(NativeElfVerdaux));
        if (!aux) return aux.error();
      }
    }
    address = next_record;
  }
  return Error::Ok();
}

}  // namespace

namespace {
template <typename EntryAt, typename Prepare, typename Store>
Error resolve_versions_impl(const InspectedImage& image, size_t count, EntryAt entry_at,
                            Prepare prepare, Store store, ScanBudget& budget) {
  auto tags = version_tags(image);
  if (!tags) return tags.error();
  const auto& t = tags.value();
  if (t.present[1] != t.present[2] || t.present[3] != t.present[4] ||
      (!t.present[0] && (t.present[1] || t.present[3])))
    return Error::Make(ErrorCode::kCorruptedElf);
  std::vector<NamedVersion> versions;
  VersionScan scan{budget, {}, {}};
  for (size_t pair : {size_t{1}, size_t{3}}) {
    if (!t.present[pair]) continue;
    if (t.values[pair + 1] == 0) return Error::Make(ErrorCode::kCorruptedElf);
    uintptr_t address;
    // Unlike SYMTAB and VERSYM, glibc leaves VERNEED/VERDEF load-relative.
    if (!safe_add(image.load_bias, t.values[pair], address))
      return Error::Make(ErrorCode::kIntegerOverflow);
    auto error = pair == 1 ? read_needs(image, address, t.values[pair + 1], versions, scan)
                           : read_definitions(image, address, t.values[pair + 1], versions, scan);
    if (error) return error;
  }
  std::sort(versions.begin(), versions.end(),
            [](const NamedVersion& a, const NamedVersion& b) { return a.index < b.index; });
  uintptr_t versym = t.values[0];
  if (t.present[0] && !image.dptr_adjusted && !safe_add(image.load_bias, versym, versym))
    return Error::Make(ErrorCode::kIntegerOverflow);
  if (t.present[0]) {
    // A referenced symbol proves this contiguous VERSYM prefix exists. Do not
    // use a LOAD-derived dynsym upper bound as an exact version-table count.
    uintptr_t last = 0;
    for (size_t i = 0; i < count; ++i) last = std::max(last, uintptr_t{entry_at(i).symbol_index});
    uintptr_t count, bytes, end;
    if (!safe_add(last, 1, count) || !safe_mul(count, sizeof(NativeElfHalf), bytes) ||
        !safe_add(versym, bytes, end))
      return Error::Make(ErrorCode::kIntegerOverflow);
    if (!readable(image, versym, bytes)) return Error::Make(ErrorCode::kOutOfBounds);
    const auto next = scan.ranges.lower_bound(versym);
    if ((next != scan.ranges.end() && next->first < end) ||
        (next != scan.ranges.begin() && std::prev(next)->second > versym))
      return Error::Make(ErrorCode::kCorruptedElf);
  }
  if (Error e = budget.take_objects<VersionedImport>(count)) return e;
  prepare(count);
  for (size_t i = 0; i < count; ++i) {
    auto& entry = entry_at(i);
    SymbolVersion version;
    version.table_present = t.present[0];
    if (t.present[0]) {
      auto value = read_version<NativeElfHalf>(image, versym, entry.symbol_index);
      if (!value) return value.error();
      version.index = value.value() & kVersionMask;
      version.hidden = (value.value() & kHiddenBit) != 0;
      if (version.index < 2 && version.hidden) return Error::Make(ErrorCode::kCorruptedElf);
      if (version.index >= 2) {
        auto found =
            std::lower_bound(versions.begin(), versions.end(), version.index,
                             [](const NamedVersion& v, uint16_t index) { return v.index < index; });
        if (found == versions.end() || found->index != version.index)
          return Error::Make(ErrorCode::kCorruptedElf);
        auto symbol = read_version<NativeElfSym>(image, image.symtab, entry.symbol_index);
        if (!symbol) return symbol.error();
        if ((symbol.value().st_shndx == SHN_UNDEF) != (found->source == VersionSource::kNeeded))
          return Error::Make(ErrorCode::kCorruptedElf);
        if (Error e = budget.take_bytes(found->name.size() + 1)) return e;
        if (Error e = budget.take_bytes(found->dependency.size() + 1)) return e;
        version.name = found->name;
        version.dependency = found->dependency;
        version.source = found->source;
      }
    }
    store(i, std::move(version));
  }
  return Error::Ok();
}

}  // namespace

Result<VersionedImportSnapshot> resolve_versions(const InspectedImage& image,
                                                 std::vector<ImportInfo> imports,
                                                 size_t skipped_relocations,
                                                 ScanBudget* shared_budget) {
  ScanBudget local_budget;
  ScanBudget& budget = shared_budget ? *shared_budget : local_budget;
  VersionedImportSnapshot result;
  result.skipped_relocations = skipped_relocations;
  auto error = resolve_versions_impl(
      image, imports.size(), [&](size_t i) -> ImportInfo& { return imports[i]; },
      [&](size_t count) { result.imports.reserve(count); },
      [&](size_t i, SymbolVersion version) {
        result.imports.push_back({std::move(imports[i]), std::move(version)});
      },
      budget);
  if (error) return error;
  return result;
}

Result<VersionedImportSnapshot> snapshot_versioned_imports(const InspectedImage& image,
                                                           CauseContext* cause) {
  auto raw = snapshot_imports(image);
  if (!raw) return raw.error();
  ScanBudget budget;
  budget.cause = cause;
  return resolve_versions(image, std::move(raw.value().imports), raw.value().skipped_relocations,
                          &budget);
}

Error validate_selector(const ImportSelector& selector) noexcept {
  if (selector.symbol == nullptr) return Error::Make(ErrorCode::kNullPointer);
  if (*selector.symbol == '\0' ||
      (selector.version_match != VersionMatch::kAny &&
       selector.version_match != VersionMatch::kUnversioned &&
       selector.version_match != VersionMatch::kExact) ||
      (selector.hidden != HiddenMatch::kAny && selector.hidden != HiddenMatch::kNonHidden &&
       selector.hidden != HiddenMatch::kHidden) ||
      (selector.version_match == VersionMatch::kExact
           ? selector.version == nullptr || *selector.version == '\0'
           : selector.version != nullptr) ||
      (selector.dependency != nullptr && *selector.dependency == '\0'))
    return Error::Make(ErrorCode::kInvalidArgument);
  return Error::Ok();
}

bool import_matches(const VersionedImport& entry, const ImportSelector& selector) noexcept {
  return !(
      entry.import.symbol != selector.symbol ||
      (selector.relocation_index != ~size_t{0} &&
       entry.import.relocation_index != selector.relocation_index) ||
      (selector.version_match == VersionMatch::kUnversioned && entry.version.index >= 2) ||
      (selector.version_match == VersionMatch::kExact && entry.version.name != selector.version) ||
      (selector.dependency != nullptr && entry.version.dependency != selector.dependency) ||
      (selector.hidden == HiddenMatch::kHidden && !entry.version.hidden) ||
      (selector.hidden == HiddenMatch::kNonHidden && entry.version.hidden));
}

Result<size_t> select_import(const VersionedImportSnapshot& snapshot,
                             const ImportSelector& selector) {
  auto invalid = validate_selector(selector);
  if (invalid) return invalid;
  size_t selected = ~size_t{0};
  for (size_t i = 0; i < snapshot.imports.size(); ++i) {
    const auto& entry = snapshot.imports[i];
    if (!import_matches(entry, selector)) continue;
    if (selected != ~size_t{0}) return Error::Make(ErrorCode::kAmbiguousSymbol);
    selected = i;
  }
  if (selected == ~size_t{0}) return Error::Make(ErrorCode::kSymbolNotFound);
  return selected;
}

namespace {

// True if [addr, addr + size) lies entirely within one readable (PF_R) LOAD.

// Resolve r_offset to a runtime slot address.
bool resolve_slot(const InspectedImage& image, uint64_t r_offset, uintptr_t& out) noexcept {
  uintptr_t offset;
  return narrow_address(r_offset, offset) && safe_add(image.load_bias, offset, out);
}

}  // namespace

Error decode_dynamic_relocations(const InspectedImage& image, RelocVisitor visit, void* context,
                                 ScanBudget* budget) {
  if (image.rela == 0 || image.rela_size == 0) return Error::Ok();
  if (image.rela_ent != 0 && image.rela_ent != sizeof(NativeElfRelocation))
    return Error::Make(ErrorCode::kUnsupportedFormat);
  if (image.rela_size % sizeof(NativeElfRelocation) != 0)
    return Error::Make(ErrorCode::kCorruptedElf);
  if (!readable(image, image.rela, image.rela_size)) return Error::Make(ErrorCode::kOutOfBounds);

  size_t count = image.rela_size / sizeof(NativeElfRelocation);

  // Allow overlap only when JMPREL is wholly inside the native table and shares
  // its end; decode the prefix once. Reject all other overlaps.
  if (image.jmprel != 0 && image.jmprel_count != 0) {
    uintptr_t rela_begin = image.rela;
    uintptr_t rela_end = image.rela + image.rela_size;  // readable() proved no overflow.
    uintptr_t plt_begin = image.jmprel;
    uintptr_t plt_bytes;
    if (!safe_mul(image.jmprel_count, sizeof(NativeElfRelocation), plt_bytes))
      return Error::Make(ErrorCode::kIntegerOverflow);
    uintptr_t plt_end;
    if (!safe_add(plt_begin, plt_bytes, plt_end)) return Error::Make(ErrorCode::kIntegerOverflow);
    const bool disjoint = plt_end <= rela_begin || plt_begin >= rela_end;
    if (!disjoint) {
      const bool contained_with_shared_end =
          plt_begin >= rela_begin && plt_end == rela_end &&
          (plt_begin - rela_begin) % sizeof(NativeElfRelocation) == 0;
      if (!contained_with_shared_end) return Error::Make(ErrorCode::kCorruptedElf);
      count = (plt_begin - rela_begin) / sizeof(NativeElfRelocation);
    }
  }

  if (budget) {
    if (Error e = budget->take_records(count)) return e;
  }
  for (size_t i = 0; i < count; ++i) {
    NativeElfRelocation rela;
    std::memcpy(&rela, reinterpret_cast<const void*>(image.rela + i * sizeof(NativeElfRelocation)),
                sizeof(rela));
    DecodedReloc decoded;
    if (!resolve_slot(image, rela.r_offset, decoded.slot_address))
      return Error::Make(ErrorCode::kIntegerOverflow);
    decoded.symbol_index = reloc_sym_of(rela.r_info);
    decoded.type = reloc_type_of(rela.r_info);
    decoded.addend = native_reloc_addend(rela);
    decoded.source = kNativeUsesRela ? RelocSource::kRela : RelocSource::kRel;
    decoded.index = i;
    if (Error e = visit(context, decoded)) return e;
  }
  return Error::Ok();
}

namespace {

/// A bounds-checked cursor over the packed byte stream.  Every read verifies it
/// stays within [base, base + size); SLEB128 reads also cap the encoding length.
class ByteCursor {
 public:
  ByteCursor(uintptr_t base, size_t size) noexcept : at_(base), end_(base + size), ok_(true) {
    // The caller proved [base, base+size) is readable, so base+size cannot wrap.
  }

  bool ok() const noexcept { return ok_; }
  size_t remaining() const noexcept { return ok_ ? static_cast<size_t>(end_ - at_) : 0; }

  // Read one byte; sets failure and returns 0 at end of stream.
  uint8_t byte() noexcept {
    if (!ok_ || at_ >= end_) {
      ok_ = false;
      return 0;
    }
    uint8_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(at_), 1);
    ++at_;
    return value;
  }

  // SLEB128 must fit int64_t: byte ten may only terminate with 0x00 or 0x7f.
  // Reject truncation, continuation and invalid sign extension.
  int64_t sleb128() noexcept {
    uint64_t result = 0;
    int shift = 0;
    constexpr int kMaxBytes = 10;
    for (int i = 0; i < kMaxBytes; ++i) {
      const uint8_t byte_value = byte();
      if (!ok_) return 0;
      if (i == kMaxBytes - 1) {
        if (byte_value != 0x00 && byte_value != 0x7f) break;
        if (byte_value == 0x7f) result |= uint64_t{1} << 63;
        return static_cast<int64_t>(result);
      }
      result |= static_cast<uint64_t>(byte_value & 0x7f) << shift;
      shift += 7;
      if ((byte_value & 0x80) == 0) {
        // Sign-extend from the final bit (shift <= 63 here).
        if (byte_value & 0x40) result |= ~uint64_t{0} << shift;
        return static_cast<int64_t>(result);
      }
    }
    ok_ = false;
    return 0;
  }

 private:
  uintptr_t at_;
  uintptr_t end_;
  bool ok_;
};

constexpr uint64_t kKnownGroupFlags =
    kRelocGroupedByInfo | kRelocGroupedByOffsetDelta | kRelocGroupedByAddend | kRelocGroupHasAddend;

}  // namespace

Error decode_android_packed(const InspectedImage& image, RelocVisitor visit, void* context,
                            ScanBudget* budget) {
  if (image.android_rela == 0 || image.android_rela_size == 0) return Error::Ok();
  if (image.android_rela_size > kMaxPackedBytes)
    return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget,
                      budget == nullptr ? nullptr : budget->cause);
  if (!readable(image, image.android_rela, image.android_rela_size))
    return Error::Make(ErrorCode::kOutOfBounds);

  ByteCursor cursor(image.android_rela, image.android_rela_size);

  // Magic "APS2".
  for (unsigned char expected : kAps2Magic) {
    if (cursor.byte() != expected || !cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
  }

  const int64_t total_signed = cursor.sleb128();
  const int64_t group_base_offset = cursor.sleb128();
  if (!cursor.ok() || total_signed < 0 || group_base_offset < 0)
    return Error::Make(ErrorCode::kCorruptedElf);
  const uint64_t total = static_cast<uint64_t>(total_signed);
  if (total > kMaxPackedRelocations)
    return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget,
                      budget == nullptr ? nullptr : budget->cause);
  if (budget) {
    if (Error e = budget->take_records(static_cast<size_t>(total))) return e;
  }

  // r_offset and the addend are accumulated with checked signed arithmetic: a
  // stream whose running value overflows int64_t is rejected, never wrapped.
  // Every emitted r_offset must also be non-negative.
  int64_t offset = group_base_offset;
  int64_t addend = 0;
  uint64_t emitted = 0;

  while (emitted < total) {
    const int64_t group_size_signed = cursor.sleb128();
    const int64_t group_flags_signed = cursor.sleb128();
    if (!cursor.ok() || group_size_signed <= 0) return Error::Make(ErrorCode::kCorruptedElf);
    const uint64_t group_size = static_cast<uint64_t>(group_size_signed);
    if (group_size > total - emitted) return Error::Make(ErrorCode::kCorruptedElf);
    if (group_flags_signed < 0 ||
        (static_cast<uint64_t>(group_flags_signed) & ~kKnownGroupFlags) != 0)
      return Error::Make(ErrorCode::kCorruptedElf);
    const uint64_t flags = static_cast<uint64_t>(group_flags_signed);

    const bool grouped_by_delta = (flags & kRelocGroupedByOffsetDelta) != 0;
    const bool grouped_by_info = (flags & kRelocGroupedByInfo) != 0;
    const bool group_has_addend = (flags & kRelocGroupHasAddend) != 0;
    const bool grouped_by_addend = (flags & kRelocGroupedByAddend) != 0;
    if (!kNativeUsesRela && (group_has_addend || grouped_by_addend))
      return Error::Make(ErrorCode::kCorruptedElf);
    // "grouped by addend" without "has addend" is contradictory.
    if (grouped_by_addend && !group_has_addend) return Error::Make(ErrorCode::kCorruptedElf);

    int64_t group_offset_delta = 0;
    uint64_t group_info = 0;
    if (grouped_by_delta) {
      group_offset_delta = cursor.sleb128();
      if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
    }
    if (grouped_by_info) {
      group_info = static_cast<uint64_t>(cursor.sleb128());
      if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
    }
    if (grouped_by_addend) {
      const int64_t group_addend_delta = cursor.sleb128();
      if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
      if (__builtin_add_overflow(addend, group_addend_delta, &addend))
        return Error::Make(ErrorCode::kIntegerOverflow);
    }

    for (uint64_t i = 0; i < group_size; ++i) {
      int64_t delta = group_offset_delta;
      if (!grouped_by_delta) {
        delta = cursor.sleb128();
        if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
      }
      if (__builtin_add_overflow(offset, delta, &offset))
        return Error::Make(ErrorCode::kIntegerOverflow);
      if (offset < 0) return Error::Make(ErrorCode::kCorruptedElf);
      uint64_t info = group_info;
      if (!grouped_by_info) {
        info = static_cast<uint64_t>(cursor.sleb128());
        if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
      }
      if (group_has_addend && !grouped_by_addend) {
        const int64_t addend_delta = cursor.sleb128();
        if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
        if (__builtin_add_overflow(addend, addend_delta, &addend))
          return Error::Make(ErrorCode::kIntegerOverflow);
      }
      if (!group_has_addend) addend = 0;

      DecodedReloc decoded;
      if (!kNativeUsesRela && info > UINT32_MAX) return Error::Make(ErrorCode::kIntegerOverflow);
      if (!resolve_slot(image, static_cast<uint64_t>(offset), decoded.slot_address))
        return Error::Make(ErrorCode::kIntegerOverflow);
      decoded.symbol_index = reloc_sym_of(info);
      decoded.type = reloc_type_of(info);
      decoded.addend = addend;
      decoded.source = kNativeUsesRela ? RelocSource::kAndroidRela : RelocSource::kAndroidRel;
      decoded.index = static_cast<size_t>(emitted);
      if (Error e = visit(context, decoded)) return e;
      ++emitted;
    }
  }

  // The declared count is consumed.  Any remaining bytes must be all-zero
  // padding (LLD aligns the section); a non-zero trailing byte is malformed.
  while (cursor.remaining() != 0) {
    if (cursor.byte() != 0) return Error::Make(ErrorCode::kCorruptedElf);
  }
  if (!cursor.ok()) return Error::Make(ErrorCode::kCorruptedElf);
  return Error::Ok();
}

namespace {

RelocationSource to_public_source(RelocSource source) noexcept {
  switch (source) {
    case RelocSource::kJmprel:
      return RelocationSource::kJmprel;
    case RelocSource::kRela:
      return RelocationSource::kRela;
    case RelocSource::kAndroidRela:
      return RelocationSource::kAndroidRela;
    case RelocSource::kRel:
      return RelocationSource::kRel;
    case RelocSource::kAndroidRel:
      return RelocationSource::kAndroidRel;
  }
  return RelocationSource::kJmprel;  // Internal enum is constructed only by decoders.
}

Error validate_sources(uint32_t sources) noexcept {
  constexpr uint32_t known =
      kSourceJmprel | kSourceRela | kSourceAndroidRela | kSourceRel | kSourceAndroidRel;
  constexpr uint32_t native = kSourceJmprel | kSourceNativeDynamic | kSourceNativeAndroidPacked;
  if (sources == 0 || (sources & ~known) != 0) return Error::Make(ErrorCode::kInvalidArgument);
  if ((sources & ~native) != 0) return Error::Make(ErrorCode::kUnsupportedFormat);
  return Error::Ok();
}

// True if [addr, addr + size) lies entirely within one readable (PF_R) LOAD.
// Every table this path reads must pass this, exactly as on the legacy path.

// Per-scan state shared by every source.  The dynsym bound and the string
// table are validated on the first record that needs a symbol, so an image
// with nothing to import never has its hash table or strings read.
struct ScanState {
  const InspectedImage& image;
  RawFunctionSnapshot& result;
  ScanBudget& budget;
  /// Destination of every non-NONE record.  Duplicates are detected as each
  /// record is decoded (AGENTS: checked early, before later records or
  /// version metadata are materialized), so this is an ordered set.
  std::set<uintptr_t> slots;
  size_t symbol_bound = 0;
  bool symbols_ready = false;
};

Error ensure_symbols(ScanState& state) {
  if (state.symbols_ready) return Error::Ok();
  auto bound = dynamic_symbol_count(state.image);
  if (!bound) return bound.error();
  if (!readable(state.image, state.image.strtab, state.image.strsz))
    return Error::Make(ErrorCode::kOutOfBounds);
  state.symbol_bound = bound.value();
  state.symbols_ready = true;
  return Error::Ok();
}

// Read a record's symbol name (bounded by STRSZ), ELF symbol type and whether it
// is undefined.  An empty name is recorded as-is, as the legacy snapshot does;
// no selector can match it, because a selector's symbol must be non-empty.
Error read_symbol(const ScanState& state, uint32_t symbol_index, std::string& name,
                  uint8_t& symbol_type, bool& undefined) {
  const InspectedImage& image = state.image;
  if (symbol_index == 0 || symbol_index >= state.symbol_bound)
    return Error::Make(ErrorCode::kCorruptedElf);
  uintptr_t sym_addr;
  uintptr_t sym_offset;
  if (!safe_mul(symbol_index, sizeof(NativeElfSym), sym_offset) ||
      !safe_add(image.symtab, sym_offset, sym_addr))
    return Error::Make(ErrorCode::kIntegerOverflow);
  if (!readable(image, sym_addr, sizeof(NativeElfSym))) return Error::Make(ErrorCode::kOutOfBounds);
  NativeElfSym sym;
  std::memcpy(&sym, reinterpret_cast<const void*>(sym_addr), sizeof(sym));
  if (sym.st_name >= image.strsz) return Error::Make(ErrorCode::kCorruptedElf);
  const char* begin = reinterpret_cast<const char*>(image.strtab + sym.st_name);
  const auto* end = static_cast<const char*>(std::memchr(begin, 0, image.strsz - sym.st_name));
  if (end == nullptr) return Error::Make(ErrorCode::kCorruptedElf);
  if (Error e = state.budget.take_bytes(static_cast<size_t>(end - begin) + 1)) return e;
  name.assign(begin, static_cast<size_t>(end - begin));
  symbol_type = static_cast<uint8_t>(ELF64_ST_TYPE(sym.st_info));
  undefined = sym.st_shndx == kShnUndef;
  return Error::Ok();
}

// Keep one record from any source.  Only JUMP_SLOT and GLOB_DAT with a non-zero
// symbol index are materialized, whichever table holds them (labelled with their
// real source and type); every other record is only counted, so a large table
// of RELATIVE or other relocations retains only its address for alias checking.
Error keep(ScanState& state, const DecodedReloc& reloc) {
  // NONE has no destination. Every other record participates even when it is
  // not a supported function import or has symbol index zero.
  if (reloc.type != 0) {
    if (state.slots.find(reloc.slot_address) != state.slots.end())
      return Error::Make(ErrorCode::kCorruptedElf);
    // Charge the set entry at its node size, not just the 8-byte key (R-10):
    // a red-black node carries parent/left/right links and a color.
    struct SlotNodeCost {
      void* links[3];
      uintptr_t color;
      uintptr_t key;
    };
    if (Error e = state.budget.take_objects<SlotNodeCost>(1)) return e;
    state.slots.insert(reloc.slot_address);
  }
  if ((reloc.type != kRelJumpSlot && reloc.type != kRelGlobDat) || reloc.symbol_index == 0) {
    ++state.result.skipped_relocations;
    return Error::Ok();
  }
  if (reloc.slot_address % kNativeSlotSize != 0 ||
      !state.image.addr_in_writable_load(reloc.slot_address, kNativeSlotSize))
    return Error::Make(ErrorCode::kCorruptedElf);
  if (Error e = ensure_symbols(state)) return e;
  if (Error e = state.budget.take_objects<FunctionReloc>(1)) return e;
  FunctionReloc record;
  if (Error e = read_symbol(state, reloc.symbol_index, record.entry.base.import.symbol,
                            record.entry.symbol_type, record.symbol_undefined))
    return e;
  record.entry.base.import.symbol_index = reloc.symbol_index;
  record.entry.base.import.relocation_index = reloc.index;
  record.entry.base.import.slot_address = reloc.slot_address;
  record.entry.source = to_public_source(reloc.source);
  record.entry.relocation_type = reloc.type;
  record.addend = reloc.addend;
  state.result.records.push_back(std::move(record));
  return Error::Ok();
}

Error visit_record(void* context, const DecodedReloc& reloc) {
  return keep(*static_cast<ScanState*>(context), reloc);
}

}  // namespace

Result<RawFunctionSnapshot> scan_function_imports(const InspectedImage& image, uint32_t sources,
                                                  ScanBudget* shared_budget) {
  if (Error e = validate_sources(sources)) return e;
  // Scanning DT_RELA or DT_ANDROID_RELA refuses an image whose relocation tags
  // are unusable (a repeated tag, a pointer without its size, an overflowing
  // address, or a REL form).  A DT_JMPREL-only scan does not read those tables
  // and, like the legacy path, ignores them.
  if (image.reloc_tags_unusable &&
      (sources & (kSourceNativeDynamic | kSourceNativeAndroidPacked)) != 0)
    return Error::Make(ErrorCode::kUnsupportedFormat);

  RawFunctionSnapshot result;
  ScanBudget local_budget;
  ScanBudget& budget = shared_budget ? *shared_budget : local_budget;
  ScanState state{image, result, budget, {}};

  if ((sources & kSourceJmprel) != 0 && image.jmprel_count != 0) {
    if (Error e = budget.take_records(image.jmprel_count)) return e;
    // inspect() bounded the table within a LOAD; reading it also needs PF_R.
    uintptr_t table_bytes;
    if (!safe_mul(image.jmprel_count, sizeof(NativeElfRelocation), table_bytes))
      return Error::Make(ErrorCode::kIntegerOverflow);
    if (!readable(image, image.jmprel, table_bytes)) return Error::Make(ErrorCode::kOutOfBounds);
    for (size_t i = 0; i < image.jmprel_count; ++i) {
      NativeElfRelocation rela;
      std::memcpy(&rela,
                  reinterpret_cast<const void*>(image.jmprel + i * sizeof(NativeElfRelocation)),
                  sizeof(rela));
      DecodedReloc reloc;
      if (!safe_add(image.load_bias, static_cast<uintptr_t>(rela.r_offset), reloc.slot_address))
        return Error::Make(ErrorCode::kIntegerOverflow);
      reloc.symbol_index = reloc_sym_of(rela.r_info);
      reloc.type = reloc_type_of(rela.r_info);
      reloc.addend = native_reloc_addend(rela);
      reloc.source = RelocSource::kJmprel;
      reloc.index = i;
      if (Error e = keep(state, reloc)) return e;
    }
  }
  if (sources & kSourceNativeDynamic) {
    if (Error e = decode_dynamic_relocations(image, visit_record, &state, &budget)) return e;
  }
  if (sources & kSourceNativeAndroidPacked) {
    if (Error e = decode_android_packed(image, visit_record, &state, &budget)) return e;
  }
  state.slots = {};

  // Preserve the logical budget and its order while attaching versions in place.
  if (Error e = budget.take_objects<ImportInfo>(result.records.size())) return e;
  auto error = resolve_versions_impl(
      image, result.records.size(),
      [&](size_t i) -> ImportInfo& { return result.records[i].entry.base.import; }, [](size_t) {},
      [&](size_t i, SymbolVersion version) {
        result.records[i].entry.base.version = std::move(version);
      },
      budget);
  if (error) return error;
  return result;
}

Result<FunctionImportSnapshot> snapshot_function_imports(const InspectedImage& image,
                                                         uint32_t sources) {
  ScanBudget budget;
  auto scan = scan_function_imports(image, sources, &budget);
  if (!scan) return scan.error();
  FunctionImportSnapshot result;
  result.skipped_relocations = scan.value().skipped_relocations;
  if (Error e = budget.take_objects<FunctionImport>(scan.value().records.size())) return e;
  result.imports.reserve(scan.value().records.size());
  for (auto& record : scan.value().records) result.imports.push_back(std::move(record.entry));
  return result;
}

Error validate_function_selector(const FunctionImportSelector& selector) noexcept {
  if (Error e = validate_selector(selector.base)) return e;
  if (static_cast<uint8_t>(selector.index_source) >
      static_cast<uint8_t>(RelocationSource::kAndroidRel))
    return Error::Make(ErrorCode::kInvalidArgument);
  if (Error e = validate_sources(selector.sources)) return e;
  if (selector.types == 0 || (selector.types & ~(kTypeJumpSlot | kTypeGlobDat)) != 0)
    return Error::Make(ErrorCode::kInvalidArgument);
  return Error::Ok();
}

namespace {

uint32_t source_bit(RelocationSource source) noexcept {
  switch (source) {
    case RelocationSource::kJmprel:
      return kSourceJmprel;
    case RelocationSource::kRela:
      return kSourceRela;
    case RelocationSource::kAndroidRela:
      return kSourceAndroidRela;
    case RelocationSource::kRel:
      return kSourceRel;
    case RelocationSource::kAndroidRel:
      return kSourceAndroidRel;
  }
  return 0;
}

uint32_t type_bit(uint32_t relocation_type) noexcept {
  if (relocation_type == kRelJumpSlot) return kTypeJumpSlot;
  if (relocation_type == kRelGlobDat) return kTypeGlobDat;
  return 0;
}

// Both explicit function relocation types require an undefined STT_FUNC.
bool is_acceptable(const FunctionReloc& record) noexcept {
  if (record.entry.relocation_type != kRelJumpSlot && record.entry.relocation_type != kRelGlobDat)
    return false;
  return record.addend == 0 && record.entry.symbol_type == kSttFunc && record.symbol_undefined;
}

// Does the base (name/version/hidden/index) selector match this record?
bool base_matches(const FunctionReloc& record, const FunctionImportSelector& selector) {
  const auto& entry = record.entry;
  const auto& sel = selector.base;
  if (entry.base.import.symbol != sel.symbol) return false;
  if (sel.relocation_index != ~size_t{0}) {
    if (entry.source != selector.index_source) return false;
    if (entry.base.import.relocation_index != sel.relocation_index) return false;
  }
  if (sel.version_match == VersionMatch::kUnversioned && entry.base.version.index >= 2)
    return false;
  if (sel.version_match == VersionMatch::kExact && entry.base.version.name != sel.version)
    return false;
  if (sel.dependency != nullptr && entry.base.version.dependency != sel.dependency) return false;
  if (sel.hidden == HiddenMatch::kHidden && !entry.base.version.hidden) return false;
  if (sel.hidden == HiddenMatch::kNonHidden && entry.base.version.hidden) return false;
  return true;
}

}  // namespace

bool function_reloc_matches(const FunctionReloc& record,
                            const FunctionImportSelector& selector) noexcept {
  return (source_bit(record.entry.source) & selector.sources) != 0 &&
         (type_bit(record.entry.relocation_type) & selector.types) != 0 &&
         base_matches(record, selector);
}

Result<size_t> select_function_reloc(const RawFunctionSnapshot& scan,
                                     const FunctionImportSelector& selector) {
  if (Error e = validate_function_selector(selector)) return e;
  size_t selected = ~size_t{0};
  bool ambiguous = false;
  for (size_t i = 0; i < scan.records.size(); ++i) {
    const FunctionReloc& record = scan.records[i];
    // Only records in the opted-in sources and types are candidates.
    if (!function_reloc_matches(record, selector)) continue;
    // A matched record that is not hookable fails the whole call: never skip it
    // and silently hook a different slot of the same name.
    if (!is_acceptable(record)) return Error::Make(ErrorCode::kUnsupportedRelocation);
    if (selected != ~size_t{0}) ambiguous = true;
    selected = i;
  }
  if (selected == ~size_t{0}) return Error::Make(ErrorCode::kSymbolNotFound);
  if (ambiguous) return Error::Make(ErrorCode::kAmbiguousSymbol);
  return selected;
}

bool function_reloc_acceptable(const FunctionReloc& record) noexcept {
  return is_acceptable(record);
}

}  // namespace yukiplt::detail

namespace yukiplt {
namespace {
bool same_image(const ImageView& a, const ImageView& b) noexcept {
  return a.load_bias == b.load_bias && a.phdr_table == b.phdr_table &&
         a.phdr_count == b.phdr_count && a.instance_token == b.instance_token;
}
struct OwnedMatch {
  TextMatchKind kind;
  std::string text;
  bool matches(std::string_view value) const noexcept {
    switch (kind) {
      case TextMatchKind::kAny:
        return true;
      case TextMatchKind::kExact:
        return value == text;
      case TextMatchKind::kPrefix:
        return value.substr(0, text.size()) == text;
      case TextMatchKind::kSuffix:
        return value.size() >= text.size() && value.substr(value.size() - text.size()) == text;
      case TextMatchKind::kSubstring:
        return value.find(text) != std::string_view::npos;
    }
    return false;
  }
};
struct OwnedPattern {
  OwnedMatch path;
  OwnedMatch symbol;
  bool matches(const std::string& image_path, const std::string& symbol_name) const noexcept {
    return path.matches(image_path) && symbol.matches(symbol_name);
  }
};
bool valid(const TextMatch& match) noexcept {
  return match.kind <= TextMatchKind::kSubstring &&
         (match.kind != TextMatchKind::kAny || match.text.empty()) &&
         (match.text.empty() || match.text.data() != nullptr);
}
Result<OwnedPattern> copy_pattern(const CandidatePattern& pattern, detail::ScanBudget& budget) {
  if (!valid(pattern.path) || !valid(pattern.symbol))
    return Error::Make(ErrorCode::kInvalidArgument);
  if (Error e = budget.take_bytes(pattern.path.text.size())) return e;
  if (Error e = budget.take_bytes(pattern.symbol.text.size())) return e;
  return OwnedPattern{{pattern.path.kind, std::string(pattern.path.text)},
                      {pattern.symbol.kind, std::string(pattern.symbol.text)}};
}
}  // namespace

static Result<std::vector<FunctionCandidate>> expand_impl(const NamedImage* images, size_t count,
                                                          const CandidateSelection& selection,
                                                          detail::ScanBudget& budget) {
  if (Error e = detail::process_entry()) return e;
  if ((!images && count != 0) || (!selection.exclude && selection.exclude_count != 0))
    return Error::Make(ErrorCode::kNullPointer);
  const FunctionImportSelector validation{{"candidate"}, selection.sources, selection.types};
  if (Error e = detail::validate_function_selector(validation)) return e;
  const CandidateLimits limits = selection.limits;
  if (limits.text_bytes > CandidateLimits{}.text_bytes ||
      limits.candidates > CandidateLimits{}.candidates)
    return Error::Make(ErrorCode::kInvalidArgument);
  const auto predicate = selection.predicate;
  void* const context = selection.context;
  const uint32_t sources = selection.sources, types = selection.types;
  try {
    auto include = copy_pattern(selection.include, budget);
    if (!include) return include.error();
    std::vector<OwnedPattern> exclusions;
    if (Error e = budget.take_objects<OwnedPattern>(selection.exclude_count)) return e;
    exclusions.reserve(selection.exclude_count);
    for (size_t i = 0; i < selection.exclude_count; ++i) {
      auto pattern = copy_pattern(selection.exclude[i], budget);
      if (!pattern) return pattern.error();
      exclusions.push_back(std::move(pattern.value()));
    }
    struct Snapshot {
      ImageView image;
      std::string path;
      detail::RawFunctionSnapshot imports;
    };
    std::vector<Snapshot> snapshots;
    if (Error e = budget.take_objects<Snapshot>(count)) return e;
    snapshots.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      if (images[i].image.instance_token == 0 && images[i].path.find('\0') != std::string::npos)
        return Error::Make(ErrorCode::kImageInvalidated);
      for (size_t j = 0; j < i; ++j)
        if (same_image(images[i].image, images[j].image))
          return Error::Make(ErrorCode::kInvalidArgument);
      if (Error e = budget.take_bytes(images[i].path.size() + 1)) return e;
      auto snapshot = detail::inspect_function_relocs(images[i].image, sources, &budget,
                                                      images[i].path.c_str());
      if (!snapshot) return snapshot.error();
      snapshots.push_back({images[i].image, images[i].path, std::move(snapshot.value())});
    }
    // Every snapshot call has returned: neither the linker nor the op lock is
    // held below. No user callback runs during snapshot acquisition.
    std::vector<FunctionCandidate> result;
    size_t text_remaining = limits.text_bytes;
    for (auto& snapshot : snapshots) {
      for (auto& record : snapshot.imports.records) {
        const auto exact = exact_function_selector(record.entry);
        if ((exact.types & types) == 0) continue;
        const auto& symbol = record.entry.base.import.symbol;
        if (snapshot.path.size() > text_remaining)
          return detail::with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget,
                                    budget.cause);
        text_remaining -= snapshot.path.size();
        if (symbol.size() > text_remaining)
          return detail::with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget,
                                    budget.cause);
        text_remaining -= symbol.size();
        if (!include.value().matches(snapshot.path, symbol)) continue;
        bool excluded = false;
        for (const auto& pattern : exclusions)
          if (pattern.matches(snapshot.path, symbol)) {
            excluded = true;
            break;
          }
        if (excluded) continue;
        if (!detail::function_reloc_acceptable(record))
          return Error::Make(ErrorCode::kUnsupportedRelocation);
        if (Error e = budget.take_objects<FunctionCandidate>(1)) return e;
        if (Error e = budget.take_bytes(snapshot.path.size() + 1)) return e;
        FunctionCandidate candidate{snapshot.image, snapshot.path, std::move(record.entry)};
        if (predicate) {
          Result<bool> keep = Error::Make(ErrorCode::kUnsupportedFormat);
          try {
            keep = predicate(candidate, context);
          } catch (...) {
            return detail::current_exception_error(detail::ExceptionOrigin::kHostCallback,
                                                   budget.cause);
          }
          if (!keep)
            return keep.error().ok() ? detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                                          ErrorCause::kHostCallback, budget.cause)
                                     : keep.error();
          if (!keep.value()) continue;
        }
        if (result.size() == limits.candidates)
          return detail::with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget,
                                    budget.cause);
        result.push_back(std::move(candidate));
      }
    }
    std::vector<uintptr_t> addresses;
    if (Error e = budget.take_objects<uintptr_t>(result.size())) return e;
    addresses.reserve(result.size());
    for (const auto& candidate : result)
      addresses.push_back(candidate.selected.base.import.slot_address);
    std::sort(addresses.begin(), addresses.end());
    if (std::adjacent_find(addresses.begin(), addresses.end()) != addresses.end())
      return detail::with_cause(Error::Make(ErrorCode::kInvalidArgument),
                                ErrorCause::kDuplicateTarget, budget.cause);
    return result;
  } catch (...) {
    return detail::current_exception_error(detail::ExceptionOrigin::kInternal, budget.cause);
  }
}

Result<std::vector<FunctionCandidate>> detail::expand_candidates_budgeted(
    const NamedImage* images, size_t count, const CandidateSelection& selection, ScanBudget& budget,
    OperationDiagnostic* diagnostic) {
  detail::CauseContext context;
  auto local_budget = budget;
  local_budget.cause = &context;
  auto result = expand_impl(images, count, selection, local_budget);
  budget.bytes = local_budget.bytes;
  budget.relocations = local_budget.relocations;
  if (diagnostic) {
    *diagnostic = {};
    diagnostic->error = result ? Error::Ok() : result.error();
    diagnostic->stage = result ? OperationStage::kComplete : OperationStage::kPrepare;
    diagnostic->cause = detail::resolve_cause(diagnostic->error, context.value);
  }
  return result;
}
Result<std::vector<FunctionCandidate>> expand_function_candidates(
    const NamedImage* images, size_t count, const CandidateSelection& selection) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  detail::ScanBudget budget;
  return expand_impl(images, count, selection, budget);
}
Result<std::vector<FunctionCandidate>> expand_function_candidates_diagnosed(
    const NamedImage* images, size_t count, const CandidateSelection& selection,
    OperationDiagnostic* diagnostic) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) {
    yukiplt::detail::admission_diagnostic(diagnostic, admission.error());
    return admission.error();
  }

  detail::ScanBudget budget;
  return detail::expand_candidates_budgeted(images, count, selection, budget, diagnostic);
}
}  // namespace yukiplt
