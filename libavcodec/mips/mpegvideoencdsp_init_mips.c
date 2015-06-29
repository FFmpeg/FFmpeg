/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavcodec/bit_depth_template.c"
#include "h263dsp_mips.h"

#if HAVE_MSA
static av_cold void mpegvideoencdsp_init_msa(MpegvideoEncDSPContext *c,
                                             AVCodecContext *avctx)
{
#if BIT_DEPTH == 8
    c->pix_sum = ff_pix_sum_msa;
#endif
}
#endif  // #if HAVE_MSA

av_cold void ff_mpegvideoencdsp_init_mips(MpegvideoEncDSPContext *c,
                                          AVCodecContext *avctx)
{
#if HAVE_MSA
    mpegvideoencdsp_init_msa(c, avctx);
#endif  // #if HAVE_MSA
}
