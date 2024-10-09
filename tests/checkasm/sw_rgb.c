/*
 *
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

#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static const uint8_t width[] = {12, 16, 20, 32, 36, 128};
static const struct {uint8_t w, h, s;} planes[] = {
    {12,16,12}, {16,16,16}, {20,23,25}, {32,18,48}, {8,128,16}, {128,128,128}
};

#define MAX_STRIDE 128
#define MAX_HEIGHT 128

static void check_shuffle_bytes(void * func, const char * report)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE]);

    declare_func(void, const uint8_t *src, uint8_t *dst, int src_size);

    memset(dst0, 0, MAX_STRIDE);
    memset(dst1, 0, MAX_STRIDE);
    randomize_buffers(src0, MAX_STRIDE);
    memcpy(src1, src0, MAX_STRIDE);

    if (check_func(func, "%s", report)) {
        for (i = 0; i < 6; i ++) {
            call_ref(src0, dst0, width[i]);
            call_new(src1, dst1, width[i]);
            if (memcmp(dst0, dst1, MAX_STRIDE))
                fail();
        }
        bench_new(src0, dst0, width[5]);
    }
}

static void check_uyvy_to_422p(void)
{
    int i;

    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_1, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);

    declare_func(void, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                 const uint8_t *src, int width, int height,
                 int lumStride, int chromStride, int srcStride);

    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT * 2);
    memcpy(src1, src0, MAX_STRIDE * MAX_HEIGHT * 2);

    if (check_func(uyvytoyuv422, "uyvytoyuv422")) {
        for (i = 0; i < 6; i ++) {
            memset(dst_y_0, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_y_1, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_u_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_u_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);

            call_ref(dst_y_0, dst_u_0, dst_v_0, src0, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            call_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            if (memcmp(dst_y_0, dst_y_1, MAX_STRIDE * MAX_HEIGHT) ||
                memcmp(dst_u_0, dst_u_1, (MAX_STRIDE/2) * MAX_HEIGHT) ||
                memcmp(dst_v_0, dst_v_1, (MAX_STRIDE/2) * MAX_HEIGHT))
                fail();
        }
        bench_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[5].w, planes[5].h,
                  MAX_STRIDE, MAX_STRIDE / 2, planes[5].s);
    }
}

#define NUM_LINES 5
#define MAX_LINE_SIZE 1920
#define BUFSIZE (NUM_LINES * MAX_LINE_SIZE)

static int cmp_off_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    for (size_t i = 0; i < n; i++) {
        if (abs(ref[i] - test[i]) > accuracy)
            return 1;
    }
    return 0;
}

static void check_rgb24toyv12(SwsInternal *ctx)
{
    static const int input_sizes[] = {16, 128, 512, MAX_LINE_SIZE, -MAX_LINE_SIZE};

    LOCAL_ALIGNED_32(uint8_t, src, [BUFSIZE * 3]);
    LOCAL_ALIGNED_32(uint8_t, buf_y_0, [BUFSIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf_y_1, [BUFSIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf_u_0, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_u_1, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_v_0, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_v_1, [BUFSIZE / 4]);

    declare_func(void, const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                       uint8_t *vdst, int width, int height, int lumStride,
                       int chromStride, int srcStride, int32_t *rgb2yuv);

    randomize_buffers(src, BUFSIZE * 3);

    for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
        int input_size = input_sizes[isi];
        int negstride = input_size < 0;
        const char *negstride_str = negstride ? "_negstride" : "";
        int width = FFABS(input_size);
        int linesize = width + 32;
        /* calculate height based on specified width to use the entire buffer. */
        int height = (BUFSIZE / linesize) & ~1;
        uint8_t *src0 = src;
        uint8_t *src1 = src;
        uint8_t *dst_y_0 = buf_y_0;
        uint8_t *dst_y_1 = buf_y_1;
        uint8_t *dst_u_0 = buf_u_0;
        uint8_t *dst_u_1 = buf_u_1;
        uint8_t *dst_v_0 = buf_v_0;
        uint8_t *dst_v_1 = buf_v_1;

        if (negstride) {
            src0    += (height - 1) * (linesize * 3);
            src1    += (height - 1) * (linesize * 3);
            dst_y_0 += (height - 1) * linesize;
            dst_y_1 += (height - 1) * linesize;
            dst_u_0 += ((height / 2) - 1) * (linesize / 2);
            dst_u_1 += ((height / 2) - 1) * (linesize / 2);
            dst_v_0 += ((height / 2) - 1) * (linesize / 2);
            dst_v_1 += ((height / 2) - 1) * (linesize / 2);
            linesize *= -1;
        }

        if (check_func(ff_rgb24toyv12, "rgb24toyv12_%d_%d%s", width, height, negstride_str)) {
            memset(buf_y_0, 0xFF, BUFSIZE);
            memset(buf_y_1, 0xFF, BUFSIZE);
            memset(buf_u_0, 0xFF, BUFSIZE / 4);
            memset(buf_u_1, 0xFF, BUFSIZE / 4);
            memset(buf_v_0, 0xFF, BUFSIZE / 4);
            memset(buf_v_1, 0xFF, BUFSIZE / 4);

            call_ref(src0, dst_y_0, dst_u_0, dst_v_0, width, height,
                     linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
            call_new(src1, dst_y_1, dst_u_1, dst_v_1, width, height,
                     linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
            if (cmp_off_by_n(buf_y_0, buf_y_1, BUFSIZE, 1) ||
                cmp_off_by_n(buf_u_0, buf_u_1, BUFSIZE / 4, 1) ||
                cmp_off_by_n(buf_v_0, buf_v_1, BUFSIZE / 4, 1))
                fail();
            bench_new(src1, dst_y_1, dst_u_1, dst_v_1, width, height,
                      linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE
#undef BUFSIZE

static void check_interleave_bytes(void)
{
    LOCAL_ALIGNED_16(uint8_t, src0_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, src1_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst0_buf, [2*MAX_STRIDE*MAX_HEIGHT+2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_buf, [2*MAX_STRIDE*MAX_HEIGHT+2]);
    // Intentionally using unaligned buffers, as this function doesn't have
    // any alignment requirements.
    uint8_t *src0 = src0_buf + 1;
    uint8_t *src1 = src1_buf + 1;
    uint8_t *dst0 = dst0_buf + 2;
    uint8_t *dst1 = dst1_buf + 2;

    declare_func(void, const uint8_t *, const uint8_t *,
                 uint8_t *, int, int, int, int, int);

    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT);
    randomize_buffers(src1, MAX_STRIDE * MAX_HEIGHT);

    if (check_func(interleaveBytes, "interleave_bytes")) {
        for (int i = 0; i <= 16; i++) {
            // Try all widths [1,16], and try one random width.

            int w = i > 0 ? i : (1 + (rnd() % (MAX_STRIDE-2)));
            int h = 1 + (rnd() % (MAX_HEIGHT-2));

            int src0_offset = 0, src0_stride = MAX_STRIDE;
            int src1_offset = 0, src1_stride = MAX_STRIDE;
            int dst_offset  = 0, dst_stride  = 2 * MAX_STRIDE;

            memset(dst0, 0, 2 * MAX_STRIDE * MAX_HEIGHT);
            memset(dst1, 0, 2 * MAX_STRIDE * MAX_HEIGHT);

            // Try different combinations of negative strides
            if (i & 1) {
                src0_offset = (h-1)*src0_stride;
                src0_stride = -src0_stride;
            }
            if (i & 2) {
                src1_offset = (h-1)*src1_stride;
                src1_stride = -src1_stride;
            }
            if (i & 4) {
                dst_offset = (h-1)*dst_stride;
                dst_stride = -dst_stride;
            }

            call_ref(src0 + src0_offset, src1 + src1_offset, dst0 + dst_offset,
                     w, h, src0_stride, src1_stride, dst_stride);
            call_new(src0 + src0_offset, src1 + src1_offset, dst1 + dst_offset,
                     w, h, src0_stride, src1_stride, dst_stride);
            // Check a one pixel-pair edge around the destination area,
            // to catch overwrites past the end.
            checkasm_check(uint8_t, dst0, 2*MAX_STRIDE, dst1, 2*MAX_STRIDE,
                           2 * w + 2, h + 1, "dst");
        }

        bench_new(src0, src1, dst1, 127, MAX_HEIGHT,
                  MAX_STRIDE, MAX_STRIDE, 2*MAX_STRIDE);
    }
    if (check_func(interleaveBytes, "interleave_bytes_aligned")) {
        // Bench the function in a more typical case, with aligned
        // buffers and widths.
        bench_new(src0_buf, src1_buf, dst1_buf, 128, MAX_HEIGHT,
                  MAX_STRIDE, MAX_STRIDE, 2*MAX_STRIDE);
    }
}

static void check_deinterleave_bytes(void)
{
    LOCAL_ALIGNED_16(uint8_t, src_buf,  [2*MAX_STRIDE*MAX_HEIGHT+2]);
    LOCAL_ALIGNED_16(uint8_t, dst0_u_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst0_v_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst1_u_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst1_v_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    // Intentionally using unaligned buffers, as this function doesn't have
    // any alignment requirements.
    uint8_t *src = src_buf + 2;
    uint8_t *dst0_u = dst0_u_buf + 1;
    uint8_t *dst0_v = dst0_v_buf + 1;
    uint8_t *dst1_u = dst1_u_buf + 1;
    uint8_t *dst1_v = dst1_v_buf + 1;

    declare_func(void, const uint8_t *src, uint8_t *dst1, uint8_t *dst2,
                       int width, int height, int srcStride,
                       int dst1Stride, int dst2Stride);

    randomize_buffers(src, 2*MAX_STRIDE*MAX_HEIGHT);

    if (check_func(deinterleaveBytes, "deinterleave_bytes")) {
        for (int i = 0; i <= 16; i++) {
            // Try all widths [1,16], and try one random width.

            int w = i > 0 ? i : (1 + (rnd() % (MAX_STRIDE-2)));
            int h = 1 + (rnd() % (MAX_HEIGHT-2));

            int src_offset   = 0, src_stride    = 2 * MAX_STRIDE;
            int dst_u_offset = 0, dst_u_stride  = MAX_STRIDE;
            int dst_v_offset = 0, dst_v_stride  = MAX_STRIDE;

            memset(dst0_u, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst0_v, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst1_u, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst1_v, 0, MAX_STRIDE * MAX_HEIGHT);

            // Try different combinations of negative strides
            if (i & 1) {
                src_offset = (h-1)*src_stride;
                src_stride = -src_stride;
            }
            if (i & 2) {
                dst_u_offset = (h-1)*dst_u_stride;
                dst_u_stride = -dst_u_stride;
            }
            if (i & 4) {
                dst_v_offset = (h-1)*dst_v_stride;
                dst_v_stride = -dst_v_stride;
            }

            call_ref(src + src_offset, dst0_u + dst_u_offset, dst0_v + dst_v_offset,
                     w, h, src_stride, dst_u_stride, dst_v_stride);
            call_new(src + src_offset, dst1_u + dst_u_offset, dst1_v + dst_v_offset,
                     w, h, src_stride, dst_u_stride, dst_v_stride);
            // Check a one pixel-pair edge around the destination area,
            // to catch overwrites past the end.
            checkasm_check(uint8_t, dst0_u, MAX_STRIDE, dst1_u, MAX_STRIDE,
                           w + 1, h + 1, "dst_u");
            checkasm_check(uint8_t, dst0_v, MAX_STRIDE, dst1_v, MAX_STRIDE,
                           w + 1, h + 1, "dst_v");
        }

        bench_new(src, dst1_u, dst1_v, 127, MAX_HEIGHT,
                  2*MAX_STRIDE, MAX_STRIDE, MAX_STRIDE);
    }
    if (check_func(deinterleaveBytes, "deinterleave_bytes_aligned")) {
        // Bench the function in a more typical case, with aligned
        // buffers and widths.
        bench_new(src_buf, dst1_u_buf, dst1_v_buf, 128, MAX_HEIGHT,
                  2*MAX_STRIDE, MAX_STRIDE, MAX_STRIDE);
    }
}

#define MAX_LINE_SIZE 1920
static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE};
static const enum AVPixelFormat rgb_formats[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_ARGB,
};

static void check_rgb_to_y(SwsInternal *ctx)
{
    LOCAL_ALIGNED_16(uint8_t, src24,  [MAX_LINE_SIZE * 3]);
    LOCAL_ALIGNED_16(uint8_t, src32,  [MAX_LINE_SIZE * 4]);
    LOCAL_ALIGNED_32(uint8_t, dst0_y, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_y, [MAX_LINE_SIZE * 2]);

    declare_func(void, uint8_t *dst, const uint8_t *src,
                 const uint8_t *unused1, const uint8_t *unused2, int width,
                 uint32_t *rgb2yuv, void *opq);

    randomize_buffers(src24, MAX_LINE_SIZE * 3);
    randomize_buffers(src32, MAX_LINE_SIZE * 4);

    for (int i = 0; i < FF_ARRAY_ELEMS(rgb_formats); i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(rgb_formats[i]);

        ctx->srcFormat = rgb_formats[i];
        ff_sws_init_scale(ctx);

        for (int j = 0; j < FF_ARRAY_ELEMS(input_sizes); j++) {
            int w = input_sizes[j];

            if (check_func(ctx->lumToYV12, "%s_to_y_%d", desc->name, w)) {
                const uint8_t *src = desc->nb_components == 3 ? src24 : src32;
                memset(dst0_y, 0xFA, MAX_LINE_SIZE * 2);
                memset(dst1_y, 0xFA, MAX_LINE_SIZE * 2);

                call_ref(dst0_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);
                call_new(dst1_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);

                if (memcmp(dst0_y, dst1_y, w * 2))
                    fail();

                if (desc->nb_components == 3 ||
                    // only bench native endian formats
                    (ctx->srcFormat == AV_PIX_FMT_RGB32 || ctx->srcFormat == AV_PIX_FMT_RGB32_1))
                    bench_new(dst1_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);
            }
        }
    }
}

static void check_rgb_to_uv(SwsInternal *ctx)
{
    LOCAL_ALIGNED_16(uint8_t, src24,  [MAX_LINE_SIZE * 3]);
    LOCAL_ALIGNED_16(uint8_t, src32,  [MAX_LINE_SIZE * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst0_u, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst0_v, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_u, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_v, [MAX_LINE_SIZE * 2]);

    declare_func(void, uint8_t *dstU, uint8_t *dstV,
                 const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
                 int width, uint32_t *pal, void *opq);

    randomize_buffers(src24, MAX_LINE_SIZE * 3);
    randomize_buffers(src32, MAX_LINE_SIZE * 4);

    for (int i = 0; i < 2 * FF_ARRAY_ELEMS(rgb_formats); i++) {
        enum AVPixelFormat src_fmt = rgb_formats[i / 2];
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src_fmt);

        ctx->chrSrcHSubSample = (i % 2) ? 0 : 1;
        ctx->srcFormat = src_fmt;
        ctx->dstFormat = ctx->chrSrcHSubSample ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV444P;
        ff_sws_init_scale(ctx);

        for (int j = 0; j < FF_ARRAY_ELEMS(input_sizes); j++) {
            int w = input_sizes[j] >> ctx->chrSrcHSubSample;

            if (check_func(ctx->chrToYV12, "%s_to_uv%s_%d", desc->name,
                           ctx->chrSrcHSubSample ? "_half" : "",
                           input_sizes[j])) {
                const uint8_t *src = desc->nb_components == 3 ? src24 : src32;
                memset(dst0_u, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst0_v, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst1_u, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst1_v, 0xFF, MAX_LINE_SIZE * 2);

                call_ref(dst0_u, dst0_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);
                call_new(dst1_u, dst1_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);

                if (memcmp(dst0_u, dst1_u, w * 2) || memcmp(dst0_v, dst1_v, w * 2))
                    fail();

                if (desc->nb_components == 3 ||
                    // only bench native endian formats
                    (ctx->srcFormat == AV_PIX_FMT_RGB32 || ctx->srcFormat == AV_PIX_FMT_RGB32_1))
                    bench_new(dst1_u, dst1_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);
            }
        }
    }
}

void checkasm_check_sw_rgb(void)
{
    SwsContext *sws;
    SwsInternal *c;

    ff_sws_rgb2rgb_init();

    check_shuffle_bytes(shuffle_bytes_2103, "shuffle_bytes_2103");
    report("shuffle_bytes_2103");

    check_shuffle_bytes(shuffle_bytes_0321, "shuffle_bytes_0321");
    report("shuffle_bytes_0321");

    check_shuffle_bytes(shuffle_bytes_1230, "shuffle_bytes_1230");
    report("shuffle_bytes_1230");

    check_shuffle_bytes(shuffle_bytes_3012, "shuffle_bytes_3012");
    report("shuffle_bytes_3012");

    check_shuffle_bytes(shuffle_bytes_3210, "shuffle_bytes_3210");
    report("shuffle_bytes_3210");

    check_uyvy_to_422p();
    report("uyvytoyuv422");

    check_interleave_bytes();
    report("interleave_bytes");

    check_deinterleave_bytes();
    report("deinterleave_bytes");

    sws = sws_getContext(MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_RGB24,
                         MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_YUV420P,
                         SWS_ACCURATE_RND | SWS_BITEXACT, NULL, NULL, NULL);
    if (!sws)
        fail();

    c = sws_internal(sws);
    check_rgb_to_y(c);
    report("rgb_to_y");

    check_rgb_to_uv(c);
    report("rgb_to_uv");

    check_rgb24toyv12(c);
    report("rgb24toyv12");

    sws_freeContext(sws);
}
