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
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

static const enum AVPixelFormat pixel_formats[] = {
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV444P16,
};

static void randomize_buffers(int16_t *buf0, int16_t *buf1, int bit_depth, int width)
{
    int32_t *buf0_32 = (int32_t *) buf0;
    int32_t *buf1_32 = (int32_t *) buf1;
    int mask = (1 << bit_depth) - 1;
    int src_shift = bit_depth <= 14 ? 15 - bit_depth : 19 - bit_depth;
    for (int i = 0; i < width; i++) {
        int32_t r = rnd() & mask;
        if (bit_depth == 16) {
            buf0_32[i] = r << src_shift;
            buf1_32[i] = r << src_shift;
        } else {
            buf0[i] = r << src_shift;
            buf1[i] = r << src_shift;
        }
    }
}

static void check_lumConvertRange(int from)
{
    const char *func_str = from ? "lumRangeFromJpeg" : "lumRangeToJpeg";
#define LARGEST_INPUT_SIZE 1920
    static const int input_sizes[] = {8, LARGEST_INPUT_SIZE};
    SwsContext *sws;
    SwsInternal *c;

    LOCAL_ALIGNED_32(int16_t, dst0, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_32(int16_t, dst1, [LARGEST_INPUT_SIZE * 2]);
    int32_t *dst0_32 = (int32_t *) dst0;
    int32_t *dst1_32 = (int32_t *) dst1;

    declare_func(void, int16_t *dst, int width,
                       uint32_t coeff, int64_t offset);

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    sws->src_range = from;
    sws->dst_range = !from;

    for (int pfi = 0; pfi < FF_ARRAY_ELEMS(pixel_formats); pfi++) {
        enum AVPixelFormat pix_fmt = pixel_formats[pfi];
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
        int bit_depth = desc->comp[0].depth;
        int sample_size = bit_depth == 16 ? sizeof(int32_t) : sizeof(int16_t);
        int src_shift = bit_depth <= 14 ? 15 - bit_depth : 19 - bit_depth;
        int mpeg_min = 16 << (bit_depth - 8);
        int mpeg_max = 235 << (bit_depth - 8);
        int jpeg_max = (1 << bit_depth) - 1;
        sws->src_format = pix_fmt;
        sws->dst_format = pix_fmt;
        c->dstBpc = bit_depth;
        ff_sws_init_scale(c);
        for (int dstWi = 0; dstWi < FF_ARRAY_ELEMS(input_sizes); dstWi++) {
            int width = input_sizes[dstWi];
            if (check_func(c->lumConvertRange, "%s%d_%d", func_str, bit_depth, width)) {
                randomize_buffers(dst0, dst1, bit_depth, width);
                if (bit_depth == 16) {
                    if (!from) {
                        dst1_32[0] = dst0_32[0] = mpeg_min << src_shift;
                        dst1_32[1] = dst0_32[1] = mpeg_max << src_shift;
                    }
                    dst1_32[2] = dst0_32[2] = -1;
                } else {
                    if (!from) {
                        dst1[0] = dst0[0] = mpeg_min << src_shift;
                        dst1[1] = dst0[1] = mpeg_max << src_shift;
                    }
                    dst1[2] = dst0[2] = -1;
                }
                call_ref(dst0, width,
                         c->lumConvertRange_coeff, c->lumConvertRange_offset);
                call_new(dst1, width,
                         c->lumConvertRange_coeff, c->lumConvertRange_offset);
                if (memcmp(dst0, dst1, width * sample_size))
                    fail();
                if (!from) {
                    /* check that the mpeg range is respected */
                    if (bit_depth == 16) {
                        if ((dst1_32[0] >> src_shift) > 0 || (dst1_32[1] >> src_shift) != jpeg_max)
                            fail();
                    } else {
                        if ((dst1[0] >> src_shift) > 0 || (dst1[1] >> src_shift) != jpeg_max)
                            fail();
                    }
                }
                if (width == LARGEST_INPUT_SIZE && (bit_depth == 8 || bit_depth == 16))
                    bench_new(dst1, width,
                              c->lumConvertRange_coeff, c->lumConvertRange_offset);
            }
        }
    }

    sws_freeContext(sws);
}
#undef LARGEST_INPUT_SIZE

static void check_chrConvertRange(int from)
{
    const char *func_str = from ? "chrRangeFromJpeg" : "chrRangeToJpeg";
#define LARGEST_INPUT_SIZE 1920
    static const int input_sizes[] = {8, LARGEST_INPUT_SIZE};
    SwsContext *sws;
    SwsInternal *c;

    LOCAL_ALIGNED_32(int16_t, dstU0, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_32(int16_t, dstV0, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_32(int16_t, dstU1, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_32(int16_t, dstV1, [LARGEST_INPUT_SIZE * 2]);
    int32_t *dstU0_32 = (int32_t *) dstU0;
    int32_t *dstU1_32 = (int32_t *) dstU1;

    declare_func(void, int16_t *dstU, int16_t *dstV, int width,
                       uint32_t coeff, int64_t offset);

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    sws->src_range = from;
    sws->dst_range = !from;

    for (int pfi = 0; pfi < FF_ARRAY_ELEMS(pixel_formats); pfi++) {
        enum AVPixelFormat pix_fmt = pixel_formats[pfi];
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
        int bit_depth = desc->comp[0].depth;
        int sample_size = bit_depth == 16 ? sizeof(int32_t) : sizeof(int16_t);
        int src_shift = bit_depth <= 14 ? 15 - bit_depth : 19 - bit_depth;
        int mpeg_min = 16 << (bit_depth - 8);
        int mpeg_max = 240 << (bit_depth - 8);
        int jpeg_max = (1 << bit_depth) - 1;
        sws->src_format = pix_fmt;
        sws->dst_format = pix_fmt;
        c->dstBpc = bit_depth;
        ff_sws_init_scale(c);
        for (int dstWi = 0; dstWi < FF_ARRAY_ELEMS(input_sizes); dstWi++) {
            int width = input_sizes[dstWi];
            if (check_func(c->chrConvertRange, "%s%d_%d", func_str, bit_depth, width)) {
                randomize_buffers(dstU0, dstU1, bit_depth, width);
                randomize_buffers(dstV0, dstV1, bit_depth, width);
                if (bit_depth == 16) {
                    if (!from) {
                        dstU1_32[0] = dstU0_32[0] = mpeg_min << src_shift;
                        dstU1_32[1] = dstU0_32[1] = mpeg_max << src_shift;
                    }
                    dstU1_32[2] = dstU0_32[2] = -1;
                } else {
                    if (!from) {
                        dstU1[0] = dstU0[0] = mpeg_min << src_shift;
                        dstU1[1] = dstU0[1] = mpeg_max << src_shift;
                    }
                    dstU1[2] = dstU0[2] = -1;
                }
                call_ref(dstU0, dstV0, width,
                         c->chrConvertRange_coeff, c->chrConvertRange_offset);
                call_new(dstU1, dstV1, width,
                         c->chrConvertRange_coeff, c->chrConvertRange_offset);
                if (memcmp(dstU0, dstU1, width * sample_size) ||
                    memcmp(dstV0, dstV1, width * sample_size))
                    fail();
                if (!from) {
                    /* check that the mpeg range is respected */
                    if (bit_depth == 16) {
                        if ((dstU1_32[0] >> src_shift) > 0 || (dstU1_32[1] >> src_shift) != jpeg_max)
                            fail();
                    } else {
                        if ((dstU1[0] >> src_shift) > 0 || (dstU1[1] >> src_shift) != jpeg_max)
                            fail();
                    }
                }
                if (width == LARGEST_INPUT_SIZE && (bit_depth == 8 || bit_depth == 16))
                    bench_new(dstU1, dstV1, width,
                              c->chrConvertRange_coeff, c->chrConvertRange_offset);
            }
        }
    }

    sws_freeContext(sws);
}
#undef LARGEST_INPUT_SIZE

void checkasm_check_sw_range_convert(void)
{
    check_lumConvertRange(1);
    report("lumRangeFromJpeg");
    check_chrConvertRange(1);
    report("chrRangeFromJpeg");
    check_lumConvertRange(0);
    report("lumRangeToJpeg");
    check_chrConvertRange(0);
    report("chrRangeToJpeg");
}
