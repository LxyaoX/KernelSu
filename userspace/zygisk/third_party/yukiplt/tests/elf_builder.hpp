// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

/// \brief Test helper: construct ELF images in memory.
///
/// ElfBuilder allocates an aligned buffer and writes ELF structures into it.
/// The resulting buffer has p_vaddr = 0 for its PT_LOAD segment, so
/// load_bias == buffer base address.  This allows the inspector to work
/// on Host (any architecture) using constructed byte buffers.

#ifndef YUKIPLT_TEST_ELF_BUILDER_HPP_
#define YUKIPLT_TEST_ELF_BUILDER_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "elf.hpp"
#include "yukiplt.hpp"

namespace yukiplt::test {

inline const char* synthetic_maps_line(bool read_only) {
#if YUKIPLT_ELF_BITS == 64
  return read_only ? "1000-fffffffffffff000 r--p 0000 00:00 0"
                   : "1000-fffffffffffff000 rw-p 0000 00:00 0";
#else
  return read_only ? "1000-fffff000 r--p 0000 00:00 0" : "1000-fffff000 rw-p 0000 00:00 0";
#endif
}

inline void set_test_addend(NativeElfRelocation& relocation, int64_t addend) {
#if YUKIPLT_USES_RELA
  relocation.r_addend = addend;
#else
  (void)relocation;
  // Native REL binding entries have effective addend zero.
  (void)addend;
#endif
}

inline NativeElfRelocation test_relocation(uintptr_t offset, NativeElfAddr info) {
  NativeElfRelocation result{};
  result.r_offset = offset;
  result.r_info = info;
  return result;
}

/// Build a minimal ELF image in memory for testing the inspector.
///
/// Usage:
///   ElfBuilder b;
///   b.add_symbol("foo", 1);
///   b.add_jump_slot("foo", 1, 0);  // sym index 1, addend 0
///   b.set_gnu_hash(true);
///   auto img = b.build();
///   // img.phdr_table(), img.phdr_count(), img.load_bias() are ready.
///
/// The builder lays out structures in a flat buffer:
///   [phdr_table | dynamic_section | strtab | symtab | gnu_hash | sysv_hash | jmprel | got]
///
/// Everything lives in one PT_LOAD segment with p_vaddr = 0,
/// so load_bias = buffer base address.
class ElfBuilder {
 public:
  /// A symbol to place in the symbol table.
  struct SymEntry {
    std::string name;
    uint32_t sym_index;  ///< Desired index in symtab (0 is reserved for STN_UNDEF).
  };

  /// A JUMP_SLOT relocation entry.
  struct JmpSlotEntry {
    uint32_t sym_index;
    int64_t addend;
    // The GOT slot address will be assigned by build().
  };

  // --- Configuration ---

  ElfBuilder& with_elf_header() {
    emit_header_ = true;
    return *this;
  }

  ElfBuilder& add_symbol(const char* name, uint32_t index) {
    symbols_.push_back({name, index});
    return *this;
  }

  ElfBuilder& add_jump_slot(uint32_t sym_index, int64_t addend = 0) {
    jmpslots_.push_back({sym_index, addend});
    return *this;
  }

  /// Emit a second JUMP_SLOT record whose r_offset aliases the GOT slot of a
  /// previously added jump slot (by 0-based index).  Used to build malformed
  /// images with duplicate relocation records pointing at one slot.
  ElfBuilder& alias_jump_slot(uint32_t sym_index, size_t target_jmpslot_index) {
    aliased_slots_.push_back({sym_index, target_jmpslot_index});
    return *this;
  }

  /// Add a relocation with a custom type (for testing unsupported types).
  ElfBuilder& add_custom_rela(uint32_t sym_index, uint32_t rela_type, int64_t addend = 0) {
    custom_relas_.push_back({sym_index, rela_type, addend});
    return *this;
  }

  /// Override a symbol's type (STT_*) and binding (STB_*).  By default every
  /// symbol is STB_GLOBAL/STT_FUNC and defined (st_shndx = 1).
  ElfBuilder& set_symbol_type(uint32_t index, uint8_t stt, uint8_t stb = STB_GLOBAL) {
    symbol_info_[index] = ELF64_ST_INFO(stb, stt);
    return *this;
  }

  /// Mark a symbol undefined (st_shndx = SHN_UNDEF), as an imported symbol is.
  ElfBuilder& set_symbol_undefined(uint32_t index, bool undefined = true) {
    symbol_undef_[index] = undefined;
    return *this;
  }

  /// Add a record to a separate DT_RELA (.rela.dyn) table with its own GOT slot.
  /// Returns *this; the slot index within the RELA table is its insertion order.
  ElfBuilder& add_rela(uint32_t sym_index, uint32_t rela_type, int64_t addend = 0) {
    rela_entries_.push_back({sym_index, rela_type, addend});
    return *this;
  }

  /// Add a DT_RELA record whose r_offset aliases the GOT slot of a previously
  /// added DT_RELA entry (by 0-based index in the RELA table).
  ElfBuilder& alias_rela(uint32_t sym_index, uint32_t rela_type, size_t target_rela_index) {
    rela_aliases_.push_back({sym_index, rela_type, target_rela_index});
    return *this;
  }

  /// Make the DT_RELA table fully contain the JMPREL table, sharing the same end
  /// address, as some linkers emit (DT_RELASZ covering the PLT relocations).
  /// (Overlap decoding is covered directly against decode_dynamic_relocations in the tests; the
  /// builder lays the two tables out separately, so this flag is reserved.)
  ElfBuilder& set_rela_includes_jmprel(bool enable = true) {
    rela_includes_jmprel_ = enable;
    return *this;
  }

  /// Override DT_RELAENT (default sizeof(yukiplt::NativeElfRelocation)).
  ElfBuilder& set_relaent(uint64_t value) {
    relaent_override_ = value;
    has_relaent_override_ = true;
    return *this;
  }

  /// Emit a raw DT_ANDROID_RELA table with these exact bytes and its own GOT
  /// slots.  `slot_count` reserves that many 8-byte slots so encoded offsets
  /// have a writable target; the encoder supplies the offsets.
  ElfBuilder& set_android_rela(std::vector<uint8_t> bytes, size_t slot_count) {
    android_rela_bytes_ = std::move(bytes);
    android_rela_slots_ = slot_count;
    has_android_rela_ = true;
    return *this;
  }

  /// Emit DT_REL (no addend) instead of validating — used to prove the opt-in
  /// path rejects REL-form images.
  ElfBuilder& emit_spurious_rel_tag(bool enable = true) {
    emit_rel_tag_ = enable;
    return *this;
  }

  ElfBuilder& set_gnu_hash(bool enable) {
    use_gnu_hash_ = enable;
    return *this;
  }
  ElfBuilder& set_sysv_hash(bool enable) {
    use_sysv_hash_ = enable;
    return *this;
  }

  /// Override DT_PLTREL (default: DT_RELA).
  ElfBuilder& set_pltrel(uint64_t val) {
    pltrel_override_ = val;
    return *this;
  }

  /// Override DT_SYMENT (default: sizeof(yukiplt::NativeElfSym)).
  ElfBuilder& set_syment(uint64_t val) {
    syment_override_ = val;
    return *this;
  }

  /// Mark the entire PT_LOAD as RELRO.
  ElfBuilder& set_relro(bool enable) {
    use_relro_ = enable;
    return *this;
  }

  /// Keep only the first `max_entries` dynamic tags, still followed by DT_NULL
  /// (for testing missing tags).  0 means no truncation.
  ElfBuilder& truncate_dynamic(size_t max_entries) {
    truncate_dyn_ = max_entries;
    return *this;
  }

  /// Inject an overflow-inducing vaddr for PT_LOAD (for testing).
  ElfBuilder& set_load_vaddr(uint64_t vaddr) {
    load_vaddr_override_ = vaddr;
    has_load_vaddr_override_ = true;
    return *this;
  }

  /// Remove all hash tables (for testing kNoHashTable).
  ElfBuilder& set_no_hash() {
    use_gnu_hash_ = false;
    use_sysv_hash_ = false;
    return *this;
  }

  /// Omit DT_SYMTAB from the dynamic section (for testing).
  ElfBuilder& omit_symtab(bool v = true) {
    omit_symtab_ = v;
    return *this;
  }

  /// Omit DT_STRTAB from the dynamic section (for testing).
  ElfBuilder& omit_strtab(bool v = true) {
    omit_strtab_ = v;
    return *this;
  }

  /// Omit DT_STRSZ from the dynamic section (for testing).
  ElfBuilder& omit_strsz(bool v = true) {
    omit_strsz_ = v;
    return *this;
  }

  /// Omit the DT_NULL terminator so the dynamic array runs to the segment end.
  ElfBuilder& omit_dt_null(bool v = true) {
    omit_dt_null_ = v;
    return *this;
  }

  /// Override the PT_LOAD p_flags (default: PF_R | PF_W).  Used to place the
  /// GOT in a segment that is not writable.
  ElfBuilder& set_load_flags(uint32_t flags) {
    load_flags_ = flags;
    return *this;
  }

  /// Emit two PT_LOADs instead of one: a read-only first LOAD with the headers,
  /// strings, symbols and hash tables, and a second LOAD starting at the
  /// relocation tables and GOT (through the dynamic section) with `flags`.
  /// Exercises "slot must lie in a PF_W LOAD" against a real neighbor that is
  /// writable or not, instead of flipping the flags of one whole image.
  ElfBuilder& set_split_got_load(uint32_t flags) {
    split_got_load_ = true;
    got_load_flags_ = flags;
    return *this;
  }

  // --- Build ---

  struct BuiltImage {
    std::vector<uint8_t> buffer;
    size_t phdr_offset = 0;
    size_t phdr_count = 0;

    uintptr_t load_bias() const { return reinterpret_cast<uintptr_t>(buffer.data()); }

    const yukiplt::NativeElfPhdr* phdr_table() const {
      return reinterpret_cast<const yukiplt::NativeElfPhdr*>(buffer.data() + phdr_offset);
    }

    /// Address of the GOT slot for the i-th JUMP_SLOT relocation.
    uintptr_t got_slot_addr(size_t i) const;

    /// Address of the i-th separate DT_RELA table's GOT slot.
    uintptr_t rela_slot_addr(size_t i) const;

    /// Address of the i-th DT_ANDROID_RELA GOT slot.
    uintptr_t android_slot_addr(size_t i) const;

    size_t got_offset = 0;          // Offset of JMPREL GOT in buffer
    size_t rela_got_offset = 0;     // Offset of DT_RELA GOT in buffer
    size_t android_got_offset = 0;  // Offset of DT_ANDROID_RELA GOT in buffer
  };

  /// Build the image.  The returned BuiltImage owns the buffer.
  BuiltImage build() const;

 private:
  struct CustomRela {
    uint32_t sym_index;
    uint32_t rela_type;
    int64_t addend;
  };

  /// A JUMP_SLOT record whose r_offset aliases another jump slot's GOT slot.
  struct AliasedSlot {
    uint32_t sym_index;
    size_t target_jmpslot_index;
  };

  /// A record in a separate DT_RELA table.
  struct RelaEntry {
    uint32_t sym_index;
    uint32_t rela_type;
    int64_t addend;
  };

  /// A DT_RELA record aliasing another RELA entry's GOT slot.
  struct RelaAlias {
    uint32_t sym_index;
    uint32_t rela_type;
    size_t target_rela_index;
  };

  std::vector<SymEntry> symbols_;
  std::vector<JmpSlotEntry> jmpslots_;
  std::vector<CustomRela> custom_relas_;
  std::vector<AliasedSlot> aliased_slots_;
  std::vector<RelaEntry> rela_entries_;
  std::vector<RelaAlias> rela_aliases_;
  std::map<uint32_t, uint8_t> symbol_info_;
  std::map<uint32_t, bool> symbol_undef_;
  std::vector<uint8_t> android_rela_bytes_;
  size_t android_rela_slots_ = 0;
  bool has_android_rela_ = false;
  bool rela_includes_jmprel_ = false;
  uint64_t relaent_override_ = 0;
  bool has_relaent_override_ = false;
  bool emit_rel_tag_ = false;
  bool use_gnu_hash_ = true;
  bool emit_header_ = false;
  bool use_sysv_hash_ = false;
  bool split_got_load_ = false;
  uint32_t got_load_flags_ = 0;
  bool use_relro_ = false;
  size_t truncate_dyn_ = 0;
  uint64_t pltrel_override_ = detail::kDtNativeRel;
  uint64_t syment_override_ = sizeof(yukiplt::NativeElfSym);
  uint64_t load_vaddr_override_ = 0;
  bool has_load_vaddr_override_ = false;
  bool omit_symtab_ = false;
  bool omit_strtab_ = false;
  bool omit_strsz_ = false;
  bool omit_dt_null_ = false;
  uint32_t load_flags_ = PF_R | PF_W;
};

}  // namespace yukiplt::test

#endif  // YUKIPLT_TEST_ELF_BUILDER_HPP_
