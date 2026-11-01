// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_TEST_VERSIONED_ELF_HPP_
#define YUKIPLT_TEST_VERSIONED_ELF_HPP_

#include "elf_builder.hpp"
#include "elf.hpp"
#include "yukiplt.hpp"

namespace yukiplt::test {

// Two same-name imports with independent version requirements. Offsets remain
// valid across vector reallocations and can be corrupted by individual tests.
struct VersionedElf {
  ElfBuilder::BuiltImage image;
  size_t versym = 0;
  size_t need = 0;
  size_t symtab = 0;
  size_t dynamic = 0;
  size_t strings = 0;
  size_t version_name[2]{};

  template <typename T>
  T get(size_t offset) const {
    T result;
    std::memcpy(&result, image.buffer.data() + offset, sizeof(result));
    return result;
  }
  template <typename T>
  void put(size_t offset, const T& value) {
    std::memcpy(image.buffer.data() + offset, &value, sizeof(value));
  }
  template <typename T>
  size_t append(const T& value) {
    size_t offset = (image.buffer.size() + alignof(T) - 1) & ~(alignof(T) - 1);
    image.buffer.resize(offset + sizeof(T));
    put(offset, value);
    return offset;
  }
  ImageView view() const { return {image.load_bias(), image.phdr_table(), image.phdr_count, 9}; }

  explicit VersionedElf(bool definition = false) {
    ElfBuilder builder;
    builder.add_symbol("versioned", 1).add_symbol("versioned", 2).add_jump_slot(1).add_jump_slot(2);
    image = builder.build();
    std::vector<yukiplt::NativeElfDyn> tags;
    const auto phdr = image.phdr_table()[1];
    size_t old_strings = 0;
    size_t old_string_size = 0;
    for (size_t offset = phdr.p_vaddr;; offset += sizeof(yukiplt::NativeElfDyn)) {
      auto tag = get<yukiplt::NativeElfDyn>(offset);
      if (tag.d_tag == DT_NULL) break;
      if (tag.d_tag == DT_SYMTAB) symtab = tag.d_un.d_ptr;
      if (tag.d_tag == DT_STRTAB) old_strings = tag.d_un.d_ptr;
      if (tag.d_tag == DT_STRSZ) old_string_size = tag.d_un.d_val;
      tags.push_back(tag);
    }
    std::string str(reinterpret_cast<const char*>(image.buffer.data() + old_strings),
                    old_string_size);
    auto add_string = [&](const char* name) {
      const auto offset = static_cast<uint32_t>(str.size());
      str.append(name);
      str.push_back('\0');
      return offset;
    };
    version_name[0] = add_string("VERSION_1");
    version_name[1] = add_string("VERSION_2");
    const auto file = add_string("libversions.so");
    strings = image.buffer.size();
    image.buffer.insert(image.buffer.end(), str.begin(), str.end());
    versym = append<yukiplt::NativeElfHalf>(0);
    append<yukiplt::NativeElfHalf>(2 | 0x8000);
    append<yukiplt::NativeElfHalf>(3);
    if (!definition) {
      need = append<yukiplt::NativeElfVerneed>({1, 2, file, sizeof(yukiplt::NativeElfVerneed), 0});
      append<yukiplt::NativeElfVernaux>({detail::elf_hash_value("VERSION_1"), 0, 2,
                                         static_cast<uint32_t>(version_name[0]),
                                         sizeof(yukiplt::NativeElfVernaux)});
      append<yukiplt::NativeElfVernaux>(
          {detail::elf_hash_value("VERSION_2"), 0, 3, static_cast<uint32_t>(version_name[1]), 0});
      for (size_t i : {size_t{1}, size_t{2}}) {
        auto symbol = get<yukiplt::NativeElfSym>(symtab + i * sizeof(yukiplt::NativeElfSym));
        symbol.st_shndx = SHN_UNDEF;
        put(symtab + i * sizeof(yukiplt::NativeElfSym), symbol);
      }
    } else {
      need = append<yukiplt::NativeElfVerdef>(
          {1, 0, 2, 1, detail::elf_hash_value("VERSION_1"), sizeof(yukiplt::NativeElfVerdef),
           sizeof(yukiplt::NativeElfVerdef) + sizeof(yukiplt::NativeElfVerdaux)});
      append<yukiplt::NativeElfVerdaux>({static_cast<uint32_t>(version_name[0]), 0});
      append<yukiplt::NativeElfVerdef>(
          {1, 0, 3, 1, detail::elf_hash_value("VERSION_2"), sizeof(yukiplt::NativeElfVerdef), 0});
      append<yukiplt::NativeElfVerdaux>({static_cast<uint32_t>(version_name[1]), 0});
    }
    for (auto& tag : tags) {
      if (tag.d_tag == DT_STRTAB) tag.d_un.d_ptr = strings;
      if (tag.d_tag == DT_STRSZ) tag.d_un.d_val = str.size();
    }
    auto tag = [](int64_t kind, uint64_t value) {
      yukiplt::NativeElfDyn result{};
      result.d_tag = kind;
      result.d_un.d_val = value;
      return result;
    };
    tags.push_back(tag(DT_VERSYM, versym));
    tags.push_back(tag(definition ? DT_VERDEF : DT_VERNEED, need));
    tags.push_back(tag(definition ? DT_VERDEFNUM : DT_VERNEEDNUM, definition ? 2 : 1));
    tags.push_back(tag(DT_NULL, 0));
    dynamic = append(tags[0]);
    for (size_t i = 1; i < tags.size(); ++i) append(tags[i]);
    auto load = get<yukiplt::NativeElfPhdr>(0);
    load.p_memsz = load.p_filesz = image.buffer.size();
    put(0, load);
    auto dyn = phdr;
    dyn.p_vaddr = dyn.p_offset = dynamic;
    dyn.p_memsz = dyn.p_filesz = tags.size() * sizeof(yukiplt::NativeElfDyn);
    put(sizeof(yukiplt::NativeElfPhdr), dyn);
  }

  void change_tag(int64_t wanted, int64_t replacement, uint64_t value) {
    for (size_t offset = dynamic;; offset += sizeof(yukiplt::NativeElfDyn)) {
      auto tag = get<yukiplt::NativeElfDyn>(offset);
      if (tag.d_tag == DT_NULL) return;
      if (tag.d_tag != wanted) continue;
      tag.d_tag = replacement;
      tag.d_un.d_val = value;
      put(offset, tag);
      return;
    }
  }
};

}  // namespace yukiplt::test
#endif
