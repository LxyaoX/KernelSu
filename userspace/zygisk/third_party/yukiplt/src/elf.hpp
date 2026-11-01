// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_ELF_HPP_
#define YUKIPLT_ELF_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "common.hpp"
#include "yukiplt.hpp"

namespace yukiplt::detail {

/// A validated PT_LOAD segment with runtime addresses.
struct LoadSegment {
  uintptr_t vaddr_begin;  ///< Runtime start address (vaddr + load_bias).
  uintptr_t vaddr_end;    ///< Runtime end address (exclusive).
  uint32_t flags;         ///< Segment flags (PF_R | PF_W | PF_X).
};

/// Result of inspecting an ELF image.  All addresses are runtime addresses.
/// All ranges have been validated: no overflow, within PT_LOAD bounds.
struct InspectedImage {
  uintptr_t load_bias = 0;

  /// PT_LOAD segments, sorted by vaddr_begin.
  SmallVec<LoadSegment> load_segments;

  /// Symbol table (DT_SYMTAB runtime address).
  uintptr_t symtab = 0;

  /// String table (DT_STRTAB runtime address and DT_STRSZ).
  uintptr_t strtab = 0;
  size_t strsz = 0;

  /// Hash tables (zero if absent; at least one must be non-zero).
  uintptr_t gnu_hash = 0;
  uintptr_t sysv_hash = 0;

  /// PLT relocation table (DT_JMPREL runtime address and entry count).
  uintptr_t jmprel = 0;
  size_t jmprel_count = 0;

  // Unvalidated native dynamic-relocation metadata. Only explicit function
  // scans validate it; malformed optional tables must not break JUMP_SLOT APIs.
  uintptr_t rela = 0;
  size_t rela_size = 0;

  /// Android packed relocations (DT_ANDROID_RELA): raw runtime address and the
  /// declared byte size.  Decoded and validated only on the opt-in path.
  uintptr_t android_rela = 0;
  size_t android_rela_size = 0;

  /// True if a RELA-family tag (DT_RELA/RELASZ, DT_ANDROID_RELA/SZ) appeared
  /// more than once or without its pair, its address overflowed, or an unusable
  /// REL form (DT_REL/DT_ANDROID_REL) was present.  The opt-in path rejects such
  /// an image when it scans DT_RELA or DT_ANDROID_RELA; hook() ignores it.
  bool reloc_tags_unusable = false;

  /// DT_RELAENT as declared (0 if absent).  Validated on the opt-in path.
  size_t rela_ent = 0;

  /// Validated dynamic array for optional metadata readers.
  uintptr_t dynamic = 0;
  size_t dynamic_size = 0;

  /// Whether d_ptr values in the dynamic section were already adjusted
  /// to runtime addresses by the dynamic linker (glibc behavior).
  /// When false (bionic), runtime address = d_ptr + load_bias.
  bool dptr_adjusted = false;

  // --- Query methods ---

  /// Returns true if [addr, addr + size) falls entirely within a PT_LOAD segment.
  [[nodiscard]] bool addr_in_load(uintptr_t addr, size_t size) const noexcept;

  /// Returns true if [addr, addr + size) falls entirely within a PT_LOAD
  /// segment whose p_flags include PF_W.  A relocation target (GOT slot) must
  /// satisfy this: a slot inside a non-writable segment means a malformed or
  /// hostile r_offset, and patching it would write into read-only data.
  [[nodiscard]] bool addr_in_writable_load(uintptr_t addr, size_t size) const noexcept;
};

// Inspect bounded native ELF metadata. The caller keeps the mapping and
// program-header table valid throughout the call.
Expected<InspectedImage, Error> inspect(uintptr_t load_bias, const NativeElfPhdr* phdr_table,
                                        size_t phdr_count);

/// Compute the GNU hash of a symbol name (dl_new_hash).
uint32_t gnu_hash_value(const char* name) noexcept;

/// Compute the SysV ELF hash of a symbol name.
uint32_t elf_hash_value(const char* name) noexcept;

/// Look up a symbol by name using the GNU hash table.
///
/// Returns the symbol index in the symbol table, or std::nullopt if not found.
/// Returns an Error if the hash table structure is corrupted.
Expected<std::optional<uint32_t>, Error> gnu_hash_lookup(const InspectedImage& image,
                                                         const char* name);

/// Look up a symbol by name using the SysV hash table.
Expected<std::optional<uint32_t>, Error> sysv_hash_lookup(const InspectedImage& image,
                                                          const char* name);

/// Unified symbol lookup: tries GNU hash first, falls back to SysV hash.
///
/// Returns kNoHashTable if neither hash table is present.
/// Returns kSymbolNotFound if the symbol is not in any hash table.
Expected<uint32_t, Error> lookup_symbol(const InspectedImage& image, const char* name);

/// Resolved GOT slot information.
struct ResolvedSlot {
  uintptr_t address;   ///< Runtime address of the GOT slot.
  uint32_t sym_index;  ///< Symbol index that this slot references.
};

// Validate all JMPREL entries and return matching records in table order.
// Duplicate slots are corrupt; no matches yields SymbolNotFound. Allocation
// exceptions are translated by the public boundary.
Expected<SmallVec<ResolvedSlot>, Error> resolve_slots(const InspectedImage& image,
                                                      const char* symbol_name);

// Resolve one slot: no match is SymbolNotFound, duplicate records are
// CorruptedElf, and multiple distinct slots are AmbiguousSymbol.
Expected<ResolvedSlot, Error> resolve_unique_slot(const InspectedImage& image,
                                                  const char* symbol_name);

// Dynamic tags for the explicit native REL/RELA and Android-packed sources.
// RELR is not scanned: its records do not participate in skipped counts or
// duplicate-slot checks across these selected sources. glibc's <elf.h> does not
// define the DT_ANDROID_* tags, so we name them here regardless of platform.
constexpr int64_t kDtRela = 7;
constexpr int64_t kDtRelaSz = 8;
constexpr int64_t kDtRelaEnt = 9;
constexpr int64_t kDtRel = 17;  // ELF REL (no addend): never valid on AArch64.
constexpr int64_t kDtRelSz = 18;
constexpr int64_t kDtRelEnt = 19;
constexpr int64_t kDtAndroidRel = 0x6000000f;
constexpr int64_t kDtAndroidRelSz = 0x60000010;
constexpr int64_t kDtAndroidRela = 0x60000011;
constexpr int64_t kDtAndroidRelaSz = 0x60000012;

constexpr int64_t kDtNativeRel = kNativeUsesRela ? kDtRela : kDtRel;
constexpr int64_t kDtNativeRelSz = kNativeUsesRela ? kDtRelaSz : kDtRelSz;
constexpr int64_t kDtNativeRelEnt = kNativeUsesRela ? kDtRelaEnt : kDtRelEnt;
constexpr int64_t kDtNativeAndroidRel = kNativeUsesRela ? kDtAndroidRela : kDtAndroidRel;
constexpr int64_t kDtNativeAndroidRelSz = kNativeUsesRela ? kDtAndroidRelaSz : kDtAndroidRelSz;
constexpr int64_t kDtForeignRel = kNativeUsesRela ? kDtRel : kDtRela;
constexpr int64_t kDtForeignRelSz = kNativeUsesRela ? kDtRelSz : kDtRelaSz;
constexpr int64_t kDtForeignRelEnt = kNativeUsesRela ? kDtRelEnt : kDtRelaEnt;
constexpr int64_t kDtForeignAndroidRel = kNativeUsesRela ? kDtAndroidRel : kDtAndroidRela;
constexpr int64_t kDtForeignAndroidRelSz = kNativeUsesRela ? kDtAndroidRelSz : kDtAndroidRelaSz;

// AArch64 dynamic relocation types.
constexpr uint32_t kRelAbsolute = kNativeAbsolute;
constexpr uint32_t kRelGlobDat = kNativeGlobDat;
constexpr uint32_t kRelJumpSlot = kNativeJumpSlot;
constexpr uint32_t kRelRelative = kNativeRelative;
constexpr uint32_t kRAarch64TlsTprel = 1030;
constexpr uint32_t kRAarch64TlsDesc = 1031;
constexpr uint32_t kRelIRelative = kNativeIRelative;

// ELF symbol types (st_info low nibble).
constexpr uint8_t kSttNotype = 0;
constexpr uint8_t kSttObject = 1;
constexpr uint8_t kSttFunc = 2;
constexpr uint8_t kSttTls = 6;
constexpr uint8_t kSttGnuIfunc = 10;

constexpr uint16_t kShnUndef = 0;

// Android packed relocation group flags (APS2 "APS2" format).
constexpr uint64_t kRelocGroupedByInfo = 1;
constexpr uint64_t kRelocGroupedByOffsetDelta = 2;
constexpr uint64_t kRelocGroupedByAddend = 4;
constexpr uint64_t kRelocGroupHasAddend = 8;

// APS2 magic: the four bytes 'A','P','S','2'.
constexpr uint8_t kAps2Magic[4] = {0x41, 0x50, 0x53, 0x32};

// Resource budget for a single packed stream (maintainer decision): reject,
// without reading the whole stream, a header that claims an implausible number
// of relocations or a table larger than this many bytes. These are resource
// limits (kOutOfBounds), not proof the ELF is malformed.
constexpr uint64_t kMaxPackedRelocations = uint64_t{1} << 22;
constexpr size_t kMaxPackedBytes = size_t{64} << 20;

inline uint32_t reloc_type_of(uint64_t r_info) noexcept {
  return static_cast<uint32_t>(native_reloc_type(r_info));
}
inline uint32_t reloc_sym_of(uint64_t r_info) noexcept {
  return static_cast<uint32_t>(native_reloc_symbol(r_info));
}

// Stream native and APS2 relocations through validated readable LOADs.
// On failure the caller discards all collected records. Legacy JMPREL uses imports.cpp.

/// Where a relocation record was found.
enum class RelocSource : uint8_t { kJmprel, kRela, kAndroidRela, kRel, kAndroidRel };

/// One decoded RELA-form relocation (the fields the function-import path needs).
struct DecodedReloc {
  uintptr_t slot_address = 0;  ///< Runtime address of the GOT slot (r_offset + bias).
  uint32_t symbol_index = 0;   ///< native_reloc_symbol(r_info).
  uint32_t type = 0;           ///< native_reloc_type(r_info).
  int64_t addend = 0;          ///< r_addend.
  RelocSource source = RelocSource::kRela;
  size_t index = 0;  ///< Index within its own source table (decode order for APS2).
};

/// Receives each decoded record in table order.  Returning an error stops the
/// decode and is passed through unchanged.  May throw (e.g. std::bad_alloc);
/// the decoder holds no state that an exception could leave inconsistent.
using RelocVisitor = Error (*)(void* context, const DecodedReloc& record);

// Validate native relocation entry size, readable interval and slot arithmetic.
// Visit all types; omit only a wholly contained JMPREL tail with the same end.
Error decode_dynamic_relocations(const InspectedImage& image, RelocVisitor visit, void* context,
                                 ScanBudget* budget = nullptr);

// Decode bounded APS2. Reject malformed SLEB128 and offset/addend overflow;
// allow only zero padding after the declared records.
Error decode_android_packed(const InspectedImage& image, RelocVisitor visit, void* context,
                            ScanBudget* budget = nullptr);

}  // namespace yukiplt::detail

#endif  // YUKIPLT_ELF_HPP_
