/* { dg-do compile } */
/* { dg-require-effective-target arm_crypto_ok } */
/* { dg-options "-O2 -mfloat-abi=softfp" } */

#pragma GCC target ("fpu=crypto-neon-fp-armv8")

#ifndef __ARM_FEATURE_CRYPTO
#error __ARM_FEATURE_CRYPTO not defined.
#endif

#ifndef __ARM_NEON
#error __ARM_NEON not defined.
#endif

#if !defined(__ARM_FP) || (__ARM_FP != 14)
#error __ARM_FP
#endif

#include "arm_neon.h"

int
foo (void)
{
  uint32x4_t a = {0xd, 0xe, 0xa, 0xd};
  uint32x4_t b = {0, 1, 2, 3};

  uint32x4_t res = vsha256su0q_u32 (a, b);
  return res[0];
}

#pragma GCC reset_options

/* Check that the FP version is correctly reset.  */

#if !defined(__ARM_FP) || (__ARM_FP != 12)
#error __ARM_FP
#endif

/* { dg-final { scan-assembler "sha256su0.32\tq\[0-9\]+, q\[0-9\]+" } } */
