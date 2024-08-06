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

static const int dst_fmts[] = {
//     AV_PIX_FMT_BGR48BE,
//     AV_PIX_FMT_BGR48LE,
//     AV_PIX_FMT_RGB48BE,
//     AV_PIX_FMT_RGB48LE,
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB565,
    AV_PIX_FMT_BGR565,
    AV_PIX_FMT_RGB555,
    AV_PIX_FMT_BGR555,
//     AV_PIX_FMT_RGB444,
//     AV_PIX_FMT_BGR444,
//     AV_PIX_FMT_RGB8,
//     AV_PIX_FMT_BGR8,
//     AV_PIX_FMT_RGB4,
//     AV_PIX_FMT_BGR4,
//     AV_PIX_FMT_RGB4_BYTE,
//     AV_PIX_FMT_BGR4_BYTE,
//     AV_PIX_FMT_MONOBLACK,
    AV_PIX_FMT_GBRP,
};

static int cmp_off_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    for (size_t i = 0; i < n; i++) {
        if (abs(ref[i] - test[i]) > accuracy)
            return 1;
    }
    return 0;
}

static int cmp_555_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    const uint16_t *ref16  = (const uint16_t *) ref;
    const uint16_t *test16 = (const uint16_t *) test;
    for (size_t i = 0; i < n; i++) {
        if (abs(( ref16[i]        & 0x1f) - ( test16[i]        & 0x1f)) > accuracy)
            return 1;
        if (abs(((ref16[i] >>  5) & 0x1f) - ((test16[i] >>  5) & 0x1f)) > accuracy)
            return 1;
        if (abs(((ref16[i] >> 10) & 0x1f) - ((test16[i] >> 10) & 0x1f)) > accuracy)
            return 1;
    }
    return 0;
}

static int cmp_565_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    const uint16_t *ref16  = (const uint16_t *) ref;
    const uint16_t *test16 = (const uint16_t *) test;
    for (size_t i = 0; i < n; i++) {
        if (abs(( ref16[i]        & 0x1f) - ( test16[i]        & 0x1f)) > accuracy)
            return 1;
        if (abs(((ref16[i] >>  5) & 0x3f) - ((test16[i] >>  5) & 0x3f)) > accuracy)
            return 1;
        if (abs(((ref16[i] >> 11) & 0x1f) - ((test16[i] >> 11) & 0x1f)) > accuracy)
            return 1;
    }
    return 0;
}

static void check_yuv2rgb(int src_pix_fmt)
{
    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
#define MAX_LINE_SIZE 1920
    static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE};

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      int, SwsContext *c, const uint8_t *src[],
                           int srcStride[], int srcSliceY, int srcSliceH,
                           uint8_t *dst[], int dstStride[]);

    LOCAL_ALIGNED_8(uint8_t, src_y, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_8(uint8_t, src_u, [MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, src_v, [MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, src_a, [MAX_LINE_SIZE * 2]);
    const uint8_t *src[4] = { src_y, src_u, src_v, src_a };

    LOCAL_ALIGNED_8(uint8_t, dst0_0, [2 * MAX_LINE_SIZE * 6]);
    LOCAL_ALIGNED_8(uint8_t, dst0_1, [2 * MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, dst0_2, [2 * MAX_LINE_SIZE]);
    uint8_t *dst0[4] = { dst0_0, dst0_1, dst0_2 };
    uint8_t *lines0[4][2] = {
        { dst0_0, dst0_0 + MAX_LINE_SIZE * 6 },
        { dst0_1, dst0_1 + MAX_LINE_SIZE },
        { dst0_2, dst0_2 + MAX_LINE_SIZE }
    };

    LOCAL_ALIGNED_8(uint8_t, dst1_0, [2 * MAX_LINE_SIZE * 6]);
    LOCAL_ALIGNED_8(uint8_t, dst1_1, [2 * MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, dst1_2, [2 * MAX_LINE_SIZE]);
    uint8_t *dst1[4] = { dst1_0, dst1_1, dst1_2 };
    uint8_t *lines1[4][2] = {
        { dst1_0, dst1_0 + MAX_LINE_SIZE * 6 },
        { dst1_1, dst1_1 + MAX_LINE_SIZE },
        { dst1_2, dst1_2 + MAX_LINE_SIZE }
    };

    randomize_buffers(src_y, MAX_LINE_SIZE * 2);
    randomize_buffers(src_u, MAX_LINE_SIZE);
    randomize_buffers(src_v, MAX_LINE_SIZE);
    randomize_buffers(src_a, MAX_LINE_SIZE * 2);

    for (int dfi = 0; dfi < FF_ARRAY_ELEMS(dst_fmts); dfi++) {
        int dst_pix_fmt = dst_fmts[dfi];
        const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
        int sample_size = av_get_padded_bits_per_pixel(dst_desc) >> 3;
        for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
            struct SwsContext *ctx;
            int log_level;
            int width = input_sizes[isi];
            int srcSliceY = 0;
            int srcSliceH = 2;
            int srcStride[4] = {
                width,
                width >> src_desc->log2_chroma_w,
                width >> src_desc->log2_chroma_w,
                width,
            };
            int dstStride[4] = {
                MAX_LINE_SIZE * 6,
                MAX_LINE_SIZE,
                MAX_LINE_SIZE,
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
                memset(dst0_0, 0xFF, 2 * MAX_LINE_SIZE * 6);
                memset(dst1_0, 0xFF, 2 * MAX_LINE_SIZE * 6);
                if (dst_pix_fmt == AV_PIX_FMT_GBRP) {
                    memset(dst0_1, 0xFF, MAX_LINE_SIZE);
                    memset(dst0_2, 0xFF, MAX_LINE_SIZE);
                    memset(dst1_1, 0xFF, MAX_LINE_SIZE);
                    memset(dst1_2, 0xFF, MAX_LINE_SIZE);
                }

                call_ref(ctx, src, srcStride, srcSliceY,
                         srcSliceH, dst0, dstStride);
                call_new(ctx, src, srcStride, srcSliceY,
                         srcSliceH, dst1, dstStride);

                if (dst_pix_fmt == AV_PIX_FMT_ARGB  ||
                    dst_pix_fmt == AV_PIX_FMT_ABGR  ||
                    dst_pix_fmt == AV_PIX_FMT_RGBA  ||
                    dst_pix_fmt == AV_PIX_FMT_BGRA  ||
                    dst_pix_fmt == AV_PIX_FMT_RGB24 ||
                    dst_pix_fmt == AV_PIX_FMT_BGR24) {
                    if (cmp_off_by_n(lines0[0][0], lines1[0][0], width * sample_size, 3) ||
                        cmp_off_by_n(lines0[0][1], lines1[0][1], width * sample_size, 3))
                        fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_RGB565 ||
                           dst_pix_fmt == AV_PIX_FMT_BGR565) {
                    if (cmp_565_by_n(lines0[0][0], lines1[0][0], width, 2) ||
                        cmp_565_by_n(lines0[0][1], lines1[0][1], width, 2))
                        fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_RGB555 ||
                           dst_pix_fmt == AV_PIX_FMT_BGR555) {
                    if (cmp_555_by_n(lines0[0][0], lines1[0][0], width, 2) ||
                        cmp_555_by_n(lines0[0][1], lines1[0][1], width, 2))
                        fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_GBRP) {
                    for (int p = 0; p < 3; p++)
                        for (int l = 0; l < 2; l++)
                            if (cmp_off_by_n(lines0[p][l], lines1[p][l], width, 3))
                                fail();
                } else {
                    fail();
                }

                bench_new(ctx, src, srcStride, srcSliceY,
                          srcSliceH, dst0, dstStride);
            }
            sws_freeContext(ctx);
        }
    }
}

#undef MAX_LINE_SIZE

void checkasm_check_sw_yuv2rgb(void)
{
    check_yuv2rgb(AV_PIX_FMT_YUV420P);
    report("yuv420p");
    check_yuv2rgb(AV_PIX_FMT_YUV422P);
    report("yuv422p");
    check_yuv2rgb(AV_PIX_FMT_YUVA420P);
    report("yuva420p");
}
