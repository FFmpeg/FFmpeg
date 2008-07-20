/*
 * GMC (Global Motion Compensation)
 * AltiVec-enabled
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/dsputil.h"

#include "gcc_fixes.h"

#include "dsputil_ppc.h"
#include "util_altivec.h"

/*
  altivec-enhanced gmc1. ATM this code assume stride is a multiple of 8,
  to preserve proper dst alignment.
*/
#define GMC1_PERF_COND (h==8)
void gmc1_altivec(uint8_t *dst /* align 8 */, uint8_t *src /* align1 */, int stride, int h, int x16, int y16, int rounder)
{
POWERPC_PERF_DECLARE(altivec_gmc1_num, GMC1_PERF_COND);
    const DECLARE_ALIGNED_16(unsigned short, rounder_a[8]) =
        {rounder, rounder, rounder, rounder,
         rounder, rounder, rounder, rounder};
    const DECLARE_ALIGNED_16(unsigned short, ABCD[8]) =
        {
            (16-x16)*(16-y16), /* A */
            (   x16)*(16-y16), /* B */
            (16-x16)*(   y16), /* C */
            (   x16)*(   y16), /* D */
            0, 0, 0, 0         /* padding */
        };
    register const vector unsigned char vczero = (const vector unsigned char)vec_splat_u8(0);
    register const vector unsigned short vcsr8 = (const vector unsigned short)vec_splat_u16(8);
    register vector unsigned char dstv, dstv2, src_0, src_1, srcvA, srcvB, srcvC, srcvD;
    register vector unsigned short Av, Bv, Cv, Dv, rounderV, tempA, tempB, tempC, tempD;
    int i;
    unsigned long dst_odd = (unsigned long)dst & 0x0000000F;
    unsigned long src_really_odd = (unsigned long)src & 0x0000000F;


POWERPC_PERF_START_COUNT(altivec_gmc1_num, GMC1_PERF_COND);

    tempA = vec_ld(0, (unsigned short*)ABCD);
    Av = vec_splat(tempA, 0);
    Bv = vec_splat(tempA, 1);
    Cv = vec_splat(tempA, 2);
    Dv = vec_splat(tempA, 3);

    rounderV = vec_ld(0, (unsigned short*)rounder_a);

    // we'll be able to pick-up our 9 char elements
    // at src from those 32 bytes
    // we load the first batch here, as inside the loop
    // we can re-use 'src+stride' from one iteration
    // as the 'src' of the next.
    src_0 = vec_ld(0, src);
    src_1 = vec_ld(16, src);
    srcvA = vec_perm(src_0, src_1, vec_lvsl(0, src));

    if (src_really_odd != 0x0000000F) {
        // if src & 0xF == 0xF, then (src+1) is properly aligned
        // on the second vector.
        srcvB = vec_perm(src_0, src_1, vec_lvsl(1, src));
    } else {
        srcvB = src_1;
    }
    srcvA = vec_mergeh(vczero, srcvA);
    srcvB = vec_mergeh(vczero, srcvB);

    for(i=0; i<h; i++) {
        dst_odd = (unsigned long)dst & 0x0000000F;
        src_really_odd = (((unsigned long)src) + stride) & 0x0000000F;

        dstv = vec_ld(0, dst);

        // we we'll be able to pick-up our 9 char elements
        // at src + stride from those 32 bytes
        // then reuse the resulting 2 vectors srvcC and srcvD
        // as the next srcvA and srcvB
        src_0 = vec_ld(stride + 0, src);
        src_1 = vec_ld(stride + 16, src);
        srcvC = vec_perm(src_0, src_1, vec_lvsl(stride + 0, src));

        if (src_really_odd != 0x0000000F) {
            // if src & 0xF == 0xF, then (src+1) is properly aligned
            // on the second vector.
            srcvD = vec_perm(src_0, src_1, vec_lvsl(stride + 1, src));
        } else {
            srcvD = src_1;
        }

        srcvC = vec_mergeh(vczero, srcvC);
        srcvD = vec_mergeh(vczero, srcvD);


        // OK, now we (finally) do the math :-)
        // those four instructions replaces 32 int muls & 32 int adds.
        // isn't AltiVec nice ?
        tempA = vec_mladd((vector unsigned short)srcvA, Av, rounderV);
        tempB = vec_mladd((vector unsigned short)srcvB, Bv, tempA);
        tempC = vec_mladd((vector unsigned short)srcvC, Cv, tempB);
        tempD = vec_mladd((vector unsigned short)srcvD, Dv, tempC);

        srcvA = srcvC;
        srcvB = srcvD;

        tempD = vec_sr(tempD, vcsr8);

        dstv2 = vec_pack(tempD, (vector unsigned short)vczero);

        if (dst_odd) {
            dstv2 = vec_perm(dstv, dstv2, vcprm(0,1,s0,s1));
        } else {
            dstv2 = vec_perm(dstv, dstv2, vcprm(s0,s1,2,3));
        }

        vec_st(dstv2, 0, dst);

        dst += stride;
        src += stride;
    }

POWERPC_PERF_STOP_COUNT(altivec_gmc1_num, GMC1_PERF_COND);
}
