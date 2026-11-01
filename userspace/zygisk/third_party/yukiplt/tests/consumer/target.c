// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Rouyashiki <https://rouyashiki.org>

extern int sample_add(int);
int call_import(int x) { return sample_add(x) + 3; }
