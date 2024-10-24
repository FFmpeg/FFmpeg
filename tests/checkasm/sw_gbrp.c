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

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static const int planar_fmts[] = {
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9BE,
    AV_PIX_FMT_GBRP9LE,
    AV_PIX_FMT_GBRP10BE,
    AV_PIX_FMT_GBRP10LE,
    AV_PIX_FMT_GBRP12BE,
    AV_PIX_FMT_GBRP12LE,
    AV_PIX_FMT_GBRP14BE,
    AV_PIX_FMT_GBRP14LE,
    AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRAP10BE,
    AV_PIX_FMT_GBRAP10LE,
    AV_PIX_FMT_GBRAP12BE,
    AV_PIX_FMT_GBRAP12LE,
    AV_PIX_FMT_GBRP16BE,
    AV_PIX_FMT_GBRP16LE,
    AV_PIX_FMT_GBRAP16BE,
    AV_PIX_FMT_GBRAP16LE,
    AV_PIX_FMT_GBRPF32BE,
    AV_PIX_FMT_GBRPF32LE,
    AV_PIX_FMT_GBRAPF32BE,
    AV_PIX_FMT_GBRAPF32LE
};

static void check_output_yuv2gbrp(void)
{
    SwsContext *sws;
    SwsInternal *c;
    const AVPixFmtDescriptor *desc;
    int fmi, fsi, isi, i;
    int dstW, byte_size, luma_filter_size, chr_filter_size;
#define LARGEST_FILTER 16
#define FILTER_SIZES 4
    static const int filter_sizes[] = {1, 4, 8, 16};
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    uint8_t *dst0[4];
    uint8_t *dst1[4];

    declare_func(void, SwsInternal *c, const int16_t *lumFilter,
                       const int16_t **lumSrcx, int lumFilterSize,
                       const int16_t *chrFilter, const int16_t **chrUSrcx,
                       const int16_t **chrVSrcx, int chrFilterSize,
                       const int16_t **alpSrcx, uint8_t **dest,
                       int dstW, int y);

    const int16_t *luma[LARGEST_FILTER];
    const int16_t *chru[LARGEST_FILTER];
    const int16_t *chrv[LARGEST_FILTER];
    const int16_t *alpha[LARGEST_FILTER];

    LOCAL_ALIGNED_8(int16_t, luma_filter, [LARGEST_FILTER]);
    LOCAL_ALIGNED_8(int16_t, chr_filter, [LARGEST_FILTER]);

    LOCAL_ALIGNED_8(int32_t, src_y, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_u, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_v, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0_r, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst0_g, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst0_b, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst0_a, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    LOCAL_ALIGNED_8(uint8_t, dst1_r, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_g, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_b, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_a, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    randomize_buffers((uint8_t*)src_y, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_u, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_v, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)luma_filter, LARGEST_FILTER * sizeof(int16_t));
    randomize_buffers((uint8_t*)chr_filter, LARGEST_FILTER * sizeof(int16_t));

    dst0[0] = (uint8_t*)dst0_g;
    dst0[1] = (uint8_t*)dst0_b;
    dst0[2] = (uint8_t*)dst0_r;
    dst0[3] = (uint8_t*)dst0_a;

    dst1[0] = (uint8_t*)dst1_g;
    dst1[1] = (uint8_t*)dst1_b;
    dst1[2] = (uint8_t*)dst1_r;
    dst1[3] = (uint8_t*)dst1_a;

    for (i = 0; i < LARGEST_FILTER; i++) {
        luma[i] =  (int16_t *)(src_y + i*LARGEST_INPUT_SIZE);
        chru[i] =  (int16_t *)(src_u + i*LARGEST_INPUT_SIZE);
        chrv[i] =  (int16_t *)(src_v + i*LARGEST_INPUT_SIZE);
        alpha[i] = (int16_t *)(src_a + i*LARGEST_INPUT_SIZE);
    }

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    sws->flags |= SWS_FULL_CHR_H_INT;

    for (fmi = 0; fmi < FF_ARRAY_ELEMS(planar_fmts); fmi++) {
        for (fsi = 0; fsi < FILTER_SIZES; fsi++) {
            for (isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++ ) {
                desc = av_pix_fmt_desc_get(planar_fmts[fmi]);
                sws->dst_format = planar_fmts[fmi];

                dstW = input_sizes[isi];
                luma_filter_size = filter_sizes[fsi];
                chr_filter_size = filter_sizes[fsi];

                if (desc->comp[0].depth > 16) {
                    byte_size = 4;
                } else if (desc->comp[0].depth > 8) {
                    byte_size = 2;
                } else {
                    byte_size = 1;
                }

                ff_sws_init_scale(c);
                if (check_func(c->yuv2anyX, "yuv2%s_full_X_%d_%d", desc->name, luma_filter_size, dstW)) {
                    for (i = 0; i < 4; i ++) {
                        memset(dst0[i], 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                        memset(dst1[i], 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                    }

                    call_ref(c, luma_filter, luma, luma_filter_size,
                             chr_filter, chru, chrv, chr_filter_size,
                             alpha, dst0, dstW, 0);
                    call_new(c, luma_filter, luma, luma_filter_size,
                             chr_filter, chru, chrv, chr_filter_size,
                             alpha, dst1, dstW, 0);

                    if (memcmp(dst0[0], dst1[0], dstW * byte_size) ||
                        memcmp(dst0[1], dst1[1], dstW * byte_size) ||
                        memcmp(dst0[2], dst1[2], dstW * byte_size) ||
                        memcmp(dst0[3], dst1[3], dstW * byte_size) )
                        fail();

                    bench_new(c, luma_filter, luma, luma_filter_size,
                              chr_filter, chru, chrv, chr_filter_size,
                              alpha, dst1, dstW, 0);
                }
            }
        }
    }
    sws_freeContext(sws);
}

#undef LARGEST_INPUT_SIZE

static void check_input_planar_rgb_to_y(void)
{
    SwsContext *sws;
    SwsInternal *c;
    const AVPixFmtDescriptor *desc;
    int fmi, isi;
    int dstW, byte_size;
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    const uint8_t *src[4];
    int32_t rgb2yuv[9] = {0};

    declare_func(void, uint8_t *dst, const uint8_t *src[4],
                       int w, int32_t *rgb2yuv, void *opaque);

    LOCAL_ALIGNED_8(int32_t, src_r, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_g, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_b, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [LARGEST_INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0_y, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_y, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    randomize_buffers((uint8_t*)src_r, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_g, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_b, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)rgb2yuv, 9 * sizeof(int32_t));

    src[0] = (uint8_t*)src_g;
    src[1] = (uint8_t*)src_b;
    src[2] = (uint8_t*)src_r;
    src[3] = (uint8_t*)src_a;

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    for (fmi = 0; fmi < FF_ARRAY_ELEMS(planar_fmts); fmi++) {
        for (isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++ ) {
            desc = av_pix_fmt_desc_get(planar_fmts[fmi]);
            sws->src_format = planar_fmts[fmi];
            sws->dst_format = AV_PIX_FMT_YUVA444P16;
            byte_size = 2;
            dstW = input_sizes[isi];

            ff_sws_init_scale(c);
            if(check_func(c->readLumPlanar, "planar_%s_to_y_%d",  desc->name, dstW)) {
                memset(dst0_y, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                memset(dst1_y, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));

                call_ref(dst0_y, src, dstW, rgb2yuv, NULL);
                call_new(dst1_y, src, dstW, rgb2yuv, NULL);

                if (memcmp(dst0_y, dst1_y, dstW * byte_size))
                    fail();

                bench_new(dst1_y, src, dstW, rgb2yuv, NULL);

            }
        }
    }
    sws_freeContext(sws);
}

#undef LARGEST_INPUT_SIZE

static void check_input_planar_rgb_to_uv(void)
{
    SwsContext *sws;
    SwsInternal *c;
    const AVPixFmtDescriptor *desc;
    int fmi, isi;
    int dstW, byte_size;
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    const uint8_t *src[4];
    int32_t rgb2yuv[9] = {0};

    declare_func(void, uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *src[4], int w, int32_t *rgb2yuv, void *opaque);

    LOCAL_ALIGNED_8(int32_t, src_r, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_g, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_b, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [LARGEST_INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0_u, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst0_v, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    LOCAL_ALIGNED_8(uint8_t, dst1_u, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_v, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    randomize_buffers((uint8_t*)src_r, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_g, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_b, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)rgb2yuv, 9 * sizeof(int32_t));

    src[0] = (uint8_t*)src_g;
    src[1] = (uint8_t*)src_b;
    src[2] = (uint8_t*)src_r;
    src[3] = (uint8_t*)src_a;

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    for (fmi = 0; fmi < FF_ARRAY_ELEMS(planar_fmts); fmi++) {
        for (isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++ ) {
            desc = av_pix_fmt_desc_get(planar_fmts[fmi]);
            sws->src_format = planar_fmts[fmi];
            sws->dst_format = AV_PIX_FMT_YUVA444P16;
            byte_size = 2;
            dstW = input_sizes[isi];

            ff_sws_init_scale(c);
            if(check_func(c->readChrPlanar, "planar_%s_to_uv_%d",  desc->name, dstW)) {
                memset(dst0_u, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                memset(dst0_v, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                memset(dst1_u, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                memset(dst1_v, 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));

                call_ref(dst0_u, dst0_v, src, dstW, rgb2yuv, NULL);
                call_new(dst1_u, dst1_v, src, dstW, rgb2yuv, NULL);

                if (memcmp(dst0_u, dst1_u, dstW * byte_size) ||
                    memcmp(dst0_v, dst1_v, dstW * byte_size))
                    fail();

                bench_new(dst1_u, dst1_v, src, dstW, rgb2yuv, NULL);
            }
        }
    }
    sws_freeContext(sws);
}

#undef LARGEST_INPUT_SIZE

static void check_input_planar_rgb_to_a(void)
{
    SwsContext *sws;
    SwsInternal *c;
    const AVPixFmtDescriptor *desc;
    int fmi, isi;
    int dstW, byte_size;
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    const uint8_t *src[4];
    int32_t rgb2yuv[9] = {0};

    declare_func(void, uint8_t *dst, const uint8_t *src[4],
                       int w, int32_t *rgb2yuv, void *opaque);

    LOCAL_ALIGNED_8(int32_t, src_r, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_g, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_b, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [LARGEST_INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0_a, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);
    LOCAL_ALIGNED_8(uint8_t, dst1_a, [LARGEST_INPUT_SIZE * sizeof(int32_t)]);

    randomize_buffers((uint8_t*)src_r, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_g, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_b, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, LARGEST_INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)rgb2yuv, 9 * sizeof(int32_t));

    src[0] = (uint8_t*)src_g;
    src[1] = (uint8_t*)src_b;
    src[2] = (uint8_t*)src_r;
    src[3] = (uint8_t*)src_a;

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    for (fmi = 0; fmi < FF_ARRAY_ELEMS(planar_fmts); fmi++) {
        for (isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++ ) {
            desc = av_pix_fmt_desc_get(planar_fmts[fmi]);
            if (!(desc->flags & AV_PIX_FMT_FLAG_ALPHA))
                continue;

            sws->src_format = planar_fmts[fmi];
            sws->dst_format = AV_PIX_FMT_YUVA444P16;
            byte_size = 2;
            dstW = input_sizes[isi];

            ff_sws_init_scale(c);
            if(check_func(c->readAlpPlanar, "planar_%s_to_a_%d",  desc->name, dstW)) {
                memset(dst0_a, 0x00, LARGEST_INPUT_SIZE * sizeof(int32_t));
                memset(dst1_a, 0x00, LARGEST_INPUT_SIZE * sizeof(int32_t));

                call_ref(dst0_a, src, dstW, rgb2yuv, NULL);
                call_new(dst1_a, src, dstW, rgb2yuv, NULL);

                if (memcmp(dst0_a, dst1_a, dstW * byte_size))
                    fail();
                bench_new(dst1_a, src, dstW, rgb2yuv, NULL);
            }
        }
    }
    sws_freeContext(sws);
}

void checkasm_check_sw_gbrp(void)
{
    check_output_yuv2gbrp();
    report("output_yuv2gbrp");

    check_input_planar_rgb_to_y();
    report("input_planar_rgb_y");

    check_input_planar_rgb_to_uv();
    report("input_planar_rgb_uv");

    check_input_planar_rgb_to_a();
    report("input_planar_rgb_a");
}
