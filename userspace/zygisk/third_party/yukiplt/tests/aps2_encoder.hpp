// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

/// Encode synthetic APS2 streams for bounded-decoder regressions.

#ifndef YUKIPLT_TEST_APS2_ENCODER_HPP_
#define YUKIPLT_TEST_APS2_ENCODER_HPP_

#include <cstdint>
#include <vector>

namespace yukiplt::test {

// Group flag bits (mirror src/relocations.hpp; duplicated so tests do not
// depend on production internals).
constexpr uint64_t kAps2GroupedByInfo = 1;
constexpr uint64_t kAps2GroupedByOffsetDelta = 2;
constexpr uint64_t kAps2GroupedByAddend = 4;
constexpr uint64_t kAps2GroupHasAddend = 8;

struct Aps2Reloc {
  int64_t offset_delta = 0;  ///< Added to the running offset for this entry.
  uint64_t info = 0;         ///< yukiplt::native_reloc_info(sym, type).
  int64_t addend_delta = 0;  ///< Added to the running addend (if has-addend).
};

struct Aps2Group {
  uint64_t flags = 0;
  int64_t group_offset_delta = 0;  ///< Used when kAps2GroupedByOffsetDelta.
  uint64_t group_info = 0;         ///< Used when kAps2GroupedByInfo.
  int64_t group_addend_delta = 0;  ///< Used when kAps2GroupedByAddend.
  std::vector<Aps2Reloc> relocs;
};

inline void append_sleb128(std::vector<uint8_t>& out, int64_t value) {
  bool more = true;
  while (more) {
    uint8_t byte = static_cast<uint8_t>(value & 0x7f);
    value >>= 7;  // Arithmetic shift keeps the sign.
    const bool sign_bit = (byte & 0x40) != 0;
    if ((value == 0 && !sign_bit) || (value == -1 && sign_bit)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    out.push_back(byte);
  }
}

/// Encode a well-formed APS2 stream.  `total` is written to the header; by
/// default it is the sum of group sizes, but a caller may override it to build a
/// stream whose header disagrees with its groups (for rejection tests).
inline std::vector<uint8_t> encode_aps2(int64_t initial_offset,
                                        const std::vector<Aps2Group>& groups,
                                        int64_t total_override = -1) {
  std::vector<uint8_t> out = {0x41, 0x50, 0x53, 0x32};  // "APS2"
  int64_t total = 0;
  for (const auto& group : groups) total += static_cast<int64_t>(group.relocs.size());
  append_sleb128(out, total_override >= 0 ? total_override : total);
  append_sleb128(out, initial_offset);
  for (const auto& group : groups) {
    append_sleb128(out, static_cast<int64_t>(group.relocs.size()));
    append_sleb128(out, static_cast<int64_t>(group.flags));
    if (group.flags & kAps2GroupedByOffsetDelta) append_sleb128(out, group.group_offset_delta);
    if (group.flags & kAps2GroupedByInfo)
      append_sleb128(out, static_cast<int64_t>(group.group_info));
    if ((group.flags & kAps2GroupedByAddend) && (group.flags & kAps2GroupHasAddend))
      append_sleb128(out, group.group_addend_delta);
    for (const auto& reloc : group.relocs) {
      if (!(group.flags & kAps2GroupedByOffsetDelta)) append_sleb128(out, reloc.offset_delta);
      if (!(group.flags & kAps2GroupedByInfo))
        append_sleb128(out, static_cast<int64_t>(reloc.info));
      if ((group.flags & kAps2GroupHasAddend) && !(group.flags & kAps2GroupedByAddend))
        append_sleb128(out, reloc.addend_delta);
    }
  }
  return out;
}

}  // namespace yukiplt::test

#endif  // YUKIPLT_TEST_APS2_ENCODER_HPP_
