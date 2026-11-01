// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../c_abi_layout_checks.h"

#include "yukiplt.h"
int run_c_scenarios(void);
static int metadata_anchor;
static int owned_metadata(void) {
  yukiplt_c_image_list images = {0};
  yukiplt_c_import_snapshot imports = {0};
  yukiplt_c_image_record record;
  size_t count = 0, bytes = 0, skipped = 0;
  uint32_t kind = 0;
  char* text = NULL;
  int result = 1;
  if (yukiplt_c_find_image_containing(&metadata_anchor, &images).code) goto done;
  if (yukiplt_c_image_list_copy(&images, NULL, 0, NULL, 0, &count, &bytes).code || count != 1)
    goto done;
  text = (char*)malloc(bytes);
  if (!text) goto done;
  if (yukiplt_c_image_list_copy(&images, &record, 1, text, bytes, &count, &bytes).code) goto done;
  yukiplt_c_named_image named = {sizeof(named), record.image, text + record.path.offset};
  if (yukiplt_c_inspect_imports_named(&named, YUKIPLT_C_SNAPSHOT_VERSIONED, 0, &imports).code)
    goto done;
  if (yukiplt_c_image_list_destroy(&images).code) goto done;
  if (yukiplt_c_import_snapshot_copy(&imports, NULL, 0, NULL, 0, &count, &bytes, &kind, &skipped)
          .code)
    goto done;
  if (kind != YUKIPLT_C_SNAPSHOT_VERSIONED) goto done;
  result = 0;
done:
  free(text);
  if (yukiplt_c_import_snapshot_destroy(&imports).code) result = 1;
  if (yukiplt_c_image_list_destroy(&images).code) result = 1;
  return result;
}
static unsigned worker_ready, worker_stop;
static void* fork_worker(void* unused) {
  (void)unused;
  __atomic_store_n(&worker_ready, 1, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&worker_stop, __ATOMIC_ACQUIRE)) sched_yield();
  return NULL;
}
static int check_multithread_fork(void) {
  yukiplt_c_fork_config config = {sizeof(config), YUKIPLT_C_FORK_NATIVE, 5000, NULL, NULL, NULL};
  if (yukiplt_c_enable_atfork(&config).code) return 70;
  pthread_t worker;
  if (pthread_create(&worker, NULL, fork_worker, NULL)) return 71;
  while (!__atomic_load_n(&worker_ready, __ATOMIC_ACQUIRE)) sched_yield();
  pid_t child = fork();
  if (child == 0) {
    yukiplt_c_fork_status status = {0};
    status.struct_size = sizeof(status);
    if (yukiplt_c_get_fork_status(&status).code || status.phase != YUKIPLT_C_FORK_READY) _exit(72);
    _exit(run_c_scenarios());
  }
  __atomic_store_n(&worker_stop, 1, __ATOMIC_RELEASE);
  pthread_join(worker, NULL);
  if (yukiplt_c_disable_atfork().code || child < 0) return 73;
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 74;
  return 0;
}
int main(void) {
  void* original = NULL;
  if (yukiplt_c_abi_version() != 1 || !yukiplt_c_has_feature(YUKIPLT_C_FEATURE_ORIGINAL_LOAD) ||
      yukiplt_c_load_original(&original) != NULL)
    return 1;
#if defined(__ANDROID__)
  printf("identity: bits=%zu page=%ld uid=%ld gid=%ld\n", sizeof(void*) * 8, sysconf(_SC_PAGESIZE),
         (long)getuid(), (long)getgid());
  const char* files[] = {"/proc/self/attr/current", "/proc/self/status"};
  for (size_t i = 0; i < 2; ++i) {
    FILE* stream = fopen(files[i], "r");
    if (!stream) return 2;
    char line[512];
    while (fgets(line, sizeof(line), stream)) {
      if (i == 0 || strncmp(line, "CapEff:", 7) == 0 || strncmp(line, "CapPrm:", 7) == 0)
        printf("%s", line);
    }
    fclose(stream);
  }
#endif
  if (owned_metadata()) return 3;
  if (check_multithread_fork()) return 75;
  const int scenarios = run_c_scenarios();
  if (scenarios) return scenarios;
  puts("c_consumer: passed");
  return 0;
}
