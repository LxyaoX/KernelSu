// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "registry.hpp"

#include <cerrno>
#include <stdexcept>

#include "maps_cache.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)

// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#pragma comment(lib, "bcrypt")
#endif

#include "common.hpp"
#include "memory.hpp"

namespace yukiplt::detail {

namespace {
#if defined(__linux__) || defined(__ANDROID__)
// The kernel CSPRNG through its device node.  Used only when getrandom(2) does
// not exist (Linux < 3.17, as on old API 23 emulator kernels): it is the same
// system random source, not a weaker fallback.  Returns 0 or an errno value.
int read_urandom(unsigned char* bytes, size_t size) noexcept {
  int fd;
  do {
    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) return errno;
  size_t filled = 0;
  int error = 0;
  while (filled < size) {
    const ssize_t n = read(fd, bytes + filled, size - filled);
    if (n < 0) {
      if (errno == EINTR) continue;
      error = errno;
      break;
    }
    if (n == 0) {
      error = EIO;
      break;
    }
    filled += static_cast<size_t>(n);
  }
  close(fd);
  return error;
}
#endif

// Use kernel randomness; fall back to /dev/urandom only for ENOSYS.
// The syscall supports bionic before API 28. No predictable salt fallback.
int draw_salt(uint64_t& out) noexcept {
#if defined(__linux__) || defined(__ANDROID__)
  unsigned char* bytes = reinterpret_cast<unsigned char*>(&out);
#if defined(SYS_getrandom)
  size_t filled = 0;
  while (filled < sizeof(out)) {
    const long n = syscall(SYS_getrandom, bytes + filled, sizeof(out) - filled, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == ENOSYS && filled == 0) break;
      return errno;
    }
    filled += static_cast<size_t>(n);
  }
  if (filled == sizeof(out)) return 0;
#endif
  return read_urandom(bytes, sizeof(out));
#elif defined(_WIN32)
  return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&out), sizeof(out),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
             ? 0
             : EIO;
#else
  (void)out;
  return ENOSYS;
#endif
}
}  // namespace

Registry::Registry() noexcept {
  uint64_t salt = 0;
  const int error = draw_salt(salt);
  if (error != 0) {
    salt_errno_ = error;
    return;
  }
  salt_errno_ = 0;
  // Keep bit 31 of the salt clear and every record index below 2^31 (claim()
  // enforces it): then index ^ low32(salt) never has bit 31 set where
  // low32(~salt) does, so no issued id can equal kInvalidId.
  salt_ = salt & kSaltMask;
  salt_ready_ = true;
}

Registry& Registry::instance() {
  // Never destroyed: a HookHandle (or a thread) may call in after static
  // destructors have begun.
  static NoDestructor<Registry> reg;
  return reg.get();
}

Registry::Record* Registry::resolve(uint64_t id) noexcept {
  if (id == kInvalidId) return nullptr;
  const uint64_t raw = id ^ salt_;
  uint32_t index = static_cast<uint32_t>(raw & 0xffffffffu);
  uint32_t gen = static_cast<uint32_t>(raw >> 32);
  if (index >= records_.size()) return nullptr;
  Record& r = records_[index];
  if (!r.in_use || r.generation != gen) return nullptr;
  return &r;
}

const Registry::Record* Registry::resolve(uint64_t id) const noexcept {
  return const_cast<Registry*>(this)->resolve(id);
}

bool Registry::slot_is_live(uintptr_t addr) const noexcept {
  // An invalidated group (its image was forgotten) no longer owns its slots: the
  // caller has relinquished them, so a new instance may be hooked at the same
  // address.  forget_custom_image() removes such a group's slots from the index,
  // so presence in the index is exactly "owned by a live, non-invalidated group".
  return index_find(addr) != index_.size();
}

namespace {
// Fibonacci hashing of a GOT slot address; retain the distinguishing bit of
// adjacent 4-byte slots on a 32-bit process.
inline size_t slot_hash(uintptr_t addr, size_t mask) noexcept {
  return static_cast<size_t>((static_cast<uint64_t>(addr >> (sizeof(uintptr_t) == 8 ? 3 : 2)) *
                              0x9E3779B97F4A7C15ull) >>
                             17) &
         mask;
}
}  // namespace

size_t Registry::index_find(uintptr_t addr) const noexcept {
  const size_t cap = index_.size();
  if (cap == 0 || addr == 0) return cap;
  const size_t mask = cap - 1;
  for (size_t i = slot_hash(addr, mask);; i = (i + 1) & mask) {
    if (index_[i].address == addr) return i;
    if (index_[i].address == 0) return cap;
  }
}

void Registry::index_insert(uintptr_t addr, uint32_t record) noexcept {
  // Capacity was reserved by index_reserve(); the load factor stays <= 1/2, so
  // an empty position always exists.
  const size_t mask = index_.size() - 1;
  size_t i = slot_hash(addr, mask);
  while (index_[i].address != 0) i = (i + 1) & mask;
  index_[i].address = addr;
  index_[i].record = record;
  ++index_used_;
}

void Registry::index_erase(uintptr_t addr, uint32_t record) noexcept {
  size_t i = index_find(addr);
  if (i == index_.size() || index_[i].record != record) return;
  // Backward-shift deletion keeps every probe chain unbroken without tombstones.
  const size_t mask = index_.size() - 1;
  size_t hole = i;
  for (size_t j = (hole + 1) & mask; index_[j].address != 0; j = (j + 1) & mask) {
    const size_t home = slot_hash(index_[j].address, mask);
    // Move j into the hole if its home position does not lie cyclically in (hole, j].
    const bool in_range = hole <= j ? (home > hole && home <= j) : (home > hole || home <= j);
    if (!in_range) {
      index_[hole] = index_[j];
      hole = j;
    }
  }
  index_[hole] = IndexEntry{};
  --index_used_;
}

void Registry::index_erase_record(const Record& r, uint32_t record) noexcept {
  for (const SlotRecord& s : r.slots) index_erase(s.address, record);
}

void Registry::index_reserve(size_t extra) {
  const size_t needed = index_used_ + extra;
  size_t cap = index_.size();
  if (cap != 0 && needed * 2 <= cap) return;
  if (cap == 0) cap = 16;
  while (needed * 2 > cap) cap *= 2;
  SmallVec<IndexEntry, 16> table;
  table.reserve(cap);
  for (size_t i = 0; i < cap; ++i) table.push_back(IndexEntry{});  // reserved: no throw
  // Rehash the live entries into the new table (nothing can throw from here).
  const size_t mask = cap - 1;
  for (const IndexEntry& e : index_) {
    if (e.address == 0) continue;
    size_t i = slot_hash(e.address, mask);
    while (table[i].address != 0) i = (i + 1) & mask;
    table[i] = e;
  }
  index_ = std::move(table);
}

uint64_t Registry::claim(const ImageIdentity& image, const SlotRecord* slots, size_t count,
                         const char* system_path) {
  if (slots == nullptr || count == 0 || !salt_ready_ || ids_exhausted_) return kInvalidId;

  // No slot may already be owned by a live group (one slot, one owner).  The
  // liveness test and the insert happen under one lock, closing the
  // check-then-insert race against a concurrent claim of the same slot.
  for (size_t i = 0; i < count; ++i) {
    if (slot_is_live(slots[i].address)) return kInvalidId;
  }

  // Grow the index before anything else, so inserting below cannot fail.
  index_reserve(count);

  if (free_list_.empty()) {
    // Indices stay below 2^31 so a salted id can never equal kInvalidId.
    if (records_.size() >= kIndexLimit) throw std::length_error("Registry capacity");
    // Keep the free list able to hold every record without a later allocation,
    // so freeing a record (finish_restore/release) never throws. Reserve before
    // adding the record, so either allocation can fail without losing it.
    free_list_.reserve(records_.size() + 1);
    const auto index = static_cast<uint32_t>(records_.size());
    Record record;
    record.generation = generation_floor_;
    records_.push_back(std::move(record));
    free_list_.push_back(index);
  }

  const uint32_t index = free_list_[free_list_.size() - 1];
  Record& r = records_[index];
  // Reserve now, before any memory is modified, so that recording the group's
  // slots and page residue after the commit cannot throw bad_alloc and leave
  // the record half-updated (kOutOfMemory contract). Every
  // residual page is a slot page, so `count` entries bound the residue too.
  r.slots.reserve(count);
  r.residue.reserve(count);
  // Copy while the record is still free. Failure cannot leave an owner, and
  // moving the prepared string below cannot throw after ownership is published.
  std::string path(image.instance_token == 0 && system_path ? system_path : "");
  // Do not consume the free entry or publish ownership until all allocation
  // has succeeded. An exception leaves this record available for a retry.
  r.slots.clear();
  r.residue.clear();
  for (size_t i = 0; i < count; ++i) r.slots.push_back(slots[i]);
  r.image = image;
  r.system_path.swap(path);
  r.diagnostic = {};
  r.state = GroupState::kPrepared;
  r.in_use = true;
  free_list_.pop_back();
  for (size_t i = 0; i < count; ++i) index_insert(slots[i].address, index);
  return make_id(r.generation, index);
}

void Registry::advance_generation(Record& record) noexcept {
  if (record.generation == UINT32_MAX) {
    ids_exhausted_ = true;
  } else {
    ++record.generation;
  }
}

Error Registry::trim_idle_storage() noexcept {
  for (const auto& record : records_)
    if (record.in_use) return Error::Make(ErrorCode::kInvalidArgument);
  for (const auto& record : records_)
    if (record.generation > generation_floor_) generation_floor_ = record.generation;
  records_ = {};
  free_list_ = {};
  index_ = {};
  index_used_ = 0;
  return Error::Ok();
}

void Registry::mark_installed(uint64_t id) {
  Record* r = resolve(id);
  if (r == nullptr || r->state != GroupState::kPrepared) return;
  r->state = GroupState::kInstalled;
}

void Registry::mark_indeterminate(uint64_t id, const SlotRecord* slots, size_t count,
                                  const PageResidueRecord* pages, size_t page_count) {
  Record* r = resolve(id);
  if (r == nullptr) return;
  r->state = GroupState::kIndeterminate;
  if (slots != nullptr) {
    // The written-back slots are the group's own slots with updated values; the
    // addresses (and so the index) do not change.  `count` never exceeds the
    // reserved capacity, so these push_backs cannot allocate.
    r->slots.clear();
    for (size_t i = 0; i < count; ++i) r->slots.push_back(slots[i]);
  }
  r->residue.clear();
  for (size_t i = 0; i < page_count; ++i) r->residue.push_back(pages[i]);
}

void Registry::drop(uint64_t id) { (void)release(id); }

Error Registry::begin_restore(uint64_t id, ImageIdentity& image, SlotRecord* slots, size_t slot_cap,
                              size_t* slot_count, PageResidueRecord* pages, size_t page_cap,
                              size_t* page_count) {
  Record* r = resolve(id);
  if (r == nullptr) return Error::Make(ErrorCode::kInvalidArgument);
  if (r->image.invalidated) return Error::Make(ErrorCode::kImageInvalidated);
  if (r->state != GroupState::kInstalled && r->state != GroupState::kIndeterminate)
    return Error::Make(ErrorCode::kNotInstalled);

  r->state = GroupState::kRestoring;
  image = r->image;
  size_t n = r->slots.size() < slot_cap ? r->slots.size() : slot_cap;
  for (size_t i = 0; i < n; ++i) slots[i] = r->slots[i];
  if (slot_count != nullptr) *slot_count = r->slots.size();
  size_t pn = r->residue.size() < page_cap ? r->residue.size() : page_cap;
  for (size_t i = 0; i < pn; ++i) pages[i] = r->residue[i];
  if (page_count != nullptr) *page_count = r->residue.size();
  return Error::Ok();
}

void Registry::finish_restore(uint64_t id, bool released, GroupState not_released_state,
                              const SlotRecord* slots, size_t slot_count,
                              const PageResidueRecord* pages, size_t page_count) {
  Record* r = resolve(id);
  if (r == nullptr) return;
  if (released) {
    const auto index = index_of(id);
    if (!r->image.invalidated) index_erase_record(*r, index);
    if (r->image.invalidated) invalidate_cache(id);
    r->cache.reset();
    r->in_use = false;
    advance_generation(*r);
    r->slots.clear();
    r->residue.clear();
    r->system_path.clear();
    free_list_.push_back(index);
    return;
  }
  r->state = not_released_state;
  if (slots != nullptr) {
    r->slots.clear();
    for (size_t i = 0; i < slot_count; ++i) r->slots.push_back(slots[i]);
  }
  if (pages != nullptr) {
    r->residue.clear();
    for (size_t i = 0; i < page_count; ++i) r->residue.push_back(pages[i]);
  }
}

Error Registry::release(uint64_t id) {
  Record* r = resolve(id);
  if (r == nullptr) return Error::Make(ErrorCode::kInvalidArgument);
  const auto index = index_of(id);
  if (!r->image.invalidated) index_erase_record(*r, index);
  if (!r->residue.empty() || r->image.invalidated) invalidate_cache(id);
  r->cache.reset();
  r->in_use = false;
  advance_generation(*r);
  r->slots.clear();
  r->residue.clear();
  r->system_path.clear();
  free_list_.push_back(index);
  return Error::Ok();
}

Error Registry::snapshot_path(uint64_t id, std::string& path) const {
  const Record* r = resolve(id);
  if (r == nullptr) return Error::Make(ErrorCode::kInvalidArgument);
  path = r->system_path;
  return Error::Ok();
}

Error Registry::snapshot(uint64_t id, ImageIdentity& image, SlotRecord* slots, size_t cap,
                         size_t* count) const {
  const Record* r = resolve(id);
  if (r == nullptr) return Error::Make(ErrorCode::kInvalidArgument);
  image = r->image;
  size_t n = r->slots.size() < cap ? r->slots.size() : cap;
  for (size_t i = 0; i < n; ++i) slots[i] = r->slots[i];
  if (count != nullptr) *count = r->slots.size();
  return Error::Ok();
}

void Registry::record_diagnostic(uint64_t id, const RecoveryDiagnostic& diagnostic) noexcept {
  if (Record* r = resolve(id)) r->diagnostic = diagnostic;
}

Error Registry::recovery_snapshot(uint64_t id, RecoveryDiagnostic& diagnostic, ResidualPage* pages,
                                  size_t capacity) const noexcept {
  const Record* r = resolve(id);
  if (r == nullptr) return Error::Make(ErrorCode::kInvalidArgument);
  diagnostic = r->diagnostic;
  diagnostic.image_invalidated = r->image.invalidated;
  diagnostic.residual_page_count = r->residue.size();
  for (size_t i = 0; i < capacity && i < r->residue.size(); ++i) {
    const auto& p = r->residue[i];
    pages[i] = {p.page, static_cast<uint32_t>(p.original_prot),
                static_cast<uint32_t>(p.original_prot | kProtWrite), p.sys_errno};
  }
  return Error::Ok();
}

size_t Registry::forget_custom_image(const ImageIdentity& image) {
  size_t n = 0;
  for (size_t index = 0; index < records_.size(); ++index) {
    Record& r = records_[index];
    if (!r.in_use || r.image.invalidated) continue;
    if (r.image.instance_token == 0) continue;  // system images are not forgettable
    if (r.image.load_bias == image.load_bias && r.image.phdr_addr == image.phdr_addr &&
        r.image.phdr_count == image.phdr_count && r.image.instance_token == image.instance_token) {
      // An invalidated group no longer owns its slots; drop them from the index
      // so a new instance at the same address can be claimed.
      index_erase_record(r, static_cast<uint32_t>(index));
      invalidate_cache(make_id(r.generation, static_cast<uint32_t>(index)));
      r.cache.reset();
      r.image.invalidated = true;
      ++n;
    }
  }
  return n;
}

bool Registry::is_installed(uint64_t id) const {
  const Record* r = resolve(id);
  return r != nullptr && r->state == GroupState::kInstalled;
}

bool Registry::owns_live_replacement(uint64_t id) const {
  const Record* r = resolve(id);
  if (r == nullptr) return false;
  if (r->state == GroupState::kInstalled) return true;
  if (r->state == GroupState::kIndeterminate) {
    for (const SlotRecord& s : r->slots) {
      if (s.ever_written) return true;
    }
  }
  return false;
}

uintptr_t Registry::first_slot_of(uint64_t id) const {
  const Record* r = resolve(id);
  return (r != nullptr && !r->slots.empty()) ? r->slots[0].address : 0;
}

size_t Registry::live_count() const {
  size_t n = 0;
  for (const Record& r : records_) {
    if (r.in_use) ++n;
  }
  return n;
}

}  // namespace yukiplt::detail

namespace yukiplt::detail {
Error Registry::check_cache(uint64_t id, const MapsCacheState* cache) const noexcept {
  const auto* record = resolve(id);
  if (!record || !cache || cache->invalid) return Error::Make(ErrorCode::kInvalidArgument);
  if (record->cache && record->cache.get() != cache)
    return Error::Make(ErrorCode::kInvalidArgument);
  if (record->cache.get() == cache) return Error::Ok();
  if (!record->cache && !record->residue.empty()) return Error::Make(ErrorCode::kInvalidArgument);
  for (const auto& slot : record->slots) {
    uintptr_t page = slot.address & ~(uintptr_t(cache->page_size) - 1);
    for (const auto& other : records_) {
      if (!other.in_use || other.image.invalidated || !other.cache || other.cache.get() == cache)
        continue;
      for (const auto& target : other.slots)
        if ((target.address & ~(uintptr_t(cache->page_size) - 1)) == page)
          return Error::Make(ErrorCode::kInvalidArgument);
    }
  }
  return Error::Ok();
}
Error Registry::bind_cache(uint64_t id, const std::shared_ptr<MapsCacheState>& cache) {
  auto* record = resolve(id);
  if (!record || !cache || cache->invalid) return Error::Make(ErrorCode::kInvalidArgument);
  if (record->cache && record->cache != cache) return Error::Make(ErrorCode::kInvalidArgument);
  if (record->cache == cache) return Error::Ok();
  if (record->state != GroupState::kPrepared &&
      (record->state != GroupState::kInstalled || !record->residue.empty()))
    return Error::Make(ErrorCode::kInvalidArgument);
  const size_t ps = cache->page_size;
  for (const auto& slot : record->slots) {
    const uintptr_t page = slot.address & ~(static_cast<uintptr_t>(ps) - 1);
    for (const auto& other : records_) {
      if (!other.in_use || other.image.invalidated || !other.cache || other.cache == cache)
        continue;
      for (const auto& target : other.slots)
        if ((target.address & ~(static_cast<uintptr_t>(ps) - 1)) == page)
          return Error::Make(ErrorCode::kInvalidArgument);
    }
    VmaInfo vma;
    if (Error e = cache->lookup(page, vma)) return e;
    if (Error e = cache->remember_page(page, vma)) return e;
  }
  record->cache = cache;
  return Error::Ok();
}
CachedPage* Registry::cached_page(uintptr_t page) noexcept {
  for (auto& record : records_) {
    if (!record.in_use || record.image.invalidated || !record.cache) continue;
    const size_t ps = record.cache->page_size;
    for (const auto& slot : record.slots) {
      if ((slot.address & ~(static_cast<uintptr_t>(ps) - 1)) != page) continue;
      auto it = record.cache->pages.find(page);
      if (it != record.cache->pages.end()) return &it->second;
    }
  }
  return nullptr;
}
bool Registry::cache_bound(const MapsCacheState* cache) const noexcept {
  for (const auto& record : records_)
    if (record.in_use && record.cache.get() == cache) return true;
  return false;
}
Error Registry::prepare_cache_update(const MapsCacheState* cache, MapsCacheState& next) const {
  for (const auto& record : records_) {
    if (!record.in_use || record.image.invalidated) continue;
    if (!cache || record.cache.get() != cache) {
      // A foreign residual page can change protection after this snapshot is
      // published. Require a refresh before using it through this cache.
      for (const auto& page : record.residue) {
        VmaInfo vma;
        Error coverage = next.lookup(page.page, vma, true);
        if (coverage.code == ErrorCode::kOutOfBounds) continue;
        if (coverage) return coverage;
        if (Error e = next.remember_page(page.page, vma)) return e;
        next.pages.find(page.page)->second.invalid = true;
      }
      continue;
    }
    for (const auto& slot : record.slots) {
      VmaInfo vma;
      if (Error e = next.lookup(slot.address & ~(uintptr_t(next.page_size) - 1), vma)) return e;
      if (!vma.readable || vma.executable || vma.shared)
        return Error::Make(ErrorCode::kPermissionDenied);
    }
    for (const auto& page : record.residue) {
      VmaInfo vma;
      if (Error e = next.lookup(page.page, vma)) return e;
      int prot = prot_of(vma);
      if (prot != page.original_prot && prot != (page.original_prot | kProtWrite))
        return Error::Make(ErrorCode::kPermissionDenied);
    }
  }
  return Error::Ok();
}
bool Registry::page_pending(uintptr_t page) const noexcept {
  for (const auto& record : records_) {
    if (!record.in_use || record.image.invalidated) continue;
    for (const auto& residue : record.residue)
      if (residue.page == page) return true;
  }
  return false;
}
bool Registry::fork_clean() const noexcept {
  for (const auto& record : records_)
    if (record.in_use && (record.state != GroupState::kInstalled || !record.residue.empty()))
      return false;
  return true;
}
void Registry::invalidate_cache(uint64_t id) noexcept {
  auto* record = resolve(id);
  if (!record || !record->cache) return;
  for (const auto& slot : record->slots) {
    auto page = slot.address & ~(uintptr_t(record->cache->page_size) - 1);
    auto it = record->cache->pages.find(page);
    if (it != record->cache->pages.end()) it->second.invalid = true;
  }
}
}  // namespace yukiplt::detail
