// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "memory.hpp"

#include <cerrno>

#include "maps_cache.hpp"
#include "registry.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <atomic>

#include "common.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <algorithm>

namespace yukiplt::detail {

namespace {

/// Parse a lowercase hex run starting at s[i] (bounded by len) into `out`.
/// Advances i.  Returns false if no hex digit is present or the value would
/// overflow uintptr_t.
bool parse_hex(const char* s, size_t len, size_t& i, uintptr_t& out) noexcept {
  if (i >= len) return false;
  uintptr_t value = 0;
  size_t digits = 0;
  for (; i < len; ++i) {
    char c = s[i];
    unsigned d;
    if (c >= '0' && c <= '9') {
      d = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<unsigned>(c - 'a' + 10);
    } else {
      break;
    }
    // value = value * 16 + d, with overflow check.
    if (digits == sizeof(uintptr_t) * 2 || value > (~uintptr_t(0) >> 4)) return false;
    value <<= 4;
    value |= d;
    ++digits;
  }
  if (digits == 0) return false;
  out = value;
  return true;
}

}  // namespace

bool parse_maps_line(const char* line, size_t len, VmaInfo& out) noexcept {
  // Expected prefix: "<begin>-<end> <perms> ..." e.g.
  //   7f8a1c00000-7f8a1c01000 r--p 00000000 00:00 0  [stack]
  size_t i = 0;
  VmaInfo info;

  if (!parse_hex(line, len, i, info.begin)) return false;
  if (i >= len || line[i] != '-') return false;
  ++i;  // skip '-'
  if (!parse_hex(line, len, i, info.end)) return false;
  if (i >= len || line[i] != ' ') return false;
  ++i;  // skip ' '

  // Permission field is exactly four characters: r/w/x/- then p/s.
  if (i + 4 > len) return false;
  char r = line[i + 0];
  char w = line[i + 1];
  char x = line[i + 2];
  char p = line[i + 3];
  if (r != 'r' && r != '-') return false;
  if (w != 'w' && w != '-') return false;
  if (x != 'x' && x != '-') return false;
  if (p != 'p' && p != 's') return false;

  if (info.end <= info.begin) return false;

  info.readable = (r == 'r');
  info.writable = (w == 'w');
  info.executable = (x == 'x');
  info.shared = (p == 's');
  out = info;
  return true;
}

MapsCoverage::MapsCoverage(const uintptr_t* pages, size_t count, size_t page_size, VmaInfo* out,
                           CauseContext* cause) noexcept
    : pages_(pages), count_(count), page_size_(page_size), out_(out), cause_(cause) {
  if (pages == nullptr || out == nullptr || count == 0 || page_size == 0 ||
      (page_size & (page_size - 1)) != 0) {
    fail(ErrorCode::kInvalidArgument);
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    if ((pages[i] & (page_size - 1)) != 0 || (i > 0 && pages[i] <= pages[i - 1])) {
      fail(ErrorCode::kInvalidArgument);
      return;
    }
  }
}

void MapsCoverage::feed_line(const char* line, size_t len) noexcept {
  if (done()) return;

  VmaInfo vma;
  if (!parse_maps_line(line, len, vma)) {
    fail(ErrorCode::kVmaUnavailable);
    return;
  }
  // The kernel lists VMAs in ascending, non-overlapping order.  Anything else
  // means we are not reading what we think we are; refuse to match against it.
  if (vma.begin < prev_end_) {
    fail(ErrorCode::kVmaUnavailable);
    return;
  }
  prev_end_ = vma.end;

  // Merge walk: resolve every pending page that this VMA covers.
  while (next_ < count_) {
    const uintptr_t page = pages_[next_];
    uintptr_t page_end;
    if (!safe_add(page, static_cast<uintptr_t>(page_size_), page_end)) {
      fail(ErrorCode::kIntegerOverflow);
      return;
    }
    if (page >= vma.end) return;  // Belongs to a later VMA.
    if (page < vma.begin || page_end > vma.end) {
      // The page starts in a gap before this VMA, or straddles its end.  Either
      // way no single VMA covers it.
      fail(ErrorCode::kOutOfBounds);
      return;
    }
    out_[next_] = vma;
    ++next_;
  }
}

Error MapsCoverage::finish() const noexcept {
  if (failure_ == ErrorCode::kOutOfBounds)
    return with_cause(Error::Make(failure_), ErrorCause::kMapsCoverage, cause_);
  if (failure_ != ErrorCode::kSuccess) return Error::Make(failure_);
  if (next_ != count_)
    return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kMapsCoverage, cause_);
  return Error::Ok();
}

#if defined(__linux__) || defined(__ANDROID__)

namespace {

/// Read /proc/self/maps once, passing each line (without its newline) to
/// on_line until it returns false.  Lines longer than the internal buffer are
/// truncated; the address range and permission field always fit.  Returns 0
/// or the errno of the failed open/read.
template <typename F>
int for_each_maps_line(F&& on_line) noexcept {
  int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return errno;

  constexpr size_t kChunk = 8192;
  char buf[kChunk];
  char line[512];
  size_t line_len = 0;
  int saved_errno = 0;
  bool stop = false;

  while (!stop) {
    ssize_t n = read(fd, buf, kChunk);
    if (n < 0) {
      if (errno == EINTR) continue;
      saved_errno = errno;
      break;
    }
    if (n == 0) {
      // A final line without a newline is still a line.
      if (line_len > 0) on_line(line, line_len);
      break;
    }
    for (ssize_t k = 0; k < n && !stop; ++k) {
      char c = buf[k];
      if (c == '\n') {
        stop = !on_line(line, line_len);
        line_len = 0;
      } else if (line_len < sizeof(line)) {
        line[line_len++] = c;
      }
    }
  }

  close(fd);
  return saved_errno;
}

}  // namespace

Error read_maps_into(MapsCoverage& coverage) noexcept {
  int err = for_each_maps_line([&](const char* line, size_t len) {
    coverage.feed_line(line, len);
    return !coverage.done();
  });
  if (err != 0) return Error::Sys(ErrorCode::kVmaUnavailable, err);
  return coverage.finish();
}

Expected<VmaInfo, Error> probe_vma(uintptr_t addr, size_t size) {
  if (size == 0) return Error::Make(ErrorCode::kInvalidArgument);
  uintptr_t range_end;
  if (!safe_add(addr, static_cast<uintptr_t>(size), range_end))
    return Error::Make(ErrorCode::kIntegerOverflow);

  bool found = false;
  VmaInfo result;
  int err = for_each_maps_line([&](const char* line, size_t len) {
    VmaInfo info;
    if (parse_maps_line(line, len, info) && addr >= info.begin && range_end <= info.end) {
      result = info;
      found = true;
    }
    return !found;
  });

  if (err != 0) return Error::Sys(ErrorCode::kVmaUnavailable, err);
  if (!found) return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kMapsCoverage);
  return result;
}

#else  // non-Linux host

Error read_maps_into(MapsCoverage& coverage) noexcept {
  (void)coverage;
  return Error::Make(ErrorCode::kUnsupportedFormat);
}

Expected<VmaInfo, Error> probe_vma(uintptr_t addr, size_t size) {
  (void)addr;
  (void)size;
  return Error::Make(ErrorCode::kUnsupportedFormat);
}

#endif

// Page size

size_t page_size() noexcept {
  // Relaxed atomics suffice: every thread computes the same value, so a racing
  // first call merely repeats the query.  A plain static here would be a data
  // race (undefined behaviour) between concurrent first callers.
  static std::atomic<size_t> cached{0};
  size_t ps = cached.load(std::memory_order_relaxed);
  if (ps != 0) return ps;

#if defined(__linux__) || defined(__ANDROID__)
  // Prefer getauxval — available since glibc 2.16 and bionic API 18.
  unsigned long aux = getauxval(AT_PAGESZ);
  if (aux != 0) {
    ps = static_cast<size_t>(aux);
  } else {
    long sc = sysconf(_SC_PAGESIZE);
    ps = sc > 0 ? static_cast<size_t>(sc) : 0;
  }
#else
  // Non-Linux host build: use 4096 for compilation only.
  // Patch operations are not functional on non-Linux hosts.
  ps = 4096;
#endif

  // A page size that is not a non-zero power of two would corrupt every page
  // mask computed from it.  Report 0; callers treat it as a hard failure.
  if (ps == 0 || (ps & (ps - 1)) != 0) return 0;

  cached.store(ps, std::memory_order_relaxed);
  return ps;
}
// Operation lock

std::mutex& op_mutex() noexcept {
  static NoDestructor<std::mutex> m;
  return m.get();
}
// System platform operations

namespace {

#if defined(__linux__) || defined(__ANDROID__)

int sys_protect(void* /*ctx*/, uintptr_t page, size_t len, int prot) {
  // The VMA policy only ever admits non-executable mappings, so a request to
  // make a page both writable and executable can only be a logic error.  Refuse
  // it here as well rather than ever creating a W+X page.
  if ((prot & kProtWrite) && (prot & kProtExec)) return EINVAL;
  int p = PROT_NONE;
  if (prot & kProtRead) p |= PROT_READ;
  if (prot & kProtWrite)
    p |= PROT_WRITE;
  else if (prot & kProtExec)
    p |= PROT_EXEC;
  return mprotect(reinterpret_cast<void*>(page), len, p) == 0 ? 0 : errno;
}

Error sys_read_maps(void* /*ctx*/, MapsCoverage& coverage) { return read_maps_into(coverage); }

uintptr_t sys_load(void* /*ctx*/, uintptr_t slot) {
  return __atomic_load_n(reinterpret_cast<const uintptr_t*>(slot), __ATOMIC_ACQUIRE);
}

bool sys_cas(void* /*ctx*/, uintptr_t slot, uintptr_t* expected, uintptr_t desired) {
  // Release on success: everything the caller wrote before the CAS (notably
  // the original-function pointer a replacement calls through) is visible to
  // a thread that acquires the new slot value.
  return __atomic_compare_exchange_n(reinterpret_cast<uintptr_t*>(slot), expected, desired,
                                     /*weak=*/false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#else  // non-Linux host: nothing can be patched.

int sys_protect(void* /*ctx*/, uintptr_t /*page*/, size_t /*len*/, int /*prot*/) { return ENOSYS; }

Error sys_read_maps(void* /*ctx*/, MapsCoverage& /*coverage*/) {
  return Error::Make(ErrorCode::kUnsupportedFormat);
}

// Unreachable: prepare() fails before any load or CAS without a memory map.
uintptr_t sys_load(void* /*ctx*/, uintptr_t /*slot*/) { return 0; }

bool sys_cas(void* /*ctx*/, uintptr_t /*slot*/, uintptr_t* /*expected*/, uintptr_t /*desired*/) {
  return false;
}

#endif

}  // namespace

const PlatformOps& system_ops() noexcept {
  static const PlatformOps ops = {sys_protect, sys_read_maps, sys_load, sys_cas, nullptr};
  return ops;
}

int prot_of(const VmaInfo& vma) noexcept {
  int prot = kProtNone;
  if (vma.readable) prot |= kProtRead;
  if (vma.writable) prot |= kProtWrite;
  if (vma.executable) prot |= kProtExec;
  return prot;
}

Error Txn::prepare(const uintptr_t* slots, size_t slot_count, const uintptr_t* extra_pages,
                   size_t extra_count, CauseContext* cause) {
  if (prepared_) return Error::Make(ErrorCode::kInvalidArgument);
  if (page_size_ == 0 || (page_size_ & (page_size_ - 1)) != 0)
    return Error::Make(ErrorCode::kInvalidArgument);
  if (slot_count + extra_count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  if ((slot_count > 0 && slots == nullptr) || (extra_count > 0 && extra_pages == nullptr))
    return Error::Make(ErrorCode::kNullPointer);

  // Slots: pointer aligned (so a slot never straddles a page and the CAS is
  // a single aligned access) and unique.
  slots_.clear();
  slots_.reserve(slot_count);
  for (size_t i = 0; i < slot_count; ++i) {
    if (slots[i] % sizeof(uintptr_t) != 0) return Error::Make(ErrorCode::kInvalidArgument);
    slots_.push_back(slots[i]);
  }
  std::sort(slots_.begin(), slots_.end());
  for (size_t i = 1; i < slots_.size(); ++i) {
    if (slots_[i] == slots_[i - 1]) return Error::Make(ErrorCode::kInvalidArgument);
  }

  // Pages: every slot's page plus the extra pages, ascending and unique.
  SmallVec<uintptr_t, 8> bases;
  bases.reserve(slot_count + extra_count);
  for (uintptr_t s : slots_) bases.push_back(page_of(s));
  for (size_t i = 0; i < extra_count; ++i) {
    if (page_of(extra_pages[i]) != extra_pages[i]) return Error::Make(ErrorCode::kInvalidArgument);
    bases.push_back(extra_pages[i]);
  }
  std::sort(bases.begin(), bases.end());
  size_t unique = 0;
  for (size_t i = 0; i < bases.size(); ++i) {
    if (i == 0 || bases[i] != bases[i - 1]) bases[unique++] = bases[i];
  }

  // One pass over the memory map resolves every page.
  SmallVec<VmaInfo, 8> vmas;
  vmas.reserve(unique);
  for (size_t i = 0; i < unique; ++i) vmas.push_back(VmaInfo{});
  MapsCoverage coverage(bases.begin(), unique, page_size_, vmas.begin(), cause);
  if (cache_) {
    for (size_t i = 0; i < unique; ++i) {
      Error e = cache_->lookup(bases[i], vmas[i], false,
                               recovering_ && extra_pages &&
                                   std::find(extra_pages, extra_pages + extra_count, bases[i]) !=
                                       extra_pages + extra_count);
      if (e)
        return with_cause(e,
                          e.code == ErrorCode::kOutOfBounds ? ErrorCause::kMapsCoverage
                                                            : ErrorCause::kCacheInvalid,
                          cause);
    }
  } else {
    Error maps = ops_.read_maps(ops_.ctx, coverage);
    if (maps) return maps;
    for (size_t i = 0; i < unique; ++i) {
      if (auto* tracked = Registry::instance().cached_page(bases[i])) {
        if (prot_of(tracked->vma) != prot_of(vmas[i]) || vmas[i].shared) tracked->invalid = true;
        tracked->vma = vmas[i];
        tracked->unknown = false;
      }
    }
  }

  // VMA policy: only private, non-executable, readable mappings.
  // r--p is raised to rw-p for the write and restored; rw-p is written as is.
  for (size_t i = 0; i < unique; ++i) {
    const VmaInfo& v = vmas[i];
    if (v.shared || v.executable || !v.readable)
      return with_cause(Error::Make(ErrorCode::kPermissionDenied), ErrorCause::kVmaPolicy, cause);
  }

  pages_.clear();
  pages_.reserve(unique);
  for (size_t i = 0; i < unique; ++i) {
    Page p;
    p.base = bases[i];
    p.vma = vmas[i];
    pages_.push_back(p);
  }
  slot_seen_.clear();
  slot_seen_.reserve(slots_.size());
  for (size_t i = 0; i < slots_.size(); ++i) slot_seen_.push_back(0);

  prepared_ = true;
  return Error::Ok();
}

size_t Txn::find_page(uintptr_t addr) const noexcept {
  const uintptr_t base = page_of(addr);
  size_t lo = 0;
  size_t hi = pages_.size();
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (pages_[mid].base < base) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return (lo < pages_.size() && pages_[lo].base == base) ? lo : pages_.size();
}

size_t Txn::find_slot(uintptr_t slot) const noexcept {
  const uintptr_t* begin = slots_.begin();
  const uintptr_t* end = slots_.end();
  const uintptr_t* it = std::lower_bound(begin, end, slot);
  return (it != end && *it == slot) ? static_cast<size_t>(it - begin) : slots_.size();
}

bool Txn::load(uintptr_t slot, uintptr_t& out) const noexcept {
  if (!prepared_ || find_slot(slot) == slots_.size()) return false;
  out = ops_.load(ops_.ctx, slot);
  return true;
}

bool Txn::page_vma(uintptr_t page, VmaInfo& out) const noexcept {
  if (!prepared_) return false;
  size_t i = find_page(page);
  if (i == pages_.size() || pages_[i].base != page) return false;
  out = pages_[i].vma;
  return true;
}

int Txn::protect(uintptr_t page, int prot) noexcept {
  const int error = ops_.protect(ops_.ctx, page, page_size_, prot);
  if (auto* tracked = Registry::instance().cached_page(page)) {
    tracked->unknown = error != 0;
    if (!error) {
      tracked->vma.readable = (prot & kProtRead) != 0;
      tracked->vma.writable = (prot & kProtWrite) != 0;
      tracked->vma.executable = (prot & kProtExec) != 0;
    }
  }
  return error;
}

int Txn::protect_page(uintptr_t page, int prot) noexcept {
  if (!prepared_) return EINVAL;
  size_t i = find_page(page);
  if (i == pages_.size() || pages_[i].base != page) return EINVAL;
  return protect(page, prot);
}

int Txn::repair_page(uintptr_t page, int prot) noexcept {
  int error = protect_page(page, prot);
  if (!error) {
    auto& vma = pages_[find_page(page)].vma;
    vma.readable = (prot & kProtRead) != 0;
    vma.writable = (prot & kProtWrite) != 0;
    vma.executable = (prot & kProtExec) != 0;
  }
  return error;
}

void Txn::restore_pages(CommitResult& result) noexcept {
  // Restore this transaction's single maps snapshot. A cooperating caller
  // serializes other page-protection writers; another maps read here could
  // still race mprotect. Cross-transaction residue retries separately validate
  // the historical protection against their new prepare snapshot.
  for (size_t k = pages_.size(); k-- > 0;) {
    Page& p = pages_[k];
    if (!p.raised || p.restore_failed) continue;
    int err = protect(p.base, prot_of(p.vma));
    if (err == 0) {
      p.raised = false;
    } else {
      p.restore_failed = true;
      p.restore_errno = err;
      result.pages_restored = false;
      if (result.restore_errno == 0) result.restore_errno = err;
    }
  }
}

CommitResult Txn::commit(SlotOp* ops, size_t count, CommitMode mode, void (*before_cas)(void*),
                         void* ctx) noexcept {
  CommitResult result;
  if (!prepared_ || committed_ || ops == nullptr || count == 0) {
    result.invalid = true;
    return result;
  }

  // Every op must name a distinct prepared slot.
  for (size_t i = 0; i < slot_seen_.size(); ++i) slot_seen_[i] = 0;
  for (size_t i = 0; i < count; ++i) {
    size_t idx = find_slot(ops[i].slot);
    if (idx == slots_.size() || slot_seen_[idx] != 0) {
      result.invalid = true;
      return result;
    }
    slot_seen_[idx] = 1;
  }
  committed_ = true;

  for (size_t i = 0; i < count; ++i) {
    ops[i].outcome = SlotOutcome::kUntouched;
    ops[i].observed = 0;
    pages_[find_page(ops[i].slot)].used = true;
  }

  // 1. Raise each used page lacking write, ascending, once.
  for (size_t k = 0; k < pages_.size(); ++k) {
    Page& p = pages_[k];
    if (!p.used || p.vma.writable) continue;
    int err = protect(p.base, prot_of(p.vma) | kProtWrite);
    if (err != 0) {
      if (cache_ || Registry::instance().cached_page(p.base)) p.raised = true;
      result.raise_errno = err;
      restore_pages(result);
      return result;
    }
    p.raised = true;
  }

  // 2. Publish caller data now that no raise can fail any more.
  if (before_cas != nullptr) before_cas(ctx);

  // 3. CAS.
  if (mode == CommitMode::kAllOrNothing) {
    for (size_t i = 0; i < count; ++i) {
      SlotOp& op = ops[i];
      uintptr_t seen = op.expected;
      if (ops_.cas(ops_.ctx, op.slot, &seen, op.desired)) {
        op.outcome = SlotOutcome::kCommitted;
        op.observed = op.desired;
        continue;
      }
      op.outcome = SlotOutcome::kConflict;
      op.observed = seen;
      ++result.conflicts;
      // Revert what this commit already wrote, newest first.
      for (size_t j = i; j-- > 0;) {
        SlotOp& prev = ops[j];
        uintptr_t found = prev.desired;
        if (ops_.cas(ops_.ctx, prev.slot, &found, prev.expected)) {
          prev.outcome = SlotOutcome::kRolledBack;
          prev.observed = prev.expected;
        } else {
          prev.outcome = SlotOutcome::kRollbackFailed;
          prev.observed = found;
          result.rollback_failed = true;
        }
      }
      break;
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      SlotOp& op = ops[i];
      uintptr_t seen = op.expected;
      if (ops_.cas(ops_.ctx, op.slot, &seen, op.desired)) {
        op.outcome = SlotOutcome::kCommitted;
        op.observed = op.desired;
      } else {
        op.outcome = SlotOutcome::kConflict;
        op.observed = seen;
        ++result.conflicts;
      }
    }
  }

  // 4. Put back every page this commit raised.
  restore_pages(result);

  result.all_committed = true;
  for (size_t i = 0; i < count; ++i) {
    if (ops[i].outcome != SlotOutcome::kCommitted) result.all_committed = false;
  }
  return result;
}

size_t Txn::residue(PageResidue* out, size_t cap) const noexcept {
  size_t n = 0;
  for (const Page& p : pages_) {
    if (!(p.raised && p.restore_failed)) continue;
    if (out != nullptr && n < cap) {
      out[n].page = p.base;
      out[n].original_prot = prot_of(p.vma);
      out[n].raised_prot = prot_of(p.vma) | kProtWrite;
      out[n].sys_errno = p.restore_errno;
    }
    ++n;
  }
  return n;
}

const PlatformOps& active_ops() noexcept { return system_ops(); }
}  // namespace yukiplt::detail
