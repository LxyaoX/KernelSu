// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>
#include "yukiplt.h"
extern "C" int host_enable(const yukiplt_c_fork_config* config) {
  return static_cast<int>(yukiplt_c_enable_atfork(config).code);
}
extern "C" int host_disable() {
  auto error = yukiplt_c_disable_atfork();
  return static_cast<int>(error.code ? error.code : yukiplt_c_trim_idle_storage().code);
}
