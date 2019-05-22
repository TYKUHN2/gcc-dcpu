/* { dg-do compile } */
/* { dg-final { check-function-bodies "**" "" "-DCHECK_ASM" } } */

#include "test_sve_acle.h"

/*
** tsmul_f64_tied1:
**	ftsmul	z0\.d, z0\.d, z16\.d
**	ret
*/
TEST_DUAL_Z (tsmul_f64_tied1, svfloat64_t, svuint64_t,
	     z0 = svtsmul_f64 (z0, z16),
	     z0 = svtsmul (z0, z16))

/*
** tsmul_f64_tied2:
**	ftsmul	z16\.d, z0\.d, z16\.d
**	ret
*/
TEST_DUAL_Z (tsmul_f64_tied2, svfloat64_t, svuint64_t,
	     z16_res = svtsmul_f64 (z0, z16),
	     z16_res = svtsmul (z0, z16))

/*
** tsmul_f64_untied:
**	ftsmul	z0\.d, z1\.d, z16\.d
**	ret
*/
TEST_DUAL_Z (tsmul_f64_untied, svfloat64_t, svuint64_t,
	     z0 = svtsmul_f64 (z1, z16),
	     z0 = svtsmul (z1, z16))
