// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>
#pragma once
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "yukiplt.h"

// A resident executable owns this registry. Subscription changes serialize with
// the complete fork cycle. Call registration and removal outside callbacks.
typedef struct fork_subscription {
  uint64_t token;
  yukiplt_c_fork_callbacks callbacks;
} fork_subscription;
static unsigned dispatcher_lock;
static uint64_t dispatcher_next = 1;
static fork_subscription dispatcher_slots[8];
static int dispatcher_registered;
static inline void dispatcher_acquire(void) {
  for (;;) {
    unsigned expected = 0;
    if (__atomic_compare_exchange_n(&dispatcher_lock, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
      return;
    syscall(SYS_futex, &dispatcher_lock, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0);
  }
}
static inline void dispatcher_release(void) {
  __atomic_store_n(&dispatcher_lock, 0, __ATOMIC_RELEASE);
  syscall(SYS_futex, &dispatcher_lock, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}
static inline void dispatcher_prepare(void) {
  int saved = errno;
  dispatcher_acquire();
  for (unsigned i = 8; i; --i)
    if (dispatcher_slots[i - 1].token)
      dispatcher_slots[i - 1].callbacks.prepare(dispatcher_slots[i - 1].callbacks.context);
  errno = saved;
}
static inline void dispatcher_finish(int child) {
  int saved = errno;
  for (unsigned i = 0; i < 8; ++i) {
    fork_subscription* s = &dispatcher_slots[i];
    if (s->token) {
      if (child)
        s->callbacks.child(s->callbacks.context);
      else
        s->callbacks.parent(s->callbacks.context);
    }
  }
  dispatcher_release();
  errno = saved;
}
static inline void dispatcher_parent(void) { dispatcher_finish(0); }
static inline void dispatcher_child(void) { dispatcher_finish(1); }
static inline int dispatcher_subscribe(void* context, const yukiplt_c_fork_callbacks* callbacks,
                                       uint64_t* token) {
  (void)context;
  dispatcher_acquire();
  int error = ENOSPC;
  for (unsigned i = 0; i < 8; ++i) {
    if (dispatcher_slots[i].token) continue;
    if (dispatcher_next == UINT64_MAX) {
      error = EOVERFLOW;
      break;
    }
    dispatcher_slots[i].callbacks = *callbacks;
    dispatcher_slots[i].token = dispatcher_next++;
    *token = dispatcher_slots[i].token;
    error = 0;
    break;
  }
  dispatcher_release();
  return error;
}
static inline int dispatcher_unsubscribe(void* context, uint64_t token) {
  (void)context;
  dispatcher_acquire();
  int error = EINVAL;
  for (unsigned i = 0; i < 8; ++i) {
    if (dispatcher_slots[i].token != token) continue;
    memmove(&dispatcher_slots[i], &dispatcher_slots[i + 1], (7 - i) * sizeof(dispatcher_slots[0]));
    memset(&dispatcher_slots[7], 0, sizeof(dispatcher_slots[7]));
    error = 0;
    break;
  }
  dispatcher_release();
  return error;
}
static inline int dispatcher_init(void) {
  if (dispatcher_registered) return 0;
  int error = pthread_atfork(dispatcher_prepare, dispatcher_parent, dispatcher_child);
  if (!error) dispatcher_registered = 1;
  return error;
}
static inline yukiplt_c_fork_config dispatcher_config(void) {
  yukiplt_c_fork_config config = {sizeof(config),       YUKIPLT_C_FORK_HOST_DISPATCHER, 5000, NULL,
                                  dispatcher_subscribe, dispatcher_unsubscribe};
  return config;
}
