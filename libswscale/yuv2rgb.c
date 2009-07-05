/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2009 Konstantin Shishkov
 *
 * MMX/MMX2 template stuff (needed for fast movntq support),
 * 1,4,8bpp support and context / deglobalize stuff
 * by Michael Niedermayer (michaelni@gmx.at)
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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

#define DITHER1XBPP // only for MMX

extern const uint8_t dither_8x8_32[8][8];
extern const uint8_t dither_8x8_73[8][8];
extern const uint8_t dither_8x8_220[8][8];

#if HAVE_MMX && CONFIG_GPL

/* hope these constant values are cache line aligned */
DECLARE_ASM_CONST(8, uint64_t, mmx_00ffw)   = 0x00ff00ff00ff00ffULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_redmask) = 0xf8f8f8f8f8f8f8f8ULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_grnmask) = 0xfcfcfcfcfcfcfcfcULL;

//MMX versions
#undef RENAME
#undef HAVE_MMX2
#undef HAVE_AMD3DNOW
#define HAVE_MMX2 0
#define HAVE_AMD3DNOW 0
#define RENAME(a) a ## _MMX
#include "yuv2rgb_template.c"

//MMX2 versions
#undef RENAME
#undef HAVE_MMX2
#define HAVE_MMX2 1
#define RENAME(a) a ## _MMX2
#include "yuv2rgb_template.c"

#endif /* HAVE_MMX && CONFIG_GPL */

const int32_t ff_yuv2rgb_coeffs[8][4] = {
    {117504, 138453, 13954, 34903}, /* no sequence_display_extension */
    {117504, 138453, 13954, 34903}, /* ITU-R Rec. 709 (1990) */
    {104597, 132201, 25675, 53279}, /* unspecified */
    {104597, 132201, 25675, 53279}, /* reserved */
    {104448, 132798, 24759, 53109}, /* FCC */
    {104597, 132201, 25675, 53279}, /* ITU-R Rec. 624-4 System B, G */
    {104597, 132201, 25675, 53279}, /* SMPTE 170M */
    {117579, 136230, 16907, 35559}  /* SMPTE 240M (1987) */
};

#define LOADCHROMA(i)                               \
    U = pu[i];                                      \
    V = pv[i];                                      \
    r = (void *)c->table_rV[V];                     \
    g = (void *)(c->table_gU[U] + c->table_gV[V]);  \
    b = (void *)c->table_bU[U];

#define PUTRGB(dst,src,i,o)          \
    Y = src[2*i+o];                  \
    dst[2*i  ] = r[Y] + g[Y] + b[Y]; \
    Y = src[2*i+1-o];                \
    dst[2*i+1] = r[Y] + g[Y] + b[Y];

#define PUTRGB24(dst,src,i)                                  \
    Y = src[2*i];                                            \
    dst[6*i+0] = r[Y]; dst[6*i+1] = g[Y]; dst[6*i+2] = b[Y]; \
    Y = src[2*i+1];                                          \
    dst[6*i+3] = r[Y]; dst[6*i+4] = g[Y]; dst[6*i+5] = b[Y];

#define PUTBGR24(dst,src,i)                                  \
    Y = src[2*i];                                            \
    dst[6*i+0] = b[Y]; dst[6*i+1] = g[Y]; dst[6*i+2] = r[Y]; \
    Y = src[2*i+1];                                          \
    dst[6*i+3] = b[Y]; dst[6*i+4] = g[Y]; dst[6*i+5] = r[Y];

#define YUV2RGBFUNC(func_name, dst_type) \
static int func_name(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY, \
                     int srcSliceH, uint8_t* dst[], int dstStride[]){\
    int y;\
\
    if (c->srcFormat == PIX_FMT_YUV422P) {\
        srcStride[1] *= 2;\
        srcStride[2] *= 2;\
    }\
    for (y=0; y<srcSliceH; y+=2) {\
        dst_type *dst_1 = (dst_type*)(dst[0] + (y+srcSliceY  )*dstStride[0]);\
        dst_type *dst_2 = (dst_type*)(dst[0] + (y+srcSliceY+1)*dstStride[0]);\
        dst_type av_unused *r, *b;\
        dst_type *g;\
        uint8_t *py_1 = src[0] + y*srcStride[0];\
        uint8_t *py_2 = py_1 + srcStride[0];\
        uint8_t *pu = src[1] + (y>>1)*srcStride[1];\
        uint8_t *pv = src[2] + (y>>1)*srcStride[2];\
        unsigned int h_size = c->dstW>>3;\
        while (h_size--) {\
            int av_unused U, V;\
            int Y;\

#define ENDYUV2RGBLINE(dst_delta)\
            pu += 4;\
            pv += 4;\
            py_1 += 8;\
            py_2 += 8;\
            dst_1 += dst_delta;\
            dst_2 += dst_delta;\
        }\
        if (c->dstW & 4) {\
            int av_unused Y, U, V;\

#define ENDYUV2RGBFUNC()\
        }\
    }\
    return srcSliceH;\
}

#define CLOSEYUV2RGBFUNC(dst_delta)\
    ENDYUV2RGBLINE(dst_delta)\
    ENDYUV2RGBFUNC()

YUV2RGBFUNC(yuv2rgb_c_32, uint32_t)
    LOADCHROMA(0);
    PUTRGB(dst_1,py_1,0,0);
    PUTRGB(dst_2,py_2,0,1);

    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);
    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);

    LOADCHROMA(2);
    PUTRGB(dst_1,py_1,2,0);
    PUTRGB(dst_2,py_2,2,1);

    LOADCHROMA(3);
    PUTRGB(dst_2,py_2,3,1);
    PUTRGB(dst_1,py_1,3,0);
ENDYUV2RGBLINE(8)
    LOADCHROMA(0);
    PUTRGB(dst_1,py_1,0,0);
    PUTRGB(dst_2,py_2,0,1);

    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);
ENDYUV2RGBFUNC()

YUV2RGBFUNC(yuv2rgb_c_24_rgb, uint8_t)
    LOADCHROMA(0);
    PUTRGB24(dst_1,py_1,0);
    PUTRGB24(dst_2,py_2,0);

    LOADCHROMA(1);
    PUTRGB24(dst_2,py_2,1);
    PUTRGB24(dst_1,py_1,1);

    LOADCHROMA(2);
    PUTRGB24(dst_1,py_1,2);
    PUTRGB24(dst_2,py_2,2);

    LOADCHROMA(3);
    PUTRGB24(dst_2,py_2,3);
    PUTRGB24(dst_1,py_1,3);
ENDYUV2RGBLINE(24)
    LOADCHROMA(0);
    PUTRGB24(dst_1,py_1,0);
    PUTRGB24(dst_2,py_2,0);

    LOADCHROMA(1);
    PUTRGB24(dst_2,py_2,1);
    PUTRGB24(dst_1,py_1,1);
ENDYUV2RGBFUNC()

// only trivial mods from yuv2rgb_c_24_rgb
YUV2RGBFUNC(yuv2rgb_c_24_bgr, uint8_t)
    LOADCHROMA(0);
    PUTBGR24(dst_1,py_1,0);
    PUTBGR24(dst_2,py_2,0);

    LOADCHROMA(1);
    PUTBGR24(dst_2,py_2,1);
    PUTBGR24(dst_1,py_1,1);

    LOADCHROMA(2);
    PUTBGR24(dst_1,py_1,2);
    PUTBGR24(dst_2,py_2,2);

    LOADCHROMA(3);
    PUTBGR24(dst_2,py_2,3);
    PUTBGR24(dst_1,py_1,3);
ENDYUV2RGBLINE(24)
    LOADCHROMA(0);
    PUTBGR24(dst_1,py_1,0);
    PUTBGR24(dst_2,py_2,0);

    LOADCHROMA(1);
    PUTBGR24(dst_2,py_2,1);
    PUTBGR24(dst_1,py_1,1);
ENDYUV2RGBFUNC()

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
YUV2RGBFUNC(yuv2rgb_c_16, uint16_t)
    LOADCHROMA(0);
    PUTRGB(dst_1,py_1,0,0);
    PUTRGB(dst_2,py_2,0,1);

    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);

    LOADCHROMA(2);
    PUTRGB(dst_1,py_1,2,0);
    PUTRGB(dst_2,py_2,2,1);

    LOADCHROMA(3);
    PUTRGB(dst_2,py_2,3,1);
    PUTRGB(dst_1,py_1,3,0);
CLOSEYUV2RGBFUNC(8)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
YUV2RGBFUNC(yuv2rgb_c_8, uint8_t)
    LOADCHROMA(0);
    PUTRGB(dst_1,py_1,0,0);
    PUTRGB(dst_2,py_2,0,1);

    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);

    LOADCHROMA(2);
    PUTRGB(dst_1,py_1,2,0);
    PUTRGB(dst_2,py_2,2,1);

    LOADCHROMA(3);
    PUTRGB(dst_2,py_2,3,1);
    PUTRGB(dst_1,py_1,3,0);
CLOSEYUV2RGBFUNC(8)

// r, g, b, dst_1, dst_2
YUV2RGBFUNC(yuv2rgb_c_8_ordered_dither, uint8_t)
    const uint8_t *d32 = dither_8x8_32[y&7];
    const uint8_t *d64 = dither_8x8_73[y&7];
#define PUTRGB8(dst,src,i,o)                                    \
    Y = src[2*i];                                               \
    dst[2*i]   = r[Y+d32[0+o]] + g[Y+d32[0+o]] + b[Y+d64[0+o]]; \
    Y = src[2*i+1];                                             \
    dst[2*i+1] = r[Y+d32[1+o]] + g[Y+d32[1+o]] + b[Y+d64[1+o]];

    LOADCHROMA(0);
    PUTRGB8(dst_1,py_1,0,0);
    PUTRGB8(dst_2,py_2,0,0+8);

    LOADCHROMA(1);
    PUTRGB8(dst_2,py_2,1,2+8);
    PUTRGB8(dst_1,py_1,1,2);

    LOADCHROMA(2);
    PUTRGB8(dst_1,py_1,2,4);
    PUTRGB8(dst_2,py_2,2,4+8);

    LOADCHROMA(3);
    PUTRGB8(dst_2,py_2,3,6+8);
    PUTRGB8(dst_1,py_1,3,6);
CLOSEYUV2RGBFUNC(8)


// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
YUV2RGBFUNC(yuv2rgb_c_4, uint8_t)
    int acc;
#define PUTRGB4(dst,src,i)          \
    Y = src[2*i];                   \
    acc = r[Y] + g[Y] + b[Y];       \
    Y = src[2*i+1];                 \
    acc |= (r[Y] + g[Y] + b[Y])<<4; \
    dst[i] = acc;

    LOADCHROMA(0);
    PUTRGB4(dst_1,py_1,0);
    PUTRGB4(dst_2,py_2,0);

    LOADCHROMA(1);
    PUTRGB4(dst_2,py_2,1);
    PUTRGB4(dst_1,py_1,1);

    LOADCHROMA(2);
    PUTRGB4(dst_1,py_1,2);
    PUTRGB4(dst_2,py_2,2);

    LOADCHROMA(3);
    PUTRGB4(dst_2,py_2,3);
    PUTRGB4(dst_1,py_1,3);
CLOSEYUV2RGBFUNC(4)

YUV2RGBFUNC(yuv2rgb_c_4_ordered_dither, uint8_t)
    const uint8_t *d64 =  dither_8x8_73[y&7];
    const uint8_t *d128 = dither_8x8_220[y&7];
    int acc;

#define PUTRGB4D(dst,src,i,o)                                     \
    Y = src[2*i];                                                 \
    acc = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]];        \
    Y = src[2*i+1];                                               \
    acc |= (r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]])<<4;  \
    dst[i]= acc;

    LOADCHROMA(0);
    PUTRGB4D(dst_1,py_1,0,0);
    PUTRGB4D(dst_2,py_2,0,0+8);

    LOADCHROMA(1);
    PUTRGB4D(dst_2,py_2,1,2+8);
    PUTRGB4D(dst_1,py_1,1,2);

    LOADCHROMA(2);
    PUTRGB4D(dst_1,py_1,2,4);
    PUTRGB4D(dst_2,py_2,2,4+8);

    LOADCHROMA(3);
    PUTRGB4D(dst_2,py_2,3,6+8);
    PUTRGB4D(dst_1,py_1,3,6);
CLOSEYUV2RGBFUNC(4)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
YUV2RGBFUNC(yuv2rgb_c_4b, uint8_t)
    LOADCHROMA(0);
    PUTRGB(dst_1,py_1,0,0);
    PUTRGB(dst_2,py_2,0,1);

    LOADCHROMA(1);
    PUTRGB(dst_2,py_2,1,1);
    PUTRGB(dst_1,py_1,1,0);

    LOADCHROMA(2);
    PUTRGB(dst_1,py_1,2,0);
    PUTRGB(dst_2,py_2,2,1);

    LOADCHROMA(3);
    PUTRGB(dst_2,py_2,3,1);
    PUTRGB(dst_1,py_1,3,0);
CLOSEYUV2RGBFUNC(8)

YUV2RGBFUNC(yuv2rgb_c_4b_ordered_dither, uint8_t)
    const uint8_t *d64 =  dither_8x8_73[y&7];
    const uint8_t *d128 = dither_8x8_220[y&7];

#define PUTRGB4DB(dst,src,i,o)                                    \
    Y = src[2*i];                                                 \
    dst[2*i]   = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]]; \
    Y = src[2*i+1];                                               \
    dst[2*i+1] = r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]];

    LOADCHROMA(0);
    PUTRGB4DB(dst_1,py_1,0,0);
    PUTRGB4DB(dst_2,py_2,0,0+8);

    LOADCHROMA(1);
    PUTRGB4DB(dst_2,py_2,1,2+8);
    PUTRGB4DB(dst_1,py_1,1,2);

    LOADCHROMA(2);
    PUTRGB4DB(dst_1,py_1,2,4);
    PUTRGB4DB(dst_2,py_2,2,4+8);

    LOADCHROMA(3);
    PUTRGB4DB(dst_2,py_2,3,6+8);
    PUTRGB4DB(dst_1,py_1,3,6);
CLOSEYUV2RGBFUNC(8)

YUV2RGBFUNC(yuv2rgb_c_1_ordered_dither, uint8_t)
        const uint8_t *d128 = dither_8x8_220[y&7];
        char out_1 = 0, out_2 = 0;
        g= c->table_gU[128] + c->table_gV[128];

#define PUTRGB1(out,src,i,o)    \
    Y = src[2*i];               \
    out+= out + g[Y+d128[0+o]]; \
    Y = src[2*i+1];             \
    out+= out + g[Y+d128[1+o]];

    PUTRGB1(out_1,py_1,0,0);
    PUTRGB1(out_2,py_2,0,0+8);

    PUTRGB1(out_2,py_2,1,2+8);
    PUTRGB1(out_1,py_1,1,2);

    PUTRGB1(out_1,py_1,2,4);
    PUTRGB1(out_2,py_2,2,4+8);

    PUTRGB1(out_2,py_2,3,6+8);
    PUTRGB1(out_1,py_1,3,6);

    dst_1[0]= out_1;
    dst_2[0]= out_2;
CLOSEYUV2RGBFUNC(1)

SwsFunc sws_yuv2rgb_get_func_ptr(SwsContext *c)
{
    SwsFunc t = NULL;
#if (HAVE_MMX2 || HAVE_MMX) && CONFIG_GPL
    if (c->flags & SWS_CPU_CAPS_MMX2) {
        switch (c->dstFormat) {
        case PIX_FMT_RGB32:  return yuv420_rgb32_MMX2;
        case PIX_FMT_BGR24:  return yuv420_rgb24_MMX2;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX2;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX2;
        }
    }
    if (c->flags & SWS_CPU_CAPS_MMX) {
        switch (c->dstFormat) {
        case PIX_FMT_RGB32:  return yuv420_rgb32_MMX;
        case PIX_FMT_BGR24:  return yuv420_rgb24_MMX;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX;
        }
    }
#endif
#if HAVE_VIS
    t = sws_yuv2rgb_init_vis(c);
#endif
#if CONFIG_MLIB
    t = sws_yuv2rgb_init_mlib(c);
#endif
#if HAVE_ALTIVEC
    if (c->flags & SWS_CPU_CAPS_ALTIVEC)
        t = sws_yuv2rgb_init_altivec(c);
#endif

#if ARCH_BFIN
    if (c->flags & SWS_CPU_CAPS_BFIN)
        t = sws_ff_bfin_yuv2rgb_get_func_ptr(c);
#endif

    if (t)
        return t;

    av_log(c, AV_LOG_WARNING, "No accelerated colorspace conversion found.\n");

    switch (c->dstFormat) {
    case PIX_FMT_BGR32_1:
    case PIX_FMT_RGB32_1:
    case PIX_FMT_BGR32:
    case PIX_FMT_RGB32:      return yuv2rgb_c_32;
    case PIX_FMT_RGB24:      return yuv2rgb_c_24_rgb;
    case PIX_FMT_BGR24:      return yuv2rgb_c_24_bgr;
    case PIX_FMT_RGB565:
    case PIX_FMT_BGR565:
    case PIX_FMT_RGB555:
    case PIX_FMT_BGR555:     return yuv2rgb_c_16;
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:       return yuv2rgb_c_8_ordered_dither;
    case PIX_FMT_RGB4:
    case PIX_FMT_BGR4:       return yuv2rgb_c_4_ordered_dither;
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:  return yuv2rgb_c_4b_ordered_dither;
    case PIX_FMT_MONOBLACK:  return yuv2rgb_c_1_ordered_dither;
    default:
        assert(0);
    }
    return NULL;
}

static void fill_table(uint8_t* table[256], const int elemsize, const int inc, uint8_t *y_table)
{
    int i;
    int64_t cb = 0;

    y_table -= elemsize * (inc >> 9);

    for (i = 0; i < 256; i++) {
        table[i] = y_table + elemsize * (cb >> 16);
        cb += inc;
    }
}

static void fill_gv_table(int table[256], const int elemsize, const int inc)
{
    int i;
    int64_t cb = 0;
    int off = -(inc >> 9);

    for (i = 0; i < 256; i++) {
        table[i] = elemsize * (off + (cb >> 16));
        cb += inc;
    }
}

av_cold int sws_yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4], int fullRange,
                                      int brightness, int contrast, int saturation)
{
    const int isRgb =      c->dstFormat==PIX_FMT_RGB32
                        || c->dstFormat==PIX_FMT_RGB32_1
                        || c->dstFormat==PIX_FMT_BGR24
                        || c->dstFormat==PIX_FMT_RGB565
                        || c->dstFormat==PIX_FMT_RGB555
                        || c->dstFormat==PIX_FMT_RGB8
                        || c->dstFormat==PIX_FMT_RGB4
                        || c->dstFormat==PIX_FMT_RGB4_BYTE
                        || c->dstFormat==PIX_FMT_MONOBLACK;
    const int bpp = fmt_depth(c->dstFormat);
    uint8_t *y_table;
    uint16_t *y_table16;
    uint32_t *y_table32;
    int i, base, rbase, gbase, bbase, abase;
    const int yoffs = fullRange ? 384 : 326;

    int64_t crv =  inv_table[0];
    int64_t cbu =  inv_table[1];
    int64_t cgu = -inv_table[2];
    int64_t cgv = -inv_table[3];
    int64_t cy  = 1<<16;
    int64_t oy  = 0;

    int64_t yb = 0;

    if (!fullRange) {
        cy = (cy*255) / 219;
        oy = 16<<16;
    } else {
        crv = (crv*224) / 255;
        cbu = (cbu*224) / 255;
        cgu = (cgu*224) / 255;
        cgv = (cgv*224) / 255;
    }

    cy  = (cy *contrast             ) >> 16;
    crv = (crv*contrast * saturation) >> 32;
    cbu = (cbu*contrast * saturation) >> 32;
    cgu = (cgu*contrast * saturation) >> 32;
    cgv = (cgv*contrast * saturation) >> 32;
    oy -= 256*brightness;

    //scale coefficients by cy
    crv = ((crv << 16) + 0x8000) / cy;
    cbu = ((cbu << 16) + 0x8000) / cy;
    cgu = ((cgu << 16) + 0x8000) / cy;
    cgv = ((cgv << 16) + 0x8000) / cy;

    av_free(c->yuvTable);

    switch (bpp) {
    case 1:
        c->yuvTable = av_malloc(1024);
        y_table = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024-110; i++) {
            y_table[i+110] = av_clip_uint8((yb + 0x8000) >> 16) >> 7;
            yb += cy;
        }
        fill_table(c->table_gU, 1, cgu, y_table + yoffs);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 4:
    case 4|128:
        rbase = isRgb ? 3 : 0;
        gbase = 1;
        bbase = isRgb ? 0 : 3;
        c->yuvTable = av_malloc(1024*3);
        y_table = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024-110; i++) {
            int yval = av_clip_uint8((yb + 0x8000) >> 16);
            y_table[i+110     ] =  (yval >> 7)       << rbase;
            y_table[i+ 37+1024] = ((yval + 43) / 85) << gbase;
            y_table[i+110+2048] =  (yval >> 7)       << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs + 1024);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs + 2048);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 8:
        rbase = isRgb ? 5 : 0;
        gbase = isRgb ? 2 : 3;
        bbase = isRgb ? 0 : 6;
        c->yuvTable = av_malloc(1024*3);
        y_table = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024-38; i++) {
            int yval = av_clip_uint8((yb + 0x8000) >> 16);
            y_table[i+16     ] = ((yval + 18) / 36) << rbase;
            y_table[i+16+1024] = ((yval + 18) / 36) << gbase;
            y_table[i+37+2048] = ((yval + 43) / 85) << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs + 1024);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs + 2048);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 15:
    case 16:
        rbase = isRgb ? bpp - 5 : 0;
        gbase = 5;
        bbase = isRgb ? 0 : (bpp - 5);
        c->yuvTable = av_malloc(1024*3*2);
        y_table16 = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024; i++) {
            uint8_t yval = av_clip_uint8((yb + 0x8000) >> 16);
            y_table16[i     ] = (yval >> 3)          << rbase;
            y_table16[i+1024] = (yval >> (18 - bpp)) << gbase;
            y_table16[i+2048] = (yval >> 3)          << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 2, crv, y_table16 + yoffs);
        fill_table(c->table_gU, 2, cgu, y_table16 + yoffs + 1024);
        fill_table(c->table_bU, 2, cbu, y_table16 + yoffs + 2048);
        fill_gv_table(c->table_gV, 2, cgv);
        break;
    case 24:
        c->yuvTable = av_malloc(1024);
        y_table = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024; i++) {
            y_table[i] = av_clip_uint8((yb + 0x8000) >> 16);
            yb += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 32:
        base = (c->dstFormat == PIX_FMT_RGB32_1 || c->dstFormat == PIX_FMT_BGR32_1) ? 8 : 0;
        rbase = base + (isRgb ? 16 : 0);
        gbase = base + 8;
        bbase = base + (isRgb ? 0 : 16);
        abase = (base + 24) & 31;
        c->yuvTable = av_malloc(1024*3*4);
        y_table32 = c->yuvTable;
        yb = -(384<<16) - oy;
        for (i = 0; i < 1024; i++) {
            uint8_t yval = av_clip_uint8((yb + 0x8000) >> 16);
            y_table32[i     ] = (yval << rbase) + (255 << abase);
            y_table32[i+1024] = yval << gbase;
            y_table32[i+2048] = yval << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 4, crv, y_table32 + yoffs);
        fill_table(c->table_gU, 4, cgu, y_table32 + yoffs + 1024);
        fill_table(c->table_bU, 4, cbu, y_table32 + yoffs + 2048);
        fill_gv_table(c->table_gV, 4, cgv);
        break;
    default:
        c->yuvTable = NULL;
        av_log(c, AV_LOG_ERROR, "%ibpp not supported by yuv2rgb\n", bpp);
        return -1;
    }
    return 0;
}
