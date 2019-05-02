/* { dg-do run } */
/* { dg-options "-O2 -msse" } */
/* { dg-require-effective-target sse } */

#ifndef CHECK_H
#define CHECK_H "sse-check.h"
#endif

#ifndef TEST
#define TEST sse_test
#endif

#include CHECK_H

#include <xmmintrin.h>

static __m128
__attribute__((noinline, unused))
test (__m128 a, __m128 b)
{
  return _mm_movelh_ps (a, b);
}

static void
TEST (void)
{
  union128 u, s1, s2;
  float e[4];

  s1.x = _mm_set_ps (24.43, 68.346, 43.35, 546.46);
  s2.x = _mm_set_ps (1.17, 2.16, 3.15, 4.14);
  u.x = _mm_set1_ps (0.0);

  u.x = test (s1.x, s2.x);

  e[0] = s1.a[0];
  e[1] = s1.a[1];
  e[2] = s2.a[0];
  e[3] = s2.a[1];

  if (check_union128 (u, e))
    abort ();
}
