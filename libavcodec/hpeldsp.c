/*
 * Half-pel DSP functions.
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Half-pel DSP functions.
 */

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "hpeldsp.h"

#define BIT_DEPTH 8
#include "hpel_template.c"
#include "pel_template.c"

#define PIXOP2(OPNAME, OP)                                              \
static inline void OPNAME ## _no_rnd_pixels8_l2_8(uint8_t *dst,         \
                                                  const uint8_t *src1,  \
                                                  const uint8_t *src2,  \
                                                  int dst_stride,       \
                                                  int src_stride1,      \
                                                  int src_stride2,      \
                                                  int h)                \
{                                                                       \
    int i;                                                              \
                                                                        \
    for (i = 0; i < h; i++) {                                           \
        uint32_t a, b;                                                  \
        a = AV_RN32(&src1[i * src_stride1]);                            \
        b = AV_RN32(&src2[i * src_stride2]);                            \
        OP(*((uint32_t *) &dst[i * dst_stride]),                        \
           no_rnd_avg32(a, b));                                         \
        a = AV_RN32(&src1[i * src_stride1 + 4]);                        \
        b = AV_RN32(&src2[i * src_stride2 + 4]);                        \
        OP(*((uint32_t *) &dst[i * dst_stride + 4]),                    \
           no_rnd_avg32(a, b));                                         \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void OPNAME ## _no_rnd_pixels8_x2_8_c(uint8_t *block,     \
                                                    const uint8_t *pixels, \
                                                    ptrdiff_t line_size, \
                                                    int h)              \
{                                                                       \
    OPNAME ## _no_rnd_pixels8_l2_8(block, pixels, pixels + 1,           \
                                   line_size, line_size, line_size, h); \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels8_x2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels8_l2_8(block, pixels, pixels + 1,                  \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _no_rnd_pixels8_y2_8_c(uint8_t *block,     \
                                                    const uint8_t *pixels, \
                                                    ptrdiff_t line_size, \
                                                    int h)              \
{                                                                       \
    OPNAME ## _no_rnd_pixels8_l2_8(block, pixels, pixels + line_size,   \
                                   line_size, line_size, line_size, h); \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels8_y2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels8_l2_8(block, pixels, pixels + line_size,          \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels4_x2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels4_l2_8(block, pixels, pixels + 1,                  \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels4_y2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels4_l2_8(block, pixels, pixels + line_size,          \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels2_x2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels2_l2_8(block, pixels, pixels + 1,                  \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels2_y2_8_c(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    OPNAME ## _pixels2_l2_8(block, pixels, pixels + line_size,          \
                            line_size, line_size, line_size, h);        \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels2_xy2_8_c(uint8_t *block,           \
                                              const uint8_t *pixels,    \
                                              ptrdiff_t line_size,      \
                                              int h)                    \
{                                                                       \
    int i, a1, b1;                                                      \
    int a0 = pixels[0];                                                 \
    int b0 = pixels[1] + 2;                                             \
                                                                        \
    a0 += b0;                                                           \
    b0 += pixels[2];                                                    \
    pixels += line_size;                                                \
    for (i = 0; i < h; i += 2) {                                        \
        a1  = pixels[0];                                                \
        b1  = pixels[1];                                                \
        a1 += b1;                                                       \
        b1 += pixels[2];                                                \
                                                                        \
        block[0] = (a1 + a0) >> 2; /* FIXME non put */                  \
        block[1] = (b1 + b0) >> 2;                                      \
                                                                        \
        pixels += line_size;                                            \
        block  += line_size;                                            \
                                                                        \
        a0  = pixels[0];                                                \
        b0  = pixels[1] + 2;                                            \
        a0 += b0;                                                       \
        b0 += pixels[2];                                                \
                                                                        \
        block[0] = (a1 + a0) >> 2;                                      \
        block[1] = (b1 + b0) >> 2;                                      \
        pixels  += line_size;                                           \
        block   += line_size;                                           \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels4_xy2_8_c(uint8_t *block,           \
                                              const uint8_t *pixels,    \
                                              ptrdiff_t line_size,      \
                                              int h)                    \
{                                                                       \
    /* FIXME HIGH BIT DEPTH */                                          \
    int i;                                                              \
    const uint32_t a = AV_RN32(pixels);                                 \
    const uint32_t b = AV_RN32(pixels + 1);                             \
    uint32_t l0 = (a & 0x03030303UL) +                                  \
                  (b & 0x03030303UL) +                                  \
                       0x02020202UL;                                    \
    uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +                           \
                  ((b & 0xFCFCFCFCUL) >> 2);                            \
    uint32_t l1, h1;                                                    \
                                                                        \
    pixels += line_size;                                                \
    for (i = 0; i < h; i += 2) {                                        \
        uint32_t a = AV_RN32(pixels);                                   \
        uint32_t b = AV_RN32(pixels + 1);                               \
        l1 = (a & 0x03030303UL) +                                       \
             (b & 0x03030303UL);                                        \
        h1 = ((a & 0xFCFCFCFCUL) >> 2) +                                \
             ((b & 0xFCFCFCFCUL) >> 2);                                 \
        OP(*((uint32_t *) block), h0 + h1 +                             \
           (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                          \
        pixels += line_size;                                            \
        block  += line_size;                                            \
        a  = AV_RN32(pixels);                                           \
        b  = AV_RN32(pixels + 1);                                       \
        l0 = (a & 0x03030303UL) +                                       \
             (b & 0x03030303UL) +                                       \
                  0x02020202UL;                                         \
        h0 = ((a & 0xFCFCFCFCUL) >> 2) +                                \
             ((b & 0xFCFCFCFCUL) >> 2);                                 \
        OP(*((uint32_t *) block), h0 + h1 +                             \
           (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                          \
        pixels += line_size;                                            \
        block  += line_size;                                            \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void OPNAME ## _pixels8_xy2_8_c(uint8_t *block,           \
                                              const uint8_t *pixels,    \
                                              ptrdiff_t line_size,      \
                                              int h)                    \
{                                                                       \
    /* FIXME HIGH BIT DEPTH */                                          \
    int j;                                                              \
                                                                        \
    for (j = 0; j < 2; j++) {                                           \
        int i;                                                          \
        const uint32_t a = AV_RN32(pixels);                             \
        const uint32_t b = AV_RN32(pixels + 1);                         \
        uint32_t l0 = (a & 0x03030303UL) +                              \
                      (b & 0x03030303UL) +                              \
                           0x02020202UL;                                \
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +                       \
                      ((b & 0xFCFCFCFCUL) >> 2);                        \
        uint32_t l1, h1;                                                \
                                                                        \
        pixels += line_size;                                            \
        for (i = 0; i < h; i += 2) {                                    \
            uint32_t a = AV_RN32(pixels);                               \
            uint32_t b = AV_RN32(pixels + 1);                           \
            l1 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL);                                    \
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block), h0 + h1 +                         \
               (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                      \
            pixels += line_size;                                        \
            block  += line_size;                                        \
            a  = AV_RN32(pixels);                                       \
            b  = AV_RN32(pixels + 1);                                   \
            l0 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL) +                                   \
                      0x02020202UL;                                     \
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block), h0 + h1 +                         \
               (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                      \
            pixels += line_size;                                        \
            block  += line_size;                                        \
        }                                                               \
        pixels += 4 - line_size * (h + 1);                              \
        block  += 4 - line_size * h;                                    \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void OPNAME ## _no_rnd_pixels8_xy2_8_c(uint8_t *block,    \
                                                     const uint8_t *pixels, \
                                                     ptrdiff_t line_size, \
                                                     int h)             \
{                                                                       \
    /* FIXME HIGH BIT DEPTH */                                          \
    int j;                                                              \
                                                                        \
    for (j = 0; j < 2; j++) {                                           \
        int i;                                                          \
        const uint32_t a = AV_RN32(pixels);                             \
        const uint32_t b = AV_RN32(pixels + 1);                         \
        uint32_t l0 = (a & 0x03030303UL) +                              \
                      (b & 0x03030303UL) +                              \
                           0x01010101UL;                                \
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +                       \
                      ((b & 0xFCFCFCFCUL) >> 2);                        \
        uint32_t l1, h1;                                                \
                                                                        \
        pixels += line_size;                                            \
        for (i = 0; i < h; i += 2) {                                    \
            uint32_t a = AV_RN32(pixels);                               \
            uint32_t b = AV_RN32(pixels + 1);                           \
            l1 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL);                                    \
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block), h0 + h1 +                         \
               (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                      \
            pixels += line_size;                                        \
            block  += line_size;                                        \
            a  = AV_RN32(pixels);                                       \
            b  = AV_RN32(pixels + 1);                                   \
            l0 = (a & 0x03030303UL) +                                   \
                 (b & 0x03030303UL) +                                   \
                      0x01010101UL;                                     \
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +                            \
                 ((b & 0xFCFCFCFCUL) >> 2);                             \
            OP(*((uint32_t *) block), h0 + h1 +                         \
               (((l0 + l1) >> 2) & 0x0F0F0F0FUL));                      \
            pixels += line_size;                                        \
            block  += line_size;                                        \
        }                                                               \
        pixels += 4 - line_size * (h + 1);                              \
        block  += 4 - line_size * h;                                    \
    }                                                                   \
}                                                                       \
                                                                        \
CALL_2X_PIXELS(OPNAME ## _pixels16_x2_8_c,                              \
               OPNAME ## _pixels8_x2_8_c,                               \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _pixels16_y2_8_c,                              \
               OPNAME ## _pixels8_y2_8_c,                               \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _pixels16_xy2_8_c,                             \
               OPNAME ## _pixels8_xy2_8_c,                              \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_8_c,                          \
               OPNAME ## _pixels8_8_c,                                  \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_x2_8_c,                       \
               OPNAME ## _no_rnd_pixels8_x2_8_c,                        \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_y2_8_c,                       \
               OPNAME ## _no_rnd_pixels8_y2_8_c,                        \
               8)                                                       \
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_xy2_8_c,                      \
               OPNAME ## _no_rnd_pixels8_xy2_8_c,                       \
               8)                                                       \

#define op_avg(a, b) a = rnd_avg32(a, b)
#define op_put(a, b) a = b
#define put_no_rnd_pixels8_8_c put_pixels8_8_c
PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#undef op_avg
#undef op_put

av_cold void ff_hpeldsp_init(HpelDSPContext *c, int flags)
{
#define hpel_funcs(prefix, idx, num) \
    c->prefix ## _pixels_tab idx [0] = prefix ## _pixels ## num ## _8_c; \
    c->prefix ## _pixels_tab idx [1] = prefix ## _pixels ## num ## _x2_8_c; \
    c->prefix ## _pixels_tab idx [2] = prefix ## _pixels ## num ## _y2_8_c; \
    c->prefix ## _pixels_tab idx [3] = prefix ## _pixels ## num ## _xy2_8_c

    hpel_funcs(put, [0], 16);
    hpel_funcs(put, [1],  8);
    hpel_funcs(put, [2],  4);
    hpel_funcs(put, [3],  2);
    hpel_funcs(put_no_rnd, [0], 16);
    hpel_funcs(put_no_rnd, [1],  8);
    hpel_funcs(avg, [0], 16);
    hpel_funcs(avg, [1],  8);
    hpel_funcs(avg, [2],  4);
    hpel_funcs(avg, [3],  2);
    hpel_funcs(avg_no_rnd,, 16);

    if (ARCH_AARCH64)
        ff_hpeldsp_init_aarch64(c, flags);
    if (ARCH_ALPHA)
        ff_hpeldsp_init_alpha(c, flags);
    if (ARCH_ARM)
        ff_hpeldsp_init_arm(c, flags);
    if (ARCH_PPC)
        ff_hpeldsp_init_ppc(c, flags);
    if (ARCH_X86)
        ff_hpeldsp_init_x86(c, flags);
    if (ARCH_MIPS)
        ff_hpeldsp_init_mips(c, flags);
}
