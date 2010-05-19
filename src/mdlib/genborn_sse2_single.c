/*
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2008, The GROMACS development team,
 * check out http://www.gromacs.org for more information.
 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * Gallium Rubidium Oxygen Manganese Argon Carbon Silicon
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <string.h>

#include "typedefs.h"
#include "smalloc.h"
#include "genborn.h"
#include "vec.h"
#include "grompp.h"
#include "pdbio.h"
#include "names.h"
#include "physics.h"
#include "partdec.h"
#include "domdec.h"
#include "network.h"
#include "gmx_fatal.h"
#include "mtop_util.h"
#include "genborn.h"

#ifdef GMX_LIB_MPI
#include <mpi.h>
#endif
#ifdef GMX_THREAD_MPI
#include "thread_mpi.h"
#endif

/* Only compile this file if SSE intrinsics are available */
#if ( (defined(GMX_IA32_SSE) || defined(GMX_X86_64_SSE) || defined(GMX_SSE2)) && !defined(GMX_DOUBLE) )
#include <xmmintrin.h>
#include <emmintrin.h>


#if (defined (_MSC_VER) || defined(__INTEL_COMPILER))
#define gmx_castsi128_ps(a) _mm_castsi128_ps(a)
#define gmx_castps_si128(a) _mm_castps_si128(a)
#elif defined(__GNUC__)
#define gmx_castsi128_ps(a) ((__m128)(a))
#define gmx_castps_si128(a) ((__m128i)(a))
#else
static __m128 gmx_castsi128_ps(__m128i a) { return *(__m128 *) &a; } 
static __m128i gmx_castps_si128(__m128 a) { return *(__m128i *) &a; } 
#endif

/* SIMD (SSE1+MMX indeed) implementation of sin, cos, exp and log
 
 Inspired by Intel Approximate Math library, and based on the
 corresponding algorithms of the cephes math library
 */

/* Copyright (C) 2007  Julien Pommier
 
 This software is provided 'as-is', without any express or implied
 warranty.  In no event will the authors be held liable for any damages
 arising from the use of this software.
 
 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it
 freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
 claim that you wrote the original software. If you use this software
 in a product, an acknowledgment in the product documentation would be
 appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
 misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 
 (this is the zlib license)
 */

#ifdef _MSC_VER /* visual c++ */
# define ALIGN16_BEG __declspec(align(16))
# define ALIGN16_END 
#else /* gcc or icc */
# define ALIGN16_BEG
# define ALIGN16_END __attribute__((aligned(16)))
#endif

#define _PS_CONST(Name, Val)                                            \
static const ALIGN16_BEG float _ps_##Name[4] ALIGN16_END = { Val, Val, Val, Val }
#define _PI32_CONST(Name, Val)                                            \
static const ALIGN16_BEG int _pi32_##Name[4] ALIGN16_END = { Val, Val, Val, Val }
#define _PS_CONST_TYPE(Name, Type, Val)                                 \
static const ALIGN16_BEG Type _ps_##Name[4] ALIGN16_END = { Val, Val, Val, Val }

/* Still parameters - make sure to edit in genborn.c too if you change these! */
#define STILL_P1  0.073*0.1              /* length        */
#define STILL_P2  0.921*0.1*CAL2JOULE    /* energy*length */
#define STILL_P3  6.211*0.1*CAL2JOULE    /* energy*length */
#define STILL_P4  15.236*0.1*CAL2JOULE
#define STILL_P5  1.254 

#define STILL_P5INV (1.0/STILL_P5)
#define STILL_PIP5  (M_PI*STILL_P5)



_PS_CONST(1  , 1.0f);
_PS_CONST(0p5, 0.5f);
/* the smallest non denormalized float number */
_PS_CONST_TYPE(min_norm_pos, int, 0x00800000);
_PS_CONST_TYPE(mant_mask, int, 0x7f800000);
_PS_CONST_TYPE(inv_mant_mask, int, ~0x7f800000);

_PS_CONST_TYPE(sign_mask, int, 0x80000000);
_PS_CONST_TYPE(inv_sign_mask, int, ~0x80000000);

_PI32_CONST(1, 1);
_PI32_CONST(inv1, ~1);
_PI32_CONST(2, 2);
_PI32_CONST(4, 4);
_PI32_CONST(0x7f, 0x7f);

_PS_CONST(cephes_SQRTHF, 0.707106781186547524);
_PS_CONST(cephes_log_p0, 7.0376836292E-2);
_PS_CONST(cephes_log_p1, - 1.1514610310E-1);
_PS_CONST(cephes_log_p2, 1.1676998740E-1);
_PS_CONST(cephes_log_p3, - 1.2420140846E-1);
_PS_CONST(cephes_log_p4, + 1.4249322787E-1);
_PS_CONST(cephes_log_p5, - 1.6668057665E-1);
_PS_CONST(cephes_log_p6, + 2.0000714765E-1);
_PS_CONST(cephes_log_p7, - 2.4999993993E-1);
_PS_CONST(cephes_log_p8, + 3.3333331174E-1);
_PS_CONST(cephes_log_q1, -2.12194440e-4);
_PS_CONST(cephes_log_q2, 0.693359375);

_PS_CONST(minus_cephes_DP1, -0.78515625);
_PS_CONST(minus_cephes_DP2, -2.4187564849853515625e-4);
_PS_CONST(minus_cephes_DP3, -3.77489497744594108e-8);
_PS_CONST(sincof_p0, -1.9515295891E-4);
_PS_CONST(sincof_p1,  8.3321608736E-3);
_PS_CONST(sincof_p2, -1.6666654611E-1);
_PS_CONST(coscof_p0,  2.443315711809948E-005);
_PS_CONST(coscof_p1, -1.388731625493765E-003);
_PS_CONST(coscof_p2,  4.166664568298827E-002);
_PS_CONST(cephes_FOPI, 1.27323954473516); /* 4 / M_PI */

_PS_CONST(exp_hi,	88.3762626647949f);
_PS_CONST(exp_lo,	-88.3762626647949f);

_PS_CONST(cephes_LOG2EF, 1.44269504088896341);
_PS_CONST(cephes_exp_C1, 0.693359375);
_PS_CONST(cephes_exp_C2, -2.12194440e-4);

_PS_CONST(cephes_exp_p0, 1.9875691500E-4);
_PS_CONST(cephes_exp_p1, 1.3981999507E-3);
_PS_CONST(cephes_exp_p2, 8.3334519073E-3);
_PS_CONST(cephes_exp_p3, 4.1665795894E-2);
_PS_CONST(cephes_exp_p4, 1.6666665459E-1);
_PS_CONST(cephes_exp_p5, 5.0000001201E-1);


#define COPY_XMM_TO_MM(xmm_, mm0_, mm1_) {          \
xmm_mm_union u; u.xmm = xmm_;                   \
mm0_ = u.mm[0];                                 \
mm1_ = u.mm[1];                                 \
}

#define COPY_MM_TO_XMM(mm0_, mm1_, xmm_) {                         \
xmm_mm_union u; u.mm[0]=mm0_; u.mm[1]=mm1_; xmm_ = u.xmm;      \
}

typedef
union 
{
	__m128 sse;
	float  f[4];
} my_sse_t;

typedef union xmm_mm_union {
	__m128 xmm;
	__m64 mm[2];
} xmm_mm_union;

void sincos_ps(__m128 x, __m128 *s, __m128 *c) {
	__m128 xmm1, xmm2, xmm3, sign_bit_sin, y, z;
	__m64 mm0, mm1, mm2, mm3, mm4, mm5;
	__m128 swap_sign_bit_sin,sign_bit_cos;
	__m128 poly_mask,tmp,y2,ysin1,ysin2;

	xmm3 = _mm_setzero_ps();
	
	sign_bit_sin = x;
	/* take the absolute value */
	x = _mm_and_ps(x, *(__m128*)_ps_inv_sign_mask);
	/* extract the sign bit (upper one) */
	sign_bit_sin = _mm_and_ps(sign_bit_sin, *(__m128*)_ps_sign_mask);
	
	/* scale by 4/Pi */
	y = _mm_mul_ps(x, *(__m128*)_ps_cephes_FOPI);
    
	/* store the integer part of y in mm0:mm1 */
	xmm3 = _mm_movehl_ps(xmm3, y);
	mm2 = _mm_cvttps_pi32(y);
	mm3 = _mm_cvttps_pi32(xmm3);
	
	/* j=(j+1) & (~1) (see the cephes sources) */
	mm2 = _mm_add_pi32(mm2, *(__m64*)_pi32_1);
	mm3 = _mm_add_pi32(mm3, *(__m64*)_pi32_1);
	mm2 = _mm_and_si64(mm2, *(__m64*)_pi32_inv1);
	mm3 = _mm_and_si64(mm3, *(__m64*)_pi32_inv1);
	
	y = _mm_cvtpi32x2_ps(mm2, mm3);
	
	mm4 = mm2;
	mm5 = mm3;
	
	/* get the swap sign flag for the sine */
	mm0 = _mm_and_si64(mm2, *(__m64*)_pi32_4);
	mm1 = _mm_and_si64(mm3, *(__m64*)_pi32_4);
	mm0 = _mm_slli_pi32(mm0, 29);
	mm1 = _mm_slli_pi32(mm1, 29);

	COPY_MM_TO_XMM(mm0, mm1, swap_sign_bit_sin);
	
	/* get the polynom selection mask for the sine */
	
	mm2 = _mm_and_si64(mm2, *(__m64*)_pi32_2);
	mm3 = _mm_and_si64(mm3, *(__m64*)_pi32_2);
	mm2 = _mm_cmpeq_pi32(mm2, _mm_setzero_si64());
	mm3 = _mm_cmpeq_pi32(mm3, _mm_setzero_si64());
	COPY_MM_TO_XMM(mm2, mm3, poly_mask);
	
	/* The magic pass: "Extended precision modular arithmetic" 
     x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(__m128*)_ps_minus_cephes_DP1;
	xmm2 = *(__m128*)_ps_minus_cephes_DP2;
	xmm3 = *(__m128*)_ps_minus_cephes_DP3;
	xmm1 = _mm_mul_ps(y, xmm1);
	xmm2 = _mm_mul_ps(y, xmm2);
	xmm3 = _mm_mul_ps(y, xmm3);
	x = _mm_add_ps(x, xmm1);
	x = _mm_add_ps(x, xmm2);
	x = _mm_add_ps(x, xmm3);
	
	
	/* get the sign flag for the cosine */
	mm4 = _mm_sub_pi32(mm4, *(__m64*)_pi32_2);
	mm5 = _mm_sub_pi32(mm5, *(__m64*)_pi32_2);
	mm4 = _mm_andnot_si64(mm4, *(__m64*)_pi32_4);
	mm5 = _mm_andnot_si64(mm5, *(__m64*)_pi32_4);
	mm4 = _mm_slli_pi32(mm4, 29);
	mm5 = _mm_slli_pi32(mm5, 29);

	COPY_MM_TO_XMM(mm4, mm5, sign_bit_cos);
	
	sign_bit_sin = _mm_xor_ps(sign_bit_sin, swap_sign_bit_sin);
	
	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	z = _mm_mul_ps(x,x);
	y = *(__m128*)_ps_coscof_p0;
	
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(__m128*)_ps_coscof_p1);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(__m128*)_ps_coscof_p2);
	y = _mm_mul_ps(y, z);
	y = _mm_mul_ps(y, z);
	tmp = _mm_mul_ps(z, *(__m128*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);
	y = _mm_add_ps(y, *(__m128*)_ps_1);
	
	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */
	y2 = *(__m128*)_ps_sincof_p0;
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(__m128*)_ps_sincof_p1);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(__m128*)_ps_sincof_p2);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_mul_ps(y2, x);
	y2 = _mm_add_ps(y2, x);
	
	/* select the correct result from the two polynoms */  
	xmm3 = poly_mask;
	ysin2 = _mm_and_ps(xmm3, y2);
	ysin1 = _mm_andnot_ps(xmm3, y);
	y2 = _mm_sub_ps(y2,ysin2);
	y = _mm_sub_ps(y, ysin1);
	
	xmm1 = _mm_add_ps(ysin1,ysin2);
	xmm2 = _mm_add_ps(y,y2);
	
	/* update the sign */
	*s = _mm_xor_ps(xmm1, sign_bit_sin);
	*c = _mm_xor_ps(xmm2, sign_bit_cos);
	_mm_empty(); /* good-bye mmx */
}


__m128 log_ps(__m128 x) {
	__m64 mm0, mm1;
	__m128 mask,tmp,z,y,e;
	__m128 one = *(__m128*)_ps_1;
	
	__m128 invalid_mask = _mm_cmple_ps(x, _mm_setzero_ps());
	
	x = _mm_max_ps(x, *(__m128*)_ps_min_norm_pos);  /* cut off denormalized stuff */
	
	
	/* part 1: x = frexpf(x, &e); */
	COPY_XMM_TO_MM(x, mm0, mm1);
	mm0 = _mm_srli_pi32(mm0, 23);
	mm1 = _mm_srli_pi32(mm1, 23);
	/* keep only the fractional part */
	x = _mm_and_ps(x, *(__m128*)_ps_inv_mant_mask);
	x = _mm_or_ps(x, *(__m128*)_ps_0p5);
	
	/* now e=mm0:mm1 contain the floatly base-2 exponent */
	mm0 = _mm_sub_pi32(mm0, *(__m64*)_pi32_0x7f);
	
	
	mm1 = _mm_sub_pi32(mm1, *(__m64*)_pi32_0x7f);
	
	e = _mm_cvtpi32x2_ps(mm0, mm1);
	e = _mm_add_ps(e, one);
	
	/* part2: 
     if( x < SQRTHF ) {
	 e -= 1;
	 x = x + x - 1.0;
     } else { x = x - 1.0; }
	 */
	mask = _mm_cmplt_ps(x, *(__m128*)_ps_cephes_SQRTHF);
	
	tmp = _mm_and_ps(x, mask);
	x = _mm_sub_ps(x, one);
	e = _mm_sub_ps(e, _mm_and_ps(one, mask));
	x = _mm_add_ps(x, tmp);
	
	
	z = _mm_mul_ps(x,x);
	
	y = *(__m128*)_ps_cephes_log_p0;
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p1);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p2);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p3);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p4);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p5);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p6);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p7);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_log_p8);
	y = _mm_mul_ps(y, x);
	
	y = _mm_mul_ps(y, z);
	
	
	tmp = _mm_mul_ps(e, *(__m128*)_ps_cephes_log_q1);
	y = _mm_add_ps(y, tmp);
	
	
	tmp = _mm_mul_ps(z, *(__m128*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);
	
	tmp = _mm_mul_ps(e, *(__m128*)_ps_cephes_log_q2);
	x = _mm_add_ps(x, y);
	x = _mm_add_ps(x, tmp);
	x = _mm_or_ps(x, invalid_mask); /* negative arg will be NAN */
	_mm_empty();
	return x;
}

__m128 exp_ps(__m128 x) {
	__m128 y,z,mask,pow2n;
	__m128 tmp, fx;
	__m64 mm0, mm1;
	__m128 one = *(__m128*)_ps_1;
	
	tmp = _mm_setzero_ps();
	x = _mm_min_ps(x, *(__m128*)_ps_exp_hi);
	x = _mm_max_ps(x, *(__m128*)_ps_exp_lo);
	
	/* express exp(x) as exp(g + n*log(2)) */
	fx = _mm_mul_ps(x, *(__m128*)_ps_cephes_LOG2EF);
	fx = _mm_add_ps(fx, *(__m128*)_ps_0p5);
	
	/* how to perform a floorf with SSE: just below */
	/* step 1 : cast to int */
	tmp = _mm_movehl_ps(tmp, fx);
	mm0 = _mm_cvttps_pi32(fx);
	mm1 = _mm_cvttps_pi32(tmp);
	/* step 2 : cast back to float */
	tmp = _mm_cvtpi32x2_ps(mm0, mm1);
	/* if greater, substract 1 */
	mask = _mm_cmpgt_ps(tmp, fx);    
	mask = _mm_and_ps(mask, one);
	fx = _mm_sub_ps(tmp, mask);
	
	tmp = _mm_mul_ps(fx, *(__m128*)_ps_cephes_exp_C1);
	z = _mm_mul_ps(fx, *(__m128*)_ps_cephes_exp_C2);
	x = _mm_sub_ps(x, tmp);
	x = _mm_sub_ps(x, z);
	
	z = _mm_mul_ps(x,x);
	
	y = *(__m128*)_ps_cephes_exp_p0;
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_exp_p1);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_exp_p2);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_exp_p3);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_exp_p4);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(__m128*)_ps_cephes_exp_p5);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, x);
	y = _mm_add_ps(y, one);
	
	/* build 2^n */
	z = _mm_movehl_ps(z, fx);
	mm0 = _mm_cvttps_pi32(fx);
	mm1 = _mm_cvttps_pi32(z);
	mm0 = _mm_add_pi32(mm0, *(__m64*)_pi32_0x7f);
	mm1 = _mm_add_pi32(mm1, *(__m64*)_pi32_0x7f);
	mm0 = _mm_slli_pi32(mm0, 23); 
	mm1 = _mm_slli_pi32(mm1, 23);
	 
	COPY_MM_TO_XMM(mm0, mm1, pow2n);
	
	y = _mm_mul_ps(y, pow2n);
	_mm_empty();
	return y;
}


__m128 log2_ps(__m128 x)
{
	const __m128 exp_ps  = gmx_castsi128_ps( _mm_set_epi32(0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000) );
	const __m128 one_ps  = gmx_castsi128_ps( _mm_set_epi32(0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000) ); 
	const __m128 off_ps  = gmx_castsi128_ps( _mm_set_epi32(0x3FBF8000, 0x3FBF8000, 0x3FBF8000, 0x3FBF8000) ); 
	const __m128 mant_ps = gmx_castsi128_ps( _mm_set_epi32(0x007FFFFF, 0x007FFFFF, 0x007FFFFF, 0x007FFFFF) );
	const __m128 sign_ps = gmx_castsi128_ps( _mm_set_epi32(0x80000000, 0x80000000, 0x80000000, 0x80000000) );
	const __m128 base_ps = gmx_castsi128_ps( _mm_set_epi32(0x43800000, 0x43800000, 0x43800000, 0x43800000) );
	const __m128 loge_ps = gmx_castsi128_ps( _mm_set_epi32(0x3F317218, 0x3F317218, 0x3F317218, 0x3F317218) );
	
	const __m128 D5      = gmx_castsi128_ps( _mm_set_epi32(0xBD0D0CC5, 0xBD0D0CC5, 0xBD0D0CC5, 0xBD0D0CC5) );
	const __m128 D4      = gmx_castsi128_ps( _mm_set_epi32(0x3EA2ECDD, 0x3EA2ECDD, 0x3EA2ECDD, 0x3EA2ECDD) ); 
	const __m128 D3      = gmx_castsi128_ps( _mm_set_epi32(0xBF9dA2C9, 0xBF9dA2C9, 0xBF9dA2C9, 0xBF9dA2C9) );
	const __m128 D2      = gmx_castsi128_ps( _mm_set_epi32(0x4026537B, 0x4026537B, 0x4026537B, 0x4026537B) );
	const __m128 D1      = gmx_castsi128_ps( _mm_set_epi32(0xC054bFAD, 0xC054bFAD, 0xC054bFAD, 0xC054bFAD) ); 
	const __m128 D0      = gmx_castsi128_ps( _mm_set_epi32(0x4047691A, 0x4047691A, 0x4047691A, 0x4047691A) );
	
	__m128  xmm0,xmm1,xmm2;
	__m128i xmm1i;
	
	xmm0  = x;
	xmm1  = xmm0;
	xmm1  = _mm_and_ps(xmm1, exp_ps);
	xmm1 = gmx_castsi128_ps( _mm_srli_epi32( gmx_castps_si128(xmm1),8) ); 
	
	xmm1  = _mm_or_ps(xmm1, one_ps);
	xmm1  = _mm_sub_ps(xmm1, off_ps);
	
	xmm1  = _mm_mul_ps(xmm1, base_ps);
	xmm0  = _mm_and_ps(xmm0, mant_ps);
	xmm0  = _mm_or_ps(xmm0, one_ps);
	
	xmm2  = _mm_mul_ps(xmm0, D5);
	xmm2  = _mm_add_ps(xmm2, D4);
	xmm2  = _mm_mul_ps(xmm2,xmm0);
	xmm2  = _mm_add_ps(xmm2, D3);
	xmm2  = _mm_mul_ps(xmm2,xmm0);
	xmm2  = _mm_add_ps(xmm2, D2);
	xmm2  = _mm_mul_ps(xmm2,xmm0);
	xmm2  = _mm_add_ps(xmm2, D1);
	xmm2  = _mm_mul_ps(xmm2,xmm0);
	xmm2  = _mm_add_ps(xmm2, D0);
	xmm0  = _mm_sub_ps(xmm0, one_ps);
	xmm0  = _mm_mul_ps(xmm0,xmm2);
	xmm1  = _mm_add_ps(xmm1,xmm0);
	
	x     = xmm1;
	x  = _mm_mul_ps(x, loge_ps);
	
    return x;
}


int 
calc_gb_rad_still_sse(t_commrec *cr, t_forcerec *fr,int natoms, gmx_localtop_t *top,
					  const t_atomtypes *atype, float *x, t_nblist *nl, gmx_genborn_t *born, t_mdatoms *md)
{
	int i,k,n,ai,ai3,aj1,aj2,aj3,aj4,nj0,nj1,offset;
	int aj13,aj23,aj33,aj43;

	float gpi_ai,gpi2;
	float factor;
	
	__m128 ix,iy,iz;
	__m128 jx,jy,jz;
	__m128 dx,dy,dz;
	__m128 t1,t2,t3;
	__m128 rsq11,rinv,rinv2,rinv4,rinv6;
	__m128 ratio,gpi,rai,raj,vai,vaj,rvdw,mask_cmp;
	__m128 ccf,dccf,theta,cosq,term,sinq,res,prod,prod_ai;
	__m128 xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8; 	
	__m128 mask,icf4,icf6;
	
	const __m128 half   = {0.5f , 0.5f , 0.5f , 0.5f };
	const __m128 three  = {3.0f , 3.0f , 3.0f , 3.0f };
	const __m128 one    = {1.0f,  1.0f , 1.0f , 1.0f };
	const __m128 two    = {2.0f , 2.0f , 2.0f,  2.0f };
	const __m128 zero   = {0.0f , 0.0f , 0.0f , 0.0f };
	const __m128 four   = {4.0f , 4.0f , 4.0f , 4.0f };
	
	const __m128 p5inv  = {STILL_P5INV, STILL_P5INV, STILL_P5INV, STILL_P5INV};
	const __m128 pip5   = {STILL_PIP5,  STILL_PIP5,  STILL_PIP5,  STILL_PIP5};
	const __m128 p4     = {STILL_P4,    STILL_P4,    STILL_P4,    STILL_P4};
		
	factor  = 0.5 * ONE_4PI_EPS0;
		
	/* keep the compiler happy */
	t1   = _mm_setzero_ps();
	t2   = _mm_setzero_ps();
	t3   = _mm_setzero_ps();
	xmm1 = _mm_setzero_ps();
	xmm2 = _mm_setzero_ps();
	xmm3 = _mm_setzero_ps();
	xmm4 = _mm_setzero_ps();
	
	aj1  = aj2  = aj3  = aj4  = 0;
	aj13 = aj23 = aj33 = aj43 = 0;
	n = 0;
		
	for(i=0;i<born->nr;i++)
	{
		born->gpol_still_work[i]=0;
	}

	for(i=0;i<nl->nri;i++)
	{
		ai      = nl->iinr[i];
		
		ai3	    = ai*3;
		
		nj0     = nl->jindex[ai];
		nj1     = nl->jindex[ai+1];
	 	
		offset  = (nj1-nj0)%4;
		
		/* Polarization energy for atom ai */
		gpi     = _mm_setzero_ps();
		
		/* Load particle ai coordinates */
		ix      = _mm_set1_ps(x[ai3]);
		iy      = _mm_set1_ps(x[ai3+1]);
		iz      = _mm_set1_ps(x[ai3+2]);
		
		/* Load particle ai gb_radius */
		rai     = _mm_set1_ps(top->atomtypes.gb_radius[md->typeA[ai]]);
		vai     = _mm_set1_ps(born->vsolv[ai]);
		prod_ai = _mm_mul_ps(p4,vai);
							  
		for(k=nj0;k<nj1-offset;k+=4)
		{
			aj1 = nl->jjnr[k];	 
			aj2 = nl->jjnr[k+1];
			aj3 = nl->jjnr[k+2];
			aj4 = nl->jjnr[k+3];
						
			aj13 = aj1 * 3; 
			aj23 = aj2 * 3;
			aj33 = aj3 * 3;
			aj43 = aj4 * 3;
			
			/* Load particle aj1-4 and transpose */
			xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13));
			xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23));
			xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33));
			xmm4 = _mm_loadh_pi(xmm4,(__m64 *) (x+aj43));
			
			xmm5    = _mm_load1_ps(x+aj13+2);  
			xmm6    = _mm_load1_ps(x+aj23+2); 
			xmm7    = _mm_load1_ps(x+aj33+2); 
			xmm8    = _mm_load1_ps(x+aj43+2);
			
			xmm5    = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
			xmm6    = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(0,0,0,0));
			jz      = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(2,0,2,0));
			
			xmm1    = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
			xmm2    = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(3,2,3,2));
			jx      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(2,0,2,0));
			jy      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,1,3,1));
			
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare */
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			rinv2     = _mm_mul_ps(rinv,rinv);
			rinv4     = _mm_mul_ps(rinv2,rinv2);
			rinv6     = _mm_mul_ps(rinv2,rinv4);
			
			xmm1 = _mm_load_ss(born->vsolv+aj1); /*see comment at invsqrta*/
			xmm2 = _mm_load_ss(born->vsolv+aj2); 
			xmm3 = _mm_load_ss(born->vsolv+aj3); 
			xmm4 = _mm_load_ss(born->vsolv+aj4);
			
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
			xmm3 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j4 j4*/
			vaj  = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
			
			raj       = _mm_set_ps(top->atomtypes.gb_radius[md->typeA[aj4]],
								   top->atomtypes.gb_radius[md->typeA[aj3]],
								   top->atomtypes.gb_radius[md->typeA[aj2]],
								   top->atomtypes.gb_radius[md->typeA[aj1]]);
			
			rvdw      = _mm_add_ps(rai,raj); 
			rvdw      = _mm_mul_ps(rvdw,rvdw);
			ratio     = _mm_div_ps(rsq11,rvdw); /*ratio = dr2/(rvdw*rvdw)*/
			
			mask_cmp  = _mm_cmpgt_ps(ratio,p5inv); /*if ratio>p5inv */
			
			switch(_mm_movemask_ps(mask_cmp))
			{
				case 0xF:
					ccf  = one;
					dccf = zero;
					break;
				default:
					
					theta	  = _mm_mul_ps(ratio,pip5);
					sincos_ps(theta,&sinq,&cosq); /* sine and cosine	*/			
					term      = _mm_sub_ps(one,cosq); /*1-cosq*/
					term      = _mm_mul_ps(half,term); /*0.5*(1.0-cosq)*/
					ccf	      = _mm_mul_ps(term,term); /* term*term */
					dccf      = _mm_mul_ps(two,term); /* 2 * term */
					dccf      = _mm_mul_ps(dccf,sinq); /* 2*term*sinq */
					dccf      = _mm_mul_ps(dccf,theta);
					
					ccf	      = _mm_or_ps(_mm_and_ps(mask_cmp,one)  ,_mm_andnot_ps(mask_cmp,ccf)); /*conditional as a mask*/
					dccf      = _mm_or_ps(_mm_and_ps(mask_cmp,zero) ,_mm_andnot_ps(mask_cmp,dccf));
			}
			
			prod      = _mm_mul_ps(p4,vaj);	
			icf4      = _mm_mul_ps(ccf,rinv4);
			xmm2      = _mm_mul_ps(icf4,prod); /*prod*ccf*idr4 */
			xmm3      = _mm_mul_ps(icf4,prod_ai);
			gpi		  = _mm_add_ps(gpi,xmm2); /* gpi = gpi + prod*ccf*idr4	*/
			
			/* Load, subtract and store atom aj gpol energy */
			xmm5      = _mm_load1_ps(born->gpol_still_work+aj1);
			xmm6      = _mm_load1_ps(born->gpol_still_work+aj2);
			xmm7      = _mm_load1_ps(born->gpol_still_work+aj3);
			xmm8      = _mm_load1_ps(born->gpol_still_work+aj4);
			
			xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); /* aj1 aj1 aj2 aj2 */
			xmm6 = _mm_shuffle_ps(xmm7,xmm8, _MM_SHUFFLE(0,0,0,0)); /* aj3 aj3 aj4 aj4 */
			xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); /* aj1 aj2 aj3 aj4 */
			
			xmm3 = _mm_add_ps(xmm7,xmm3);
			
			_mm_store_ss(born->gpol_still_work+aj1,xmm3);
			xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_still_work+aj2,xmm3);
			xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_still_work+aj3,xmm3);
			xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_still_work+aj4,xmm3);
			
			/* Chain rule terms */
			ccf       = _mm_mul_ps(four,ccf);
			xmm3      = _mm_sub_ps(ccf,dccf);
			icf6      = _mm_mul_ps(xmm3,rinv6); /* (4*ccf-dccf)*rinv6, icf6 */
			xmm1      = _mm_mul_ps(icf6,prod);
			xmm2      = _mm_mul_ps(icf6,prod_ai);
			
			/* Here we need to do some unpacking to avoid 8 separate store operations 
			 * The idea is to get terms ai->aj1, aj1->ai, ai->aj2, aj2->ai in xmm3
			 and then ai->aj3, aj3->ai, ai->aj4, aj4->ai in xmm4
			 */
			xmm3 = _mm_unpacklo_ps(xmm1,xmm2);
			xmm4 = _mm_unpackhi_ps(xmm1,xmm2);
					
			_mm_storeu_ps(fr->dadx+n, xmm3);
			n = n + 4;
			_mm_storeu_ps(fr->dadx+n, xmm4);
			n = n + 4;
		}
		
		/* deal with odd elements*/
		if(offset!=0)
		{
			aj1=aj2=aj3=aj4=0;
			
			if(offset==1)
			{
				aj1   = nl->jjnr[k];	 /*jnr1-4*/
				aj13  = aj1 * 3; /*Replace jnr with j3*/
								
				xmm1  = _mm_loadl_pi(xmm1,(__m64 *) (x+aj13));
				xmm5  = _mm_load1_ps(x+aj13+2);
				
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(0,0,0,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(1,1,1,1));
				
				raj   = _mm_set_ps(0.0f, 0.0f, 0.0f, top->atomtypes.gb_radius[md->typeA[aj1]]); 
				vaj   = _mm_set_ps(0.0f, 0.0f, 0.0f, born->vsolv[aj1]);				   
				
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0,0xffffffff) );
			}
			else if(offset==2)
			{
				aj1 = nl->jjnr[k];	 
				aj2 = nl->jjnr[k+1];
				
				aj13 = aj1 * 3; 
				aj23 = aj2 * 3;
				
				xmm1  = _mm_loadh_pi(xmm1, (__m64 *) (x+aj13));
				xmm2  = _mm_loadh_pi(xmm2, (__m64 *) (x+aj23));
				
				xmm5  = _mm_load1_ps(x+aj13+2);
				xmm6  = _mm_load1_ps(x+aj23+2);
				
				xmm5  = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
				xmm5  = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0));
				
				xmm1  = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,3,1));
								
				raj  = _mm_set_ps(0.0f, 0.0f, top->atomtypes.gb_radius[md->typeA[aj2]],top->atomtypes.gb_radius[md->typeA[aj1]]); 
				vaj  = _mm_set_ps(0.0f, 0.0f, born->vsolv[aj2], born->vsolv[aj1]);		
				
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0xffffffff,0xffffffff) );
				
			}
			else
			{
				aj1 = nl->jjnr[k];	 
				aj2 = nl->jjnr[k+1];
				aj3 = nl->jjnr[k+2];
				
				aj13 = aj1 * 3; 
				aj23 = aj2 * 3;
				aj33 = aj3 * 3;
				
				xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13)); 
				xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23)); 
				xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33)); 
				
				xmm5 = _mm_load1_ps(x+aj13+2); 
				xmm6 = _mm_load1_ps(x+aj23+2); 
				xmm7 = _mm_load1_ps(x+aj33+2); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0));
				xmm5 = _mm_shuffle_ps(xmm5,xmm7, _MM_SHUFFLE(3,1,3,1));						
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,2,3,2));
				xmm2 = _mm_shuffle_ps(xmm3,xmm3, _MM_SHUFFLE(3,2,3,2));
				
				xmm6 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(2,0,2,0)); 
				xmm4 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,1,3,1));
				
				
				raj  = _mm_set_ps(0.0f, 
								  top->atomtypes.gb_radius[md->typeA[aj3]],
								  top->atomtypes.gb_radius[md->typeA[aj2]],
								  top->atomtypes.gb_radius[md->typeA[aj1]]);
				
				vaj  = _mm_set_ps(0.0f, 
								  born->vsolv[aj3], 
								  born->vsolv[aj2], 
								  born->vsolv[aj1]);	
				
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0xffffffff,0xffffffff,0xffffffff) );
			}
			
			jx = _mm_and_ps( mask, xmm6);
			jy = _mm_and_ps( mask, xmm4);
			jz = _mm_and_ps( mask, xmm5);
			
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare*/
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			rinv2     = _mm_mul_ps(rinv,rinv);
			rinv4     = _mm_mul_ps(rinv2,rinv2);
			rinv6     = _mm_mul_ps(rinv2,rinv4);
			
			rvdw      = _mm_add_ps(rai,raj); 
			rvdw      = _mm_mul_ps(rvdw,rvdw);
			ratio     = _mm_div_ps(rsq11,rvdw); /*ratio = dr2/(rvdw*rvdw)*/
			
			mask_cmp  = _mm_cmpgt_ps(ratio,p5inv); /*if ratio>p5inv*/
				
			switch(_mm_movemask_ps(mask_cmp))
			{
				case 0xF:
					ccf  = one;
					dccf = zero;
					break;
				default:
					
					theta	  = _mm_mul_ps(ratio,pip5); /* ratio * STILL_PIP5 */
					sincos_ps(theta,&sinq,&cosq); 
					term      = _mm_sub_ps(one,cosq);  /* 1.0 - cosq */
					term      = _mm_mul_ps(half,term); /* term = 0.5*(1-cosq) */
					ccf	      = _mm_mul_ps(term,term); /* ccf  = term*term */
					dccf      = _mm_mul_ps(two,term);  /* dccf = 2.0* term */
					dccf      = _mm_mul_ps(dccf,sinq); /* dccf = 2.0*term *sinq */
					dccf      = _mm_mul_ps(dccf,theta);
					
					ccf	      = _mm_or_ps(_mm_and_ps(mask_cmp,one)  ,_mm_andnot_ps(mask_cmp,ccf)); /*conditional as a mask*/
					dccf      = _mm_or_ps(_mm_and_ps(mask_cmp,zero) ,_mm_andnot_ps(mask_cmp,dccf));
			}
			
			prod      = _mm_mul_ps(p4,vaj);	
			icf4      = _mm_mul_ps(ccf,rinv4);
			xmm2      = _mm_mul_ps(icf4,prod); /* prod*ccf*idr4*/
			xmm3      = _mm_mul_ps(icf4,prod_ai);
			
			xmm2      = _mm_and_ps(mask, xmm2);
			xmm3      = _mm_and_ps(mask, xmm3);
			gpi       = _mm_add_ps(gpi,xmm2);  /*gpi = gpi + prod*ccf*idr4 */
			
			/* Store pol energy */
			if(offset==1)
			{
				xmm7 = _mm_load1_ps(born->gpol_still_work+aj1);
				xmm3  = _mm_add_ps(xmm7,xmm3);
				_mm_store_ss(born->gpol_still_work+aj1,xmm3);
				
				/* Chain rule terms */
				ccf       = _mm_mul_ps(four,ccf);
				xmm3      = _mm_sub_ps(ccf,dccf);
				icf6      = _mm_mul_ps(xmm3,rinv6);
				xmm1      = _mm_mul_ps(icf6, prod);
				xmm2      = _mm_mul_ps(icf6, prod_ai);
				
				xmm1      = _mm_and_ps( mask, xmm1); /* ? */
				xmm2      = _mm_and_ps( mask, xmm2); /* ? */
				
				_mm_storeu_ps(fr->dadx+n, xmm1); 
				n = n + 1;
				_mm_storeu_ps(fr->dadx+n, xmm2);
				n = n + 1;
			}
			else if(offset==2)
			{
				xmm5 = _mm_load1_ps(born->gpol_still_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_still_work+aj2); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0)); 
				
				xmm3  = _mm_add_ps(xmm7,xmm3);
				
				_mm_store_ss(born->gpol_still_work+aj1,xmm3);
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_still_work+aj2,xmm3);
				
				/* Chain rule terms */
				ccf       = _mm_mul_ps(four,ccf);
				xmm3      = _mm_sub_ps(ccf,dccf);
				icf6      = _mm_mul_ps(xmm3,rinv6);
				xmm1      = _mm_mul_ps(icf6, prod);
				xmm2      = _mm_mul_ps(icf6, prod_ai);
				
				xmm1      = _mm_and_ps( mask, xmm1);
				xmm2      = _mm_and_ps( mask, xmm2);
				
				xmm3 = _mm_unpacklo_ps(xmm1,xmm2); /* Same idea as above */
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				/* Here we advance by 2*offset, since all four values fit in one xmm variable and
				 * can be stored all at once */
				n = n + 4; 
			}
			else
			{
				xmm5 = _mm_load1_ps(born->gpol_still_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_still_work+aj2); 
				xmm7 = _mm_load1_ps(born->gpol_still_work+aj3); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); 
				xmm6 = _mm_shuffle_ps(xmm7,xmm7, _MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); 
				
				xmm3  = _mm_add_ps(xmm7,xmm3);
				
				_mm_store_ss(born->gpol_still_work+aj1,xmm3);
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_still_work+aj2,xmm3);
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_still_work+aj3,xmm3);
				
				/* Chain rule terms */
				ccf       = _mm_mul_ps(four,ccf);
				xmm3      = _mm_sub_ps(ccf,dccf);
				icf6      = _mm_mul_ps(xmm3,rinv6);
				xmm1      = _mm_mul_ps(icf6, prod);
				xmm2      = _mm_mul_ps(icf6, prod_ai);
				
				xmm1      = _mm_and_ps( mask, xmm1);
				xmm2      = _mm_and_ps( mask, xmm2);
				
				xmm3 = _mm_unpacklo_ps(xmm1,xmm2); /* Same idea as above, but extra shuffles because of odd elements */
				xmm4 = _mm_unpackhi_ps(xmm1,xmm2);
				xmm4 = _mm_shuffle_ps(xmm4,xmm3,_MM_SHUFFLE(3,3,1,0));
				xmm4 = _mm_shuffle_ps(xmm4,xmm4,_MM_SHUFFLE(1,1,0,3));
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				n = n + offset;
				_mm_storeu_ps(fr->dadx+n, xmm4);
				n = n + offset;
			}
		} 
		
		/* gpi now contains four partial terms that need to be added to particle ai gpi*/
		xmm2  = _mm_movehl_ps(xmm2,gpi);
		gpi   = _mm_add_ps(gpi,xmm2);
		xmm2  = _mm_shuffle_ps(gpi,gpi,_MM_SHUFFLE(1,1,1,1));
		gpi   = _mm_add_ss(gpi,xmm2);
		
		xmm2 = _mm_load1_ps(born->gpol_still_work+ai);
		gpi  = _mm_add_ss(gpi,xmm2);
		_mm_store_ss(born->gpol_still_work+ai,gpi);
	}
	
	/* Parallel summations */
	if(PARTDECOMP(cr))
	{
		gmx_sum(natoms, born->gpol_still_work, cr);
	}
	else if(DOMAINDECOMP(cr))
	{
		dd_atom_sum_real(cr->dd, born->gpol_still_work);
	}
	
	/* Compute the radii */
	for(i=0;i<nl->nri;i++)
	{		
		ai               = nl->iinr[i];
		gpi_ai           = born->gpol[ai] + born->gpol_still_work[ai]; /* add gpi to the initial pol energy gpi_ai*/
		gpi2             = gpi_ai * gpi_ai;
		born->bRad[ai]   = factor*invsqrt(gpi2);
		fr->invsqrta[ai] = invsqrt(born->bRad[ai]);
	}
	
	/* Extra (local) communication required for DD */
	if(DOMAINDECOMP(cr))
	{
		dd_atom_spread_real(cr->dd, born->bRad);
		dd_atom_spread_real(cr->dd, fr->invsqrta);
	}

	return 0;
	
}

int 
calc_gb_rad_hct_sse(t_commrec *cr, t_forcerec *fr, int natoms, gmx_localtop_t *top, const t_atomtypes *atype, float *x,
					t_nblist *nl, gmx_genborn_t *born, t_mdatoms *md)
{
	int i,k,n,ai,ai3,aj1,aj2,aj3,aj4,nj0,nj1,at0,at1;
	int aj13,aj23,aj33,aj43,p1,p2,p3,p4;
	int offset;
	float ri,rr,sum,sum_tmp,min_rad,rad;
	float doff;
	float *sum_mpi;
	
	__m128 ix,iy,iz,jx,jy,jz;
	__m128 dx,dy,dz,t1,t2,t3;
	__m128 rsq11,rinv,r,rai;
	__m128 rai_inv,sk,sk2,lij,dlij,duij;
	__m128 uij,lij2,uij2,lij3,uij3,diff2;
	__m128 sk_ai, sk2_ai,raj,raj_inv,doffset;
	__m128 lij_inv,sk2_inv,prod,log_term,tmp,tmp_sum;
	__m128 sum_ai,chrule, chrule_ai,tmp_ai;
	__m128 xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8,xmm9;
	__m128 mask_cmp,mask_cmp2,mask_cmp3;
	
	__m128 maski;
	
	const __m128 neg   = {-1.0f , -1.0f , -1.0f , -1.0f };
	const __m128 zero  = {0.0f , 0.0f , 0.0f , 0.0f };
	const __m128 eigth = {0.125f , 0.125f , 0.125f , 0.125f };
	const __m128 qrtr  = {0.25f , 0.25f , 0.25f , 0.25f };
	const __m128 half  = {0.5f , 0.5f , 0.5f , 0.5f };
	const __m128 one   = {1.0f , 1.0f , 1.0f , 1.0f };
	const __m128 two   = {2.0f , 2.0f , 2.0f , 2.0f };
	const __m128 three = {3.0f , 3.0f , 3.0f , 3.0f };

	/* Keep the compiler happy */
	tmp    = _mm_setzero_ps();
	tmp_ai = _mm_setzero_ps();
	xmm1   = _mm_setzero_ps();
	xmm2   = _mm_setzero_ps();
	xmm3   = _mm_setzero_ps();
	xmm4   = _mm_setzero_ps();
	
	aj1=aj2=aj3=aj4=0;
	aj13=aj23=aj33=aj43=0;
	p1=p2=p3=p4=0;
	n=0;
	
	/* Set the dielectric offset */
	doff    = born->gb_doffset;
	doffset = _mm_load1_ps(&doff);
	
	for(i=0;i<born->nr;i++)
	{
		born->gpol_hct_work[i] = 0;
	}
	
	for(i=0;i<nl->nri;i++)
	{
		ai  = nl->iinr[i];
		ai3 = ai*3;
		
		nj0 = nl->jindex[ai];
		nj1 = nl->jindex[ai+1];
		
		offset = (nj1-nj0)%4;
	
		/* Load rai */
		rr      = top->atomtypes.gb_radius[md->typeA[ai]]-doff;
		rai     = _mm_load1_ps(&rr);
		rr      = 1.0/rr;
		rai_inv = _mm_load1_ps(&rr);
				
		/* Zero out sums */
		sum_ai  = _mm_setzero_ps();
		
		/* Load ai coordinates*/
		ix = _mm_load1_ps(x+ai3);
		iy = _mm_load1_ps(x+ai3+1);
		iz = _mm_load1_ps(x+ai3+2);
		
		sk_ai  = _mm_load1_ps(born->param+ai);
		sk2_ai = _mm_mul_ps(sk_ai,sk_ai);
	
		for(k=nj0;k<nj1-offset;k+=4)
		{
			aj1 = nl->jjnr[k];	
			aj2 = nl->jjnr[k+1];
			aj3 = nl->jjnr[k+2];
			aj4 = nl->jjnr[k+3];
			
			aj13 = aj1 * 3; 
			aj23 = aj2 * 3;
			aj33 = aj3 * 3;
			aj43 = aj4 * 3;
			
			/* Load particle aj1-4 and transpose*/
			xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13));
			xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23));
			xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33));
			xmm4 = _mm_loadh_pi(xmm4,(__m64 *) (x+aj43));
			
			xmm5    = _mm_load1_ps(x+aj13+2);  
			xmm6    = _mm_load1_ps(x+aj23+2); 
			xmm7    = _mm_load1_ps(x+aj33+2); 
			xmm8    = _mm_load1_ps(x+aj43+2);
						
			xmm5    = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
			xmm6    = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(0,0,0,0));
			jz      = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(2,0,2,0));
			
			xmm1    = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
			xmm2    = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(3,2,3,2));
			jx      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(2,0,2,0));
			jy      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,1,3,1));
						
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare*/
				
			/* Load raj aj1-4 */
			p1 = md->typeA[aj1];
			p2 = md->typeA[aj2];
			p3 = md->typeA[aj3];
			p4 = md->typeA[aj4];
			
			xmm1 = _mm_load_ss(top->atomtypes.gb_radius+p1); 
			xmm2 = _mm_load_ss(top->atomtypes.gb_radius+p2);  
			xmm3 = _mm_load_ss(top->atomtypes.gb_radius+p3); 
			xmm4 = _mm_load_ss(top->atomtypes.gb_radius+p4);
			
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
			xmm3 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j4 j4*/
			raj  = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
			raj  = _mm_sub_ps(raj,doffset);
			
			/* Compute raj_inv aj1-4 */
			xmm3      = _mm_rcp_ps(raj); /*1.0/(raj), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,raj);
			t1        = _mm_sub_ps(two,t1);
			raj_inv   = _mm_mul_ps(t1,xmm3);
			
			/* Perform reciprocal square root lookup, 8 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration to get 12 bits correct*/
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			r         = _mm_mul_ps(rinv,rsq11);
			
			xmm1 = _mm_load_ss(born->param+aj1); 
			xmm2 = _mm_load_ss(born->param+aj2);  
			xmm3 = _mm_load_ss(born->param+aj3); 
			xmm4 = _mm_load_ss(born->param+aj4);
					
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
			xmm3 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j4 j4*/
			sk   = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
			
			/* INTERACTION aj->ai STARTS HERE */
			/* conditional mask for rai<dr+sk */ 
			xmm1      = _mm_add_ps(r,sk); 
			mask_cmp  = _mm_cmplt_ps(rai,xmm1);
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk); /*xmm2 = dr-sk*/
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
			
			mask_cmp2 = _mm_cmpgt_ps(rai,xmm2); /*rai>dr-sk */

			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,rai_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));

			uij		= _mm_rcp_ps(xmm1); /* better approximation than just _mm_rcp_ps, which is just 8 bits*/
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
					
			diff2   = _mm_sub_ps(uij2,lij2);
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = _mm_mul_ps(sk,sk);
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
				
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
			
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /* 0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /* 0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm9    = _mm_mul_ps(neg,diff2); /* (-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm9,prod); /* (-1)*diff2*prod */
			tmp_ai  = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */
			xmm3    = _mm_sub_ps(sk,r);
			mask_cmp3 = _mm_cmplt_ps(rai,xmm3); /* rai<sk-dr */
			
			xmm4    = _mm_sub_ps(rai_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(tmp_ai,xmm4);
					
			tmp_ai	    = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp_ai)); /*conditional as a mask*/
					
			/* the tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask. */
			tmp_ai     = _mm_mul_ps(half,tmp_ai); /* 0.5*tmp */
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp_ai)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			sum_ai     = _mm_add_ps(sum_ai,tmp_ai);
		
			xmm2   = _mm_mul_ps(half,lij2); 
			xmm3   = _mm_mul_ps(prod,lij3); 
			xmm2   = _mm_add_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(lij,rinv); 
			xmm4   = _mm_mul_ps(lij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t1     = _mm_sub_ps(xmm2,xmm3); 
					
			xmm2   = _mm_mul_ps(half,uij2);
			xmm2   = _mm_mul_ps(neg,xmm2); 
			xmm3   = _mm_mul_ps(qrtr,sk2_inv);
			xmm3   = _mm_mul_ps(xmm3,uij3); 
			xmm2   = _mm_sub_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(uij,rinv); 
			xmm4   = _mm_mul_ps(uij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t2     = _mm_add_ps(xmm2,xmm3); 
					
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm9); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
			
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /* dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2); /* dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); 
			
			/* temporary storage of chain rule terms, since we have to compute
			 the reciprocal terms also before storing them */
			chrule = _mm_mul_ps(xmm2,rinv); 			
					
			/* INTERACTION ai->aj STARTS HERE */
			/* conditional mask for raj<dr+sk_ai */
			xmm1      = _mm_add_ps(r,sk_ai); 
			mask_cmp  = _mm_cmplt_ps(raj,xmm1); 
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk_ai); /*xmm2 = dr-sk_ai*/
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk_ai), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
			
			mask_cmp2 = _mm_cmpgt_ps(raj,xmm2); /*raj>dr-sk_ai */
			
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,raj_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));
			
			uij		= _mm_rcp_ps(xmm1); /* better approximation than just _mm_rcp_ps, which is just 8 bits*/
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
			
			diff2   = _mm_sub_ps(uij2,lij2);
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = sk2_ai;
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
			
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
			
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /* 0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /* 0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm9    = _mm_mul_ps(neg,diff2); /* (-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm9,prod); /* (-1)*diff2*prod */
			tmp     = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */
			xmm3    = _mm_sub_ps(sk_ai,r);
			mask_cmp3 = _mm_cmplt_ps(raj,xmm3); /* rai<sk-dr */
			
			xmm4    = _mm_sub_ps(raj_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(tmp,xmm4);
			
			tmp	    = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp)); /*conditional as a mask*/
			
			/* the tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask. */
			tmp     = _mm_mul_ps(half,tmp); /* 0.5*tmp */
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
		
			/* Load, add and store ai->aj pol energy */
			xmm5 = _mm_load_ss(born->gpol_hct_work+aj1); 
			xmm6 = _mm_load_ss(born->gpol_hct_work+aj2); 
			xmm7 = _mm_load_ss(born->gpol_hct_work+aj3); 
			xmm8 = _mm_load_ss(born->gpol_hct_work+aj4); 
			
			xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); 
			xmm6 = _mm_shuffle_ps(xmm7,xmm8, _MM_SHUFFLE(0,0,0,0)); 
			xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); 
			
			xmm7 = _mm_add_ps(xmm7,tmp);
			
			_mm_store_ss(born->gpol_hct_work+aj1,xmm7); /* aj1 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_hct_work+aj2,xmm7); /* aj2 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_hct_work+aj3,xmm7); /* aj3 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1)); 
			_mm_store_ss(born->gpol_hct_work+aj4,xmm7); /* aj4 */
			
			/* duij   = one; */
			xmm2   = _mm_mul_ps(half,lij2); 
			xmm3   = _mm_mul_ps(prod,lij3); 
			xmm2   = _mm_add_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(lij,rinv); 
			xmm4   = _mm_mul_ps(lij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t1     = _mm_sub_ps(xmm2,xmm3); 
			
			xmm2   = _mm_mul_ps(half,uij2);
			xmm2   = _mm_mul_ps(neg,xmm2); 
			xmm3   = _mm_mul_ps(qrtr,sk2_inv);
			xmm3   = _mm_mul_ps(xmm3,uij3); 
			xmm2   = _mm_sub_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(uij,rinv); 
			xmm4   = _mm_mul_ps(uij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t2     = _mm_add_ps(xmm2,xmm3); 
			
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm9); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
						
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /*dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2);/*dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); 
			chrule_ai = _mm_mul_ps(xmm2,rinv);
			
			/* Here we need to do some unpacking to avoid 8 separate store operations 
			 * The idea is to get terms ai->aj1, aj1->ai, ai->aj2, aj2->ai in xmm3
			 and then ai->aj3, aj3->ai, ai->aj4, aj4->ai in xmm4
			 */
			xmm3 = _mm_unpacklo_ps(chrule,chrule_ai);
			xmm4 = _mm_unpackhi_ps(chrule,chrule_ai);
			
			_mm_storeu_ps(fr->dadx+n, xmm3);
			n = n + 4;
			_mm_storeu_ps(fr->dadx+n, xmm4);
			n = n + 4;
		} 
		
		if(offset!=0)
		{
			aj1=aj2=aj3=aj4=0;
			
			if(offset==1)
			{
				aj1   = nl->jjnr[k];
				aj13  = aj1 * 3;
				p1    = md->typeA[aj1];
				
				xmm1  = _mm_loadl_pi(xmm1,(__m64 *) (x+aj13));
				xmm5  = _mm_load1_ps(x+aj13+2);
				
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(0,0,0,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(1,1,1,1));
				
				sk    = _mm_load1_ps(born->param+aj1);
				
				raj   = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				raj   = _mm_sub_ps(raj,doffset);
				
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0,0,0xffffffff) );
			}
			else if(offset==2)
			{
				aj1   = nl->jjnr[k];
				aj2   = nl->jjnr[k+1];
				p1    = md->typeA[aj1];
				p2    = md->typeA[aj2];
				aj13  = aj1 * 3;
				aj23  = aj2 * 3;

				xmm1  = _mm_loadh_pi(xmm1, (__m64 *) (x+aj13));
				xmm2  = _mm_loadh_pi(xmm2, (__m64 *) (x+aj23));
				
				xmm5  = _mm_load1_ps(x+aj13+2);
				xmm6  = _mm_load1_ps(x+aj23+2);
				
				xmm5  = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
				xmm5  = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0));
				
				xmm1  = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,3,1));
				
				xmm1 = _mm_load1_ps(born->param+aj1);
				xmm2 = _mm_load1_ps(born->param+aj2);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0));
				sk   = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				
				xmm1 = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				xmm2 = _mm_load1_ps(top->atomtypes.gb_radius+p2);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0));
				raj  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				raj  = _mm_sub_ps(raj, doffset);
				
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0,0xffffffff,0xffffffff) );
			}
			else
			{
				aj1   = nl->jjnr[k];
				aj2   = nl->jjnr[k+1];
				aj3   = nl->jjnr[k+2];
				p1    = md->typeA[aj1];
				p2    = md->typeA[aj2];
				p3    = md->typeA[aj3];
				aj13  = aj1 * 3;
				aj23  = aj2 * 3;
				aj33  = aj3 * 3;
				
				xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13)); 
				xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23)); 
				xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33)); 
				
				xmm5 = _mm_load1_ps(x+aj13+2); 
				xmm6 = _mm_load1_ps(x+aj23+2); 
				xmm7 = _mm_load1_ps(x+aj33+2); 
											
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0));
				xmm5 = _mm_shuffle_ps(xmm5,xmm7, _MM_SHUFFLE(3,1,3,1));						
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,2,3,2));
				xmm2 = _mm_shuffle_ps(xmm3,xmm3, _MM_SHUFFLE(3,2,3,2));
				
				xmm6 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(2,0,2,0)); 
				xmm4 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,1,3,1));
			
				xmm1 = _mm_load1_ps(born->param+aj1);
				xmm2 = _mm_load1_ps(born->param+aj2);
				xmm3 = _mm_load1_ps(born->param+aj3);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j3 j3*/
				sk   = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
				
				xmm1 = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				xmm2 = _mm_load1_ps(top->atomtypes.gb_radius+p2);
				xmm3 = _mm_load1_ps(top->atomtypes.gb_radius+p3);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j3 j3*/
				raj  = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
				raj  = _mm_sub_ps(raj,doffset);
				
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0xffffffff,0xffffffff,0xffffffff) );
			}
			
			jx = _mm_and_ps( maski, xmm6);
			jy = _mm_and_ps( maski, xmm4);
			jz = _mm_and_ps( maski, xmm5);
			
			sk = _mm_and_ps ( maski, sk);
			sk_ai = _mm_and_ps( maski, sk_ai);
			
			/* Compute raj_inv offset for aj atoms */
			xmm3      = _mm_rcp_ps(raj); /*1.0/(raj), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,raj);
			t1        = _mm_sub_ps(two,t1);
			raj_inv   = _mm_mul_ps(t1,xmm3);
			raj_inv   = _mm_and_ps( maski,raj_inv); 
			
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare*/
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			r         = _mm_mul_ps(rinv,rsq11);
			
				   
			
			/* OFFSET INTERACTION aj->ai starts here */
			/* conditional mask for rai<dr+sk */
			xmm1      = _mm_add_ps(r,sk); /*dr+sk		*/		
			mask_cmp  = _mm_cmplt_ps(rai,xmm1);
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk); /*xmm2 = dr-sk */
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk)*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
									
			mask_cmp2 = _mm_cmpgt_ps(rai,xmm2); /*rai>dr-sk */
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,rai_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));

			uij		= _mm_rcp_ps(xmm1);
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
					
			diff2   = _mm_sub_ps(uij2,lij2);
			
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */

			sk2     = _mm_mul_ps(sk,sk);
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
				
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
							
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /*0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /*0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm9    = _mm_mul_ps(neg,diff2); /*(-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm9,prod); /*(-1)*diff2*prod */
			tmp_ai     = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */					
			xmm3    = _mm_sub_ps(sk,r);
			mask_cmp3 = _mm_cmplt_ps(rai,xmm3); /*rai<sk-dr*/
			
			xmm4    = _mm_sub_ps(rai_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(xmm1,xmm4);
					
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp_ai)); /*conditional as a mask*/
		
			/* tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask.*/ 
			tmp_ai     = _mm_mul_ps(half,tmp_ai); /*0.5*tmp*/
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp_ai)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			sum_ai  = _mm_add_ps(sum_ai,tmp_ai);
					
			/* start t1 */
			xmm2   = _mm_mul_ps(half,lij2); /*0.5*lij2 */
			xmm3   = _mm_mul_ps(prod,lij3); /*prod*lij3;*/
			xmm2   = _mm_add_ps(xmm2,xmm3); /*0.5*lij2+prod*lij3 */
			xmm3   = _mm_mul_ps(lij,rinv); /*lij*rinv */
			xmm4   = _mm_mul_ps(lij3,r); /*lij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*lij*rinv+lij3*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(lij*rinv+lij3*dr) */
			t1     = _mm_sub_ps(xmm2,xmm3); /* done t1 */
		
			/* start t2 */
			xmm2   = _mm_mul_ps(half,uij2); /*0.5*uij2 */
			xmm2   = _mm_mul_ps(neg,xmm2); /*(-1)*0.5*lij2 */
			xmm3   = _mm_mul_ps(qrtr,sk2_inv); /*0.25*sk2_rinv */
			xmm3   = _mm_mul_ps(xmm3,uij3); /*0.25*sk2_rinv*uij3 */
			xmm2   = _mm_sub_ps(xmm2,xmm3); /*(-1)*0.5*lij2-0.25*sk2_rinv*uij3 */
			xmm3   = _mm_mul_ps(uij,rinv); /*uij*rinv */
			xmm4   = _mm_mul_ps(uij3,r); /*uij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*uij*rinv+uij*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(uij*rinv+uij*dr) */
			t2     = _mm_add_ps(xmm2,xmm3); /* done t2 */
					
			/* start t3 */
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv; */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm9); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
						 
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /*dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2);/*dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); /*everyhting * t3 */
			xmm2   = _mm_mul_ps(xmm2,rinv); /*everything * t3 *rinv */
			chrule = _mm_and_ps(maski,xmm2);
			
			
			/* OFFSET INTERACTION ai->aj starts here */
			/* conditional mask for raj<dr+sk */
			xmm1      = _mm_add_ps(r,sk_ai); /*dr+sk		*/		
			mask_cmp  = _mm_cmplt_ps(raj,xmm1); 
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk_ai); /*xmm2 = dr-sk */
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk)*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
			
			mask_cmp2 = _mm_cmpgt_ps(raj,xmm2); /*rai>dr-sk */
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,raj_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));
			
			uij		= _mm_rcp_ps(xmm1);
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
			
			diff2   = _mm_sub_ps(uij2,lij2);
			
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = sk2_ai;
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
			
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
			
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /*0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /*0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm8    = _mm_mul_ps(neg,diff2); /*(-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm8,prod); /*(-1)*diff2*prod */
			tmp     = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */					
			xmm3    = _mm_sub_ps(sk_ai,r);
			mask_cmp3 = _mm_cmplt_ps(raj,xmm3); /*rai<sk-dr*/
			
			xmm4    = _mm_sub_ps(raj_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(xmm1,xmm4);
			
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp)); /*conditional as a mask*/
			
			/* tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask.*/ 
			tmp     = _mm_mul_ps(half,tmp); /*0.5*tmp*/
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			
			/* start t1 */
			xmm2   = _mm_mul_ps(half,lij2); /*0.5*lij2 */
			xmm3   = _mm_mul_ps(prod,lij3); /*prod*lij3;*/
			xmm2   = _mm_add_ps(xmm2,xmm3); /*0.5*lij2+prod*lij3 */
			xmm3   = _mm_mul_ps(lij,rinv); /*lij*rinv */
			xmm4   = _mm_mul_ps(lij3,r); /*lij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*lij*rinv+lij3*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(lij*rinv+lij3*dr) */
			t1     = _mm_sub_ps(xmm2,xmm3); /* done t1 */
			
			/* start t2 */
			xmm2   = _mm_mul_ps(half,uij2); /*0.5*uij2 */
			xmm2   = _mm_mul_ps(neg,xmm2); /*(-1)*0.5*lij2 */
			xmm3   = _mm_mul_ps(qrtr,sk2_inv); /*0.25*sk2_rinv */
			xmm3   = _mm_mul_ps(xmm3,uij3); /*0.25*sk2_rinv*uij3 */
			xmm2   = _mm_sub_ps(xmm2,xmm3); /*(-1)*0.5*lij2-0.25*sk2_rinv*uij3 */
			xmm3   = _mm_mul_ps(uij,rinv); /*uij*rinv */
			xmm4   = _mm_mul_ps(uij3,r); /*uij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*uij*rinv+uij*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(uij*rinv+uij*dr) */
			t2     = _mm_add_ps(xmm2,xmm3); /* done t2 */
			
			/* start t3 */
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv; */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm8); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
			
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /*dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2);/*dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); /*everyhting * t3 */
			xmm2   = _mm_mul_ps(xmm2,rinv); /*everything * t3 *rinv */
			chrule_ai = _mm_and_ps(maski,xmm2);
			
			if(offset==1)
			{
				xmm7 = _mm_load1_ps(born->gpol_hct_work+aj1);
				xmm7  = _mm_add_ps(xmm7,tmp);
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				
				_mm_storeu_ps(fr->dadx+n, chrule); 
				n = n + 1;
				_mm_storeu_ps(fr->dadx+n, chrule_ai);
				n = n + 1;
			}
			else if(offset==2)
			{
				xmm5 = _mm_load1_ps(born->gpol_hct_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_hct_work+aj2); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0)); 
				
				xmm7  = _mm_add_ps(xmm7,tmp);
				
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj2,xmm7);
				
				xmm3 = _mm_unpacklo_ps(chrule,chrule_ai); /* Same idea as above */
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				/* Here we advance by 2*offset, since all four values fit in one xmm variable and
				 * can be stored all at once */
				n = n + 4; 
			}
			else
			{
				xmm5 = _mm_load1_ps(born->gpol_hct_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_hct_work+aj2); 
				xmm7 = _mm_load1_ps(born->gpol_hct_work+aj3); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); 
				xmm6 = _mm_shuffle_ps(xmm7,xmm7, _MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); 
				
				xmm7  = _mm_add_ps(xmm7,tmp);
				
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj2,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj3,xmm7);
				
				xmm3 = _mm_unpacklo_ps(chrule,chrule_ai); /* Same idea as above, but extra shuffles because of odd elements */
				xmm4 = _mm_unpackhi_ps(chrule,chrule_ai);
				xmm4 = _mm_shuffle_ps(xmm4,xmm3,_MM_SHUFFLE(3,3,1,0));
				xmm4 = _mm_shuffle_ps(xmm4,xmm4,_MM_SHUFFLE(1,1,0,3));
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				n = n + offset;
				_mm_storeu_ps(fr->dadx+n, xmm4);
				n = n + offset;
			}
		} /* end offset */

		/* the sum_ai array will contain partial values that need to be added together */
		tmp_ai     = _mm_movehl_ps(tmp_ai,sum_ai);
		sum_ai  = _mm_add_ps(sum_ai,tmp_ai);
		tmp_ai     = _mm_shuffle_ps(sum_ai,sum_ai,_MM_SHUFFLE(1,1,1,1));
		sum_ai  = _mm_add_ss(sum_ai,tmp_ai);
		
		xmm2    = _mm_load1_ps(born->gpol_hct_work+ai);
		sum_ai  = _mm_add_ss(sum_ai,xmm2);
		_mm_store_ss(born->gpol_hct_work+ai,sum_ai);
	}
	
	/* Parallel summations */
	if(PARTDECOMP(cr))
	{
		gmx_sum(natoms, born->gpol_hct_work, cr);
	}
	else if(DOMAINDECOMP(cr))
	{
		dd_atom_sum_real(cr->dd, born->gpol_hct_work);
	}
	
	/* Compute the radii */
	for(i=0;i<nl->nri;i++)
	{
		ai      = nl->iinr[i];
		rr      = top->atomtypes.gb_radius[md->typeA[ai]]-doff; 
		sum     = 1.0/rr - born->gpol_hct_work[ai];
		min_rad = rr + doff;
		rad     = 1.0/sum;  
	
		born->bRad[ai]   = rad > min_rad ? rad : min_rad;
		fr->invsqrta[ai] = invsqrt(born->bRad[ai]);
	}
		
	/* Extra (local) communication required for DD */
	if(DOMAINDECOMP(cr))
	{
		dd_atom_spread_real(cr->dd, born->bRad);
		dd_atom_spread_real(cr->dd, fr->invsqrta);
	}

	return 0;
}

int 
calc_gb_rad_obc_sse(t_commrec *cr, t_forcerec * fr, int natoms, gmx_localtop_t *top,
					const t_atomtypes *atype, float *x, t_nblist *nl, gmx_genborn_t *born,t_mdatoms *md)
{
	int i,k,n,ai,ai3,aj1,aj2,aj3,aj4,nj0,nj1,at0,at1;
	int p1,p2,p3,p4;
	int aj13,aj23,aj33,aj43,offset;
	float doff;
	float rr,rr_inv,rr_inv2,sum_tmp,sum,sum2,sum3,gbr;
	float sum_ai2, sum_ai3,tsum,tchain;
	
	__m128 ix,iy,iz,jx,jy,jz;
	__m128 dx,dy,dz,t1,t2,t3;
	__m128 rsq11,rinv,r;
	__m128 rai,rai_inv,raj, raj_inv,rai_inv2,sk,sk2,lij,dlij,duij;
	__m128 uij,lij2,uij2,lij3,uij3,diff2;
	__m128 lij_inv,sk2_inv,prod,log_term,tmp,tmp_sum, doffset;
	__m128 sum_ai, chrule, chrule_ai, tmp_ai,sk_ai,sk2_ai;
	__m128 xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8,xmm9;
	__m128 mask_cmp,mask_cmp2,mask_cmp3;
	
	__m128 maski;
	
	const __m128 neg   = {-1.0f , -1.0f , -1.0f , -1.0f };
	const __m128 zero  = {0.0f , 0.0f , 0.0f , 0.0f };
	const __m128 eigth = {0.125f , 0.125f , 0.125f , 0.125f };
	const __m128 qrtr  = {0.25f , 0.25f , 0.25f , 0.25f };
	const __m128 half  = {0.5f , 0.5f , 0.5f , 0.5f };
	const __m128 one   = {1.0f , 1.0f , 1.0f , 1.0f };
	const __m128 two   = {2.0f , 2.0f , 2.0f , 2.0f };
	const __m128 three = {3.0f , 3.0f , 3.0f , 3.0f };
	
	/* keep the compiler happy */
	t1     = _mm_setzero_ps();
	t2     = _mm_setzero_ps();
	t3     = _mm_setzero_ps();
	xmm1   = _mm_setzero_ps();
	xmm2   = _mm_setzero_ps();
	xmm3   = _mm_setzero_ps();
	xmm4   = _mm_setzero_ps();
	tmp    = _mm_setzero_ps();
	tmp_ai = _mm_setzero_ps();
		
	/* Set the dielectric offset */
	doff = born->gb_doffset;
	doffset = _mm_load1_ps(&doff);
	
	aj1=aj2=aj3=aj4=0;
	aj13=aj23=aj33=aj43=0;
	p1=p2=p3=0;
	n=0;
	
	for(i=0;i<born->nr;i++)
	{
		born->gpol_hct_work[i] = 0;
	}
	
	for(i=0;i<nl->nri;i++)
	{
		ai       = nl->iinr[i];
		ai3      = ai*3;
		
		nj0      = nl->jindex[ai];
		nj1      = nl->jindex[ai+1];
		
		offset   = (nj1-nj0)%4;
		
		rr       = top->atomtypes.gb_radius[md->typeA[ai]]-doff;
		rai      = _mm_load1_ps(&rr);
		rr       = 1.0/rr;
		rai_inv  = _mm_load1_ps(&rr);
		
		/* Load ai coordinates */
		ix		 = _mm_load1_ps(x+ai3);
		iy		 = _mm_load1_ps(x+ai3+1);
		iz	     = _mm_load1_ps(x+ai3+2);
		
		sum_ai   = _mm_setzero_ps();
		
		sk_ai  = _mm_load1_ps(born->param+ai);
		sk2_ai = _mm_mul_ps(sk_ai,sk_ai);
				
		for(k=nj0;k<nj1-offset;k+=4)
		{
			aj1 = nl->jjnr[k];	 
			aj2 = nl->jjnr[k+1];
			aj3 = nl->jjnr[k+2];
			aj4 = nl->jjnr[k+3];
			
			aj13 = aj1 * 3; 
			aj23 = aj2 * 3;
			aj33 = aj3 * 3;
			aj43 = aj4 * 3;
			
			/* Load particle aj1-4 and transpose */
			xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13));
			xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23));
			xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33));
			xmm4 = _mm_loadh_pi(xmm4,(__m64 *) (x+aj43));
			
			xmm5    = _mm_load1_ps(x+aj13+2);  
			xmm6    = _mm_load1_ps(x+aj23+2); 
			xmm7    = _mm_load1_ps(x+aj33+2); 
			xmm8    = _mm_load1_ps(x+aj43+2);
						
			xmm5    = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
			xmm6    = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(0,0,0,0));
			jz      = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(2,0,2,0));
			
			xmm1    = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
			xmm2    = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(3,2,3,2));
			jx      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(2,0,2,0));
			jy      = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,1,3,1));
						
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare */
			
			/* Load raj aj1-4 */
			p1 = md->typeA[aj1];
			p2 = md->typeA[aj2];
			p3 = md->typeA[aj3];
			p4 = md->typeA[aj4];
			
			xmm1 = _mm_load_ss(top->atomtypes.gb_radius+p1); 
			xmm2 = _mm_load_ss(top->atomtypes.gb_radius+p2);  
			xmm3 = _mm_load_ss(top->atomtypes.gb_radius+p3); 
			xmm4 = _mm_load_ss(top->atomtypes.gb_radius+p4);
			
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
			xmm3 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j4 j4*/
			raj  = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
			raj  = _mm_sub_ps(raj,doffset);
			
			/* Compute raj_inv aj1-4 */
			xmm3      = _mm_rcp_ps(raj); /*1.0/(raj), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,raj);
			t1        = _mm_sub_ps(two,t1);
			raj_inv   = _mm_mul_ps(t1,xmm3);
			
			/* Perform reciprocal square root lookup, 8 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration to get 12 bits correct*/
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			r         = _mm_mul_ps(rinv,rsq11);
			
			xmm1 = _mm_load_ss(born->param+aj1); 
			xmm2 = _mm_load_ss(born->param+aj2); 
			xmm3 = _mm_load_ss(born->param+aj3); 
			xmm4 = _mm_load_ss(born->param+aj4);
						
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
			xmm3 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j4 j4*/
			sk   = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
						   
						   
			/* INTERACTION aj->ai STARTS HERE */
			/* conditional mask for rai<dr+sk */
			xmm1      = _mm_add_ps(r,sk); /*dr+sk*/		
			mask_cmp  = _mm_cmplt_ps(rai,xmm1);
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk); /*xmm2 = dr-sk */
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk), 8 bits accuracy */
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
										
			mask_cmp2 = _mm_cmpgt_ps(rai,xmm2); /*rai>dr-sk */
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,rai_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));

			uij		= _mm_rcp_ps(xmm1); 
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
					
			diff2   = _mm_sub_ps(uij2,lij2);
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = _mm_mul_ps(sk,sk);
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
			
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
		
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /*0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /*0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm9    = _mm_mul_ps(neg,diff2); /*(-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm9,prod); /*(-1)*diff2*prod */
			tmp_ai  = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */
			xmm3    = _mm_sub_ps(sk,r);
			mask_cmp3 = _mm_cmplt_ps(rai,xmm3); /*rai<sk-dr*/
			
			xmm4    = _mm_sub_ps(rai_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(tmp_ai,xmm4);
					
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp_ai)); /*conditional as a mask*/
			
			/* the tmp will now contain four partial values, that not all are to be used. Which
			* ones are governed by the mask_cmp mask. 
			*/
			tmp_ai     = _mm_mul_ps(half,tmp_ai); /*0.5*tmp*/
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp_ai)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			sum_ai     = _mm_add_ps(sum_ai,tmp_ai);
					
			/* start t1 */
			xmm2   = _mm_mul_ps(half,lij2); /*0.5*lij2 */
			xmm3   = _mm_mul_ps(prod,lij3); /*prod*lij3; */
			xmm2   = _mm_add_ps(xmm2,xmm3); /*0.5*lij2+prod*lij3 */
			xmm3   = _mm_mul_ps(lij,rinv); /*lij*rinv */
			xmm4   = _mm_mul_ps(lij3,r); /*lij3*dr;*/
			xmm3   = _mm_add_ps(xmm3,xmm4); /*lij*rinv+lij3*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(lij*rinv+lij3*dr) */
			t1     = _mm_sub_ps(xmm2,xmm3); /* done t1 */
					
			/* start t2 */
			xmm2   = _mm_mul_ps(half,uij2); /*0.5*uij2 */
			xmm2   = _mm_mul_ps(neg,xmm2); /*(-1)*0.5*uij2 */
			xmm3   = _mm_mul_ps(qrtr,sk2_inv); /*0.25*sk2_rinv */
			xmm3   = _mm_mul_ps(xmm3,uij3); /*0.25*sk2_rinv*uij3 */
			xmm2   = _mm_sub_ps(xmm2,xmm3); /*(-1)*0.5*lij2-0.25*sk2_rinv*uij3 */
			xmm3   = _mm_mul_ps(uij,rinv); /*uij*rinv */
			xmm4   = _mm_mul_ps(uij3,r); /*uij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*uij*rinv+uij*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(uij*rinv+uij*dr) */
			t2     = _mm_add_ps(xmm2,xmm3); /* done t2 */
					
			/* start t3 */
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv; */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm9); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
			
			/* chain rule terms  */
			xmm2   = _mm_mul_ps(dlij,t1); /* dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2); /* dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); 
			
			/* temporary storage of chain rule terms, since we have to compute
			 the reciprocal terms also before storing them */
			chrule = _mm_mul_ps(xmm2,rinv);
			
			/* INTERACTION ai->aj STARTS HERE */
			/* conditional mask for raj<dr+sk_ai */
			xmm1      = _mm_add_ps(r,sk_ai); 
			mask_cmp  = _mm_cmplt_ps(raj,xmm1); 
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk_ai); /*xmm2 = dr-sk_ai*/
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk_ai), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
			
			mask_cmp2 = _mm_cmpgt_ps(raj,xmm2); /*raj>dr-sk_ai */
			
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,raj_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));
			
			uij		= _mm_rcp_ps(xmm1); /* better approximation than just _mm_rcp_ps, which is just 8 bits*/
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
			
			diff2   = _mm_sub_ps(uij2,lij2);
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = sk2_ai;
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
			
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
			
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /* 0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /* 0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /* lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm9    = _mm_mul_ps(neg,diff2); /* (-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm9,prod); /* (-1)*diff2*prod */
			tmp     = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */
			xmm3    = _mm_sub_ps(sk_ai,r);
			mask_cmp3 = _mm_cmplt_ps(raj,xmm3); /* rai<sk-dr */
			
			xmm4    = _mm_sub_ps(raj_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(tmp,xmm4);
			
			tmp	    = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp)); /*conditional as a mask*/
			
			/* the tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask. */
			tmp     = _mm_mul_ps(half,tmp); /* 0.5*tmp */
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			
			/* Load, add and store ai->aj pol energy */
			xmm5 = _mm_load_ss(born->gpol_hct_work+aj1); 
			xmm6 = _mm_load_ss(born->gpol_hct_work+aj2); 
			xmm7 = _mm_load_ss(born->gpol_hct_work+aj3); 
			xmm8 = _mm_load_ss(born->gpol_hct_work+aj4); 
			
			xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); 
			xmm6 = _mm_shuffle_ps(xmm7,xmm8, _MM_SHUFFLE(0,0,0,0)); 
			xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); 
			
			xmm7 = _mm_add_ps(xmm7,tmp);
			
			_mm_store_ss(born->gpol_hct_work+aj1,xmm7); /* aj1 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_hct_work+aj2,xmm7); /* aj2 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(born->gpol_hct_work+aj3,xmm7); /* aj3 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1)); 
			_mm_store_ss(born->gpol_hct_work+aj4,xmm7); /* aj4 */
			
			/* duij   = one; */
			xmm2   = _mm_mul_ps(half,lij2); 
			xmm3   = _mm_mul_ps(prod,lij3); 
			xmm2   = _mm_add_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(lij,rinv); 
			xmm4   = _mm_mul_ps(lij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t1     = _mm_sub_ps(xmm2,xmm3); 
			
			xmm2   = _mm_mul_ps(half,uij2);
			xmm2   = _mm_mul_ps(neg,xmm2); 
			xmm3   = _mm_mul_ps(qrtr,sk2_inv);
			xmm3   = _mm_mul_ps(xmm3,uij3); 
			xmm2   = _mm_sub_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(uij,rinv); 
			xmm4   = _mm_mul_ps(uij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t2     = _mm_add_ps(xmm2,xmm3); 
			
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm9); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
			
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /*dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2);/*dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); 
			chrule_ai = _mm_mul_ps(xmm2,rinv);
			
			/* Here we need to do some unpacking to avoid 8 separate store operations 
			 * The idea is to get terms ai->aj1, aj1->ai, ai->aj2, aj2->ai in xmm3
			 and then ai->aj3, aj3->ai, ai->aj4, aj4->ai in xmm4
			 */
			xmm3 = _mm_unpacklo_ps(chrule,chrule_ai);
			xmm4 = _mm_unpackhi_ps(chrule,chrule_ai);
			
			_mm_storeu_ps(fr->dadx+n, xmm3);
			n = n + 4;
			_mm_storeu_ps(fr->dadx+n, xmm4);
			n = n + 4;
			
		} /* end normal inner loop */
		
		/* deal with offset elements */
		if(offset!=0)
		{
			aj1=aj2=aj3=aj4=0;
			
			if(offset==1)
			{
				aj1   = nl->jjnr[k];
				aj13  = aj1 * 3;
				p1    = md->typeA[aj1];
				
				xmm1  = _mm_loadl_pi(xmm1,(__m64 *) (x+aj13));
				xmm5  = _mm_load1_ps(x+aj13+2);
				
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(0,0,0,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(1,1,1,1));
				
				sk    = _mm_load1_ps(born->param+aj1);
				
				raj   = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				raj   = _mm_sub_ps(raj,doffset);
						
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0,0,0xffffffff) );
			}
			else if(offset==2)
			{
				aj1   = nl->jjnr[k];
				aj2   = nl->jjnr[k+1];
				p1    = md->typeA[aj1];
				p2    = md->typeA[aj2];
				aj13  = aj1 * 3;
				aj23  = aj2 * 3;

				xmm1  = _mm_loadh_pi(xmm1, (__m64 *) (x+aj13));
				xmm2  = _mm_loadh_pi(xmm2, (__m64 *) (x+aj23));
				
				xmm5  = _mm_load1_ps(x+aj13+2);
				xmm6  = _mm_load1_ps(x+aj23+2);
				
				xmm5  = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0));
				xmm5  = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0));
				
				xmm1  = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2));
				xmm6  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				xmm4  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,3,1));
				
				xmm1 = _mm_load1_ps(born->param+aj1);
				xmm2 = _mm_load1_ps(born->param+aj2);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0));
				sk   = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				
				xmm1 = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				xmm2 = _mm_load1_ps(top->atomtypes.gb_radius+p2);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0));
				raj  = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0));
				raj  = _mm_sub_ps(raj, doffset);
				
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0,0xffffffff,0xffffffff) );
			}
			else
			{
				aj1   = nl->jjnr[k];
				aj2   = nl->jjnr[k+1];
				aj3   = nl->jjnr[k+2];
				p1    = md->typeA[aj1];
				p2    = md->typeA[aj2];
				p3    = md->typeA[aj3];
				aj13  = aj1 * 3;
				aj23  = aj2 * 3;
				aj33  = aj3 * 3;
				
				xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (x+aj13)); 
				xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (x+aj23)); 
				xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (x+aj33)); 
				
				xmm5 = _mm_load1_ps(x+aj13+2); 
				xmm6 = _mm_load1_ps(x+aj23+2); 
				xmm7 = _mm_load1_ps(x+aj33+2); 
											
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0));
				xmm5 = _mm_shuffle_ps(xmm5,xmm7, _MM_SHUFFLE(3,1,3,1));						
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,2,3,2));
				xmm2 = _mm_shuffle_ps(xmm3,xmm3, _MM_SHUFFLE(3,2,3,2));
				
				xmm6 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(2,0,2,0)); 
				xmm4 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,1,3,1));
				
				xmm1 = _mm_load1_ps(born->param+aj1);
				xmm2 = _mm_load1_ps(born->param+aj2);
				xmm3 = _mm_load1_ps(born->param+aj3);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j3 j3*/
				sk   = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
				
				xmm1 = _mm_load1_ps(top->atomtypes.gb_radius+p1);
				xmm2 = _mm_load1_ps(top->atomtypes.gb_radius+p2);
				xmm3 = _mm_load1_ps(top->atomtypes.gb_radius+p3);
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(0,0,0,0)); /*j1 j1 j2 j2*/
				xmm3 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(0,0,0,0)); /*j3 j3 j3 j3*/
				raj  = _mm_shuffle_ps(xmm1,xmm3,_MM_SHUFFLE(2,0,2,0));
				raj  = _mm_sub_ps(raj,doffset);
				
				maski = gmx_castsi128_ps( _mm_set_epi32(0,0xffffffff,0xffffffff,0xffffffff) );
			}
			
			jx = _mm_and_ps( maski, xmm6);
			jy = _mm_and_ps( maski, xmm4);
			jz = _mm_and_ps( maski, xmm5);
			
			sk = _mm_and_ps ( maski, sk);
			sk_ai = _mm_and_ps( maski, sk_ai);
			
			/* Compute raj_inv offset for aj atoms */
			xmm3      = _mm_rcp_ps(raj); /*1.0/(raj), 12 bits accuracy*/
			t1        = _mm_mul_ps(xmm3,raj);
			t1        = _mm_sub_ps(two,t1);
			raj_inv   = _mm_mul_ps(t1,xmm3);
			raj_inv   = _mm_and_ps( maski,raj_inv); 
			
			dx    = _mm_sub_ps(ix, jx);
			dy    = _mm_sub_ps(iy, jy);
			dz    = _mm_sub_ps(iz, jz);
			
			t1    = _mm_mul_ps(dx,dx);
			t2    = _mm_mul_ps(dy,dy);
			t3    = _mm_mul_ps(dz,dz);
			
			rsq11 = _mm_add_ps(t1,t2);
			rsq11 = _mm_add_ps(rsq11,t3); /*rsq11=rsquare*/
			
			/* Perform reciprocal square root lookup, 12 bits accuracy */
			t1        = _mm_rsqrt_ps(rsq11);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(rsq11,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			rinv      = _mm_mul_ps(half,t3); /* result for all four particles */
			
			r         = _mm_mul_ps(rinv,rsq11);
			
							   
			/* OFFSET INTERACTION aj->ai starts here */
			/* conditional mask for rai<dr+sk */
			xmm1      = _mm_add_ps(r,sk); /*dr+sk*/	
			mask_cmp  = _mm_cmplt_ps(rai,xmm1);
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk); /*xmm2 = dr-sk*/
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk)*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
										
			mask_cmp2 = _mm_cmpgt_ps(rai,xmm2); /* rai>dr-sk */
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,rai_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /* conditional as a mask */
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));

			uij		= _mm_rcp_ps(xmm1);
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
					
			diff2   = _mm_sub_ps(uij2,lij2);
			
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */

			sk2     = _mm_mul_ps(sk,sk);
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
		
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
																					
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); 
			xmm2    = _mm_mul_ps(xmm2,diff2); 
			xmm1    = _mm_add_ps(xmm1,xmm2); 
			xmm2    = _mm_mul_ps(half,rinv); 
			xmm2    = _mm_mul_ps(xmm2,log_term);
			xmm1    = _mm_add_ps(xmm1,xmm2); 
			xmm9    = _mm_mul_ps(neg,diff2); 
			xmm2    = _mm_mul_ps(xmm9,prod); 
			tmp_ai  = _mm_add_ps(xmm1,xmm2); 
			
			/* contitional for rai<sk-dr	*/				
			xmm3    = _mm_sub_ps(sk,r);
			mask_cmp3 = _mm_cmplt_ps(rai,xmm3); /*rai<sk-dr */
			
			xmm4    = _mm_sub_ps(rai_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(xmm1,xmm4);
					
			tmp_ai  = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp_ai)); /*conditional as a mask*/
				
			/* tmp will now contain four partial values, that not all are to be used. Which
			* ones are governed by the mask_cmp mask. 
			*/
			tmp_ai     = _mm_mul_ps(half,tmp_ai); 
			tmp_ai     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp_ai)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			sum_ai     = _mm_add_ps(sum_ai,tmp_ai);
			
			/* start t1 */
			xmm2   = _mm_mul_ps(half,lij2);
			xmm3   = _mm_mul_ps(prod,lij3);
			xmm2   = _mm_add_ps(xmm2,xmm3);
			xmm3   = _mm_mul_ps(lij,rinv); 
			xmm4   = _mm_mul_ps(lij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t1     = _mm_sub_ps(xmm2,xmm3); 
		
			/* start t2 */
			xmm2   = _mm_mul_ps(half,uij2); 
			xmm2   = _mm_mul_ps(neg,xmm2); 
			xmm3   = _mm_mul_ps(qrtr,sk2_inv);
			xmm3   = _mm_mul_ps(xmm3,uij3); 
			xmm2   = _mm_sub_ps(xmm2,xmm3); 
			xmm3   = _mm_mul_ps(uij,rinv); 
			xmm4   = _mm_mul_ps(uij3,r); 
			xmm3   = _mm_add_ps(xmm3,xmm4);
			xmm3   = _mm_mul_ps(qrtr,xmm3);
			t2     = _mm_add_ps(xmm2,xmm3);
					
			/* start t3 */
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); 
			xmm2   = _mm_mul_ps(eigth,xmm2); 
			xmm2   = _mm_mul_ps(xmm2,xmm9); 
			xmm3   = _mm_mul_ps(log_term, rinv);
			xmm3   = _mm_mul_ps(xmm3,rinv); 
			xmm3   = _mm_mul_ps(qrtr,xmm3); 
			t3     = _mm_add_ps(xmm2,xmm3);
						 
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); 
			xmm2   = _mm_add_ps(xmm2,t2);
			xmm2   = _mm_add_ps(xmm2,t3); 
			xmm2   = _mm_mul_ps(xmm2,rinv);
			chrule   = _mm_and_ps( maski,xmm2);
			
			
			
			/* OFFSET INTERACTION ai->aj starts here */
			/* conditional mask for raj<dr+sk */
			xmm1      = _mm_add_ps(r,sk_ai); /*dr+sk		*/		
			mask_cmp  = _mm_cmplt_ps(raj,xmm1); 
			
			/* conditional for rai>dr-sk, ends with mask_cmp2 */
			xmm2      = _mm_sub_ps(r,sk_ai); /*xmm2 = dr-sk */
			
			xmm3      = _mm_rcp_ps(xmm2); /*1.0/(dr-sk)*/
			t1        = _mm_mul_ps(xmm3,xmm2);
			t1        = _mm_sub_ps(two,t1);
			xmm3      = _mm_mul_ps(t1,xmm3);
			
			mask_cmp2 = _mm_cmpgt_ps(raj,xmm2); /*rai>dr-sk */
			lij	      = _mm_or_ps(_mm_and_ps(mask_cmp2,raj_inv)  ,_mm_andnot_ps(mask_cmp2,xmm3)); /*conditional as a mask*/
			dlij      = _mm_or_ps(_mm_and_ps(mask_cmp2,zero) ,_mm_andnot_ps(mask_cmp2,one));
			
			uij		= _mm_rcp_ps(xmm1);
			t1      = _mm_mul_ps(uij,xmm1);
			t1      = _mm_sub_ps(two,t1);
			uij     = _mm_mul_ps(t1,uij);
			
			lij2    = _mm_mul_ps(lij,lij); 
			lij3    = _mm_mul_ps(lij2,lij);
			uij2    = _mm_mul_ps(uij,uij);
			uij3    = _mm_mul_ps(uij2,uij);		
			
			diff2   = _mm_sub_ps(uij2,lij2);
			
			t1        = _mm_rsqrt_ps(lij2);   /* t1=lookup, r2=x */
			/* Newton-Rhapson iteration */
			t2        = _mm_mul_ps(t1,t1); /* lu*lu */
			t3        = _mm_mul_ps(lij2,t2);  /* x*lu*lu */
			t3        = _mm_sub_ps(three,t3); /* 3.0-x*lu*lu */
			t3        = _mm_mul_ps(t1,t3); /* lu*(3-x*lu*lu) */
			lij_inv   = _mm_mul_ps(half,t3); /* result for all four particles */
			
			sk2     = sk2_ai;
			sk2_inv = _mm_mul_ps(sk2,rinv);
			prod    = _mm_mul_ps(qrtr,sk2_inv);
			
			log_term = _mm_mul_ps(uij,lij_inv);
			log_term = log2_ps(log_term);
			
			xmm1    = _mm_sub_ps(lij,uij);
			xmm2    = _mm_mul_ps(qrtr,r); /* 0.25*dr */
			xmm2    = _mm_mul_ps(xmm2,diff2); /*0.25*dr*prod */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij + 0.25*dr*diff2 */
			xmm2    = _mm_mul_ps(half,rinv); /* 0.5*rinv */
			xmm2    = _mm_mul_ps(xmm2,log_term); /*0.5*rinv*log_term */
			xmm1    = _mm_add_ps(xmm1,xmm2); /*lij-uij+0.25*dr*diff2+0.5*rinv*log_term */
			xmm8    = _mm_mul_ps(neg,diff2); /*(-1)*diff2 */
			xmm2    = _mm_mul_ps(xmm8,prod); /*(-1)*diff2*prod */
			tmp     = _mm_add_ps(xmm1,xmm2); /* done tmp-term */
			
			/* contitional for rai<sk-dr */					
			xmm3    = _mm_sub_ps(sk_ai,r);
			mask_cmp3 = _mm_cmplt_ps(raj,xmm3); /*rai<sk-dr*/
			
			xmm4    = _mm_sub_ps(raj_inv,lij);
			xmm4    = _mm_mul_ps(two,xmm4);
			xmm4    = _mm_add_ps(xmm1,xmm4);
			
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp3,xmm4)  ,_mm_andnot_ps(mask_cmp3,tmp)); /*conditional as a mask*/
			
			/* tmp will now contain four partial values, that not all are to be used. Which */
			/* ones are governed by the mask_cmp mask.*/ 
			tmp     = _mm_mul_ps(half,tmp); /*0.5*tmp*/
			tmp     = _mm_or_ps(_mm_and_ps(mask_cmp,tmp)  ,_mm_andnot_ps(mask_cmp,zero)); /*conditional as a mask*/
			
			/* start t1 */
			xmm2   = _mm_mul_ps(half,lij2); /*0.5*lij2 */
			xmm3   = _mm_mul_ps(prod,lij3); /*prod*lij3;*/
			xmm2   = _mm_add_ps(xmm2,xmm3); /*0.5*lij2+prod*lij3 */
			xmm3   = _mm_mul_ps(lij,rinv); /*lij*rinv */
			xmm4   = _mm_mul_ps(lij3,r); /*lij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*lij*rinv+lij3*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(lij*rinv+lij3*dr) */
			t1     = _mm_sub_ps(xmm2,xmm3); /* done t1 */
			
			/* start t2 */
			xmm2   = _mm_mul_ps(half,uij2); /*0.5*uij2 */
			xmm2   = _mm_mul_ps(neg,xmm2); /*(-1)*0.5*lij2 */
			xmm3   = _mm_mul_ps(qrtr,sk2_inv); /*0.25*sk2_rinv */
			xmm3   = _mm_mul_ps(xmm3,uij3); /*0.25*sk2_rinv*uij3 */
			xmm2   = _mm_sub_ps(xmm2,xmm3); /*(-1)*0.5*lij2-0.25*sk2_rinv*uij3 */
			xmm3   = _mm_mul_ps(uij,rinv); /*uij*rinv */
			xmm4   = _mm_mul_ps(uij3,r); /*uij3*dr; */
			xmm3   = _mm_add_ps(xmm3,xmm4); /*uij*rinv+uij*dr */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*(uij*rinv+uij*dr) */
			t2     = _mm_add_ps(xmm2,xmm3); /* done t2 */
			
			/* start t3 */
			xmm2   = _mm_mul_ps(sk2_inv,rinv);
			xmm2   = _mm_add_ps(one,xmm2); /*1+sk2_rinv*rinv; */
			xmm2   = _mm_mul_ps(eigth,xmm2); /*0.125*(1+sk2_rinv*rinv) */
			xmm2   = _mm_mul_ps(xmm2,xmm8); /*0.125*(1+sk2_rinv*rinv)*(-diff2) */
			xmm3   = _mm_mul_ps(log_term, rinv); /*log_term*rinv */
			xmm3   = _mm_mul_ps(xmm3,rinv); /*log_term*rinv*rinv */
			xmm3   = _mm_mul_ps(qrtr,xmm3); /*0.25*log_term*rinv*rinv */
			t3     = _mm_add_ps(xmm2,xmm3); /* done t3 */
			
			/* chain rule terms */
			xmm2   = _mm_mul_ps(dlij,t1); /*dlij*t1 */
			xmm2   = _mm_add_ps(xmm2,t2);/*dlij*t1+duij*t2 */
			xmm2   = _mm_add_ps(xmm2,t3); /*everyhting * t3 */
			xmm2   = _mm_mul_ps(xmm2,rinv); /*everything * t3 *rinv */
			chrule_ai = _mm_and_ps(maski,xmm2);
			
			if(offset==1)
			{
				xmm7 = _mm_load1_ps(born->gpol_hct_work+aj1);
				xmm7  = _mm_add_ps(xmm7,tmp);
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				
				_mm_storeu_ps(fr->dadx+n, chrule); 
				n = n + 1;
				_mm_storeu_ps(fr->dadx+n, chrule_ai);
				n = n + 1;
			}
			else if(offset==2)
			{
				xmm5 = _mm_load1_ps(born->gpol_hct_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_hct_work+aj2); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0)); 
				
				xmm7  = _mm_add_ps(xmm7,tmp);
				
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj2,xmm7);
				
				xmm3 = _mm_unpacklo_ps(chrule,chrule_ai); /* Same idea as above */
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				/* Here we advance by 2*offset, since all four values fit in one xmm variable and
				 * can be stored all at once */
				n = n + 4; 
			}
			else
			{
				xmm5 = _mm_load1_ps(born->gpol_hct_work+aj1); 
				xmm6 = _mm_load1_ps(born->gpol_hct_work+aj2); 
				xmm7 = _mm_load1_ps(born->gpol_hct_work+aj3); 
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); 
				xmm6 = _mm_shuffle_ps(xmm7,xmm7, _MM_SHUFFLE(0,0,0,0)); 
				xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); 
				
				xmm7  = _mm_add_ps(xmm7,tmp);
				
				_mm_store_ss(born->gpol_hct_work+aj1,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj2,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(born->gpol_hct_work+aj3,xmm7);
				
				xmm3 = _mm_unpacklo_ps(chrule,chrule_ai); /* Same idea as above, but extra shuffles because of odd elements */
				xmm4 = _mm_unpackhi_ps(chrule,chrule_ai);
				xmm4 = _mm_shuffle_ps(xmm4,xmm3,_MM_SHUFFLE(3,3,1,0));
				xmm4 = _mm_shuffle_ps(xmm4,xmm4,_MM_SHUFFLE(1,1,0,3));
				
				_mm_storeu_ps(fr->dadx+n, xmm3); 
				n = n + offset;
				_mm_storeu_ps(fr->dadx+n, xmm4);
				n = n + offset;
			}
		} /* end offset */
		
		/* the sum_ai array will contain partial values that need to be added together*/
		tmp_ai  = _mm_movehl_ps(tmp_ai,sum_ai);
		sum_ai  = _mm_add_ps(sum_ai,tmp_ai);
		tmp_ai  = _mm_shuffle_ps(sum_ai,sum_ai,_MM_SHUFFLE(1,1,1,1));
		sum_ai  = _mm_add_ss(sum_ai,tmp_ai);
		
		xmm2    = _mm_load1_ps(born->gpol_hct_work+ai);
		sum_ai  = _mm_add_ss(sum_ai,xmm2);
		_mm_store_ss(born->gpol_hct_work+ai,sum_ai);
	}
	
	/* Parallel summations */
	if(PARTDECOMP(cr))
	{
		gmx_sum(natoms, born->gpol_hct_work, cr);
	}
	else if(DOMAINDECOMP(cr))
	{
		dd_atom_sum_real(cr->dd, born->gpol_hct_work);
	}
	
	/* Compute the radii */
	for(i=0;i<nl->nri;i++)
	{
		ai      = nl->iinr[i];
		rr      = top->atomtypes.gb_radius[md->typeA[ai]];
		rr_inv2 = 1.0/rr;
		rr      = rr-doff; 
		rr_inv  = 1.0/rr;
		sum     = rr * born->gpol_hct_work[ai];
		sum2    = sum  * sum;
		sum3    = sum2 * sum;
		
		tsum    = tanh(born->obc_alpha*sum-born->obc_beta*sum2+born->obc_gamma*sum3);
		born->bRad[ai] = rr_inv - tsum*rr_inv2;
		born->bRad[ai] = 1.0 / born->bRad[ai];
		
		fr->invsqrta[ai]=invsqrt(born->bRad[ai]);
		
		tchain  = rr * (born->obc_alpha-2*born->obc_beta*sum+3*born->obc_gamma*sum2);
		born->drobc[ai] = (1.0-tsum*tsum)*tchain*rr_inv2;
	}
	
	/* Extra (local) communication required for DD */
	if(DOMAINDECOMP(cr))
	{
		dd_atom_spread_real(cr->dd, born->bRad);
		dd_atom_spread_real(cr->dd, fr->invsqrta);
		dd_atom_spread_real(cr->dd, born->drobc);
	}

	
	return 0;
}



float calc_gb_chainrule_sse(int natoms, t_nblist *nl, float *dadx, float *dvda, float *xd, float *f, int gb_algorithm, gmx_genborn_t *born)						
{
	int    i,k,n,ai,aj,ai3,nj0,nj1,offset;
	int	   aj1,aj2,aj3,aj4; 
	
	float   rbi;
	float   *rb;
	
	__m128 ix,iy,iz;
	__m128 jx,jy,jz;
	__m128 fix,fiy,fiz;
	__m128 dx,dy,dz;
	__m128 t1,t2,t3;
	__m128 dva,dvaj,dax,dax_ai,fgb;
	__m128 rbaj, fgb_ai;
	__m128 xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8;
	
	__m128 mask   = gmx_castsi128_ps( _mm_set_epi32(0, 0xffffffff,0xffffffff,0xffffffff) );
	__m128 maski  = gmx_castsi128_ps( _mm_set_epi32(0, 0xffffffff,0xffffffff,0xffffffff) );
	
	const __m128 two = {2.0f , 2.0f , 2.0f , 2.0f };
	float z = 0;
	rb     = born->work; 
			
	/* Loop to get the proper form for the Born radius term, sse style */
	offset=natoms%4;
	
	if(offset!=0)
	{
		if(offset==1)
			mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0,0xffffffff) );
		else if(offset==2)
			mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0xffffffff,0xffffffff) );
		else
			mask = gmx_castsi128_ps( _mm_set_epi32(0,0xffffffff,0xffffffff,0xffffffff) );
	}
	
	if(gb_algorithm==egbSTILL) {
		
		xmm3 = _mm_set1_ps(ONE_4PI_EPS0);
	
		for(i=0;i<natoms-offset;i+=4)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1,xmm1);
			xmm1 = _mm_mul_ps(xmm1,two); /*2 * rbi * rbi*/
			
			xmm2 = _mm_loadu_ps(dvda+i);
			
			xmm1 = _mm_mul_ps(xmm1,xmm2); /* 2 * rbi * rbi * dvda[i] */
			xmm1 = _mm_div_ps(xmm1,xmm3); /* (2 * rbi * rbi * dvda[i]) / ONE_4PI_EPS0 */
		
			_mm_storeu_ps(rb+i, xmm1); /* store to memory */
		}
		
		/* with the offset element, the mask stores excess elements to zero. This could cause problems
		* when something gets allocated right after rb (solution: allocate three positions bigger)	
		*/
		if(offset!=0)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1,xmm1);
			xmm1 = _mm_mul_ps(xmm1,two); 
			
			xmm2 = _mm_loadu_ps(dvda+i);
			
			xmm1 = _mm_mul_ps(xmm1,xmm2); 
			xmm1 = _mm_div_ps(xmm1,xmm3); 
			xmm1 = _mm_and_ps(mask, xmm1);
		
			_mm_storeu_ps(rb+i, xmm1); 
		}
	}
		
	else if(gb_algorithm==egbHCT) {
		for(i=0;i<natoms-offset;i+=4)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1, xmm1); /* rbi*rbi */
			 
			xmm2 = _mm_loadu_ps(dvda+i);
			xmm1 = _mm_mul_ps(xmm1,xmm2); /* rbi*rbi*dvda[i] */
			
			_mm_storeu_ps(rb+i, xmm1); 
		}
		
		if(offset!=0)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1, xmm1); /* rbi*rbi */
			 
			xmm2 = _mm_loadu_ps(dvda+i);
			
			xmm1 = _mm_mul_ps(xmm1,xmm2); /* rbi*rbi*dvda[i] */
			xmm1 = _mm_and_ps(mask, xmm1);
			
			_mm_storeu_ps(rb+i, xmm1);
		}
	}
	 
	else if(gb_algorithm==egbOBC) {
		for(i=0;i<natoms-offset;i+=4)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1, xmm1); /* rbi*rbi */
			 
			xmm2 = _mm_loadu_ps(dvda+i);
			xmm1 = _mm_mul_ps(xmm1,xmm2); /* rbi*rbi*dvda[i] */
			xmm2 = _mm_loadu_ps(born->drobc+i);
			xmm1 = _mm_mul_ps(xmm1, xmm2); /*rbi*rbi*dvda[i]*born->drobc[i] */
						
			_mm_storeu_ps(rb+i, xmm1);
		}
	
		if(offset!=0)
		{
			xmm1 = _mm_loadu_ps(born->bRad+i);
			xmm1 = _mm_mul_ps(xmm1, xmm1); /* rbi*rbi */
			 
			xmm2 = _mm_loadu_ps(dvda+i);
			xmm1 = _mm_mul_ps(xmm1,xmm2); /* rbi*rbi*dvda[i] */
			xmm2 = _mm_loadu_ps(born->drobc+i);
			xmm1 = _mm_mul_ps(xmm1, xmm2); /*rbi*rbi*dvda[i]*born->drobc[i] */
			xmm1 = _mm_and_ps(mask, xmm1);				
												
			_mm_storeu_ps(rb+i, xmm1);
		}
	}
		
	/* Keep the compiler happy */
	t1   = _mm_setzero_ps();
	t2   = _mm_setzero_ps();
	t3   = _mm_setzero_ps();
	xmm1 = _mm_setzero_ps();
	xmm2 = _mm_setzero_ps();
	xmm3 = _mm_setzero_ps();
	xmm4 = _mm_setzero_ps();
		
	aj1 = aj2 = aj3 = aj4 = 0;
	n=0;
	
	for(i=0;i<nl->nri;i++)
	{
		ai     = nl->iinr[i];
		ai3	   = ai*3;
	
		nj0    = nl->jindex[ai];
		nj1    = nl->jindex[ai+1];
		
		offset = (nj1-nj0)%4;
		
		/* Load particle ai coordinates */
		ix  = _mm_load1_ps(xd+ai3);
		iy  = _mm_load1_ps(xd+ai3+1);
		iz  = _mm_load1_ps(xd+ai3+2);
		
		/* Load particle ai dvda */
		dva = _mm_load1_ps(rb+ai);
			
		fix = _mm_setzero_ps();
		fiy = _mm_setzero_ps();
		fiz = _mm_setzero_ps();	
				
		/* Inner loop for all particles where n%4==0 */
		for(k=nj0;k<nj1-offset;k+=4)
		{
			aj1 = nl->jjnr[k];	
			aj2 = nl->jjnr[k+1];
			aj3 = nl->jjnr[k+2];
			aj4 = nl->jjnr[k+3];
			
			/* Load dvda_j */
			dvaj       = _mm_set_ps(rb[aj4],
									rb[aj3],
									rb[aj2],
									rb[aj1]);
			
			aj1 = aj1 * 3; 
			aj2 = aj2 * 3;
			aj3 = aj3 * 3;
			aj4 = aj4 * 3;
			
			/* Load j1-4 coordinates, first x and y */
			xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (xd+aj1)); /*x1 y1 - - */
			xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (xd+aj2)); /*x2 y2 - - */
			xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (xd+aj3)); /*x3 y3 - - */
			xmm4 = _mm_loadh_pi(xmm4,(__m64 *) (xd+aj4)); /*x4 y4 - - */
			
			/* ... then z */
			xmm5 = _mm_load1_ps(xd+aj1+2); /* z1 z1 z1 z1 */
			xmm6 = _mm_load1_ps(xd+aj2+2); /* z2 z2 z2 z2 */
			xmm7 = _mm_load1_ps(xd+aj3+2); /* z3 z3 z3 z3 */
			xmm8 = _mm_load1_ps(xd+aj4+2); /* z4 z4 z4 z4 */
			
			/* transpose */
			xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); /* z1 z1 z2 z2 */
			xmm6 = _mm_shuffle_ps(xmm7,xmm8, _MM_SHUFFLE(0,0,0,0)); /* z3 z3 z4 z4 */
			jz   = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); /* z1 z2 z3 z4 */
			
			xmm1 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,2,3,2)); /* x1 y1 x2 y2 */
			xmm2 = _mm_shuffle_ps(xmm3,xmm4, _MM_SHUFFLE(3,2,3,2)); /* x3 y3 x4 y4 */
			
			jx   = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(2,0,2,0)); /* x1 x2 x3 x4 */
			jy   = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,1,3,1)); /* y1 y2 y3 y4 */
									 
			/* load chain rule terms for j1-4 */
			xmm7 = _mm_loadu_ps(dadx+n);
			n   = n + 4;
			xmm8 = _mm_loadu_ps(dadx+n);
			n   = n + 4;
			
			/* Shuffle to get the ai and aj components right */ 
			dax    = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(2,0,2,0));
			dax_ai = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(3,1,3,1));
			
			/* distances i -> j1-4 */
			dx   = _mm_sub_ps(ix, jx);
			dy   = _mm_sub_ps(iy, jy);
			dz   = _mm_sub_ps(iz, jz);
			
			/* calculate scalar force */
			fgb    = _mm_mul_ps(dva,dax); 
			fgb_ai = _mm_mul_ps(dvaj,dax_ai);
			fgb    = _mm_add_ps(fgb,fgb_ai);
		
			/* calculate partial force terms */
			t1   = _mm_mul_ps(fgb,dx); /* fx1, fx2, fx3, fx4 */
			t2   = _mm_mul_ps(fgb,dy); /* fy1, fy2, fy3, fy4  */
			t3   = _mm_mul_ps(fgb,dz); /* fz1, fz2, fz3, fz4 */
		
			/* update the i force */
			fix       = _mm_add_ps(fix,t1);
			fiy       = _mm_add_ps(fiy,t2);
			fiz       = _mm_add_ps(fiz,t3);	
			
			/* accumulate the aj1-4 fx and fy forces from memory */
			xmm1 = _mm_loadh_pi(xmm1, (__m64 *) (f+aj1)); /*fx1 fy1 - - */
			xmm2 = _mm_loadh_pi(xmm2, (__m64 *) (f+aj2)); /*fx2 fy2 - - */
			xmm3 = _mm_loadh_pi(xmm3, (__m64 *) (f+aj3)); /*fx3 fy3 - - */
			xmm4 = _mm_loadh_pi(xmm4, (__m64 *) (f+aj4)); /*fx4 fy4 - - */
			
			xmm5 = _mm_load1_ps(f+aj1+2); /* fz1 fz1 fz1 fz1 */
			xmm6 = _mm_load1_ps(f+aj2+2); /* fz2 fz2 fz2 fz2 */
			xmm7 = _mm_load1_ps(f+aj3+2); /* fz3 fz3 fz3 fz3 */
			xmm8 = _mm_load1_ps(f+aj4+2); /* fz4 fz4 fz4 fz4 */
			
			/* transpose forces */
			xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); /* fz1 fz1 fz2 fz2 */
			xmm6 = _mm_shuffle_ps(xmm7,xmm8, _MM_SHUFFLE(0,0,0,0)); /* fz3 fz3 fz4 fz4 */
			xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); /* fz1 fz2 fz3 fz4 */
			
			xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2)); /*fx1 fy1 fx2 fy2 */
			xmm2 = _mm_shuffle_ps(xmm3,xmm4,_MM_SHUFFLE(3,2,3,2)); /*fx2 fy3 fx4 fy4 */
			
			xmm5 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(2,0,2,0)); /*fx1 fx2 fx3 fx4 */
			xmm6 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,1,3,1)); /*fy1 fy2 fy3 fy4 */
			
			/* subtract partial forces */
			xmm5 = _mm_sub_ps(xmm5, t1); /*fx1 fx2 fx3 fx4 */
			xmm6 = _mm_sub_ps(xmm6, t2); /*fy1 fy2 fy3 fy4 */
			xmm7 = _mm_sub_ps(xmm7, t3); /*fz1 fz2 fz3 fz4 */
	
			/* transpose back fx's and fy's */
			xmm1 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(1,0,1,0)); /*fx1 fx2 fy1 fy2 */ 
			xmm2 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(3,2,3,2)); /*fx3 fx4 fy3 fy4 */
			xmm1 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,2,0)); /*fx1 fy1 fx2 fy2 */
			xmm2 = _mm_shuffle_ps(xmm2,xmm2,_MM_SHUFFLE(3,1,2,0)); /*fx3 fy3 fx4 fy4 */
			
			/* store the force, first fx's and fy's */
			_mm_storel_pi( (__m64 *) (f+aj1), xmm1);
			_mm_storeh_pi( (__m64 *) (f+aj2), xmm1);
			_mm_storel_pi( (__m64 *) (f+aj3), xmm2);
			_mm_storeh_pi( (__m64 *) (f+aj4), xmm2);
			
			/* now do fz�s*/
			_mm_store_ss(f+aj1+2,xmm7); /*fz1 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(f+aj2+2,xmm7); /*fz2 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
			_mm_store_ss(f+aj3+2,xmm7); /*fz4 */
			xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1)); 
			_mm_store_ss(f+aj4+2,xmm7); /*fz3 */
		}
		
		/*deal with odd elements */
		if(offset!=0) {
		
			aj1 = aj2 = aj3 = aj4 = 0;
			
			if(offset==1)
			{
				aj1 = nl->jjnr[k];	
							
				/* Load dvda_j */
				dvaj       = _mm_set_ps(0.0f,
										0.0f,
										0.0f,
										rb[aj1]);
				
				/* Load dadx */
				dax        = _mm_set_ps(0.0f,
										0.0f,
										0.0f,
										dadx[n++]);
				
				dax_ai     = _mm_set_ps(0.0f,
										0.0f,
										0.0f,
										dadx[n++]);
				
				aj1 = aj1 * 3; 
				
				xmm1 = _mm_loadl_pi(xmm1,(__m64 *) (xd+aj1)); /*x1 y1 */
				xmm5 = _mm_load1_ps(xd+aj1+2); /*z1 z1 z1 z1*/
								
				xmm6 = _mm_shuffle_ps(xmm1, xmm1, _MM_SHUFFLE(0,0,0,0)); /*x1 - - - */ 
				xmm4 = _mm_shuffle_ps(xmm1, xmm1, _MM_SHUFFLE(1,1,1,1)); /*y1 - - - */
			
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0,0xffffffff) );
			}
			else if(offset==2)
			{
				aj1 = nl->jjnr[k];	 
				aj2 = nl->jjnr[k+1];
				
				/* Load dvda_j */
				dvaj       = _mm_set_ps(0.0f,
										0.0f,
										rb[aj2],
										rb[aj1]);
				
				/* Load dadx */
				xmm7 = _mm_loadu_ps(dadx+n);
				n    = n + offset;
				xmm8 = _mm_loadu_ps(dadx+n);
				n    = n + offset;
				
				dax = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(1,1,2,0));
				dax_ai = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(2,2,3,1));
				
				aj1 = aj1 * 3; 
				aj2 = aj2 * 3;
				
				xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (xd+aj1)); /* x1 y1 - - */
				xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (xd+aj2)); /* x2 y2 - - */
				
				xmm5 = _mm_load1_ps(xd+aj1+2); /*z1 z1 z1 z1*/
				xmm6 = _mm_load1_ps(xd+aj2+2); /*z2 z2 z2 z2*/
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0)); /*z1 z1 z2 z2 */
				xmm5 = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0)); /*z1 z2 z1 z2 */
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2)); /*x1 y1 x2 y2 */
				xmm6 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0)); /*x1 x2 x1 x2 */
				xmm4 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,3,1)); /*y1 y2 y1 y2 */	
				
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0,0xffffffff,0xffffffff) );
			}
			else
			{
				mask = gmx_castsi128_ps( _mm_set_epi32(0,0xffffffff,0xffffffff,0xffffffff) );
				
				aj1 = nl->jjnr[k];	 
				aj2 = nl->jjnr[k+1];
				aj3 = nl->jjnr[k+2];
				
				/* Load dvda_j */
				dvaj       = _mm_set_ps(0.0f,
										rb[aj3],
										rb[aj2],
										rb[aj1]);
				
				/* Load dadx */
				xmm7       = _mm_loadu_ps(dadx+n);
				n          = n + offset;
				xmm8       = _mm_loadu_ps(dadx+n);
				n          = n + offset;
				
				dax    = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(1,1,2,0));
				dax_ai = _mm_shuffle_ps(xmm7,xmm8,_MM_SHUFFLE(2,2,3,1));
			
				aj1 = aj1 * 3; 
				aj2 = aj2 * 3;
				aj3 = aj3 * 3;
				
				xmm1 = _mm_loadh_pi(xmm1,(__m64 *) (xd+aj1)); /*x1 y1 - - */
				xmm2 = _mm_loadh_pi(xmm2,(__m64 *) (xd+aj2)); /*x2 y2 - - */
				xmm3 = _mm_loadh_pi(xmm3,(__m64 *) (xd+aj3)); /*x3 y3 - - */
				
				xmm5 = _mm_load1_ps(xd+aj1+2); /* z1 z1 z1 z1 */
				xmm6 = _mm_load1_ps(xd+aj2+2); /* z2 z2 z2 z2 */
				xmm7 = _mm_load1_ps(xd+aj3+2); /* z3 z3 z3 z3 */
											
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); /* z1 z1 z2 z2 */
				xmm5 = _mm_shuffle_ps(xmm5,xmm7, _MM_SHUFFLE(3,1,3,1)); /* z1 z2 z3 z3 */						
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,2,3,2)); /* x1 y1 x2 y2 */
				xmm2 = _mm_shuffle_ps(xmm3,xmm3, _MM_SHUFFLE(3,2,3,2)); /* x3 y3 x3 y3 */
				
				xmm6 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(2,0,2,0)); /* x1 x2 x3 x3 */
				xmm4 = _mm_shuffle_ps(xmm1,xmm2, _MM_SHUFFLE(3,1,3,1)); /* y1 y2 y3 y3 */
							
				
			}
						
			jx = _mm_and_ps( mask, xmm6);
			jy = _mm_and_ps( mask, xmm4);
			jz = _mm_and_ps( mask, xmm5);
			
			dx   = _mm_sub_ps(ix, jx);
			dy   = _mm_sub_ps(iy, jy);
			dz   = _mm_sub_ps(iz, jz);
			
			fgb    = _mm_mul_ps(dva,dax); 
			fgb_ai = _mm_mul_ps(dvaj,dax_ai);
			fgb    = _mm_add_ps(fgb,fgb_ai);
		
			t1   = _mm_mul_ps(fgb,dx); /* fx1, fx2, fx3, fx4 */
			t2   = _mm_mul_ps(fgb,dy); /* fy1, fy2, fy3, fy4 */
			t3   = _mm_mul_ps(fgb,dz); /* fz1, fz2, fz3, fz4 */
							
			if(offset==1) {
				xmm1 = _mm_loadl_pi(xmm1, (__m64 *) (f+aj1)); /* fx1 fy1 */
				xmm7 = _mm_load1_ps(f+aj1+2); /* fz1 fz1 fz1 fz1 */
				
				xmm5 = _mm_shuffle_ps(xmm1,xmm1, _MM_SHUFFLE(0,0,0,0)); /* fx1 - - - */
				xmm6 = _mm_shuffle_ps(xmm1,xmm1, _MM_SHUFFLE(1,1,1,1)); /* fy1 - - - */
				
				xmm5 = _mm_sub_ps(xmm5,t1);
				xmm6 = _mm_sub_ps(xmm6,t2);
				xmm7 = _mm_sub_ps(xmm7,t3);
								
				_mm_store_ss(f+aj1 , xmm5);
				_mm_store_ss(f+aj1+1,xmm6);
				_mm_store_ss(f+aj1+2,xmm7);
				
			}
			else if(offset==2) {
				xmm1 = _mm_loadh_pi(xmm1, (__m64 *) (f+aj1)); /*fx1 fy1 - -*/ 
				xmm2 = _mm_loadh_pi(xmm2, (__m64 *) (f+aj2)); /*fx2 fy2 - - */
				
				xmm5 = _mm_load1_ps(f+aj1+2); /* fz1 fz1 fz1 fz1 */
				xmm6 = _mm_load1_ps(f+aj2+2); /* fz2 fz2 fz2 fz2 */
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(0,0,0,0)); /*fz1 fz1 fz2 fz2*/
				xmm7 = _mm_shuffle_ps(xmm5,xmm5,_MM_SHUFFLE(2,0,2,0)); /*fz1 fz2 fz1 fz2 */
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2)); /*x1 y1 x2 y2 */
				xmm5 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(2,0,2,0)); /*x1 x2 x1 x2 */
				xmm6 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,3,1)); /*y1 y2 y1 y2 */
				
				xmm5 = _mm_sub_ps(xmm5, t1);
				xmm6 = _mm_sub_ps(xmm6, t2);
				xmm7 = _mm_sub_ps(xmm7, t3);
				
				xmm1 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(1,0,1,0)); /*fx1 fx2 fy1 fy2*/
				xmm5 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,2,0)); /*fx1 fy1 fx2 fy2*/
				
				_mm_storel_pi( (__m64 *) (f+aj1), xmm5);
				_mm_storeh_pi( (__m64 *) (f+aj2), xmm5);
				
				_mm_store_ss(f+aj1+2,xmm7);
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(f+aj2+2,xmm7);
			}
			else {
				xmm1 = _mm_loadh_pi(xmm1, (__m64 *) (f+aj1)); /*fx1 fy1 - - */
				xmm2 = _mm_loadh_pi(xmm2, (__m64 *) (f+aj2)); /*fx2 fy2 - - */
				xmm3 = _mm_loadh_pi(xmm3, (__m64 *) (f+aj3)); /*fx3 fy3 - - */
				
				xmm5 = _mm_load1_ps(f+aj1+2); /* fz1 fz1 fz1 fz1 */
				xmm6 = _mm_load1_ps(f+aj2+2); /* fz2 fz2 fz2 fz2 */
				xmm7 = _mm_load1_ps(f+aj3+2); /* fz3 fz3 fz3 fz3 */
				
				xmm5 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(0,0,0,0)); /* fz1 fz1 fz2 fz2 */
				xmm6 = _mm_shuffle_ps(xmm7,xmm7, _MM_SHUFFLE(0,0,0,0)); /* fz3 fz3 fz3 fz3 */
				xmm7 = _mm_shuffle_ps(xmm5,xmm6, _MM_SHUFFLE(2,0,2,0)); /* fz1 fz2 fz3 fz4 */
				
				xmm1 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,2,3,2)); /*fx1 fy1 fx2 fy2 */
				xmm2 = _mm_shuffle_ps(xmm3,xmm3,_MM_SHUFFLE(3,2,3,2)); /*fx2 fy3 fx3 fy3 */
			
				xmm5 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(2,0,2,0)); /*fx1 fx2 fx3 fx3 */
				xmm6 = _mm_shuffle_ps(xmm1,xmm2,_MM_SHUFFLE(3,1,3,1)); /*fy1 fy2 fy3 fy3  */
				
				xmm5 = _mm_sub_ps(xmm5, t1);
				xmm6 = _mm_sub_ps(xmm6, t2);
				xmm7 = _mm_sub_ps(xmm7, t3);
				
				xmm1 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(1,0,1,0)); /*fx1 fx2 fy1 fy2 */
				xmm2 = _mm_shuffle_ps(xmm5,xmm6,_MM_SHUFFLE(3,2,3,2)); /*fx3 fx3 fy3 fy3 */
				xmm1 = _mm_shuffle_ps(xmm1,xmm1,_MM_SHUFFLE(3,1,2,0)); /*fx1 fy1 fx2 fy2 */
				xmm2 = _mm_shuffle_ps(xmm2,xmm2,_MM_SHUFFLE(3,1,2,0)); /*fx3 fy3 fx3 fy3 */
			
				_mm_storel_pi( (__m64 *) (f+aj1), xmm1);
				_mm_storeh_pi( (__m64 *) (f+aj2), xmm1);
				_mm_storel_pi( (__m64 *) (f+aj3), xmm2);
				
				_mm_store_ss(f+aj1+2,xmm7); /*fz1*/
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(f+aj2+2,xmm7); /*fz2*/
				xmm7 = _mm_shuffle_ps(xmm7,xmm7,_MM_SHUFFLE(0,3,2,1));
				_mm_store_ss(f+aj3+2,xmm7); /*fz3*/

			}
			
			t1 = _mm_and_ps( mask, t1);
			t2 = _mm_and_ps( mask, t2);
			t3 = _mm_and_ps( mask, t3);

			fix = _mm_add_ps(fix,t1);
			fiy = _mm_add_ps(fiy,t2);
			fiz = _mm_add_ps(fiz,t3);	
			
		} /*end offset!=0*/
	 
		/* fix/fiy/fiz now contain four partial force terms, that all should be
		* added to the i particle forces. 
		*/
		t1 = _mm_movehl_ps(t1,fix);
		t2 = _mm_movehl_ps(t2,fiy);
		t3 = _mm_movehl_ps(t3,fiz);
	
		fix = _mm_add_ps(fix,t1);
		fiy = _mm_add_ps(fiy,t2);
		fiz = _mm_add_ps(fiz,t3);
		
		t1 = _mm_shuffle_ps( fix, fix, _MM_SHUFFLE(1,1,1,1) );
		t2 = _mm_shuffle_ps( fiy, fiy, _MM_SHUFFLE(1,1,1,1) );
		t3 = _mm_shuffle_ps( fiz, fiz, _MM_SHUFFLE(1,1,1,1) );
		
		fix = _mm_add_ss(fix,t1); /*fx - - - */
		fiy = _mm_add_ss(fiy,t2); /*fy - - - */
		fiz = _mm_add_ss(fiz,t3); /*fz - - - */
		
		xmm2 = _mm_unpacklo_ps(fix,fiy); /*fx, fy, - - */
		xmm2 = _mm_movelh_ps(xmm2,fiz);
		xmm2 = _mm_and_ps(maski, xmm2);
		
		/* load i force from memory */
		xmm4 = _mm_loadl_pi(xmm4,(__m64 *) (f+ai3)); /*fx fy - -  */
		xmm5 = _mm_load1_ps(f+ai3+2); /* fz fz fz fz */
		xmm4 = _mm_shuffle_ps(xmm4,xmm5,_MM_SHUFFLE(3,2,1,0)); /*fx fy fz fz*/
		
		/* add to i force */
		xmm4 = _mm_add_ps(xmm4,xmm2);
		
		/* store i force to memory */
		_mm_storel_pi( (__m64 *) (f+ai3),xmm4); /* fx fy - - */
		xmm4 = _mm_shuffle_ps(xmm4,xmm4,_MM_SHUFFLE(2,2,2,2)); /* only the third term will be correct for fz */
		_mm_store_ss(f+ai3+2,xmm4); /*fz*/
	}	

	return 0;	
}






#else
/* keep compiler happy */
int genborn_sse_dummy;

#endif /* SSE intrinsics available */
