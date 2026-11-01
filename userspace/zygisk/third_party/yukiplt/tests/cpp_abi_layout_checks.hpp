// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

// Frozen C++ ABI baseline. C++ clients must rebuild against these headers.
#ifndef YUKIPLT_TEST_CPP_ABI_LAYOUT_CHECKS_HPP_
#define YUKIPLT_TEST_CPP_ABI_LAYOUT_CHECKS_HPP_
#include <cstddef>
#include <type_traits>

#include "yukiplt.hpp"

static_assert(std::is_standard_layout_v<yukiplt::OperationDiagnostic>);
static_assert(offsetof(yukiplt::OperationDiagnostic, version) == 0);
static_assert(offsetof(yukiplt::OperationDiagnostic, stage) == 4);
static_assert(offsetof(yukiplt::OperationDiagnostic, cause) == 8);
static_assert(offsetof(yukiplt::OperationDiagnostic, error) == 12);
static_assert(offsetof(yukiplt::OperationDiagnostic, raise_errno) == 20);
static_assert(offsetof(yukiplt::OperationDiagnostic, restore_errno) == 24);
static_assert(offsetof(yukiplt::OperationDiagnostic, rollback_failed) == 28);
static_assert(offsetof(yukiplt::OperationDiagnostic, image_invalidated) == 29);
static_assert(offsetof(yukiplt::OperationDiagnostic, handle_consumed) == 30);
static_assert(offsetof(yukiplt::OperationDiagnostic, residual_page_count) == 32);
static_assert(sizeof(yukiplt::OperationDiagnostic) == 32 + sizeof(size_t));
static_assert(alignof(yukiplt::OperationDiagnostic) == alignof(size_t));
static_assert(offsetof(yukiplt::RecoveryDiagnostic, stage) == 0);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, error) == 4);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, raise_errno) == 12);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, restore_errno) == 16);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, rollback_failed) == 20);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, image_invalidated) == 21);
static_assert(offsetof(yukiplt::RecoveryDiagnostic, residual_page_count) == 24);
static_assert(sizeof(yukiplt::RecoveryDiagnostic) == 24 + sizeof(size_t));

#if !defined(_GLIBCXX_DEBUG)
static_assert(sizeof(yukiplt::HookPlan) == 6 * sizeof(void*));
static_assert(sizeof(yukiplt::FunctionHookPlan) == 6 * sizeof(void*));
static_assert(sizeof(yukiplt::HookSession) == 9 * sizeof(void*));
static_assert(sizeof(yukiplt::FunctionHookSession) == 9 * sizeof(void*));
static_assert(alignof(yukiplt::HookPlan) == alignof(void*));
static_assert(alignof(yukiplt::FunctionHookPlan) == alignof(void*));
static_assert(alignof(yukiplt::HookSession) == alignof(void*));
static_assert(alignof(yukiplt::FunctionHookSession) == alignof(void*));
#endif
#if defined(__ANDROID__) && defined(_LIBCPP_VERSION)
static_assert(sizeof(yukiplt::RuleSet) == (sizeof(void*) == 8 ? 120 : 64));
static_assert(alignof(yukiplt::RuleSet) == alignof(uint64_t));
static_assert(sizeof(yukiplt::VersionedImport) == (sizeof(void*) == 8 ? 104 : 56));
static_assert(sizeof(yukiplt::PlannedTarget) == (sizeof(void*) == 8       ? 152
                                                 : alignof(uint64_t) == 8 ? 88
                                                                          : 84));
#endif
#endif
