/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

#include "libavutil/attributes.h"
#include "libavcodec/dsputil.h"
#include "dsputil_alpha.h"
#include "asm.h"

#if 0
/* These functions were the base for the optimized assembler routines,
   and remain here for documentation purposes.  */
static void put_pixels_clamped_mvi(const int16_t *block, uint8_t *pixels,
                                   ptrdiff_t line_size)
{
    int i = 8;
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */

    do {
        uint64_t shorts0, shorts1;

        shorts0 = ldq(block);
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);
        stl(pkwb(shorts0), pixels);

        shorts1 = ldq(block + 4);
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--i);
}

void add_pixels_clamped_mvi(const int16_t *block, uint8_t *pixels,
                            ptrdiff_t line_size)
{
    int h = 8;
    /* Keep this function a leaf function by generating the constants
       manually (mainly for the hack value ;-).  */
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */
    uint64_t signmask  = zap(-1, 0x33);
    signmask ^= signmask >> 1;  /* 0x8000800080008000 */

    do {
        uint64_t shorts0, pix0, signs0;
        uint64_t shorts1, pix1, signs1;

        shorts0 = ldq(block);
        shorts1 = ldq(block + 4);

        pix0    = unpkbw(ldl(pixels));
        /* Signed subword add (MMX paddw).  */
        signs0  = shorts0 & signmask;
        shorts0 &= ~signmask;
        shorts0 += pix0;
        shorts0 ^= signs0;
        /* Clamp. */
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);

        /* Next 4.  */
        pix1    = unpkbw(ldl(pixels + 4));
        signs1  = shorts1 & signmask;
        shorts1 &= ~signmask;
        shorts1 += pix1;
        shorts1 ^= signs1;
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);

        stl(pkwb(shorts0), pixels);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--h);
}
#endif

av_cold void ff_dsputil_init_alpha(DSPContext *c, AVCodecContext *avctx)
{
    /* amask clears all bits that correspond to present features.  */
    if (amask(AMASK_MVI) == 0) {
        c->sad[0]           = pix_abs16x16_mvi_asm;
        c->sad[1]           = pix_abs8x8_mvi;
        c->pix_abs[0][0]    = pix_abs16x16_mvi_asm;
        c->pix_abs[1][0]    = pix_abs8x8_mvi;
        c->pix_abs[0][1]    = pix_abs16x16_x2_mvi;
        c->pix_abs[0][2]    = pix_abs16x16_y2_mvi;
        c->pix_abs[0][3]    = pix_abs16x16_xy2_mvi;
    }

}
