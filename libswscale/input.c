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

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avutil.h"
#include "libavutil/bswap.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

#define RGB2YUV_SHIFT 15
#define BY  ((int)(0.114 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define BV (-(int)(0.081 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define BU  ((int)(0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GY  ((int)(0.587 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GV (-(int)(0.419 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define GU (-(int)(0.331 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RY  ((int)(0.299 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RV  ((int)(0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))
#define RU (-(int)(0.169 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5))

#define input_pixel(pos) (isBE(origin) ? AV_RB16(pos) : AV_RL16(pos))

#define r ((origin == PIX_FMT_BGR48BE || origin == PIX_FMT_BGR48LE) ? b_r : r_b)
#define b ((origin == PIX_FMT_BGR48BE || origin == PIX_FMT_BGR48LE) ? r_b : b_r)

static av_always_inline void
rgb64ToY_c_template(uint16_t *dst, const uint16_t *src, int width,
                    enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src[i*4+0]);
        unsigned int   g = input_pixel(&src[i*4+1]);
        unsigned int b_r = input_pixel(&src[i*4+2]);

        dst[i] = (RY*r + GY*g + BY*b + (0x2001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb64ToUV_c_template(uint16_t *dstU, uint16_t *dstV,
                    const uint16_t *src1, const uint16_t *src2,
                    int width, enum PixelFormat origin)
{
    int i;
    av_assert1(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = input_pixel(&src1[i*4+0]);
        int   g = input_pixel(&src1[i*4+1]);
        int b_r = input_pixel(&src1[i*4+2]);

        dstU[i] = (RU*r + GU*g + BU*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV*r + GV*g + BV*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void
rgb64ToUV_half_c_template(uint16_t *dstU, uint16_t *dstV,
                          const uint16_t *src1, const uint16_t *src2,
                          int width, enum PixelFormat origin)
{
    int i;
    av_assert1(src1==src2);
    for (i = 0; i < width; i++) {
        int r_b = (input_pixel(&src1[8 * i + 0]) + input_pixel(&src1[8 * i + 4]) + 1) >> 1;
        int   g = (input_pixel(&src1[8 * i + 1]) + input_pixel(&src1[8 * i + 5]) + 1) >> 1;
        int b_r = (input_pixel(&src1[8 * i + 2]) + input_pixel(&src1[8 * i + 6]) + 1) >> 1;

        dstU[i]= (RU*r + GU*g + BU*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (0x10001<<(RGB2YUV_SHIFT-1))) >> RGB2YUV_SHIFT;
    }
}

#define rgb64funcs(pattern, BE_LE, origin) \
static void pattern ## 64 ## BE_LE ## ToY_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused0, const uint8_t *unused1,\
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src = (const uint16_t *) _src; \
    uint16_t *dst = (uint16_t *) _dst; \
    rgb64ToY_c_template(dst, src, width, origin); \
} \
 \
static void pattern ## 64 ## BE_LE ## ToUV_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb64ToUV_c_template(dstU, dstV, src1, src2, width, origin); \
} \
 \
static void pattern ## 64 ## BE_LE ## ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, \
                                    const uint8_t *unused0, const uint8_t *_src1, const uint8_t *_src2, \
                                    int width, uint32_t *unused) \
{ \
    const uint16_t *src1 = (const uint16_t *) _src1, \
                   *src2 = (const uint16_t *) _src2; \
    uint16_t *dstU = (uint16_t *) _dstU, *dstV = (uint16_t *) _dstV; \
    rgb64ToUV_half_c_template(dstU, dstV, src1, src2, width, origin); \
}

rgb64funcs(rgb, LE, PIX_FMT_RGBA64LE)
rgb64funcs(rgb, BE, PIX_FMT_RGBA64BE)

static av_always_inline void rgb48ToY_c_template(uint16_t *dst,
                                                 const uint16_t *src, int width,
                                                 enum PixelFormat origin)
{
    int i;
    for (i = 0; i < width; i++) {
        unsigned int r_b = input_pixel(&src[i * 3 + 0]);
        unsigned int g   = input_pixel(&src[i * 3 + 1]);
        unsigned int b_r = input_pixel(&src[i * 3 + 2]);

        dst[i] = (RY * r + GY * g + BY * b + (0x2001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgb48ToUV_c_template(uint16_t *dstU,
                                                  uint16_t *dstV,
                                                  const uint16_t *src1,
                                                  const uint16_t *src2,
                                                  int width,
                                                  enum PixelFormat origin)
{
    int i;
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r_b = input_pixel(&src1[i * 3 + 0]);
        int g   = input_pixel(&src1[i * 3 + 1]);
        int b_r = input_pixel(&src1[i * 3 + 2]);

        dstU[i] = (RU * r + GU * g + BU * b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV * r + GV * g + BV * b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

static av_always_inline void rgb48ToUV_half_c_template(uint16_t *dstU,
                                                       uint16_t *dstV,
                                                       const uint16_t *src1,
                                                       const uint16_t *src2,
                                                       int width,
                                                       enum PixelFormat origin)
{
    int i;
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r_b = (input_pixel(&src1[6 * i + 0]) +
                   input_pixel(&src1[6 * i + 3]) + 1) >> 1;
        int g   = (input_pixel(&src1[6 * i + 1]) +
                   input_pixel(&src1[6 * i + 4]) + 1) >> 1;
        int b_r = (input_pixel(&src1[6 * i + 2]) +
                   input_pixel(&src1[6 * i + 5]) + 1) >> 1;

        dstU[i] = (RU * r + GU * g + BU * b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV * r + GV * g + BV * b + (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

#undef r
#undef b
#undef input_pixel

#define rgb48funcs(pattern, BE_LE, origin)                              \
static void pattern ## 48 ## BE_LE ## ToY_c(uint8_t *_dst,              \
                                            const uint8_t *_src,        \
                                            const uint8_t *unused0, const uint8_t *unused1,\
                                            int width,                  \
                                            uint32_t *unused)           \
{                                                                       \
    const uint16_t *src = (const uint16_t *)_src;                       \
    uint16_t *dst       = (uint16_t *)_dst;                             \
    rgb48ToY_c_template(dst, src, width, origin);                       \
}                                                                       \
                                                                        \
static void pattern ## 48 ## BE_LE ## ToUV_c(uint8_t *_dstU,            \
                                             uint8_t *_dstV,            \
                                             const uint8_t *unused0,    \
                                             const uint8_t *_src1,      \
                                             const uint8_t *_src2,      \
                                             int width,                 \
                                             uint32_t *unused)          \
{                                                                       \
    const uint16_t *src1 = (const uint16_t *)_src1,                     \
                   *src2 = (const uint16_t *)_src2;                     \
    uint16_t *dstU = (uint16_t *)_dstU,                                 \
             *dstV = (uint16_t *)_dstV;                                 \
    rgb48ToUV_c_template(dstU, dstV, src1, src2, width, origin);        \
}                                                                       \
                                                                        \
static void pattern ## 48 ## BE_LE ## ToUV_half_c(uint8_t *_dstU,       \
                                                  uint8_t *_dstV,       \
                                                  const uint8_t *unused0,    \
                                                  const uint8_t *_src1, \
                                                  const uint8_t *_src2, \
                                                  int width,            \
                                                  uint32_t *unused)     \
{                                                                       \
    const uint16_t *src1 = (const uint16_t *)_src1,                     \
                   *src2 = (const uint16_t *)_src2;                     \
    uint16_t *dstU = (uint16_t *)_dstU,                                 \
             *dstV = (uint16_t *)_dstV;                                 \
    rgb48ToUV_half_c_template(dstU, dstV, src1, src2, width, origin);   \
}

rgb48funcs(rgb, LE, PIX_FMT_RGB48LE)
rgb48funcs(rgb, BE, PIX_FMT_RGB48BE)
rgb48funcs(bgr, LE, PIX_FMT_BGR48LE)
rgb48funcs(bgr, BE, PIX_FMT_BGR48BE)

#define input_pixel(i) ((origin == PIX_FMT_RGBA ||                      \
                         origin == PIX_FMT_BGRA ||                      \
                         origin == PIX_FMT_ARGB ||                      \
                         origin == PIX_FMT_ABGR)                        \
                        ? AV_RN32A(&src[(i) * 4])                       \
                        : (isBE(origin) ? AV_RB16(&src[(i) * 2])        \
                                        : AV_RL16(&src[(i) * 2])))

static av_always_inline void rgb16_32ToY_c_template(int16_t *dst,
                                                    const uint8_t *src,
                                                    int width,
                                                    enum PixelFormat origin,
                                                    int shr, int shg,
                                                    int shb, int shp,
                                                    int maskr, int maskg,
                                                    int maskb, int rsh,
                                                    int gsh, int bsh, int S)
{
    const int ry       = RY << rsh, gy = GY << gsh, by = BY << bsh;
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
                                                     enum PixelFormat origin,
                                                     int shr, int shg,
                                                     int shb, int shp,
                                                     int maskr, int maskg,
                                                     int maskb, int rsh,
                                                     int gsh, int bsh, int S)
{
    const int ru       = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv       = RV << rsh, gv = GV << gsh, bv = BV << bsh;
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
                                                          enum PixelFormat origin,
                                                          int shr, int shg,
                                                          int shb, int shp,
                                                          int maskr, int maskg,
                                                          int maskb, int rsh,
                                                          int gsh, int bsh, int S)
{
    const int ru       = RU << rsh, gu = GU << gsh, bu = BU << bsh,
              rv       = RV << rsh, gv = GV << gsh, bv = BV << bsh,
              maskgx   = ~(maskr | maskb);
    const unsigned rnd = (256U<<(S)) + (1<<(S-6));
    int i;

    maskr |= maskr << 1;
    maskb |= maskb << 1;
    maskg |= maskg << 1;
    for (i = 0; i < width; i++) {
        int px0 = input_pixel(2 * i + 0) >> shp;
        int px1 = input_pixel(2 * i + 1) >> shp;
        int b, r, g = (px0 & maskgx) + (px1 & maskgx);
        int rb = px0 + px1 - g;

        b = (rb & maskb) >> shb;
        if (shp ||
            origin == PIX_FMT_BGR565LE || origin == PIX_FMT_BGR565BE ||
            origin == PIX_FMT_RGB565LE || origin == PIX_FMT_RGB565BE) {
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

#define rgb16_32_wrapper(fmt, name, shr, shg, shb, shp, maskr,          \
                         maskg, maskb, rsh, gsh, bsh, S)                \
static void name ## ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,            \
                          int width, uint32_t *unused)                  \
{                                                                       \
    rgb16_32ToY_c_template((int16_t*)dst, src, width, fmt, shr, shg, shb, shp,    \
                           maskr, maskg, maskb, rsh, gsh, bsh, S);      \
}                                                                       \
                                                                        \
static void name ## ToUV_c(uint8_t *dstU, uint8_t *dstV,                \
                           const uint8_t *unused0, const uint8_t *src, const uint8_t *dummy,    \
                           int width, uint32_t *unused)                 \
{                                                                       \
    rgb16_32ToUV_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt,                \
                            shr, shg, shb, shp,                         \
                            maskr, maskg, maskb, rsh, gsh, bsh, S);     \
}                                                                       \
                                                                        \
static void name ## ToUV_half_c(uint8_t *dstU, uint8_t *dstV,           \
                                const uint8_t *unused0, const uint8_t *src,                     \
                                const uint8_t *dummy,                   \
                                int width, uint32_t *unused)            \
{                                                                       \
    rgb16_32ToUV_half_c_template((int16_t*)dstU, (int16_t*)dstV, src, width, fmt,           \
                                 shr, shg, shb, shp,                    \
                                 maskr, maskg, maskb,                   \
                                 rsh, gsh, bsh, S);                     \
}

rgb16_32_wrapper(PIX_FMT_BGR32,    bgr32,  16, 0,  0, 0, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_BGR32_1,  bgr321, 16, 0,  0, 8, 0xFF0000, 0xFF00,   0x00FF,  8, 0,  8, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_RGB32,    rgb32,   0, 0, 16, 0,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_RGB32_1,  rgb321,  0, 0, 16, 8,   0x00FF, 0xFF00, 0xFF0000,  8, 0,  8, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_BGR565LE, bgr16le, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_BGR555LE, bgr15le, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT + 7)
rgb16_32_wrapper(PIX_FMT_BGR444LE, bgr12le, 0, 0,  0, 0,   0x000F, 0x00F0,   0x0F00,  8, 4,  0, RGB2YUV_SHIFT + 4)
rgb16_32_wrapper(PIX_FMT_RGB565LE, rgb16le, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_RGB555LE, rgb15le, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT + 7)
rgb16_32_wrapper(PIX_FMT_RGB444LE, rgb12le, 0, 0,  0, 0,   0x0F00, 0x00F0,   0x000F,  0, 4,  8, RGB2YUV_SHIFT + 4)
rgb16_32_wrapper(PIX_FMT_BGR565BE, bgr16be, 0, 0,  0, 0,   0x001F, 0x07E0,   0xF800, 11, 5,  0, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_BGR555BE, bgr15be, 0, 0,  0, 0,   0x001F, 0x03E0,   0x7C00, 10, 5,  0, RGB2YUV_SHIFT + 7)
rgb16_32_wrapper(PIX_FMT_BGR444BE, bgr12be, 0, 0,  0, 0,   0x000F, 0x00F0,   0x0F00,  8, 4,  0, RGB2YUV_SHIFT + 4)
rgb16_32_wrapper(PIX_FMT_RGB565BE, rgb16be, 0, 0,  0, 0,   0xF800, 0x07E0,   0x001F,  0, 5, 11, RGB2YUV_SHIFT + 8)
rgb16_32_wrapper(PIX_FMT_RGB555BE, rgb15be, 0, 0,  0, 0,   0x7C00, 0x03E0,   0x001F,  0, 5, 10, RGB2YUV_SHIFT + 7)
rgb16_32_wrapper(PIX_FMT_RGB444BE, rgb12be, 0, 0,  0, 0,   0x0F00, 0x00F0,   0x000F,  0, 4,  8, RGB2YUV_SHIFT + 4)

static void gbr24pToUV_half_c(uint8_t *_dstU, uint8_t *_dstV,
                         const uint8_t *gsrc, const uint8_t *bsrc, const uint8_t *rsrc,
                         int width, uint32_t *unused)
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    int i;
    for (i = 0; i < width; i++) {
        unsigned int g   = gsrc[2*i] + gsrc[2*i+1];
        unsigned int b   = bsrc[2*i] + bsrc[2*i+1];
        unsigned int r   = rsrc[2*i] + rsrc[2*i+1];

        dstU[i] = (RU*r + GU*g + BU*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
        dstV[i] = (RV*r + GV*g + BV*b + (0x4001<<(RGB2YUV_SHIFT-6))) >> (RGB2YUV_SHIFT-6+1);
    }
}

static void rgba64ToA_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1,
                        const uint8_t *unused2, int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    const uint16_t *src = (const uint16_t *)_src;
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[4 * i + 3];
}

static void abgrToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i]<<6;
    }
}

static void rgbaToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        dst[i]= src[4*i+3]<<6;
    }
}

static void palToA_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *pal)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i=0; i<width; i++) {
        int d= src[i];

        dst[i]= (pal[d] >> 24)<<6;
    }
}

static void palToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width, uint32_t *pal)
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
                      int width, uint32_t *pal)
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

static void monowhite2Y_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width, uint32_t *unused)
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

static void monoblack2Y_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width, uint32_t *unused)
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

static void yuy2ToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                      uint32_t *unused)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[2 * i];
}

static void yuy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = src1[4 * i + 1];
        dstV[i] = src1[4 * i + 3];
    }
    av_assert1(src1 == src2);
}

static void bswap16Y_c(uint8_t *_dst, const uint8_t *_src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                       uint32_t *unused)
{
    int i;
    const uint16_t *src = (const uint16_t *)_src;
    uint16_t *dst       = (uint16_t *)_dst;
    for (i = 0; i < width; i++)
        dst[i] = av_bswap16(src[i]);
}

static void bswap16UV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *_src1,
                        const uint8_t *_src2, int width, uint32_t *unused)
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

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src + 1, ...) would have 100% unaligned accesses. */
static void uyvyToY_c(uint8_t *dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,  int width,
                      uint32_t *unused)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = src[2 * i + 1];
}

static void uyvyToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
                       const uint8_t *src2, int width, uint32_t *unused)
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
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstU, dstV, src1, width);
}

static void nv21ToUV_c(uint8_t *dstU, uint8_t *dstV,
                       const uint8_t *unused0, const uint8_t *src1, const uint8_t *src2,
                       int width, uint32_t *unused)
{
    nvXXtoUV_c(dstV, dstU, src1, width);
}

#define input_pixel(pos) (isBE(origin) ? AV_RB16(pos) : AV_RL16(pos))

static void bgr24ToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2,
                       int width, uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i = 0; i < width; i++) {
        int b = src[i * 3 + 0];
        int g = src[i * 3 + 1];
        int r = src[i * 3 + 2];

        dst[i] = ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void bgr24ToUV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    for (i = 0; i < width; i++) {
        int b = src1[3 * i + 0];
        int g = src1[3 * i + 1];
        int r = src1[3 * i + 2];

        dstU[i] = (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i] = (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
    av_assert1(src1 == src2);
}

static void bgr24ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *unused)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    for (i = 0; i < width; i++) {
        int b = src1[6 * i + 0] + src1[6 * i + 3];
        int g = src1[6 * i + 1] + src1[6 * i + 4];
        int r = src1[6 * i + 2] + src1[6 * i + 5];

        dstU[i] = (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i] = (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
    av_assert1(src1 == src2);
}

static void rgb24ToY_c(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1, const uint8_t *unused2, int width,
                       uint32_t *unused)
{
    int16_t *dst = (int16_t *)_dst;
    int i;
    for (i = 0; i < width; i++) {
        int r = src[i * 3 + 0];
        int g = src[i * 3 + 1];
        int b = src[i * 3 + 2];

        dst[i] = ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static void rgb24ToUV_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                        const uint8_t *src2, int width, uint32_t *unused)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r = src1[3 * i + 0];
        int g = src1[3 * i + 1];
        int b = src1[3 * i + 2];

        dstU[i] = (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i] = (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
}

static void rgb24ToUV_half_c(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *unused0, const uint8_t *src1,
                             const uint8_t *src2, int width, uint32_t *unused)
{
    int16_t *dstU = (int16_t *)_dstU;
    int16_t *dstV = (int16_t *)_dstV;
    int i;
    av_assert1(src1 == src2);
    for (i = 0; i < width; i++) {
        int r = src1[6 * i + 0] + src1[6 * i + 3];
        int g = src1[6 * i + 1] + src1[6 * i + 4];
        int b = src1[6 * i + 2] + src1[6 * i + 5];

        dstU[i] = (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i] = (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
}

static void planar_rgb_to_y(uint8_t *_dst, const uint8_t *src[4], int width)
{
    uint16_t *dst = (uint16_t *)_dst;
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dst[i] = (RY*r + GY*g + BY*b + (0x801<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

static void planar_rgb_to_uv(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *src[4], int width)
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    int i;
    for (i = 0; i < width; i++) {
        int g = src[0][i];
        int b = src[1][i];
        int r = src[2][i];

        dstU[i] = (RU*r + GU*g + BU*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
        dstV[i] = (RV*r + GV*g + BV*b + (0x4001<<(RGB2YUV_SHIFT-7))) >> (RGB2YUV_SHIFT-6);
    }
}

#define rdpx(src) \
    is_be ? AV_RB16(src) : AV_RL16(src)
static av_always_inline void planar_rgb16_to_y(uint8_t *_dst, const uint8_t *_src[4],
                                               int width, int bpc, int is_be)
{
    int i;
    const uint16_t **src = (const uint16_t **)_src;
    uint16_t *dst        = (uint16_t *)_dst;
    for (i = 0; i < width; i++) {
        int g = rdpx(src[0] + i);
        int b = rdpx(src[1] + i);
        int r = rdpx(src[2] + i);

        dst[i] = ((RY * r + GY * g + BY * b + (33 << (RGB2YUV_SHIFT + bpc - 9))) >> RGB2YUV_SHIFT);
    }
}

static void planar_rgb9le_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 9, 0);
}

static void planar_rgb9be_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 9, 1);
}

static void planar_rgb10le_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 10, 0);
}

static void planar_rgb10be_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 10, 1);
}

static void planar_rgb12le_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 12, 0);
}

static void planar_rgb12be_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 12, 1);
}

static void planar_rgb14le_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 14, 0);
}

static void planar_rgb14be_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 14, 1);
}

static void planar_rgb16le_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 16, 0);
}

static void planar_rgb16be_to_y(uint8_t *dst, const uint8_t *src[4], int w)
{
    planar_rgb16_to_y(dst, src, w, 16, 1);
}

static av_always_inline void planar_rgb16_to_uv(uint8_t *_dstU, uint8_t *_dstV,
                                                const uint8_t *_src[4], int width,
                                                int bpc, int is_be)
{
    int i;
    const uint16_t **src = (const uint16_t **)_src;
    uint16_t *dstU       = (uint16_t *)_dstU;
    uint16_t *dstV       = (uint16_t *)_dstV;
    for (i = 0; i < width; i++) {
        int g = rdpx(src[0] + i);
        int b = rdpx(src[1] + i);
        int r = rdpx(src[2] + i);

        dstU[i] = (RU * r + GU * g + BU * b + (257 << (RGB2YUV_SHIFT + bpc - 9))) >> RGB2YUV_SHIFT;
        dstV[i] = (RV * r + GV * g + BV * b + (257 << (RGB2YUV_SHIFT + bpc - 9))) >> RGB2YUV_SHIFT;
    }
}
#undef rdpx

static void planar_rgb9le_to_uv(uint8_t *dstU, uint8_t *dstV,
                                const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 9, 0);
}

static void planar_rgb9be_to_uv(uint8_t *dstU, uint8_t *dstV,
                                const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 9, 1);
}

static void planar_rgb10le_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 10, 0);
}

static void planar_rgb10be_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 10, 1);
}

static void planar_rgb12le_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 12, 0);
}

static void planar_rgb12be_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 12, 1);
}

static void planar_rgb14le_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 14, 0);
}

static void planar_rgb14be_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 14, 1);
}

static void planar_rgb16le_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 16, 0);
}

static void planar_rgb16be_to_uv(uint8_t *dstU, uint8_t *dstV,
                                 const uint8_t *src[4], int w)
{
    planar_rgb16_to_uv(dstU, dstV, src, w, 16, 1);
}

av_cold void ff_sws_init_input_funcs(SwsContext *c)
{
    enum PixelFormat srcFormat = c->srcFormat;

    c->chrToYV12 = NULL;
    switch (srcFormat) {
    case PIX_FMT_YUYV422:
        c->chrToYV12 = yuy2ToUV_c;
        break;
    case PIX_FMT_UYVY422:
        c->chrToYV12 = uyvyToUV_c;
        break;
    case PIX_FMT_NV12:
        c->chrToYV12 = nv12ToUV_c;
        break;
    case PIX_FMT_NV21:
        c->chrToYV12 = nv21ToUV_c;
        break;
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:
    case PIX_FMT_PAL8:
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_RGB4_BYTE:
        c->chrToYV12 = palToUV_c;
        break;
    case PIX_FMT_GBRP9LE:
        c->readChrPlanar = planar_rgb9le_to_uv;
        break;
    case PIX_FMT_GBRP10LE:
        c->readChrPlanar = planar_rgb10le_to_uv;
        break;
    case PIX_FMT_GBRP12LE:
        c->readChrPlanar = planar_rgb12le_to_uv;
        break;
    case PIX_FMT_GBRP14LE:
        c->readChrPlanar = planar_rgb14le_to_uv;
        break;
    case PIX_FMT_GBRP16LE:
        c->readChrPlanar = planar_rgb16le_to_uv;
        break;
    case PIX_FMT_GBRP9BE:
        c->readChrPlanar = planar_rgb9be_to_uv;
        break;
    case PIX_FMT_GBRP10BE:
        c->readChrPlanar = planar_rgb10be_to_uv;
        break;
    case PIX_FMT_GBRP12BE:
        c->readChrPlanar = planar_rgb12be_to_uv;
        break;
    case PIX_FMT_GBRP14BE:
        c->readChrPlanar = planar_rgb14be_to_uv;
        break;
    case PIX_FMT_GBRP16BE:
        c->readChrPlanar = planar_rgb16be_to_uv;
        break;
    case PIX_FMT_GBRP:
        c->readChrPlanar = planar_rgb_to_uv;
        break;
#if HAVE_BIGENDIAN
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV422P9LE:
    case PIX_FMT_YUV420P9LE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV420P10LE:
    case PIX_FMT_YUV422P12LE:
    case PIX_FMT_YUV444P12LE:
    case PIX_FMT_YUV420P12LE:
    case PIX_FMT_YUV422P14LE:
    case PIX_FMT_YUV444P14LE:
    case PIX_FMT_YUV420P14LE:
    case PIX_FMT_YUV420P16LE:
    case PIX_FMT_YUV422P16LE:
    case PIX_FMT_YUV444P16LE:
        c->chrToYV12 = bswap16UV_c;
        break;
#else
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV422P9BE:
    case PIX_FMT_YUV420P9BE:
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV420P10BE:
    case PIX_FMT_YUV444P12BE:
    case PIX_FMT_YUV422P12BE:
    case PIX_FMT_YUV420P12BE:
    case PIX_FMT_YUV444P14BE:
    case PIX_FMT_YUV422P14BE:
    case PIX_FMT_YUV420P14BE:
    case PIX_FMT_YUV420P16BE:
    case PIX_FMT_YUV422P16BE:
    case PIX_FMT_YUV444P16BE:
        c->chrToYV12 = bswap16UV_c;
        break;
#endif
    }
    if (c->chrSrcHSubSample) {
        switch (srcFormat) {
        case PIX_FMT_RGBA64BE:
            c->chrToYV12 = rgb64BEToUV_half_c;
            break;
        case PIX_FMT_RGBA64LE:
            c->chrToYV12 = rgb64LEToUV_half_c;
            break;
        case PIX_FMT_RGB48BE:
            c->chrToYV12 = rgb48BEToUV_half_c;
            break;
        case PIX_FMT_RGB48LE:
            c->chrToYV12 = rgb48LEToUV_half_c;
            break;
        case PIX_FMT_BGR48BE:
            c->chrToYV12 = bgr48BEToUV_half_c;
            break;
        case PIX_FMT_BGR48LE:
            c->chrToYV12 = bgr48LEToUV_half_c;
            break;
        case PIX_FMT_RGB32:
            c->chrToYV12 = bgr32ToUV_half_c;
            break;
        case PIX_FMT_RGB32_1:
            c->chrToYV12 = bgr321ToUV_half_c;
            break;
        case PIX_FMT_BGR24:
            c->chrToYV12 = bgr24ToUV_half_c;
            break;
        case PIX_FMT_BGR565LE:
            c->chrToYV12 = bgr16leToUV_half_c;
            break;
        case PIX_FMT_BGR565BE:
            c->chrToYV12 = bgr16beToUV_half_c;
            break;
        case PIX_FMT_BGR555LE:
            c->chrToYV12 = bgr15leToUV_half_c;
            break;
        case PIX_FMT_BGR555BE:
            c->chrToYV12 = bgr15beToUV_half_c;
            break;
        case PIX_FMT_GBR24P  :
            c->chrToYV12 = gbr24pToUV_half_c;
            break;
        case PIX_FMT_BGR444LE:
            c->chrToYV12 = bgr12leToUV_half_c;
            break;
        case PIX_FMT_BGR444BE:
            c->chrToYV12 = bgr12beToUV_half_c;
            break;
        case PIX_FMT_BGR32:
            c->chrToYV12 = rgb32ToUV_half_c;
            break;
        case PIX_FMT_BGR32_1:
            c->chrToYV12 = rgb321ToUV_half_c;
            break;
        case PIX_FMT_RGB24:
            c->chrToYV12 = rgb24ToUV_half_c;
            break;
        case PIX_FMT_RGB565LE:
            c->chrToYV12 = rgb16leToUV_half_c;
            break;
        case PIX_FMT_RGB565BE:
            c->chrToYV12 = rgb16beToUV_half_c;
            break;
        case PIX_FMT_RGB555LE:
            c->chrToYV12 = rgb15leToUV_half_c;
            break;
        case PIX_FMT_RGB555BE:
            c->chrToYV12 = rgb15beToUV_half_c;
            break;
        case PIX_FMT_RGB444LE:
            c->chrToYV12 = rgb12leToUV_half_c;
            break;
        case PIX_FMT_RGB444BE:
            c->chrToYV12 = rgb12beToUV_half_c;
            break;
        }
    } else {
        switch (srcFormat) {
        case PIX_FMT_RGBA64BE:
            c->chrToYV12 = rgb64BEToUV_c;
            break;
        case PIX_FMT_RGBA64LE:
            c->chrToYV12 = rgb64LEToUV_c;
            break;
        case PIX_FMT_RGB48BE:
            c->chrToYV12 = rgb48BEToUV_c;
            break;
        case PIX_FMT_RGB48LE:
            c->chrToYV12 = rgb48LEToUV_c;
            break;
        case PIX_FMT_BGR48BE:
            c->chrToYV12 = bgr48BEToUV_c;
            break;
        case PIX_FMT_BGR48LE:
            c->chrToYV12 = bgr48LEToUV_c;
            break;
        case PIX_FMT_RGB32:
            c->chrToYV12 = bgr32ToUV_c;
            break;
        case PIX_FMT_RGB32_1:
            c->chrToYV12 = bgr321ToUV_c;
            break;
        case PIX_FMT_BGR24:
            c->chrToYV12 = bgr24ToUV_c;
            break;
        case PIX_FMT_BGR565LE:
            c->chrToYV12 = bgr16leToUV_c;
            break;
        case PIX_FMT_BGR565BE:
            c->chrToYV12 = bgr16beToUV_c;
            break;
        case PIX_FMT_BGR555LE:
            c->chrToYV12 = bgr15leToUV_c;
            break;
        case PIX_FMT_BGR555BE:
            c->chrToYV12 = bgr15beToUV_c;
            break;
        case PIX_FMT_BGR444LE:
            c->chrToYV12 = bgr12leToUV_c;
            break;
        case PIX_FMT_BGR444BE:
            c->chrToYV12 = bgr12beToUV_c;
            break;
        case PIX_FMT_BGR32:
            c->chrToYV12 = rgb32ToUV_c;
            break;
        case PIX_FMT_BGR32_1:
            c->chrToYV12 = rgb321ToUV_c;
            break;
        case PIX_FMT_RGB24:
            c->chrToYV12 = rgb24ToUV_c;
            break;
        case PIX_FMT_RGB565LE:
            c->chrToYV12 = rgb16leToUV_c;
            break;
        case PIX_FMT_RGB565BE:
            c->chrToYV12 = rgb16beToUV_c;
            break;
        case PIX_FMT_RGB555LE:
            c->chrToYV12 = rgb15leToUV_c;
            break;
        case PIX_FMT_RGB555BE:
            c->chrToYV12 = rgb15beToUV_c;
            break;
        case PIX_FMT_RGB444LE:
            c->chrToYV12 = rgb12leToUV_c;
            break;
        case PIX_FMT_RGB444BE:
            c->chrToYV12 = rgb12beToUV_c;
            break;
        }
    }

    c->lumToYV12 = NULL;
    c->alpToYV12 = NULL;
    switch (srcFormat) {
    case PIX_FMT_GBRP9LE:
        c->readLumPlanar = planar_rgb9le_to_y;
        break;
    case PIX_FMT_GBRP10LE:
        c->readLumPlanar = planar_rgb10le_to_y;
        break;
    case PIX_FMT_GBRP12LE:
        c->readLumPlanar = planar_rgb12le_to_y;
        break;
    case PIX_FMT_GBRP14LE:
        c->readLumPlanar = planar_rgb14le_to_y;
        break;
    case PIX_FMT_GBRP16LE:
        c->readLumPlanar = planar_rgb16le_to_y;
        break;
    case PIX_FMT_GBRP9BE:
        c->readLumPlanar = planar_rgb9be_to_y;
        break;
    case PIX_FMT_GBRP10BE:
        c->readLumPlanar = planar_rgb10be_to_y;
        break;
    case PIX_FMT_GBRP12BE:
        c->readLumPlanar = planar_rgb12be_to_y;
        break;
    case PIX_FMT_GBRP14BE:
        c->readLumPlanar = planar_rgb14be_to_y;
        break;
    case PIX_FMT_GBRP16BE:
        c->readLumPlanar = planar_rgb16be_to_y;
        break;
    case PIX_FMT_GBRP:
        c->readLumPlanar = planar_rgb_to_y;
        break;
#if HAVE_BIGENDIAN
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV422P9LE:
    case PIX_FMT_YUV420P9LE:
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV420P10LE:
    case PIX_FMT_YUV444P12LE:
    case PIX_FMT_YUV422P12LE:
    case PIX_FMT_YUV420P12LE:
    case PIX_FMT_YUV444P14LE:
    case PIX_FMT_YUV422P14LE:
    case PIX_FMT_YUV420P14LE:
    case PIX_FMT_YUV420P16LE:
    case PIX_FMT_YUV422P16LE:
    case PIX_FMT_YUV444P16LE:
    case PIX_FMT_GRAY16LE:
        c->lumToYV12 = bswap16Y_c;
        break;
#else
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV422P9BE:
    case PIX_FMT_YUV420P9BE:
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV420P10BE:
    case PIX_FMT_YUV444P12BE:
    case PIX_FMT_YUV422P12BE:
    case PIX_FMT_YUV420P12BE:
    case PIX_FMT_YUV444P14BE:
    case PIX_FMT_YUV422P14BE:
    case PIX_FMT_YUV420P14BE:
    case PIX_FMT_YUV420P16BE:
    case PIX_FMT_YUV422P16BE:
    case PIX_FMT_YUV444P16BE:
    case PIX_FMT_GRAY16BE:
        c->lumToYV12 = bswap16Y_c;
        break;
#endif
    case PIX_FMT_YUYV422:
    case PIX_FMT_Y400A:
        c->lumToYV12 = yuy2ToY_c;
        break;
    case PIX_FMT_UYVY422:
        c->lumToYV12 = uyvyToY_c;
        break;
    case PIX_FMT_BGR24:
        c->lumToYV12 = bgr24ToY_c;
        break;
    case PIX_FMT_BGR565LE:
        c->lumToYV12 = bgr16leToY_c;
        break;
    case PIX_FMT_BGR565BE:
        c->lumToYV12 = bgr16beToY_c;
        break;
    case PIX_FMT_BGR555LE:
        c->lumToYV12 = bgr15leToY_c;
        break;
    case PIX_FMT_BGR555BE:
        c->lumToYV12 = bgr15beToY_c;
        break;
    case PIX_FMT_BGR444LE:
        c->lumToYV12 = bgr12leToY_c;
        break;
    case PIX_FMT_BGR444BE:
        c->lumToYV12 = bgr12beToY_c;
        break;
    case PIX_FMT_RGB24:
        c->lumToYV12 = rgb24ToY_c;
        break;
    case PIX_FMT_RGB565LE:
        c->lumToYV12 = rgb16leToY_c;
        break;
    case PIX_FMT_RGB565BE:
        c->lumToYV12 = rgb16beToY_c;
        break;
    case PIX_FMT_RGB555LE:
        c->lumToYV12 = rgb15leToY_c;
        break;
    case PIX_FMT_RGB555BE:
        c->lumToYV12 = rgb15beToY_c;
        break;
    case PIX_FMT_RGB444LE:
        c->lumToYV12 = rgb12leToY_c;
        break;
    case PIX_FMT_RGB444BE:
        c->lumToYV12 = rgb12beToY_c;
        break;
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:
    case PIX_FMT_PAL8:
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_RGB4_BYTE:
        c->lumToYV12 = palToY_c;
        break;
    case PIX_FMT_MONOBLACK:
        c->lumToYV12 = monoblack2Y_c;
        break;
    case PIX_FMT_MONOWHITE:
        c->lumToYV12 = monowhite2Y_c;
        break;
    case PIX_FMT_RGB32:
        c->lumToYV12 = bgr32ToY_c;
        break;
    case PIX_FMT_RGB32_1:
        c->lumToYV12 = bgr321ToY_c;
        break;
    case PIX_FMT_BGR32:
        c->lumToYV12 = rgb32ToY_c;
        break;
    case PIX_FMT_BGR32_1:
        c->lumToYV12 = rgb321ToY_c;
        break;
    case PIX_FMT_RGB48BE:
        c->lumToYV12 = rgb48BEToY_c;
        break;
    case PIX_FMT_RGB48LE:
        c->lumToYV12 = rgb48LEToY_c;
        break;
    case PIX_FMT_BGR48BE:
        c->lumToYV12 = bgr48BEToY_c;
        break;
    case PIX_FMT_BGR48LE:
        c->lumToYV12 = bgr48LEToY_c;
        break;
    case PIX_FMT_RGBA64BE:
        c->lumToYV12 = rgb64BEToY_c;
        break;
    case PIX_FMT_RGBA64LE:
        c->lumToYV12 = rgb64LEToY_c;
        break;
    }
    if (c->alpPixBuf) {
        switch (srcFormat) {
        case PIX_FMT_RGBA64LE:
        case PIX_FMT_RGBA64BE:  c->alpToYV12 = rgba64ToA_c; break;
        case PIX_FMT_BGRA:
        case PIX_FMT_RGBA:
            c->alpToYV12 = rgbaToA_c;
            break;
        case PIX_FMT_ABGR:
        case PIX_FMT_ARGB:
            c->alpToYV12 = abgrToA_c;
            break;
        case PIX_FMT_Y400A:
            c->alpToYV12 = uyvyToY_c;
            break;
        case PIX_FMT_PAL8 :
            c->alpToYV12 = palToA_c;
            break;
        }
    }
}
