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
#include "libavutil/mem.h"
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

static void yuv2planeX_8_ref(const int16_t *filter, int filterSize,
                             const int16_t **src, uint8_t *dest, int dstW,
                             const uint8_t *dither, int offset)
{
    // This corresponds to the yuv2planeX_8_c function
    int i;
    for (i = 0; i < dstW; i++) {
        int val = dither[(i + offset) & 7] << 12;
        int j;
        for (j = 0; j < filterSize; j++)
            val += src[j][i] * filter[j];

        dest[i]= av_clip_uint8(val >> 19);
    }
}

static int cmp_off_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    for (size_t i = 0; i < n; i++) {
        if (abs(ref[i] - test[i]) > accuracy)
            return 1;
    }
    return 0;
}

static void print_data(uint8_t *p, size_t len, size_t offset)
{
    size_t i = 0;
    for (; i < len; i++) {
        if (i % 8 == 0) {
            printf("0x%04zx: ", i+offset);
        }
        printf("0x%02x ", (uint32_t) p[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    if (i % 8 != 0) {
        printf("\n");
    }
}

static size_t show_differences(uint8_t *a, uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            size_t offset_of_mismatch = i;
            size_t offset;
            if (i >= 8) i-=8;
            offset = i & (~7);
            printf("test a:\n");
            print_data(&a[offset], 32, offset);
            printf("\ntest b:\n");
            print_data(&b[offset], 32, offset);
            printf("\n");
            return offset_of_mismatch;
        }
    }
    return len;
}

static void check_yuv2yuv1(int accurate)
{
    SwsContext *sws;
    SwsInternal *c;
    int osi, isi;
    int dstW, offset;
    size_t fail_offset;
    const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    #define LARGEST_INPUT_SIZE 512

    const int offsets[] = {0, 3, 8, 11, 16, 19};
    const int OFFSET_SIZES = sizeof(offsets)/sizeof(offsets[0]);
    const char *accurate_str = (accurate) ? "accurate" : "approximate";

    declare_func(void,
                 const int16_t *src, uint8_t *dest,
                 int dstW, const uint8_t *dither, int offset);

    LOCAL_ALIGNED_16(int16_t, src_pixels, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, dither, [8]);

    randomize_buffers((uint8_t*)dither, 8);
    randomize_buffers((uint8_t*)src_pixels, LARGEST_INPUT_SIZE * sizeof(int16_t));
    sws = sws_alloc_context();
    if (accurate)
        sws->flags |= SWS_ACCURATE_RND;
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    ff_sws_init_scale(c);
    for (isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); ++isi) {
        dstW = input_sizes[isi];
        for (osi = 0; osi < OFFSET_SIZES; osi++) {
            offset = offsets[osi];
            if (check_func(c->yuv2plane1, "yuv2yuv1_%d_%d_%s", offset, dstW, accurate_str)){
                memset(dst0, 0, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                memset(dst1, 0, LARGEST_INPUT_SIZE * sizeof(dst1[0]));

                call_ref(src_pixels, dst0, dstW, dither, offset);
                call_new(src_pixels, dst1, dstW, dither, offset);
                if (cmp_off_by_n(dst0, dst1, dstW * sizeof(dst0[0]), accurate ? 0 : 2)) {
                    fail();
                    printf("failed: yuv2yuv1_%d_%di_%s\n", offset, dstW, accurate_str);
                    fail_offset = show_differences(dst0, dst1, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                    printf("failing values: src: 0x%04x dither: 0x%02x dst-c: %02x dst-asm: %02x\n",
                            (int) src_pixels[fail_offset],
                            (int) dither[(fail_offset + fail_offset) & 7],
                            (int) dst0[fail_offset],
                            (int) dst1[fail_offset]);
                }
                if(dstW == LARGEST_INPUT_SIZE)
                    bench_new(src_pixels, dst1, dstW, dither, offset);
            }
        }
    }
    sws_freeContext(sws);
}

static void check_yuv2yuvX(int accurate)
{
    SwsContext *sws;
    SwsInternal *c;
    int fsi, osi, isi, i, j;
    int dstW;
#define LARGEST_FILTER 16
    // ff_yuv2planeX_8_sse2 can't handle odd filter sizes
    const int filter_sizes[] = {2, 4, 8, 16};
    const int FILTER_SIZES = sizeof(filter_sizes)/sizeof(filter_sizes[0]);
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    const char *accurate_str = (accurate) ? "accurate" : "approximate";

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
    sws = sws_alloc_context();
    if (accurate)
        sws->flags |= SWS_ACCURATE_RND;
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    ff_sws_init_scale(c);
    for(isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); ++isi){
        dstW = input_sizes[isi];
        for(osi = 0; osi < 64; osi += 16){
            if (dstW <= osi)
                continue;
            for (fsi = 0; fsi < FILTER_SIZES; ++fsi) {
                // Generate filter coefficients for the given filter size,
                // with some properties:
                // - The coefficients add up to the intended sum (4096, 1<<12)
                // - The coefficients contain negative values
                // - The filter intermediates don't overflow for worst case
                //   inputs (all positive coefficients are coupled with
                //   input_max and all negative coefficients with input_min,
                //   or vice versa).
                // Produce a filter with all coefficients set to
                // -((1<<12)/(filter_size-1)) except for one (randomly chosen)
                // which is set to ((1<<13)-1).
                for (i = 0; i < filter_sizes[fsi]; ++i)
                    filter_coeff[i] = -((1 << 12) / (filter_sizes[fsi] - 1));
                filter_coeff[rnd() % filter_sizes[fsi]] = (1 << 13) - 1;

                src = av_malloc(sizeof(int16_t*) * filter_sizes[fsi]);
                vFilterData = av_malloc((filter_sizes[fsi] + 2) * sizeof(union VFilterData));
                memset(vFilterData, 0, (filter_sizes[fsi] + 2) * sizeof(union VFilterData));
                for (i = 0; i < filter_sizes[fsi]; ++i) {
                    src[i] = &src_pixels[i * LARGEST_INPUT_SIZE];
                    vFilterData[i].src = src[i] - osi;
                    for(j = 0; j < 4; ++j)
                        vFilterData[i].coeff[j + 4] = filter_coeff[i];
                }
                if (check_func(c->yuv2planeX, "yuv2yuvX_%d_%d_%d_%s", filter_sizes[fsi], osi, dstW, accurate_str)){
                    // use vFilterData for the mmx function
                    const int16_t *filter = c->use_mmx_vfilter ? (const int16_t*)vFilterData : &filter_coeff[0];
                    memset(dst0, 0, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                    memset(dst1, 0, LARGEST_INPUT_SIZE * sizeof(dst1[0]));

                    // We can't use call_ref here, because we don't know if use_mmx_vfilter was set for that
                    // function or not, so we can't pass it the parameters correctly.
                    yuv2planeX_8_ref(&filter_coeff[0], filter_sizes[fsi], src, dst0, dstW - osi, dither, osi);

                    call_new(filter, filter_sizes[fsi], src, dst1, dstW - osi, dither, osi);
                    if (cmp_off_by_n(dst0, dst1, LARGEST_INPUT_SIZE * sizeof(dst0[0]), accurate ? 0 : 2)) {
                        fail();
                        printf("failed: yuv2yuvX_%d_%d_%d_%s\n", filter_sizes[fsi], osi, dstW, accurate_str);
                        show_differences(dst0, dst1, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                    }
                    if(dstW == LARGEST_INPUT_SIZE)
                        bench_new((const int16_t*)vFilterData, filter_sizes[fsi], src, dst1, dstW - osi, dither, osi);

                }
                av_freep(&src);
                av_freep(&vFilterData);
            }
        }
    }
    sws_freeContext(sws);
#undef FILTER_SIZES
}

static void check_yuv2nv12cX(int accurate)
{
    SwsContext *sws;
    SwsInternal *c;
#define LARGEST_FILTER 16
    const int filter_sizes[] = {2, 4, 8, 16};
#define LARGEST_INPUT_SIZE 512
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};
    const char *accurate_str = (accurate) ? "accurate" : "approximate";

    declare_func_emms(AV_CPU_FLAG_MMX, void, enum AVPixelFormat dstFormat,
                      const uint8_t *chrDither, const int16_t *chrFilter,
                      int chrFilterSize, const int16_t **chrUSrc,
                      const int16_t **chrVSrc, uint8_t *dest, int dstW);

    const int16_t *srcU[LARGEST_FILTER], *srcV[LARGEST_FILTER];
    LOCAL_ALIGNED_16(int16_t, srcU_pixels, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(int16_t, srcV_pixels, [LARGEST_FILTER * LARGEST_INPUT_SIZE]);
    LOCAL_ALIGNED_16(int16_t, filter_coeff, [LARGEST_FILTER]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [LARGEST_INPUT_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dither, [LARGEST_INPUT_SIZE]);
    uint8_t d_val = rnd();
    memset(dither, d_val, LARGEST_INPUT_SIZE);
    randomize_buffers((uint8_t*)srcU_pixels, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int16_t));
    randomize_buffers((uint8_t*)srcV_pixels, LARGEST_FILTER * LARGEST_INPUT_SIZE * sizeof(int16_t));
    for (int i = 0; i < LARGEST_FILTER; i++) {
        srcU[i] = &srcU_pixels[i * LARGEST_INPUT_SIZE];
        srcV[i] = &srcV_pixels[i * LARGEST_INPUT_SIZE];
    }

    sws = sws_alloc_context();
    sws->dst_format = AV_PIX_FMT_NV12;
    if (accurate)
        sws->flags |= SWS_ACCURATE_RND;
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    ff_sws_init_scale(c);
    for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++){
        const int dstW = input_sizes[isi];
        for (int fsi = 0; fsi < FF_ARRAY_ELEMS(filter_sizes); fsi++) {
            const int filter_size = filter_sizes[fsi];
            for (int i = 0; i < filter_size; i++)
                filter_coeff[i] = -((1 << 12) / (filter_size - 1));
            filter_coeff[rnd() % filter_size] = (1 << 13) - 1;

            if (check_func(c->yuv2nv12cX, "yuv2nv12cX_%d_%d_%s", filter_size, dstW, accurate_str)){
                memset(dst0, 0, LARGEST_INPUT_SIZE * sizeof(dst0[0]));
                memset(dst1, 0, LARGEST_INPUT_SIZE * sizeof(dst1[0]));

                call_ref(sws->dst_format, dither, &filter_coeff[0], filter_size, srcU, srcV, dst0, dstW);
                call_new(sws->dst_format, dither, &filter_coeff[0], filter_size, srcU, srcV, dst1, dstW);

                if (cmp_off_by_n(dst0, dst1, dstW * 2 * sizeof(dst0[0]), accurate ? 0 : 2)) {
                    fail();
                    printf("failed: yuv2nv12wX_%d_%d_%s\n", filter_size, dstW, accurate_str);
                    show_differences(dst0, dst1, dstW * 2 * sizeof(dst0[0]));
                }
                if (dstW == LARGEST_INPUT_SIZE)
                    bench_new(sws->dst_format, dither, &filter_coeff[0], filter_size, srcU, srcV, dst1, dstW);

            }
        }
    }
    sws_freeContext(sws);
}
#undef LARGEST_FILTER
#undef LARGEST_INPUT_SIZE

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
    static const int input_sizes[] = {8, 24, 128, 144, 256, 512};

    int i, j, fsi, hpi, width, dstWi;
    SwsContext *sws;
    SwsInternal *c;

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
    declare_func(void, void *c, void *dst, int dstW,
                 const uint8_t *src, const int16_t *filter,
                 const int32_t *filterPos, int filterSize);

    sws = sws_alloc_context();
    if (sws_init_context(sws, NULL, NULL) < 0)
        fail();

    c = sws_internal(sws);
    randomize_buffers(src, SRC_PIXELS + MAX_FILTER_WIDTH - 1);

    for (hpi = 0; hpi < HSCALE_PAIRS; hpi++) {
        for (fsi = 0; fsi < FILTER_SIZES; fsi++) {
            for (dstWi = 0; dstWi < FF_ARRAY_ELEMS(input_sizes); dstWi++) {
                width = filter_sizes[fsi];

                c->srcBpc = hscale_pairs[hpi][0];
                c->dstBpc = hscale_pairs[hpi][1];
                c->hLumFilterSize = c->hChrFilterSize = width;

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
                sws->dst_w = c->chrDstW = input_sizes[dstWi];
                ff_sws_init_scale(c);
                memcpy(filterAvx2, filter, sizeof(uint16_t) * (SRC_PIXELS * MAX_FILTER_WIDTH + MAX_FILTER_WIDTH));
                ff_shuffle_filter_coefficients(c, filterPosAvx, width, filterAvx2, sws->dst_w);

                av_assert0(c->hyScale == c->hcScale);
                if (check_func(c->hcScale, "hscale_%d_to_%d__fs_%d_dstW_%d", c->srcBpc, c->dstBpc + 1, width, sws->dst_w)) {
                    memset(dst0, 0, SRC_PIXELS * sizeof(dst0[0]));
                    memset(dst1, 0, SRC_PIXELS * sizeof(dst1[0]));

                    call_ref(NULL, dst0, sws->dst_w, src, filter, filterPos, width);
                    call_new(NULL, dst1, sws->dst_w, src, filterAvx2, filterPosAvx, width);
                    if (memcmp(dst0, dst1, sws->dst_w * sizeof(dst0[0])))
                        fail();
                    bench_new(NULL, dst0, sws->dst_w, src, filter, filterPosAvx, width);
                }
            }
        }
    }
    sws_freeContext(sws);
}

void checkasm_check_sw_scale(void)
{
    check_hscale();
    report("hscale");
    check_yuv2yuv1(0);
    check_yuv2yuv1(1);
    report("yuv2yuv1");
    check_yuv2yuvX(0);
    check_yuv2yuvX(1);
    report("yuv2yuvX");
    check_yuv2nv12cX(0);
    check_yuv2nv12cX(1);
    report("yuv2nv12cX");
}
