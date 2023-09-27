/*
 * Copyright (C) 2001-2012 Michael Niedermayer <michaelni@gmx.at>
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "libavutil/bswap.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include "config.h"
#include "swscale_internal.h"

#define input_pixel(pos) (is_be ? AV_RB16(pos) : AV_RL16(pos))

#define IS_BE_LE 0
#define IS_BE_BE 1
#define IS_BE_   0
/* ENDIAN_IDENTIFIER needs to be "BE", "LE" or "". The latter is intended
 * for single-byte cases where the concept of endianness does not apply. */
#define IS_BE(ENDIAN_IDENTIFIER) IS_BE_ ## ENDIAN_IDENTIFIER

#define r ((origin == AV_PIX_FMT_BGR48BE || origin == AV_PIX_FMT_BGR48LE || origin == AV_PIX_FMT_BGRA64BE || origin == AV_PIX_FMT_BGRA64LE) ? b_r : r_b)
#define b ((origin == AV_PIX_FMT_BGR48BE || origin == AV_PIX_FMT_BGR48LE || origin == AV_PIX_FMT_BGRA64BE || origin == AV_PIX_FMT_BGRA64LE) ? r_b : b_r)

static av_always_inline void
rgb64ToY_c_template(uint16_t *dst, const uint16_t *src, int width,
                    enum AVPixelFormat origin, int32_t *rgb2yuv, int is_be)
{
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src[i*4+0]);
        unsigned int   g = input_pixel(&src[i*4+1]);
        unsigned int b_r = input_pixel(&src[i*4+2]);

        dst[i] = (ry*r + gy*g + by*b + (0x2001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb64ToUV_c_template(uint16_t *dstU, uint16_t *dstV,
                    const uint16_t *src1, const uint16_t *src2,
                    int width, enum AVPixelFormat origin, int32_t *rgb2yuv, int is_be)
{
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1==src2);
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src1[i*4+0]);
        unsigned int   g = input_pixel(&src1[i*4+1]);
        unsigned int b_r = input_pixel(&src1[i*4+2]);

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb64ToUV_half_c_template(uint16_t *dstU, uint16_t *dstV,
                          const uint16_t *src1, const uint16_t *src2,
                          int width, enum AVPixelFormat origin, int32_t *rgb2yuv, int is_be)
{
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1==src2);
    for (i = 0; i < width; i++) {
        unsigned r_b = (input_pixel(&src1[8 * i + 0]) + input_pixel(&src1[8 * i + 4]) + 1) >> 1;
        unsigned   g = (input_pixel(&src1[8 * i + 1]) + input_pixel(&src1[8 * i + 5]) + 1) >> 1;
        unsigned b_r = (input_pixel(&src1[8 * i + 2]) + input_pixel(&src1[8 * i + 6]) + 1) >> 1;

        dstU[i]= (ru*r + gu*g + bu*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i]= (rv*r + gv*g + bv*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

#define RGB64FUNCS_EXT(pattern, BE_LE, origin, is_be) \
static void pattern ## 64 ## BE_LE ## ToY_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused0, const uint8_t *unused1,\
                                    int width, uint32_t *rgb2yuv, void *opq) \
{ \
    const uint16_t *src = (const uint16_t *) _src; \
    uint16_t *dst = (uint16_t *) _dst; \
    rgb64ToY_c_template(dst, src, width, origin, rgb2yuv, is_be); \
} \
 \
static void pattern ## 64 ## BE_LE ## ToUV_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *rgb2yuv, void *opq) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb64ToUV_c_template(dstU, dstV, src1, src2, width, origin, rgb2yuv, is_be); \
} \
 \
static void pattern ## 64 ## BE_LE ## ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *rgb2yuv, void *opq) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb64ToUV_half_c_template(dstU, dstV, src1, src2, width, origin, rgb2yuv, is_be); \
}
#define RGB64FUNCS(pattern, endianness, base_fmt) \
        RGB64FUNCS_EXT(pattern, endianness, base_fmt ## endianness, IS_BE(endianness))

RGB64FUNCS(rgb, LE, AV_PIX_FMT_RGBA64)
RGB64FUNCS(rgb, BE, AV_PIX_FMT_RGBA64)
RGB64FUNCS(bgr, LE, AV_PIX_FMT_BGRA64)
RGB64FUNCS(bgr, BE, AV_PIX_FMT_BGRA64)

static av_always_inline void rgb48ToY_c_template(uint16_t *dst,
                                                 const uint16_t *src, int width,
                                                 enum AVPixelFormat origin,
                                                 int32_t *rgb2yuv, int is_be)
{
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src[i * 3 + 0]);
        unsigned int g   = input_pixel(&src[i * 3 + 1]);
        unsigned int b_r = input_pixel(&src[i * 3 + 2]);

        dst[i] = (ry*r + gy*g + by*b + (0x2001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgb48ToUV_c_template(uint16_t *dstU,
                                                  uint16_t *dstV,
                                                  const uint16_t *src1,
                                                  const uint16_t *src2,
                                                  int width,
                                                  enum AVPixelFormat origin,
                                                  int32_t *rgb2yuv, int is_be)
{
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        unsigned r_b = input_pixel(&src1[i * 3 + 0]);
        unsigned g   = input_pixel(&src1[i * 3 + 1]);
        unsigned b_r = input_pixel(&src1[i * 3 + 2]);

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgb48ToUV_half_c_template(uint16_t *dstU,
                                                       uint16_t *dstV,
                                                       const uint16_t *src1,
                                                       const uint16_t *src2,
                                                       int width,
                                                       enum AVPixelFormat origin,
                                                       int32_t *rgb2yuv, int is_be)
{
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        unsigned r_b = (input_pixel(&src1[6 * i + 0]) +
                        input_pixel(&src1[6 * i + 3]) + 1) >> 1;
        unsigned g   = (input_pixel(&src1[6 * i + 1]) +
                        input_pixel(&src1[6 * i + 4]) + 1) >> 1;
        unsigned b_r = (input_pixel(&src1[6 * i + 2]) +
                        input_pixel(&src1[6 * i + 5]) + 1) >> 1;

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

#undef r
#undef b
#undef input_pixel

#define RGB48FUNCS_EXT(pattern, BE_LE, origin, is_be)                   \
static void pattern ## 48 ## BE_LE ## ToY_c(uint8_t *_dst,              \
                                            const uint8_t *_src,        \
                                            const uint8_t *unused0, const uint8_t *unused1,\
                                            int width,                  \
                                            uint32_t *rgb2yuv,          \
                                            void *opq)                  \
{                                                                       \
    const uint16_t *src = (const uint16_t *)_src;                       \
    uint16_t *dst       = (uint16_t *)_dst;                             \
    rgb48ToY_c_template(dst, src, width, origin, rgb2yuv, is_be);       \
}                                                                       \
                                                                        \
static void pattern ## 48 ## BE_LE ## ToUV_c(uint8_t *_dstU,            \
                                             uint8_t *_dstV,            \
                                             const uint8_t *unused0,    \
                                             const uint8_t *_src1,      \
                                             const uint8_t *_src2,      \
                                             int width,                 \
                                             uint32_t *rgb2yuv,         \
                                             void *opq)                 \
{                                                                       \
    const uint16_t *src1 = (const uint16_t *)_src1,                     \
                   *src2 = (const uint16_t *)_src2;                     \
    uint16_t *dstU = (uint16_t *)_dstU,                                 \
             *dstV = (uint16_t *)_dstV;                                 \
    rgb48ToUV_c_template(dstU, dstV, src1, src2, width, origin, rgb2yuv, is_be); \
}                                                                       \
                                                                        \
static void pattern ## 48 ## BE_LE ## ToUV_half_c(uint8_t *_dstU,       \
                                                  uint8_t *_dstV,       \
                                                  const uint8_t *unused0,    \
                                                  const uint8_t *_src1, \
                                                  const uint8_t *_src2, \
                                                  int width,            \
                                                  uint32_t *rgb2yuv,    \
                                                  void *opq)            \
{                                                                       \
    const uint16_t *src1 = (const uint16_t *)_src1,                     \
                   *src2 = (const uint16_t *)_src2;                     \
    uint16_t *dstU = (uint16_t *)_dstU,                                 \
             *dstV = (uint16_t *)_dstV;                                 \
    rgb48ToUV_half_c_template(dstU, dstV, src1, src2, width, origin, rgb2yuv, is_be); \
}
#define RGB48FUNCS(pattern, endianness, base_fmt) \
        RGB48FUNCS_EXT(pattern, endianness, base_fmt ## endianness, IS_BE(endianness))

RGB48FUNCS(rgb, LE, AV_PIX_FMT_RGB48)
RGB48FUNCS(rgb, BE, AV_PIX_FMT_RGB48)
RGB48FUNCS(bgr, LE, AV_PIX_FMT_BGR48)
RGB48FUNCS(bgr, BE, AV_PIX_FMT_BGR48)

#define input_pixel(i) ((origin == AV_PIX_FMT_RGBA ||                      \
                         origin == AV_PIX_FMT_BGRA ||                      \
                         origin == AV_PIX_FMT_ARGB ||                      \
                         origin == AV_PIX_FMT_ABGR)                        \
                        ? AV_RN32A(&src[(i) * 4])                          \
                        : ((origin == AV_PIX_FMT_X2RGB10LE ||              \
                            origin == AV_PIX_FMT_X2BGR10LE)                \
                           ? AV_RL32(&src[(i) * 4])                        \
                           : (is_be ? AV_RB16(&src[(i) * 2])               \
                              : AV_RL16(&src[(i) * 2]))))

static av_always_inline void rgb16_32ToY_c_template(int16_t *dst,
                                                    const uint8_t *src,
                                                    int width,
                                                    enum AVPixelFormat origin,
                                                    int shr, int shg,
                                                    int shb, int shp,
                                                    int maskr, int maskg,
                                                    int maskb, int rsh,
                                                    int gsh, int bsh, int S,
                                                    int32_t *rgb2yuv, int is_be)
{
    const int ry       = rgb2yuv[RY_IDX]<<rsh, gy = rgb2yuv[GY_IDX]<<gsh, by = rgb2yuv[BY_IDX]<<bsh;
    const unsigned rnd = (32<<((S)-1)) + (1<<(S-7));
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b  = (px & maskb) >> shb;
        int g  = (px & maskg) >> shg;
        int r  = (px & maskr) >> shr;

        dst[i] = (ry * r + gy * g + by * b + rnd) >> ((S)-6);
    }
}

static av_always_inline void rgb16_32ToUV_c_template(int16_t *dstU,
                                                     int16_t *dstV,
                                                     const uint8_t *src,
                                                     int width,
                                                     enum AVPixelFormat origin,
                                                     int shr, int shg,
                                                     int shb, int shp,
                                                     int maskr, int maskg,
                                                     int maskb, int rsh,
                                                     int gsh, int bsh, int S,
                                                     int32_t *rgb2yuv, int is_be)
{
    const int ru       = rgb2yuv[RU_IDX] * (1 << rsh), gu = rgb2yuv[GU_IDX] * (1 << gsh), bu = rgb2yuv[BU_IDX] * (1 << bsh),
              rv       = rgb2yuv[RV_IDX] * (1 << rsh), gv = rgb2yuv[GV_IDX] * (1 << gsh), bv = rgb2yuv[BV_IDX] * (1 << bsh);
    const unsigned rnd = (256u<<((S)-1)) + (1<<(S-7));
    int i;

    for (i = 0; i < width; i++) {
        int px = input_pixel(i) >> shp;
        int b  = (px & maskb)   >> shb;
        int g  = (px & maskg)   >> shg;
        int r  = (px & maskr)   >> shr;

        dstU[i] = (ru * r + gu * g + bu * b + rnd) >> ((S)-6);
        dstV[i] = (rv * r + gv * g + bv * b + rnd) >> ((S)-6);
    }
}

static av_always_inline void rgb16_32ToUV_half_c_template(int16_t *dstU,
                                                          int16_t *dstV,
                                                          const uint8_t *src,
                                                          int width,
                                                          enum AVPixelFormat origin,
                                                          int shr, int shg,
                                                          int shb, int shp,
                                                          int maskr, int maskg,
                                                          int maskb, int rsh,
                                                          int gsh, int bsh, int S,
                                                          int32_t *rgb2yuv, int is_be)
{
    const int ru       = rgb2yuv[RU_IDX] * (1 << rsh), gu = rgb2yuv[GU_IDX] * (1 << gsh), bu = rgb2yuv[BU_IDX] * (1 << bsh),
              rv       = rgb2yuv[RV_IDX] * (1 << rsh), gv = rgb2yuv[GV_IDX] * (1 << gsh), bv = rgb2yuv[BV_IDX] * (1 << bsh),
              maskgx   = ~(maskr | maskb);
    const unsigned rnd = (256U<<(S)) + (1<<(S-6));
    int i;

    maskr |= maskr << 1;
    maskb |= maskb << 1;
    maskg |= maskg << 1;
    for (i = 0; i < width; i++) {
        unsigned px0 = input_pixel(2 * i + 0) >> shp;
        unsigned px1 = input_pixel(2 * i + 1) >> shp;
        int b, r, g = (px0 & maskgx) + (px1 & maskgx);
        int rb = px0 + px1 - g;

        b = (rb & maskb) >> shb;
        if (shp ||
            origin == AV_PIX_FMT_BGR565LE || origin == AV_PIX_FMT_BGR565BE ||
            origin == AV_PIX_FMT_RGB565LE || origin == AV_PIX_FMT_RGB565BE) {
            g >>= shg;
        } else {
            g = (g & maskg) >> shg;
        }
        r = (rb & maskr) >> shr;

        dstU[i] = (ru * r + gu * g + bu * b + (unsigned)rnd) >> ((S)-6+1);
        dstV[i] = (rv * r + gv * g + bv * b + (unsigned)rnd) >> ((S)-6+1);
    }
}

#undef input_pixel

#define RGB16_32FUNCS_EXT(fmt, name, shr, shg, shb, shp, maskr,         \
                          maskg, maskb, rsh, gsh, bsh, S, is_be)        \
static void name ## ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,            \
                          int width, uint32_t *tab, void *opq)          \
{                                                                       \
    rgb16_32ToY_c_template((int16_t*)dst, src, width, fmt, shr, shg, shb, shp,    \
                           maskr, maskg, maskb, rsh, gsh, bsh, S, tab, is_be); \
}                                                                       \
                                                                        \
static void name ## ToUV_c(uint8_t *dstU, uint8_t *dstV,                \
                           const uint8_t *unused0, const uint8_t *src, const uint8_t *dummy,    \
                           int width, uint32_t *tab, void *opq)         \
{                                                                       \
    rgb16_32ToUV_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt,                \
                            shr, shg, shb, shp,                         \
                            maskr, maskg, maskb, rsh, gsh, bsh, S, tab, is_be); \
}                                                                       \
                                                                        \
static void name ## ToUV_half_c(uint8_t *dstU, uint8_t *dstV,           \
                                const uint8_t *unused0, const uint8_t *src,                     \
                                const uint8_t *dummy,                   \
                                int width, uint32_t *tab, void *opq)    \
{                                                                       \
    rgb16_32ToUV_half_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt,           \
                                 shr, shg, shb, shp,                    \
                                 maskr, maskg, maskb,                   \
                                 rsh, gsh, bsh, S, tab, is_be);         \
}

#define RGB16_32FUNCS(base_fmt, endianness, name, shr, shg, shb, shp, maskr, \
                      maskg, maskb, rsh, gsh, bsh, S) \
    RGB16_32FUNCS_EXT(base_fmt ## endianness, name, shr, shg, shb, shp, maskr, \
                      maskg, maskb, rsh, gsh, bsh, S, IS_BE(endianness))

RGB16_32FUNCS(AV_PIX_FMT_BGR32,     , bgr32,  16, 0,  0, 0, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_BGR32_1,   , bgr321, 16, 0,  0, 8, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_RGB32,     , rgb32,   0, 0, 16, 0,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_RGB32_1,   , rgb321,  0, 0, 16, 8,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_BGR565,  LE, bgr16le, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_BGR555,  LE, bgr15le, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT + 7)
RGB16_32FUNCS(AV_PIX_FMT_BGR444,  LE, bgr12le, 0, 0,  0, 0,   0x000F, 0x00F0,   0x0F00,  8, 4,  0, RGB2YUV_SHIFT + 4)
RGB16_32FUNCS(AV_PIX_FMT_RGB565,  LE, rgb16le, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_RGB555,  LE, rgb15le, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT + 7)
RGB16_32FUNCS(AV_PIX_FMT_RGB444,  LE, rgb12le, 0, 0,  0, 0,   0x0F00, 0x00F0,   0x000F,  0, 4,  8, RGB2YUV_SHIFT + 4)
RGB16_32FUNCS(AV_PIX_FMT_BGR565,  BE, bgr16be, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_BGR555,  BE, bgr15be, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT + 7)
RGB16_32FUNCS(AV_PIX_FMT_BGR444,  BE, bgr12be, 0, 0,  0, 0,   0x000F, 0x00F0,   0x0F00,  8, 4,  0, RGB2YUV_SHIFT + 4)
RGB16_32FUNCS(AV_PIX_FMT_RGB565,  BE, rgb16be, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT + 8)
RGB16_32FUNCS(AV_PIX_FMT_RGB555,  BE, rgb15be, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT + 7)
RGB16_32FUNCS(AV_PIX_FMT_RGB444,  BE, rgb12be, 0, 0,  0, 0,   0x0F00, 0x00F0,   0x000F,  0, 4,  8, RGB2YUV_SHIFT + 4)
RGB16_32FUNCS(AV_PIX_FMT_X2RGB10, LE, rgb30le, 16, 6, 0, 0, 0x3FF00000, 0xFFC00, 0x3FF, 0, 0, 4, RGB2YUV_SHIFT + 6)
RGB16_32FUNCS(AV_PIX_FMT_X2BGR10, LE, bgr30le, 0, 6, 16, 0, 0x3FF, 0xFFC00, 0x3FF00000, 4, 0, 0, RGB2YUV_SHIFT + 6)

static void gbr24pToUV_half_c(uint8_t *_dstU, uint8_t *_dstV,
                         const uint8_t *gsrc, const uint8_t *bsrc, const uint8_t *rsrc,
                         int width, uint32_t *rgb2yuv, void *opq)
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];

    int i;
    for (i = 0; i < width; i++) {
        unsigned int g   = gsrc[2*i] + gsrc[2*i+1];
        unsigned int b   = bsrc[2*i] + bsrc[2*i+1];
        unsigned int r   = rsrc[2*i] + rsrc[2*i+1];

        dstU[i] = (ru*r + gu*g + bu*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
        dstV[i] = (rv*r + gv*g + bv*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
    }
}

static void rgba64leToA_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1,
                          const uint8_t *unused2, int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    const uint16_t *src = (const uint16_t *)_src;
    int i;
    for (i = 0; i < width; i++)
        dst[i] = AV_RL16(src + 4 * i + 3);
}

static void rgba64beToA_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1,
                          const uint8_t *unused2, int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    const uint16_t *src = (const uint16_t *)_src;
    int i;
    for (i = 0; i < width; i++)
        dst[i] = AV_RB16(src + 4 * i + 3);
}

static void abgrToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                      const uint8_t *unused2, int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i]<<6 | src[4*i]>>2;
    }
}

static void rgbaToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                      const uint8_t *unused2, int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i+3]<<6 | src[4*i+3]>>2;
    }
}

static void palToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                     const uint8_t *unused2, int width, uint32_t *pal, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= (pal[d] >> 24)<<6 | pal[d]>>26;
    }
}

static void palToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                     const uint8_t *unused2, int width, uint32_t *pal, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i = 0; i < width; i++) {
        int d = src[i];

        dst[i] = (pal[d] & 0xFF)<<6;
    }
}

static void palToUV_c(uint8_t *_dstU, uint8_t *_dstV,
                      const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                      int width, uint32_t *pal, void *opq)
{
    uint16_t *dstU = (uint16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int p = pal[src1[i]];

        dstU[i] = (uint8_t)(p>> 8)<<6;
        dstV[i] = (uint8_t)(p>>16)<<6;
    }
}

static void monowhite2Y_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                          const uint8_t *unused2,  int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i, j;
    width = (width + 7) >> 3;
    for (i = 0; i < width; i++) {
        int d = ~src[i];
        for (j = 0; j < 8; j++)
            dst[8*i+j]= ((d>>(7-j))&1) * 16383;
    }
    if(width&7){
        int d= ~src[i];
        for (j = 0; j < (width&7); j++)
            dst[8*i+j]= ((d>>(7-j))&1) * 16383;
    }
}

static void monoblack2Y_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
                          const uint8_t *unused2,  int width, uint32_t *unused, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int i, j;
    width = (width + 7) >> 3;
    for (i = 0; i < width; i++) {
        int d = src[i];
        for (j = 0; j < 8; j++)
            dst[8*i+j]= ((d>>(7-j))&1) * 16383;
    }
    if(width&7){
        int d = src[i];
        for (j = 0; j < (width&7); j++)
            dst[8*i+j] = ((d>>(7-j))&1) * 16383;
    }
}

static void yuy2ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                      uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[2 * i];
}

static void yuy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = src1[4 * i + 1];
        dstV[i] = src1[4 * i + 3];
    }
    av_assert1(src1 == src2);
}

static void yvy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        dstV[i] = src1[4 * i + 1];
        dstU[i] = src1[4 * i + 3];
    }
    av_assert1(src1 == src2);
}

#define y21xle_wrapper(bits, shift) \
    static void y2 ## bits ## le_UV_c(uint8_t *dstU, uint8_t *dstV,      \
                                      const uint8_t *unused0,            \
                                      const uint8_t *src,                \
                                      const uint8_t *unused1, int width, \
                                      uint32_t *unused2, void *opq)      \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++) {                                    \
            AV_WN16(dstU + i * 2, AV_RL16(src + i * 8 + 2) >> shift);    \
            AV_WN16(dstV + i * 2, AV_RL16(src + i * 8 + 6) >> shift);    \
        }                                                                \
    }                                                                    \
                                                                         \
    static void y2 ## bits ## le_Y_c(uint8_t *dst, const uint8_t *src,   \
                                     const uint8_t *unused0,             \
                                     const uint8_t *unused1, int width,  \
                                     uint32_t *unused2, void *opq)       \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++)                                      \
            AV_WN16(dst + i * 2, AV_RL16(src + i * 4) >> shift);         \
    }

y21xle_wrapper(10, 6)
y21xle_wrapper(12, 4)

static void bswap16Y_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1, const uint8_t *unused2, int width,
                       uint32_t *unused, void *opq)
{
    int i;
    const uint16_t *src = (const uint16_t *)_src;
    uint16_t *dst       = (uint16_t *)_dst;
    for (i = 0; i < width; i++)
        dst[i] = av_bswap16(src[i]);
}

static void bswap16UV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *_src1,
                        const uint8_t *_src2, int width, uint32_t *unused, void *opq)
{
    int i;
    const uint16_t *src1 = (const uint16_t *)_src1,
    *src2                = (const uint16_t *)_src2;
    uint16_t *dstU       = (uint16_t *)_dstU, *dstV = (uint16_t *)_dstV;
    for (i = 0; i < width; i++) {
        dstU[i] = av_bswap16(src1[i]);
        dstV[i] = av_bswap16(src2[i]);
    }
}

static void read_ya16le_gray_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                               uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RL16(src + i * 4));
}

static void read_ya16le_alpha_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                                uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RL16(src + i * 4 + 2));
}

static void read_ya16be_gray_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                               uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RB16(src + i * 4));
}

static void read_ya16be_alpha_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                                uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RB16(src + i * 4 + 2));
}

static void read_ayuv64le_Y_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                               uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RL16(src + i * 8 + 2));
}


static void read_ayuv64le_UV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src,
                               const uint8_t *unused1, int width, uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        AV_WN16(dstU + i * 2, AV_RL16(src + i * 8 + 4));
        AV_WN16(dstV + i * 2, AV_RL16(src + i * 8 + 6));
    }
}

static void read_ayuv64le_A_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                              uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RL16(src + i * 8));
}

static void read_vuyx_UV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src,
                           const uint8_t *unused1, int width, uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = src[i * 4 + 1];
        dstV[i] = src[i * 4];
    }
}

static void read_vuyx_Y_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                          uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[i * 4 + 2];
}

static void read_vuya_A_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                          uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[i * 4 + 3];
}

static void read_xv30le_Y_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                               uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, (AV_RL32(src + i * 4) >> 10) & 0x3FFu);
}


static void read_xv30le_UV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src,
                               const uint8_t *unused1, int width, uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        AV_WN16(dstU + i * 2, AV_RL32(src + i * 4) & 0x3FFu);
        AV_WN16(dstV + i * 2, (AV_RL32(src + i * 4) >> 20) & 0x3FFu);
    }
}

static void read_xv36le_Y_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused0, const uint8_t *unused1, int width,
                               uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        AV_WN16(dst + i * 2, AV_RL16(src + i * 8 + 2) >> 4);
}


static void read_xv36le_UV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src,
                               const uint8_t *unused1, int width, uint32_t *unused2, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        AV_WN16(dstU + i * 2, AV_RL16(src + i * 8 + 0) >> 4);
        AV_WN16(dstV + i * 2, AV_RL16(src + i * 8 + 4) >> 4);
    }
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src + 1, ...) would have 100% unaligned accesses. */
static void uyvyToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                      uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[2 * i + 1];
}

static void uyvyToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused, void *opq)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = src1[4 * i + 0];
        dstV[i] = src1[4 * i + 2];
    }
    av_assert1(src1 == src2);
}

static av_always_inline void nvXXtoUV_c(uint8_t *dst1, uint8_t *dst2,
                                        const uint8_t *src, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dst1[i] = src[2 * i + 0];
        dst2[i] = src[2 * i + 1];
    }
}

static void nv12ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused, void *opq)
{
    nvXXtoUV_c(dstU, dstV, src1, width);
}

static void nv21ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused, void *opq)
{
    nvXXtoUV_c(dstV, dstU, src1, width);
}

#define p01x_uv_wrapper(bits, shift) \
    static void p0 ## bits ## LEToUV_c(uint8_t *dstU, uint8_t *dstV,     \
                                       const uint8_t *unused0,           \
                                       const uint8_t *src1,              \
                                       const uint8_t *src2, int width,   \
                                       uint32_t *unused, void *opq)      \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++) {                                    \
            AV_WN16(dstU + i * 2, AV_RL16(src1 + i * 4 + 0) >> shift);   \
            AV_WN16(dstV + i * 2, AV_RL16(src1 + i * 4 + 2) >> shift);   \
        }                                                                \
    }                                                                    \
                                                                         \
    static void p0 ## bits ## BEToUV_c(uint8_t *dstU, uint8_t *dstV,     \
                                       const uint8_t *unused0,           \
                                       const uint8_t *src1,              \
                                       const uint8_t *src2, int width,   \
                                       uint32_t *unused, void *opq)      \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++) {                                    \
            AV_WN16(dstU + i * 2, AV_RB16(src1 + i * 4 + 0) >> shift);   \
            AV_WN16(dstV + i * 2, AV_RB16(src1 + i * 4 + 2) >> shift);   \
        }                                                                \
    }

#define p01x_wrapper(bits, shift) \
    static void p0 ## bits ## LEToY_c(uint8_t *dst, const uint8_t *src,  \
                                      const uint8_t *unused1,            \
                                      const uint8_t *unused2, int width, \
                                      uint32_t *unused, void *opq)       \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++) {                                    \
            AV_WN16(dst + i * 2, AV_RL16(src + i * 2) >> shift);         \
        }                                                                \
    }                                                                    \
                                                                         \
    static void p0 ## bits ## BEToY_c(uint8_t *dst, const uint8_t *src,  \
                                      const uint8_t *unused1,            \
                                      const uint8_t *unused2, int width, \
                                      uint32_t *unused, void *opq)       \
    {                                                                    \
        int i;                                                           \
        for (i = 0; i < width; i++) {                                    \
            AV_WN16(dst + i * 2, AV_RB16(src + i * 2) >> shift);         \
        }                                                                \
    }                                                                    \
    p01x_uv_wrapper(bits, shift)

p01x_wrapper(10, 6)
p01x_wrapper(12, 4)
p01x_uv_wrapper(16, 0)

static void bgr24ToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,
                       int width, uint32_t *rgb2yuv, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int b = src[i * 3 + 0];
        int g = src[i * 3 + 1];
        int r = src[i * 3 + 2];

        dst[i] = ((ry*r + gy*g + by*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void bgr24ToUV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *rgb2yuv, void *opq)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int b = src1[3 * i + 0];
        int g = src1[3 * i + 1];
        int r = src1[3 * i + 2];

        dstU[i] = (ru*r + gu*g + bu*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i] = (rv*r + gv*g + bv*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
    av_assert1(src1 == src2);
}

static void bgr24ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *rgb2yuv, void *opq)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    for (i = 0; i < width; i++) {
        int b = src1[6 * i + 0] + src1[6 * i + 3];
        int g = src1[6 * i + 1] + src1[6 * i + 4];
        int r = src1[6 * i + 2] + src1[6 * i + 5];

        dstU[i] = (ru*r + gu*g + bu*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i] = (rv*r + gv*g + bv*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
    av_assert1(src1 == src2);
}

static void rgb24ToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                       uint32_t *rgb2yuv, void *opq)
{
    int16_t *dst = (int16_t *)_dst;
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int r = src[i * 3 + 0];
        int g = src[i * 3 + 1];
        int b = src[i * 3 + 2];

        dst[i] = ((ry*r + gy*g + by*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void rgb24ToUV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *rgb2yuv, void *opq)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r = src1[3 * i + 0];
        int g = src1[3 * i + 1];
        int b = src1[3 * i + 2];

        dstU[i] = (ru*r + gu*g + bu*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i] = (rv*r + gv*g + bv*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
}

static void rgb24ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *rgb2yuv, void *opq)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r = src1[6 * i + 0] + src1[6 * i + 3];
        int g = src1[6 * i + 1] + src1[6 * i + 4];
        int b = src1[6 * i + 2] + src1[6 * i + 5];

        dstU[i] = (ru*r + gu*g + bu*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i] = (rv*r + gv*g + bv*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
}

static void planar_rgb_to_y(uint8_t *_dst, const uint8_t *src[4], int width, int32_t *rgb2yuv, void *opq)
{
    uint16_t *dst = (uint16_t *)_dst;
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dst[i] = (ry*r + gy*g + by*b + (0x801<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void planar_rgb_to_a(uint8_t *_dst, const uint8_t *src[4], int width, int32_t *unused, void *opq)
{
    uint16_t *dst = (uint16_t *)_dst;
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[3][i] << 6;
}

static void planar_rgb_to_uv(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *src[4], int width, int32_t *rgb2yuv, void *opq)
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dstU[i] = (ru*r + gu*g + bu*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
        dstV[i] = (rv*r + gv*g + bv*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

#define rdpx(src) \
    (is_be ? AV_RB16(src) : AV_RL16(src))
static av_always_inline void planar_rgb16_to_y(uint8_t *_dst, const uint8_t *_src[4],
                                               int width, int bpc, int is_be, int32_t *rgb2yuv)
{
    int i;
    const uint16_t **src = (const uint16_t **)_src;
    uint16_t *dst        = (uint16_t *)_dst;
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int shift = bpc < 16 ? bpc : 14;
    for (i = 0; i < width; i++) {
        int g = rdpx(src[0] + i);
        int b = rdpx(src[1] + i);
        int r = rdpx(src[2] + i);

        dst[i] = (ry*r + gy*g + by*b + (16 << (RGB2YUV_SHIFT + bpc - 8)) + (1 << (RGB2YUV_SHIFT + shift - 15))) >> (RGB2YUV_SHIFT + shift - 14);
    }
}

static av_always_inline void planar_rgb16_to_a(uint8_t *_dst, const uint8_t *_src[4],
                                               int width, int bpc, int is_be, int32_t *rgb2yuv)
{
    int i;
    const uint16_t **src = (const uint16_t **)_src;
    uint16_t *dst        = (uint16_t *)_dst;
    int shift = bpc < 16 ? bpc : 14;

    for (i = 0; i < width; i++) {
        dst[i] = rdpx(src[3] + i) << (14 - shift);
    }
}

static av_always_inline void planar_rgb16_to_uv(uint8_t *_dstU, uint8_t *_dstV,
                                                const uint8_t *_src[4], int width,
                                                int bpc, int is_be, int32_t *rgb2yuv)
{
    int i;
    const uint16_t **src = (const uint16_t **)_src;
    uint16_t *dstU       = (uint16_t *)_dstU;
    uint16_t *dstV       = (uint16_t *)_dstV;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    int shift = bpc < 16 ? bpc : 14;
    for (i = 0; i < width; i++) {
        int g = rdpx(src[0] + i);
        int b = rdpx(src[1] + i);
        int r = rdpx(src[2] + i);

        dstU[i] = (ru*r + gu*g + bu*b + (128 << (RGB2YUV_SHIFT + bpc - 8)) + (1 << (RGB2YUV_SHIFT + shift - 15))) >> (RGB2YUV_SHIFT + shift - 14);
        dstV[i] = (rv*r + gv*g + bv*b + (128 << (RGB2YUV_SHIFT + bpc - 8)) + (1 << (RGB2YUV_SHIFT + shift - 15))) >> (RGB2YUV_SHIFT + shift - 14);
    }
}
#undef rdpx

#define rdpx(src) (is_be ? av_int2float(AV_RB32(src)): av_int2float(AV_RL32(src)))

static av_always_inline void planar_rgbf32_to_a(uint8_t *_dst, const uint8_t *_src[4], int width, int is_be, int32_t *rgb2yuv)
{
    int i;
    const float **src = (const float **)_src;
    uint16_t *dst        = (uint16_t *)_dst;

    for (i = 0; i < width; i++) {
        dst[i] = lrintf(av_clipf(65535.0f * rdpx(src[3] + i), 0.0f, 65535.0f));
    }
}

static av_always_inline void planar_rgbf32_to_uv(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *_src[4], int width, int is_be, int32_t *rgb2yuv)
{
    int i;
    const float **src = (const float **)_src;
    uint16_t *dstU       = (uint16_t *)_dstU;
    uint16_t *dstV       = (uint16_t *)_dstV;
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];

    for (i = 0; i < width; i++) {
        int g = lrintf(av_clipf(65535.0f * rdpx(src[0] + i), 0.0f, 65535.0f));
        int b = lrintf(av_clipf(65535.0f * rdpx(src[1] + i), 0.0f, 65535.0f));
        int r = lrintf(av_clipf(65535.0f * rdpx(src[2] + i), 0.0f, 65535.0f));

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void planar_rgbf32_to_y(uint8_t *_dst, const uint8_t *_src[4], int width, int is_be, int32_t *rgb2yuv)
{
    int i;
    const float **src = (const float **)_src;
    uint16_t *dst    = (uint16_t *)_dst;

    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];

    for (i = 0; i < width; i++) {
        int g = lrintf(av_clipf(65535.0f * rdpx(src[0] + i), 0.0f, 65535.0f));
        int b = lrintf(av_clipf(65535.0f * rdpx(src[1] + i), 0.0f, 65535.0f));
        int r = lrintf(av_clipf(65535.0f * rdpx(src[2] + i), 0.0f, 65535.0f));

        dst[i] = (ry*r + gy*g + by*b + (0x2001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void grayf32ToY16_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1,
                                            const uint8_t *unused2, int width, int is_be, uint32_t *unused)
{
    int i;
    const float *src = (const float *)_src;
    uint16_t *dst    = (uint16_t *)_dst;

    for (i = 0; i < width; ++i){
        dst[i] = lrintf(av_clipf(65535.0f * rdpx(src + i), 0.0f,  65535.0f));
    }
}

#undef rdpx

#define rgb9plus_planar_funcs_endian(nbits, endian_name, endian)                                    \
static void planar_rgb##nbits##endian_name##_to_y(uint8_t *dst, const uint8_t *src[4],              \
                                                  int w, int32_t *rgb2yuv, void *opq)               \
{                                                                                                   \
    planar_rgb16_to_y(dst, src, w, nbits, endian, rgb2yuv);                                         \
}                                                                                                   \
static void planar_rgb##nbits##endian_name##_to_uv(uint8_t *dstU, uint8_t *dstV,                    \
                                                   const uint8_t *src[4], int w, int32_t *rgb2yuv,  \
                                                   void *opq)                                       \
{                                                                                                   \
    planar_rgb16_to_uv(dstU, dstV, src, w, nbits, endian, rgb2yuv);                                 \
}                                                                                                   \

#define rgb9plus_planar_transparency_funcs(nbits)                           \
static void planar_rgb##nbits##le_to_a(uint8_t *dst, const uint8_t *src[4], \
                                       int w, int32_t *rgb2yuv,             \
                                       void *opq)                           \
{                                                                           \
    planar_rgb16_to_a(dst, src, w, nbits, 0, rgb2yuv);                      \
}                                                                           \
static void planar_rgb##nbits##be_to_a(uint8_t *dst, const uint8_t *src[4], \
                                       int w, int32_t *rgb2yuv,             \
                                       void *opq)                           \
{                                                                           \
    planar_rgb16_to_a(dst, src, w, nbits, 1, rgb2yuv);                      \
}

#define rgb9plus_planar_funcs(nbits)            \
    rgb9plus_planar_funcs_endian(nbits, le, 0)  \
    rgb9plus_planar_funcs_endian(nbits, be, 1)

rgb9plus_planar_funcs(9)
rgb9plus_planar_funcs(10)
rgb9plus_planar_funcs(12)
rgb9plus_planar_funcs(14)
rgb9plus_planar_funcs(16)

rgb9plus_planar_transparency_funcs(10)
rgb9plus_planar_transparency_funcs(12)
rgb9plus_planar_transparency_funcs(14)
rgb9plus_planar_transparency_funcs(16)

#define rgbf32_planar_funcs_endian(endian_name, endian)                                             \
static void planar_rgbf32##endian_name##_to_y(uint8_t *dst, const uint8_t *src[4],                  \
                                                  int w, int32_t *rgb2yuv, void *opq)               \
{                                                                                                   \
    planar_rgbf32_to_y(dst, src, w, endian, rgb2yuv);                                               \
}                                                                                                   \
static void planar_rgbf32##endian_name##_to_uv(uint8_t *dstU, uint8_t *dstV,                        \
                                               const uint8_t *src[4], int w, int32_t *rgb2yuv,      \
                                               void *opq)                                           \
{                                                                                                   \
    planar_rgbf32_to_uv(dstU, dstV, src, w, endian, rgb2yuv);                                       \
}                                                                                                   \
static void planar_rgbf32##endian_name##_to_a(uint8_t *dst, const uint8_t *src[4],                  \
                                              int w, int32_t *rgb2yuv, void *opq)                   \
{                                                                                                   \
    planar_rgbf32_to_a(dst, src, w, endian, rgb2yuv);                                               \
}                                                                                                   \
static void grayf32##endian_name##ToY16_c(uint8_t *dst, const uint8_t *src,                         \
                                          const uint8_t *unused1, const uint8_t *unused2,           \
                                          int width, uint32_t *unused, void *opq)                   \
{                                                                                                   \
    grayf32ToY16_c(dst, src, unused1, unused2, width, endian, unused);                              \
}

rgbf32_planar_funcs_endian(le, 0)
rgbf32_planar_funcs_endian(be, 1)

#define rdpx(src) av_int2float(half2float(is_be ? AV_RB16(&src) : AV_RL16(&src), h2f_tbl))

static av_always_inline void rgbaf16ToUV_half_endian(uint16_t *dstU, uint16_t *dstV, int is_be,
                                                     const uint16_t *src, int width,
                                                     int32_t *rgb2yuv, Half2FloatTables *h2f_tbl)
{
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int r = (lrintf(av_clipf(65535.0f * rdpx(src[i*8+0]), 0.0f, 65535.0f)) +
                 lrintf(av_clipf(65535.0f * rdpx(src[i*8+4]), 0.0f, 65535.0f))) >> 1;
        int g = (lrintf(av_clipf(65535.0f * rdpx(src[i*8+1]), 0.0f, 65535.0f)) +
                 lrintf(av_clipf(65535.0f * rdpx(src[i*8+5]), 0.0f, 65535.0f))) >> 1;
        int b = (lrintf(av_clipf(65535.0f * rdpx(src[i*8+2]), 0.0f, 65535.0f)) +
                 lrintf(av_clipf(65535.0f * rdpx(src[i*8+6]), 0.0f, 65535.0f))) >> 1;

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgbaf16ToUV_endian(uint16_t *dstU, uint16_t *dstV, int is_be,
                                                const uint16_t *src, int width,
                                                int32_t *rgb2yuv, Half2FloatTables *h2f_tbl)
{
    int32_t ru = rgb2yuv[RU_IDX], gu = rgb2yuv[GU_IDX], bu = rgb2yuv[BU_IDX];
    int32_t rv = rgb2yuv[RV_IDX], gv = rgb2yuv[GV_IDX], bv = rgb2yuv[BV_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int r = lrintf(av_clipf(65535.0f * rdpx(src[i*4+0]), 0.0f, 65535.0f));
        int g = lrintf(av_clipf(65535.0f * rdpx(src[i*4+1]), 0.0f, 65535.0f));
        int b = lrintf(av_clipf(65535.0f * rdpx(src[i*4+2]), 0.0f, 65535.0f));

        dstU[i] = (ru*r + gu*g + bu*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (rv*r + gv*g + bv*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgbaf16ToY_endian(uint16_t *dst, const uint16_t *src, int is_be,
                                               int width, int32_t *rgb2yuv, Half2FloatTables *h2f_tbl)
{
    int32_t ry = rgb2yuv[RY_IDX], gy = rgb2yuv[GY_IDX], by = rgb2yuv[BY_IDX];
    int i;
    for (i = 0; i < width; i++) {
        int r = lrintf(av_clipf(65535.0f * rdpx(src[i*4+0]), 0.0f, 65535.0f));
        int g = lrintf(av_clipf(65535.0f * rdpx(src[i*4+1]), 0.0f, 65535.0f));
        int b = lrintf(av_clipf(65535.0f * rdpx(src[i*4+2]), 0.0f, 65535.0f));

        dst[i] = (ry*r + gy*g + by*b + (0x2001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgbaf16ToA_endian(uint16_t *dst, const uint16_t *src, int is_be,
                                               int width, Half2FloatTables *h2f_tbl)
{
    int i;
    for (i=0; i<width; i++) {
        dst[i] = lrintf(av_clipf(65535.0f * rdpx(src[i*4+3]), 0.0f, 65535.0f));
    }
}

#undef rdpx

#define rgbaf16_funcs_endian(endian_name, endian)                                                         \
static void rgbaf16##endian_name##ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused,      \
                                              const uint8_t *src1, const uint8_t *src2,                   \
                                              int width, uint32_t *_rgb2yuv, void *opq)                   \
{                                                                                                         \
    const uint16_t *src = (const uint16_t*)src1;                                                          \
    uint16_t *dstU = (uint16_t*)_dstU;                                                                    \
    uint16_t *dstV = (uint16_t*)_dstV;                                                                    \
    int32_t *rgb2yuv = (int32_t*)_rgb2yuv;                                                                \
    av_assert1(src1==src2);                                                                               \
    rgbaf16ToUV_half_endian(dstU, dstV, endian, src, width, rgb2yuv, opq);                                \
}                                                                                                         \
static void rgbaf16##endian_name##ToUV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused,           \
                                         const uint8_t *src1, const uint8_t *src2,                        \
                                         int width, uint32_t *_rgb2yuv, void *opq)                        \
{                                                                                                         \
    const uint16_t *src = (const uint16_t*)src1;                                                          \
    uint16_t *dstU = (uint16_t*)_dstU;                                                                    \
    uint16_t *dstV = (uint16_t*)_dstV;                                                                    \
    int32_t *rgb2yuv = (int32_t*)_rgb2yuv;                                                                \
    av_assert1(src1==src2);                                                                               \
    rgbaf16ToUV_endian(dstU, dstV, endian, src, width, rgb2yuv, opq);                                \
}                                                                                                         \
static void rgbaf16##endian_name##ToY_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused0,       \
                                        const uint8_t *unused1, int width, uint32_t *_rgb2yuv, void *opq) \
{                                                                                                         \
    const uint16_t *src = (const uint16_t*)_src;                                                          \
    uint16_t *dst = (uint16_t*)_dst;                                                                      \
    int32_t *rgb2yuv = (int32_t*)_rgb2yuv;                                                                \
    rgbaf16ToY_endian(dst, src, endian, width, rgb2yuv, opq);                                             \
}                                                                                                         \
static void rgbaf16##endian_name##ToA_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused0,       \
                                        const uint8_t *unused1, int width, uint32_t *unused2, void *opq)  \
{                                                                                                         \
    const uint16_t *src = (const uint16_t*)_src;                                                          \
    uint16_t *dst = (uint16_t*)_dst;                                                                      \
    rgbaf16ToA_endian(dst, src, endian, width, opq);                                                      \
}

rgbaf16_funcs_endian(le, 0)
rgbaf16_funcs_endian(be, 1)

av_cold void ff_sws_init_input_funcs(SwsContext *c)
{
    enum AVPixelFormat srcFormat = c->srcFormat;

    c->chrToYV12 = NULL;
    switch (srcFormat) {
    case AV_PIX_FMT_YUYV422:
        c->chrToYV12 = yuy2ToUV_c;
        break;
    case AV_PIX_FMT_YVYU422:
        c->chrToYV12 = yvy2ToUV_c;
        break;
    case AV_PIX_FMT_UYVY422:
        c->chrToYV12 = uyvyToUV_c;
        break;
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV16:
    case AV_PIX_FMT_NV24:
        c->chrToYV12 = nv12ToUV_c;
        break;
    case AV_PIX_FMT_NV21:
    case AV_PIX_FMT_NV42:
        c->chrToYV12 = nv21ToUV_c;
        break;
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_PAL8:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_RGB4_BYTE:
        c->chrToYV12 = palToUV_c;
        break;
    case AV_PIX_FMT_GBRP9LE:
        c->readChrPlanar = planar_rgb9le_to_uv;
        break;
    case AV_PIX_FMT_GBRAP10LE:
    case AV_PIX_FMT_GBRP10LE:
        c->readChrPlanar = planar_rgb10le_to_uv;
        break;
    case AV_PIX_FMT_GBRAP12LE:
    case AV_PIX_FMT_GBRP12LE:
        c->readChrPlanar = planar_rgb12le_to_uv;
        break;
    case AV_PIX_FMT_GBRAP14LE:
    case AV_PIX_FMT_GBRP14LE:
        c->readChrPlanar = planar_rgb14le_to_uv;
        break;
    case AV_PIX_FMT_GBRAP16LE:
    case AV_PIX_FMT_GBRP16LE:
        c->readChrPlanar = planar_rgb16le_to_uv;
        break;
    case AV_PIX_FMT_GBRAPF32LE:
    case AV_PIX_FMT_GBRPF32LE:
        c->readChrPlanar = planar_rgbf32le_to_uv;
        break;
    case AV_PIX_FMT_GBRP9BE:
        c->readChrPlanar = planar_rgb9be_to_uv;
        break;
    case AV_PIX_FMT_GBRAP10BE:
    case AV_PIX_FMT_GBRP10BE:
        c->readChrPlanar = planar_rgb10be_to_uv;
        break;
    case AV_PIX_FMT_GBRAP12BE:
    case AV_PIX_FMT_GBRP12BE:
        c->readChrPlanar = planar_rgb12be_to_uv;
        break;
    case AV_PIX_FMT_GBRAP14BE:
    case AV_PIX_FMT_GBRP14BE:
        c->readChrPlanar = planar_rgb14be_to_uv;
        break;
    case AV_PIX_FMT_GBRAP16BE:
    case AV_PIX_FMT_GBRP16BE:
        c->readChrPlanar = planar_rgb16be_to_uv;
        break;
    case AV_PIX_FMT_GBRAPF32BE:
    case AV_PIX_FMT_GBRPF32BE:
        c->readChrPlanar = planar_rgbf32be_to_uv;
        break;
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        c->readChrPlanar = planar_rgb_to_uv;
        break;
#if HAVE_BIGENDIAN
    case AV_PIX_FMT_YUV420P9LE:
    case AV_PIX_FMT_YUV422P9LE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV440P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV440P12LE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV420P14LE:
    case AV_PIX_FMT_YUV422P14LE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV444P16LE:

    case AV_PIX_FMT_YUVA420P9LE:
    case AV_PIX_FMT_YUVA422P9LE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUVA420P10LE:
    case AV_PIX_FMT_YUVA422P10LE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUVA422P12LE:
    case AV_PIX_FMT_YUVA444P12LE:
    case AV_PIX_FMT_YUVA420P16LE:
    case AV_PIX_FMT_YUVA422P16LE:
    case AV_PIX_FMT_YUVA444P16LE:
        c->chrToYV12 = bswap16UV_c;
        break;
#else
    case AV_PIX_FMT_YUV420P9BE:
    case AV_PIX_FMT_YUV422P9BE:
    case AV_PIX_FMT_YUV444P9BE:
    case AV_PIX_FMT_YUV420P10BE:
    case AV_PIX_FMT_YUV422P10BE:
    case AV_PIX_FMT_YUV440P10BE:
    case AV_PIX_FMT_YUV444P10BE:
    case AV_PIX_FMT_YUV420P12BE:
    case AV_PIX_FMT_YUV422P12BE:
    case AV_PIX_FMT_YUV440P12BE:
    case AV_PIX_FMT_YUV444P12BE:
    case AV_PIX_FMT_YUV420P14BE:
    case AV_PIX_FMT_YUV422P14BE:
    case AV_PIX_FMT_YUV444P14BE:
    case AV_PIX_FMT_YUV420P16BE:
    case AV_PIX_FMT_YUV422P16BE:
    case AV_PIX_FMT_YUV444P16BE:

    case AV_PIX_FMT_YUVA420P9BE:
    case AV_PIX_FMT_YUVA422P9BE:
    case AV_PIX_FMT_YUVA444P9BE:
    case AV_PIX_FMT_YUVA420P10BE:
    case AV_PIX_FMT_YUVA422P10BE:
    case AV_PIX_FMT_YUVA444P10BE:
    case AV_PIX_FMT_YUVA422P12BE:
    case AV_PIX_FMT_YUVA444P12BE:
    case AV_PIX_FMT_YUVA420P16BE:
    case AV_PIX_FMT_YUVA422P16BE:
    case AV_PIX_FMT_YUVA444P16BE:
        c->chrToYV12 = bswap16UV_c;
        break;
#endif
    case AV_PIX_FMT_VUYA:
    case AV_PIX_FMT_VUYX:
        c->chrToYV12 = read_vuyx_UV_c;
        break;
    case AV_PIX_FMT_XV30LE:
        c->chrToYV12 = read_xv30le_UV_c;
        break;
    case AV_PIX_FMT_AYUV64LE:
        c->chrToYV12 = read_ayuv64le_UV_c;
        break;
    case AV_PIX_FMT_XV36LE:
        c->chrToYV12 = read_xv36le_UV_c;
        break;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P210LE:
    case AV_PIX_FMT_P410LE:
        c->chrToYV12 = p010LEToUV_c;
        break;
    case AV_PIX_FMT_P010BE:
    case AV_PIX_FMT_P210BE:
    case AV_PIX_FMT_P410BE:
        c->chrToYV12 = p010BEToUV_c;
        break;
    case AV_PIX_FMT_P012LE:
    case AV_PIX_FMT_P212LE:
    case AV_PIX_FMT_P412LE:
        c->chrToYV12 = p012LEToUV_c;
        break;
    case AV_PIX_FMT_P012BE:
    case AV_PIX_FMT_P212BE:
    case AV_PIX_FMT_P412BE:
        c->chrToYV12 = p012BEToUV_c;
        break;
    case AV_PIX_FMT_P016LE:
    case AV_PIX_FMT_P216LE:
    case AV_PIX_FMT_P416LE:
        c->chrToYV12 = p016LEToUV_c;
        break;
    case AV_PIX_FMT_P016BE:
    case AV_PIX_FMT_P216BE:
    case AV_PIX_FMT_P416BE:
        c->chrToYV12 = p016BEToUV_c;
        break;
    case AV_PIX_FMT_Y210LE:
        c->chrToYV12 = y210le_UV_c;
        break;
    case AV_PIX_FMT_Y212LE:
        c->chrToYV12 = y212le_UV_c;
        break;
    }
    if (c->chrSrcHSubSample) {
        switch (srcFormat) {
        case AV_PIX_FMT_RGBA64BE:
            c->chrToYV12 = rgb64BEToUV_half_c;
            break;
        case AV_PIX_FMT_RGBA64LE:
            c->chrToYV12 = rgb64LEToUV_half_c;
            break;
        case AV_PIX_FMT_BGRA64BE:
            c->chrToYV12 = bgr64BEToUV_half_c;
            break;
        case AV_PIX_FMT_BGRA64LE:
            c->chrToYV12 = bgr64LEToUV_half_c;
            break;
        case AV_PIX_FMT_RGB48BE:
            c->chrToYV12 = rgb48BEToUV_half_c;
            break;
        case AV_PIX_FMT_RGB48LE:
            c->chrToYV12 = rgb48LEToUV_half_c;
            break;
        case AV_PIX_FMT_BGR48BE:
            c->chrToYV12 = bgr48BEToUV_half_c;
            break;
        case AV_PIX_FMT_BGR48LE:
            c->chrToYV12 = bgr48LEToUV_half_c;
            break;
        case AV_PIX_FMT_RGB32:
            c->chrToYV12 = bgr32ToUV_half_c;
            break;
        case AV_PIX_FMT_RGB32_1:
            c->chrToYV12 = bgr321ToUV_half_c;
            break;
        case AV_PIX_FMT_BGR24:
            c->chrToYV12 = bgr24ToUV_half_c;
            break;
        case AV_PIX_FMT_BGR565LE:
            c->chrToYV12 = bgr16leToUV_half_c;
            break;
        case AV_PIX_FMT_BGR565BE:
            c->chrToYV12 = bgr16beToUV_half_c;
            break;
        case AV_PIX_FMT_BGR555LE:
            c->chrToYV12 = bgr15leToUV_half_c;
            break;
        case AV_PIX_FMT_BGR555BE:
            c->chrToYV12 = bgr15beToUV_half_c;
            break;
        case AV_PIX_FMT_GBRAP:
        case AV_PIX_FMT_GBRP:
            c->chrToYV12 = gbr24pToUV_half_c;
            break;
        case AV_PIX_FMT_BGR444LE:
            c->chrToYV12 = bgr12leToUV_half_c;
            break;
        case AV_PIX_FMT_BGR444BE:
            c->chrToYV12 = bgr12beToUV_half_c;
            break;
        case AV_PIX_FMT_BGR32:
            c->chrToYV12 = rgb32ToUV_half_c;
            break;
        case AV_PIX_FMT_BGR32_1:
            c->chrToYV12 = rgb321ToUV_half_c;
            break;
        case AV_PIX_FMT_RGB24:
            c->chrToYV12 = rgb24ToUV_half_c;
            break;
        case AV_PIX_FMT_RGB565LE:
            c->chrToYV12 = rgb16leToUV_half_c;
            break;
        case AV_PIX_FMT_RGB565BE:
            c->chrToYV12 = rgb16beToUV_half_c;
            break;
        case AV_PIX_FMT_RGB555LE:
            c->chrToYV12 = rgb15leToUV_half_c;
            break;
        case AV_PIX_FMT_RGB555BE:
            c->chrToYV12 = rgb15beToUV_half_c;
            break;
        case AV_PIX_FMT_RGB444LE:
            c->chrToYV12 = rgb12leToUV_half_c;
            break;
        case AV_PIX_FMT_RGB444BE:
            c->chrToYV12 = rgb12beToUV_half_c;
            break;
        case AV_PIX_FMT_X2RGB10LE:
            c->chrToYV12 = rgb30leToUV_half_c;
            break;
        case AV_PIX_FMT_X2BGR10LE:
            c->chrToYV12 = bgr30leToUV_half_c;
            break;
        case AV_PIX_FMT_RGBAF16BE:
            c->chrToYV12 = rgbaf16beToUV_half_c;
            break;
        case AV_PIX_FMT_RGBAF16LE:
            c->chrToYV12 = rgbaf16leToUV_half_c;
            break;
        }
    } else {
        switch (srcFormat) {
        case AV_PIX_FMT_RGBA64BE:
            c->chrToYV12 = rgb64BEToUV_c;
            break;
        case AV_PIX_FMT_RGBA64LE:
            c->chrToYV12 = rgb64LEToUV_c;
            break;
        case AV_PIX_FMT_BGRA64BE:
            c->chrToYV12 = bgr64BEToUV_c;
            break;
        case AV_PIX_FMT_BGRA64LE:
            c->chrToYV12 = bgr64LEToUV_c;
            break;
        case AV_PIX_FMT_RGB48BE:
            c->chrToYV12 = rgb48BEToUV_c;
            break;
        case AV_PIX_FMT_RGB48LE:
            c->chrToYV12 = rgb48LEToUV_c;
            break;
        case AV_PIX_FMT_BGR48BE:
            c->chrToYV12 = bgr48BEToUV_c;
            break;
        case AV_PIX_FMT_BGR48LE:
            c->chrToYV12 = bgr48LEToUV_c;
            break;
        case AV_PIX_FMT_RGB32:
            c->chrToYV12 = bgr32ToUV_c;
            break;
        case AV_PIX_FMT_RGB32_1:
            c->chrToYV12 = bgr321ToUV_c;
            break;
        case AV_PIX_FMT_BGR24:
            c->chrToYV12 = bgr24ToUV_c;
            break;
        case AV_PIX_FMT_BGR565LE:
            c->chrToYV12 = bgr16leToUV_c;
            break;
        case AV_PIX_FMT_BGR565BE:
            c->chrToYV12 = bgr16beToUV_c;
            break;
        case AV_PIX_FMT_BGR555LE:
            c->chrToYV12 = bgr15leToUV_c;
            break;
        case AV_PIX_FMT_BGR555BE:
            c->chrToYV12 = bgr15beToUV_c;
            break;
        case AV_PIX_FMT_BGR444LE:
            c->chrToYV12 = bgr12leToUV_c;
            break;
        case AV_PIX_FMT_BGR444BE:
            c->chrToYV12 = bgr12beToUV_c;
            break;
        case AV_PIX_FMT_BGR32:
            c->chrToYV12 = rgb32ToUV_c;
            break;
        case AV_PIX_FMT_BGR32_1:
            c->chrToYV12 = rgb321ToUV_c;
            break;
        case AV_PIX_FMT_RGB24:
            c->chrToYV12 = rgb24ToUV_c;
            break;
        case AV_PIX_FMT_RGB565LE:
            c->chrToYV12 = rgb16leToUV_c;
            break;
        case AV_PIX_FMT_RGB565BE:
            c->chrToYV12 = rgb16beToUV_c;
            break;
        case AV_PIX_FMT_RGB555LE:
            c->chrToYV12 = rgb15leToUV_c;
            break;
        case AV_PIX_FMT_RGB555BE:
            c->chrToYV12 = rgb15beToUV_c;
            break;
        case AV_PIX_FMT_RGB444LE:
            c->chrToYV12 = rgb12leToUV_c;
            break;
        case AV_PIX_FMT_RGB444BE:
            c->chrToYV12 = rgb12beToUV_c;
            break;
        case AV_PIX_FMT_X2RGB10LE:
            c->chrToYV12 = rgb30leToUV_c;
            break;
        case AV_PIX_FMT_X2BGR10LE:
            c->chrToYV12 = bgr30leToUV_c;
            break;
        case AV_PIX_FMT_RGBAF16BE:
            c->chrToYV12 = rgbaf16beToUV_c;
            break;
        case AV_PIX_FMT_RGBAF16LE:
            c->chrToYV12 = rgbaf16leToUV_c;
            break;
        }
    }

    c->lumToYV12 = NULL;
    c->alpToYV12 = NULL;
    switch (srcFormat) {
    case AV_PIX_FMT_GBRP9LE:
        c->readLumPlanar = planar_rgb9le_to_y;
        break;
    case AV_PIX_FMT_GBRAP10LE:
        c->readAlpPlanar = planar_rgb10le_to_a;
    case AV_PIX_FMT_GBRP10LE:
        c->readLumPlanar = planar_rgb10le_to_y;
        break;
    case AV_PIX_FMT_GBRAP12LE:
        c->readAlpPlanar = planar_rgb12le_to_a;
    case AV_PIX_FMT_GBRP12LE:
        c->readLumPlanar = planar_rgb12le_to_y;
        break;
    case AV_PIX_FMT_GBRAP14LE:
        c->readAlpPlanar = planar_rgb14le_to_a;
    case AV_PIX_FMT_GBRP14LE:
        c->readLumPlanar = planar_rgb14le_to_y;
        break;
    case AV_PIX_FMT_GBRAP16LE:
        c->readAlpPlanar = planar_rgb16le_to_a;
    case AV_PIX_FMT_GBRP16LE:
        c->readLumPlanar = planar_rgb16le_to_y;
        break;
    case AV_PIX_FMT_GBRAPF32LE:
        c->readAlpPlanar = planar_rgbf32le_to_a;
    case AV_PIX_FMT_GBRPF32LE:
        c->readLumPlanar = planar_rgbf32le_to_y;
        break;
    case AV_PIX_FMT_GBRP9BE:
        c->readLumPlanar = planar_rgb9be_to_y;
        break;
    case AV_PIX_FMT_GBRAP10BE:
        c->readAlpPlanar = planar_rgb10be_to_a;
    case AV_PIX_FMT_GBRP10BE:
        c->readLumPlanar = planar_rgb10be_to_y;
        break;
    case AV_PIX_FMT_GBRAP12BE:
        c->readAlpPlanar = planar_rgb12be_to_a;
    case AV_PIX_FMT_GBRP12BE:
        c->readLumPlanar = planar_rgb12be_to_y;
        break;
    case AV_PIX_FMT_GBRAP14BE:
        c->readAlpPlanar = planar_rgb14be_to_a;
    case AV_PIX_FMT_GBRP14BE:
        c->readLumPlanar = planar_rgb14be_to_y;
        break;
    case AV_PIX_FMT_GBRAP16BE:
        c->readAlpPlanar = planar_rgb16be_to_a;
    case AV_PIX_FMT_GBRP16BE:
        c->readLumPlanar = planar_rgb16be_to_y;
        break;
    case AV_PIX_FMT_GBRAPF32BE:
        c->readAlpPlanar = planar_rgbf32be_to_a;
    case AV_PIX_FMT_GBRPF32BE:
        c->readLumPlanar = planar_rgbf32be_to_y;
        break;
    case AV_PIX_FMT_GBRAP:
        c->readAlpPlanar = planar_rgb_to_a;
    case AV_PIX_FMT_GBRP:
        c->readLumPlanar = planar_rgb_to_y;
        break;
#if HAVE_BIGENDIAN
    case AV_PIX_FMT_YUV420P9LE:
    case AV_PIX_FMT_YUV422P9LE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV440P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV440P12LE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV420P14LE:
    case AV_PIX_FMT_YUV422P14LE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV444P16LE:

    case AV_PIX_FMT_GRAY9LE:
    case AV_PIX_FMT_GRAY10LE:
    case AV_PIX_FMT_GRAY12LE:
    case AV_PIX_FMT_GRAY14LE:
    case AV_PIX_FMT_GRAY16LE:

    case AV_PIX_FMT_P016LE:
    case AV_PIX_FMT_P216LE:
    case AV_PIX_FMT_P416LE:
        c->lumToYV12 = bswap16Y_c;
        break;
    case AV_PIX_FMT_YUVA420P9LE:
    case AV_PIX_FMT_YUVA422P9LE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUVA420P10LE:
    case AV_PIX_FMT_YUVA422P10LE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUVA422P12LE:
    case AV_PIX_FMT_YUVA444P12LE:
    case AV_PIX_FMT_YUVA420P16LE:
    case AV_PIX_FMT_YUVA422P16LE:
    case AV_PIX_FMT_YUVA444P16LE:
        c->lumToYV12 = bswap16Y_c;
        c->alpToYV12 = bswap16Y_c;
        break;
#else
    case AV_PIX_FMT_YUV420P9BE:
    case AV_PIX_FMT_YUV422P9BE:
    case AV_PIX_FMT_YUV444P9BE:
    case AV_PIX_FMT_YUV420P10BE:
    case AV_PIX_FMT_YUV422P10BE:
    case AV_PIX_FMT_YUV440P10BE:
    case AV_PIX_FMT_YUV444P10BE:
    case AV_PIX_FMT_YUV420P12BE:
    case AV_PIX_FMT_YUV422P12BE:
    case AV_PIX_FMT_YUV440P12BE:
    case AV_PIX_FMT_YUV444P12BE:
    case AV_PIX_FMT_YUV420P14BE:
    case AV_PIX_FMT_YUV422P14BE:
    case AV_PIX_FMT_YUV444P14BE:
    case AV_PIX_FMT_YUV420P16BE:
    case AV_PIX_FMT_YUV422P16BE:
    case AV_PIX_FMT_YUV444P16BE:

    case AV_PIX_FMT_GRAY9BE:
    case AV_PIX_FMT_GRAY10BE:
    case AV_PIX_FMT_GRAY12BE:
    case AV_PIX_FMT_GRAY14BE:
    case AV_PIX_FMT_GRAY16BE:

    case AV_PIX_FMT_P016BE:
    case AV_PIX_FMT_P216BE:
    case AV_PIX_FMT_P416BE:
        c->lumToYV12 = bswap16Y_c;
        break;
    case AV_PIX_FMT_YUVA420P9BE:
    case AV_PIX_FMT_YUVA422P9BE:
    case AV_PIX_FMT_YUVA444P9BE:
    case AV_PIX_FMT_YUVA420P10BE:
    case AV_PIX_FMT_YUVA422P10BE:
    case AV_PIX_FMT_YUVA444P10BE:
    case AV_PIX_FMT_YUVA422P12BE:
    case AV_PIX_FMT_YUVA444P12BE:
    case AV_PIX_FMT_YUVA420P16BE:
    case AV_PIX_FMT_YUVA422P16BE:
    case AV_PIX_FMT_YUVA444P16BE:
        c->lumToYV12 = bswap16Y_c;
        c->alpToYV12 = bswap16Y_c;
        break;
#endif
    case AV_PIX_FMT_YA16LE:
        c->lumToYV12 = read_ya16le_gray_c;
        break;
    case AV_PIX_FMT_YA16BE:
        c->lumToYV12 = read_ya16be_gray_c;
        break;
    case AV_PIX_FMT_VUYA:
    case AV_PIX_FMT_VUYX:
        c->lumToYV12 = read_vuyx_Y_c;
        break;
    case AV_PIX_FMT_XV30LE:
        c->lumToYV12 = read_xv30le_Y_c;
        break;
    case AV_PIX_FMT_AYUV64LE:
        c->lumToYV12 = read_ayuv64le_Y_c;
        break;
    case AV_PIX_FMT_XV36LE:
        c->lumToYV12 = read_xv36le_Y_c;
        break;
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_YVYU422:
    case AV_PIX_FMT_YA8:
        c->lumToYV12 = yuy2ToY_c;
        break;
    case AV_PIX_FMT_UYVY422:
        c->lumToYV12 = uyvyToY_c;
        break;
    case AV_PIX_FMT_BGR24:
        c->lumToYV12 = bgr24ToY_c;
        break;
    case AV_PIX_FMT_BGR565LE:
        c->lumToYV12 = bgr16leToY_c;
        break;
    case AV_PIX_FMT_BGR565BE:
        c->lumToYV12 = bgr16beToY_c;
        break;
    case AV_PIX_FMT_BGR555LE:
        c->lumToYV12 = bgr15leToY_c;
        break;
    case AV_PIX_FMT_BGR555BE:
        c->lumToYV12 = bgr15beToY_c;
        break;
    case AV_PIX_FMT_BGR444LE:
        c->lumToYV12 = bgr12leToY_c;
        break;
    case AV_PIX_FMT_BGR444BE:
        c->lumToYV12 = bgr12beToY_c;
        break;
    case AV_PIX_FMT_RGB24:
        c->lumToYV12 = rgb24ToY_c;
        break;
    case AV_PIX_FMT_RGB565LE:
        c->lumToYV12 = rgb16leToY_c;
        break;
    case AV_PIX_FMT_RGB565BE:
        c->lumToYV12 = rgb16beToY_c;
        break;
    case AV_PIX_FMT_RGB555LE:
        c->lumToYV12 = rgb15leToY_c;
        break;
    case AV_PIX_FMT_RGB555BE:
        c->lumToYV12 = rgb15beToY_c;
        break;
    case AV_PIX_FMT_RGB444LE:
        c->lumToYV12 = rgb12leToY_c;
        break;
    case AV_PIX_FMT_RGB444BE:
        c->lumToYV12 = rgb12beToY_c;
        break;
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_PAL8:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_RGB4_BYTE:
        c->lumToYV12 = palToY_c;
        break;
    case AV_PIX_FMT_MONOBLACK:
        c->lumToYV12 = monoblack2Y_c;
        break;
    case AV_PIX_FMT_MONOWHITE:
        c->lumToYV12 = monowhite2Y_c;
        break;
    case AV_PIX_FMT_RGB32:
        c->lumToYV12 = bgr32ToY_c;
        break;
    case AV_PIX_FMT_RGB32_1:
        c->lumToYV12 = bgr321ToY_c;
        break;
    case AV_PIX_FMT_BGR32:
        c->lumToYV12 = rgb32ToY_c;
        break;
    case AV_PIX_FMT_BGR32_1:
        c->lumToYV12 = rgb321ToY_c;
        break;
    case AV_PIX_FMT_RGB48BE:
        c->lumToYV12 = rgb48BEToY_c;
        break;
    case AV_PIX_FMT_RGB48LE:
        c->lumToYV12 = rgb48LEToY_c;
        break;
    case AV_PIX_FMT_BGR48BE:
        c->lumToYV12 = bgr48BEToY_c;
        break;
    case AV_PIX_FMT_BGR48LE:
        c->lumToYV12 = bgr48LEToY_c;
        break;
    case AV_PIX_FMT_RGBA64BE:
        c->lumToYV12 = rgb64BEToY_c;
        break;
    case AV_PIX_FMT_RGBA64LE:
        c->lumToYV12 = rgb64LEToY_c;
        break;
    case AV_PIX_FMT_BGRA64BE:
        c->lumToYV12 = bgr64BEToY_c;
        break;
    case AV_PIX_FMT_BGRA64LE:
        c->lumToYV12 = bgr64LEToY_c;
        break;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P210LE:
    case AV_PIX_FMT_P410LE:
        c->lumToYV12 = p010LEToY_c;
        break;
    case AV_PIX_FMT_P010BE:
    case AV_PIX_FMT_P210BE:
    case AV_PIX_FMT_P410BE:
        c->lumToYV12 = p010BEToY_c;
        break;
    case AV_PIX_FMT_P012LE:
    case AV_PIX_FMT_P212LE:
    case AV_PIX_FMT_P412LE:
        c->lumToYV12 = p012LEToY_c;
        break;
    case AV_PIX_FMT_P012BE:
    case AV_PIX_FMT_P212BE:
    case AV_PIX_FMT_P412BE:
        c->lumToYV12 = p012BEToY_c;
        break;
    case AV_PIX_FMT_GRAYF32LE:
        c->lumToYV12 = grayf32leToY16_c;
        break;
    case AV_PIX_FMT_GRAYF32BE:
        c->lumToYV12 = grayf32beToY16_c;
        break;
    case AV_PIX_FMT_Y210LE:
        c->lumToYV12 = y210le_Y_c;
        break;
    case AV_PIX_FMT_Y212LE:
        c->lumToYV12 = y212le_Y_c;
        break;
    case AV_PIX_FMT_X2RGB10LE:
        c->lumToYV12 = rgb30leToY_c;
        break;
    case AV_PIX_FMT_X2BGR10LE:
        c->lumToYV12 = bgr30leToY_c;
        break;
    case AV_PIX_FMT_RGBAF16BE:
        c->lumToYV12 = rgbaf16beToY_c;
        break;
    case AV_PIX_FMT_RGBAF16LE:
        c->lumToYV12 = rgbaf16leToY_c;
        break;
    }
    if (c->needAlpha) {
        if (is16BPS(srcFormat) || isNBPS(srcFormat)) {
            if (HAVE_BIGENDIAN == !isBE(srcFormat) && !c->readAlpPlanar)
                c->alpToYV12 = bswap16Y_c;
        }
        switch (srcFormat) {
        case AV_PIX_FMT_BGRA64LE:
        case AV_PIX_FMT_RGBA64LE:  c->alpToYV12 = rgba64leToA_c; break;
        case AV_PIX_FMT_BGRA64BE:
        case AV_PIX_FMT_RGBA64BE:  c->alpToYV12 = rgba64beToA_c; break;
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_RGBA:
            c->alpToYV12 = rgbaToA_c;
            break;
        case AV_PIX_FMT_ABGR:
        case AV_PIX_FMT_ARGB:
            c->alpToYV12 = abgrToA_c;
            break;
        case AV_PIX_FMT_RGBAF16BE:
            c->alpToYV12 = rgbaf16beToA_c;
            break;
        case AV_PIX_FMT_RGBAF16LE:
            c->alpToYV12 = rgbaf16leToA_c;
            break;
        case AV_PIX_FMT_YA8:
            c->alpToYV12 = uyvyToY_c;
            break;
        case AV_PIX_FMT_YA16LE:
            c->alpToYV12 = read_ya16le_alpha_c;
            break;
        case AV_PIX_FMT_YA16BE:
            c->alpToYV12 = read_ya16be_alpha_c;
            break;
        case AV_PIX_FMT_VUYA:
            c->alpToYV12 = read_vuya_A_c;
            break;
        case AV_PIX_FMT_AYUV64LE:
            c->alpToYV12 = read_ayuv64le_A_c;
            break;
        case AV_PIX_FMT_PAL8 :
            c->alpToYV12 = palToA_c;
            break;
        }
    }
}
