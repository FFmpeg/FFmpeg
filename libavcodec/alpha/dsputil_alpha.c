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
