/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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

#include "pixblockdsp_mips.h"

#if HAVE_MSA
static av_cold void pixblockdsp_init_msa(PixblockDSPContext *c,
                                         AVCodecContext *avctx,
                                         unsigned high_bit_depth)
{
    c->diff_pixels = ff_diff_pixels_msa;

    switch (avctx->bits_per_raw_sample) {
    case 9:
    case 10:
    case 12:
    case 14:
        c->get_pixels = ff_get_pixels_16_msa;
        break;
    default:
        if (avctx->bits_per_raw_sample <= 8 || avctx->codec_type !=
            AVMEDIA_TYPE_VIDEO) {
            c->get_pixels = ff_get_pixels_8_msa;
        }
        break;
    }
}
#endif  // #if HAVE_MSA

#if HAVE_MMI
static av_cold void pixblockdsp_init_mmi(PixblockDSPContext *c,
        AVCodecContext *avctx, unsigned high_bit_depth)
{
    c->diff_pixels = ff_diff_pixels_mmi;

    if (!high_bit_depth || avctx->codec_type != AVMEDIA_TYPE_VIDEO) {
        c->get_pixels = ff_get_pixels_8_mmi;
    }
}
#endif /* HAVE_MMI */

void ff_pixblockdsp_init_mips(PixblockDSPContext *c, AVCodecContext *avctx,
                              unsigned high_bit_depth)
{
#if HAVE_MMI
    pixblockdsp_init_mmi(c, avctx, high_bit_depth);
#endif /* HAVE_MMI */
#if HAVE_MSA
    pixblockdsp_init_msa(c, avctx, high_bit_depth);
#endif  // #if HAVE_MSA
}
