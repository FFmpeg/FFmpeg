/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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
#include "dsputil_arm.h"

void ff_simple_idct_armv6(DCTELEM *data);
void ff_simple_idct_put_armv6(uint8_t *dest, int line_size, DCTELEM *data);
void ff_simple_idct_add_armv6(uint8_t *dest, int line_size, DCTELEM *data);

void av_cold ff_dsputil_init_armv6(DSPContext* c, AVCodecContext *avctx)
{
    if (!avctx->lowres && (avctx->idct_algo == FF_IDCT_AUTO ||
                           avctx->idct_algo == FF_IDCT_SIMPLEARMV6)) {
        c->idct_put              = ff_simple_idct_put_armv6;
        c->idct_add              = ff_simple_idct_add_armv6;
        c->idct                  = ff_simple_idct_armv6;
        c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
    }
}
