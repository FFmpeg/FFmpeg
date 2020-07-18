/*
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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
#include "h264chroma_mips.h"


av_cold void ff_h264chroma_init_mips(H264ChromaContext *c, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();
    int high_bit_depth = bit_depth > 8;

    /* MMI apears to be faster than MSA here */
    if (have_msa(cpu_flags)) {
        if (!high_bit_depth) {
            c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_msa;
            c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_msa;
            c->put_h264_chroma_pixels_tab[2] = ff_put_h264_chroma_mc2_msa;

            c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_msa;
            c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_msa;
            c->avg_h264_chroma_pixels_tab[2] = ff_avg_h264_chroma_mc2_msa;
        }
    }

    if (have_mmi(cpu_flags)) {
        if (!high_bit_depth) {
            c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_mmi;
            c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_mmi;
            c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_mmi;
            c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_mmi;
        }
    }
}
