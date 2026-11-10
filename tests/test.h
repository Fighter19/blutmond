/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Minimal test helpers: each test is a program that returns 0 on success.

#include <stdio.h>

static int g_testFailures = 0;

#define TEST_ASSERT(x) do { \
    if (!(x)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      g_testFailures++; \
      return; \
    } \
  } while (0)

#define RUN_TEST(fn) do { \
    int before = g_testFailures; \
    fn(); \
    printf("%s %s\n", g_testFailures == before ? "ok  " : "FAIL", #fn); \
  } while (0)

#define TEST_RESULT() (g_testFailures == 0 ? 0 : 1)
