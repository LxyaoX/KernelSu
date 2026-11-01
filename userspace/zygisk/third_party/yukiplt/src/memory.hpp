// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_MEMORY_HPP_
#define YUKIPLT_MEMORY_HPP_

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "common.hpp"
#include "yukiplt.hpp"

namespace yukiplt::detail {

/// One virtual memory area, as reported by /proc/self/maps.
struct VmaInfo {
  uintptr_t begin = 0;
  uintptr_t end = 0;        ///< Exclusive.
  bool readable = false;    ///< 'r'
  bool writable = false;    ///< 'w'
  bool executable = false;  ///< 'x'
  bool shared = false;      ///< 's' (true) vs private 'p' (false).
};

/// Parse a single /proc/self/maps line, reading only the address range and
/// permission field.  `line` need not be NUL-terminated; `len` bounds it.
///
/// Returns false if the line does not begin with a well-formed
/// "begin-end rwxp" prefix.  This parser performs no I/O.
bool parse_maps_line(const char* line, size_t len, VmaInfo& out) noexcept;

/// Resolves, from one ordered pass over the memory map, the VMA that covers
/// each page of a set.
///
/// Feed every maps line in file order with feed_line(), stopping early once
/// done() is true, then call finish().  Each page must lie entirely inside a
/// single VMA.  Pure logic: it does no I/O.
///
/// Errors reported by finish():
///   - kInvalidArgument   the page set is not ascending, unique and aligned,
///                        or the page size is not a power of two;
///   - kVmaUnavailable    a line is malformed or the lines are not in
///                        ascending, non-overlapping order;
///   - kOutOfBounds       some page is not covered, or straddles two VMAs;
///   - kIntegerOverflow   a page end overflows the address space.
class MapsCoverage {
 public:
  /// `pages` (ascending, unique, aligned to `page_size`) and `out` (one entry
  /// per page) must outlive this object.  out[i] receives the VMA of pages[i].
  MapsCoverage(const uintptr_t* pages, size_t count, size_t page_size, VmaInfo* out,
               CauseContext* cause = nullptr) noexcept;

  /// Consume one maps line (without its trailing newline).
  void feed_line(const char* line, size_t len) noexcept;

  /// True once every page is resolved or a failure has been recorded; the
  /// reader may stop feeding lines.
  [[nodiscard]] bool done() const noexcept {
    return failure_ != ErrorCode::kSuccess || next_ == count_;
  }

  /// The outcome after the last line has been fed.
  [[nodiscard]] Error finish() const noexcept;

 private:
  void fail(ErrorCode code) noexcept {
    if (failure_ == ErrorCode::kSuccess) failure_ = code;
  }

  const uintptr_t* pages_;
  size_t count_;
  size_t page_size_;
  VmaInfo* out_;
  size_t next_ = 0;
  uintptr_t prev_end_ = 0;
  ErrorCode failure_ = ErrorCode::kSuccess;
  CauseContext* cause_;
};

/// Feed /proc/self/maps to `coverage` in one pass and return its outcome.
///
/// Returns kVmaUnavailable (with errno) if the file cannot be opened or read,
/// otherwise coverage.finish().  kUnsupportedFormat on non-Linux hosts.
Error read_maps_into(MapsCoverage& coverage) noexcept;

/// Find the VMA that fully covers [addr, addr + size) by reading
/// /proc/self/maps.  Diagnostic helper; transactions use MapsCoverage.
///
/// Returns:
///   - the covering VmaInfo on success,
///   - kOutOfBounds if no single mapping covers the whole range,
///   - kIntegerOverflow if addr + size overflows,
///   - kVmaUnavailable (with errno) if /proc/self/maps cannot be read,
///   - kUnsupportedFormat on non-Linux hosts.
///
/// The returned protection is a snapshot at read time; /proc/self/maps offers
/// no atomic guarantee against a concurrent mprotect/mmap/munmap by other code.
Expected<VmaInfo, Error> probe_vma(uintptr_t addr, size_t size);

/// Page protection bits as used by the engine.  System ops translate them to
/// PROT_*; keeping them abstract lets the engine build and run on any host.
enum ProtBits : int {
  kProtNone = 0,
  kProtRead = 1,
  kProtWrite = 2,
  kProtExec = 4,
};

/// The protection bits a VMA reports.
int prot_of(const VmaInfo& vma) noexcept;

/// Side effects the engine needs.  Every function receives `ctx`.
struct PlatformOps {
  /// Set the protection of [page, page + len).  Returns 0 or an errno value.
  int (*protect)(void* ctx, uintptr_t page, size_t len, int prot);
  /// Feed the current memory map to `coverage` in one pass; return its outcome.
  Error (*read_maps)(void* ctx, MapsCoverage& coverage);
  /// Atomically load a slot (acquire).
  uintptr_t (*load)(void* ctx, uintptr_t slot);
  /// Atomically replace *slot with `desired` if it equals *expected.  On
  /// failure *expected receives the value found.  Success publishes prior
  /// writes (release).
  bool (*cas)(void* ctx, uintptr_t slot, uintptr_t* expected, uintptr_t desired);
  void* ctx;
};

/// What happened to one slot during a commit.
enum class SlotOutcome : uint8_t {
  kUntouched,       ///< No write was attempted.
  kCommitted,       ///< The CAS succeeded; the slot holds `desired`.
  kConflict,        ///< The CAS failed; the slot held `observed`, left as found.
  kRolledBack,      ///< Committed, then reverted to `expected` after another slot failed.
  kRollbackFailed,  ///< Committed, but the revert CAS failed; the slot held `observed`.
};

/// One requested slot write.  The caller fills slot/expected/desired; commit()
/// fills outcome/observed.
struct SlotOp {
  uintptr_t slot = 0;
  uintptr_t expected = 0;
  uintptr_t desired = 0;
  SlotOutcome outcome = SlotOutcome::kUntouched;
  uintptr_t observed = 0;
};

enum class CommitMode : uint8_t {
  /// Stop at the first conflict and revert, in reverse order, every slot this
  /// commit already wrote.
  kAllOrNothing,
  /// Attempt every slot independently; a conflict on one does not affect the
  /// others and nothing is reverted.
  kIndependent,
};

/// A page this transaction raised to writable but could not restore.
struct PageResidue {
  uintptr_t page = 0;
  int original_prot = kProtNone;  ///< Protection the live VMA reported.
  int raised_prot = kProtNone;    ///< Protection we set it to.
  int sys_errno = 0;
};

/// Summary of a commit.  Per-slot detail is in the SlotOps.
struct CommitResult {
  bool invalid = false;          ///< Ops did not match the prepared slots; nothing was done.
  bool all_committed = false;    ///< Every op ended kCommitted.
  bool rollback_failed = false;  ///< Some op ended kRollbackFailed.
  bool pages_restored = true;    ///< Every page raised by this commit was put back.
  int raise_errno = 0;           ///< Non-zero: raising a page failed and no slot was written.
  int restore_errno = 0;         ///< First errno of a failed restore.
  size_t conflicts = 0;          ///< Number of ops that ended kConflict.
};

struct MapsCacheState;
class Txn {
 public:
  Txn(const PlatformOps& ops, size_t page_size, MapsCacheState* cache = nullptr,
      bool recovering = false) noexcept
      : ops_(ops), cache_(cache), recovering_(recovering), page_size_(page_size) {}

  Txn(const Txn&) = delete;
  Txn& operator=(const Txn&) = delete;

  /// Plan a transaction over `slots`, plus `extra_pages` that are only
  /// observed (e.g. pages left raised by an earlier failed commit).
  ///
  /// Reads the memory map exactly once.  Fails, without writing anything, if:
  ///   - kInvalidArgument   no slots and no pages, a slot is not native-pointer
  ///                        aligned, a slot appears twice, an extra page is
  ///                        unaligned, or the page size is unusable;
  ///   - kOutOfBounds       some page is unmapped or split across VMAs;
  ///   - kPermissionDenied  some page is shared, executable or unreadable;
  ///   - kVmaUnavailable    the memory map cannot be read or parsed.
  ///
  /// May throw std::bad_alloc; call it before any write.
  Error prepare(const uintptr_t* slots, size_t slot_count, const uintptr_t* extra_pages = nullptr,
                size_t extra_count = 0, CauseContext* cause = nullptr);

  /// Atomically load a prepared slot.  Returns false if `slot` was not
  /// prepared (its page has not been confirmed mapped and readable).
  bool load(uintptr_t slot, uintptr_t& out) const noexcept;

  /// The VMA observed for a prepared page.  Returns false if not prepared.
  bool page_vma(uintptr_t page, VmaInfo& out) const noexcept;

  /// Set the protection of a prepared page.  Used to put back a page left
  /// raised by an earlier commit.  Returns 0, an errno value, or EINVAL if the
  /// page was not prepared.
  int protect_page(uintptr_t page, int prot) noexcept;
  int repair_page(uintptr_t page, int prot) noexcept;

  /// Apply `ops`.  Every op's slot must have been passed to prepare(), each at
  /// most once.  A Txn commits at most once; a second call is `invalid`.
  ///
  ///   1. Raise, in ascending order and once each, the pages used by `ops`
  ///      whose VMA lacks write.  If one fails, restore those already raised
  ///      and return with raise_errno set; no slot is written.
  ///   2. Call `before_cas(ctx)` once, if given.  It runs only after every
  ///      raise succeeded and before any CAS, so data it publishes is visible
  ///      before a replacement becomes reachable.  It must not throw.
  ///   3. CAS each op in order (see CommitMode).
  ///   4. Restore, in descending order, every page raised in step 1, even if
  ///      an earlier restore failed.
  CommitResult commit(SlotOp* ops, size_t count, CommitMode mode,
                      void (*before_cas)(void* ctx) = nullptr, void* ctx = nullptr) noexcept;

  /// Pages that commit() raised but could not restore.  Writes up to `cap`
  /// entries and returns the total number.
  size_t residue(PageResidue* out, size_t cap) const noexcept;

  [[nodiscard]] uintptr_t page_of(uintptr_t addr) const noexcept {
    return addr & ~(static_cast<uintptr_t>(page_size_) - 1);
  }

 private:
  struct Page {
    uintptr_t base = 0;
    VmaInfo vma;
    bool used = false;            ///< Touched by the current commit.
    bool raised = false;          ///< Currently raised by this transaction.
    bool restore_failed = false;  ///< A restore was attempted and failed.
    int restore_errno = 0;
  };

  /// Index of the prepared page containing `addr`, or pages_.size() if none.
  size_t find_page(uintptr_t addr) const noexcept;

  /// Index of a prepared slot, or slots_.size() if not prepared.
  size_t find_slot(uintptr_t slot) const noexcept;

  /// Restore every raised page, highest address first.
  void restore_pages(CommitResult& result) noexcept;

  int protect(uintptr_t page, int prot) noexcept;
  const PlatformOps& ops_;
  MapsCacheState* cache_;
  bool recovering_;
  size_t page_size_;
  SmallVec<Page, 4> pages_;
  SmallVec<uintptr_t, 8> slots_;    ///< Prepared slots, sorted.
  SmallVec<uint8_t, 8> slot_seen_;  ///< Scratch for commit's duplicate check.
  bool prepared_ = false;
  bool committed_ = false;
};

/// Get the runtime page size (cached after first call).  Returns 0 if the
/// system reports a value that is not a non-zero power of two.
size_t page_size() noexcept;

/// The real platform: mprotect, /proc/self/maps, and __atomic builtins on the
/// slot.  On non-Linux hosts read_maps fails with kUnsupportedFormat, so no
/// transaction can reach a write.
const PlatformOps& system_ops() noexcept;

/// Platform operations used by public entry points.
const PlatformOps& active_ops() noexcept;

// Per-copy operation lock serializing maps, permissions, slots and Registry.
// Lock order is linker -> op; never enter the loader while holding op.
// Other library copies and third-party writers remain independent. Never destroyed.
std::mutex& op_mutex() noexcept;

}  // namespace yukiplt::detail

#endif  // YUKIPLT_MEMORY_HPP_
