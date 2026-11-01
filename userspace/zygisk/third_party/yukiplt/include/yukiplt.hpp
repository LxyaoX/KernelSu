// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_HPP_
#define YUKIPLT_HPP_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yukiplt.h"

/// \brief Portable ELF type definitions for YukiPLT.
///
/// On Linux/Android, <elf.h> provides these.  On other hosts (Windows, macOS)
/// portable definitions are provided for other hosts.
/// Values come from the ELF specification and supported architecture ELF ABIs.

#if defined(__linux__) || defined(__ANDROID__)
#include <elf.h>
#else

#include <cstdint>

// --- Basic ELF types (64-bit) ---
using Elf64_Addr = uint64_t;
using Elf64_Off = uint64_t;
using Elf64_Half = uint16_t;
using Elf64_Word = uint32_t;
using Elf64_Sword = int32_t;
using Elf64_Xword = uint64_t;
using Elf64_Sxword = int64_t;

using Elf32_Addr = uint32_t;
using Elf32_Off = uint32_t;
using Elf32_Half = uint16_t;
using Elf32_Word = uint32_t;
using Elf32_Sword = int32_t;

struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};
struct Elf32_Dyn {
  Elf32_Sword d_tag;
  union {
    Elf32_Word d_val;
    Elf32_Addr d_ptr;
  } d_un;
};
struct Elf32_Sym {
  Elf32_Word st_name;
  Elf32_Addr st_value;
  Elf32_Word st_size;
  unsigned char st_info;
  unsigned char st_other;
  Elf32_Half st_shndx;
};
struct Elf32_Rel {
  Elf32_Addr r_offset;
  Elf32_Word r_info;
};
struct Elf32_Rela {
  Elf32_Addr r_offset;
  Elf32_Word r_info;
  Elf32_Sword r_addend;
};
struct Elf64_Rel {
  Elf64_Addr r_offset;
  Elf64_Xword r_info;
};

// --- ELF Header ---
#define EI_NIDENT 16

struct Elf64_Ehdr {
  unsigned char e_ident[EI_NIDENT];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
};

// --- Program Header ---
struct Elf64_Phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
};

// --- Dynamic Section ---
struct Elf64_Dyn {
  Elf64_Sxword d_tag;
  union {
    Elf64_Xword d_val;
    Elf64_Addr d_ptr;
  } d_un;
};

// --- Symbol Table ---
struct Elf64_Sym {
  Elf64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  Elf64_Half st_shndx;
  Elf64_Addr st_value;
  Elf64_Xword st_size;
};

// --- Relocation (with addend) ---
struct Elf64_Rela {
  Elf64_Addr r_offset;
  Elf64_Xword r_info;
  Elf64_Sxword r_addend;
};

// --- e_ident indices ---
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6

// --- e_ident values ---
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1

// --- e_type ---
#define ET_DYN 3

// --- e_machine ---
#define EM_AARCH64 183

// --- p_type ---
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_GNU_RELRO 0x6474e552

// --- p_flags ---
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// --- d_tag ---
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_JMPREL 23

// GNU extensions
#define DT_GNU_HASH 0x6ffffef5
#define DT_VERSYM 0x6ffffff0
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED 0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff

struct Elf64_Verneed {
  Elf64_Half vn_version;
  Elf64_Half vn_cnt;
  Elf64_Word vn_file;
  Elf64_Word vn_aux;
  Elf64_Word vn_next;
};
struct Elf64_Vernaux {
  Elf64_Word vna_hash;
  Elf64_Half vna_flags;
  Elf64_Half vna_other;
  Elf64_Word vna_name;
  Elf64_Word vna_next;
};
struct Elf64_Verdef {
  Elf64_Half vd_version;
  Elf64_Half vd_flags;
  Elf64_Half vd_ndx;
  Elf64_Half vd_cnt;
  Elf64_Word vd_hash;
  Elf64_Word vd_aux;
  Elf64_Word vd_next;
};
struct Elf64_Verdaux {
  Elf64_Word vda_name;
  Elf64_Word vda_next;
};

using Elf32_Verneed = Elf64_Verneed;
using Elf32_Vernaux = Elf64_Vernaux;
using Elf32_Verdef = Elf64_Verdef;
using Elf32_Verdaux = Elf64_Verdaux;

// --- Relocation info macros ---
#define ELF64_R_SYM(info) ((info) >> 32)
#define ELF64_R_TYPE(info) ((info) & 0xffffffffULL)
#define ELF64_R_INFO(sym, type) (((Elf64_Xword)(sym) << 32) | ((type) & 0xffffffffULL))

// --- AArch64 relocation types ---
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027
#define R_AARCH64_IRELATIVE 1032

// --- Symbol binding / type macros ---
#define ELF64_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_TYPE(info) ((info) & 0xf)
#define ELF64_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xf))

#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_FUNC 2
#define STT_OBJECT 1

#define SHN_UNDEF 0

#endif  // __linux__ || __ANDROID__

namespace yukiplt {

// The spelling changes with the compilation ABI; the ARM64 aliases are the
// original ELF64 types, preserving the existing public layout and type identity.
#if YUKIPLT_ELF_BITS == 64
using NativeElfEhdr = Elf64_Ehdr;
using NativeElfPhdr = Elf64_Phdr;
using NativeElfDyn = Elf64_Dyn;
using NativeElfSym = Elf64_Sym;
using NativeElfAddr = Elf64_Addr;
using NativeElfRel = Elf64_Rel;
using NativeElfRela = Elf64_Rela;
using NativeElfRelocation = Elf64_Rela;
using NativeElfVerneed = Elf64_Verneed;
using NativeElfVernaux = Elf64_Vernaux;
using NativeElfVerdef = Elf64_Verdef;
using NativeElfVerdaux = Elf64_Verdaux;
#else
using NativeElfEhdr = Elf32_Ehdr;
using NativeElfPhdr = Elf32_Phdr;
using NativeElfDyn = Elf32_Dyn;
using NativeElfSym = Elf32_Sym;
using NativeElfAddr = Elf32_Addr;
using NativeElfRel = Elf32_Rel;
using NativeElfRela = Elf32_Rela;
using NativeElfRelocation = Elf32_Rel;
using NativeElfVerneed = Elf32_Verneed;
using NativeElfVernaux = Elf32_Vernaux;
using NativeElfVerdef = Elf32_Verdef;
using NativeElfVerdaux = Elf32_Verdaux;
#endif
using NativeElfHalf = uint16_t;
using NativeElfWord = uint32_t;

inline constexpr bool kNativeUsesRela = YUKIPLT_USES_RELA != 0;
inline constexpr size_t kNativeSlotSize = YUKIPLT_ELF_BITS / 8;
inline constexpr uint16_t kNativeMachine = YUKIPLT_ELF_MACHINE;
inline constexpr uint32_t kNativeJumpSlot = YUKIPLT_RELOC_JUMP_SLOT;
inline constexpr uint32_t kNativeGlobDat = YUKIPLT_RELOC_GLOB_DAT;
inline constexpr uint32_t kNativeRelative = YUKIPLT_RELOC_RELATIVE;
inline constexpr uint32_t kNativeIRelative = YUKIPLT_RELOC_IRELATIVE;
inline constexpr uint32_t kNativeAbsolute = YUKIPLT_RELOC_ABSOLUTE;

// r_info layout is a property of the ELF class, not of REL vs RELA: ELF64
// packs (symbol << 32 | type), ELF32 packs (symbol << 8 | type) with a 24-bit
// symbol and an 8-bit type.  Every supported ABI pairs ELF64 with RELA and
// ELF32 with REL; the assertion below keeps that pairing from silently
// changing what the helpers decode.
inline constexpr bool kNativeElf64 = YUKIPLT_ELF_BITS == 64;
static_assert(kNativeElf64 == kNativeUsesRela,
              "r_info helpers assume ELF64 uses RELA and ELF32 uses REL");

constexpr uint32_t native_reloc_symbol(uint64_t info) noexcept {
  return static_cast<uint32_t>(info >> (kNativeElf64 ? 32 : 8));
}
constexpr uint32_t native_reloc_type(uint64_t info) noexcept {
  return static_cast<uint32_t>(info & (kNativeElf64 ? UINT64_C(0xffffffff) : UINT64_C(0xff)));
}
// Encode r_info, or leave output unchanged and return false if an ELF32 field
// cannot be represented (symbol above 24 bits or type above 8 bits).
constexpr bool try_native_reloc_info(uint32_t symbol, uint32_t type,
                                     NativeElfAddr& output) noexcept {
  if (!kNativeElf64 && (symbol > UINT32_C(0xffffff) || type > UINT32_C(0xff))) return false;
  output = static_cast<NativeElfAddr>((uint64_t{symbol} << (kNativeElf64 ? 32 : 8)) | type);
  return true;
}
// Encode r_info for fields known to be representable.  An unrepresentable
// ELF32 field yields 0 (R_*_NONE with symbol 0) instead of a silently
// truncated value, so a mistaken caller builds an inert record, never a
// different symbol.  Use try_native_reloc_info() for untrusted input.
constexpr NativeElfAddr native_reloc_info(uint32_t symbol, uint32_t type) noexcept {
  NativeElfAddr output = 0;
  return try_native_reloc_info(symbol, type, output) ? output : NativeElfAddr{0};
}
constexpr int64_t native_reloc_addend(const NativeElfRelocation& reloc) noexcept {
#if YUKIPLT_USES_RELA
  return reloc.r_addend;
#else
  // Android REL JUMP_SLOT/GLOB_DAT do not consume an implicit GOT addend.
  (void)reloc;
  return 0;
#endif
}

static_assert(sizeof(uintptr_t) == kNativeSlotSize, "Native ABI and pointer width disagree");
static_assert(sizeof(NativeElfAddr) == kNativeSlotSize);

/// \brief YukiPLT error types.

// Fixed C/C++ error values; new codes append without reuse. Use diagnosed
// causes and errno to distinguish failures. Retry and recovery also depend on
// handle validity; query_recovery describes Indeterminate residue.
enum class ErrorCode : uint32_t {
  kSuccess = 0,

  // --- Argument errors ---
  kInvalidArgument,
  kNullPointer,

  // --- Image / ELF errors ---
  kInvalidImage,
  kCorruptedElf,
  kUnsupportedFormat,
  kUnsupportedRelocation,

  // --- Symbol errors ---
  kSymbolNotFound,
  kNoHashTable,
  kAmbiguousSymbol,

  // --- Hook errors ---
  kSlotOwnershipConflict,
  kAlreadyHooked,
  kNotInstalled,
  kSlotModifiedExternally,
  kIndeterminate,  ///< Recovery was partial; per-slot/per-page state is reported.

  // --- System errors ---
  kPermissionDenied,
  kImageInvalidated,
  kOutOfMemory,  ///< An allocation failed; the operation did nothing.

  // --- Internal validation ---
  kIntegerOverflow,
  kOutOfBounds,

  // --- Image lookup ---
  kImageNotFound,   ///< No loaded image matches a lookup or enumeration.
  kAmbiguousImage,  ///< More than one loaded image matches the requested name.

  // --- Memory map ---
  kForkInProgress = 23,
  kForkChildUnavailable = 24,
  kVmaUnavailable = 22,  ///< /proc/self/maps could not be read (errno set) or parsed.
};

/// Describes an error with a code and optional system errno.
struct [[nodiscard]] Error {
  ErrorCode code = ErrorCode::kSuccess;

  /// System errno for permission/maps failures, including kIndeterminate:
  /// first restoration errno takes precedence over a page-raise errno.
  /// Zero when no system-call error occurred (e.g. CAS/protection conflict).
  int sys_errno = 0;

  /// Returns true when no error.
  [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::kSuccess; }

  /// Returns true when an error is present.
  [[nodiscard]] explicit operator bool() const noexcept { return !ok(); }

  static Error Ok() noexcept { return {}; }
  static Error Make(ErrorCode c) noexcept { return {c, 0}; }
  static Error Sys(ErrorCode c, int e) noexcept { return {c, e}; }
};

/// Stable, allocation-free name; unknown values return "Unknown".
[[nodiscard]] inline const char* error_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kSuccess:
      return "Success";
    case ErrorCode::kInvalidArgument:
      return "InvalidArgument";
    case ErrorCode::kNullPointer:
      return "NullPointer";
    case ErrorCode::kInvalidImage:
      return "InvalidImage";
    case ErrorCode::kCorruptedElf:
      return "CorruptedElf";
    case ErrorCode::kUnsupportedFormat:
      return "UnsupportedFormat";
    case ErrorCode::kUnsupportedRelocation:
      return "UnsupportedRelocation";
    case ErrorCode::kSymbolNotFound:
      return "SymbolNotFound";
    case ErrorCode::kNoHashTable:
      return "NoHashTable";
    case ErrorCode::kAmbiguousSymbol:
      return "AmbiguousSymbol";
    case ErrorCode::kSlotOwnershipConflict:
      return "SlotOwnershipConflict";
    case ErrorCode::kAlreadyHooked:
      return "AlreadyHooked";
    case ErrorCode::kNotInstalled:
      return "NotInstalled";
    case ErrorCode::kSlotModifiedExternally:
      return "SlotModifiedExternally";
    case ErrorCode::kIndeterminate:
      return "Indeterminate";
    case ErrorCode::kPermissionDenied:
      return "PermissionDenied";
    case ErrorCode::kImageInvalidated:
      return "ImageInvalidated";
    case ErrorCode::kOutOfMemory:
      return "OutOfMemory";
    case ErrorCode::kIntegerOverflow:
      return "IntegerOverflow";
    case ErrorCode::kOutOfBounds:
      return "OutOfBounds";
    case ErrorCode::kImageNotFound:
      return "ImageNotFound";
    case ErrorCode::kAmbiguousImage:
      return "AmbiguousImage";
    case ErrorCode::kForkInProgress:
      return "ForkInProgress";
    case ErrorCode::kForkChildUnavailable:
      return "ForkChildUnavailable";
    case ErrorCode::kVmaUnavailable:
      return "VmaUnavailable";
  }
  return "Unknown";
}

}  // namespace yukiplt

/// \brief Minimal Expected<T, E> for error propagation (C++17).

namespace yukiplt::detail {

/// A discriminated union holding either a value of type T or an error of type E.
///
/// For the special case Expected<void, E>, only the error branch stores data.
/// Typed placement addresses intentionally preserve class-specific placement-new
/// overload resolution; converting them to void* changes valid T/E behavior.
template <typename T, typename E>
class [[nodiscard]] Expected {
  static_assert(!std::is_same_v<T, E>, "T and E must be different types");
  static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>,
                "Expected requires non-throwing move construction");

 public:
  Expected(const T& val) : has_value_(true) {  // NOLINT: implicit
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
    new (&storage_.value) T(val);
  }
  Expected(T&& val) : has_value_(true) {  // NOLINT: implicit
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
    new (&storage_.value) T(std::move(val));
  }
  Expected(const E& err) : has_value_(false) {  // NOLINT: implicit
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
    new (&storage_.error) E(err);
  }
  Expected(E&& err) : has_value_(false) {  // NOLINT: implicit
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
    new (&storage_.error) E(std::move(err));
  }

  Expected(const Expected& o) : has_value_(o.has_value_) {
    if (has_value_)
      // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
      new (&storage_.value) T(o.storage_.value);
    else
      // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
      new (&storage_.error) E(o.storage_.error);
  }
  Expected(Expected&& o) noexcept : has_value_(o.has_value_) {
    if (has_value_)
      // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
      new (&storage_.value) T(std::move(o.storage_.value));
    else
      // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
      new (&storage_.error) E(std::move(o.storage_.error));
  }

  Expected& operator=(const Expected& o) {
    if (this != &o) {
      // Copy first: an owning value (e.g. ImportSnapshot) can throw while
      // copying. Destroying the old union member first would leave this
      // object claiming a value whose construction never completed.
      static_assert(
          std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>,
          "Expected copy assignment needs a non-throwing commit");
      Expected copy(o);
      *this = std::move(copy);
    }
    return *this;
  }
  Expected& operator=(Expected&& o) noexcept {
    if (this != &o) {
      destroy();
      has_value_ = o.has_value_;
      if (has_value_)
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        new (&storage_.value) T(std::move(o.storage_.value));
      else
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        new (&storage_.error) E(std::move(o.storage_.error));
    }
    return *this;
  }

  ~Expected() { destroy(); }

  [[nodiscard]] bool has_value() const noexcept { return has_value_; }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] T& value() & {
    assert(has_value_);
    return storage_.value;
  }
  [[nodiscard]] const T& value() const& {
    assert(has_value_);
    return storage_.value;
  }
  [[nodiscard]] T&& value() && {
    assert(has_value_);
    return std::move(storage_.value);
  }

  [[nodiscard]] E& error() & {
    assert(!has_value_);
    return storage_.error;
  }
  [[nodiscard]] const E& error() const& {
    assert(!has_value_);
    return storage_.error;
  }

  /// Transform the value with f, propagating errors.
  template <typename F>
  auto map(F&& f) const& -> Expected<decltype(f(std::declval<const T&>())), E> {
    using U = decltype(f(std::declval<const T&>()));
    if (has_value_) {
      if constexpr (std::is_void_v<U>) {
        f(storage_.value);
        return Expected<void, E>();
      } else {
        return Expected<U, E>(f(storage_.value));
      }
    }
    return Expected<U, E>(storage_.error);
  }

  /// Chain with a function returning Expected<U, E>, propagating errors.
  template <typename F>
  auto and_then(F&& f) const& -> decltype(f(std::declval<const T&>())) {
    if (has_value_) return f(storage_.value);
    using Ret = decltype(f(std::declval<const T&>()));
    return Ret(storage_.error);
  }

 private:
  void destroy() {
    if (has_value_)
      storage_.value.~T();
    else
      storage_.error.~E();
  }

  union Storage {
    T value;
    E error;
    Storage() {}   // NOLINT: leave uninitialized
    ~Storage() {}  // NOLINT: destroyed by Expected
  } storage_;
  bool has_value_;
};

/// Specialization for Expected<void, E>.
template <typename E>
class [[nodiscard]] Expected<void, E> {
  static_assert(std::is_nothrow_move_constructible_v<E>,
                "Expected requires non-throwing move construction");

 public:
  Expected() : has_value_(true) {}                                  // NOLINT
  Expected(const E& err) : has_value_(false), error_(err) {}        // NOLINT: implicit
  Expected(E&& err) : has_value_(false), error_(std::move(err)) {}  // NOLINT: implicit

  Expected(const Expected&) = default;
  Expected(Expected&&) noexcept = default;
  Expected& operator=(const Expected& other) {
    if (this != &other) {
      Expected copy(other);
      *this = std::move(copy);
    }
    return *this;
  }
  Expected& operator=(Expected&& other) noexcept {
    if (this != &other) {
      // Commit with the constrained constructor, not E::operator=.
      error_.~E();
      // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
      new (&error_) E(std::move(other.error_));
      has_value_ = other.has_value_;
    }
    return *this;
  }

  [[nodiscard]] bool has_value() const noexcept { return has_value_; }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value_; }

  void value() const { assert(has_value_); }

  [[nodiscard]] E& error() & {
    assert(!has_value_);
    return error_;
  }
  [[nodiscard]] const E& error() const& {
    assert(!has_value_);
    return error_;
  }

 private:
  bool has_value_;
  E error_{};
};

}  // namespace yukiplt::detail

namespace yukiplt {
// Read non-null, aligned, stable and live out_original storage with an acquire
// fence before the acquire load. An acquire load alone does not order an earlier
// ARM GOT read. No validation, allocation or locking. The stored value may be
// null; a non-null value proves neither installation, provider identity nor quiescence.
YUKIPLT_API void* load_original(void* const* storage) noexcept;

// Android API23+ PLT/GOT hooks. Bind imports first; keep images, original
// outputs and replacement code/context alive through recovery and quiescence.
// Read originals with load_original or OriginalSlot::load; never clear or reuse them.
// Select one slot explicitly; duplicate records are corrupt. Hooks affect future
// slot reads only. Indeterminate retains a recovery handle; no cross-slot atomicity
// or ABA protection is promised. Serialize external page changes and remapping.
// System images use linker -> op locking; custom loads require unique nonzero
// tokens and forget_image. Named APIs also bind the path; callers handle reload ABA.
// Serialize each handle/session/RuleSet and its result reads. Synchronous reentry
// from functions called under internal locks is forbidden; lock-free host callbacks
// may reenter. No signals or cancellation/longjmp across API frames. Calls after
// multithreaded fork before exec require the controlled fork protocol and host
// runtime coordination. Never fork during an operation or callback.
// Destructors do not unhook. Each copy owns separate locks/handles; coordinate
// shared pages across copies. Self-image installs are rejected. Salted ids detect
// misuse, not attacks; entropy failure rejects installation.
// See docs/integration.md and docs/lifecycle.md.

struct NamedImage;
struct RecoveryDiagnostic;
namespace detail {
struct MapsCacheState;
struct MapsCacheAccess;
}  // namespace detail

// Owning permission snapshot. Serialize access, including moves and destruction.
// The host keeps relevant mappings stable from capture through their last use.
class MapsCache {
 public:
  MapsCache() = default;
  MapsCache(const MapsCache&) = delete;
  MapsCache& operator=(const MapsCache&) = delete;
  MapsCache(MapsCache&&) noexcept = default;
  MapsCache& operator=(MapsCache&&) noexcept = default;
  YUKIPLT_API Error capture();
  YUKIPLT_API Error import_text(const char* text, size_t length);
  YUKIPLT_API Error invalidate();
  YUKIPLT_API bool has_bindings() const noexcept;

 private:
  friend struct detail::MapsCacheAccess;
  std::shared_ptr<detail::MapsCacheState> state_;
};
using ForkConfig = yukiplt_c_fork_config;
using ForkStatus = yukiplt_c_fork_status;
YUKIPLT_API Error enable_atfork(const ForkConfig&) noexcept;
YUKIPLT_API Error disable_atfork() noexcept;
YUKIPLT_API Error fork_status(ForkStatus&) noexcept;
YUKIPLT_API Error prepare_fork() noexcept;
YUKIPLT_API Error after_fork_parent() noexcept;
YUKIPLT_API Error after_fork_child() noexcept;

/// Result is the public-facing result type for YukiPLT operations.
template <typename T>
using Result = detail::Expected<T, Error>;

// Borrowed native ELF view. Use the loader's exact load bias and a nonempty,
// aligned, readable PHDR table; keep custom mappings and metadata stable until
// handles are released. A readable ELF header must agree on e_phnum; an in-image
// table must match e_phoff in a readable zero-offset LOAD. External copies are
// allowed; without a readable ELF header the supplied view is trusted.
// Token 0 selects linker-validated system images; a unique nonzero token selects
// caller-managed custom loads. Mislabeling bypasses or fails system validation.
// The view carries neither a path nor a load generation.
struct ImageView {
  uintptr_t load_bias;                       ///< Runtime load bias (dlpi_addr).
  const yukiplt::NativeElfPhdr* phdr_table;  ///< Program header table address.
  size_t phdr_count;                         ///< Number of program headers.
  uint64_t instance_token = 0;               ///< 0 = system-linker image; else custom load id.
};

/// One named YUKIPLT_RELOC_JUMP_SLOT record, copied from DT_JMPREL.
/// Addresses and indices are diagnostic metadata, not an installation token.
struct ImportInfo {
  std::string symbol;
  uint32_t symbol_index = 0;
  size_t relocation_index = 0;
  uintptr_t slot_address = 0;
};

/// Owning, read-only metadata snapshot. Strings survive image unloading;
/// addresses do not. The snapshot neither pins the image nor owns any slots.
struct ImportSnapshot {
  std::vector<ImportInfo> imports;  ///< In DT_JMPREL order, including duplicate names.
  size_t skipped_relocations = 0;   ///< Non-JUMP_SLOT or symbol-index-zero records.
};

// Copy owning JUMP_SLOT metadata without GOT access, permission changes or
// ownership. No GLOB_DAT/APS2 scan; use inspect_versioned_imports for versions.
// The three PLT tags must be unique and complete. Empty tables succeed; duplicate
// non-NONE destinations or malformed metadata reject the entire snapshot.
// Unsupported records are counted. This path is bounded by JMPREL size, without
// the function-scan budget. System copying stays under the linker lock; callers
// keep custom metadata stable. Allocation/size failures return errors, never exceptions.
YUKIPLT_API Result<ImportSnapshot> inspect_imports(const ImageView& image);

enum class VersionSource : uint8_t { kNone, kNeeded, kDefined };

/// Version metadata declared by this image, not proof of the bound provider.
struct SymbolVersion {
  std::string name;
  std::string dependency;  ///< VERNEED's file; empty for definitions/no version.
  uint16_t index = 1;      ///< Low 15 bits of VERSYM; 0/1 have no version name.
  bool table_present = false;
  bool hidden = false;  ///< VERSYM's high bit, not ELF symbol visibility.
  VersionSource source = VersionSource::kNone;
};

struct VersionedImport {
  ImportInfo import;
  SymbolVersion version;
};

struct VersionedImportSnapshot {
  std::vector<VersionedImport> imports;
  size_t skipped_relocations = 0;
};

/// Like inspect_imports(), also validates/copies VERSYM, VERNEED and VERDEF.
/// Missing version metadata is represented explicitly, not guessed from names.
YUKIPLT_API Result<VersionedImportSnapshot> inspect_versioned_imports(const ImageView& image);

enum class VersionMatch : uint8_t { kAny, kUnversioned, kExact };
enum class HiddenMatch : uint8_t { kAny, kNonHidden, kHidden };

/// All supplied filters must match ONE slot. No fallback to another version.
/// Strings are borrowed only during the API call. A null symbol is
/// kNullPointer; an empty symbol, version or dependency filter is
/// kInvalidArgument; nullptr means no dependency filter. kUnversioned matches
/// absent VERSYM or indices 0/1. kAny never resolves ambiguity automatically.
struct ImportSelector {
  const char* symbol = nullptr;
  VersionMatch version_match = VersionMatch::kAny;
  const char* version = nullptr;  ///< Required only for kExact; otherwise null.
  const char* dependency = nullptr;
  HiddenMatch hidden = HiddenMatch::kAny;
  size_t relocation_index = ~size_t{0};  ///< ~0: any DT_JMPREL index.
};

/// Read-only unique selection. Result addresses remain diagnostic snapshots.
YUKIPLT_API Result<VersionedImport> find_import(const ImageView& image,
                                                const ImportSelector& selector);

struct SelectedHookRequest {
  ImportSelector target;
  void* replacement = nullptr;
  void** out_original = nullptr;
};

/// What happened to one slot in a hook/unhook/query.
enum class SlotStatus : uint8_t {
  kUntouched,       ///< No write was attempted on this slot.
  kInstalled,       ///< Replacement installed (value_before holds the prior value).
  kRestored,        ///< Original value put back.
  kConflict,        ///< Slot did not hold the expected value; left as found.
  kRolledBack,      ///< Installed, then reverted after another slot conflicted.
  kRollbackFailed,  ///< Installed, but the revert failed; slot left as observed.
};

/// Per-slot report, optionally filled by hook/hook_batch/unhook/query.
struct SlotReport {
  uintptr_t address = 0;         ///< GOT slot runtime address.
  uintptr_t value_before = 0;    ///< Value in the slot before this operation.
  uintptr_t value_observed = 0;  ///< Value seen if the CAS conflicted.
  SlotStatus status = SlotStatus::kUntouched;
};

/// One slot to install in a batch.
/// The legacy name request.  A null symbol is kNullPointer; an empty symbol
/// never matches and is kSymbolNotFound (the selector-based entry points reject
/// an empty name with kInvalidArgument instead).
struct HookRequest {
  const char* symbol;   ///< Symbol name to hook (must resolve to one slot).
  void* replacement;    ///< Replacement function pointer.
  void** out_original;  ///< Receives the prior slot value; written just before
                        ///< commit and never cleared once set (may be null).
};

/// Opaque handle representing a hook group (one slot or a batch).
///
/// Move-only.  Must be explicitly unhook()'d or abandon()'d before destruction.
/// Destroying or move-overwriting a handle that still owns an installed hook
/// logs a warning but does NOT unhook.
class HookHandle {
 public:
  HookHandle() noexcept : id_(kInvalid) {}
  YUKIPLT_API ~HookHandle();

  HookHandle(const HookHandle&) = delete;
  HookHandle& operator=(const HookHandle&) = delete;

  HookHandle(HookHandle&& o) noexcept : id_(o.id_) { o.id_ = kInvalid; }
  YUKIPLT_API HookHandle& operator=(HookHandle&& o) noexcept;

  /// Returns true if this handle refers to a live hook entry.
  [[nodiscard]] bool valid() const noexcept { return id_ != kInvalid; }

 private:
  friend class detail_api;

  explicit HookHandle(uint64_t id) noexcept : id_(id) {}
  void warn_if_installed() const noexcept;

  static constexpr uint64_t kInvalid = ~uint64_t(0);
  uint64_t id_;
};

/// Result of a hook/hook_batch: an error and, on success, an owning handle.
///
/// `error.ok()` is the single source of truth for success.  A valid handle is
/// returned on success (the hook is active) and, exceptionally, on
/// kIndeterminate — where the hook is NOT fully active but the handle must be
/// kept so the caller can recover (unhook) or relinquish (abandon) the
/// partially-applied state.  On every other error the handle is invalid.
struct [[nodiscard]] HookResult {
  Error error;
  HookHandle handle;

  /// True only on a clean install (error.ok()).  kIndeterminate is not ok(),
  /// even though it carries a valid handle.
  [[nodiscard]] bool ok() const noexcept { return error.ok(); }
};

// Linux/Android loader lookup: null/empty selects the main executable via
// AT_PHDR; a slash selects exact dlpi_name; a bare name also matches basename.
// Return a token-0 view or ImageNotFound/AmbiguousImage; never choose arbitrarily.
YUKIPLT_API Result<ImageView> image_from_soname(const char* soname);

// Install one uniquely selected JUMP_SLOT with a non-null replacement.
// out_original is optional, pointer-aligned, and published before replacement
// becomes reachable. Success owns the slot; Indeterminate retains recovery
// state. Other failures leave no installed hook or ownership. Check the result
// and optional SlotReport; published originals are never cleared.
YUKIPLT_API HookResult hook(const ImageView& image, const char* symbol_name, void* replacement,
                            void** out_original, SlotReport* report = nullptr);

// Install 1..64 requests in one image. Targets and non-null aligned original
// outputs must be distinct; all targets must be unowned. Preflight rejects the
// whole batch. CAS conflicts roll back; failed rollback/protection restoration
// returns Indeterminate with a recovery handle. reports has count entries.
YUKIPLT_API HookResult hook_batch(const ImageView& image, const HookRequest* reqs, size_t count,
                                  SlotReport* reports = nullptr);

/// Exact selection with the same transaction/ownership/recovery and
/// original-pointer output contract as hook()/hook_batch(). Re-inspects
/// metadata under the lifetime lock; never accepts a raw snapshot address. A
/// batch is limited to 64 distinct slots in one image.
YUKIPLT_API HookResult hook_import(const ImageView& image, const ImportSelector& selector,
                                   void* replacement, void** out_original,
                                   SlotReport* report = nullptr);
YUKIPLT_API HookResult hook_imports(const ImageView& image, const SelectedHookRequest* reqs,
                                    size_t count, SlotReport* reports = nullptr);
// Explicit function sources/types extend the JUMP_SLOT APIs with GLOB_DAT.
// Every match must be an undefined STT_FUNC with zero effective addend; only
// GLOB_DAT also requires a nonzero current value. Any ineligible match rejects
// the whole operation. RELA uses r_addend; Android REL binding uses no addend.
// Scanned function records validate versions; legacy name-only APIs do not.
// Import STT_FUNC cannot exclude an IFUNC provider. Cached pointers remain valid
// caller responsibilities; out_original is the pre-install slot value.

/// Which relocation tables to scan (a bitmask over these bits).
enum RelocationSourceBit : uint32_t {
  kSourceJmprel = 1u << 0,       ///< DT_JMPREL (PLT relocations).
  kSourceRela = 1u << 1,         ///< DT_RELA (.rela.dyn).
  kSourceAndroidRela = 1u << 2,  ///< DT_ANDROID_RELA (APS2 packed).
  kSourceRel = 1u << 3,          ///< DT_REL (.rel.dyn), native REL ABIs only.
  kSourceAndroidRel = 1u << 4,   ///< DT_ANDROID_REL (APS2 packed), native REL ABIs only.
};

inline constexpr uint32_t kSourceNativeDynamic = kNativeUsesRela ? kSourceRela : kSourceRel;
inline constexpr uint32_t kSourceNativeAndroidPacked =
    kNativeUsesRela ? kSourceAndroidRela : kSourceAndroidRel;

/// Which relocation types to accept (a bitmask over these bits).
enum RelocationTypeBit : uint32_t {
  kTypeJumpSlot = 1u << 0,  ///< YUKIPLT_RELOC_JUMP_SLOT.
  kTypeGlobDat = 1u << 1,   ///< YUKIPLT_RELOC_GLOB_DAT (function, addend 0, UND FUNC).
};

/// The table a decoded function import came from.
enum class RelocationSource : uint8_t {
  kJmprel = 0,
  kRela = 1,
  kAndroidRela = 2,
  kRel = 3,
  kAndroidRel = 4
};

/// One function import, with its relocation source and type.  `base` carries the
/// name, symbol index, version metadata and slot address; `base.import`'s
/// relocation_index is the index within `source`'s own table.
struct FunctionImport {
  VersionedImport base;
  RelocationSource source = RelocationSource::kJmprel;
  uint32_t relocation_type = 0;  ///< Native relocation value as found.
  uint8_t symbol_type = 0;       ///< ELF symbol type (STT_*).
};

struct FunctionImportSnapshot {
  std::vector<FunctionImport> imports;
  /// Records in a scanned source that are neither JUMP_SLOT nor GLOB_DAT, or
  /// have symbol index 0.  A JUMP_SLOT or GLOB_DAT is listed in `imports`
  /// whichever scanned table holds it (labelled with its real source and type),
  /// so a GLOB_DAT inside DT_JMPREL is listed, not skipped.  This differs from
  /// inspect_imports(), which counts such a record as skipped.
  size_t skipped_relocations = 0;
};

/// Selector for a function import.  `sources` and `types` are bitmasks that must
/// both be non-zero — naming them is the explicit opt-in.  `index_source` scopes
/// `base.relocation_index` (when not ~0) to one table.
struct FunctionImportSelector {
  ImportSelector base;
  uint32_t sources = 0;
  uint32_t types = 0;
  RelocationSource index_source = RelocationSource::kJmprel;
};

struct FunctionHookRequest {
  FunctionImportSelector target;
  void* replacement = nullptr;
  void** out_original = nullptr;
};

// Copy owning function-import metadata from a nonzero valid source mask.
// No GOT access, permissions or ownership; optional tables are validated only
// when selected. All scanned non-NONE records participate in duplicate-slot
// checks, including skipped types. Share a 2^22-record and 64 MiB logical
// materialization budget, including indexes, copied text and temporaries;
// excess returns OutOfBounds before allocation. This is not an RSS limit.
YUKIPLT_API Result<FunctionImportSnapshot> inspect_function_imports(const ImageView& image,
                                                                    uint32_t sources);

/// Read-only unique selection over the scanned sources/types.
YUKIPLT_API Result<FunctionImport> find_function_import(const ImageView& image,
                                                        const FunctionImportSelector& selector);

/// Install one / several function hooks on one image, same transaction,
/// ownership, recovery and original-pointer contract as hook_import()/
/// hook_imports().  A batch mixing JUMP_SLOT and GLOB_DAT slots on one image
/// runs as a single transaction and is limited to 64 distinct slots.
YUKIPLT_API HookResult hook_function_import(const ImageView& image,
                                            const FunctionImportSelector& selector,
                                            void* replacement, void** out_original,
                                            SlotReport* report = nullptr);
YUKIPLT_API HookResult hook_function_imports(const ImageView& image,
                                             const FunctionHookRequest* reqs, size_t count,
                                             SlotReport* reports = nullptr);

// Restore owned replacement values by CAS; already-original values are done.
// Never-written slots are Untouched with live values. Preserve third-party values
// as conflicts. Roll back this attempt if any restore CAS loses a race.
// Success and ImageInvalidated consume the handle; other failures retain it.
// Maps failure or incomplete coverage does not prove invalidation. Indeterminate
// retains failed rollback/page residue. Retry uses live maps: accept original
// protection, restore raised protection, and retain third-party protection conflicts.
// System identity includes the installation path; mismatch consumes the old handle
// without accessing the new image. Custom invalidation requires forget_image.
// reports is an optional array of cap SlotReports. Success does not prove quiescence.
YUKIPLT_API Error unhook(HookHandle& handle, SlotReport* reports = nullptr, size_t cap = 0);

struct OperationDiagnostic;
struct FunctionHookRequestEx;

/// Diagnosed variants (see diagnostics.h).  Each behaves exactly like the
/// undiagnosed entry point — same validation, transaction, return codes and
/// handle semantics — and additionally writes an OperationDiagnostic for this
/// call when `diagnostic` is non-null, on success and on every failure.  The
/// diagnostic is caller-owned and must not alias any other argument.
YUKIPLT_API HookResult hook_batch_diagnosed(const ImageView& image, const HookRequest* reqs,
                                            size_t count, SlotReport* reports,
                                            OperationDiagnostic* diagnostic);
YUKIPLT_API HookResult hook_imports_diagnosed(const ImageView& image,
                                              const SelectedHookRequest* reqs, size_t count,
                                              SlotReport* reports, OperationDiagnostic* diagnostic);
YUKIPLT_API HookResult hook_function_imports_diagnosed(const ImageView& image,
                                                       const FunctionHookRequestEx* reqs,
                                                       size_t count, SlotReport* reports,
                                                       OperationDiagnostic* diagnostic);
/// Also sets diagnostic->handle_consumed when this call consumed `handle`
/// (success, or kImageInvalidated); a failure with handle_consumed false keeps
/// the handle for a retry, abandon() or query_recovery().
YUKIPLT_API Error unhook_diagnosed(HookHandle& handle, SlotReport* reports, size_t cap,
                                   OperationDiagnostic* diagnostic);

// Release bookkeeping and consume the handle without changing memory.
// The caller assumes responsibility for remaining replacements and page residue.
YUKIPLT_API Error abandon(HookHandle& handle);

// Copy at most cap reports and return the total slot count. address/value_before
// are recorded; readable live slots report Installed and the observed value.
// Unreadable or invalidated slots report Untouched/zero, never a fabricated value.
// System reads require the recorded identity/path under the linker lock. Null out
// or zero cap requests only the count. No writes or handle consumption.
YUKIPLT_API Error query(const HookHandle& handle, SlotReport* out, size_t cap, size_t* count);

// Forget a custom image by bias, PHDR identity and nonzero load token without
// modifying memory. Its handles are consumed as ImageInvalidated on unhook.
// System images are rejected. Call before unmapping the custom instance.
YUKIPLT_API Error forget_image(const ImageView& custom_image);

/// Path-bound system-image entry points. NamedImage (session.h) owns the exact
/// dlpi_name obtained during discovery. An empty path is an exact name; it does
/// not disable matching. Custom-image tokens and embedded NUL paths are rejected.
/// The view and path must both match under the linker lock before metadata is
/// read or installation starts. Mismatch returns kImageInvalidated with no
/// publication, ownership or memory modification. These APIs do not pin an
/// image or distinguish a same-path, same-address reload. The caller keeps the
/// image alive. Separate names preserve existing braced ImageView calls.
YUKIPLT_API Result<ImportSnapshot> inspect_imports_named(const NamedImage& image);
YUKIPLT_API Result<VersionedImportSnapshot> inspect_versioned_imports_named(
    const NamedImage& image);
YUKIPLT_API Result<VersionedImport> find_import_named(const NamedImage& image,
                                                      const ImportSelector& selector);
YUKIPLT_API Result<FunctionImportSnapshot> inspect_function_imports_named(const NamedImage& image,
                                                                          uint32_t sources);
YUKIPLT_API Result<FunctionImport> find_function_import_named(
    const NamedImage& image, const FunctionImportSelector& selector);
YUKIPLT_API HookResult hook_named(const NamedImage& image, const char* symbol_name,
                                  void* replacement, void** out_original,
                                  SlotReport* report = nullptr);
YUKIPLT_API HookResult hook_batch_named(const NamedImage& image, const HookRequest* reqs,
                                        size_t count, SlotReport* reports = nullptr);
YUKIPLT_API HookResult hook_import_named(const NamedImage& image, const ImportSelector& selector,
                                         void* replacement, void** out_original,
                                         SlotReport* report = nullptr);
YUKIPLT_API HookResult hook_imports_named(const NamedImage& image, const SelectedHookRequest* reqs,
                                          size_t count, SlotReport* reports = nullptr);
YUKIPLT_API HookResult hook_function_import_named(const NamedImage& image,
                                                  const FunctionImportSelector& selector,
                                                  void* replacement, void** out_original,
                                                  SlotReport* report = nullptr);
YUKIPLT_API HookResult hook_function_imports_named(const NamedImage& image,
                                                   const FunctionHookRequest* reqs, size_t count,
                                                   SlotReport* reports = nullptr);
YUKIPLT_API HookResult hook_function_imports_ex_named(const NamedImage& image,
                                                      const FunctionHookRequestEx* reqs,
                                                      size_t count, SlotReport* reports = nullptr,
                                                      RecoveryDiagnostic* diagnostic = nullptr);
YUKIPLT_API HookResult hook_batch_diagnosed_named(const NamedImage& image, const HookRequest* reqs,
                                                  size_t count, SlotReport* reports,
                                                  OperationDiagnostic* diagnostic);
/// Named JUMP_SLOT batch that rejects a zero original under the operation lock,
/// before publication or writes. Other entry points keep their zero-slot behavior.
YUKIPLT_API HookResult hook_batch_nonnull_diagnosed_named(const NamedImage& image,
                                                          const HookRequest* reqs, size_t count,
                                                          SlotReport* reports,
                                                          OperationDiagnostic* diagnostic);
YUKIPLT_API HookResult hook_imports_diagnosed_named(const NamedImage& image,
                                                    const SelectedHookRequest* reqs, size_t count,
                                                    SlotReport* reports,
                                                    OperationDiagnostic* diagnostic);
YUKIPLT_API HookResult hook_function_imports_diagnosed_named(const NamedImage& image,
                                                             const FunctionHookRequestEx* reqs,
                                                             size_t count, SlotReport* reports,
                                                             OperationDiagnostic* diagnostic);

class HookHandle;

enum class OperationStage : uint32_t {
  kNone = 0,
  kPrepare = 1,
  kExpectedCurrent = 2,
  kRaise = 3,
  kCommit = 4,
  kRollback = 5,
  kRestorePages = 6,
  kComplete = 7,
};

/// Recorded recovery state, not a live inspection of memory or permissions.
struct RecoveryDiagnostic {
  OperationStage stage = OperationStage::kNone;
  Error error;
  int raise_errno = 0;
  int restore_errno = 0;
  bool rollback_failed = false;
  bool image_invalidated = false;
  size_t residual_page_count = 0;
};

struct ResidualPage {
  uintptr_t address = 0;
  uint32_t original_protection = 0;  ///< Linux PROT_* bits, as last recorded.
  uint32_t raised_protection = 0;
  int sys_errno = 0;  ///< Last failed restoration; 0 may mean a protection conflict.
};

/// Why an operation failed, beyond its ErrorCode.  Several codes cover more
/// than one cause (see error.h); the cause tells them apart.  kNone means the
/// operation succeeded or its code is already specific.  Values are stable and
/// new values may be appended; treat an unknown value like kNone.
enum class ErrorCause : uint32_t {
  kNone = 0,
  kInvalidRequest = 1,        ///< Malformed argument, selector, count or output buffer.
  kHandleInvalid = 2,         ///< Handle empty, consumed, stale or issued by another copy.
  kOwnImage = 3,              ///< Target is the image containing this copy of YukiPLT.
  kDuplicateTarget = 4,       ///< Two requests of one batch select the same slot.
  kOutputAlias = 5,           ///< An original output is a target slot or an owned slot.
  kReplacementIsCurrent = 6,  ///< The slot already holds the replacement value.
  kOriginalCapacity = 7,      ///< RuleSet original storage is exhausted.
  kMetadataBounds = 8,        ///< ELF metadata lies outside readable LOAD segments.
  kScanBudget = 9,            ///< A per-operation decode/byte/candidate limit was reached.
  kMapsUnavailable = 10,      ///< /proc/self/maps could not be read or parsed.
  kMapsCoverage = 11,         ///< A slot page is not covered by the maps read.
  kVmaPolicy = 12,            ///< The mapping is shared, executable or unreadable.
  kProtectFailed = 13,        ///< mprotect failed; the errno is in the Error.
  kExpectedCurrent = 14,      ///< The expected-current precondition did not hold.
  kCasConflict = 15,          ///< Another writer changed a slot during the CAS.
  kInternalException = 16,    ///< An unexpected internal exception was contained.
  kHostCallback = 17,         ///< A host callback threw or returned an invalid result.
  kRandomSource = 18,         ///< The handle salt could not be drawn; errno attached.
  kPlatform = 19,             ///< Unsupported platform or invalid runtime page size.
  /// A system image no longer carries the identity (bias, program headers and
  /// path) it was planned or bound with.
  kIdentityMismatch = 20,
  /// A slot this library installed no longer holds its replacement.
  kSlotDrift = 21,
  kCacheInvalid = 22,
  kCacheConflict = 23,
  kForkProtocol = 24,
};

/// Output identifier for this frozen OperationDiagnostic layout. It is not a
/// caller buffer size or an append-in-place extension mechanism. New layouts
/// require a new type and entry point; existing functions always write v1.
inline constexpr uint32_t kOperationDiagnosticVersion = 1;

/// Immediate result of one operation, written by the *_diagnosed entry points
/// whether or not they succeed.  It is a report of this call, not recorded
/// state: query_recovery() remains the source for a handle's recovery history.
struct OperationDiagnostic {
  uint32_t version = kOperationDiagnosticVersion;
  OperationStage stage = OperationStage::kNone;
  ErrorCause cause = ErrorCause::kNone;
  Error error;
  int raise_errno = 0;
  int restore_errno = 0;
  bool rollback_failed = false;
  bool image_invalidated = false;
  /// unhook only: the call consumed the handle (success or invalidated image).
  bool handle_consumed = false;
  size_t residual_page_count = 0;
};

/// Stable, allocation-free name; unknown values return "Unknown".
[[nodiscard]] inline const char* error_cause_name(ErrorCause cause) noexcept {
  switch (cause) {
    case ErrorCause::kCacheInvalid:
      return "CacheInvalid";
    case ErrorCause::kCacheConflict:
      return "CacheConflict";
    case ErrorCause::kForkProtocol:
      return "ForkProtocol";
    case ErrorCause::kNone:
      return "None";
    case ErrorCause::kInvalidRequest:
      return "InvalidRequest";
    case ErrorCause::kHandleInvalid:
      return "HandleInvalid";
    case ErrorCause::kOwnImage:
      return "OwnImage";
    case ErrorCause::kDuplicateTarget:
      return "DuplicateTarget";
    case ErrorCause::kOutputAlias:
      return "OutputAlias";
    case ErrorCause::kReplacementIsCurrent:
      return "ReplacementIsCurrent";
    case ErrorCause::kOriginalCapacity:
      return "OriginalCapacity";
    case ErrorCause::kMetadataBounds:
      return "MetadataBounds";
    case ErrorCause::kScanBudget:
      return "ScanBudget";
    case ErrorCause::kMapsUnavailable:
      return "MapsUnavailable";
    case ErrorCause::kMapsCoverage:
      return "MapsCoverage";
    case ErrorCause::kVmaPolicy:
      return "VmaPolicy";
    case ErrorCause::kProtectFailed:
      return "ProtectFailed";
    case ErrorCause::kExpectedCurrent:
      return "ExpectedCurrent";
    case ErrorCause::kCasConflict:
      return "CasConflict";
    case ErrorCause::kInternalException:
      return "InternalException";
    case ErrorCause::kHostCallback:
      return "HostCallback";
    case ErrorCause::kRandomSource:
      return "RandomSource";
    case ErrorCause::kPlatform:
      return "Platform";
    case ErrorCause::kIdentityMismatch:
      return "IdentityMismatch";
    case ErrorCause::kSlotDrift:
      return "SlotDrift";
  }
  return "Unknown";
}

/// Copies the last operation and residual pages under the Registry lock only.
/// Never reads an image, GOT or maps. diagnostic is required; pages may be null
/// for count-only (capacity must then be zero). At most capacity pages are
/// copied; diagnostic.residual_page_count always gives the total. Outputs are
/// unchanged on failure. A consumed handle has no queryable history.
[[nodiscard]] YUKIPLT_API Error query_recovery(const HookHandle& handle,
                                               RecoveryDiagnostic* diagnostic,
                                               ResidualPage* pages = nullptr, size_t capacity = 0);

enum class ImageMatch : uint8_t { kMainExecutable, kExactPath, kBasename, kLoadBias };
struct ImageSelector {
  ImageMatch match = ImageMatch::kMainExecutable;
  const char* name = nullptr;
  uintptr_t load_bias = 0;
};
struct NamedImage {
  ImageView image;
  std::string path;
};

/// Explicit matching of publicly enumerable system images, never implicit
/// match-all. Empty matches return kImageNotFound. Paths are owned; ImageViews
/// are not pinned. No promise of visibility into every linker namespace.
YUKIPLT_API Result<std::vector<NamedImage>> find_images(const ImageSelector& selector);

/// Find one publicly enumerable image whose actual PT_LOAD memory interval
/// contains address. Holes are excluded, all images are enumerated to reject
/// ambiguity. The result owns its path but does not hold the image alive.
/// On Android ARM64 only the query's TBI top byte is ignored. This does not
/// validate a memory tag, strip PAC signatures, or establish pointer validity.
YUKIPLT_API Result<NamedImage> find_image_containing(const void* address);

struct HookTarget {
  ImageView image;
  ImportSelector import;
  void* replacement = nullptr;
  void** out_original = nullptr;
};
struct PlannedTarget {
  ImageView image;
  VersionedImport selected;
  void* replacement = nullptr;
  void** out_original = nullptr;
};

class HookSession;
class HookPlan {
 public:
  const std::vector<PlannedTarget>& targets() const noexcept { return targets_; }

 private:
  friend Result<HookPlan> build_hook_plan(const HookTarget*, size_t, size_t*);
  friend Result<HookSession> prepare_session(const HookPlan&);
  std::vector<PlannedTarget> targets_;
  std::vector<std::string> paths_;  ///< Per target: system image path when planned.
};

/// Resolve every target without writing GOT, outputs, permissions or Registry.
/// For a system image the plan also records the image's path as loaded at
/// planning time; installation requires the image at the same bias and program
/// headers to still carry that path, otherwise the group fails with
/// kImageInvalidated (another image now occupies that address).  This cannot
/// detect a same-address, same-path reload: unhook or abandon before dlclose.
/// Failure reports the first target index (~0 for an error not tied to one).
/// Strings are copied. Same-slot targets and shared non-null outputs fail.
/// At most 64 targets per identical ImageView; no cross-image atomicity.
/// Caller keeps images, headers, replacements and output storage alive through
/// installation/recovery; planning does not pin images or freeze GOT values.
YUKIPLT_API Result<HookPlan> build_hook_plan(const HookTarget* targets, size_t count,
                                             size_t* failed_target = nullptr);

enum class SessionState : uint8_t {
  kPrepared,
  kInstalled,
  kInstallFailed,
  kIndeterminate,
  kRestoreFailed,
  kReleased,
  kInvalidated,
  kAbandoned,
};
struct SessionTargetResult {
  PlannedTarget target;
  /// Prior slot value reported by the last install attempt, not a lifetime
  /// guarantee.  After a failed attempt it may be a candidate original even
  /// though nothing is installed; check the group's state.
  void* original = nullptr;
  SlotReport install_report;
  SlotReport restore_report;
};
struct SessionImageResult {
  ImageView image;
  std::vector<size_t> target_indices;
  HookHandle handle;
  SessionState state = SessionState::kPrepared;
  Error install_error;
  Error restore_error;
};
struct [[nodiscard]] SessionSummary {
  size_t attempted = 0;
  size_t succeeded = 0;
  size_t failed = 0;
  size_t retained = 0;  ///< Groups still holding ownership/recovery handles.
  bool ok() const noexcept { return failed == 0; }
};

/// Caller serializes all operations AND result access on one session. Storage
/// is allocated before any install. Destruction/move-overwrite does not unhook;
/// live handles retain the core's warning behavior. Explicit unhook or abandon
/// is required. Original outputs are caller-owned and distinct for each target.
class HookSession {
 public:
  HookSession() = default;
  HookSession(const HookSession&) = delete;
  HookSession& operator=(const HookSession&) = delete;
  HookSession(HookSession&&) noexcept = default;
  HookSession& operator=(HookSession&&) noexcept = default;

  const std::vector<SessionTargetResult>& targets() const noexcept { return targets_; }
  const std::vector<SessionImageResult>& images() const noexcept { return images_; }

  /// One transaction per image in first-appearance order. Continue after group
  /// errors; no rollback across images. Retry only Prepared/InstallFailed.
  /// A remaining Indeterminate/RestoreFailed group counts as failed and is
  /// never overwritten: explicitly unhook/abandon it first.
  YUKIPLT_API SessionSummary install();
  YUKIPLT_API SessionSummary install(Error& admission_error);
  /// Reverse image order, attempt every valid handle despite earlier errors.
  /// Failed handles remain retryable; invalidated images consume their handle.
  YUKIPLT_API SessionSummary unhook();
  YUKIPLT_API SessionSummary unhook(Error& admission_error);
  /// Drop all remaining ownership without touching slots or page protections.
  YUKIPLT_API SessionSummary abandon();
  YUKIPLT_API SessionSummary abandon(Error& admission_error);

  YUKIPLT_API SessionSummary install_cached(MapsCache&);
  YUKIPLT_API SessionSummary install_cached(MapsCache& cache, Error& admission_error);
  YUKIPLT_API SessionSummary unhook_cached(MapsCache&);
  YUKIPLT_API SessionSummary unhook_cached(MapsCache& cache, Error& admission_error);

 private:
  SessionSummary install_impl(MapsCache*);
  SessionSummary unhook_impl(MapsCache*);
  friend Result<HookSession> prepare_session(const HookPlan&);
  std::vector<SessionTargetResult> targets_;
  std::vector<SessionImageResult> images_;
  std::vector<std::string> paths_;  ///< Per image group: planned system path.
};

/// Copy a validated plan and preallocate every report/group before any write.
/// Failure never leaves a partial session or any installed hook.
YUKIPLT_API Result<HookSession> prepare_session(const HookPlan& plan);

/// Optional value precondition, checked after VMA validation and before page
/// raises/original publication. A later external writer is still checked by CAS.
/// Equality is not proof of provider identity or of an unchanged ownership history.
struct FunctionHookRequestEx {
  FunctionImportSelector target;
  void* replacement = nullptr;
  void** out_original = nullptr;
  bool require_current = false;
  uintptr_t expected_current = 0;
};
/// diagnostic is the immediate operation result, including failures without a
/// surviving handle. It must not alias requests/reports/original outputs.
YUKIPLT_API HookResult hook_function_imports_ex(const ImageView&, const FunctionHookRequestEx*,
                                                size_t count, SlotReport* reports = nullptr,
                                                RecoveryDiagnostic* diagnostic = nullptr);

/// Borrowed selector strings remain valid only while the owning import is
/// unchanged/alive. Pins exact source, type, version, hidden bit and table index.
YUKIPLT_API FunctionImportSelector exact_function_selector(const FunctionImport&) noexcept;

struct FunctionHookTarget {
  ImageView image;
  FunctionHookRequestEx request;
};
struct FunctionPlannedTarget {
  ImageView image;
  FunctionImport selected;
  void* replacement = nullptr;
  void** out_original = nullptr;
  bool require_current = false;
  uintptr_t expected_current = 0;
};
class FunctionHookSession;
class FunctionHookPlan {
 public:
  const std::vector<FunctionPlannedTarget>& targets() const noexcept { return targets_; }

 private:
  friend Result<FunctionHookPlan> build_function_hook_plan(const FunctionHookTarget*, size_t,
                                                           size_t*);
  friend Result<FunctionHookSession> prepare_function_session(const FunctionHookPlan&);
  std::vector<FunctionPlannedTarget> targets_;
  std::vector<std::string> paths_;  ///< Per target: system image path when planned.
};
/// Read-only owning metadata; never reads GOT or holds an image alive. One
/// image group is limited to 64 targets, with no automatic splitting.  Like
/// build_hook_plan(), a system image's path is recorded at planning time and
/// must still match at installation (kImageInvalidated otherwise).
YUKIPLT_API Result<FunctionHookPlan> build_function_hook_plan(const FunctionHookTarget*,
                                                              size_t count,
                                                              size_t* failed_target = nullptr);
struct FunctionSessionTargetResult {
  FunctionPlannedTarget target;
  void* original = nullptr;
  SlotReport install_report;
  SlotReport restore_report;
};
struct FunctionSessionImageResult {
  ImageView image;
  std::vector<size_t> target_indices;
  HookHandle handle;
  SessionState state = SessionState::kPrepared;
  Error install_error;
  Error restore_error;
  /// Last recovery state. After kAbandoned this is the snapshot taken before
  /// relinquishing ownership, not a live inspection or evidence of restoration.
  /// restore_error separately reports the result of the abandon operation.
  RecoveryDiagnostic operation;
};
/// Same lifetime and synchronization contract as HookSession: one transaction
/// per image, continue after failures, undo in reverse group order. Installation
/// revalidates metadata/VMAs/ownership. Destruction does not undo hooks or wait
/// for calls to finish. The caller owns replacement code and original storage.
class FunctionHookSession {
 public:
  FunctionHookSession() = default;
  FunctionHookSession(const FunctionHookSession&) = delete;
  FunctionHookSession& operator=(const FunctionHookSession&) = delete;
  FunctionHookSession(FunctionHookSession&&) noexcept = default;
  FunctionHookSession& operator=(FunctionHookSession&&) noexcept = default;
  const std::vector<FunctionSessionTargetResult>& targets() const noexcept { return targets_; }
  const std::vector<FunctionSessionImageResult>& images() const noexcept { return images_; }
  YUKIPLT_API SessionSummary install();
  YUKIPLT_API SessionSummary install(Error& admission_error);
  YUKIPLT_API SessionSummary unhook();
  YUKIPLT_API SessionSummary unhook(Error& admission_error);
  /// Leave slots/protections unchanged, preserving each group's last recovery
  /// snapshot. A snapshot failure retains that group's handle for a retry.
  YUKIPLT_API SessionSummary abandon();
  YUKIPLT_API SessionSummary abandon(Error& admission_error);

  YUKIPLT_API SessionSummary install_cached(MapsCache&);
  YUKIPLT_API SessionSummary install_cached(MapsCache& cache, Error& admission_error);
  YUKIPLT_API SessionSummary unhook_cached(MapsCache&);
  YUKIPLT_API SessionSummary unhook_cached(MapsCache& cache, Error& admission_error);

 private:
  SessionSummary install_impl(MapsCache*);
  SessionSummary unhook_impl(MapsCache*);
  friend Result<FunctionHookSession> prepare_function_session(const FunctionHookPlan&);
  std::vector<FunctionSessionTargetResult> targets_;
  std::vector<FunctionSessionImageResult> images_;
  std::vector<std::string> paths_;  ///< Per image group: planned system path.
};
YUKIPLT_API Result<FunctionHookSession> prepare_function_session(const FunctionHookPlan&);

// Explicit rule refresh; no background tracking. Each (rule, image instance)
// owns one single-slot group. Serialize all operations and result reads per
// RuleSet; different RuleSets share the core Registry/op lock. No user callback
// runs under internal locks. bindings() references expire at the next mutation.

/// State of one (rule, image instance) binding.
enum class RuleState : uint8_t {
  kPending,            ///< Matched but not yet installed (e.g. a prior conflict/OOM).
  kInstalled,          ///< Installed and owned.
  kInstallFailed,      ///< A clean install failure (conflict, OOM); retryable.
  kIndeterminate,      ///< Partial install; handle retained, must unhook/abandon.
  kReleased,           ///< Cleanly unhooked (by remove_rule/release_image/unhook).
  kInvalidated,        ///< Image no longer loaded; handle consumed by the core.
  kRestoreFailed,      ///< Unhook failed; handle retained, retryable.
  kAbandoned,          ///< Ownership relinquished without touching memory.
  kCapacityExhausted,  ///< No original slot available; terminal for this rule.
};

/// No original-output slot was assigned (output disabled or capacity refused).
/// Never use this value as an array index or increment it.
inline constexpr size_t kNoOriginalIndex = ~size_t{0};

/// One (rule, image instance) binding and its last result.  A read-only view;
/// valid until the next mutation of the owning RuleSet.
struct RuleBinding {
  uint64_t rule_id = 0;
  ImageView image{};  ///< Instance identity (bias/phdr/phnum/token).
  std::string path;   ///< Owning path used by the binding's installation.
  /// Slot in the rule's out_originals array, or kNoOriginalIndex. A valid index
  /// reserves storage; it does not imply that an original was published.
  size_t original_index = kNoOriginalIndex;
  RuleState state = RuleState::kPending;
  Error error;
  SlotReport install_report;
  SlotReport restore_report;
};

/// Summary of a RuleSet operation, same shape as SessionSummary.
struct [[nodiscard]] RuleSummary {
  size_t attempted = 0;
  size_t succeeded = 0;
  size_t failed = 0;
  size_t retained = 0;  ///< Live handles among this operation's targets, including skipped ones.
  [[nodiscard]] bool ok() const noexcept { return failed == 0; }
};

enum class RuleOperationStage : uint32_t {
  kValidate,
  kEnumerate,
  kBindingPrepare,
  kCapacity,
  kInstall,
  kRestore,
  kAbandon,
  /// refresh() re-verified an installed system-image binding under the linker
  /// lock.  kImageInvalidated: the bound identity (bias, program headers and
  /// path) is no longer loaded.  kSlotModifiedExternally: a slot no longer holds
  /// the replacement.  kVmaUnavailable: a slot could not be read.  The binding
  /// keeps its handle and ownership in every case; nothing is reinstalled.
  kVerify
};
struct RuleOperationEvent {
  uint64_t rule_id = 0;
  ImageView image{};
  std::string path;
  RuleOperationStage stage = RuleOperationStage::kValidate;
  Error error;
  bool attempted = false;
  RuleState state = RuleState::kPending;
  size_t binding_index = ~size_t{0};
  /// For kAbandon, the last recorded recovery state copied before relinquishing
  /// ownership. A successful event.error does not mean residual pages or slots
  /// were restored. This snapshot is not a live inspection after abandonment.
  RecoveryDiagnostic recovery;
};
/// Recover the cause recorded unambiguously by an event's stage. Other failures
/// return kNone: events do not retain every core operation's detailed cause.
[[nodiscard]] inline ErrorCause rule_event_cause(const RuleOperationEvent& event) noexcept {
  return event.stage == RuleOperationStage::kCapacity && !event.error.ok()
             ? ErrorCause::kOriginalCapacity
             : ErrorCause::kNone;
}
/// Caller-owned results. Preparation errors precede every side effect; events
/// own their text and are not references into the RuleSet. An ImageView remains
/// an identity, not a lifetime guarantee. kImageNotFound enumeration is normal.
struct RuleOperationReport {
  Error preparation_error;
  std::vector<RuleOperationEvent> events;
};
struct RuleStatistics {
  uint64_t rule_id = 0;
  bool removed = false;
  bool original_storage_enabled = false;
  size_t original_capacity = 0;
  size_t original_used = 0;
  size_t original_remaining = 0;
  size_t bindings = 0;
  size_t active_handles = 0;
};
struct RuleSetStatistics {
  size_t registered_rules = 0;
  size_t active_rules = 0;
  size_t bindings = 0;
  size_t active_handles = 0;
};

/// Move-only, like HookSession.  Destruction/move-overwrite does NOT unhook;
/// live handles retain the core's warning behavior.  The caller must explicitly
/// unhook() or abandon() before destruction.
class RuleSet {
 public:
  RuleSet() = default;
  YUKIPLT_API ~RuleSet();
  RuleSet(const RuleSet&) = delete;
  RuleSet& operator=(const RuleSet&) = delete;
  RuleSet(RuleSet&&) noexcept = default;
  RuleSet& operator=(RuleSet&&) noexcept = default;

  // Copy the selector and return a nonzero rule id. Caller-owned original slots
  // must be aligned, stable and disjoint; published slots are never cleared or reused.
  // Capacity zero disables original output. Exhaustion fails before writes and is
  // terminal for that binding; remove and re-register with larger storage to recover.
  // A null/empty symbol returns NullPointer/InvalidArgument.
  YUKIPLT_API Result<uint64_t> add_rule(const ImageSelector& filter,
                                        const FunctionImportSelector& selector, void* replacement,
                                        void** out_originals, size_t capacity);

  /// Convenience overload for a plain JUMP_SLOT name/version selector.
  YUKIPLT_API Result<uint64_t> add_rule(const ImageSelector& filter, const ImportSelector& selector,
                                        void* replacement, void** out_originals, size_t capacity);

  // Prepare all enumeration caches/reports before any install; preparation failure
  // returns failed=1, attempted=retained=0. Use the report overload for its error.
  // Then install one slot per (rule, instance), allowing partial success. Clean
  // failures retry with the same original slot; released/invalidated bindings take
  // a fresh slot. Indeterminate, restore-failed, abandoned and exhausted bindings
  // are not reinstalled. Retained system bindings revalidate path/identity/value
  // under linker -> op locks; mismatch preserves ownership and reports failure.
  // Reuse one maps read per verified image, discard it before later installations.
  // Only core-confirmed invalidation ends ownership; callers prevent reload ABA.
  // retained counts unique targets enumerated this call, including skipped live
  // bindings. statistics().active_handles counts the entire outstanding population.
  YUKIPLT_API RuleSummary refresh();
  YUKIPLT_API RuleSummary refresh(Error& admission_error);

  /// Undo this RuleSet's hooks on one image instance before the caller unloads
  /// it.  Failure keeps the handle and ownership and is reported.  This does not
  /// disable the rules: a later refresh()/notify_loaded() that still finds the
  /// instance applies them again (see refresh()); remove_rule() stops a rule.
  YUKIPLT_API RuleSummary release_image(const ImageView& image);
  YUKIPLT_API RuleSummary release_image(const ImageView& image, Error& admission_error);

  // Custom-loader notifications run synchronously after relocation/final
  // protections and before unmapping, respectively. Keep the instance stable,
  // serialize RuleSet access, and hold no loader lock or dl_iterate_phdr callback.
  // path supplies diagnostics and matching. Token-0 system images are rejected.
  YUKIPLT_API RuleSummary notify_loaded(const ImageView& image, const char* path);
  YUKIPLT_API RuleSummary notify_loaded(const ImageView& image, const char* path,
                                        Error& admission_error);
  YUKIPLT_API RuleSummary notify_unloading(const ImageView& image);
  YUKIPLT_API RuleSummary notify_unloading(const ImageView& image, Error& admission_error);

  /// Remove a rule: undo all its bindings (reverse order), keep failures, and
  /// stop it participating in future refreshes.
  /// A third-party slot value does not release this library's ownership. Retry
  /// release_image()/unhook() after resolving the conflict, or explicitly
  /// abandon() the whole RuleSet; there is no single-binding abandon operation.
  YUKIPLT_API RuleSummary remove_rule(uint64_t rule_id);
  YUKIPLT_API RuleSummary remove_rule(uint64_t rule_id, Error& admission_error);

  /// Undo all of this RuleSet's hooks (reverse order), keeping failures.  The
  /// rules stay registered; a later refresh() applies them again.
  YUKIPLT_API RuleSummary unhook();
  YUKIPLT_API RuleSummary unhook(Error& admission_error);

  /// Relinquish all registrations without touching memory.  Abandoned bindings
  /// are never re-applied by refresh()/notify_loaded().
  YUKIPLT_API RuleSummary abandon();
  YUKIPLT_API RuleSummary abandon(Error& admission_error);

  /// Detailed overloads reserve all report storage before any hook/unhook.
  /// A preparation failure returns failed=1, attempted=0 and preserves hooks.
  YUKIPLT_API RuleSummary refresh(RuleOperationReport& report);
  YUKIPLT_API RuleSummary refresh(RuleOperationReport& report, Error& admission_error);
  YUKIPLT_API RuleSummary notify_loaded(const ImageView& image, const char* path,
                                        RuleOperationReport& report);
  YUKIPLT_API RuleSummary notify_loaded(const ImageView& image, const char* path,
                                        RuleOperationReport& report, Error& admission_error);
  YUKIPLT_API RuleSummary notify_unloading(const ImageView& image, RuleOperationReport& report);
  YUKIPLT_API RuleSummary notify_unloading(const ImageView& image, RuleOperationReport& report,
                                           Error& admission_error);
  YUKIPLT_API RuleSummary release_image(const ImageView& image, RuleOperationReport& report);
  YUKIPLT_API RuleSummary release_image(const ImageView& image, RuleOperationReport& report,
                                        Error& admission_error);
  YUKIPLT_API RuleSummary remove_rule(uint64_t rule_id, RuleOperationReport& report);
  YUKIPLT_API RuleSummary remove_rule(uint64_t rule_id, RuleOperationReport& report,
                                      Error& admission_error);
  YUKIPLT_API RuleSummary unhook(RuleOperationReport& report);
  YUKIPLT_API RuleSummary unhook(RuleOperationReport& report, Error& admission_error);
  /// Prepare all report storage and recovery snapshots before relinquishing any
  /// ownership. Preparation failure leaves every handle intact. The events keep
  /// the last known recovery state even after successful abandonment.
  YUKIPLT_API RuleSummary abandon(RuleOperationReport& report);
  YUKIPLT_API RuleSummary abandon(RuleOperationReport& report, Error& admission_error);
  [[nodiscard]] YUKIPLT_API RuleSetStatistics statistics() const noexcept;
  YUKIPLT_API RuleSetStatistics statistics(Error& admission_error) const noexcept;
  [[nodiscard]] YUKIPLT_API Result<RuleStatistics> statistics(uint64_t rule_id) const noexcept;

  /// Explicitly discard released/invalidated bindings with no live handle and
  /// return the number removed. Other states, including abandoned tombstones,
  /// remain. Does not touch images, Registry, permissions or original outputs;
  /// published original indices are never recycled and capacity is not reset.
  /// Allocation failure leaves the object unchanged. A successful removal
  /// invalidates bindings() references and all previously reported binding_index
  /// values; caller-owned report contents remain valid. This bounds reclaimable
  /// history only, not the population of live/failed/abandoned bindings.
  [[nodiscard]] YUKIPLT_API Result<size_t> prune_history();

  /// Per-(rule, image instance) results.  Valid until the next mutation.
  [[nodiscard]] const std::vector<RuleBinding>& bindings() const noexcept { return bindings_; }

  YUKIPLT_API RuleSummary unhook_cached(MapsCache&, RuleOperationReport& report);
  YUKIPLT_API RuleSummary unhook_cached(MapsCache& cache, RuleOperationReport& report,
                                        Error& admission_error);
  YUKIPLT_API RuleSummary remove_rule_cached(MapsCache&, uint64_t rule_id,
                                             RuleOperationReport& report);
  YUKIPLT_API RuleSummary remove_rule_cached(MapsCache& cache, uint64_t rule_id,
                                             RuleOperationReport& report, Error& admission_error);
  YUKIPLT_API RuleSummary notify_unloading_cached(MapsCache&, const ImageView& image,
                                                  RuleOperationReport& report);
  YUKIPLT_API RuleSummary notify_unloading_cached(MapsCache& cache, const ImageView& image,
                                                  RuleOperationReport& report,
                                                  Error& admission_error);
  YUKIPLT_API RuleSummary release_image_cached(MapsCache&, const ImageView& image,
                                               RuleOperationReport& report);
  YUKIPLT_API RuleSummary release_image_cached(MapsCache& cache, const ImageView& image,
                                               RuleOperationReport& report, Error& admission_error);
  YUKIPLT_API RuleSummary notify_loaded_cached(MapsCache&, const ImageView& image, const char* path,
                                               RuleOperationReport& report);
  YUKIPLT_API RuleSummary notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                               const char* path, RuleOperationReport& report,
                                               Error& admission_error);
  YUKIPLT_API RuleSummary refresh_cached(MapsCache&, RuleOperationReport& report);
  YUKIPLT_API RuleSummary refresh_cached(MapsCache& cache, RuleOperationReport& report,
                                         Error& admission_error);
  YUKIPLT_API RuleSummary unhook_cached(MapsCache&);
  YUKIPLT_API RuleSummary unhook_cached(MapsCache& cache, Error& admission_error);
  YUKIPLT_API RuleSummary remove_rule_cached(MapsCache&, uint64_t rule_id);
  YUKIPLT_API RuleSummary remove_rule_cached(MapsCache& cache, uint64_t rule_id,
                                             Error& admission_error);
  YUKIPLT_API RuleSummary notify_unloading_cached(MapsCache&, const ImageView& image);
  YUKIPLT_API RuleSummary notify_unloading_cached(MapsCache& cache, const ImageView& image,
                                                  Error& admission_error);
  YUKIPLT_API RuleSummary notify_loaded_cached(MapsCache&, const ImageView& image,
                                               const char* path);
  YUKIPLT_API RuleSummary notify_loaded_cached(MapsCache& cache, const ImageView& image,
                                               const char* path, Error& admission_error);
  YUKIPLT_API RuleSummary release_image_cached(MapsCache&, const ImageView& image);
  YUKIPLT_API RuleSummary release_image_cached(MapsCache& cache, const ImageView& image,
                                               Error& admission_error);
  YUKIPLT_API RuleSummary refresh_cached(MapsCache&);
  YUKIPLT_API RuleSummary refresh_cached(MapsCache& cache, Error& admission_error);

 private:
  RuleSummary unhook_impl(RuleOperationReport& report, MapsCache* cache);
  RuleSummary remove_rule_impl(uint64_t rule_id, RuleOperationReport& report, MapsCache* cache);
  RuleSummary notify_unloading_impl(const ImageView& image, RuleOperationReport& report,
                                    MapsCache* cache);
  RuleSummary release_image_impl(const ImageView& image, RuleOperationReport& report,
                                 MapsCache* cache);
  RuleSummary notify_loaded_impl(const ImageView& image, const char* path,
                                 RuleOperationReport& report, MapsCache* cache);
  RuleSummary refresh_impl(RuleOperationReport& report, MapsCache* cache);
  RuleSummary unhook_impl(MapsCache* cache);
  RuleSummary remove_rule_impl(uint64_t rule_id, MapsCache* cache);
  RuleSummary notify_unloading_impl(const ImageView& image, MapsCache* cache);
  RuleSummary notify_loaded_impl(const ImageView& image, const char* path, MapsCache* cache);
  RuleSummary release_image_impl(const ImageView& image, MapsCache* cache);
  RuleSummary refresh_impl(MapsCache* cache);
  struct Rule {
    uint64_t id = 0;
    ImageSelector filter{};
    std::string filter_name;                  ///< Owns filter.name.
    FunctionImportSelector selector{};        ///< Points into the owned strings below.
    std::string symbol, version, dependency;  ///< Owned selector strings.
    void* replacement = nullptr;
    void** out_originals = nullptr;
    size_t capacity = 0;
    size_t next_slot = 0;  ///< Next free out_originals index (grow-only).
    bool removed = false;
  };

  // Internal helpers (rules.cpp).
  Rule* find_rule(uint64_t id) noexcept;
  Rule* rule_by_id(uint64_t id) noexcept;
  const Rule* rule_by_id(uint64_t id) const noexcept;
  size_t find_binding(uint64_t rule_id, const ImageView& image) const noexcept;
  static void rebind_selector(Rule& rule) noexcept;
  size_t retained_for(uint64_t rule_id = 0, const ImageView* image = nullptr,
                      bool active_only = false) const noexcept;
  // Apply one rule to one already-matched image instance, updating bindings.
  void apply_rule_to_image(Rule& rule, const ImageView& image, const char* path,
                           const char* expected_path, RuleSummary& summary,
                           RuleOperationEvent* event = nullptr, MapsCache* cache = nullptr);
  // Re-verify an installed system-image binding found again by refresh().
  void record_verification(size_t index, Error error, RuleSummary& summary,
                           RuleOperationEvent& event);
  Error prepare_undo_report(RuleOperationReport& report, uint64_t rule_id, const ImageView* image,
                            RuleOperationStage stage) const;
  void finish_undo_report(RuleOperationReport& report) const;

  std::vector<Rule> rules_;
  std::vector<RuleBinding> bindings_;
  std::vector<HookHandle> handles_;  ///< Parallel to bindings_ (HookHandle is move-only).
  /// (rule id, image instance) -> index of that pair's current binding in
  /// bindings_, so refresh() looks it up in O(1).  A binding superseded
  /// by a re-application stays in bindings_ but leaves the index.
  struct BindingKey {
    uint64_t rule_id;
    uintptr_t load_bias;
    const void* phdr_table;
    size_t phdr_count;
    uint64_t instance_token;
    bool operator==(const BindingKey& o) const noexcept {
      return rule_id == o.rule_id && load_bias == o.load_bias && phdr_table == o.phdr_table &&
             phdr_count == o.phdr_count && instance_token == o.instance_token;
    }
  };
  struct BindingKeyHash {
    size_t operator()(const BindingKey& k) const noexcept;
  };
  static BindingKey key_of(uint64_t rule_id, const ImageView& image) noexcept {
    return {rule_id, image.load_bias, image.phdr_table, image.phdr_count, image.instance_token};
  }
  std::unordered_map<BindingKey, size_t, BindingKeyHash> binding_index_;
  uint64_t next_rule_id_ = 1;
};

enum class TextMatchKind : uint8_t { kAny, kExact, kPrefix, kSuffix, kSubstring };
struct TextMatch {
  TextMatchKind kind = TextMatchKind::kAny;
  std::string_view text;
};
struct CandidatePattern {
  TextMatch path;
  TextMatch symbol;
};
struct FunctionCandidate {
  ImageView image;
  std::string path;
  FunctionImport selected;
  /// Borrowed strings refer to this candidate. Explicitly build a function
  /// plan/session to install; candidate generation never writes target memory.
  FunctionImportSelector selector() const noexcept { return exact_function_selector(selected); }
};
/// The candidate reference is borrowed for this invocation. The host may copy
/// metadata and reenter library APIs; no linker/op lock is held. Return false
/// to exclude, or an error to fail the whole expansion. Exceptions are contained
/// (bad_alloc -> OutOfMemory, other exceptions -> UnsupportedFormat). The host
/// controls its own policy/work/memory, including regex if desired.
using CandidatePredicate = Result<bool> (*)(const FunctionCandidate&, void* context);
struct CandidateLimits {
  size_t text_bytes = size_t{8} * 1024 * 1024;
  size_t candidates = 4096;
};
struct CandidateSelection {
  uint32_t sources = 0;  // Explicit nonzero RelocationSourceBit mask.
  uint32_t types = 0;    // Explicit nonzero RelocationTypeBit mask.
  CandidatePattern include;
  const CandidatePattern* exclude = nullptr;
  size_t exclude_count = 0;
  CandidatePredicate predicate = nullptr;
  void* context = nullptr;
  CandidateLimits limits;
};
/// Only the explicitly supplied images are inspected; count==0 yields an empty
/// result. All image metadata/path strings and filter text are owned before any
/// host callback. Matching is case-sensitive by byte, with no pattern syntax;
/// path AND symbol, exclusions win. Empty prefix/suffix/substring matches every
/// text; empty exact matches only empty text.
/// A kAny match requires empty text. Duplicate image instances/selected slots
/// are errors. A matched ineligible relocation fails rather than being skipped;
/// GLOB_DAT current-value conditions are deferred to installation.
/// Limits may only be lowered. Text counts path+symbol bytes once per examined
/// source/type record. These are not total snapshot/process/host memory limits.
/// Snapshot acquisition additionally shares the function scanner's 2^22-record
/// and 64 MiB cumulative logical materialization budgets across all images.
/// System image paths must match dlpi_name under the same linker callback as
/// inspection (including the empty main-executable name). Mismatch invalidates
/// the image before any host callback. This does not eliminate same-path ABA.
/// Failure returns no partial candidates; host callback side effects, if any,
/// cannot be undone. Caller keeps supplied images stable for snapshot acquisition.
YUKIPLT_API Result<std::vector<FunctionCandidate>> expand_function_candidates(
    const NamedImage* images, size_t count, const CandidateSelection& selection);
/// Same result and callback error codes as expand_function_candidates. When
/// non-null, diagnostic is written on every return; callback exceptions and
/// invalid Result<bool> values have cause kHostCallback. Nested diagnosed calls
/// from the predicate cannot leak their cause into this operation.
YUKIPLT_API Result<std::vector<FunctionCandidate>> expand_function_candidates_diagnosed(
    const NamedImage* images, size_t count, const CandidateSelection& selection,
    OperationDiagnostic* diagnostic);

/// Stable original-address storage. Keep it and all replacement code/context
/// alive until recovery AND caller-established quiescence. No implicit unhook,
/// reset or reuse. The declared Fn does not prove the ELF target's actual ABI.
template <typename Fn>
class OriginalSlot {
  static_assert(std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>,
                "OriginalSlot requires a function pointer type");

 public:
  OriginalSlot() = default;
  OriginalSlot(const OriginalSlot&) = delete;
  OriginalSlot& operator=(const OriginalSlot&) = delete;
  OriginalSlot(OriginalSlot&&) = delete;
  OriginalSlot& operator=(OriginalSlot&&) = delete;
  [[nodiscard]] Fn load() const noexcept { return reinterpret_cast<Fn>(load_original(&value_)); }
  [[nodiscard]] void** output_address() noexcept { return &value_; }
  [[nodiscard]] FunctionHookRequest request(const FunctionImportSelector& selector,
                                            Fn replacement) noexcept {
    return {selector, reinterpret_cast<void*>(replacement), &value_};
  }

 private:
  alignas(void*) void* value_ = nullptr;
};
}  // namespace yukiplt

namespace yukiplt {
YUKIPLT_API HookResult hook_batch_cached(MapsCache&, const ImageView&, const HookRequest*, size_t,
                                         SlotReport* = nullptr, OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_batch_named_cached(MapsCache&, const NamedImage&, const HookRequest*,
                                               size_t, SlotReport* = nullptr,
                                               OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_imports_cached(MapsCache&, const ImageView&, const SelectedHookRequest*,
                                           size_t, SlotReport* = nullptr,
                                           OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_imports_named_cached(MapsCache&, const NamedImage&,
                                                 const SelectedHookRequest*, size_t,
                                                 SlotReport* = nullptr,
                                                 OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_function_imports_cached(MapsCache&, const ImageView&,
                                                    const FunctionHookRequestEx*, size_t,
                                                    SlotReport* = nullptr,
                                                    OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_function_imports_named_cached(MapsCache&, const NamedImage&,
                                                          const FunctionHookRequestEx*, size_t,
                                                          SlotReport* = nullptr,
                                                          OperationDiagnostic* = nullptr);
YUKIPLT_API HookResult hook_batch_nonnull_named_cached(MapsCache&, const NamedImage&,
                                                       const HookRequest*, size_t,
                                                       SlotReport* = nullptr,
                                                       OperationDiagnostic* = nullptr);
YUKIPLT_API Error unhook_cached(MapsCache&, HookHandle&, SlotReport* = nullptr, size_t = 0,
                                OperationDiagnostic* = nullptr);
YUKIPLT_API Error query_cached(MapsCache&, const HookHandle&, SlotReport*, size_t, size_t*);
}  // namespace yukiplt
#endif  // YUKIPLT_HPP_
