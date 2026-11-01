// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#include "common.hpp"
#include "imports.hpp"
#include "maps_cache.hpp"
#include "system_images.hpp"
#include "yukiplt.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#include <sys/auxv.h>
#endif

#include "elf.hpp"

namespace yukiplt {
namespace {
constexpr size_t kGroupLimit = 64;

bool same_image(const ImageView& a, const ImageView& b) noexcept {
  return a.load_bias == b.load_bias && a.phdr_table == b.phdr_table &&
         a.phdr_count == b.phdr_count && a.instance_token == b.instance_token;
}

ImportSelector exact_selector(const VersionedImport& chosen) noexcept {
  ImportSelector result;
  result.symbol = chosen.import.symbol.c_str();
  result.version_match =
      chosen.version.index >= 2 ? VersionMatch::kExact : VersionMatch::kUnversioned;
  result.version = chosen.version.index >= 2 ? chosen.version.name.c_str() : nullptr;
  result.dependency =
      chosen.version.dependency.empty() ? nullptr : chosen.version.dependency.c_str();
  result.hidden = chosen.version.hidden ? HiddenMatch::kHidden : HiddenMatch::kNonHidden;
  result.relocation_index = chosen.import.relocation_index;
  return result;
}
#if defined(__linux__) || defined(__ANDROID__)
// Older x86 kernels link vDSO at a high virtual address, then map it lower.
// dlpi_addr represents a negative displacement modulo ElfW(Addr). Anchor this
// kernel-owned image with AT_SYSINFO_EHDR, without allowing arbitrary wrapped
// arithmetic in custom images or in transaction paths.
Result<bool> system_contains_address(const ImageView& view, uintptr_t address) {
  auto ordinary = detail::image_contains_address(view, address);
  if (ordinary || ordinary.error().code != ErrorCode::kIntegerOverflow) return ordinary;
#if defined(AT_SYSINFO_EHDR)
  const uintptr_t base = getauxval(AT_SYSINFO_EHDR);
  if (base == 0) return ordinary;
  // The auxiliary vector supplies a readable, process-lifetime ELF header.
  // Offsets inside that header remain untrusted.
  NativeElfEhdr header;
  std::memcpy(&header, reinterpret_cast<const void*>(base), sizeof(header));
  uintptr_t phdr_address;
  if (std::memcmp(header.e_ident, "\177ELF", 4) != 0 ||
      header.e_ident[EI_CLASS] != YUKIPLT_ELF_CLASS || header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_machine != kNativeMachine || header.e_type != ET_DYN ||
      header.e_phentsize != sizeof(NativeElfPhdr) || header.e_phnum != view.phdr_count ||
      !detail::safe_add(base, header.e_phoff, phdr_address) ||
      phdr_address != reinterpret_cast<uintptr_t>(view.phdr_table))
    return ordinary;
  const NativeElfPhdr* anchor = nullptr;
  for (size_t i = 0; i < view.phdr_count; ++i) {
    const auto& ph = view.phdr_table[i];
    if (ph.p_type == PT_LOAD && ph.p_offset == 0 && (ph.p_flags & PF_R) &&
        ph.p_filesz >= sizeof(NativeElfEhdr)) {
      if (anchor) return Error::Make(ErrorCode::kCorruptedElf);
      anchor = &ph;
    }
  }
  if (!anchor || static_cast<NativeElfAddr>(base - anchor->p_vaddr) != view.load_bias)
    return ordinary;
  bool found = false;
  for (size_t i = 0; i < view.phdr_count; ++i) {
    const auto& ph = view.phdr_table[i];
    if (ph.p_type != PT_LOAD) continue;
    if (ph.p_filesz > ph.p_memsz) return Error::Make(ErrorCode::kCorruptedElf);
    uintptr_t virtual_end;
    if (!detail::safe_add(ph.p_vaddr, ph.p_memsz, virtual_end))
      return Error::Make(ErrorCode::kIntegerOverflow);
    uintptr_t begin;
    if (ph.p_vaddr >= anchor->p_vaddr) {
      if (!detail::safe_add(base, ph.p_vaddr - anchor->p_vaddr, begin))
        return Error::Make(ErrorCode::kIntegerOverflow);
    } else {
      const uintptr_t delta = anchor->p_vaddr - ph.p_vaddr;
      if (delta > base) return Error::Make(ErrorCode::kIntegerOverflow);
      begin = base - delta;
    }
    uintptr_t end;
    if (!detail::safe_add(begin, ph.p_memsz, end)) return Error::Make(ErrorCode::kIntegerOverflow);
    if (address >= begin && address < end) found = true;
  }
  return found;
#else
  return ordinary;
#endif
}
#endif
}  // namespace

Result<std::vector<NamedImage>> find_images(const ImageSelector& selector) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (Error invalid = detail::validate_image_selector(selector)) return invalid;
#if defined(__linux__) || defined(__ANDROID__)
  struct Context {
    const ImageSelector& selector;
    std::vector<NamedImage> images;
    uintptr_t main_phdr;
    Error error;
  };
  try {
    Context ctx{selector, {}, getauxval(AT_PHDR), {}};
    detail::iterate_system_phdr(
        [](dl_phdr_info* info, size_t, void* opaque) -> int {
          auto& c = *static_cast<Context*>(opaque);
          const char* path = info->dlpi_name == nullptr ? "" : info->dlpi_name;
          bool match = false;
          switch (c.selector.match) {
            case ImageMatch::kMainExecutable:
              match = reinterpret_cast<uintptr_t>(info->dlpi_phdr) == c.main_phdr;
              break;
            case ImageMatch::kLoadBias:
              match = info->dlpi_addr == c.selector.load_bias;
              break;
            case ImageMatch::kExactPath:
              match = std::strcmp(path, c.selector.name) == 0;
              break;
            case ImageMatch::kBasename: {
              const char* slash = std::strrchr(path, '/');
              match = std::strcmp(slash ? slash + 1 : path, c.selector.name) == 0;
              break;
            }
          }
          if (!match) return 0;
          try {
            c.images.push_back(
                {{static_cast<uintptr_t>(info->dlpi_addr), info->dlpi_phdr, info->dlpi_phnum, 0},
                 path});
          } catch (...) {
            c.error = detail::current_exception_error();
            return 1;
          }
          return 0;
        },
        &ctx);
    if (ctx.error) return ctx.error;
    if (ctx.images.empty()) return Error::Make(ErrorCode::kImageNotFound);
    return std::move(ctx.images);
  } catch (...) {
    return detail::current_exception_error();
  }
#else
  return Error::Make(ErrorCode::kUnsupportedFormat);
#endif
}

Result<NamedImage> find_image_containing(const void* address) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  uintptr_t query_address = reinterpret_cast<uintptr_t>(address);
#if defined(__ANDROID__) && defined(__aarch64__)
  // Android ARM64 data pointers can carry a TBI/HWASan tag. Compare the virtual
  // address to ELF intervals without dereferencing or validating that tag.
  query_address &= UINT64_C(0x00ffffffffffffff);
#endif
  if (query_address == 0) return Error::Make(ErrorCode::kInvalidArgument);
#if defined(__linux__) || defined(__ANDROID__)
  struct Context {
    uintptr_t address;
    size_t matches = 0;
    NamedImage found;
    Error error;
  };
  Context ctx{query_address, 0, {}, {}};
  detail::iterate_system_phdr(
      [](dl_phdr_info* info, size_t, void* opaque) -> int {
        auto& c = *static_cast<Context*>(opaque);
        try {
          ImageView view{static_cast<uintptr_t>(info->dlpi_addr), info->dlpi_phdr, info->dlpi_phnum,
                         0};
          auto contains = system_contains_address(view, c.address);
          if (!contains) {
            c.error = contains.error();
            return 1;
          }
          if (!contains.value()) return 0;
          ++c.matches;
          if (c.matches != 1) return 0;
          c.found = {view, info->dlpi_name ? info->dlpi_name : ""};
        } catch (...) {
          c.error = detail::current_exception_error();
          return 1;
        }
        return 0;
      },
      &ctx);
  if (ctx.error) return ctx.error;
  if (ctx.matches == 0) return Error::Make(ErrorCode::kImageNotFound);
  if (ctx.matches != 1) return Error::Make(ErrorCode::kAmbiguousImage);
  return std::move(ctx.found);
#else
  return Error::Make(ErrorCode::kUnsupportedFormat);
#endif
}

}  // namespace yukiplt

namespace yukiplt::detail {
Result<bool> image_contains_address(const ImageView& image, uintptr_t address) {
  // Some Android enumerators include an empty bookkeeping entry. It has no
  // LOAD intervals and cannot contain an address; keep enumerating real images.
  if (image.phdr_count == 0) return false;
  if (!image.phdr_table) return Error::Make(ErrorCode::kInvalidArgument);
  bool found = false;
  for (size_t i = 0; i < image.phdr_count; ++i) {
    const auto& ph = image.phdr_table[i];
    if (ph.p_type != PT_LOAD) continue;
    if (ph.p_filesz > ph.p_memsz) return Error::Make(ErrorCode::kCorruptedElf);
    const uintptr_t max = std::numeric_limits<uintptr_t>::max();
    if (ph.p_vaddr > max - image.load_bias) return Error::Make(ErrorCode::kIntegerOverflow);
    const uintptr_t begin = image.load_bias + static_cast<uintptr_t>(ph.p_vaddr);
    if (ph.p_memsz > max - begin) return Error::Make(ErrorCode::kIntegerOverflow);
    if (address >= begin && address - begin < ph.p_memsz) found = true;
  }
  return found;
}

Error validate_image_selector(const ImageSelector& selector) noexcept {
  if (selector.match == ImageMatch::kExactPath || selector.match == ImageMatch::kBasename) {
    if (selector.name == nullptr || *selector.name == '\0' ||
        ((std::strchr(selector.name, '/') != nullptr) !=
         (selector.match == ImageMatch::kExactPath)))
      return Error::Make(ErrorCode::kInvalidArgument);
  } else if (selector.match != ImageMatch::kMainExecutable &&
             selector.match != ImageMatch::kLoadBias) {
    return Error::Make(ErrorCode::kInvalidArgument);
  }
  return Error::Ok();
}

}  // namespace yukiplt::detail

namespace yukiplt {

Result<HookPlan> build_hook_plan(const HookTarget* targets, size_t count, size_t* failed_target) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (failed_target) *failed_target = ~size_t{0};
  if (targets == nullptr || count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    HookPlan plan;
    plan.targets_.reserve(count);
    struct CachedSnapshot {
      ImageView image;
      VersionedImportSnapshot snapshot;
    };
    std::vector<CachedSnapshot> snapshots;
    for (size_t i = 0; i < count; ++i) {
      if (failed_target) *failed_target = i;
      const auto& target = targets[i];
      auto invalid = detail::validate_selector(target.import);
      if (invalid) return invalid;
      if (!target.replacement ||
          reinterpret_cast<uintptr_t>(target.out_original) % alignof(void*) != 0)
        return Error::Make(ErrorCode::kInvalidArgument);
      size_t group_size = 1;
      for (size_t j = 0; j < i; ++j) {
        if (target.out_original && target.out_original == targets[j].out_original)
          return Error::Make(ErrorCode::kInvalidArgument);
        if (same_image(target.image, targets[j].image)) ++group_size;
      }
      if (group_size > kGroupLimit) return Error::Make(ErrorCode::kInvalidArgument);
      if (count == 1) {
        auto found = find_import(target.image, target.import);
        if (!found) return found.error();
        plan.targets_.push_back(
            {target.image, std::move(found.value()), target.replacement, target.out_original});
        continue;
      }
      size_t cached = 0;
      for (; cached < snapshots.size(); ++cached)
        if (same_image(snapshots[cached].image, target.image)) break;
      if (cached == snapshots.size()) {
        auto snapshot = inspect_versioned_imports(target.image);
        if (!snapshot) return snapshot.error();
        snapshots.push_back({target.image, std::move(snapshot.value())});
      }
      const auto& snapshot = snapshots[cached].snapshot;
      auto selected = detail::select_import(snapshot, target.import);
      if (!selected) return selected.error();
      const auto& found = snapshot.imports[selected.value()];
      for (const auto& previous : plan.targets_) {
        if (previous.selected.import.slot_address == found.import.slot_address)
          return Error::Make(ErrorCode::kInvalidArgument);
      }
      plan.targets_.push_back({target.image, found, target.replacement, target.out_original});
    }
    for (size_t i = 0; i < plan.targets_.size(); ++i) {
      const auto output = reinterpret_cast<uintptr_t>(plan.targets_[i].out_original);
      if (!output) continue;
      for (const auto& target : plan.targets_)
        if (output == target.selected.import.slot_address) {
          if (failed_target) *failed_target = i;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
    }
    plan.paths_.reserve(plan.targets_.size());
    for (size_t i = 0; i < plan.targets_.size(); ++i) {
      size_t previous = 0;
      while (previous < i && !same_image(plan.targets_[previous].image, plan.targets_[i].image))
        ++previous;
      if (previous < i) {
        plan.paths_.push_back(plan.paths_[previous]);
        continue;
      }
      if (failed_target) *failed_target = i;
      auto path = detail::system_image_path(plan.targets_[i].image);
      if (!path) return path.error();
      plan.paths_.push_back(std::move(path.value()));
    }
    if (failed_target) *failed_target = ~size_t{0};
    return plan;
  } catch (...) {
    return detail::current_exception_error();
  }
}

Result<HookSession> prepare_session(const HookPlan& plan) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (plan.targets().empty()) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    HookSession session;
    session.targets_.reserve(plan.targets().size());
    for (const auto& target : plan.targets()) {
      size_t group = 0;
      for (; group < session.images_.size(); ++group) {
        if (same_image(session.images_[group].image, target.image)) break;
      }
      if (group == session.images_.size()) {
        SessionImageResult result;
        result.image = target.image;
        session.images_.push_back(std::move(result));
        const size_t index = session.targets_.size();
        session.paths_.push_back(index < plan.paths_.size() ? plan.paths_[index] : std::string());
      }
      session.images_[group].target_indices.push_back(session.targets_.size());
      session.targets_.push_back({target, nullptr, {}, {}});
    }
    return session;
  } catch (...) {
    return detail::current_exception_error();
  }
}

SessionSummary HookSession::install_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (size_t g = 0; g < images_.size(); ++g) {
    auto& group = images_[g];
    if (group.state == SessionState::kIndeterminate ||
        group.state == SessionState::kRestoreFailed) {
      ++summary.failed;
    } else if (group.state == SessionState::kPrepared ||
               group.state == SessionState::kInstallFailed) {
      ++summary.attempted;
      SelectedHookRequest requests[kGroupLimit];
      SlotReport reports[kGroupLimit];
      size_t n = 0;
      for (size_t index : group.target_indices) {
        auto& target = targets_[index].target;
        requests[n++] = {exact_selector(target.selected), target.replacement, target.out_original};
      }
      // A system image must still carry the path it had when planned.
      const char* path =
          group.image.instance_token == 0 && g < paths_.size() ? paths_[g].c_str() : nullptr;
      auto result = detail::hook_imports_at(group.image, path, requests, n, reports, cache);
      group.install_error = result.error;
      group.handle = std::move(result.handle);
      group.state = result.ok()            ? SessionState::kInstalled
                    : group.handle.valid() ? SessionState::kIndeterminate
                    : result.error.code == ErrorCode::kImageInvalidated
                        ? SessionState::kInvalidated
                        : SessionState::kInstallFailed;
      if (result.ok())
        ++summary.succeeded;
      else
        ++summary.failed;
      for (size_t i = 0; i < n; ++i) {
        auto& target = targets_[group.target_indices[i]];
        target.install_report = reports[i];
        if (reports[i].address != 0)
          target.original = reinterpret_cast<void*>(reports[i].value_before);
      }
    }
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}
SessionSummary HookSession::install() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return install_impl(nullptr);
}
SessionSummary HookSession::install_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return install_impl(&cache);
}

SessionSummary HookSession::unhook_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (auto it = images_.rbegin(); it != images_.rend(); ++it) {
    auto& group = *it;
    if (!group.handle.valid()) continue;
    ++summary.attempted;
    SlotReport reports[kGroupLimit];
    group.restore_error =
        cache ? yukiplt::unhook_cached(*cache, group.handle, reports, group.target_indices.size())
              : yukiplt::unhook(group.handle, reports, group.target_indices.size());
    if (!group.restore_error) {
      group.state = SessionState::kReleased;
      ++summary.succeeded;
    } else {
      group.state = group.restore_error.code == ErrorCode::kImageInvalidated
                        ? SessionState::kInvalidated
                        : SessionState::kRestoreFailed;
      ++summary.failed;
    }
    for (size_t i = 0; i < group.target_indices.size(); ++i)
      targets_[group.target_indices[i]].restore_report = reports[i];
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}
SessionSummary HookSession::unhook() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(nullptr);
}
SessionSummary HookSession::unhook_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(&cache);
}

SessionSummary HookSession::abandon() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (auto& group : images_) {
    if (!group.handle.valid()) continue;
    ++summary.attempted;
    group.restore_error = yukiplt::abandon(group.handle);
    if (!group.restore_error) {
      group.state = SessionState::kAbandoned;
      ++summary.succeeded;
    } else
      ++summary.failed;
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}

FunctionImportSelector exact_function_selector(const FunctionImport& chosen) noexcept {
  FunctionImportSelector result;
  result.base.symbol = chosen.base.import.symbol.c_str();
  result.base.version_match =
      chosen.base.version.index >= 2 ? VersionMatch::kExact : VersionMatch::kUnversioned;
  result.base.version = chosen.base.version.index >= 2 ? chosen.base.version.name.c_str() : nullptr;
  result.base.dependency =
      chosen.base.version.dependency.empty() ? nullptr : chosen.base.version.dependency.c_str();
  result.base.hidden = chosen.base.version.hidden ? HiddenMatch::kHidden : HiddenMatch::kNonHidden;
  result.base.relocation_index = chosen.base.import.relocation_index;
  switch (chosen.source) {
    case RelocationSource::kJmprel:
      result.sources = kSourceJmprel;
      break;
    case RelocationSource::kRela:
      result.sources = kSourceRela;
      break;
    case RelocationSource::kAndroidRela:
      result.sources = kSourceAndroidRela;
      break;
    case RelocationSource::kRel:
      result.sources = kSourceRel;
      break;
    case RelocationSource::kAndroidRel:
      result.sources = kSourceAndroidRel;
      break;
  }
  result.index_source = chosen.source;
  result.types = chosen.relocation_type == detail::kRelJumpSlot  ? kTypeJumpSlot
                 : chosen.relocation_type == detail::kRelGlobDat ? kTypeGlobDat
                                                                 : 0;
  return result;
}
Result<FunctionHookPlan> build_function_hook_plan(const FunctionHookTarget* targets, size_t count,
                                                  size_t* failed_target) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (failed_target) *failed_target = ~size_t{0};
  if (!targets || count == 0) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    struct Cached {
      ImageView image;
      uint32_t sources = 0;
      size_t count = 0;
      bool loaded = false;
      detail::RawFunctionSnapshot snapshot;
    };
    std::vector<Cached> cache;
    FunctionHookPlan plan;
    plan.targets_.reserve(count);
    // Validate requests before scanning each group's union of explicit sources.
    for (size_t i = 0; i < count; ++i) {
      if (failed_target) *failed_target = i;
      const auto& request = targets[i].request;
      if (Error e = detail::validate_function_selector(request.target)) return e;
      if (!request.replacement ||
          reinterpret_cast<uintptr_t>(request.out_original) % alignof(void*) != 0)
        return Error::Make(ErrorCode::kInvalidArgument);
      for (size_t j = 0; j < i; ++j)
        if (request.out_original && request.out_original == targets[j].request.out_original)
          return Error::Make(ErrorCode::kInvalidArgument);
      size_t group = 0;
      for (; group < cache.size(); ++group)
        if (same_image(cache[group].image, targets[i].image)) break;
      if (group == cache.size()) cache.push_back({targets[i].image, 0, 0, false, {}});
      if (++cache[group].count > kGroupLimit) return Error::Make(ErrorCode::kInvalidArgument);
      cache[group].sources |= request.target.sources;
    }
    for (size_t i = 0; i < count; ++i) {
      if (failed_target) *failed_target = i;
      const auto& target = targets[i];
      const auto& request = target.request;
      size_t group = 0;
      while (!same_image(cache[group].image, target.image)) ++group;
      auto& cached = cache[group];
      if (!cached.loaded) {
        auto snapshot = detail::inspect_function_relocs(cached.image, cached.sources);
        if (!snapshot) return snapshot.error();
        cached.snapshot = std::move(snapshot.value());
        cached.loaded = true;
      }
      auto selected = detail::select_function_reloc(cached.snapshot, request.target);
      if (!selected) return selected.error();
      const auto& found = cached.snapshot.records[selected.value()].entry;
      for (const auto& prior : plan.targets_)
        if (prior.selected.base.import.slot_address == found.base.import.slot_address)
          return Error::Make(ErrorCode::kInvalidArgument);
      plan.targets_.push_back({target.image, found, request.replacement, request.out_original,
                               request.require_current, request.expected_current});
    }
    for (size_t i = 0; i < plan.targets_.size(); ++i) {
      const auto output = reinterpret_cast<uintptr_t>(plan.targets_[i].out_original);
      if (!output) continue;
      for (const auto& target : plan.targets_)
        if (output == target.selected.base.import.slot_address) {
          if (failed_target) *failed_target = i;
          return Error::Make(ErrorCode::kInvalidArgument);
        }
    }
    plan.paths_.reserve(plan.targets_.size());
    for (size_t i = 0; i < plan.targets_.size(); ++i) {
      size_t previous = 0;
      while (previous < i && !same_image(plan.targets_[previous].image, plan.targets_[i].image))
        ++previous;
      if (previous < i) {
        plan.paths_.push_back(plan.paths_[previous]);
        continue;
      }
      if (failed_target) *failed_target = i;
      auto path = detail::system_image_path(plan.targets_[i].image);
      if (!path) return path.error();
      plan.paths_.push_back(std::move(path.value()));
    }
    if (failed_target) *failed_target = ~size_t{0};
    return plan;
  } catch (...) {
    return detail::current_exception_error();
  }
}
Result<FunctionHookSession> prepare_function_session(const FunctionHookPlan& plan) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return admission.error();

  if (Error e = detail::process_entry()) return e;
  if (plan.targets().empty()) return Error::Make(ErrorCode::kInvalidArgument);
  try {
    FunctionHookSession session;
    session.targets_.reserve(plan.targets().size());
    for (const auto& target : plan.targets()) {
      size_t group = 0;
      for (; group < session.images_.size(); ++group) {
        if (same_image(session.images_[group].image, target.image)) break;
      }
      if (group == session.images_.size()) {
        FunctionSessionImageResult result;
        result.image = target.image;
        session.images_.push_back(std::move(result));
        const size_t index = session.targets_.size();
        session.paths_.push_back(index < plan.paths_.size() ? plan.paths_[index] : std::string());
      }
      session.images_[group].target_indices.push_back(session.targets_.size());
      session.targets_.push_back({target, nullptr, {}, {}});
    }
    return session;
  } catch (...) {
    return detail::current_exception_error();
  }
}

SessionSummary FunctionHookSession::install_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (size_t g = 0; g < images_.size(); ++g) {
    auto& group = images_[g];
    if (group.state == SessionState::kIndeterminate ||
        group.state == SessionState::kRestoreFailed) {
      ++summary.failed;
    } else if (group.state == SessionState::kPrepared ||
               group.state == SessionState::kInstallFailed) {
      ++summary.attempted;
      FunctionHookRequestEx requests[kGroupLimit];
      SlotReport reports[kGroupLimit];
      size_t n = 0;
      for (size_t index : group.target_indices) {
        auto& target = targets_[index].target;
        requests[n++] = {exact_function_selector(target.selected), target.replacement,
                         target.out_original, target.require_current, target.expected_current};
      }
      // A system image must still carry the path it had when planned.
      const char* path =
          group.image.instance_token == 0 && g < paths_.size() ? paths_[g].c_str() : nullptr;
      auto result = detail::hook_function_imports_ex_at(group.image, path, requests, n, reports,
                                                        &group.operation, cache);
      group.install_error = result.error;
      group.handle = std::move(result.handle);
      group.state = result.ok()            ? SessionState::kInstalled
                    : group.handle.valid() ? SessionState::kIndeterminate
                    : result.error.code == ErrorCode::kImageInvalidated
                        ? SessionState::kInvalidated
                        : SessionState::kInstallFailed;
      if (result.ok())
        ++summary.succeeded;
      else
        ++summary.failed;
      for (size_t i = 0; i < n; ++i) {
        auto& target = targets_[group.target_indices[i]];
        target.install_report = reports[i];
        if (reports[i].address != 0)
          target.original = reinterpret_cast<void*>(reports[i].value_before);
      }
    }
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}
SessionSummary FunctionHookSession::install() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return install_impl(nullptr);
}
SessionSummary FunctionHookSession::install_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  return install_impl(&cache);
}

SessionSummary FunctionHookSession::unhook_impl(MapsCache* cache) {
  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (auto it = images_.rbegin(); it != images_.rend(); ++it) {
    auto& group = *it;
    if (!group.handle.valid()) continue;
    ++summary.attempted;
    SlotReport reports[kGroupLimit];
    group.restore_error =
        cache ? yukiplt::unhook_cached(*cache, group.handle, reports, group.target_indices.size())
              : yukiplt::unhook(group.handle, reports, group.target_indices.size());
    group.operation = {};
    group.operation.error = group.restore_error;
    group.operation.stage =
        group.restore_error ? OperationStage::kPrepare : OperationStage::kComplete;
    group.operation.image_invalidated = group.restore_error.code == ErrorCode::kImageInvalidated;
    if (group.handle.valid()) {
      if (Error e = query_recovery(group.handle, &group.operation)) group.operation.error = e;
    }
    if (!group.restore_error) {
      group.state = SessionState::kReleased;
      ++summary.succeeded;
    } else {
      group.state = group.restore_error.code == ErrorCode::kImageInvalidated
                        ? SessionState::kInvalidated
                        : SessionState::kRestoreFailed;
      ++summary.failed;
    }
    for (size_t i = 0; i < group.target_indices.size(); ++i)
      targets_[group.target_indices[i]].restore_report = reports[i];
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}
SessionSummary FunctionHookSession::unhook() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(nullptr);
}
SessionSummary FunctionHookSession::unhook_cached(MapsCache& cache) {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};
  return unhook_impl(&cache);
}

SessionSummary FunctionHookSession::abandon() {
  yukiplt::detail::OperationGuard admission;
  if (admission.error()) return {0, 0, 1, 0};

  if (detail::process_entry()) return {0, 0, 1, 0};
  SessionSummary summary;
  for (auto& group : images_) {
    if (!group.handle.valid()) continue;
    ++summary.attempted;
    RecoveryDiagnostic recovery;
    group.restore_error = query_recovery(group.handle, &recovery);
    if (!group.restore_error) {
      // The registry will be discarded, while residual slots/pages stay as
      // they are. Preserve their last known state independently of the result
      // of relinquishing ownership.
      group.operation = recovery;
      group.restore_error = yukiplt::abandon(group.handle);
    }
    if (!group.restore_error) {
      group.state = SessionState::kAbandoned;
      ++summary.succeeded;
    } else
      ++summary.failed;
    if (group.handle.valid()) ++summary.retained;
  }
  return summary;
}

}  // namespace yukiplt

namespace yukiplt::detail {
ImageEnumerator active_image_enumerator() noexcept { return &find_images; }
}  // namespace yukiplt::detail

namespace yukiplt {
SessionSummary HookSession::unhook_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook_cached(cache);
}
SessionSummary HookSession::install_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return install_cached(cache);
}
SessionSummary HookSession::abandon(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return abandon();
}
SessionSummary HookSession::unhook(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook();
}
SessionSummary HookSession::install(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return install();
}
SessionSummary FunctionHookSession::unhook_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook_cached(cache);
}
SessionSummary FunctionHookSession::install_cached(MapsCache& cache, Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return install_cached(cache);
}
SessionSummary FunctionHookSession::abandon(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return abandon();
}
SessionSummary FunctionHookSession::unhook(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return unhook();
}
SessionSummary FunctionHookSession::install(Error& admission_error) {
  detail::OperationGuard admission;
  admission_error = admission.error();
  if (admission_error) return {0, 0, 1, 0};
  return install();
}
}  // namespace yukiplt
