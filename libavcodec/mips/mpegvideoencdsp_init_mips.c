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

#include "libavutil/attributes.h"
#include "libavutil/mips/cpu.h"
#include "libavcodec/bit_depth_template.c"
#include "libavcodec/mpegvideoencdsp.h"
#include "h263dsp_mips.h"

av_cold void ff_mpegvideoencdsp_init_mips(MpegvideoEncDSPContext *c,
                                          AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_msa(cpu_flags)) {
#if BIT_DEPTH == 8
        c->pix_sum = ff_pix_sum_msa;
#endif
    }
}
