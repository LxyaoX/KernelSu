// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "elf.hpp"

#include <cstring>

#include "common.hpp"
#include "imports.hpp"

namespace yukiplt::detail {
// Helpers
// InspectedImage query methods

bool InspectedImage::addr_in_load(uintptr_t addr, size_t size) const noexcept {
  if (size == 0) return false;
  uintptr_t end;
  if (!safe_add(addr, static_cast<uintptr_t>(size), end)) return false;
  for (const auto& seg : load_segments) {
    if (addr >= seg.vaddr_begin && end <= seg.vaddr_end) return true;
  }
  return false;
}

bool InspectedImage::addr_in_writable_load(uintptr_t addr, size_t size) const noexcept {
  if (size == 0) return false;
  uintptr_t end;
  if (!safe_add(addr, static_cast<uintptr_t>(size), end)) return false;
  for (const auto& seg : load_segments) {
    if ((seg.flags & PF_W) != 0 && addr >= seg.vaddr_begin && end <= seg.vaddr_end) return true;
  }
  return false;
}
// Dynamic section parsing

/// Collected raw values from the dynamic section (before address resolution).
struct DynamicInfo {
  uint64_t dt_symtab = 0;
  uint64_t dt_strtab = 0;
  uint64_t dt_strsz = 0;
  uint64_t dt_syment = 0;
  uint64_t dt_gnu_hash = 0;
  uint64_t dt_hash = 0;  // SysV hash
  uint64_t dt_jmprel = 0;
  uint64_t dt_pltrelsz = 0;
  uint64_t dt_pltrel = 0;
  uint64_t dt_relaent = 0;
  uint64_t dt_rela = 0;
  uint64_t dt_relasz = 0;
  uint64_t dt_android_rela = 0;
  uint64_t dt_android_relasz = 0;

  bool has_symtab = false;
  bool has_strtab = false;
  bool has_strsz = false;
  bool has_syment = false;
  bool has_gnu_hash = false;
  bool has_hash = false;
  bool has_jmprel = false;
  bool has_pltrelsz = false;
  bool has_pltrel = false;
  bool has_relaent = false;
  bool has_rela = false;
  bool has_relasz = false;
  bool has_android_rela = false;
  bool has_android_relasz = false;
  // Set when a RELA-family tag repeats, or an unusable REL form is present.
  // The opt-in GLOB_DAT/APS2 path rejects such an image; hook() ignores it.
  bool reloc_tags_unusable = false;
};

/// Parse the dynamic section, extracting the tags we care about.
/// The dynamic section is at [dyn_addr, dyn_addr + dyn_size) in memory.
static Expected<DynamicInfo, Error> parse_dynamic(uintptr_t dyn_addr, size_t dyn_size) {
  if (dyn_size < sizeof(NativeElfDyn) || dyn_size % sizeof(NativeElfDyn) != 0 ||
      dyn_addr % alignof(NativeElfDyn) != 0)
    return Error::Make(ErrorCode::kCorruptedElf);

  const size_t max_entries = dyn_size / sizeof(NativeElfDyn);
  const auto* dyn = reinterpret_cast<const NativeElfDyn*>(dyn_addr);

  DynamicInfo info;
  bool found_null = false;

  for (size_t i = 0; i < max_entries; ++i) {
    const auto& d = dyn[i];
    if (d.d_tag == DT_NULL) {
      found_null = true;
      break;
    }

    switch (d.d_tag) {
      case DT_SYMTAB:
        info.dt_symtab = d.d_un.d_ptr;
        info.has_symtab = true;
        break;
      case DT_STRTAB:
        info.dt_strtab = d.d_un.d_ptr;
        info.has_strtab = true;
        break;
      case DT_STRSZ:
        info.dt_strsz = d.d_un.d_val;
        info.has_strsz = true;
        break;
      case DT_SYMENT:
        info.dt_syment = d.d_un.d_val;
        info.has_syment = true;
        break;
      case DT_GNU_HASH:
        info.dt_gnu_hash = d.d_un.d_ptr;
        info.has_gnu_hash = true;
        break;
      case DT_HASH:
        info.dt_hash = d.d_un.d_ptr;
        info.has_hash = true;
        break;
      case DT_JMPREL:
        if (info.has_jmprel) return Error::Make(ErrorCode::kCorruptedElf);
        info.dt_jmprel = d.d_un.d_ptr;
        info.has_jmprel = true;
        break;
      case DT_PLTRELSZ:
        if (info.has_pltrelsz) return Error::Make(ErrorCode::kCorruptedElf);
        info.dt_pltrelsz = d.d_un.d_val;
        info.has_pltrelsz = true;
        break;
      case DT_PLTREL:
        if (info.has_pltrel) return Error::Make(ErrorCode::kCorruptedElf);
        info.dt_pltrel = d.d_un.d_val;
        info.has_pltrel = true;
        break;
      case kDtNativeRelEnt:
        if (info.has_relaent) info.reloc_tags_unusable = true;
        info.dt_relaent = d.d_un.d_val;
        info.has_relaent = true;
        break;
      case kDtNativeRel:
        if (info.has_rela) info.reloc_tags_unusable = true;
        info.dt_rela = d.d_un.d_ptr;
        info.has_rela = true;
        break;
      case kDtNativeRelSz:
        if (info.has_relasz) info.reloc_tags_unusable = true;
        info.dt_relasz = d.d_un.d_val;
        info.has_relasz = true;
        break;
      case kDtNativeAndroidRel:
        if (info.has_android_rela) info.reloc_tags_unusable = true;
        info.dt_android_rela = d.d_un.d_ptr;
        info.has_android_rela = true;
        break;
      case kDtNativeAndroidRelSz:
        if (info.has_android_relasz) info.reloc_tags_unusable = true;
        info.dt_android_relasz = d.d_un.d_val;
        info.has_android_relasz = true;
        break;
      case kDtForeignRel:
      case kDtForeignRelSz:
      case kDtForeignRelEnt:
      case kDtForeignAndroidRel:
      case kDtForeignAndroidRelSz:
        // A foreign REL/RELA form is not usable by this native ABI.  Its presence makes
        // the opt-in relocation path refuse the image; hook() still ignores it.
        info.reloc_tags_unusable = true;
        break;
      default:
        break;
    }
  }

  // The dynamic array must terminate with DT_NULL within its segment bounds.
  // A table that runs to the end of PT_DYNAMIC without a terminator is either
  // truncated or malformed; we refuse to guess where it ends.
  if (!found_null) return Error::Make(ErrorCode::kCorruptedElf);

  return info;
}
// Main inspection

Expected<InspectedImage, Error> inspect(uintptr_t load_bias, const NativeElfPhdr* phdr_table,
                                        size_t phdr_count) {
  if (phdr_table == nullptr) return Error::Make(ErrorCode::kNullPointer);
  if (phdr_count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  uintptr_t phdr_bytes, phdr_end;
  if (!safe_mul(phdr_count, sizeof(NativeElfPhdr), phdr_bytes) ||
      !safe_add(reinterpret_cast<uintptr_t>(phdr_table), phdr_bytes, phdr_end))
    return Error::Make(ErrorCode::kIntegerOverflow);
  if (reinterpret_cast<uintptr_t>(phdr_table) % alignof(NativeElfPhdr))
    return Error::Make(ErrorCode::kInvalidArgument);

  InspectedImage image;
  image.load_bias = load_bias;

  // Collect PT_LOAD, PT_DYNAMIC, PT_GNU_RELRO.
  const NativeElfPhdr* dyn_phdr = nullptr;
  const NativeElfPhdr* relro_phdr = nullptr;

  for (size_t i = 0; i < phdr_count; ++i) {
    const auto& ph = phdr_table[i];

    switch (ph.p_type) {
      case PT_LOAD: {
        if (ph.p_filesz > ph.p_memsz) return Error::Make(ErrorCode::kCorruptedElf);
        // Validate vaddr + memsz doesn't overflow.
        uint64_t vaddr_end64;
        if (!safe_add64(ph.p_vaddr, ph.p_memsz, vaddr_end64))
          return Error::Make(ErrorCode::kIntegerOverflow);

        // Validate vaddr + load_bias doesn't overflow.
        uintptr_t runtime_begin;
        if (!safe_add(load_bias, static_cast<uintptr_t>(ph.p_vaddr), runtime_begin))
          return Error::Make(ErrorCode::kIntegerOverflow);

        uintptr_t runtime_end;
        if (!safe_add(runtime_begin, static_cast<uintptr_t>(ph.p_memsz), runtime_end))
          return Error::Make(ErrorCode::kIntegerOverflow);

        // Reject zero-size segments.
        if (runtime_begin == runtime_end) continue;

        image.load_segments.push_back({runtime_begin, runtime_end, ph.p_flags});
        break;
      }
      case PT_DYNAMIC:
        if (dyn_phdr != nullptr)
          return Error::Make(ErrorCode::kCorruptedElf);  // Duplicate PT_DYNAMIC
        dyn_phdr = &ph;
        break;
      case PT_GNU_RELRO:
        if (relro_phdr != nullptr) return Error::Make(ErrorCode::kCorruptedElf);  // Duplicate
        relro_phdr = &ph;
        break;
      default:
        break;
    }
  }

  if (dyn_phdr == nullptr) return Error::Make(ErrorCode::kInvalidImage);  // No PT_DYNAMIC

  // A PHDR-only custom view may omit the ELF header. If a readable, file-backed
  // LOAD at offset zero contains an ELF magic, validate its native ABI before
  // reading any dynamic metadata. PHDR alone cannot prove machine or class.
  bool saw_header = false;
  bool matched_header_table = false;
  uintptr_t canonical_phoff = 0;
  const auto table = reinterpret_cast<uintptr_t>(phdr_table);
  const bool table_in_image = image.addr_in_load(table, phdr_bytes);
  for (size_t i = 0; i < phdr_count; ++i) {
    const auto& ph = phdr_table[i];
    if (ph.p_type != PT_LOAD || ph.p_offset != 0 || !(ph.p_flags & PF_R) ||
        ph.p_filesz < sizeof(NativeElfEhdr))
      continue;
    uintptr_t address;
    if (!safe_add(load_bias, ph.p_vaddr, address)) return Error::Make(ErrorCode::kIntegerOverflow);
    if (!image.addr_in_load(address, sizeof(NativeElfEhdr))) continue;
    NativeElfEhdr header;
    std::memcpy(&header, reinterpret_cast<const void*>(address), sizeof(header));
    const auto* ident = header.e_ident;
    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') continue;
    if (ident[EI_CLASS] != YUKIPLT_ELF_CLASS || ident[EI_DATA] != ELFDATA2LSB ||
        header.e_machine != kNativeMachine || header.e_type != ET_DYN)
      return Error::Make(ErrorCode::kUnsupportedFormat);
    if (ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT ||
        header.e_ehsize != sizeof(NativeElfEhdr) || header.e_phentsize != sizeof(NativeElfPhdr))
      return Error::Make(ErrorCode::kCorruptedElf);
    // Require the image's PHDR count. An in-image table must match a canonical
    // e_phoff copy in a zero-offset LOAD; external copied tables are allowed.
    if (header.e_phnum != phdr_count) return Error::Make(ErrorCode::kCorruptedElf);
    // Offset-zero aliases describe the same file. A valid matching copy must
    // not hide a contradictory offset in another readable header. Validate
    // the file-table arithmetic even for an external copied PHDR view, but do
    // not require every alias to map the table: an alias may cover Ehdr only.
    const auto phoff = static_cast<uintptr_t>(header.e_phoff);
    uintptr_t file_table_end, expected;
    if (!safe_add(phoff, phdr_bytes, file_table_end) || !safe_add(address, phoff, expected))
      return Error::Make(ErrorCode::kIntegerOverflow);
    if (phoff < sizeof(NativeElfEhdr) || phoff % alignof(NativeElfPhdr) != 0 ||
        (saw_header && canonical_phoff != phoff))
      return Error::Make(ErrorCode::kCorruptedElf);
    canonical_phoff = phoff;
    saw_header = true;
    matched_header_table |= expected == table;
  }
  if (saw_header && table_in_image && !matched_header_table)
    return Error::Make(ErrorCode::kCorruptedElf);

  // Validate and compute PT_DYNAMIC runtime range.
  uintptr_t dyn_begin;
  if (!safe_add(load_bias, static_cast<uintptr_t>(dyn_phdr->p_vaddr), dyn_begin))
    return Error::Make(ErrorCode::kIntegerOverflow);

  uintptr_t dyn_end;
  if (!safe_add(dyn_begin, static_cast<uintptr_t>(dyn_phdr->p_memsz), dyn_end))
    return Error::Make(ErrorCode::kIntegerOverflow);

  size_t dyn_size = static_cast<size_t>(dyn_end - dyn_begin);

  // PT_DYNAMIC must be within a PT_LOAD segment.
  if (!image.addr_in_load(dyn_begin, dyn_size)) return Error::Make(ErrorCode::kCorruptedElf);
  bool readable_dynamic = false;
  for (const auto& segment : image.load_segments)
    if ((segment.flags & PF_R) != 0 && dyn_begin >= segment.vaddr_begin &&
        dyn_end <= segment.vaddr_end)
      readable_dynamic = true;
  if (!readable_dynamic) return Error::Make(ErrorCode::kOutOfBounds);
  image.dynamic = dyn_begin;
  image.dynamic_size = dyn_size;

  // Validate RELRO arithmetic without inferring live page permissions.
  if (relro_phdr != nullptr) {
    uintptr_t relro_begin, relro_end;
    if (!safe_add(load_bias, static_cast<uintptr_t>(relro_phdr->p_vaddr), relro_begin))
      return Error::Make(ErrorCode::kIntegerOverflow);
    if (!safe_add(relro_begin, static_cast<uintptr_t>(relro_phdr->p_memsz), relro_end))
      return Error::Make(ErrorCode::kIntegerOverflow);
  }

  // Parse dynamic section.
  auto dyn_result = parse_dynamic(dyn_begin, dyn_size);
  if (!dyn_result) return dyn_result.error();
  const DynamicInfo& dinfo = dyn_result.value();

  // We need at minimum a symbol table and string table.
  if (!dinfo.has_symtab) return Error::Make(ErrorCode::kCorruptedElf);
  if (!dinfo.has_strtab) return Error::Make(ErrorCode::kCorruptedElf);
  if (!dinfo.has_strsz) return Error::Make(ErrorCode::kCorruptedElf);

  // Validate DT_SYMENT == sizeof(NativeElfSym).
  if (dinfo.has_syment && dinfo.dt_syment != sizeof(NativeElfSym))
    return Error::Make(ErrorCode::kUnsupportedFormat);

  // bionic keeps relative d_ptr; glibc may adjust it. Treat DT_SYMTAB as
  // adjusted when its raw address already lies within a runtime LOAD.
  {
    uintptr_t raw_symtab = static_cast<uintptr_t>(dinfo.dt_symtab);
    image.dptr_adjusted = image.addr_in_load(raw_symtab, sizeof(NativeElfSym));
  }

  // Helper: resolve a d_ptr to a runtime address.
  auto resolve_ptr = [&](uint64_t d_ptr, uintptr_t& out) -> bool {
    uintptr_t native;
    if (!narrow_address(d_ptr, native)) return false;
    if (image.dptr_adjusted) {
      out = native;
      return true;
    }
    return safe_add(load_bias, native, out);
  };

  // Resolve and validate address-type tags.

  // SYMTAB
  if (!resolve_ptr(dinfo.dt_symtab, image.symtab)) return Error::Make(ErrorCode::kIntegerOverflow);
  if (!image.addr_in_load(image.symtab, sizeof(NativeElfSym)))
    return Error::Make(ErrorCode::kCorruptedElf);

  // STRTAB + STRSZ
  if (!resolve_ptr(dinfo.dt_strtab, image.strtab)) return Error::Make(ErrorCode::kIntegerOverflow);
  image.strsz = static_cast<size_t>(dinfo.dt_strsz);
  if (image.strsz == 0) return Error::Make(ErrorCode::kCorruptedElf);
  if (!image.addr_in_load(image.strtab, image.strsz)) return Error::Make(ErrorCode::kCorruptedElf);

  // GNU Hash (optional)
  if (dinfo.has_gnu_hash) {
    if (!resolve_ptr(dinfo.dt_gnu_hash, image.gnu_hash))
      return Error::Make(ErrorCode::kIntegerOverflow);
    // We validate the hash table structure later in elf.cpp; here just
    // check the base address is within a load segment (at least 16 bytes
    // for the header).
    if (!image.addr_in_load(image.gnu_hash, 16)) return Error::Make(ErrorCode::kCorruptedElf);
  }

  // SysV Hash (optional)
  if (dinfo.has_hash) {
    if (!resolve_ptr(dinfo.dt_hash, image.sysv_hash))
      return Error::Make(ErrorCode::kIntegerOverflow);
    // SysV hash header is two uint32_t: nbucket, nchain.
    if (!image.addr_in_load(image.sysv_hash, 8)) return Error::Make(ErrorCode::kCorruptedElf);
  }

  // At least one hash table is required for symbol lookup.
  if (image.gnu_hash == 0 && image.sysv_hash == 0) return Error::Make(ErrorCode::kNoHashTable);

  // No PLT is valid; a partially described PLT is not.
  if (dinfo.has_jmprel != dinfo.has_pltrelsz || dinfo.has_jmprel != dinfo.has_pltrel)
    return Error::Make(ErrorCode::kCorruptedElf);
  if (dinfo.has_jmprel) {
    // Validate DT_PLTREL matches the native REL/RELA format.
    if (dinfo.dt_pltrel != static_cast<uint64_t>(kDtNativeRel))
      return Error::Make(ErrorCode::kUnsupportedFormat);

    // REL/RELAENT describes the ordinary dynamic table and is validated only
    // when that optional path is scanned. JMPREL uses the native ABI format.

    // Validate PLTRELSZ is a multiple of sizeof(NativeElfRelocation).
    if (dinfo.dt_pltrelsz == 0 || dinfo.dt_pltrelsz % sizeof(NativeElfRelocation) != 0)
      return Error::Make(ErrorCode::kCorruptedElf);

    if (!resolve_ptr(dinfo.dt_jmprel, image.jmprel))
      return Error::Make(ErrorCode::kIntegerOverflow);

    image.jmprel_count = static_cast<size_t>(dinfo.dt_pltrelsz / sizeof(NativeElfRelocation));

    // Validate the entire JMPREL table is within a PT_LOAD segment.
    if (!image.addr_in_load(image.jmprel, static_cast<size_t>(dinfo.dt_pltrelsz)))
      return Error::Make(ErrorCode::kCorruptedElf);
  }

  // Record optional relocation metadata here; validate it only when scanned.
  // Malformed optional tables must not reject the legacy JUMP_SLOT path.
  image.reloc_tags_unusable = dinfo.reloc_tags_unusable;
  image.rela_ent = static_cast<size_t>(dinfo.dt_relaent);
  if (dinfo.has_relaent && dinfo.dt_relaent != sizeof(NativeElfRelocation))
    image.reloc_tags_unusable = true;
  if (dinfo.has_rela != dinfo.has_relasz) image.reloc_tags_unusable = true;
  if (dinfo.has_android_rela != dinfo.has_android_relasz) image.reloc_tags_unusable = true;
  if (dinfo.has_rela && dinfo.has_relasz) {
    if (resolve_ptr(dinfo.dt_rela, image.rela)) {
      image.rela_size = static_cast<size_t>(dinfo.dt_relasz);
    } else {
      image.rela = 0;
      image.reloc_tags_unusable = true;
    }
  }
  if (dinfo.has_android_rela && dinfo.has_android_relasz) {
    if (resolve_ptr(dinfo.dt_android_rela, image.android_rela)) {
      image.android_rela_size = static_cast<size_t>(dinfo.dt_android_relasz);
    } else {
      image.android_rela = 0;
      image.reloc_tags_unusable = true;
    }
  }

  return image;
}

// Hash functions

uint32_t gnu_hash_value(const char* name) noexcept {
  uint32_t h = 5381;
  for (const auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) h = (h << 5) + h + *p;
  return h;
}

uint32_t elf_hash_value(const char* name) noexcept {
  uint32_t h = 0;
  for (const auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
    h = (h << 4) + *p;
    uint32_t g = h & 0xf0000000;
    if (g) h ^= g >> 24;
    h &= ~g;
  }
  return h;
}
// Helper: read a value from an address, checking bounds.

/// Read a T from a runtime address, verifying it falls within a load segment.
template <typename T>
static Expected<T, Error> read_checked(const InspectedImage& image, uintptr_t addr,
                                       size_t index = 0) {
  uintptr_t bytes;
  if (!safe_mul(index, sizeof(T), bytes) || !safe_add(addr, bytes, addr))
    return Error::Make(ErrorCode::kIntegerOverflow);
  uintptr_t end;
  if (!safe_add(addr, sizeof(T), end)) return Error::Make(ErrorCode::kIntegerOverflow);
  bool readable = false;
  for (const auto& segment : image.load_segments)
    if ((segment.flags & PF_R) && addr >= segment.vaddr_begin && end <= segment.vaddr_end)
      readable = true;
  if (!readable) return Error::Make(ErrorCode::kOutOfBounds);
  T val;
  std::memcpy(&val, reinterpret_cast<const void*>(addr), sizeof(T));
  return val;
}

static Expected<const char*, Error> symbol_name(const InspectedImage& image, uint32_t offset) {
  if (offset >= image.strsz) return Error::Make(ErrorCode::kCorruptedElf);
  uintptr_t end;
  if (!safe_add(image.strtab, image.strsz, end)) return Error::Make(ErrorCode::kIntegerOverflow);
  bool readable = false;
  for (const auto& segment : image.load_segments)
    if ((segment.flags & PF_R) && image.strtab >= segment.vaddr_begin && end <= segment.vaddr_end)
      readable = true;
  if (!readable) return Error::Make(ErrorCode::kOutOfBounds);
  const char* name = reinterpret_cast<const char*>(image.strtab + offset);
  if (!std::memchr(name, 0, image.strsz - offset)) return Error::Make(ErrorCode::kCorruptedElf);
  return name;
}
// GNU Hash lookup

// GNU hash: four uint32_t header words, native-width bloom words, then
// uint32_t buckets and chains indexed from symoffset.

Expected<std::optional<uint32_t>, Error> gnu_hash_lookup(const InspectedImage& image,
                                                         const char* name) {
  if (image.gnu_hash == 0) return std::optional<uint32_t>(std::nullopt);

  uintptr_t base = image.gnu_hash;

  // Read header.
  auto r_nbuckets = read_checked<uint32_t>(image, base);
  auto r_symoffset = read_checked<uint32_t>(image, base, 1);
  auto r_bloom_size = read_checked<uint32_t>(image, base, 2);
  auto r_bloom_shift = read_checked<uint32_t>(image, base, 3);

  if (!r_nbuckets || !r_symoffset || !r_bloom_size || !r_bloom_shift)
    return Error::Make(ErrorCode::kCorruptedElf);

  uint32_t nbuckets = r_nbuckets.value();
  uint32_t symoffset = r_symoffset.value();
  uint32_t bloom_size = r_bloom_size.value();
  uint32_t bloom_shift = r_bloom_shift.value();

  if (nbuckets == 0 || bloom_size == 0 || bloom_shift >= 32)
    return Error::Make(ErrorCode::kCorruptedElf);

  // Compute layout addresses.
  uintptr_t bloom_addr, buckets_addr, chains_addr, bloom_bytes, bucket_bytes;
  if (!safe_add(base, 16, bloom_addr) ||
      !safe_mul(bloom_size, sizeof(NativeElfAddr), bloom_bytes) ||
      !safe_mul(nbuckets, sizeof(uint32_t), bucket_bytes) ||
      !safe_add(bloom_addr, bloom_bytes, buckets_addr) ||
      !safe_add(buckets_addr, bucket_bytes, chains_addr))
    return Error::Make(ErrorCode::kIntegerOverflow);

  // Validate bloom array bounds.
  if (!image.addr_in_load(bloom_addr, bloom_bytes)) return Error::Make(ErrorCode::kCorruptedElf);

  // Validate bucket array bounds.
  if (!image.addr_in_load(buckets_addr, bucket_bytes)) return Error::Make(ErrorCode::kCorruptedElf);

  uint32_t h = gnu_hash_value(name);

  // Bloom filter check.
  {
    constexpr uint32_t bits = sizeof(NativeElfAddr) * 8;
    uint32_t word_idx = (h / bits) % bloom_size;
    auto r_bloom = read_checked<NativeElfAddr>(image, bloom_addr, word_idx);
    if (!r_bloom) return r_bloom.error();
    NativeElfAddr bloom_word = r_bloom.value();
    NativeElfAddr mask =
        (NativeElfAddr{1} << (h % bits)) | (NativeElfAddr{1} << ((h >> bloom_shift) % bits));
    if ((bloom_word & mask) != mask)
      return std::optional<uint32_t>(std::nullopt);  // Definitely not present.
  }

  // Bucket lookup.
  uint32_t bucket_idx = h % nbuckets;
  auto r_sym_idx = read_checked<uint32_t>(image, buckets_addr, bucket_idx);
  if (!r_sym_idx) return r_sym_idx.error();
  uint32_t sym_idx = r_sym_idx.value();

  if (sym_idx == 0) return std::optional<uint32_t>(std::nullopt);  // Empty bucket.

  if (sym_idx < symoffset) return Error::Make(ErrorCode::kCorruptedElf);

  // Bound every chain index before interpreting it as a dynsym index. This
  // uses a reliable hash count, or a readable LOAD bound when no exact count
  // exists; it never invents an exact count from an empty GNU hash.
  auto symbol_bound = dynamic_symbol_count(image);
  if (!symbol_bound) return symbol_bound.error();

  // Chain walk.
  uint32_t h1 = h & ~uint32_t(1);  // Clear lowest bit for comparison.

  for (;;) {
    if (sym_idx >= symbol_bound.value()) return Error::Make(ErrorCode::kCorruptedElf);
    uint32_t chain_idx = sym_idx - symoffset;
    auto r_chain = read_checked<uint32_t>(image, chains_addr, chain_idx);
    if (!r_chain) return r_chain.error();
    uint32_t chain_val = r_chain.value();

    if ((chain_val & ~uint32_t(1)) == h1) {
      // Hash matches — compare the actual symbol name.
      auto r_sym = read_checked<NativeElfSym>(image, image.symtab, sym_idx);
      if (!r_sym) return r_sym.error();
      const NativeElfSym& sym = r_sym.value();

      auto text = symbol_name(image, sym.st_name);
      if (!text) return text.error();
      if (std::strcmp(text.value(), name) == 0) return std::optional<uint32_t>(sym_idx);
    }

    // End of chain?
    if (chain_val & 1) break;

    if (sym_idx == UINT32_MAX) return Error::Make(ErrorCode::kIntegerOverflow);
    ++sym_idx;
  }

  return std::optional<uint32_t>(std::nullopt);
}
// SysV Hash lookup

// SysV hash: nbucket, nchain, followed by uint32_t bucket and chain arrays.

Expected<std::optional<uint32_t>, Error> sysv_hash_lookup(const InspectedImage& image,
                                                          const char* name) {
  if (image.sysv_hash == 0) return std::optional<uint32_t>(std::nullopt);

  uintptr_t base = image.sysv_hash;

  auto r_nbucket = read_checked<uint32_t>(image, base);
  auto r_nchain = read_checked<uint32_t>(image, base, 1);
  if (!r_nbucket || !r_nchain) return Error::Make(ErrorCode::kCorruptedElf);

  uint32_t nbucket = r_nbucket.value();
  uint32_t nchain = r_nchain.value();

  if (nbucket == 0) return Error::Make(ErrorCode::kCorruptedElf);

  uintptr_t bucket_addr, chain_addr, bucket_bytes, chain_bytes;
  if (!safe_add(base, 8, bucket_addr) || !safe_mul(nbucket, sizeof(uint32_t), bucket_bytes) ||
      !safe_mul(nchain, sizeof(uint32_t), chain_bytes) ||
      !safe_add(bucket_addr, bucket_bytes, chain_addr))
    return Error::Make(ErrorCode::kIntegerOverflow);

  // Validate arrays.
  if (!image.addr_in_load(bucket_addr, bucket_bytes)) return Error::Make(ErrorCode::kCorruptedElf);
  if (!image.addr_in_load(chain_addr, chain_bytes)) return Error::Make(ErrorCode::kCorruptedElf);

  uint32_t h = elf_hash_value(name);
  uint32_t bucket_idx = h % nbucket;

  auto r_sym_idx = read_checked<uint32_t>(image, bucket_addr, bucket_idx);
  if (!r_sym_idx) return r_sym_idx.error();
  uint32_t sym_idx = r_sym_idx.value();

  // Walk the chain.
  for (uint32_t visited = 0; sym_idx != 0; ++visited) {
    if (visited >= nchain) return Error::Make(ErrorCode::kCorruptedElf);
    if (sym_idx >= nchain) return Error::Make(ErrorCode::kCorruptedElf);

    // Read symbol.
    auto r_sym = read_checked<NativeElfSym>(image, image.symtab, sym_idx);
    if (!r_sym) return r_sym.error();
    const NativeElfSym& sym = r_sym.value();

    auto text = symbol_name(image, sym.st_name);
    if (!text) return text.error();
    if (std::strcmp(text.value(), name) == 0) return std::optional<uint32_t>(sym_idx);

    // Next in chain.
    auto r_next = read_checked<uint32_t>(image, chain_addr, sym_idx);
    if (!r_next) return r_next.error();
    sym_idx = r_next.value();
  }

  return std::optional<uint32_t>(std::nullopt);
}
// Unified lookup

Expected<uint32_t, Error> lookup_symbol(const InspectedImage& image, const char* name) {
  if (name == nullptr) return Error::Make(ErrorCode::kNullPointer);

  // Try GNU hash first.
  if (image.gnu_hash != 0) {
    auto result = gnu_hash_lookup(image, name);
    if (!result) return result.error();
    const auto& found = result.value();
    if (found.has_value()) return found.value();
  }

  // Fall back to SysV hash.
  if (image.sysv_hash != 0) {
    auto result = sysv_hash_lookup(image, name);
    if (!result) return result.error();
    const auto& found = result.value();
    if (found.has_value()) return found.value();
  }

  // If neither hash table exists, report it.
  if (image.gnu_hash == 0 && image.sysv_hash == 0) return Error::Make(ErrorCode::kNoHashTable);

  return Error::Make(ErrorCode::kSymbolNotFound);
}
// Relocation resolver

Expected<SmallVec<ResolvedSlot>, Error> resolve_slots(const InspectedImage& image,
                                                      const char* symbol_name) {
  if (symbol_name == nullptr) return Error::Make(ErrorCode::kNullPointer);

  // Validate all JMPREL metadata and reject duplicate slots before filtering.
  // Undefined imports require relocation scanning, not hash lookup.
  auto snapshot = snapshot_imports(image);
  if (!snapshot) return snapshot.error();

  SmallVec<ResolvedSlot> slots;
  for (const ImportInfo& entry : snapshot.value().imports) {
    if (entry.symbol != symbol_name) continue;
    slots.push_back({entry.slot_address, entry.symbol_index});
  }
  if (slots.empty()) return Error::Make(ErrorCode::kSymbolNotFound);
  return slots;
}

Expected<ResolvedSlot, Error> resolve_unique_slot(const InspectedImage& image,
                                                  const char* symbol_name) {
  auto slots = resolve_slots(image, symbol_name);
  if (!slots) return slots.error();

  const auto& matches = slots.value();
  // resolve_slots never returns an empty success, but keep the guard explicit.
  if (matches.empty()) return Error::Make(ErrorCode::kSymbolNotFound);

  // resolve_slots() already rejected any two records naming one slot, so
  // several matches are distinct slots.

  // Distinct matching slots are ambiguous even when their values agree.
  if (matches.size() > 1) return Error::Make(ErrorCode::kAmbiguousSymbol);

  return matches[0];
}

}  // namespace yukiplt::detail
