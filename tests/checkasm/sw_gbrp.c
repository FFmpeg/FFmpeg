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
    struct SwsContext *ctx;
    const AVPixFmtDescriptor *desc;
    int fmi, fsi, isi, i;
    int dstW, byte_size, luma_filter_size, chr_filter_size;
#define LARGEST_FILTER 16
#define FILTER_SIZES 4
    static const int filter_sizes[] = {1, 4, 8, 16};
#define LARGEST_INPUT_SIZE 512
#define INPUT_SIZES 6
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    uint8_t *dst0[4];
    uint8_t *dst1[4];

    declare_func(void, void *c, const int16_t *lumFilter,
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

    ctx = sws_alloc_context();
    if (sws_init_context(ctx, NULL, NULL) < 0)
        fail();

    ctx->flags |= SWS_FULL_CHR_H_INT;
    ctx->yuv2rgb_y_offset  = rnd();
    ctx->yuv2rgb_y_coeff   = rnd();
    ctx->yuv2rgb_v2r_coeff = rnd();
    ctx->yuv2rgb_v2g_coeff = rnd();
    ctx->yuv2rgb_u2g_coeff = rnd();
    ctx->yuv2rgb_u2b_coeff = rnd();

    for (fmi = 0; fmi < FF_ARRAY_ELEMS(planar_fmts); fmi++) {
        for (fsi = 0; fsi < FILTER_SIZES; fsi++) {
            for (isi = 0; isi < INPUT_SIZES; isi++ ) {
                desc = av_pix_fmt_desc_get(planar_fmts[fmi]);
                ctx->dstFormat = planar_fmts[fmi];

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

                ff_sws_init_scale(ctx);
                if (check_func(ctx->yuv2anyX, "yuv2%s_full_X_%d_%d", desc->name, luma_filter_size, dstW)) {
                    for (i = 0; i < 4; i ++) {
                        memset(dst0[i], 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                        memset(dst1[i], 0xFF, LARGEST_INPUT_SIZE * sizeof(int32_t));
                    }

                    call_ref(ctx, luma_filter, luma, luma_filter_size,
                             chr_filter, chru, chrv, chr_filter_size,
                             alpha, dst0, dstW, 0);
                    call_new(ctx, luma_filter, luma, luma_filter_size,
                             chr_filter, chru, chrv, chr_filter_size,
                             alpha, dst1, dstW, 0);

                    if (memcmp(dst0[0], dst1[0], dstW * byte_size) ||
                        memcmp(dst0[1], dst1[1], dstW * byte_size) ||
                        memcmp(dst0[2], dst1[2], dstW * byte_size) ||
                        memcmp(dst0[3], dst1[3], dstW * byte_size) )
                        fail();

                    bench_new(ctx, luma_filter, luma, luma_filter_size,
                              chr_filter, chru, chrv, chr_filter_size,
                              alpha, dst1, dstW, 0);
                }
            }
        }
    }
    sws_freeContext(ctx);
}


void checkasm_check_sw_gbrp(void)
{
    check_output_yuv2gbrp();
    report("output_yuv2gbrp");
}
