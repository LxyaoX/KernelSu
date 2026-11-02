// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_H_
#define YUKIPLT_H_

#include <stddef.h>
#include <stdint.h>

// Native ABI selection shared by C and C++ callers.

#if defined(__aarch64__) || defined(_M_ARM64)
#define YUKIPLT_ELF_BITS 64
#define YUKIPLT_ELF_MACHINE 183
#define YUKIPLT_RELOC_JUMP_SLOT 1026
#define YUKIPLT_RELOC_GLOB_DAT 1025
#define YUKIPLT_RELOC_RELATIVE 1027
#define YUKIPLT_RELOC_IRELATIVE 1032
#define YUKIPLT_RELOC_ABSOLUTE 257
#elif defined(__arm__) || defined(_M_ARM)
#define YUKIPLT_ELF_BITS 32
#define YUKIPLT_ELF_MACHINE 40
#define YUKIPLT_RELOC_JUMP_SLOT 22
#define YUKIPLT_RELOC_GLOB_DAT 21
#define YUKIPLT_RELOC_RELATIVE 23
#define YUKIPLT_RELOC_IRELATIVE 160
#define YUKIPLT_RELOC_ABSOLUTE 2
#elif defined(__x86_64__) || defined(_M_X64)
#define YUKIPLT_ELF_BITS 64
#define YUKIPLT_ELF_MACHINE 62
#define YUKIPLT_RELOC_JUMP_SLOT 7
#define YUKIPLT_RELOC_GLOB_DAT 6
#define YUKIPLT_RELOC_RELATIVE 8
#define YUKIPLT_RELOC_IRELATIVE 37
#define YUKIPLT_RELOC_ABSOLUTE 1
#elif defined(__i386__) || defined(_M_IX86)
#define YUKIPLT_ELF_BITS 32
#define YUKIPLT_ELF_MACHINE 3
#define YUKIPLT_RELOC_JUMP_SLOT 7
#define YUKIPLT_RELOC_GLOB_DAT 6
#define YUKIPLT_RELOC_RELATIVE 8
#define YUKIPLT_RELOC_IRELATIVE 42
#define YUKIPLT_RELOC_ABSOLUTE 1
#else
#error "YukiPLT supports only ARM64, ARM32, x86_64 and x86"
#endif

#if YUKIPLT_ELF_BITS == 64
#define YUKIPLT_USES_RELA 1
#define YUKIPLT_ELF_CLASS 2
#else
#define YUKIPLT_USES_RELA 0
#define YUKIPLT_ELF_CLASS 1
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "YukiPLT supports only little-endian targets"
#endif

// Public symbol visibility; static consumers define YUKIPLT_STATIC.

#if defined(YUKIPLT_STATIC) && defined(__ELF__)
// Define consistently when compiling an embedded copy and its callers. Also
// hide all implementation symbols and archive dependencies in the host DSO;
// this marker only controls the public API (see docs/abi.md).
#define YUKIPLT_API __attribute__((visibility("hidden")))
#elif defined(__GNUC__) || defined(__clang__)
#define YUKIPLT_API __attribute__((visibility("default")))
#else
#define YUKIPLT_API
#endif

#ifdef __cplusplus
extern "C" {
#define YUKIPLT_C_NOEXCEPT noexcept
#else
#define YUKIPLT_C_NOEXCEPT
#endif

#define YUKIPLT_C_ABI_VERSION 1u
#define YUKIPLT_C_ANY_RELOCATION SIZE_MAX

typedef struct yukiplt_c_maps_cache {
  uint64_t token;
} yukiplt_c_maps_cache;

// Calls are not async-signal-safe: do not enter from a signal handler. Thread
// cancellation through an operation and longjmp across library frames are not
// supported. After a multithreaded fork, calls before exec require the controlled
// fork protocol and host runtime coordination. Never fork inside an operation
// or callback. Callers serialize
// access to each handle and retain images, replacements and outputs through
// recovery and their own quiescence protocol.

enum {
  YUKIPLT_C_SUCCESS = 0,
  YUKIPLT_C_INVALID_ARGUMENT = 1,
  YUKIPLT_C_NULL_POINTER = 2,
  YUKIPLT_C_INVALID_IMAGE = 3,
  YUKIPLT_C_CORRUPTED_ELF = 4,
  YUKIPLT_C_UNSUPPORTED_FORMAT = 5,
  YUKIPLT_C_UNSUPPORTED_RELOCATION = 6,
  YUKIPLT_C_SYMBOL_NOT_FOUND = 7,
  YUKIPLT_C_NO_HASH_TABLE = 8,
  YUKIPLT_C_AMBIGUOUS_SYMBOL = 9,
  YUKIPLT_C_SLOT_OWNERSHIP_CONFLICT = 10,
  YUKIPLT_C_ALREADY_HOOKED = 11,
  YUKIPLT_C_NOT_INSTALLED = 12,
  YUKIPLT_C_SLOT_MODIFIED_EXTERNALLY = 13,
  YUKIPLT_C_INDETERMINATE = 14,
  YUKIPLT_C_PERMISSION_DENIED = 15,
  YUKIPLT_C_IMAGE_INVALIDATED = 16,
  YUKIPLT_C_OUT_OF_MEMORY = 17,
  YUKIPLT_C_INTEGER_OVERFLOW = 18,
  YUKIPLT_C_OUT_OF_BOUNDS = 19,
  YUKIPLT_C_IMAGE_NOT_FOUND = 20,
  YUKIPLT_C_AMBIGUOUS_IMAGE = 21,
  YUKIPLT_C_VMA_UNAVAILABLE = 22,
  YUKIPLT_C_FORK_IN_PROGRESS = 23,
  YUKIPLT_C_FORK_CHILD_UNAVAILABLE = 24
};
enum { YUKIPLT_C_VERSION_ANY = 0, YUKIPLT_C_VERSION_UNVERSIONED = 1, YUKIPLT_C_VERSION_EXACT = 2 };
enum { YUKIPLT_C_HIDDEN_ANY = 0, YUKIPLT_C_NON_HIDDEN = 1, YUKIPLT_C_HIDDEN = 2 };
enum {
  YUKIPLT_C_UNTOUCHED = 0,
  YUKIPLT_C_INSTALLED = 1,
  YUKIPLT_C_RESTORED = 2,
  YUKIPLT_C_CONFLICT = 3,
  YUKIPLT_C_ROLLED_BACK = 4,
  YUKIPLT_C_ROLLBACK_FAILED = 5
};

typedef struct yukiplt_c_error {
  uint32_t code;
  // INDETERMINATE may carry the first restore errno, otherwise the raise errno.
  int32_t sys_errno;
} yukiplt_c_error;
// 0 is invalid. A live token has one owner. Do not copy it into another owner,
// overwrite it, or discard it before explicit unhook/abandon. No destructor.
typedef struct yukiplt_c_handle {
  uint64_t token;
} yukiplt_c_handle;
typedef struct yukiplt_c_image {
  uintptr_t load_bias;
  const void* program_headers;  // Native ABI Program Headers, kept readable by caller.
  size_t program_header_count;
  uint64_t instance_token;  // 0: system linker; otherwise unique custom load.
} yukiplt_c_image;
typedef struct yukiplt_c_selector {
  const char* symbol;
  uint32_t version_match;
  const char* version;     // Nonempty only for VERSION_EXACT; otherwise NULL.
  const char* dependency;  // NULL: no filter; otherwise nonempty VERNEED name.
  uint32_t hidden;
  size_t relocation_index;  // YUKIPLT_C_ANY_RELOCATION for no index filter.
} yukiplt_c_selector;
#define YUKIPLT_C_SELECTOR(name) \
  {(name), YUKIPLT_C_VERSION_ANY, NULL, NULL, YUKIPLT_C_HIDDEN_ANY, YUKIPLT_C_ANY_RELOCATION}
typedef struct yukiplt_c_request {
  yukiplt_c_selector target;
  void* replacement;
  void** out_original;
} yukiplt_c_request;
typedef struct yukiplt_c_slot_report {
  uintptr_t address;
  uintptr_t value_before;
  uintptr_t value_observed;
  uint32_t status;
} yukiplt_c_slot_report;

// --- Function GLOB_DAT / APS2 (opt-in, added in ABI v1; see yukiplt.h) ---
//
// These extend selection/installation to GLOB_DAT function slots in
// DT_RELA and DT_ANDROID_RELA, alongside JUMP_SLOT.  They are strictly opt-in:
// the caller names the relocation sources and types.  v1's existing structs and
// functions are unchanged; a caller probes availability with
// yukiplt_c_has_feature(YUKIPLT_C_FEATURE_FUNCTION_IMPORTS).
enum {
  YUKIPLT_C_SOURCE_JMPREL = 1u << 0,        // DT_JMPREL (PLT).
  YUKIPLT_C_SOURCE_RELA = 1u << 1,          // DT_RELA (.rela.dyn).
  YUKIPLT_C_SOURCE_ANDROID_RELA = 1u << 2,  // DT_ANDROID_RELA (APS2 packed).
  YUKIPLT_C_SOURCE_REL = 1u << 3,
  YUKIPLT_C_SOURCE_ANDROID_REL = 1u << 4,
  YUKIPLT_C_SOURCE_NATIVE_DYNAMIC =
      YUKIPLT_USES_RELA ? YUKIPLT_C_SOURCE_RELA : YUKIPLT_C_SOURCE_REL,
  YUKIPLT_C_SOURCE_NATIVE_ANDROID_PACKED =
      YUKIPLT_USES_RELA ? YUKIPLT_C_SOURCE_ANDROID_RELA : YUKIPLT_C_SOURCE_ANDROID_REL
};
enum {
  YUKIPLT_C_TYPE_JUMP_SLOT = 1u << 0,  // JUMP_SLOT.
  YUKIPLT_C_TYPE_GLOB_DAT = 1u << 1    // GLOB_DAT (function, addend 0, UND FUNC).
};
// Scopes a relocation_index filter to one table (matches the source bits' order).
enum {
  YUKIPLT_C_RELOC_SOURCE_JMPREL = 0,
  YUKIPLT_C_RELOC_SOURCE_RELA = 1,
  YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA = 2,
  YUKIPLT_C_RELOC_SOURCE_REL = 3,
  YUKIPLT_C_RELOC_SOURCE_ANDROID_REL = 4
};
enum {
  YUKIPLT_C_FEATURE_FUNCTION_IMPORTS = 1,
  YUKIPLT_C_FEATURE_RECOVERY_DIAGNOSTICS = 2,
  YUKIPLT_C_FEATURE_ERROR_NAMES = 3,
  YUKIPLT_C_FEATURE_EXPECTED_CURRENT = 4,
  YUKIPLT_C_FEATURE_ADDRESS_DISCOVERY = 5,
  YUKIPLT_C_FEATURE_REL_IMPORTS = 6,
  YUKIPLT_C_FEATURE_ERROR_CAUSES = 7,
  YUKIPLT_C_FEATURE_ORIGINAL_LOAD = 8,
  YUKIPLT_C_FEATURE_NAMED_IMAGE = 9,
  YUKIPLT_C_FEATURE_OWNED_IMAGES = 10,
  YUKIPLT_C_FEATURE_IMPORT_SNAPSHOTS = 11,
  YUKIPLT_C_FEATURE_PLANS = 12,
  YUKIPLT_C_FEATURE_SESSIONS = 13,
  YUKIPLT_C_FEATURE_RULES = 14,
  YUKIPLT_C_FEATURE_CANDIDATES = 15,
  YUKIPLT_C_FEATURE_IDLE_STORAGE = 16,
  YUKIPLT_C_FEATURE_CACHED_MAPS = 17,
  YUKIPLT_C_FEATURE_CONTROLLED_FORK = 18,
  YUKIPLT_C_FEATURE_MT_FORK = 19
};

enum {
  YUKIPLT_C_STAGE_NONE = 0,
  YUKIPLT_C_STAGE_PREPARE = 1,
  YUKIPLT_C_STAGE_EXPECTED_CURRENT = 2,
  YUKIPLT_C_STAGE_RAISE = 3,
  YUKIPLT_C_STAGE_COMMIT = 4,
  YUKIPLT_C_STAGE_ROLLBACK = 5,
  YUKIPLT_C_STAGE_RESTORE_PAGES = 6,
  YUKIPLT_C_STAGE_COMPLETE = 7
};
typedef struct yukiplt_c_recovery_diagnostic {
  uint32_t stage;
  yukiplt_c_error error;
  int32_t raise_errno;
  int32_t restore_errno;
  uint32_t rollback_failed;
  uint32_t image_invalidated;
  size_t residual_page_count;
} yukiplt_c_recovery_diagnostic;
typedef struct yukiplt_c_residual_page {
  uintptr_t address;
  uint32_t original_protection;
  uint32_t raised_protection;
  int32_t sys_errno;
} yukiplt_c_residual_page;

// Registry-only recorded state, not current permissions. diagnostic is required;
// pages may be NULL with zero capacity for count-only. Copies at most capacity
// pages, and reports the full count. Outputs remain unchanged on error.
YUKIPLT_API yukiplt_c_error yukiplt_c_query_recovery(const yukiplt_c_handle*,
                                                     yukiplt_c_recovery_diagnostic* diagnostic,
                                                     yukiplt_c_residual_page* pages,
                                                     size_t capacity) YUKIPLT_C_NOEXCEPT;

// struct_size must be set to sizeof(yukiplt_c_function_selector) (use the macro);
// a mismatch is rejected with INVALID_ARGUMENT.  It is a layout check, not an
// extension mechanism: yukiplt_c_function_request embeds this struct by value,
// so its size is part of the v1 ABI and fields cannot be appended in place.  A
// later extension adds new structs and entry points instead.  sources and types
// are bitmasks that must both be nonzero — naming them is the opt-in.
typedef struct yukiplt_c_function_selector {
  uint32_t struct_size;
  uint32_t sources;
  uint32_t types;
  uint32_t index_source;  // YUKIPLT_C_RELOC_SOURCE_*; scopes base.relocation_index.
  yukiplt_c_selector base;
} yukiplt_c_function_selector;
#define YUKIPLT_C_FUNCTION_SELECTOR(name)                                             \
  {(uint32_t)sizeof(yukiplt_c_function_selector),                                     \
   YUKIPLT_C_SOURCE_JMPREL | YUKIPLT_C_SOURCE_NATIVE_DYNAMIC |                        \
       YUKIPLT_C_SOURCE_NATIVE_ANDROID_PACKED,                                        \
   YUKIPLT_C_TYPE_JUMP_SLOT | YUKIPLT_C_TYPE_GLOB_DAT, YUKIPLT_C_RELOC_SOURCE_JMPREL, \
   YUKIPLT_C_SELECTOR(name)}
typedef struct yukiplt_c_function_request {
  yukiplt_c_function_selector target;
  void* replacement;
  void** out_original;
} yukiplt_c_function_request;

// ABI v1 targets native ARM64/ARM32/x86_64/x86 Android API23+. No STL, allocation ownership or C++
// exceptions cross this boundary. Error values match the C++ core. No operation
// silently succeeds after partial commit: INDETERMINATE retains a valid token.
// Threading: the C entry points follow the threading contract in yukiplt.h.
// They may be called concurrently; one handle token must not be used by two
// threads at once; no callback runs under internal locks.
YUKIPLT_API uint32_t yukiplt_c_abi_version(void) YUKIPLT_C_NOEXCEPT;
// Stable literal; never allocates. Unknown numeric values return "Unknown".
YUKIPLT_API const char* yukiplt_c_error_name(uint32_t code) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_image_from_soname(const char* name,
                                                        yukiplt_c_image* out) YUKIPLT_C_NOEXCEPT;

// Strings are borrowed during the call. Selector filters are ANDed and must
// select one JUMP_SLOT. No GLOB_DAT/APS2. Caller holds images/replacements/output
// storage alive. Original outputs must be caller-owned writable storage, never
// GOT slots; aliases of planned or already owned slots are rejected before
// publication. The output handle must be zero; failure never overwrites an
// existing handle. Non-null original outputs must be distinct and aligned, else
// INVALID_ARGUMENT before any lock or write. A system image with no matching
// live identity returns IMAGE_INVALIDATED. A bare image has no discovery path;
// use the named entries to reject another path at the same address. Recorded
// system hooks also check their installation path during unhook/live query.
// Same-path/same-address reloads still require caller lifecycle coordination.
// Originals are release-published after every page
// raise succeeded and before the first CAS, and never cleared afterward; a
// rolled-back conflict may leave a candidate original there without any hook
// installed. Replacement must use yukiplt_c_load_original(), or an acquire
// fence followed by an acquire load; an acquire load alone does not order the
// preceding PLT/GOT load on ARM. They may be another hook's
// replacement. A clean failure leaves no net GOT change, restored protections
// and no ownership; that excludes original outputs.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook(const yukiplt_c_image*, const yukiplt_c_selector*,
                                           void* replacement, void** out_original,
                                           yukiplt_c_handle* out_handle,
                                           yukiplt_c_slot_report* report) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch(const yukiplt_c_image*, const yukiplt_c_request*,
                                                 size_t count, yukiplt_c_handle* out_handle,
                                                 yukiplt_c_slot_report* reports) YUKIPLT_C_NOEXCEPT;
// One image, 1..64 requests, reports either NULL or at least count elements.
// Transaction recovery is guaranteed, not an atomic cross-slot switch.

// Success and IMAGE_INVALIDATED consume the handle. IMAGE_INVALIDATED means a
// system image no longer loaded or a custom image passed to forget_image.
// Transient/conflict/OOM/OUT_OF_BOUNDS/VMA_UNAVAILABLE/INDETERMINATE failures
// preserve it for retry; for unhook, OUT_OF_BOUNDS can only mean a slot page
// missing from the maps read (unhook parses no ELF metadata). Success does not establish
// replacement quiescence; caller must coordinate code/context reclamation.
YUKIPLT_API yukiplt_c_error yukiplt_c_unhook(yukiplt_c_handle*, yukiplt_c_slot_report* reports,
                                             size_t capacity) YUKIPLT_C_NOEXCEPT;
// Only relinquishes ownership; never changes slots or permissions.
YUKIPLT_API yukiplt_c_error yukiplt_c_abandon(yukiplt_c_handle*) YUKIPLT_C_NOEXCEPT;
// Slot status is operation-specific: hook reports commit/rollback outcomes;
// unhook reports RESTORED only for slots previously written by this owner and
// UNTOUCHED for never-written slots, with actual before/observed values. Query
// uses INSTALLED for readable registered slots, not as a value/ownership proof;
// its value_before is the recorded original and observed is the current value.
// Unreadable query slots are UNTOUCHED. On query failure all outputs, including
// count, remain unchanged.
// count receives total slots; at most capacity reports are filled. Reports may
// be NULL (count-only). A copied token here is a borrowed view, not a new owner.
YUKIPLT_API yukiplt_c_error yukiplt_c_query(const yukiplt_c_handle*, yukiplt_c_slot_report* reports,
                                            size_t capacity, size_t* count) YUKIPLT_C_NOEXCEPT;
// Custom images only; matches load bias, Program Header address/count and
// token. Coordinate with the owner before unmapping; handles then unhook as
// IMAGE_INVALIDATED. System reload at the same address remains ABA.
YUKIPLT_API yukiplt_c_error yukiplt_c_forget_image(const yukiplt_c_image*) YUKIPLT_C_NOEXCEPT;

// Returns nonzero if the running library implements the given feature id (one of
// YUKIPLT_C_FEATURE_*).  The ABI version stays 1; this lets a caller built
// against a newer header detect whether the opt-in function-import entry points
// below are present before calling them.
YUKIPLT_API int yukiplt_c_has_feature(uint32_t feature_id) YUKIPLT_C_NOEXCEPT;

// Opt-in function-import hooks (JUMP_SLOT + function GLOB_DAT across the named
// sources).  Same image/selector/transaction/ownership/original-output contract
// as yukiplt_c_hook/yukiplt_c_hook_batch.  Each selector's struct_size must equal
// sizeof(yukiplt_c_function_selector), and its sources/types masks must both be
// nonzero. Both JUMP_SLOT and GLOB_DAT require an undefined STT_FUNC with zero
// effective addend. Only GLOB_DAT additionally requires a nonzero current slot
// value; any ineligible matched relocation fails the whole call
// with UNSUPPORTED_RELOCATION and modifies nothing.  Unhook/abandon/query with
// the shared yukiplt_c_unhook/abandon/query functions.
YUKIPLT_API yukiplt_c_error
yukiplt_c_hook_function(const yukiplt_c_image*, const yukiplt_c_function_selector*,
                        void* replacement, void** out_original, yukiplt_c_handle* out_handle,
                        yukiplt_c_slot_report* report) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions(
    const yukiplt_c_image*, const yukiplt_c_function_request*, size_t count,
    yukiplt_c_handle* out_handle, yukiplt_c_slot_report* reports) YUKIPLT_C_NOEXCEPT;

typedef struct yukiplt_c_function_request_ex {
  uint32_t struct_size;  // Must equal sizeof(this exact layout).
  yukiplt_c_function_request base;
  uint32_t require_current;  // 0 or 1 only.
  uintptr_t expected_current;
} yukiplt_c_function_request_ex;
// Value equality is a precondition, not provider authentication. Checked after
// complete VMA validation and before permissions/publication; CAS still applies.
// diagnostic is optional and immediate, including clean precondition failure.
// Output buffers must not alias requests or each other.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_ex(
    const yukiplt_c_image*, const yukiplt_c_function_request_ex*, size_t count,
    yukiplt_c_handle* out_handle, yukiplt_c_slot_report* reports,
    yukiplt_c_recovery_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
// Actual LOAD intervals only; complete enumeration rejects ambiguity. Android
// ARM64 ignores only the query's TBI top byte, without validating tags/PAC or
// pointer validity. The returned image is not held alive.
YUKIPLT_API yukiplt_c_error
yukiplt_c_image_containing(const void* address, yukiplt_c_image* out_image) YUKIPLT_C_NOEXCEPT;

// --- Error causes (YUKIPLT_C_FEATURE_ERROR_CAUSES) ---
//
// Some error codes cover several causes (for example OUT_OF_BOUNDS is metadata
// outside a LOAD, an exhausted scan budget, or a page missing from the maps
// read).  The *_diagnosed entry points behave exactly like their counterparts
// and also fill a yukiplt_c_operation_diagnostic for this call, on success and
// on every failure.  Values match yukiplt::ErrorCause; new ones may be
// appended, and an unknown value must be treated like NONE.
enum {
  YUKIPLT_C_CAUSE_NONE = 0,
  YUKIPLT_C_CAUSE_INVALID_REQUEST = 1,
  YUKIPLT_C_CAUSE_HANDLE_INVALID = 2,
  YUKIPLT_C_CAUSE_OWN_IMAGE = 3,
  YUKIPLT_C_CAUSE_DUPLICATE_TARGET = 4,
  YUKIPLT_C_CAUSE_OUTPUT_ALIAS = 5,
  YUKIPLT_C_CAUSE_REPLACEMENT_IS_CURRENT = 6,
  YUKIPLT_C_CAUSE_ORIGINAL_CAPACITY = 7,
  YUKIPLT_C_CAUSE_METADATA_BOUNDS = 8,
  YUKIPLT_C_CAUSE_SCAN_BUDGET = 9,
  YUKIPLT_C_CAUSE_MAPS_UNAVAILABLE = 10,
  YUKIPLT_C_CAUSE_MAPS_COVERAGE = 11,
  YUKIPLT_C_CAUSE_VMA_POLICY = 12,
  YUKIPLT_C_CAUSE_PROTECT_FAILED = 13,
  YUKIPLT_C_CAUSE_EXPECTED_CURRENT = 14,
  YUKIPLT_C_CAUSE_CAS_CONFLICT = 15,
  YUKIPLT_C_CAUSE_INTERNAL_EXCEPTION = 16,
  YUKIPLT_C_CAUSE_HOST_CALLBACK = 17,
  YUKIPLT_C_CAUSE_RANDOM_SOURCE = 18,
  YUKIPLT_C_CAUSE_PLATFORM = 19,
  YUKIPLT_C_CAUSE_IDENTITY_MISMATCH = 20,
  YUKIPLT_C_CAUSE_SLOT_DRIFT = 21,
  YUKIPLT_C_CAUSE_CACHE_INVALID = 22,
  YUKIPLT_C_CAUSE_CACHE_CONFLICT = 23,
  YUKIPLT_C_CAUSE_FORK_PROTOCOL = 24
};
// A new struct (version 1); existing structs are unchanged.  struct_size must
// equal sizeof(yukiplt_c_operation_diagnostic) on input, else INVALID_ARGUMENT
// and nothing else happens.
typedef struct yukiplt_c_operation_diagnostic {
  uint32_t struct_size;
  uint32_t stage;  // YUKIPLT_C_STAGE_*
  uint32_t cause;  // YUKIPLT_C_CAUSE_*
  yukiplt_c_error error;
  int32_t raise_errno;
  int32_t restore_errno;
  uint32_t rollback_failed;
  uint32_t image_invalidated;
  uint32_t handle_consumed;  // unhook only: this call consumed the handle.
  size_t residual_page_count;
} yukiplt_c_operation_diagnostic;
// Same contract as yukiplt_c_hook_functions_ex, plus the diagnostic.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_diagnosed(
    const yukiplt_c_image*, const yukiplt_c_function_request_ex*, size_t count,
    yukiplt_c_handle* out_handle, yukiplt_c_slot_report* reports,
    yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
// Same contract as yukiplt_c_unhook, plus the diagnostic.
YUKIPLT_API yukiplt_c_error
yukiplt_c_unhook_diagnosed(yukiplt_c_handle*, yukiplt_c_slot_report* reports, size_t capacity,
                           yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
// Stable literal; never allocates. Unknown values return "Unknown".
YUKIPLT_API const char* yukiplt_c_error_cause_name(uint32_t cause) YUKIPLT_C_NOEXCEPT;

// Read a published original with the required preceding acquire fence and
// acquire load. storage must be non-null, pointer-aligned, readable, alive and
// the same stable object passed as out_original. This helper does not validate
// memory or lifetime, allocate or lock. A null value is allowed; a non-null value
// proves neither successful installation, provider identity nor quiescence.
YUKIPLT_API void* yukiplt_c_load_original(void* const* storage) YUKIPLT_C_NOEXCEPT;

// Frozen new layout; struct_size is exact layout validation, not an extension
// mechanism. path is a caller-owned NUL-terminated exact dlpi_name, readable for
// this call. NULL is invalid; "" matches only an empty name. The image must have
// instance_token == 0. The library copies the live path before acquiring hook
// ownership. Neither this descriptor nor its path pins the image or identifies
// same-path/same-address reloads. Existing C image layout and entry points stay
// unchanged. Discover the name through the loader's public enumeration API.
typedef struct yukiplt_c_named_image {
  uint32_t struct_size;
  yukiplt_c_image image;
  const char* path;
} yukiplt_c_named_image;

// Same result, handle and output contracts as the corresponding old entry
// points, with the additional exact system-image path check before side effects.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_named(const yukiplt_c_named_image*,
                                                 const yukiplt_c_selector*, void* replacement,
                                                 void** out_original, yukiplt_c_handle*,
                                                 yukiplt_c_slot_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_named(const yukiplt_c_named_image*,
                                                       const yukiplt_c_request*, size_t count,
                                                       yukiplt_c_handle*,
                                                       yukiplt_c_slot_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_function_named(
    const yukiplt_c_named_image*, const yukiplt_c_function_selector*, void* replacement,
    void** out_original, yukiplt_c_handle*, yukiplt_c_slot_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_named(
    const yukiplt_c_named_image*, const yukiplt_c_function_request*, size_t count,
    yukiplt_c_handle*, yukiplt_c_slot_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_ex_named(
    const yukiplt_c_named_image*, const yukiplt_c_function_request_ex*, size_t count,
    yukiplt_c_handle*, yukiplt_c_slot_report*, yukiplt_c_recovery_diagnostic*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_diagnosed_named(
    const yukiplt_c_named_image*, const yukiplt_c_function_request_ex*, size_t count,
    yukiplt_c_handle*, yukiplt_c_slot_report*, yukiplt_c_operation_diagnostic*) YUKIPLT_C_NOEXCEPT;

// Release this copy's idle Registry allocations without touching GOT, page
// permissions, original storage or advanced objects. INVALID_ARGUMENT preserves
// storage if any hook/recovery record remains, including invalidated handles
// not yet consumed. Success permits later reuse; stale tokens stay invalid.
// This does NOT establish quiescence, restore abandoned hooks, destroy caller
// objects or reclaim compiler/runtime TLS. Before unloading a static host,
// separately restore all hooks, destroy all C/C++ objects and drain all calls.
YUKIPLT_API yukiplt_c_error yukiplt_c_trim_idle_storage(void) YUKIPLT_C_NOEXCEPT;

// Owned objects belong to the issuing library copy. Tokens are unique owners,
// not pointers and not yukiplt_c_handle values. Do not copy tokens into owners.
// Creation requires a zero output token; failure preserves it. Destroy clears
// a token on success; destroying zero succeeds. Stale/wrong-kind tokens fail.
// Serialize operations, reads and destruction on each object. No object pins an
// image. Keep the issuing library loaded until every object is destroyed.
typedef struct yukiplt_c_image_list {
  uint64_t token;
} yukiplt_c_image_list;
typedef struct yukiplt_c_import_snapshot {
  uint64_t token;
} yukiplt_c_import_snapshot;

enum {
  YUKIPLT_C_IMAGE_MAIN = 0,
  YUKIPLT_C_IMAGE_PATH = 1,
  YUKIPLT_C_IMAGE_BASENAME = 2,
  YUKIPLT_C_IMAGE_BIAS = 3
};
typedef struct yukiplt_c_image_selector {
  uint32_t struct_size;
  uint32_t match;
  const char* name;
  uintptr_t load_bias;
} yukiplt_c_image_selector;

// References into the caller's copied text buffer. length excludes the trailing
// NUL; required_text includes it. Empty strings still occupy one byte.
typedef struct yukiplt_c_text_ref {
  size_t offset;
  size_t length;
} yukiplt_c_text_ref;
typedef struct yukiplt_c_image_record {
  yukiplt_c_image image;
  yukiplt_c_text_ref path;
} yukiplt_c_image_record;

enum {
  YUKIPLT_C_SNAPSHOT_IMPORTS = 0,
  YUKIPLT_C_SNAPSHOT_VERSIONED = 1,
  YUKIPLT_C_SNAPSHOT_FUNCTIONS = 2
};
enum {
  YUKIPLT_C_VERSION_SOURCE_NONE = 0,
  YUKIPLT_C_VERSION_SOURCE_NEEDED = 1,
  YUKIPLT_C_VERSION_SOURCE_DEFINED = 2
};
typedef struct yukiplt_c_import_record {
  yukiplt_c_text_ref symbol;
  yukiplt_c_text_ref version;
  yukiplt_c_text_ref dependency;
  uintptr_t slot_address;
  size_t relocation_index;
  uint32_t symbol_index;
  uint32_t version_index;
  uint32_t version_table_present;
  uint32_t version_hidden;
  uint32_t version_source;
  uint32_t relocation_source;
  uint32_t relocation_type;
  uint32_t symbol_type;
} yukiplt_c_import_record;

// All input structures use exact struct_size checks, not appendable layouts.
// Strings are borrowed for the call. find_images requires an explicit selector.
// The returned immutable list owns paths but does not extend mapping lifetime.
YUKIPLT_API yukiplt_c_error yukiplt_c_find_images(const yukiplt_c_image_selector*,
                                                  yukiplt_c_image_list*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_find_image_containing(const void* address, yukiplt_c_image_list*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_image_list_destroy(yukiplt_c_image_list*) YUKIPLT_C_NOEXCEPT;

// Queries never re-enumerate, inspect GOT or modify ownership. required_count
// and required_text are mandatory, disjoint writable size_t objects. Both NULL
// buffers with zero capacities request sizes only. Otherwise both capacities
// must suffice; OUT_OF_BOUNDS writes only the required sizes, no partial data.
// Other errors preserve all outputs. Buffers/counts must not overlap each other
// or input storage, and must be aligned, accessible and live for the call.
YUKIPLT_API yukiplt_c_error yukiplt_c_image_list_copy(const yukiplt_c_image_list*,
                                                      yukiplt_c_image_record*,
                                                      size_t record_capacity, char* text,
                                                      size_t text_capacity, size_t* required_count,
                                                      size_t* required_text) YUKIPLT_C_NOEXCEPT;

// kind chooses the existing plain, versioned or function snapshot semantics.
// sources must be zero for plain/versioned snapshots and a valid nonzero source
// mask for function snapshots. Optional tables remain lazily validated. A named
// image also checks the exact system path; same-path/address ABA remains the
// caller's responsibility. Addresses are metadata, never installation tokens.
YUKIPLT_API yukiplt_c_error yukiplt_c_inspect_imports(const yukiplt_c_image*, uint32_t kind,
                                                      uint32_t sources, yukiplt_c_import_snapshot*)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_inspect_imports_named(const yukiplt_c_named_image*, uint32_t kind, uint32_t sources,
                                yukiplt_c_import_snapshot*) YUKIPLT_C_NOEXCEPT;
// Unique selection uses the existing full-scan ambiguity and eligibility rules.
// These return a one-entry owned snapshot, with skipped_relocations == 0.
YUKIPLT_API yukiplt_c_error yukiplt_c_select_import(const yukiplt_c_image*,
                                                    const yukiplt_c_selector*,
                                                    yukiplt_c_import_snapshot*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_select_function_import(const yukiplt_c_image*, const yukiplt_c_function_selector*,
                                 yukiplt_c_import_snapshot*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_import_snapshot_destroy(yukiplt_c_import_snapshot*)
    YUKIPLT_C_NOEXCEPT;
// Same buffer contract as image_list_copy. kind and skipped are mandatory output
// scalars, also disjoint. Plain snapshots report empty version/dependency and
// symbol_type=0 (not inspected), rather than validating extra ELF metadata.
YUKIPLT_API yukiplt_c_error yukiplt_c_import_snapshot_copy(
    const yukiplt_c_import_snapshot*, yukiplt_c_import_record*, size_t record_capacity, char* text,
    size_t text_capacity, size_t* required_count, size_t* required_text, uint32_t* kind,
    size_t* skipped_relocations) YUKIPLT_C_NOEXCEPT;

// Plans own selected metadata/path identities. Sessions copy their plans and
// allocate results before installation. Neither pins images or original storage.
typedef struct yukiplt_c_plan {
  uint64_t token;
} yukiplt_c_plan;
typedef struct yukiplt_c_session {
  uint64_t token;
} yukiplt_c_session;
typedef struct yukiplt_c_hook_target {
  uint32_t struct_size;
  yukiplt_c_image image;
  yukiplt_c_request request;
} yukiplt_c_hook_target;
typedef struct yukiplt_c_function_target {
  uint32_t struct_size;
  yukiplt_c_image image;
  yukiplt_c_function_request_ex request;
} yukiplt_c_function_target;
typedef struct yukiplt_c_summary {
  size_t attempted;
  size_t succeeded;
  size_t failed;
  size_t retained;
} yukiplt_c_summary;
enum {
  YUKIPLT_C_SESSION_PREPARED = 0,
  YUKIPLT_C_SESSION_INSTALLED = 1,
  YUKIPLT_C_SESSION_INSTALL_FAILED = 2,
  YUKIPLT_C_SESSION_INDETERMINATE = 3,
  YUKIPLT_C_SESSION_RESTORE_FAILED = 4,
  YUKIPLT_C_SESSION_RELEASED = 5,
  YUKIPLT_C_SESSION_INVALIDATED = 6,
  YUKIPLT_C_SESSION_ABANDONED = 7
};
typedef struct yukiplt_c_target_record {
  yukiplt_c_image image;
  yukiplt_c_import_record selected;
  void* replacement;
  void** out_original;
  uint32_t require_current;
  uintptr_t expected_current;
  void* original;  // Diagnostic value only; replacement uses load_original.
  yukiplt_c_slot_report install_report;
  yukiplt_c_slot_report restore_report;
} yukiplt_c_target_record;
typedef struct yukiplt_c_session_group {
  yukiplt_c_image image;
  uint32_t state;
  uint32_t has_live_handle;  // A boolean, never an owning Hook token.
  yukiplt_c_error install_error;
  yukiplt_c_error restore_error;
} yukiplt_c_session_group;

// Exact-size inputs, 1..64 targets per identical image, no automatic splitting.
// Failure sets failed_target to the first bad input or SIZE_MAX for global errors.
// All other outputs are preserved on failure. Strings are copied; original
// outputs/replacement contexts remain borrowed through recovery and quiescence.
YUKIPLT_API yukiplt_c_error yukiplt_c_build_plan(const yukiplt_c_hook_target*, size_t,
                                                 yukiplt_c_plan*,
                                                 size_t* failed_target) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_build_function_plan(const yukiplt_c_function_target*, size_t,
                                                          yukiplt_c_plan*,
                                                          size_t* failed_target) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_plan_destroy(yukiplt_c_plan*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_plan_size(const yukiplt_c_plan*, size_t*) YUKIPLT_C_NOEXCEPT;
// Single-record copying: both record/text NULL and capacity zero request text
// size only. Short buffers write only required_text. Plan-only result fields
// (original and operation reports) are zero. All outputs must be disjoint.
YUKIPLT_API yukiplt_c_error yukiplt_c_plan_target(const yukiplt_c_plan*, size_t index,
                                                  yukiplt_c_target_record*, char* text,
                                                  size_t capacity,
                                                  size_t* required_text) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_prepare_session(const yukiplt_c_plan*,
                                                      yukiplt_c_session*) YUKIPLT_C_NOEXCEPT;
// Destroy refuses any live/recovery handle with INVALID_ARGUMENT and retains
// the entire object. Explicit unhook or abandon must succeed first. Abandon
// never restores memory and never establishes quiescence.
YUKIPLT_API yukiplt_c_error yukiplt_c_session_destroy(yukiplt_c_session*) YUKIPLT_C_NOEXCEPT;
// One transaction per image, continue after errors; undo in reverse group order.
// summary is required. Partial failure returns its first failing group's error
// and retains every result/handle in the session. Caller serializes reads and
// operations. No fallible restoration runs in destruction.
YUKIPLT_API yukiplt_c_error yukiplt_c_session_install(yukiplt_c_session*,
                                                      yukiplt_c_summary*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_session_unhook(yukiplt_c_session*,
                                                     yukiplt_c_summary*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_session_abandon(yukiplt_c_session*,
                                                      yukiplt_c_summary*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_session_size(const yukiplt_c_session*, size_t* targets,
                                                   size_t* groups) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_session_target(const yukiplt_c_session*, size_t index,
                                                     yukiplt_c_target_record*, char* text,
                                                     size_t capacity,
                                                     size_t* required_text) YUKIPLT_C_NOEXCEPT;
// group is required; indices NULL/capacity zero is count-only. On insufficient
// capacity only required_indices is updated. No core handle is exposed.
YUKIPLT_API yukiplt_c_error yukiplt_c_session_group_copy(
    const yukiplt_c_session*, size_t index, yukiplt_c_session_group*, size_t* indices,
    size_t capacity, size_t* required_indices) YUKIPLT_C_NOEXCEPT;
// A retained diagnostic, not current memory permissions. After abandon this
// is the pre-abandon snapshot. Released groups have no residual page records.
YUKIPLT_API yukiplt_c_error yukiplt_c_session_recovery(const yukiplt_c_session*, size_t index,
                                                       yukiplt_c_recovery_diagnostic*,
                                                       yukiplt_c_residual_page*, size_t capacity,
                                                       size_t* required_pages) YUKIPLT_C_NOEXCEPT;

// Rule sets retain history until explicit pruning. Original slots are stable,
// grow-only caller storage; capacity zero disables original publication.
#define YUKIPLT_C_NO_ORIGINAL_INDEX SIZE_MAX
typedef struct yukiplt_c_rules {
  uint64_t token;
} yukiplt_c_rules;
typedef struct yukiplt_c_rule_report {
  uint64_t token;
} yukiplt_c_rule_report;
typedef struct yukiplt_c_bindings {
  uint64_t token;
} yukiplt_c_bindings;
enum {
  YUKIPLT_C_RULE_PENDING = 0,
  YUKIPLT_C_RULE_INSTALLED = 1,
  YUKIPLT_C_RULE_INSTALL_FAILED = 2,
  YUKIPLT_C_RULE_INDETERMINATE = 3,
  YUKIPLT_C_RULE_RELEASED = 4,
  YUKIPLT_C_RULE_INVALIDATED = 5,
  YUKIPLT_C_RULE_RESTORE_FAILED = 6,
  YUKIPLT_C_RULE_ABANDONED = 7,
  YUKIPLT_C_RULE_CAPACITY_EXHAUSTED = 8
};
enum {
  YUKIPLT_C_RULE_VALIDATE = 0,
  YUKIPLT_C_RULE_ENUMERATE = 1,
  YUKIPLT_C_RULE_BINDING_PREPARE = 2,
  YUKIPLT_C_RULE_CAPACITY = 3,
  YUKIPLT_C_RULE_INSTALL = 4,
  YUKIPLT_C_RULE_RESTORE = 5,
  YUKIPLT_C_RULE_ABANDON = 6,
  YUKIPLT_C_RULE_VERIFY = 7
};
typedef struct yukiplt_c_rule_event {
  uint64_t rule_id;
  yukiplt_c_image image;
  yukiplt_c_text_ref path;
  uint32_t stage;
  yukiplt_c_error error;
  uint32_t attempted;
  uint32_t state;
  size_t binding_index;
  yukiplt_c_recovery_diagnostic recovery;
} yukiplt_c_rule_event;
typedef struct yukiplt_c_rule_report_info {
  yukiplt_c_summary summary;
  yukiplt_c_error preparation_error;
  yukiplt_c_error operation_error;
} yukiplt_c_rule_report_info;
typedef struct yukiplt_c_binding_record {
  uint64_t rule_id;
  yukiplt_c_image image;
  yukiplt_c_text_ref path;
  size_t original_index;
  uint32_t state;
  yukiplt_c_error error;
  yukiplt_c_slot_report install_report;
  yukiplt_c_slot_report restore_report;
} yukiplt_c_binding_record;
typedef struct yukiplt_c_rules_statistics {
  size_t registered_rules;
  size_t active_rules;
  size_t bindings;
  size_t active_handles;
} yukiplt_c_rules_statistics;
typedef struct yukiplt_c_rule_statistics {
  uint64_t rule_id;
  uint32_t removed;
  uint32_t original_storage_enabled;
  size_t original_capacity;
  size_t original_used;
  size_t original_remaining;
  size_t bindings;
  size_t active_handles;
} yukiplt_c_rule_statistics;

YUKIPLT_API yukiplt_c_error yukiplt_c_rules_create(yukiplt_c_rules*) YUKIPLT_C_NOEXCEPT;
// Rejects ANY active/recovery handle, including targets absent from last refresh.
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_destroy(yukiplt_c_rules*) YUKIPLT_C_NOEXCEPT;
// Copies selector/filter strings; borrows original slots through recovery and
// quiescence. On error rule_id is unchanged. All output storage is disjoint.
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_add(yukiplt_c_rules*, const yukiplt_c_image_selector*,
                                                const yukiplt_c_selector*, void* replacement,
                                                void** originals, size_t capacity,
                                                uint64_t* rule_id) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_add_function(
    yukiplt_c_rules*, const yukiplt_c_image_selector*, const yukiplt_c_function_selector*,
    void* replacement, void** originals, size_t capacity, uint64_t* rule_id) YUKIPLT_C_NOEXCEPT;
// Reports are required zero output owners. Report allocation/token registration
// precedes effects; a nonzero report can be returned even on operation failure.
// Query and destroy it regardless of the error. Reports remain independent of
// later RuleSet changes/destruction. Preparation errors install nothing. Later
// targets may partly succeed; the first failing event in report order is returned. retained is
// this operation's target population, not the complete RuleSet ownership count.
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_refresh(yukiplt_c_rules*,
                                                    yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_release_image(
    yukiplt_c_rules*, const yukiplt_c_image*, yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
// Notifications accept only stable custom instances with nonzero tokens. Call
// after relocation/final protections, or before unmapping, outside loader locks.
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_notify_loaded(yukiplt_c_rules*, const yukiplt_c_image*,
                                                          const char* path, yukiplt_c_rule_report*)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_notify_unloading(
    yukiplt_c_rules*, const yukiplt_c_image*, yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_remove(yukiplt_c_rules*, uint64_t rule_id,
                                                   yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_unhook(yukiplt_c_rules*,
                                                   yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_abandon(yukiplt_c_rules*,
                                                    yukiplt_c_rule_report*) YUKIPLT_C_NOEXCEPT;
// Removes only released/invalidated history without handles, never abandoned
// records or original capacity. Existing copied reports remain intact, but their
// binding indices no longer identify the current mutable RuleSet vector.
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_prune_history(yukiplt_c_rules*,
                                                          size_t* removed) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_get_statistics(
    const yukiplt_c_rules*, yukiplt_c_rules_statistics*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_get_rule_statistics(
    const yukiplt_c_rules*, uint64_t rule_id, yukiplt_c_rule_statistics*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_rules_snapshot_bindings(const yukiplt_c_rules*, yukiplt_c_bindings*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_bindings_destroy(yukiplt_c_bindings*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_bindings_copy(const yukiplt_c_bindings*,
                                                    yukiplt_c_binding_record*, size_t capacity,
                                                    char* text, size_t text_capacity,
                                                    size_t* required_count,
                                                    size_t* required_text) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rule_report_destroy(yukiplt_c_rule_report*)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rule_report_get_info(
    const yukiplt_c_rule_report*, yukiplt_c_rule_report_info*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rule_report_copy(const yukiplt_c_rule_report*,
                                                       yukiplt_c_rule_event*, size_t capacity,
                                                       char* text, size_t text_capacity,
                                                       size_t* required_count,
                                                       size_t* required_text) YUKIPLT_C_NOEXCEPT;

// Candidate expansion is read-only. Explicit images only; no global match-all.
typedef struct yukiplt_c_candidates {
  uint64_t token;
} yukiplt_c_candidates;
typedef struct yukiplt_c_candidate_image {
  uint32_t struct_size;
  yukiplt_c_image image;
  const char* path;  // Exact system path, or caller-supplied custom instance path.
} yukiplt_c_candidate_image;
enum {
  YUKIPLT_C_TEXT_ANY = 0,
  YUKIPLT_C_TEXT_EXACT = 1,
  YUKIPLT_C_TEXT_PREFIX = 2,
  YUKIPLT_C_TEXT_SUFFIX = 3,
  YUKIPLT_C_TEXT_SUBSTRING = 4
};
typedef struct yukiplt_c_text_match {
  uint32_t kind;
  const char* text;
  size_t length;
} yukiplt_c_text_match;
typedef struct yukiplt_c_candidate_pattern {
  yukiplt_c_text_match path;
  yukiplt_c_text_match symbol;
} yukiplt_c_candidate_pattern;
typedef struct yukiplt_c_candidate_view {
  yukiplt_c_image image;
  const char* path;
  yukiplt_c_function_selector selector;
  uintptr_t slot_address;
  uint32_t symbol_index;
  uint32_t version_index;
  uint32_t version_table_present;
  uint32_t version_source;
  uint32_t relocation_type;
  uint32_t symbol_type;
} yukiplt_c_candidate_view;
// All pointers in view are borrowed for this invocation only. The callback runs
// after owning snapshots exist and outside linker/op/object-table locks; it may
// call other APIs. On success set accept to 0 or 1. Error or invalid accept fails
// the complete expansion, with no partial candidate list. Callback side effects
// are not rolled back. No exceptions, longjmp or Rust panic may cross this ABI.
typedef yukiplt_c_error (*yukiplt_c_candidate_predicate)(const yukiplt_c_candidate_view*,
                                                         void* context, uint32_t* accept);
typedef struct yukiplt_c_candidate_selection {
  uint32_t struct_size;
  uint32_t sources;
  uint32_t types;
  yukiplt_c_candidate_pattern include;
  const yukiplt_c_candidate_pattern* exclude;
  size_t exclude_count;
  yukiplt_c_candidate_predicate predicate;
  void* context;
  size_t text_bytes;  // At most 8 MiB; counted with the existing candidate policy.
  size_t candidates;  // At most 4096. Both limits may only be lowered.
} yukiplt_c_candidate_selection;
typedef struct yukiplt_c_candidate_record {
  yukiplt_c_image image;
  yukiplt_c_text_ref path;
  yukiplt_c_import_record selected;
} yukiplt_c_candidate_record;
// Inputs are borrowed for the call; text matches use explicit byte lengths, not
// NUL termination. Any requires length zero. A zero-length null text is valid.
// diagnostic is optional, exact struct_size on input, written on every valid-
// layout return. No candidate pins an image or authorizes installation without
// revalidation. Build an explicit function plan from the selected metadata.
YUKIPLT_API yukiplt_c_error yukiplt_c_expand_candidates(
    const yukiplt_c_candidate_image*, size_t count, const yukiplt_c_candidate_selection*,
    yukiplt_c_candidates*, yukiplt_c_operation_diagnostic*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_candidates_destroy(yukiplt_c_candidates*) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_candidates_copy(const yukiplt_c_candidates*,
                                                      yukiplt_c_candidate_record*, size_t capacity,
                                                      char* text, size_t text_capacity,
                                                      size_t* required_count,
                                                      size_t* required_text) YUKIPLT_C_NOEXCEPT;

YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output) YUKIPLT_C_NOEXCEPT;
// Diagnostic variants also report cache, permission and fork protocol causes.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_diagnosed_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
// Name-only selectors (YUKIPLT_C_SELECTOR) with nonzero original values required.
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_batch_nonnull_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_request* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_image* image,
    const yukiplt_c_function_request_ex* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_hook_functions_diagnosed_named_cached(
    const yukiplt_c_maps_cache* cache, const yukiplt_c_named_image* image,
    const yukiplt_c_function_request_ex* requests, size_t count, yukiplt_c_handle* handle,
    yukiplt_c_slot_report* output, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_unhook_diagnosed_cached(
    const yukiplt_c_maps_cache* cache, yukiplt_c_handle* handle, yukiplt_c_slot_report* output,
    size_t capacity, yukiplt_c_operation_diagnostic* diagnostic) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_query_cached(const yukiplt_c_maps_cache* cache,
                                                   const yukiplt_c_handle* handle,
                                                   yukiplt_c_slot_report* output, size_t capacity,
                                                   size_t* total) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_maps_cache_create(yukiplt_c_maps_cache* out)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_maps_cache_capture(yukiplt_c_maps_cache* cache)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_maps_cache_import_text(yukiplt_c_maps_cache* cache,
                                                             const char* text,
                                                             size_t length) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_maps_cache_invalidate(yukiplt_c_maps_cache* cache)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_maps_cache_destroy(yukiplt_c_maps_cache* cache)
    YUKIPLT_C_NOEXCEPT;
enum { YUKIPLT_C_FORK_NATIVE = 1, YUKIPLT_C_FORK_HOST_DISPATCHER = 2 };
enum { YUKIPLT_C_FORK_READY = 0, YUKIPLT_C_FORK_DRAINING = 1, YUKIPLT_C_FORK_REJECTED = 2 };
enum {
  YUKIPLT_C_FORK_REASON_NONE = 0,
  YUKIPLT_C_FORK_REASON_TIMEOUT = 1,
  YUKIPLT_C_FORK_REASON_RECOVERY = 2,
  YUKIPLT_C_FORK_REASON_REENTRY = 3,
  YUKIPLT_C_FORK_REASON_PROTOCOL = 4
};
typedef struct yukiplt_c_fork_callbacks {
  void (*prepare)(void*);
  void (*parent)(void*);
  void (*child)(void*);
  void* context;
} yukiplt_c_fork_callbacks;
// Dispatcher calls return zero or an errno. Success copies the callback table.
// Unsubscribe success excludes future callbacks and drains whole fork cycles.
typedef struct yukiplt_c_fork_config {
  uint32_t struct_size;
  uint32_t mode;
  uint32_t timeout_ms;  // Zero selects 5000 ms.
  void* context;
  int (*subscribe)(void*, const yukiplt_c_fork_callbacks*, uint64_t*);
  int (*unsubscribe_and_drain)(void*, uint64_t);
} yukiplt_c_fork_config;
typedef struct yukiplt_c_fork_status {
  uint32_t struct_size;
  uint32_t mode;  // Zero when disabled.
  uint32_t phase;
  uint32_t reason;
  uint32_t pid;
  uint32_t generation;
} yukiplt_c_fork_status;
YUKIPLT_API yukiplt_c_error yukiplt_c_enable_atfork(const yukiplt_c_fork_config*)
    YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_disable_atfork(void) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_get_fork_status(yukiplt_c_fork_status*) YUKIPLT_C_NOEXCEPT;

YUKIPLT_API yukiplt_c_error yukiplt_c_prepare_fork(void) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_after_fork_parent(void) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_after_fork_child(void) YUKIPLT_C_NOEXCEPT;

YUKIPLT_API yukiplt_c_error
yukiplt_c_session_install_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_session* h,
                                 yukiplt_c_summary* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_session_unhook_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_session* h,
                                yukiplt_c_summary* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_rules_refresh_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h,
                               yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_rules_unhook_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h,
                              yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_release_image_cached(
    const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h, const yukiplt_c_image* v,
    yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_notify_loaded_cached(
    const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h, const yukiplt_c_image* v,
    const char* path, yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error yukiplt_c_rules_notify_unloading_cached(
    const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h, const yukiplt_c_image* v,
    yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
YUKIPLT_API yukiplt_c_error
yukiplt_c_rules_remove_cached(const yukiplt_c_maps_cache* cache, yukiplt_c_rules* h, uint64_t id,
                              yukiplt_c_rule_report* out) YUKIPLT_C_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef YUKIPLT_C_NOEXCEPT

#endif  // YUKIPLT_H_
