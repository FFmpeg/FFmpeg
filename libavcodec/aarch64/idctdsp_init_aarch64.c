/*
 * ARM-NEON-optimized IDCT functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2017 Matthieu Bouron <matthieu.bouron@gmail.com>
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
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"
#include "idct.h"

void ff_put_pixels_clamped_neon(const int16_t *, uint8_t *, ptrdiff_t);
void ff_put_signed_pixels_clamped_neon(const int16_t *, uint8_t *, ptrdiff_t);
void ff_add_pixels_clamped_neon(const int16_t *, uint8_t *, ptrdiff_t);

av_cold void ff_idctdsp_init_aarch64(IDCTDSPContext *c, AVCodecContext *avctx,
                                     unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        if (!avctx->lowres && !high_bit_depth) {
            if (avctx->idct_algo == FF_IDCT_AUTO ||
                avctx->idct_algo == FF_IDCT_SIMPLEAUTO ||
                avctx->idct_algo == FF_IDCT_SIMPLENEON) {
                c->idct_put  = ff_simple_idct_put_neon;
                c->idct_add  = ff_simple_idct_add_neon;
                c->idct      = ff_simple_idct_neon;
                c->perm_type = FF_IDCT_PERM_PARTTRANS;
            }
        }

        c->add_pixels_clamped        = ff_add_pixels_clamped_neon;
        c->put_pixels_clamped        = ff_put_pixels_clamped_neon;
        c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_neon;
    }
}
