/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
 *                    Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "libavutil/mips/cpu.h"
#include "idctdsp_mips.h"
#include "xvididct_mips.h"

av_cold void ff_idctdsp_init_mips(IDCTDSPContext *c, AVCodecContext *avctx,
                          unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_mmi(cpu_flags)) {
        if ((avctx->lowres != 1) && (avctx->lowres != 2) && (avctx->lowres != 3) &&
            (avctx->bits_per_raw_sample != 10) &&
            (avctx->bits_per_raw_sample != 12) &&
            ((avctx->idct_algo == FF_IDCT_AUTO) || (avctx->idct_algo == FF_IDCT_SIMPLE))) {
                    c->idct_put = ff_simple_idct_put_8_mmi;
                    c->idct_add = ff_simple_idct_add_8_mmi;
                    c->idct = ff_simple_idct_8_mmi;
                    c->perm_type = FF_IDCT_PERM_NONE;
        }

        c->put_pixels_clamped = ff_put_pixels_clamped_mmi;
        c->add_pixels_clamped = ff_add_pixels_clamped_mmi;
        c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_mmi;
    }

    if (have_msa(cpu_flags)) {
        if ((avctx->lowres != 1) && (avctx->lowres != 2) && (avctx->lowres != 3) &&
            (avctx->bits_per_raw_sample != 10) &&
            (avctx->bits_per_raw_sample != 12) &&
            (avctx->idct_algo == FF_IDCT_AUTO)) {
                    c->idct_put = ff_simple_idct_put_msa;
                    c->idct_add = ff_simple_idct_add_msa;
                    c->idct = ff_simple_idct_msa;
                    c->perm_type = FF_IDCT_PERM_NONE;
        }

        c->put_pixels_clamped = ff_put_pixels_clamped_msa;
        c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_msa;
        c->add_pixels_clamped = ff_add_pixels_clamped_msa;
    }
}
