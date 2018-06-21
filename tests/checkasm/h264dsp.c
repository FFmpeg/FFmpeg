/*
 * Copyright (c) 2016 Martin Storsjo
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
#include "checkasm.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/h264dsp.h"
#include "libavcodec/h264data.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define SIZEOF_COEF  (2 * ((bit_depth + 7) / 8))
#define PIXEL_STRIDE 16

#define randomize_buffers()                                                  \
    do {                                                                     \
        int x, y;                                                            \
        uint32_t mask = pixel_mask[bit_depth - 8];                           \
        for (y = 0; y < sz; y++) {                                           \
            for (x = 0; x < PIXEL_STRIDE; x += 4) {                          \
                AV_WN32A(src + y * PIXEL_STRIDE + x, rnd() & mask);          \
                AV_WN32A(dst + y * PIXEL_STRIDE + x, rnd() & mask);          \
            }                                                                \
            for (x = 0; x < sz; x++) {                                       \
                if (bit_depth == 8) {                                        \
                    coef[y * sz + x] = src[y * PIXEL_STRIDE + x] -           \
                                       dst[y * PIXEL_STRIDE + x];            \
                } else {                                                     \
                    ((int32_t *)coef)[y * sz + x] =                          \
                        ((uint16_t *)src)[y * (PIXEL_STRIDE/2) + x] -        \
                        ((uint16_t *)dst)[y * (PIXEL_STRIDE/2) + x];         \
                }                                                            \
            }                                                                \
        }                                                                    \
    } while (0)

#define dct4x4_impl(size, dctcoef)                                           \
static void dct4x4_##size(dctcoef *coef)                                     \
{                                                                            \
    int i, y, x;                                                             \
    dctcoef tmp[16];                                                         \
    for (i = 0; i < 4; i++) {                                                \
        const int z0 = coef[i*4 + 0] + coef[i*4 + 3];                        \
        const int z1 = coef[i*4 + 1] + coef[i*4 + 2];                        \
        const int z2 = coef[i*4 + 0] - coef[i*4 + 3];                        \
        const int z3 = coef[i*4 + 1] - coef[i*4 + 2];                        \
        tmp[i + 4*0] =   z0 +   z1;                                          \
        tmp[i + 4*1] = 2*z2 +   z3;                                          \
        tmp[i + 4*2] =   z0 -   z1;                                          \
        tmp[i + 4*3] =   z2 - 2*z3;                                          \
    }                                                                        \
    for (i = 0; i < 4; i++) {                                                \
        const int z0 = tmp[i*4 + 0] + tmp[i*4 + 3];                          \
        const int z1 = tmp[i*4 + 1] + tmp[i*4 + 2];                          \
        const int z2 = tmp[i*4 + 0] - tmp[i*4 + 3];                          \
        const int z3 = tmp[i*4 + 1] - tmp[i*4 + 2];                          \
        coef[i*4 + 0] =   z0 +   z1;                                         \
        coef[i*4 + 1] = 2*z2 +   z3;                                         \
        coef[i*4 + 2] =   z0 -   z1;                                         \
        coef[i*4 + 3] =   z2 - 2*z3;                                         \
    }                                                                        \
    for (y = 0; y < 4; y++) {                                                \
        for (x = 0; x < 4; x++) {                                            \
            static const int scale[] = { 13107 * 10, 8066 * 13, 5243 * 16 }; \
            const int idx = (y & 1) + (x & 1);                               \
            coef[y*4 + x] = (coef[y*4 + x] * scale[idx] + (1 << 14)) >> 15;  \
        }                                                                    \
    }                                                                        \
}

#define DCT8_1D(src, srcstride, dst, dststride) do {                         \
    const int a0 = (src)[srcstride * 0] + (src)[srcstride * 7];              \
    const int a1 = (src)[srcstride * 0] - (src)[srcstride * 7];              \
    const int a2 = (src)[srcstride * 1] + (src)[srcstride * 6];              \
    const int a3 = (src)[srcstride * 1] - (src)[srcstride * 6];              \
    const int a4 = (src)[srcstride * 2] + (src)[srcstride * 5];              \
    const int a5 = (src)[srcstride * 2] - (src)[srcstride * 5];              \
    const int a6 = (src)[srcstride * 3] + (src)[srcstride * 4];              \
    const int a7 = (src)[srcstride * 3] - (src)[srcstride * 4];              \
    const int b0 = a0 + a6;                                                  \
    const int b1 = a2 + a4;                                                  \
    const int b2 = a0 - a6;                                                  \
    const int b3 = a2 - a4;                                                  \
    const int b4 = a3 + a5 + (a1 + (a1 >> 1));                               \
    const int b5 = a1 - a7 - (a5 + (a5 >> 1));                               \
    const int b6 = a1 + a7 - (a3 + (a3 >> 1));                               \
    const int b7 = a3 - a5 + (a7 + (a7 >> 1));                               \
    (dst)[dststride * 0] =  b0 +  b1;                                        \
    (dst)[dststride * 1] =  b4 + (b7 >> 2);                                  \
    (dst)[dststride * 2] =  b2 + (b3 >> 1);                                  \
    (dst)[dststride * 3] =  b5 + (b6 >> 2);                                  \
    (dst)[dststride * 4] =  b0  - b1;                                        \
    (dst)[dststride * 5] =  b6 - (b5 >> 2);                                  \
    (dst)[dststride * 6] = (b2 >> 1) - b3;                                   \
    (dst)[dststride * 7] = (b4 >> 2) - b7;                                   \
} while (0)

#define dct8x8_impl(size, dctcoef)                                           \
static void dct8x8_##size(dctcoef *coef)                                     \
{                                                                            \
    int i, x, y;                                                             \
    dctcoef tmp[64];                                                         \
    for (i = 0; i < 8; i++)                                                  \
        DCT8_1D(coef + i, 8, tmp + i, 8);                                    \
                                                                             \
    for (i = 0; i < 8; i++)                                                  \
        DCT8_1D(tmp + 8*i, 1, coef + i, 8);                                  \
                                                                             \
    for (y = 0; y < 8; y++) {                                                \
        for (x = 0; x < 8; x++) {                                            \
            static const int scale[] = {                                     \
                13107 * 20, 11428 * 18, 20972 * 32,                          \
                12222 * 19, 16777 * 25, 15481 * 24,                          \
            };                                                               \
            static const int idxmap[] = {                                    \
                0, 3, 4, 3,                                                  \
                3, 1, 5, 1,                                                  \
                4, 5, 2, 5,                                                  \
                3, 1, 5, 1,                                                  \
            };                                                               \
            const int idx = idxmap[(y & 3) * 4 + (x & 3)];                   \
            coef[y*8 + x] = ((int64_t)coef[y*8 + x] *                        \
                             scale[idx] + (1 << 17)) >> 18;                  \
        }                                                                    \
    }                                                                        \
}

dct4x4_impl(16, int16_t)
dct4x4_impl(32, int32_t)

dct8x8_impl(16, int16_t)
dct8x8_impl(32, int32_t)

static void dct4x4(int16_t *coef, int bit_depth)
{
    if (bit_depth == 8)
        dct4x4_16(coef);
    else
        dct4x4_32((int32_t *) coef);
}

static void dct8x8(int16_t *coef, int bit_depth)
{
    if (bit_depth == 8) {
        dct8x8_16(coef);
    } else {
        dct8x8_32((int32_t *) coef);
    }
}


static void check_idct(void)
{
    LOCAL_ALIGNED_16(uint8_t, src,  [8 * 8 * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst,  [8 * 8 * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [8 * 8 * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_base, [8 * 8 * 2 + 32]);
    LOCAL_ALIGNED_16(int16_t, coef, [8 * 8 * 2]);
    LOCAL_ALIGNED_16(int16_t, subcoef0, [8 * 8 * 2]);
    LOCAL_ALIGNED_16(int16_t, subcoef1, [8 * 8 * 2]);
    H264DSPContext h;
    int bit_depth, sz, align, dc;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, int16_t *block, int stride);

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_h264dsp_init(&h, bit_depth, 1);
        for (sz = 4; sz <= 8; sz += 4) {
            randomize_buffers();

            if (sz == 4)
                dct4x4(coef, bit_depth);
            else
                dct8x8(coef, bit_depth);

            for (dc = 0; dc <= 1; dc++) {
                void (*idct)(uint8_t *, int16_t *, int) = NULL;
                switch ((sz << 1) | dc) {
                case (4 << 1) | 0: idct = h.h264_idct_add; break;
                case (4 << 1) | 1: idct = h.h264_idct_dc_add; break;
                case (8 << 1) | 0: idct = h.h264_idct8_add; break;
                case (8 << 1) | 1: idct = h.h264_idct8_dc_add; break;
                }
                if (check_func(idct, "h264_idct%d_add%s_%dbpp", sz, dc ? "_dc" : "", bit_depth)) {
                    for (align = 0; align < 16; align += sz * SIZEOF_PIXEL) {
                        uint8_t *dst1 = dst1_base + align;
                        if (dc) {
                            memset(subcoef0, 0, sz * sz * SIZEOF_COEF);
                            memcpy(subcoef0, coef, SIZEOF_COEF);
                        } else {
                            memcpy(subcoef0, coef, sz * sz * SIZEOF_COEF);
                        }
                        memcpy(dst0, dst, sz * PIXEL_STRIDE);
                        memcpy(dst1, dst, sz * PIXEL_STRIDE);
                        memcpy(subcoef1, subcoef0, sz * sz * SIZEOF_COEF);
                        call_ref(dst0, subcoef0, PIXEL_STRIDE);
                        call_new(dst1, subcoef1, PIXEL_STRIDE);
                        if (memcmp(dst0, dst1, sz * PIXEL_STRIDE) ||
                            memcmp(subcoef0, subcoef1, sz * sz * SIZEOF_COEF))
                            fail();
                        bench_new(dst1, subcoef1, sz * SIZEOF_PIXEL);
                    }
                }
            }
        }
    }
}

static void check_idct_multiple(void)
{
    LOCAL_ALIGNED_16(uint8_t, dst_full,  [16 * 16 * 2]);
    LOCAL_ALIGNED_16(int16_t, coef_full, [16 * 16 * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst0,  [16 * 16 * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1,  [16 * 16 * 2]);
    LOCAL_ALIGNED_16(int16_t, coef0, [16 * 16 * 2]);
    LOCAL_ALIGNED_16(int16_t, coef1, [16 * 16 * 2]);
    LOCAL_ALIGNED_16(uint8_t, nnzc,  [15 * 8]);
    H264DSPContext h;
    int bit_depth, i, y, func;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, const int *block_offset, int16_t *block, int stride, const uint8_t nnzc[15*8]);

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_h264dsp_init(&h, bit_depth, 1);
        for (func = 0; func < 3; func++) {
            void (*idct)(uint8_t *, const int *, int16_t *, int, const uint8_t[]) = NULL;
            const char *name;
            int sz = 4, intra = 0;
            int block_offset[16] = { 0 };
            switch (func) {
            case 0:
                idct = h.h264_idct_add16;
                name = "h264_idct_add16";
                break;
            case 1:
                idct = h.h264_idct_add16intra;
                name = "h264_idct_add16intra";
                intra = 1;
                break;
            case 2:
                idct = h.h264_idct8_add4;
                name = "h264_idct8_add4";
                sz = 8;
                break;
            }
            memset(nnzc, 0, 15 * 8);
            memset(coef_full, 0, 16 * 16 * SIZEOF_COEF);
            for (i = 0; i < 16 * 16; i += sz * sz) {
                uint8_t src[8 * 8 * 2];
                uint8_t dst[8 * 8 * 2];
                int16_t coef[8 * 8 * 2];
                int index = i / sz;
                int block_y = (index / 16) * sz;
                int block_x = index % 16;
                int offset = (block_y * 16 + block_x) * SIZEOF_PIXEL;
                int nnz = rnd() % 3;

                randomize_buffers();
                if (sz == 4)
                    dct4x4(coef, bit_depth);
                else
                    dct8x8(coef, bit_depth);

                for (y = 0; y < sz; y++)
                    memcpy(&dst_full[offset + y * 16 * SIZEOF_PIXEL],
                           &dst[PIXEL_STRIDE * y], sz * SIZEOF_PIXEL);

                if (nnz > 1)
                    nnz = sz * sz;
                memcpy(&coef_full[i * SIZEOF_COEF/sizeof(coef[0])],
                       coef, nnz * SIZEOF_COEF);

                if (intra && nnz == 1)
                    nnz = 0;

                nnzc[scan8[i / 16]] = nnz;
                block_offset[i / 16] = offset;
            }

            if (check_func(idct, "%s_%dbpp", name, bit_depth)) {
                memcpy(coef0, coef_full, 16 * 16 * SIZEOF_COEF);
                memcpy(coef1, coef_full, 16 * 16 * SIZEOF_COEF);
                memcpy(dst0, dst_full, 16 * 16 * SIZEOF_PIXEL);
                memcpy(dst1, dst_full, 16 * 16 * SIZEOF_PIXEL);
                call_ref(dst0, block_offset, coef0, 16 * SIZEOF_PIXEL, nnzc);
                call_new(dst1, block_offset, coef1, 16 * SIZEOF_PIXEL, nnzc);
                if (memcmp(dst0, dst1, 16 * 16 * SIZEOF_PIXEL) ||
                    memcmp(coef0, coef1, 16 * 16 * SIZEOF_COEF))
                    fail();
                bench_new(dst1, block_offset, coef1, 16 * SIZEOF_PIXEL, nnzc);
            }
        }
    }
}

void checkasm_check_h264dsp(void)
{
    check_idct();
    check_idct_multiple();
    report("idct");
}
