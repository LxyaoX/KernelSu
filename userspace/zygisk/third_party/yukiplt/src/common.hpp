// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_COMMON_HPP_
#define YUKIPLT_COMMON_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "yukiplt.hpp"

namespace yukiplt::detail {
Error process_entry() noexcept;
// Stack-owned admission. A nested operation borrows the active thread's lease.
class AdmittedOperation;
class OperationGuard {
 public:
  OperationGuard() noexcept;
  ~OperationGuard();
  OperationGuard(const OperationGuard&) = delete;
  OperationGuard& operator=(const OperationGuard&) = delete;
  Error error() const noexcept { return error_; }
  AdmittedOperation context() const noexcept;
  OperationGuard* next = nullptr;
  unsigned thread = 0;

 private:
  Error error_;
  bool owns_ = false;
};

// A borrowed lexical proof for direct internal calls; never retain it or pass
// it across a host callback or process transition.
class AdmittedOperation {
 public:
  Error error() const noexcept { return guard_.error(); }

 private:
  friend class OperationGuard;
  explicit AdmittedOperation(const OperationGuard& guard) noexcept : guard_(guard) {}
  const OperationGuard& guard_;
};
inline AdmittedOperation OperationGuard::context() const noexcept {
  return AdmittedOperation(*this);
}
Error query_recovery_admitted(const AdmittedOperation&, const HookHandle&, RecoveryDiagnostic*,
                              ResidualPage*, size_t);

// Admission failure diagnostics use only caller-owned scalar storage.
inline void admission_diagnostic(OperationDiagnostic* out, Error error) noexcept {
  if (!out) return;
  *out = {};
  out->stage = OperationStage::kPrepare;
  out->cause = ErrorCause::kForkProtocol;
  out->error = error;
}
inline void admission_diagnostic(RecoveryDiagnostic* out, Error error) noexcept {
  if (!out) return;
  *out = {};
  out->stage = OperationStage::kPrepare;
  out->error = error;
}
inline void admission_diagnostic(yukiplt_c_operation_diagnostic* out, Error error) noexcept {
  if (!out || out->struct_size != sizeof(*out)) return;
  *out = {};
  out->struct_size = sizeof(*out);
  out->stage = YUKIPLT_C_STAGE_PREPARE;
  out->cause = YUKIPLT_C_CAUSE_FORK_PROTOCOL;
  out->error = {static_cast<uint32_t>(error.code), error.sys_errno};
}
inline void admission_diagnostic(yukiplt_c_recovery_diagnostic* out, Error error) noexcept {
  if (!out) return;
  *out = {};
  out->stage = YUKIPLT_C_STAGE_PREPARE;
  out->error = {static_cast<uint32_t>(error.code), error.sys_errno};
}

/// Unsigned addition with overflow detection.
/// Returns false on overflow; result is undefined on overflow.
inline bool safe_add(uintptr_t a, uintptr_t b, uintptr_t& result) noexcept {
  result = a + b;
  return result >= a;
}

inline bool safe_add64(uint64_t a, uint64_t b, uint64_t& result) noexcept {
  result = a + b;
  return result >= a;
}

inline bool safe_mul(uintptr_t a, uintptr_t b, uintptr_t& result) noexcept {
  if (b != 0 && a > std::numeric_limits<uintptr_t>::max() / b) return false;
  result = a * b;
  return true;
}

inline bool narrow_address(uint64_t value, uintptr_t& result) noexcept {
  if (value > std::numeric_limits<uintptr_t>::max()) return false;
  result = static_cast<uintptr_t>(value);
  return true;
}

// Inline process state without exit-time destruction; late handles and threads
// must not access a destroyed Registry or operation mutex.
template <typename T>
class NoDestructor {
 public:
  template <typename... Args>
  explicit NoDestructor(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  [[nodiscard]] T& get() noexcept { return *std::launder(reinterpret_cast<T*>(storage_)); }

 private:
  alignas(T) unsigned char storage_[sizeof(T)];
};

/// Fixed-capacity inline storage with heap overflow.
///
/// Stores up to N elements on the stack.  When the count exceeds N, all
/// elements are moved to a heap allocation that doubles in capacity.
template <typename T, size_t N = 8>
class SmallVec {
  static_assert(std::is_trivially_destructible_v<T> || std::is_nothrow_destructible_v<T>,
                "T must be trivially or nothrow destructible");

 public:
  SmallVec() noexcept = default;

  ~SmallVec() {
    clear();
    if (heap_) ::operator delete(heap_);
  }

  SmallVec(const SmallVec& o) {
    reserve(o.size_);
    for (size_t i = 0; i < o.size_; ++i) new (ptr() + i) T(o[i]);
    size_ = o.size_;
  }

  SmallVec(SmallVec&& o) noexcept {
    if (o.on_heap()) {
      heap_ = o.heap_;
      capacity_ = o.capacity_;
      size_ = o.size_;
      o.heap_ = nullptr;
      o.capacity_ = 0;
      o.size_ = 0;
    } else {
      for (size_t i = 0; i < o.size_; ++i) new (inline_ptr() + i) T(std::move(o.inline_ptr()[i]));
      size_ = o.size_;
      o.clear();
    }
  }

  SmallVec& operator=(const SmallVec& o) {
    if (this != &o) {
      clear();
      reserve(o.size_);
      for (size_t i = 0; i < o.size_; ++i) new (ptr() + i) T(o[i]);
      size_ = o.size_;
    }
    return *this;
  }

  SmallVec& operator=(SmallVec&& o) noexcept {
    if (this != &o) {
      clear();
      if (heap_) {
        ::operator delete(heap_);
        heap_ = nullptr;
      }
      if (o.on_heap()) {
        heap_ = o.heap_;
        capacity_ = o.capacity_;
        size_ = o.size_;
        o.heap_ = nullptr;
        o.capacity_ = 0;
        o.size_ = 0;
      } else {
        capacity_ = 0;
        for (size_t i = 0; i < o.size_; ++i) new (inline_ptr() + i) T(std::move(o.inline_ptr()[i]));
        size_ = o.size_;
        o.clear();
      }
    }
    return *this;
  }

  void push_back(const T& val) {
    ensure_capacity(size_ + 1);
    new (ptr() + size_) T(val);
    ++size_;
  }

  void push_back(T&& val) {
    ensure_capacity(size_ + 1);
    new (ptr() + size_) T(std::move(val));
    ++size_;
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    ensure_capacity(size_ + 1);
    T* p = new (ptr() + size_) T(std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_t i = 0; i < size_; ++i) ptr()[i].~T();
    }
    size_ = 0;
  }

  /// Remove the last element.  Undefined if empty.
  void pop_back() noexcept {
    --size_;
    if constexpr (!std::is_trivially_destructible_v<T>) ptr()[size_].~T();
  }

  void reserve(size_t new_cap) {
    if (new_cap <= capacity()) return;
    grow(new_cap);
  }

  [[nodiscard]] T& operator[](size_t i) noexcept { return ptr()[i]; }
  [[nodiscard]] const T& operator[](size_t i) const noexcept { return ptr()[i]; }

  [[nodiscard]] T* begin() noexcept { return ptr(); }
  [[nodiscard]] T* end() noexcept { return ptr() + size_; }
  [[nodiscard]] const T* begin() const noexcept { return ptr(); }
  [[nodiscard]] const T* end() const noexcept { return ptr() + size_; }
  [[nodiscard]] const T* data() const noexcept { return ptr(); }

  [[nodiscard]] size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_t capacity() const noexcept { return on_heap() ? capacity_ : N; }

 private:
  [[nodiscard]] bool on_heap() const noexcept { return heap_ != nullptr; }

  [[nodiscard]] T* inline_ptr() noexcept { return reinterpret_cast<T*>(&inline_storage_); }
  [[nodiscard]] const T* inline_ptr() const noexcept {
    return reinterpret_cast<const T*>(&inline_storage_);
  }

  [[nodiscard]] T* ptr() noexcept { return on_heap() ? heap_ : inline_ptr(); }
  [[nodiscard]] const T* ptr() const noexcept { return on_heap() ? heap_ : inline_ptr(); }

  void ensure_capacity(size_t needed) {
    if (needed <= capacity()) return;
    size_t new_cap = capacity() * 2;
    if (new_cap < needed) new_cap = needed;
    grow(new_cap);
  }

  void grow(size_t new_cap) {
    T* new_buf = static_cast<T*>(::operator new(new_cap * sizeof(T)));
    T* old = ptr();
    for (size_t i = 0; i < size_; ++i) {
      new (new_buf + i) T(std::move(old[i]));
      old[i].~T();
    }
    if (heap_) ::operator delete(heap_);
    heap_ = new_buf;
    capacity_ = new_cap;
  }

  // Inline storage: aligned for T, room for N elements.
  typename std::aligned_storage<sizeof(T) * N, alignof(T)>::type inline_storage_{};
  T* heap_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
};

struct CauseContext {
  ErrorCause value = ErrorCause::kNone;
};

inline void note_cause(ErrorCause value, CauseContext* cause = nullptr) noexcept {
  if (cause != nullptr) cause->value = value;
}

inline Error with_cause(Error error, ErrorCause value, CauseContext* cause = nullptr) noexcept {
  note_cause(value, cause);
  return error;
}

/// The cause to report for a final `error`: the noted cause when it is one of
/// the causes `error.code` can have, otherwise the code's default cause.
ErrorCause resolve_cause(const Error& error, ErrorCause noted) noexcept;

// Map contained exceptions: bad_alloc to OutOfMemory, length_error to
// IntegerOverflow, others to UnsupportedFormat with InternalException.
// Host callbacks use OutOfMemory or UnsupportedFormat with HostCallback.

enum class ExceptionOrigin { kInternal, kHostCallback };

/// Map the exception currently being handled.  Call only inside a catch block.
inline Error current_exception_error(ExceptionOrigin origin = ExceptionOrigin::kInternal,
                                     CauseContext* cause = nullptr) noexcept {
  try {
    throw;
  } catch (const std::bad_alloc&) {
    const auto error = Error::Make(ErrorCode::kOutOfMemory);
    return origin == ExceptionOrigin::kHostCallback
               ? with_cause(error, ErrorCause::kHostCallback, cause)
               : error;
  } catch (const std::length_error&) {
    if (origin == ExceptionOrigin::kHostCallback)
      return with_cause(Error::Make(ErrorCode::kUnsupportedFormat), ErrorCause::kHostCallback,
                        cause);
    return Error::Make(ErrorCode::kIntegerOverflow);
  } catch (...) {
    return with_cause(Error::Make(ErrorCode::kUnsupportedFormat),
                      origin == ExceptionOrigin::kHostCallback ? ErrorCause::kHostCallback
                                                               : ErrorCause::kInternalException,
                      cause);
  }
}

// Per-operation work limits, shared across selected sources and candidate images.
// Bytes count cumulative logical records/index payloads/text copied, including
// temporaries. Container slack, allocator overhead and host callbacks are not an
// RSS limit. No refund: a released temporary must not grant unlimited new work.
struct ScanBudget {
  size_t relocations = size_t{1} << 22;
  size_t bytes = size_t{64} << 20;
  CauseContext* cause = nullptr;

  Error take_records(size_t count) noexcept {
    if (count > relocations)
      return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget, cause);
    relocations -= count;
    return Error::Ok();
  }
  Error take_bytes(size_t count) noexcept {
    if (count > bytes)
      return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget, cause);
    bytes -= count;
    return Error::Ok();
  }
  template <typename T>
  Error take_objects(size_t count) noexcept {
    if (count > bytes / sizeof(T))
      return with_cause(Error::Make(ErrorCode::kOutOfBounds), ErrorCause::kScanBudget, cause);
    return take_bytes(count * sizeof(T));
  }
};
}  // namespace yukiplt::detail

#endif  // YUKIPLT_COMMON_HPP_
