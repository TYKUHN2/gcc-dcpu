/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-O2 -mno-sse" } */
/* { dg-final { scan-assembler "subq\[\\t \]*\\\$88,\[\\t \]*%rsp" } } */
/* { dg-final { scan-assembler-not "subq\[\\t \]*\\\$216,\[\\t \]*%rsp" } } */

#include <stdarg.h>

void foo (va_list va_arglist);

void
test (int a1, ...)
{
  va_list va_arglist;
  va_start (va_arglist, a1);
  foo (va_arglist);
  va_end (va_arglist);
}
