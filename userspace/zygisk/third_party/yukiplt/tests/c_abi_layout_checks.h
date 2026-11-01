// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

// Compile the same ABI checks in C and C++, including native 32-bit targets.
#ifndef YUKIPLT_TEST_C_ABI_LAYOUT_CHECKS_H_
#define YUKIPLT_TEST_C_ABI_LAYOUT_CHECKS_H_
#include "yukiplt.h"
#ifdef __cplusplus
#define YK_STATIC_ASSERT(value) static_assert(value)
#define YK_ALIGNOF(type) alignof(type)
#else
#define YK_STATIC_ASSERT(value) _Static_assert(value, #value)
#define YK_ALIGNOF(type) _Alignof(type)
#endif
// Fixed numeric contract from the pre-extension C ABI baseline.
YK_STATIC_ASSERT(YUKIPLT_C_SUCCESS == (0));
YK_STATIC_ASSERT(YUKIPLT_C_INVALID_ARGUMENT == (1));
YK_STATIC_ASSERT(YUKIPLT_C_NULL_POINTER == (2));
YK_STATIC_ASSERT(YUKIPLT_C_INVALID_IMAGE == (3));
YK_STATIC_ASSERT(YUKIPLT_C_CORRUPTED_ELF == (4));
YK_STATIC_ASSERT(YUKIPLT_C_UNSUPPORTED_FORMAT == (5));
YK_STATIC_ASSERT(YUKIPLT_C_UNSUPPORTED_RELOCATION == (6));
YK_STATIC_ASSERT(YUKIPLT_C_SYMBOL_NOT_FOUND == (7));
YK_STATIC_ASSERT(YUKIPLT_C_NO_HASH_TABLE == (8));
YK_STATIC_ASSERT(YUKIPLT_C_AMBIGUOUS_SYMBOL == (9));
YK_STATIC_ASSERT(YUKIPLT_C_SLOT_OWNERSHIP_CONFLICT == (10));
YK_STATIC_ASSERT(YUKIPLT_C_ALREADY_HOOKED == (11));
YK_STATIC_ASSERT(YUKIPLT_C_NOT_INSTALLED == (12));
YK_STATIC_ASSERT(YUKIPLT_C_SLOT_MODIFIED_EXTERNALLY == (13));
YK_STATIC_ASSERT(YUKIPLT_C_INDETERMINATE == (14));
YK_STATIC_ASSERT(YUKIPLT_C_PERMISSION_DENIED == (15));
YK_STATIC_ASSERT(YUKIPLT_C_IMAGE_INVALIDATED == (16));
YK_STATIC_ASSERT(YUKIPLT_C_OUT_OF_MEMORY == (17));
YK_STATIC_ASSERT(YUKIPLT_C_INTEGER_OVERFLOW == (18));
YK_STATIC_ASSERT(YUKIPLT_C_OUT_OF_BOUNDS == (19));
YK_STATIC_ASSERT(YUKIPLT_C_IMAGE_NOT_FOUND == (20));
YK_STATIC_ASSERT(YUKIPLT_C_AMBIGUOUS_IMAGE == (21));
YK_STATIC_ASSERT(YUKIPLT_C_VMA_UNAVAILABLE == (22));
YK_STATIC_ASSERT(YUKIPLT_C_VERSION_ANY == (0));
YK_STATIC_ASSERT(YUKIPLT_C_VERSION_UNVERSIONED == (1));
YK_STATIC_ASSERT(YUKIPLT_C_VERSION_EXACT == (2));
YK_STATIC_ASSERT(YUKIPLT_C_HIDDEN_ANY == (0));
YK_STATIC_ASSERT(YUKIPLT_C_NON_HIDDEN == (1));
YK_STATIC_ASSERT(YUKIPLT_C_HIDDEN == (2));
YK_STATIC_ASSERT(YUKIPLT_C_UNTOUCHED == (0));
YK_STATIC_ASSERT(YUKIPLT_C_INSTALLED == (1));
YK_STATIC_ASSERT(YUKIPLT_C_RESTORED == (2));
YK_STATIC_ASSERT(YUKIPLT_C_CONFLICT == (3));
YK_STATIC_ASSERT(YUKIPLT_C_ROLLED_BACK == (4));
YK_STATIC_ASSERT(YUKIPLT_C_ROLLBACK_FAILED == (5));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_JMPREL == (1u << 0));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_RELA == (1u << 1));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_ANDROID_RELA == (1u << 2));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_REL == (1u << 3));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_ANDROID_REL == (1u << 4));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_NATIVE_DYNAMIC ==
                 (YUKIPLT_USES_RELA ? YUKIPLT_C_SOURCE_RELA : YUKIPLT_C_SOURCE_REL));
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_NATIVE_ANDROID_PACKED == (YUKIPLT_USES_RELA
                                                                ? YUKIPLT_C_SOURCE_ANDROID_RELA
                                                                : YUKIPLT_C_SOURCE_ANDROID_REL));
YK_STATIC_ASSERT(YUKIPLT_C_TYPE_JUMP_SLOT == (1u << 0));
YK_STATIC_ASSERT(YUKIPLT_C_TYPE_GLOB_DAT == (1u << 1));
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_JMPREL == (0));
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_RELA == (1));
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA == (2));
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_REL == (3));
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_ANDROID_REL == (4));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_FUNCTION_IMPORTS == (1));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_RECOVERY_DIAGNOSTICS == (2));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ERROR_NAMES == (3));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_EXPECTED_CURRENT == (4));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ADDRESS_DISCOVERY == (5));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_REL_IMPORTS == (6));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ERROR_CAUSES == (7));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ORIGINAL_LOAD == (8));
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_NAMED_IMAGE == (9));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_NONE == (0));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_PREPARE == (1));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_EXPECTED_CURRENT == (2));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_RAISE == (3));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_COMMIT == (4));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_ROLLBACK == (5));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_RESTORE_PAGES == (6));
YK_STATIC_ASSERT(YUKIPLT_C_STAGE_COMPLETE == (7));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_NONE == (0));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_INVALID_REQUEST == (1));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_HANDLE_INVALID == (2));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_OWN_IMAGE == (3));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_DUPLICATE_TARGET == (4));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_OUTPUT_ALIAS == (5));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_REPLACEMENT_IS_CURRENT == (6));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_ORIGINAL_CAPACITY == (7));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_METADATA_BOUNDS == (8));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_SCAN_BUDGET == (9));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_MAPS_UNAVAILABLE == (10));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_MAPS_COVERAGE == (11));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_VMA_POLICY == (12));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_PROTECT_FAILED == (13));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_EXPECTED_CURRENT == (14));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_CAS_CONFLICT == (15));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_INTERNAL_EXCEPTION == (16));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_HOST_CALLBACK == (17));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_RANDOM_SOURCE == (18));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_PLATFORM == (19));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_IDENTITY_MISMATCH == (20));
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_SLOT_DRIFT == (21));
YK_STATIC_ASSERT(YUKIPLT_C_ABI_VERSION == 1);
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_RELA == 2 && YUKIPLT_C_SOURCE_ANDROID_RELA == 4);
YK_STATIC_ASSERT(YUKIPLT_C_SOURCE_REL == 8 && YUKIPLT_C_SOURCE_ANDROID_REL == 16);
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_RELA == 1 && YUKIPLT_C_RELOC_SOURCE_ANDROID_RELA == 2);
YK_STATIC_ASSERT(YUKIPLT_C_RELOC_SOURCE_REL == 3 && YUKIPLT_C_RELOC_SOURCE_ANDROID_REL == 4);
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_REL_IMPORTS == 6);
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ERROR_CAUSES == 7);
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_ORIGINAL_LOAD == 8);
YK_STATIC_ASSERT(YUKIPLT_C_FEATURE_NAMED_IMAGE == 9);
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_NONE == 0 && YUKIPLT_C_CAUSE_PLATFORM == 19);
YK_STATIC_ASSERT(YUKIPLT_C_CAUSE_SLOT_DRIFT == 21);
YK_STATIC_ASSERT(sizeof(yukiplt_c_error) == 8 && sizeof(yukiplt_c_handle) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_error, code) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_error, sys_errno) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_handle, token) == 0);
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_error) == YK_ALIGNOF(uint32_t));
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_handle) == YK_ALIGNOF(uint64_t));
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_selector, base) == 16);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, stage) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, error) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, raise_errno) == 12);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, restore_errno) == 16);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, rollback_failed) == 20);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, image_invalidated) == 24);
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_recovery_diagnostic) == YK_ALIGNOF(size_t));
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, address) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, raised_protection) == sizeof(void*) + 4);
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_residual_page) == YK_ALIGNOF(void*));
// Field order of every by-value structure: same-width field swaps must fail.
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, symbol) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, version_match) == sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, version) == 2 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, dependency) == 3 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, hidden) == 4 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_selector, relocation_index) == 5 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_request, target) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_request, replacement) == sizeof(yukiplt_c_selector));
YK_STATIC_ASSERT(offsetof(yukiplt_c_request, out_original) ==
                 sizeof(yukiplt_c_selector) + sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_slot_report, address) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_slot_report, value_before) == sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_slot_report, value_observed) == 2 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_slot_report, status) == 3 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, load_bias) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, program_headers) == sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, program_header_count) == 2 * sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, struct_size) == 0);
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_named_image) == YK_ALIGNOF(yukiplt_c_image));
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_selector, struct_size) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_selector, sources) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_selector, types) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_selector, index_source) == 12);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request, target) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request, replacement) ==
                 sizeof(yukiplt_c_function_selector));
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request, out_original) ==
                 sizeof(yukiplt_c_function_selector) + sizeof(void*));
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, struct_size) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, require_current) ==
                 offsetof(yukiplt_c_function_request_ex, base) +
                     sizeof(yukiplt_c_function_request));
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, struct_size) == 0);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, stage) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, cause) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, error) == 12);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, raise_errno) == 20);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, restore_errno) == 24);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, rollback_failed) == 28);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, image_invalidated) == 32);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, handle_consumed) == 36);
YK_STATIC_ASSERT(offsetof(yukiplt_c_operation_diagnostic, residual_page_count) == 40);
YK_STATIC_ASSERT(sizeof(yukiplt_c_operation_diagnostic) == 40 + sizeof(size_t));
YK_STATIC_ASSERT(YK_ALIGNOF(yukiplt_c_operation_diagnostic) == YK_ALIGNOF(size_t));
#if YUKIPLT_ELF_BITS == 64
YK_STATIC_ASSERT(sizeof(yukiplt_c_recovery_diagnostic) == 40);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, residual_page_count) == 32);
YK_STATIC_ASSERT(sizeof(yukiplt_c_residual_page) == 24);
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, original_protection) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, sys_errno) == 16);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_request_ex) == 104);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, base) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, expected_current) == 96);
YK_STATIC_ASSERT(sizeof(yukiplt_c_image) == 32);
YK_STATIC_ASSERT(sizeof(yukiplt_c_named_image) == 48);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, image) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, path) == 40);
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, instance_token) == 24);
YK_STATIC_ASSERT(sizeof(yukiplt_c_selector) == 48);
YK_STATIC_ASSERT(sizeof(yukiplt_c_request) == 64);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_selector) == 64);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_request) == 80);
YK_STATIC_ASSERT(sizeof(yukiplt_c_slot_report) == 32);
#else
YK_STATIC_ASSERT(sizeof(yukiplt_c_recovery_diagnostic) == 32);
YK_STATIC_ASSERT(offsetof(yukiplt_c_recovery_diagnostic, residual_page_count) == 28);
YK_STATIC_ASSERT(sizeof(yukiplt_c_residual_page) == 16);
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, original_protection) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_residual_page, sys_errno) == 12);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_request_ex) == 60);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, base) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_function_request_ex, expected_current) == 56);
YK_STATIC_ASSERT(sizeof(yukiplt_c_selector) == 24);
YK_STATIC_ASSERT(sizeof(yukiplt_c_request) == 32);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_selector) == 40);
YK_STATIC_ASSERT(sizeof(yukiplt_c_function_request) == 48);
YK_STATIC_ASSERT(sizeof(yukiplt_c_slot_report) == 16);
#if YUKIPLT_ELF_MACHINE == 40
YK_STATIC_ASSERT(sizeof(yukiplt_c_image) == 24);
YK_STATIC_ASSERT(sizeof(yukiplt_c_named_image) == 40);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, image) == 8);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, path) == 32);
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, instance_token) == 16);
#else
YK_STATIC_ASSERT(sizeof(yukiplt_c_image) == 20);
YK_STATIC_ASSERT(sizeof(yukiplt_c_named_image) == 28);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, image) == 4);
YK_STATIC_ASSERT(offsetof(yukiplt_c_named_image, path) == 24);
YK_STATIC_ASSERT(offsetof(yukiplt_c_image, instance_token) == 12);
#endif
#endif
#undef YK_STATIC_ASSERT
#undef YK_ALIGNOF
#endif
