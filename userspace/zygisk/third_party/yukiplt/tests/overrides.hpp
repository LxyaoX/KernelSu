// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#pragma once
#include <pthread.h>

#include "memory.hpp"
#include "system_images.hpp"

namespace yukiplt::detail {
void set_thread_lookup_callback(void (*callback)()) noexcept;
void set_mutex_unlock_callback(pthread_mutex_t* mutex, bool after, void (*callback)()) noexcept;
void set_active_ops(const PlatformOps* ops) noexcept;
void set_active_image_enumerator(ImageEnumerator enumerator) noexcept;
using SystemIterator = int (*)(int (*)(dl_phdr_info*, size_t, void*), void*);
void set_system_iterator(SystemIterator iterator) noexcept;
}  // namespace yukiplt::detail
