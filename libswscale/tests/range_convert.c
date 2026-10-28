/*
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

#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale_internal.h"

int main(void)
{
    static const enum AVPixelFormat formats[] = {
        AV_PIX_FMT_YUV444P,   AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16,
    };
    const int cpu_flags = av_get_cpu_flags();
    SwsContext *sws = sws_alloc_context();
    SwsInternal *c;

    if (!sws)
        return 1;
    c = sws_internal(sws);

    for (int native = 0; native < 2; native++) {
        av_force_cpu_flags(native ? cpu_flags : 0);
        for (int p = 0; p < FF_ARRAY_ELEMS(formats); p++) {
            const int depth = av_pix_fmt_desc_get(formats[p])->comp[0].depth;
            const int shift = (depth <= 14 ? 15 : 19) - depth;
            const int center = 128 << (depth - 8);
            const int samples[] = {
                0, 16 << (depth - 8), center - 1, center,
                center + 1, 235 << (depth - 8), 240 << (depth - 8),
                (1 << depth) - 1,
            };
            DECLARE_ALIGNED(32, int16_t, u16)[16];
            DECLARE_ALIGNED(32, int16_t, v16)[16];
            DECLARE_ALIGNED(32, int32_t, u32)[16];
            DECLARE_ALIGNED(32, int32_t, v32)[16];

            sws->src_format = sws->dst_format = formats[p];
            c->dstBpc = depth;
            for (int from = 0; from < 2; from++) {
                sws->src_range = from;
                sws->dst_range = !from;
                ff_sws_init_range_convert(c);
                for (int i = 0; i < 16; i++) {
                    const int u = samples[i % 8] << shift;
                    const int v = samples[7 - i % 8] << shift;
                    if (depth == 16) {
                        u32[i] = u;
                        v32[i] = v;
                    } else {
                        u16[i] = u;
                        v16[i] = v;
                    }
                }
                c->chrConvertRange(depth == 16 ? (int16_t *) u32 : u16,
                                   depth == 16 ? (int16_t *) v32 : v16, 16,
                                   c->chrConvertRange_coeff,
                                   c->chrConvertRange_offset);
                printf("%s %2d-bit %s U:", native ? "native" : "c     ",
                       depth, from ? "full-to-limited" : "limited-to-full");
                for (int i = 0; i < 8; i++)
                    printf(" %d", depth == 16 ? u32[i] : u16[i]);
                printf(" V:");
                for (int i = 0; i < 8; i++)
                    printf(" %d", depth == 16 ? v32[i] : v16[i]);
                printf("\n");
            }
        }
    }

    av_force_cpu_flags(cpu_flags);
    sws_freeContext(sws);
    return 0;
}
