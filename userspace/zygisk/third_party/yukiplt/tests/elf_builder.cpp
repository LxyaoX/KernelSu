// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "elf_builder.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace yukiplt::test {
// GNU Hash helpers

/// Compute GNU hash of a symbol name (dl_new_hash).
static uint32_t gnu_hash(const char* name) {
  uint32_t h = 5381;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
    h = (h << 5) + h + *p;
  return h;
}

/// Compute SysV ELF hash.  The builder's own copy of the gABI algorithm, so
/// the bucket a symbol is placed in does not depend on the code under test.
static uint32_t elf_hash(const char* name) {
  uint32_t h = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
    h = (h << 4) + *p;
    uint32_t g = h & 0xf0000000;
    if (g) h ^= g >> 24;
    h &= ~g;
  }
  return h;
}
// Layout helper

/// Simple bump allocator for laying out structures in a flat buffer.
struct LayoutBuilder {
  std::vector<uint8_t> buf;
  size_t pos = 0;

  size_t align(size_t alignment) {
    size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
    if (aligned > buf.size()) buf.resize(aligned, 0);
    pos = aligned;
    return pos;
  }

  size_t alloc(size_t size, size_t alignment = 1) {
    align(alignment);
    size_t offset = pos;
    pos += size;
    if (pos > buf.size()) buf.resize(pos, 0);
    return offset;
  }

  template <typename T>
  void write_at(size_t offset, const T& val) {
    assert(offset + sizeof(T) <= buf.size());
    std::memcpy(buf.data() + offset, &val, sizeof(T));
  }

  template <typename T>
  size_t append(const T& val, size_t alignment = alignof(T)) {
    size_t off = alloc(sizeof(T), alignment);
    write_at(off, val);
    return off;
  }
};
// ElfBuilder::BuiltImage

uintptr_t ElfBuilder::BuiltImage::got_slot_addr(size_t i) const {
  return reinterpret_cast<uintptr_t>(buffer.data() + got_offset + i * kNativeSlotSize);
}

uintptr_t ElfBuilder::BuiltImage::rela_slot_addr(size_t i) const {
  return reinterpret_cast<uintptr_t>(buffer.data() + rela_got_offset + i * kNativeSlotSize);
}

uintptr_t ElfBuilder::BuiltImage::android_slot_addr(size_t i) const {
  return reinterpret_cast<uintptr_t>(buffer.data() + android_got_offset + i * kNativeSlotSize);
}
// ElfBuilder::build

ElfBuilder::BuiltImage ElfBuilder::build() const {
  LayoutBuilder lb;

  // Reserve generous space upfront to avoid excessive reallocs.
  lb.buf.resize(4096, 0);
  // 1. Determine the number of phdrs.
  size_t num_phdrs = split_got_load_ ? 2 : 1;  // PT_LOAD(s)
  num_phdrs += 1;                              // PT_DYNAMIC
  if (use_relro_) num_phdrs += 1;              // PT_GNU_RELRO
  // 2. Program headers (at the start of the buffer).
  if (emit_header_) lb.alloc(sizeof(NativeElfEhdr), alignof(NativeElfEhdr));
  size_t phdr_offset = lb.alloc(num_phdrs * sizeof(yukiplt::NativeElfPhdr), alignof(NativeElfPhdr));
  if (emit_header_) {
    NativeElfEhdr header{};
    std::memcpy(header.e_ident, "\177ELF", 4);
    header.e_ident[EI_CLASS] = YUKIPLT_ELF_CLASS;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_machine = kNativeMachine;
    header.e_type = ET_DYN;
    header.e_version = EV_CURRENT;
    header.e_ehsize = sizeof(header);
    header.e_phentsize = sizeof(NativeElfPhdr);
    header.e_phoff = phdr_offset;
    header.e_phnum = static_cast<NativeElfHalf>(num_phdrs);
    lb.write_at(0, header);
  }
  // 3. String table (\0 + symbol names).
  size_t strtab_offset = lb.align(1);

  // First byte is always \0 (STN_UNDEF name).
  lb.alloc(1);

  // Map symbol index → name offset in strtab.
  // Find max sym index.
  uint32_t max_sym_index = 0;
  for (const auto& s : symbols_)
    if (s.sym_index > max_sym_index) max_sym_index = s.sym_index;

  std::vector<uint32_t> name_offsets(max_sym_index + 1, 0);
  for (const auto& s : symbols_) {
    name_offsets[s.sym_index] = static_cast<uint32_t>(lb.pos - strtab_offset);
    size_t name_off = lb.alloc(s.name.size() + 1);
    std::memcpy(lb.buf.data() + name_off, s.name.c_str(), s.name.size() + 1);
  }

  size_t strtab_size = lb.pos - strtab_offset;
  // 4. Symbol table (index 0 = STN_UNDEF, then up to max_sym_index).
  size_t symtab_offset = lb.align(8);
  size_t sym_count = max_sym_index + 1;
  lb.alloc(sym_count * sizeof(yukiplt::NativeElfSym), 8);

  // Fill in symbols.
  for (size_t i = 0; i < sym_count; ++i) {
    yukiplt::NativeElfSym sym{};
    sym.st_name = name_offsets[i];
    auto info_it = symbol_info_.find(static_cast<uint32_t>(i));
    sym.st_info =
        info_it != symbol_info_.end() ? info_it->second : ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    auto undef_it = symbol_undef_.find(static_cast<uint32_t>(i));
    const bool undefined = (i == 0) || (undef_it != symbol_undef_.end() && undef_it->second);
    sym.st_shndx = undefined ? SHN_UNDEF : 1;
    sym.st_value = 0;
    sym.st_size = 0;
    lb.write_at(symtab_offset + i * sizeof(yukiplt::NativeElfSym), sym);
  }
  // 5. GNU Hash table (optional).
  size_t gnu_hash_offset = 0;
  if (use_gnu_hash_) {
    gnu_hash_offset = lb.align(8);

    // Simple layout: 1 bucket, symoffset = 1 (skip STN_UNDEF),
    // 1 bloom word, bloom_shift = 6.
    //
    // GNU Hash header: { nbuckets, symoffset, bloom_size, bloom_shift }
    // Followed by: bloom[bloom_size], buckets[nbuckets], chains[]
    //
    // The chain has one entry per symbol from symoffset to max_sym_index.

    uint32_t nbuckets = 1;
    uint32_t symoffset = 1;  // First hashed symbol index
    uint32_t bloom_size = 1;
    uint32_t bloom_shift = 6;

    // Header
    lb.append<uint32_t>(nbuckets, 4);
    lb.append<uint32_t>(symoffset, 4);
    lb.append<uint32_t>(bloom_size, 4);
    lb.append<uint32_t>(bloom_shift, 4);

    NativeElfAddr bloom = 0;
    constexpr uint32_t bits = sizeof(NativeElfAddr) * 8;
    for (const auto& symbol : symbols_) {
      const uint32_t hash = gnu_hash(symbol.name.c_str());
      bloom |= (NativeElfAddr{1} << (hash % bits)) |
               (NativeElfAddr{1} << ((hash >> bloom_shift) % bits));
    }
    lb.append<NativeElfAddr>(bloom, alignof(NativeElfAddr));

    // Buckets: for each hashed symbol, bucket[hash % nbuckets] = sym_index.
    // With 1 bucket, everything chains from bucket 0.
    // bucket[0] = symoffset (first symbol).
    size_t bucket_offset = lb.alloc(nbuckets * sizeof(uint32_t), 4);
    if (sym_count > 1) {
      uint32_t first_sym = symoffset;
      lb.write_at(bucket_offset, first_sym);
    } else {
      // No symbols to hash.
      uint32_t zero = 0;
      lb.write_at(bucket_offset, zero);
    }

    // Chains: one uint32_t per symbol [symoffset .. max_sym_index].
    // chain[i - symoffset] = (gnu_hash(name) & ~1) | is_last
    size_t chain_count = (sym_count > symoffset) ? (sym_count - symoffset) : 0;
    size_t chain_offset = lb.alloc(chain_count * sizeof(uint32_t), 4);

    for (size_t ci = 0; ci < chain_count; ++ci) {
      uint32_t sym_idx = static_cast<uint32_t>(symoffset + ci);
      uint32_t h = 0;
      // Find the symbol name for this index.
      for (const auto& s : symbols_) {
        if (s.sym_index == sym_idx) {
          h = gnu_hash(s.name.c_str());
          break;
        }
      }
      uint32_t chain_val = (h & ~uint32_t(1));
      if (ci == chain_count - 1) chain_val |= 1;  // Last entry in chain
      lb.write_at(chain_offset + ci * sizeof(uint32_t), chain_val);
    }
  }
  // 6. SysV Hash table (optional).
  size_t sysv_hash_offset = 0;
  if (use_sysv_hash_) {
    sysv_hash_offset = lb.align(4);

    // Several buckets, so lookup only succeeds if the reader computes the
    // same hash and therefore the same bucket as this independent builder.
    const uint32_t nbucket = 7;
    uint32_t nchain = static_cast<uint32_t>(sym_count);

    lb.append<uint32_t>(nbucket, 4);
    lb.append<uint32_t>(nchain, 4);

    size_t buckets_off = lb.alloc(nbucket * sizeof(uint32_t), 4);
    size_t chains_off = lb.alloc(nchain * sizeof(uint32_t), 4);

    // Each bucket heads a chain of the symbols hashing to it, in index order;
    // chain[i] links to the next symbol of the same bucket, 0 ends it.
    std::vector<std::pair<uint32_t, std::string>> named;
    for (const auto& sym : symbols_) named.emplace_back(sym.sym_index, sym.name);
    std::sort(named.begin(), named.end());
    std::vector<uint32_t> tail(nbucket, 0);
    for (const auto& [index, name] : named) {
      const uint32_t bucket = elf_hash(name.c_str()) % nbucket;
      if (tail[bucket] == 0)
        lb.write_at(buckets_off + bucket * sizeof(uint32_t), index);
      else
        lb.write_at(chains_off + tail[bucket] * sizeof(uint32_t), index);
      lb.write_at(chains_off + index * sizeof(uint32_t), uint32_t{0});
      tail[bucket] = index;
    }
  }
  // 7. JMPREL table (PLT relocations) + GOT slots.
  // With a split layout the second LOAD starts here, on its own page-aligned
  // offset so the two segments never share a byte.
  const size_t second_load_offset = split_got_load_ ? lb.align(0x1000) : 0;
  size_t owned_slots = jmpslots_.size() + custom_relas_.size();
  size_t total_relas = owned_slots + aliased_slots_.size();
  size_t jmprel_offset = 0;
  size_t got_offset = 0;

  if (total_relas > 0) {
    // GOT slots: 8 bytes each, aligned to 8.  Aliased records reuse an
    // existing slot, so only owned records get their own slot.
    got_offset = lb.align(8);
    lb.alloc(owned_slots * kNativeSlotSize, kNativeSlotSize);

    // JMPREL table.
    jmprel_offset = lb.align(8);

    size_t slot_idx = 0;

    for (const auto& js : jmpslots_) {
      yukiplt::NativeElfRelocation rela{};
      // r_offset is the vaddr of the GOT slot.
      // Since p_vaddr = 0 and load_bias = buffer base,
      // we use the offset within the buffer.
      rela.r_offset = got_offset + slot_idx * kNativeSlotSize;
      rela.r_info = yukiplt::native_reloc_info(js.sym_index, YUKIPLT_RELOC_JUMP_SLOT);
      set_test_addend(rela, js.addend);
      lb.append(rela, 8);
      ++slot_idx;
    }

    for (const auto& cr : custom_relas_) {
      yukiplt::NativeElfRelocation rela{};
      rela.r_offset = got_offset + slot_idx * kNativeSlotSize;
      rela.r_info = yukiplt::native_reloc_info(cr.sym_index, cr.rela_type);
      set_test_addend(rela, cr.addend);
      lb.append(rela, 8);
      ++slot_idx;
    }

    for (const auto& as : aliased_slots_) {
      yukiplt::NativeElfRelocation rela{};
      // Reuse the GOT slot of the target jump slot: a duplicate record.
      rela.r_offset = got_offset + as.target_jmpslot_index * kNativeSlotSize;
      rela.r_info = yukiplt::native_reloc_info(as.sym_index, YUKIPLT_RELOC_JUMP_SLOT);
      set_test_addend(rela, 0);
      lb.append(rela, 8);
    }
  }

  size_t jmprel_size = total_relas * sizeof(yukiplt::NativeElfRelocation);
  // 7b. Separate DT_RELA table (.rela.dyn) + its own GOT slots.
  size_t rela_table_offset = 0;
  size_t rela_got_offset = 0;
  size_t rela_count = rela_entries_.size() + rela_aliases_.size();
  size_t rela_table_size = 0;
  if (rela_count > 0) {
    rela_got_offset = lb.align(8);
    lb.alloc(rela_count * kNativeSlotSize, kNativeSlotSize);
    rela_table_offset = lb.align(8);
    size_t slot_idx = 0;
    for (const auto& entry : rela_entries_) {
      yukiplt::NativeElfRelocation rela{};
      rela.r_offset = rela_got_offset + slot_idx * kNativeSlotSize;
      rela.r_info = yukiplt::native_reloc_info(entry.sym_index, entry.rela_type);
      set_test_addend(rela, entry.addend);
      lb.append(rela, 8);
      ++slot_idx;
    }
    for (const auto& alias : rela_aliases_) {
      yukiplt::NativeElfRelocation rela{};
      rela.r_offset = rela_got_offset + alias.target_rela_index * kNativeSlotSize;
      rela.r_info = yukiplt::native_reloc_info(alias.sym_index, alias.rela_type);
      set_test_addend(rela, 0);
      lb.append(rela, 8);
    }
    rela_table_size = rela_count * sizeof(yukiplt::NativeElfRelocation);
  }
  // 7c. DT_ANDROID_RELA table (raw bytes) + its own GOT slots.
  size_t android_table_offset = 0;
  size_t android_got_offset = 0;
  if (has_android_rela_) {
    if (android_rela_slots_ > 0) {
      android_got_offset = lb.align(8);
      lb.alloc(android_rela_slots_ * kNativeSlotSize, kNativeSlotSize);
    }
    android_table_offset = lb.align(8);
    size_t bytes_off = lb.alloc(android_rela_bytes_.size(), 8);
    if (!android_rela_bytes_.empty())
      std::memcpy(lb.buf.data() + bytes_off, android_rela_bytes_.data(),
                  android_rela_bytes_.size());
  }
  // 8. Dynamic section.
  size_t dyn_offset = lb.align(8);

  auto add_dyn = [&](int64_t tag, uint64_t val) {
    yukiplt::NativeElfDyn d{};
    d.d_tag = tag;
    d.d_un.d_val = val;
    lb.append(d, 8);
  };

  size_t dyn_count = 0;
  auto maybe_add_dyn = [&](int64_t tag, uint64_t val, bool condition = true) {
    if (!condition) return;
    if (truncate_dyn_ > 0 && dyn_count >= truncate_dyn_) return;
    add_dyn(tag, val);
    ++dyn_count;
  };

  // d_ptr values are offsets within the buffer (p_vaddr = 0, bionic-style).
  maybe_add_dyn(DT_SYMTAB, symtab_offset, !omit_symtab_);
  maybe_add_dyn(DT_STRTAB, strtab_offset, !omit_strtab_);
  maybe_add_dyn(DT_STRSZ, strtab_size, !omit_strsz_);
  maybe_add_dyn(DT_SYMENT, syment_override_);
  if (use_gnu_hash_) maybe_add_dyn(DT_GNU_HASH, gnu_hash_offset);
  if (use_sysv_hash_) maybe_add_dyn(DT_HASH, sysv_hash_offset);
  if (total_relas > 0) {
    maybe_add_dyn(DT_JMPREL, jmprel_offset);
    maybe_add_dyn(DT_PLTRELSZ, jmprel_size);
    maybe_add_dyn(DT_PLTREL, pltrel_override_);
    maybe_add_dyn(detail::kDtNativeRelEnt,
                  has_relaent_override_ ? relaent_override_ : sizeof(yukiplt::NativeElfRelocation));
  }
  if (rela_count > 0) {
    maybe_add_dyn(detail::kDtNativeRel, rela_table_offset);
    maybe_add_dyn(detail::kDtNativeRelSz, rela_table_size);
    if (total_relas == 0)
      maybe_add_dyn(detail::kDtNativeRelEnt, has_relaent_override_
                                                 ? relaent_override_
                                                 : sizeof(yukiplt::NativeElfRelocation));
  }
  if (has_android_rela_) {
    maybe_add_dyn(detail::kDtNativeAndroidRel, android_table_offset);
    maybe_add_dyn(detail::kDtNativeAndroidRelSz, android_rela_bytes_.size());
  }
  if (emit_rel_tag_) maybe_add_dyn(detail::kDtForeignRel, 0);

  // DT_NULL terminator.  Truncation drops tags, not the terminator: a
  // truncated table is still well formed, so a test reaches the missing-tag
  // check instead of the missing-DT_NULL one (use omit_dt_null for that).
  if (!omit_dt_null_) add_dyn(DT_NULL, 0);

  size_t dyn_size = lb.pos - dyn_offset;
  // 9. Fill in program headers.
  size_t total_size = lb.pos;
  size_t phi = 0;

  // PT_LOAD: covers the entire buffer, or the two halves of a split layout.
  {
    const uint64_t base = has_load_vaddr_override_ ? load_vaddr_override_ : 0;
    yukiplt::NativeElfPhdr ph{};
    ph.p_type = PT_LOAD;
    ph.p_flags = split_got_load_ ? PF_R : load_flags_;
    ph.p_offset = 0;
    ph.p_vaddr = base;
    ph.p_paddr = ph.p_vaddr;
    ph.p_filesz = split_got_load_ ? second_load_offset : total_size;
    ph.p_memsz = ph.p_filesz;
    ph.p_align = 0x1000;
    lb.write_at(phdr_offset + phi * sizeof(yukiplt::NativeElfPhdr), ph);
    ++phi;
    if (split_got_load_) {
      yukiplt::NativeElfPhdr got{};
      got.p_type = PT_LOAD;
      got.p_flags = got_load_flags_;
      got.p_offset = second_load_offset;
      got.p_vaddr = base + second_load_offset;
      got.p_paddr = got.p_vaddr;
      got.p_filesz = total_size - second_load_offset;
      got.p_memsz = got.p_filesz;
      got.p_align = 0x1000;
      lb.write_at(phdr_offset + phi * sizeof(yukiplt::NativeElfPhdr), got);
      ++phi;
    }
  }

  // PT_DYNAMIC
  {
    yukiplt::NativeElfPhdr ph{};
    ph.p_type = PT_DYNAMIC;
    ph.p_flags = PF_R | PF_W;
    ph.p_offset = dyn_offset;
    ph.p_vaddr = has_load_vaddr_override_ ? (load_vaddr_override_ + dyn_offset) : dyn_offset;
    ph.p_paddr = ph.p_vaddr;
    ph.p_filesz = dyn_size;
    ph.p_memsz = dyn_size;
    ph.p_align = 8;
    lb.write_at(phdr_offset + phi * sizeof(yukiplt::NativeElfPhdr), ph);
    ++phi;
  }

  // PT_GNU_RELRO (optional)
  if (use_relro_) {
    yukiplt::NativeElfPhdr ph{};
    ph.p_type = PT_GNU_RELRO;
    ph.p_flags = PF_R;
    ph.p_offset = 0;
    ph.p_vaddr = has_load_vaddr_override_ ? load_vaddr_override_ : 0;
    ph.p_paddr = ph.p_vaddr;
    ph.p_filesz = total_size;
    ph.p_memsz = total_size;
    ph.p_align = 1;
    lb.write_at(phdr_offset + phi * sizeof(yukiplt::NativeElfPhdr), ph);
    ++phi;
  }

  BuiltImage result;
  result.buffer = std::move(lb.buf);
  result.phdr_offset = phdr_offset;
  result.phdr_count = num_phdrs;
  result.got_offset = got_offset;
  result.rela_got_offset = rela_got_offset;
  result.android_got_offset = android_got_offset;

  return result;
}

}  // namespace yukiplt::test
