/*
 * Copyright (c) 2020 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include "h264chroma_lasx.h"
#include "libavutil/attributes.h"
#include "libavutil/loongarch/cpu.h"
#include "libavcodec/h264chroma.h"

av_cold void ff_h264chroma_init_loongarch(H264ChromaContext *c, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_lasx(cpu_flags)) {
        if (bit_depth <= 8) {
            c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_lasx;
            c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_lasx;
            c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_lasx;
        }
    }
}
