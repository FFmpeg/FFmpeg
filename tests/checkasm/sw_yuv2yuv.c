/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        for (int j = 0; j < size; j += 4) \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static void check_semiplanar(int dst_pix_fmt)
{
    static const int src_fmts[] = {
        AV_PIX_FMT_NV24,
        AV_PIX_FMT_NV42,
    };
    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
#define NUM_LINES 4
#define MAX_LINE_SIZE 1920
    static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE};

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      int, SwsContext *c, const uint8_t *src[],
                           int srcStride[], int srcSliceY, int srcSliceH,
                           uint8_t *dst[], int dstStride[]);

    LOCAL_ALIGNED_8(uint8_t, src_y,  [MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, src_uv, [MAX_LINE_SIZE * NUM_LINES * 2]);
    const uint8_t *src[4] = { src_y, src_uv };

    LOCAL_ALIGNED_8(uint8_t, dst0_y, [MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, dst0_u, [MAX_LINE_SIZE * NUM_LINES / 2]);
    LOCAL_ALIGNED_8(uint8_t, dst0_v, [MAX_LINE_SIZE * NUM_LINES / 2]);
    uint8_t *dst0[4] = { dst0_y, dst0_u, dst0_v };

    LOCAL_ALIGNED_8(uint8_t, dst1_y, [MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, dst1_u, [MAX_LINE_SIZE * NUM_LINES / 2]);
    LOCAL_ALIGNED_8(uint8_t, dst1_v, [MAX_LINE_SIZE * NUM_LINES / 2]);
    uint8_t *dst1[4] = { dst1_y, dst1_u, dst1_v };

    randomize_buffers(src_y,  MAX_LINE_SIZE * NUM_LINES);
    randomize_buffers(src_uv, MAX_LINE_SIZE * NUM_LINES * 2);

    for (int sfi = 0; sfi < FF_ARRAY_ELEMS(src_fmts); sfi++) {
        int src_pix_fmt = src_fmts[sfi];
        const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
        for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
            struct SwsContext *ctx;
            int log_level;
            int width = input_sizes[isi];
            int srcSliceY = 0;
            int srcSliceH = NUM_LINES;
            int srcStride[4] = {
                MAX_LINE_SIZE,
                MAX_LINE_SIZE * 2,
            };
            int dstStride[4] = {
                MAX_LINE_SIZE,
                MAX_LINE_SIZE >> dst_desc->log2_chroma_w,
                MAX_LINE_SIZE >> dst_desc->log2_chroma_w,
            };

            // override log level to prevent spamming of the message
            // "No accelerated colorspace conversion found from %s to %s"
            log_level = av_log_get_level();
            av_log_set_level(AV_LOG_ERROR);
            ctx = sws_getContext(width, srcSliceH, src_pix_fmt,
                                 width, srcSliceH, dst_pix_fmt,
                                 0, NULL, NULL, NULL);
            av_log_set_level(log_level);
            if (!ctx)
                fail();

            if (check_func(ctx->convert_unscaled, "%s_%s_%d", src_desc->name, dst_desc->name, width)) {
                memset(dst0_y, 0xFF, MAX_LINE_SIZE * NUM_LINES);
                memset(dst0_u, 0xFF, MAX_LINE_SIZE * NUM_LINES / 2);
                memset(dst0_v, 0xFF, MAX_LINE_SIZE * NUM_LINES / 2);
                memset(dst1_y, 0xFF, MAX_LINE_SIZE * NUM_LINES);
                memset(dst1_u, 0xFF, MAX_LINE_SIZE * NUM_LINES / 2);
                memset(dst1_v, 0xFF, MAX_LINE_SIZE * NUM_LINES / 2);

                call_ref(ctx, src, srcStride, srcSliceY,
                         srcSliceH, dst0, dstStride);
                call_new(ctx, src, srcStride, srcSliceY,
                         srcSliceH, dst1, dstStride);

                if (memcmp(dst0_y, dst1_y, MAX_LINE_SIZE * NUM_LINES) ||
                    memcmp(dst0_u, dst1_u, MAX_LINE_SIZE * NUM_LINES / 2) ||
                    memcmp(dst0_v, dst1_v, MAX_LINE_SIZE * NUM_LINES / 2))
                    fail();

                bench_new(ctx, src, srcStride, srcSliceY,
                          srcSliceH, dst0, dstStride);
            }
            sws_freeContext(ctx);
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE

void checkasm_check_sw_yuv2yuv(void)
{
    check_semiplanar(AV_PIX_FMT_YUV420P);
    report("yuv420p");
}
