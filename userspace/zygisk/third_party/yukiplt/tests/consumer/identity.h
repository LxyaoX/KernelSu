// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

#ifndef YUKIPLT_TEST_IDENTITY_H_
#define YUKIPLT_TEST_IDENTITY_H_
#include <stdio.h>
#if defined(__ANDROID__)
#include <string.h>
#include <sys/system_properties.h>
#include <unistd.h>
#endif
static int test_identity(void) {
#if defined(__ANDROID__)
  char api[PROP_VALUE_MAX] = {0};
  if (__system_property_get("ro.build.version.sdk", api) <= 0) return 1;
  printf("identity: bits=%zu page=%ld uid=%ld gid=%ld api=%s\n", sizeof(void*) * 8,
         sysconf(_SC_PAGESIZE), (long)getuid(), (long)getgid(), api);
  const char* files[] = {"/proc/self/attr/current", "/proc/self/status"};
  for (size_t i = 0; i < 2; ++i) {
    FILE* stream = fopen(files[i], "r");
    if (!stream) return 1;
    char line[512];
    while (fgets(line, sizeof(line), stream))
      if (i == 0 || strncmp(line, "CapEff:", 7) == 0 || strncmp(line, "CapPrm:", 7) == 0)
        printf("%s", line);
    fclose(stream);
  }
#endif
  return 0;
}
#endif
