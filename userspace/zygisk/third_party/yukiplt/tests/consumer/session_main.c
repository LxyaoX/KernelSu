// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yukiplt.h"

typedef int (*function)(int);
static void* originals[2];
static void* retained_library;
static int replacement0(int x) {
  return ((function)yukiplt_c_load_original(&originals[0]))(x) + 100;
}
static int replacement1(int x) {
  return ((function)yukiplt_c_load_original(&originals[1]))(x) + 100;
}

static int exercise(int functions) {
  void* library = dlopen("./libhook_target.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    puts(dlerror());
    return 1;
  }
  void* address = dlsym(library, "call_import");
  function call = (function)address;
  yukiplt_c_image image;
  if (!call || call(1) != 11 || yukiplt_c_image_containing(address, &image).code) return 2;
  yukiplt_c_plan plan = {0};
  yukiplt_c_session session = {0};
  yukiplt_c_error error;
  size_t failed = 0;
  if (!functions) {
    yukiplt_c_hook_target target = {
        sizeof(target),
        image,
        {YUKIPLT_C_SELECTOR("sample_add"), (void*)replacement0, &originals[0]}};
    error = yukiplt_c_build_plan(&target, 1, &plan, &failed);
  } else {
    yukiplt_c_function_target target = {0};
    target.struct_size = sizeof(target);
    target.image = image;
    target.request.struct_size = sizeof(target.request);
    yukiplt_c_function_selector selector = YUKIPLT_C_FUNCTION_SELECTOR("sample_add");
    selector.sources = YUKIPLT_C_SOURCE_JMPREL;
    selector.types = YUKIPLT_C_TYPE_JUMP_SLOT;
    target.request.base.target = selector;
    target.request.base.replacement = (void*)replacement1;
    target.request.base.out_original = &originals[1];
    target.request.require_current = 1;
    target.request.expected_current = (uintptr_t)yukiplt_c_load_original(&originals[0]);
    error = yukiplt_c_build_function_plan(&target, 1, &plan, &failed);
  }
  if (error.code || failed != SIZE_MAX) return 3;
  if (yukiplt_c_prepare_session(&plan, &session).code || yukiplt_c_plan_destroy(&plan).code)
    return 4;
  yukiplt_c_maps_cache cache = {0};
  if (yukiplt_c_maps_cache_create(&cache).code || yukiplt_c_maps_cache_capture(&cache).code)
    return 39;
  yukiplt_c_summary summary;
  if (yukiplt_c_session_install_cached(&cache, &session, &summary).code || summary.succeeded != 1)
    return 5;
  if (call(1) != 111 || yukiplt_c_session_destroy(&session).code != YUKIPLT_C_INVALID_ARGUMENT ||
      yukiplt_c_maps_cache_destroy(&cache).code != YUKIPLT_C_INVALID_ARGUMENT)
    return 6;
  if (yukiplt_c_session_unhook_cached(&cache, &session, &summary).code || summary.retained ||
      call(1) != 11)
    return 7;
  if (yukiplt_c_session_destroy(&session).code) return 8;
  if (yukiplt_c_maps_cache_destroy(&cache).code) return 40;
  // Keep provider/target loaded until both original snapshots are no longer used.
  if (!functions) {
    retained_library = library;
    return 0;
  }
  return dlclose(library) == 0 ? 0 : 9;
}

static int invalidation(void) {
  void* library = dlopen("./libhook_target.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) return 10;
  yukiplt_c_image image;
  if (yukiplt_c_image_containing(dlsym(library, "call_import"), &image).code) return 11;
  yukiplt_c_hook_target target = {
      sizeof(target), image, {YUKIPLT_C_SELECTOR("sample_add"), (void*)replacement0, NULL}};
  yukiplt_c_plan plan = {0};
  yukiplt_c_session session = {0};
  if (yukiplt_c_build_plan(&target, 1, &plan, NULL).code ||
      yukiplt_c_prepare_session(&plan, &session).code || yukiplt_c_plan_destroy(&plan).code)
    return 12;
  if (dlclose(library)) return 13;
  yukiplt_c_summary summary;
  if (yukiplt_c_session_install(&session, &summary).code != YUKIPLT_C_IMAGE_INVALIDATED) return 14;
  yukiplt_c_recovery_diagnostic diagnostic;
  size_t pages = 0;
  if (yukiplt_c_session_recovery(&session, 0, &diagnostic, NULL, 0, &pages).code ||
      diagnostic.error.code != YUKIPLT_C_IMAGE_INVALIDATED || !diagnostic.image_invalidated)
    return 15;
  return yukiplt_c_session_destroy(&session).code ? 16 : 0;
}

static void* candidate_original;
static void* nonnull_original;
static void* rule_originals[1];
static int candidate_replacement(int x) {
  return ((function)yukiplt_c_load_original(&candidate_original))(x) + 100;
}
static int nonnull_replacement(int x) {
  return ((function)yukiplt_c_load_original(&nonnull_original))(x) + 100;
}
static int rule_replacement(int x) {
  return ((function)yukiplt_c_load_original(&rule_originals[0]))(x) + 100;
}
static yukiplt_c_error accept_candidate(const yukiplt_c_candidate_view* view, void* context,
                                        uint32_t* accept) {
  yukiplt_c_rules temporary = {0};
  yukiplt_c_error error = yukiplt_c_rules_create(&temporary);
  if (error.code) return error;
  error = yukiplt_c_rules_destroy(&temporary);
  if (error.code) return error;
  ++*(unsigned*)context;
  *accept = strcmp(view->selector.base.symbol, "sample_add") == 0;
  return (yukiplt_c_error){0, 0};
}
static int rules_and_candidates(void) {
  void* library = dlopen("./libhook_target.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) return 20;
  void* address = dlsym(library, "call_import");
  function call = (function)address;
  yukiplt_c_image_list images = {0};
  yukiplt_c_image_record image_record;
  size_t count = 0, bytes = 0;
  if (yukiplt_c_find_image_containing(address, &images).code ||
      yukiplt_c_image_list_copy(&images, NULL, 0, NULL, 0, &count, &bytes).code || count != 1)
    return 21;
  char* path_text = (char*)malloc(bytes);
  if (!path_text ||
      yukiplt_c_image_list_copy(&images, &image_record, 1, path_text, bytes, &count, &bytes).code)
    return 22;
  if (yukiplt_c_image_list_destroy(&images).code) return 23;
  const char* path = path_text + image_record.path.offset;
  yukiplt_c_candidate_image input = {sizeof(input), image_record.image, path};
  yukiplt_c_candidate_selection selection = {0};
  selection.struct_size = sizeof(selection);
  selection.sources = YUKIPLT_C_SOURCE_JMPREL;
  selection.types = YUKIPLT_C_TYPE_JUMP_SLOT;
  selection.include.symbol =
      (yukiplt_c_text_match){YUKIPLT_C_TEXT_EXACT, "sample_add", sizeof("sample_add") - 1};
  selection.text_bytes = 8 * 1024 * 1024;
  selection.candidates = 4096;
  unsigned callbacks = 0;
  selection.predicate = accept_candidate;
  selection.context = &callbacks;
  yukiplt_c_candidates candidates = {0};
  if (yukiplt_c_expand_candidates(&input, 1, &selection, &candidates, NULL).code || callbacks != 1)
    return 24;
  if (yukiplt_c_candidates_copy(&candidates, NULL, 0, NULL, 0, &count, &bytes).code || count != 1)
    return 25;
  char* text = (char*)malloc(bytes);
  yukiplt_c_candidate_record candidate;
  if (!text ||
      yukiplt_c_candidates_copy(&candidates, &candidate, 1, text, bytes, &count, &bytes).code)
    return 26;
  if (yukiplt_c_candidates_destroy(&candidates).code) return 27;
  yukiplt_c_function_selector exact =
      YUKIPLT_C_FUNCTION_SELECTOR(text + candidate.selected.symbol.offset);
  exact.sources = 1u << candidate.selected.relocation_source;
  exact.types = YUKIPLT_C_TYPE_JUMP_SLOT;
  exact.index_source = candidate.selected.relocation_source;
  exact.base.relocation_index = candidate.selected.relocation_index;
  exact.base.version_match = candidate.selected.version_index >= 2 ? YUKIPLT_C_VERSION_EXACT
                                                                   : YUKIPLT_C_VERSION_UNVERSIONED;
  exact.base.version =
      candidate.selected.version_index >= 2 ? text + candidate.selected.version.offset : NULL;
  exact.base.dependency =
      candidate.selected.dependency.length ? text + candidate.selected.dependency.offset : NULL;
  exact.base.hidden = candidate.selected.version_hidden ? YUKIPLT_C_HIDDEN : YUKIPLT_C_NON_HIDDEN;
  yukiplt_c_function_target target = {0};
  target.struct_size = sizeof(target);
  target.image = candidate.image;
  target.request.struct_size = sizeof(target.request);
  target.request.base.target = exact;
  target.request.base.replacement = (void*)candidate_replacement;
  target.request.base.out_original = &candidate_original;
  yukiplt_c_plan plan = {0};
  yukiplt_c_session session = {0};
  yukiplt_c_summary summary;
  if (yukiplt_c_build_function_plan(&target, 1, &plan, NULL).code ||
      yukiplt_c_prepare_session(&plan, &session).code || yukiplt_c_plan_destroy(&plan).code)
    return 28;
  if (yukiplt_c_session_install(&session, &summary).code || call(1) != 111) return 29;
  if (yukiplt_c_session_unhook(&session, &summary).code || call(1) != 11 ||
      yukiplt_c_session_destroy(&session).code)
    return 30;
  yukiplt_c_rules rules = {0};
  yukiplt_c_image_selector filter = {sizeof(filter), YUKIPLT_C_IMAGE_PATH, path, 0};
  uint64_t rule_id = 0;
  if (yukiplt_c_rules_create(&rules).code ||
      yukiplt_c_rules_add_function(&rules, &filter, &exact, (void*)rule_replacement, rule_originals,
                                   1, &rule_id)
          .code)
    return 31;
  free(text);
  yukiplt_c_maps_cache cache = {0};
  if (yukiplt_c_maps_cache_create(&cache).code || yukiplt_c_maps_cache_capture(&cache).code)
    return 41;
  yukiplt_c_rule_report installed = {0}, restored = {0};
  if (yukiplt_c_rules_refresh_cached(&cache, &rules, &installed).code || call(1) != 111) return 32;
  yukiplt_c_rule_report retained = {0};
  if (yukiplt_c_rules_refresh_cached(&cache, &rules, &retained).code ||
      yukiplt_c_rule_report_destroy(&retained).code)
    return 42;
  if (yukiplt_c_rules_destroy(&rules).code != YUKIPLT_C_INVALID_ARGUMENT) return 33;
  if (yukiplt_c_rules_remove_cached(&cache, &rules, rule_id, &restored).code || call(1) != 11)
    return 34;
  size_t removed = 0;
  if (yukiplt_c_rules_prune_history(&rules, &removed).code || removed != 1 ||
      yukiplt_c_rules_destroy(&rules).code)
    return 35;
  yukiplt_c_named_image named = {sizeof(named), image_record.image, path};
  yukiplt_c_request request = {YUKIPLT_C_SELECTOR("sample_add"), (void*)nonnull_replacement,
                               &nonnull_original};
  yukiplt_c_handle handle = {0};
  if (yukiplt_c_hook_batch_nonnull_named_cached(&cache, &named, &request, 1, &handle, NULL, NULL)
          .code ||
      call(1) != 111 || yukiplt_c_unhook_diagnosed_cached(&cache, &handle, NULL, 0, NULL).code ||
      call(1) != 11)
    return 44;
  free(path_text);
  if (yukiplt_c_maps_cache_destroy(&cache).code) return 43;
  if (dlclose(library)) return 36;
  yukiplt_c_rule_report_info info;
  if (yukiplt_c_rule_report_get_info(&installed, &info).code || info.summary.retained != 1 ||
      yukiplt_c_rule_report_copy(&installed, NULL, 0, NULL, 0, &count, &bytes).code || count != 1)
    return 37;
  if (yukiplt_c_rule_report_destroy(&installed).code ||
      yukiplt_c_rule_report_destroy(&restored).code)
    return 38;
  return 0;
}

int run_c_scenarios(void) {
  int result = invalidation();
  if (!result) result = exercise(0);
  if (!result) result = exercise(1);
  if (!result && retained_library && dlclose(retained_library)) result = 17;
  if (!result) result = rules_and_candidates();
  if (!result && yukiplt_c_trim_idle_storage().code) result = 39;
  printf("session_consumer: %d\n", result);
  return result;
}
