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

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

// This reference function is the same approximate algorithm employed by the
// SIMD functions
static void ref_function(const int16_t *filter, int filterSize,
                                                 const int16_t **src, uint8_t *dest, int dstW,
                                                 const uint8_t *dither, int offset)
{
    int i, d;
    d = ((filterSize - 1) * 8 + dither[0]) >> 4;
    for ( i = 0; i < dstW; i++) {
        int16_t val = d;
        int j;
        union {
            int val;
            int16_t v[2];
        } t;
        for (j = 0; j < filterSize; j++){
            t.val = (int)src[j][i + offset] * (int)filter[j];
            val += t.v[1];
        }
        dest[i]= av_clip_uint8(val>>3);
    }
}

static void check_yuv2yuvX(void)
{
    struct SwsContext *ctx;
    int fsi, osi, isi, i, j;
    int dstW;
#define LARGEST_FILTER 16
#define FILTER_SIZES 4
    static const int filter_sizes[FILTER_SIZES] = {1, 4, 8, 16};
#define LARGEST_INPUT_SIZE 512
#define INPUT_SIZES 6
    static const int input_sizes[INPUT_SIZES] = {8, 24, 128, 144, 256, 512};

    declare_func_emms(AV_CPU_FLAG_MMX, void, const int16_t *filter,
                      int filterSize, const int16_t **src, uint8_t *dest,
                      int dstW, const uint8_t *dither, int offset);

    const int16_t **src;
    LOCAL_ALIGNED_16(int16_t, src_pixels, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(int16_t, filter_coeff, [LARGEST_FILTER]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dither, [LARGEST_INPUT_SIZE]);
    union VFilterData{
        const int16_t *src;
        uint16_t coeff[8];
    } *vFilterData;
    uint8_t d_val = rnd();
    memset(dither, d_val, LARGEST_INPUT_SIZE);
    randomize_buffers((uint8_t*)src_pixels, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int16_t));
    randomize_buffers((uint8_t*)filter_coeff, LARGEST_FILTER * sizeof(int16_t));
    ctx = sws_alloc_context();
    if (sws_init_context(ctx, NULL, NULL) < 0)
        fail();

    ff_sws_init_scale(ctx);
    for(isi = 0; isi < INPUT_SIZES; ++isi){
        dstW = input_sizes[isi];
        for(osi = 0; osi < 64; osi += 16){
            for(fsi = 0; fsi < FILTER_SIZES; ++fsi){
                src = av_malloc(sizeof(int16_t*) * filter_sizes[fsi]);
                vFilterData = av_malloc((filter_sizes[fsi] + 2) * sizeof(union VFilterData));
                memset(vFilterData, 0, (filter_sizes[fsi] + 2) * sizeof(union VFilterData));
                for(i = 0; i < filter_sizes[fsi]; ++i){
                    src[i] = &src_pixels[i * LARGEST_INPUT_SIZE];
                    vFilterData[i].src = src[i];
                    for(j = 0; j < 4; ++j)
                        vFilterData[i].coeff[j + 4] = filter_coeff[i];
                }
                if (check_func(ctx->yuv2planeX, "yuv2yuvX_%d_%d_%d", filter_sizes[fsi], osi, dstW)){
                    memset(dst0, 0, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                    memset(dst1, 0, LARGEST_INPUT_SIZE * sizeof(dst1[0]));

                    // The reference function is not the scalar function selected when mmx
                    // is deactivated as the SIMD functions do not give the same result as
                    // the scalar ones due to rounding. The SIMD functions are activated by
                    // the flag SWS_ACCURATE_RND
                    ref_function(&filter_coeff[0], filter_sizes[fsi], src, dst0, dstW - osi, dither, osi);
                    // There's no point in calling new for the reference function
                    if(ctx->use_mmx_vfilter){
                        call_new((const int16_t*)vFilterData, filter_sizes[fsi], src, dst1, dstW - osi, dither, osi);
                        if (memcmp(dst0, dst1, LARGEST_INPUT_SIZE * sizeof(dst0[0])))
                            fail();
                        if(dstW == LARGEST_INPUT_SIZE)
                            bench_new((const int16_t*)vFilterData, filter_sizes[fsi], src, dst1, dstW - osi, dither, osi);
                    }
                }
                av_freep(&src);
                av_freep(&vFilterData);
            }
        }
    }
    sws_freeContext(ctx);
#undef FILTER_SIZES
}

#undef SRC_PIXELS
#define SRC_PIXELS 512

static void check_hscale(void)
{
#define MAX_FILTER_WIDTH 40
#define FILTER_SIZES 6
    static const int filter_sizes[FILTER_SIZES] = { 4, 8, 12, 16, 32, 40 };

#define HSCALE_PAIRS 2
    static const int hscale_pairs[HSCALE_PAIRS][2] = {
        { 8, 14 },
        { 8, 18 },
    };

#define LARGEST_INPUT_SIZE 512
#define INPUT_SIZES 6
    static const int input_sizes[INPUT_SIZES] = {8, 24, 128, 144, 256, 512};

    int i, j, fsi, hpi, width, dstWi;
    struct SwsContext *ctx;

    // padded
    LOCAL_ALIGNED_32(uint8_t, src, [FFALIGN(SRC_PIXELS + MAX_FILTER_WIDTH - 1, 4)]);
    LOCAL_ALIGNED_32(uint32_t, dst0, [SRC_PIXELS]);
    LOCAL_ALIGNED_32(uint32_t, dst1, [SRC_PIXELS]);

    // padded
    LOCAL_ALIGNED_32(int16_t, filter, [SRC_PIXELS * MAX_FILTER_WIDTH + MAX_FILTER_WIDTH]);
    LOCAL_ALIGNED_32(int32_t, filterPos, [SRC_PIXELS]);
    LOCAL_ALIGNED_32(int16_t, filterAvx2, [SRC_PIXELS * MAX_FILTER_WIDTH + MAX_FILTER_WIDTH]);
    LOCAL_ALIGNED_32(int32_t, filterPosAvx, [SRC_PIXELS]);

    // The dst parameter here is either int16_t or int32_t but we use void* to
    // just cover both cases.
    declare_func_emms(AV_CPU_FLAG_MMX, void, void *c, void *dst, int dstW,
                      const uint8_t *src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);

    int cpu_flags = av_get_cpu_flags();

    ctx = sws_alloc_context();
    if (sws_init_context(ctx, NULL, NULL) < 0)
        fail();

    randomize_buffers(src, SRC_PIXELS + MAX_FILTER_WIDTH - 1);

    for (hpi = 0; hpi < HSCALE_PAIRS; hpi++) {
        for (fsi = 0; fsi < FILTER_SIZES; fsi++) {
            for (dstWi = 0; dstWi < INPUT_SIZES; dstWi++) {
                width = filter_sizes[fsi];

                ctx->srcBpc = hscale_pairs[hpi][0];
                ctx->dstBpc = hscale_pairs[hpi][1];
                ctx->hLumFilterSize = ctx->hChrFilterSize = width;

                for (i = 0; i < SRC_PIXELS; i++) {
                    filterPos[i] = i;
                    filterPosAvx[i] = i;

                    // These filter cofficients are chosen to try break two corner
                    // cases, namely:
                    //
                    // - Negative filter coefficients. The filters output signed
                    //   values, and it should be possible to end up with negative
                    //   output values.
                    //
                    // - Positive clipping. The hscale filter function has clipping
                    //   at (1<<15) - 1
                    //
                    // The coefficients sum to the 1.0 point for the hscale
                    // functions (1 << 14).

                    for (j = 0; j < width; j++) {
                        filter[i * width + j] = -((1 << 14) / (width - 1));
                    }
                    filter[i * width + (rnd() % width)] = ((1 << 15) - 1);
                }

                for (i = 0; i < MAX_FILTER_WIDTH; i++) {
                    // These values should be unused in SIMD implementations but
                    // may still be read, random coefficients here should help show
                    // issues where they are used in error.

                    filter[SRC_PIXELS * width + i] = rnd();
                }
                ctx->dstW = ctx->chrDstW = input_sizes[dstWi];
                ff_sws_init_scale(ctx);
                memcpy(filterAvx2, filter, sizeof(uint16_t) * (SRC_PIXELS * MAX_FILTER_WIDTH + MAX_FILTER_WIDTH));
                if ((cpu_flags & AV_CPU_FLAG_AVX2) && !(cpu_flags & AV_CPU_FLAG_SLOW_GATHER))
                    ff_shuffle_filter_coefficients(ctx, filterPosAvx, width, filterAvx2, SRC_PIXELS);

                if (check_func(ctx->hcScale, "hscale_%d_to_%d__fs_%d_dstW_%d", ctx->srcBpc, ctx->dstBpc + 1, width, ctx->dstW)) {
                    memset(dst0, 0, SRC_PIXELS * sizeof(dst0[0]));
                    memset(dst1, 0, SRC_PIXELS * sizeof(dst1[0]));

                    call_ref(NULL, dst0, ctx->dstW, src, filter, filterPos, width);
                    call_new(NULL, dst1, ctx->dstW, src, filterAvx2, filterPosAvx, width);
                    if (memcmp(dst0, dst1, ctx->dstW * sizeof(dst0[0])))
                        fail();
                    bench_new(NULL, dst0, ctx->dstW, src, filter, filterPosAvx, width);
                }
            }
        }
    }
    sws_freeContext(ctx);
}

void checkasm_check_sw_scale(void)
{
    check_hscale();
    report("hscale");
    check_yuv2yuvX();
    report("yuv2yuvX");
}
