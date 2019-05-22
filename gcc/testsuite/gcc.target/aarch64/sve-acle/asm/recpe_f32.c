/* { dg-do compile } */
/* { dg-final { check-function-bodies "**" "" "-DCHECK_ASM" } } */

#include "test_sve_acle.h"

/*
** recpe_f32_tied1:
**	frecpe	z0\.s, z0\.s
**	ret
*/
TEST_UNIFORM_Z (recpe_f32_tied1, svfloat32_t,
		z0 = svrecpe_f32 (z0),
		z0 = svrecpe (z0))

/*
** recpe_f32_untied:
**	frecpe	z0\.s, z1\.s
**	ret
*/
TEST_UNIFORM_Z (recpe_f32_untied, svfloat32_t,
		z0 = svrecpe_f32 (z1),
		z0 = svrecpe (z1))
