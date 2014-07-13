/*
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
#include "libavcodec/idctdsp.h"
#include "idctdsp_alpha.h"
#include "asm.h"

void put_pixels_clamped_mvi_asm(const int16_t *block, uint8_t *pixels,
                                int line_size);
void add_pixels_clamped_mvi_asm(const int16_t *block, uint8_t *pixels,
                                int line_size);

void (*put_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                 int line_size);
void (*add_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                 int line_size);

av_cold void ff_idctdsp_init_alpha(IDCTDSPContext *c, AVCodecContext *avctx,
                                   unsigned high_bit_depth)
{
    /* amask clears all bits that correspond to present features.  */
    if (amask(AMASK_MVI) == 0) {
        c->put_pixels_clamped = put_pixels_clamped_mvi_asm;
        c->add_pixels_clamped = add_pixels_clamped_mvi_asm;
    }

    put_pixels_clamped_axp_p = c->put_pixels_clamped;
    add_pixels_clamped_axp_p = c->add_pixels_clamped;

    if (!high_bit_depth && !avctx->lowres &&
        (avctx->idct_algo == FF_IDCT_AUTO ||
         avctx->idct_algo == FF_IDCT_SIMPLEALPHA)) {
        c->idct_put = ff_simple_idct_put_axp;
        c->idct_add = ff_simple_idct_add_axp;
        c->idct =     ff_simple_idct_axp;
    }
}
