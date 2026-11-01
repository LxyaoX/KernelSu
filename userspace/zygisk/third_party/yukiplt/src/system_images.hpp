// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_SYSTEM_IMAGES_HPP_
#define YUKIPLT_SYSTEM_IMAGES_HPP_

#include "yukiplt.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#if defined(YUKIPLT_SYSTEM_PHDR_ITERATOR)
extern "C" int YUKIPLT_SYSTEM_PHDR_ITERATOR(int (*)(dl_phdr_info*, size_t, void*), void*);
#else
#define YUKIPLT_SYSTEM_PHDR_ITERATOR dl_iterate_phdr
#endif
#endif

namespace yukiplt::detail {
#if defined(__linux__) || defined(__ANDROID__)
inline int iterate_system_phdr(int (*callback)(dl_phdr_info*, size_t, void*), void* data) {
  return YUKIPLT_SYSTEM_PHDR_ITERATOR(callback, data);
}
#endif

// Caller keeps Program Headers readable; pure LOAD interval check, no holes.
Result<bool> image_contains_address(const ImageView&, uintptr_t address);

/// Enumerate images matching `selector`.  Same contract as find_images().
using ImageEnumerator = Result<std::vector<NamedImage>> (*)(const ImageSelector&);

/// Validate an image filter with find_images()' rules (kInvalidArgument on a
/// bad match kind, or a missing/ill-formed name for path/basename matching).
Error validate_image_selector(const ImageSelector& selector) noexcept;

/// The system-image enumerator used by RuleSet.
ImageEnumerator active_image_enumerator() noexcept;

// For system images, match expected_path when supplied and record the live
// path for later recovery/query. Custom images ignore it. Otherwise use the
// hook_function_imports contract.
HookResult hook_function_imports_at(const ImageView& image, const char* expected_path,
                                    const FunctionHookRequest* reqs, size_t count,
                                    SlotReport* reports, MapsCache* cache = nullptr);

/// hook_imports() / hook_function_imports_ex() for a planned system image whose
/// discovery path is known; same expected_path rule as above.
HookResult hook_imports_at(const ImageView& image, const char* expected_path,
                           const SelectedHookRequest* reqs, size_t count, SlotReport* reports,
                           MapsCache* cache = nullptr);
HookResult hook_function_imports_ex_at(const ImageView& image, const char* expected_path,
                                       const FunctionHookRequestEx* reqs, size_t count,
                                       SlotReport* reports, RecoveryDiagnostic* diagnostic,
                                       MapsCache* cache = nullptr);

/// The live path (dlpi_name, "" when absent) of the system image with exactly
/// this bias/phdr/phnum, read under the linker lock.  kImageInvalidated when no
/// such image is loaded.  Custom images (nonzero token) have no linker path and
/// return an empty string.
Result<std::string> system_image_path(const ImageView& image);

/// One RuleSet single-slot binding to revalidate. All pointers are borrowed for
/// this call. Both paths are required for a system image; an empty string is a
/// recorded name, never a request to omit validation. The result does not
/// consume a handle or change its Registry state.
struct InstalledBindingVerification {
  const HookHandle* handle = nullptr;
  ImageView image;
  const char* bound_path = nullptr;
  const char* enumerated_path = nullptr;
  Error error;
  SlotReport report;
};

struct BindingVerificationStatistics {
  size_t enumerations = 0;
  size_t maps_reads = 0;
};

// Prepare all grouping before enumeration; inside each linker callback take
// op_mutex and read maps once. Paths must agree. Errors preserve ownership;
// preparation failure forbids installs. Matching addresses/paths cannot exclude ABA.
Error verify_installed_bindings(InstalledBindingVerification* entries, size_t count,
                                BindingVerificationStatistics* statistics = nullptr,
                                MapsCache* cache = nullptr);

}  // namespace yukiplt::detail

#endif  // YUKIPLT_SYSTEM_IMAGES_HPP_
