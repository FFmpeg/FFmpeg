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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"
#include "idct.h"
#include "idctdsp_arm.h"

av_cold void ff_idctdsp_init_armv5te(IDCTDSPContext *c, AVCodecContext *avctx,
                                     unsigned high_bit_depth)
{
    if (!avctx->lowres && !high_bit_depth &&
        (avctx->idct_algo == FF_IDCT_AUTO ||
         avctx->idct_algo == FF_IDCT_SIMPLEAUTO ||
         avctx->idct_algo == FF_IDCT_SIMPLEARMV5TE)) {
        c->idct_put  = ff_simple_idct_put_armv5te;
        c->idct_add  = ff_simple_idct_add_armv5te;
        c->idct      = ff_simple_idct_armv5te;
        c->perm_type = FF_IDCT_PERM_NONE;
    }
}
