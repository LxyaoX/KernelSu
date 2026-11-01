// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_MAPS_CACHE_HPP_
#define YUKIPLT_MAPS_CACHE_HPP_

#include <map>
#include <memory>
#include <vector>

#include "memory.hpp"

namespace yukiplt::detail {
struct CachedPage {
  VmaInfo vma;
  bool invalid = false;
  bool unknown = false;
};
struct MapsCacheState {
  static constexpr size_t kMaterializedBytes = size_t{64} * 1024 * 1024;
  std::vector<VmaInfo> mappings;
  std::map<uintptr_t, CachedPage> pages;
  size_t page_size = 0;
  bool invalid = false;
  Error lookup(uintptr_t page, VmaInfo& out, bool baseline = false,
               bool allow_unknown = false) const noexcept;
  Error remember_page(uintptr_t page, const VmaInfo& vma);
};
struct MapsCacheAccess {
  static std::shared_ptr<MapsCacheState> get(const MapsCache& cache) noexcept;
};
Error process_entry() noexcept;
Error initialize_c_store() noexcept;
}  // namespace yukiplt::detail
#endif
