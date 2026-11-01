// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include "overrides.hpp"

#include <pthread.h>

#include <atomic>

namespace yukiplt::detail {
std::atomic<const PlatformOps*> g_active_ops{nullptr};
const PlatformOps& wrapped_active_ops() noexcept asm("__wrap__ZN7yukiplt6detail10active_opsEv");
const PlatformOps& wrapped_active_ops() noexcept {
  const PlatformOps* p = g_active_ops.load(std::memory_order_acquire);
  return p != nullptr ? *p : system_ops();
}
void set_active_ops(const PlatformOps* ops) noexcept {
  g_active_ops.store(ops, std::memory_order_release);
}
std::atomic<ImageEnumerator> g_image_enumerator{nullptr};
ImageEnumerator wrapped_image_enumerator() noexcept
    asm("__wrap__ZN7yukiplt6detail23active_image_enumeratorEv");
ImageEnumerator wrapped_image_enumerator() noexcept {
  ImageEnumerator override_fn = g_image_enumerator.load(std::memory_order_acquire);
  return override_fn != nullptr ? override_fn : &find_images;
}
void set_active_image_enumerator(ImageEnumerator enumerator) noexcept {
  g_image_enumerator.store(enumerator, std::memory_order_release);
}
std::atomic<SystemIterator> g_system_iterator{nullptr};
void set_system_iterator(SystemIterator iterator) noexcept {
  g_system_iterator.store(iterator, std::memory_order_release);
}
}  // namespace yukiplt::detail

extern "C" int __real_dl_iterate_phdr(int (*callback)(dl_phdr_info*, size_t, void*), void* data);
extern "C" int __wrap_dl_iterate_phdr(int (*callback)(dl_phdr_info*, size_t, void*), void* data) {
  auto iterator = yukiplt::detail::g_system_iterator.load(std::memory_order_acquire);
  return iterator ? iterator(callback, data) : __real_dl_iterate_phdr(callback, data);
}

namespace {
std::atomic<void (*)()> lookup_callback{nullptr};
std::atomic<pthread_key_t> latest_null_key{0}, armed_key{0};
}  // namespace

namespace {
thread_local pthread_mutex_t* unlock_mutex = nullptr;
thread_local bool unlock_after = false;
thread_local void (*unlock_callback)() = nullptr;
}  // namespace
namespace yukiplt::detail {
void set_mutex_unlock_callback(pthread_mutex_t* mutex, bool after, void (*callback)()) noexcept {
  unlock_mutex = mutex;
  unlock_after = after;
  unlock_callback = callback;
}
}  // namespace yukiplt::detail
extern "C" int __real_pthread_mutex_unlock(pthread_mutex_t*);
extern "C" int __wrap_pthread_mutex_unlock(pthread_mutex_t* mutex) {
  auto callback = (!unlock_mutex || unlock_mutex == mutex) ? unlock_callback : nullptr;
  const bool after = unlock_after;
  if (callback) unlock_callback = nullptr;
  if (callback && !after) callback();
  int result = __real_pthread_mutex_unlock(mutex);
  if (callback && after) callback();
  return result;
}
namespace yukiplt::detail {
void set_thread_lookup_callback(void (*callback)()) noexcept {
  armed_key.store(latest_null_key.load());
  lookup_callback.store(callback);
}
}  // namespace yukiplt::detail
extern "C" void* __real_pthread_getspecific(pthread_key_t);
extern "C" void* __wrap_pthread_getspecific(pthread_key_t key) {
  auto callback = key == armed_key.load() ? lookup_callback.exchange(nullptr) : nullptr;
  if (callback) callback();
  return __real_pthread_getspecific(key);
}

extern "C" int __real_pthread_key_create(pthread_key_t*, void (*)(void*));
extern "C" int __wrap_pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
  int result = __real_pthread_key_create(key, destructor);
  if (!result && !destructor) latest_null_key.store(*key);
  return result;
}
