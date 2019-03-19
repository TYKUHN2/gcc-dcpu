/* { dg-do compile } */
/* { dg-final { check-function-bodies "**" "" "-DCHECK_ASM" } } */

#include "test_sve_acle.h"

/*
** set3_s32_z0_0:
**	mov	z1\.d, z17\.d
**	mov	z2\.d, z18\.d
**	mov	z0\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z0_0, svint32x3_t, svint32_t,
	  z0 = svset3_s32 (z16, 0, z4),
	  z0 = svset3 (z16, 0, z4))

/*
** set3_s32_z0_1:
**	mov	z0\.d, z16\.d
**	mov	z2\.d, z18\.d
**	mov	z1\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z0_1, svint32x3_t, svint32_t,
	  z0 = svset3_s32 (z16, 1, z4),
	  z0 = svset3 (z16, 1, z4))

/*
** set3_s32_z0_2:
**	mov	z0\.d, z16\.d
**	mov	z1\.d, z17\.d
**	mov	z2\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z0_2, svint32x3_t, svint32_t,
	  z0 = svset3_s32 (z16, 2, z4),
	  z0 = svset3 (z16, 2, z4))

/*
** set3_s32_z16_0:
**	mov	z16\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z16_0, svint32x3_t, svint32_t,
	  z16 = svset3_s32 (z16, 0, z4),
	  z16 = svset3 (z16, 0, z4))

/*
** set3_s32_z16_1:
**	mov	z17\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z16_1, svint32x3_t, svint32_t,
	  z16 = svset3_s32 (z16, 1, z4),
	  z16 = svset3 (z16, 1, z4))

/*
** set3_s32_z16_2:
**	mov	z18\.d, z4\.d
**	ret
*/
TEST_SET (set3_s32_z16_2, svint32x3_t, svint32_t,
	  z16 = svset3_s32 (z16, 2, z4),
	  z16 = svset3 (z16, 2, z4))
