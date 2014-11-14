/* { dg-do compile } */
/* { dg-options "-mavx512vl -O2" } */
/* { dg-final { scan-assembler-times "vscatterqps\[ \\t\]+\[^\n\]*xmm\[0-9\]\[^\n\]*ymm\[0-9\]\[^\n\]*{%k\[1-7\]}" 2 } } */
/* { dg-final { scan-assembler-times "vscatterqps\[ \\t\]+\[^\n\]*xmm\[0-9\]\[^\n\]*xmm\[0-9\]\[^\n\]*{%k\[1-7\]}" 2 } } */

#include <immintrin.h>

volatile __m128 src;
volatile __m256i idx1;
volatile __m128i idx2;
volatile __mmask8 m8;
float *addr;

void extern
avx512vl_test (void)
{
  _mm256_i64scatter_ps (addr, idx1, src, 8);
  _mm256_mask_i64scatter_ps (addr, m8, idx1, src, 8);

  _mm_i64scatter_ps (addr, idx2, src, 8);
  _mm_mask_i64scatter_ps (addr, m8, idx2, src, 8);
}
