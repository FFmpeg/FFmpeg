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
#include "libavutil/imgutils.h"
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
    AV_PIX_FMT_RGB565LE,
    AV_PIX_FMT_BGR565LE,
    AV_PIX_FMT_RGB555LE,
    AV_PIX_FMT_BGR555LE,
    AV_PIX_FMT_RGB565BE,
    AV_PIX_FMT_BGR565BE,
    AV_PIX_FMT_RGB555BE,
    AV_PIX_FMT_BGR555BE,
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

static int cmp_555_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy, int is_be)
{
    for (size_t i = 0; i < n; i++) {
        uint16_t r = is_be ? AV_RB16(ref  + i * 2) : AV_RL16(ref  + i * 2);
        uint16_t t = is_be ? AV_RB16(test + i * 2) : AV_RL16(test + i * 2);
        if (abs(( r        & 0x1f) - ( t        & 0x1f)) > accuracy)
            return 1;
        if (abs(((r >>  5) & 0x1f) - ((t >>  5) & 0x1f)) > accuracy)
            return 1;
        if (abs(((r >> 10) & 0x1f) - ((t >> 10) & 0x1f)) > accuracy)
            return 1;
    }
    return 0;
}

static int cmp_565_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy, int is_be)
{
    for (size_t i = 0; i < n; i++) {
        uint16_t r = is_be ? AV_RB16(ref  + i * 2) : AV_RL16(ref  + i * 2);
        uint16_t t = is_be ? AV_RB16(test + i * 2) : AV_RL16(test + i * 2);
        if (abs(( r        & 0x1f) - ( t        & 0x1f)) > accuracy)
            return 1;
        if (abs(((r >>  5) & 0x3f) - ((t >>  5) & 0x3f)) > accuracy)
            return 1;
        if (abs(((r >> 11) & 0x1f) - ((t >> 11) & 0x1f)) > accuracy)
            return 1;
    }
    return 0;
}

static void check_yuv2rgb(int src_pix_fmt)
{
    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
#define MAX_LINE_SIZE 1920
#define SRC_STRIDE_PAD 32
#define NUM_LINES 4
    static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE};

    declare_func(int, SwsInternal *c, const uint8_t *const src[],
                      const int srcStride[], int srcSliceY, int srcSliceH,
                      uint8_t *const dst[], const int dstStride[]);

    LOCAL_ALIGNED_8(uint8_t, src_y, [(MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, src_u, [(MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, src_v, [(MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, src_a, [(MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES]);
    const uint8_t *src[4] = { src_y, src_u, src_v, src_a };

    LOCAL_ALIGNED_8(uint8_t, dst0_0, [NUM_LINES * MAX_LINE_SIZE * 6]);
    LOCAL_ALIGNED_8(uint8_t, dst0_1, [NUM_LINES * MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, dst0_2, [NUM_LINES * MAX_LINE_SIZE]);
    uint8_t *dst0[4] = { dst0_0, dst0_1, dst0_2 };

    LOCAL_ALIGNED_8(uint8_t, dst1_0, [NUM_LINES * MAX_LINE_SIZE * 6]);
    LOCAL_ALIGNED_8(uint8_t, dst1_1, [NUM_LINES * MAX_LINE_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, dst1_2, [NUM_LINES * MAX_LINE_SIZE]);
    uint8_t *dst1[4] = { dst1_0, dst1_1, dst1_2 };

    randomize_buffers(src_y, (MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES);
    randomize_buffers(src_u, (MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES);
    randomize_buffers(src_v, (MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES);
    randomize_buffers(src_a, (MAX_LINE_SIZE + SRC_STRIDE_PAD) * NUM_LINES);

    for (int dfi = 0; dfi < FF_ARRAY_ELEMS(dst_fmts); dfi++) {
        int dst_pix_fmt = dst_fmts[dfi];
        const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
        int sample_size = av_get_padded_bits_per_pixel(dst_desc) >> 3;
        for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
            SwsContext *sws;
            SwsInternal *c;
            int log_level;
            int width = input_sizes[isi];
            int srcSliceY = 0;
            int srcSliceH = NUM_LINES;
            /* Use av_image_get_linesize so that semi-planar formats (NV12,
             * NV21) get the correct interleaved-UV stride (= width bytes),
             * not (width >> log2_chroma_w) which would only count UV pairs. */
            int chroma_linesize = av_image_get_linesize(src_pix_fmt, width, 1);
            int srcStride[4] = {
                width + SRC_STRIDE_PAD,
                chroma_linesize + SRC_STRIDE_PAD,
                chroma_linesize + SRC_STRIDE_PAD,
                width + SRC_STRIDE_PAD,
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
            sws = sws_getContext(width, srcSliceH, src_pix_fmt,
                                 width, srcSliceH, dst_pix_fmt,
                                 0, NULL, NULL, NULL);
            av_log_set_level(log_level);
            if (!sws)
                fail();

            c = sws_internal(sws);
            if (check_func(c->convert_unscaled, "%s_%s_%d", src_desc->name, dst_desc->name, width)) {
                memset(dst0_0, 0xFF, NUM_LINES * MAX_LINE_SIZE * 6);
                memset(dst1_0, 0xFF, NUM_LINES * MAX_LINE_SIZE * 6);
                if (dst_pix_fmt == AV_PIX_FMT_GBRP) {
                    memset(dst0_1, 0xFF, NUM_LINES * MAX_LINE_SIZE);
                    memset(dst0_2, 0xFF, NUM_LINES * MAX_LINE_SIZE);
                    memset(dst1_1, 0xFF, NUM_LINES * MAX_LINE_SIZE);
                    memset(dst1_2, 0xFF, NUM_LINES * MAX_LINE_SIZE);
                }

                call_ref(c, src, srcStride, srcSliceY,
                         srcSliceH, dst0, dstStride);
                call_new(c, src, srcStride, srcSliceY,
                         srcSliceH, dst1, dstStride);

                if (dst_pix_fmt == AV_PIX_FMT_ARGB  ||
                    dst_pix_fmt == AV_PIX_FMT_ABGR  ||
                    dst_pix_fmt == AV_PIX_FMT_RGBA  ||
                    dst_pix_fmt == AV_PIX_FMT_BGRA  ||
                    dst_pix_fmt == AV_PIX_FMT_RGB24 ||
                    dst_pix_fmt == AV_PIX_FMT_BGR24) {
                    for (int row = 0; row < srcSliceH; row++)
                        if (cmp_off_by_n(dst0_0 + row * dstStride[0],
                                         dst1_0 + row * dstStride[0],
                                         width * sample_size, 3))
                            fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_RGB565LE ||
                           dst_pix_fmt == AV_PIX_FMT_BGR565LE ||
                           dst_pix_fmt == AV_PIX_FMT_RGB565BE ||
                           dst_pix_fmt == AV_PIX_FMT_BGR565BE) {
                    int is_be = dst_pix_fmt == AV_PIX_FMT_RGB565BE ||
                                dst_pix_fmt == AV_PIX_FMT_BGR565BE;
                    for (int row = 0; row < srcSliceH; row++)
                        if (cmp_565_by_n(dst0_0 + row * dstStride[0],
                                         dst1_0 + row * dstStride[0],
                                         width, 2, is_be))
                            fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_RGB555LE ||
                           dst_pix_fmt == AV_PIX_FMT_BGR555LE ||
                           dst_pix_fmt == AV_PIX_FMT_RGB555BE ||
                           dst_pix_fmt == AV_PIX_FMT_BGR555BE) {
                    int is_be = dst_pix_fmt == AV_PIX_FMT_RGB555BE ||
                                dst_pix_fmt == AV_PIX_FMT_BGR555BE;
                    for (int row = 0; row < srcSliceH; row++)
                        if (cmp_555_by_n(dst0_0 + row * dstStride[0],
                                         dst1_0 + row * dstStride[0],
                                         width, 2, is_be))
                            fail();
                } else if (dst_pix_fmt == AV_PIX_FMT_GBRP) {
                    for (int p = 0; p < 3; p++)
                        for (int row = 0; row < srcSliceH; row++)
                            if (cmp_off_by_n(dst0[p] + row * dstStride[p],
                                             dst1[p] + row * dstStride[p],
                                             width, 3))
                                fail();
                } else {
                    fail();
                }

                bench_new(c, src, srcStride, srcSliceY,
                          srcSliceH, dst0, dstStride);
            }
            sws_freeContext(sws);
        }
    }
}

#undef NUM_LINES
#undef SRC_STRIDE_PAD
#undef MAX_LINE_SIZE

void checkasm_check_sw_yuv2rgb(void)
{
    check_yuv2rgb(AV_PIX_FMT_YUV420P);
    report("yuv420p");
    check_yuv2rgb(AV_PIX_FMT_YUV422P);
    report("yuv422p");
    check_yuv2rgb(AV_PIX_FMT_YUVA420P);
    report("yuva420p");
    check_yuv2rgb(AV_PIX_FMT_NV12);
    report("nv12");
    check_yuv2rgb(AV_PIX_FMT_NV21);
    report("nv21");
}
