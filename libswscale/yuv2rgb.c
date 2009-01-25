/*
 * yuv2rgb.c, Software YUV to RGB converter
 *
 *  Copyright (C) 1999, Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 *  Functions broken out from display_x11.c and several new modes
 *  added by Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 *  15 & 16 bpp support by Franck Sicard <Franck.Sicard@solsoft.fr>
 *
 *  MMX/MMX2 template stuff (needed for fast movntq support),
 *  1,4,8bpp support and context / deglobalize stuff
 *  by Michael Niedermayer (michaelni@gmx.at)
 *
 *  This file is part of mpeg2dec, a free MPEG-2 video decoder
 *
 *  mpeg2dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  mpeg2dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with mpeg2dec; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

#if HAVE_MMX

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

#endif /* HAVE_MMX */

const int32_t Inverse_Table_6_9[8][4] = {
    {117504, 138453, 13954, 34903}, /* no sequence_display_extension */
    {117504, 138453, 13954, 34903}, /* ITU-R Rec. 709 (1990) */
    {104597, 132201, 25675, 53279}, /* unspecified */
    {104597, 132201, 25675, 53279}, /* reserved */
    {104448, 132798, 24759, 53109}, /* FCC */
    {104597, 132201, 25675, 53279}, /* ITU-R Rec. 624-4 System B, G */
    {104597, 132201, 25675, 53279}, /* SMPTE 170M */
    {117579, 136230, 16907, 35559}  /* SMPTE 240M (1987) */
};

#define RGB(i)                                      \
    U = pu[i];                                      \
    V = pv[i];                                      \
    r = (void *)c->table_rV[V];                     \
    g = (void *)(c->table_gU[U] + c->table_gV[V]);  \
    b = (void *)c->table_bU[U];

#define DST1(i)                         \
    Y = py_1[2*i];                      \
    dst_1[2*i] = r[Y] + g[Y] + b[Y];    \
    Y = py_1[2*i+1];                    \
    dst_1[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST2(i)                         \
    Y = py_2[2*i];                      \
    dst_2[2*i] = r[Y] + g[Y] + b[Y];    \
    Y = py_2[2*i+1];                    \
    dst_2[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST1RGB(i)                                                \
    Y = py_1[2*i];                                                \
    dst_1[6*i] = r[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = b[Y];  \
    Y = py_1[2*i+1];                                              \
    dst_1[6*i+3] = r[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = b[Y];

#define DST2RGB(i)                                                \
    Y = py_2[2*i];                                                \
    dst_2[6*i] = r[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = b[Y];  \
    Y = py_2[2*i+1];                                              \
    dst_2[6*i+3] = r[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = b[Y];

#define DST1BGR(i)                                                \
    Y = py_1[2*i];                                                \
    dst_1[6*i] = b[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = r[Y];  \
    Y = py_1[2*i+1];                                              \
    dst_1[6*i+3] = b[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = r[Y];

#define DST2BGR(i)                                                \
    Y = py_2[2*i];                                                \
    dst_2[6*i] = b[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = r[Y];  \
    Y = py_2[2*i+1];                                              \
    dst_2[6*i+3] = b[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = r[Y];

#define PROLOG(func_name, dst_type) \
static int func_name(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY, \
                     int srcSliceH, uint8_t* dst[], int dstStride[]){\
    int y;\
\
    if (c->srcFormat == PIX_FMT_YUV422P){\
        srcStride[1] *= 2;\
        srcStride[2] *= 2;\
    }\
    for (y=0; y<srcSliceH; y+=2){\
        dst_type *dst_1= (dst_type*)(dst[0] + (y+srcSliceY  )*dstStride[0]);\
        dst_type *dst_2= (dst_type*)(dst[0] + (y+srcSliceY+1)*dstStride[0]);\
        dst_type av_unused *r, *b;\
        dst_type *g;\
        uint8_t *py_1= src[0] + y*srcStride[0];\
        uint8_t *py_2= py_1 + srcStride[0];\
        uint8_t *pu= src[1] + (y>>1)*srcStride[1];\
        uint8_t *pv= src[2] + (y>>1)*srcStride[2];\
        unsigned int h_size= c->dstW>>3;\
        while (h_size--) {\
            int av_unused U, V;\
            int Y;\

#define EPILOG1(dst_delta)\
            pu += 4;\
            pv += 4;\
            py_1 += 8;\
            py_2 += 8;\
            dst_1 += dst_delta;\
            dst_2 += dst_delta;\
        }\
        if (c->dstW & 4) {\
            int av_unused Y, U, V;\

#define EPILOG2()\
        }\
    }\
    return srcSliceH;\
}

#define EPILOG(dst_delta)\
    EPILOG1(dst_delta)\
    EPILOG2()

PROLOG(yuv2rgb_c_32, uint32_t)
    RGB(0);
    DST1(0);
    DST2(0);

    RGB(1);
    DST2(1);
    DST1(1);

    RGB(2);
    DST1(2);
    DST2(2);

    RGB(3);
    DST2(3);
    DST1(3);
EPILOG1(8)
    RGB(0);
    DST1(0);
    DST2(0);

    RGB(1);
    DST2(1);
    DST1(1);
EPILOG2()

PROLOG(yuv2rgb_c_24_rgb, uint8_t)
    RGB(0);
    DST1RGB(0);
    DST2RGB(0);

    RGB(1);
    DST2RGB(1);
    DST1RGB(1);

    RGB(2);
    DST1RGB(2);
    DST2RGB(2);

    RGB(3);
    DST2RGB(3);
    DST1RGB(3);
EPILOG1(24)
    RGB(0);
    DST1RGB(0);
    DST2RGB(0);

    RGB(1);
    DST2RGB(1);
    DST1RGB(1);
EPILOG2()

// only trivial mods from yuv2rgb_c_24_rgb
PROLOG(yuv2rgb_c_24_bgr, uint8_t)
    RGB(0);
    DST1BGR(0);
    DST2BGR(0);

    RGB(1);
    DST2BGR(1);
    DST1BGR(1);

    RGB(2);
    DST1BGR(2);
    DST2BGR(2);

    RGB(3);
    DST2BGR(3);
    DST1BGR(3);
EPILOG1(24)
    RGB(0);
    DST1BGR(0);
    DST2BGR(0);

    RGB(1);
    DST2BGR(1);
    DST1BGR(1);
EPILOG2()

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_16, uint16_t)
    RGB(0);
    DST1(0);
    DST2(0);

    RGB(1);
    DST2(1);
    DST1(1);

    RGB(2);
    DST1(2);
    DST2(2);

    RGB(3);
    DST2(3);
    DST1(3);
EPILOG(8)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_8, uint8_t)
    RGB(0);
    DST1(0);
    DST2(0);

    RGB(1);
    DST2(1);
    DST1(1);

    RGB(2);
    DST1(2);
    DST2(2);

    RGB(3);
    DST2(3);
    DST1(3);
EPILOG(8)

// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_8_ordered_dither, uint8_t)
    const uint8_t *d32= dither_8x8_32[y&7];
    const uint8_t *d64= dither_8x8_73[y&7];
#define DST1bpp8(i,o)                                               \
    Y = py_1[2*i];                                                  \
    dst_1[2*i]   = r[Y+d32[0+o]] + g[Y+d32[0+o]] + b[Y+d64[0+o]];   \
    Y = py_1[2*i+1];                                                \
    dst_1[2*i+1] = r[Y+d32[1+o]] + g[Y+d32[1+o]] + b[Y+d64[1+o]];

#define DST2bpp8(i,o)                                               \
    Y = py_2[2*i];                                                  \
    dst_2[2*i]   =  r[Y+d32[8+o]] + g[Y+d32[8+o]] + b[Y+d64[8+o]];  \
    Y = py_2[2*i+1];                                                \
    dst_2[2*i+1] =  r[Y+d32[9+o]] + g[Y+d32[9+o]] + b[Y+d64[9+o]];


    RGB(0);
    DST1bpp8(0,0);
    DST2bpp8(0,0);

    RGB(1);
    DST2bpp8(1,2);
    DST1bpp8(1,2);

    RGB(2);
    DST1bpp8(2,4);
    DST2bpp8(2,4);

    RGB(3);
    DST2bpp8(3,6);
    DST1bpp8(3,6);
EPILOG(8)


// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_4, uint8_t)
    int acc;
#define DST1_4(i)                   \
    Y = py_1[2*i];                  \
    acc = r[Y] + g[Y] + b[Y];       \
    Y = py_1[2*i+1];                \
    acc |= (r[Y] + g[Y] + b[Y])<<4; \
    dst_1[i] = acc;

#define DST2_4(i)                   \
    Y = py_2[2*i];                  \
    acc = r[Y] + g[Y] + b[Y];       \
    Y = py_2[2*i+1];                \
    acc |= (r[Y] + g[Y] + b[Y])<<4; \
    dst_2[i] = acc;

    RGB(0);
    DST1_4(0);
    DST2_4(0);

    RGB(1);
    DST2_4(1);
    DST1_4(1);

    RGB(2);
    DST1_4(2);
    DST2_4(2);

    RGB(3);
    DST2_4(3);
    DST1_4(3);
EPILOG(4)

PROLOG(yuv2rgb_c_4_ordered_dither, uint8_t)
    const uint8_t *d64= dither_8x8_73[y&7];
    const uint8_t *d128=dither_8x8_220[y&7];
    int acc;

#define DST1bpp4(i,o)                                             \
    Y = py_1[2*i];                                                \
    acc = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]];        \
    Y = py_1[2*i+1];                                              \
    acc |= (r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]])<<4;  \
    dst_1[i]= acc;

#define DST2bpp4(i,o)                                             \
    Y = py_2[2*i];                                                \
    acc =  r[Y+d128[8+o]] + g[Y+d64[8+o]] + b[Y+d128[8+o]];       \
    Y = py_2[2*i+1];                                              \
    acc |=  (r[Y+d128[9+o]] + g[Y+d64[9+o]] + b[Y+d128[9+o]])<<4; \
    dst_2[i]= acc;


    RGB(0);
    DST1bpp4(0,0);
    DST2bpp4(0,0);

    RGB(1);
    DST2bpp4(1,2);
    DST1bpp4(1,2);

    RGB(2);
    DST1bpp4(2,4);
    DST2bpp4(2,4);

    RGB(3);
    DST2bpp4(3,6);
    DST1bpp4(3,6);
EPILOG(4)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_4b, uint8_t)
    RGB(0);
    DST1(0);
    DST2(0);

    RGB(1);
    DST2(1);
    DST1(1);

    RGB(2);
    DST1(2);
    DST2(2);

    RGB(3);
    DST2(3);
    DST1(3);
EPILOG(8)

PROLOG(yuv2rgb_c_4b_ordered_dither, uint8_t)
    const uint8_t *d64= dither_8x8_73[y&7];
    const uint8_t *d128=dither_8x8_220[y&7];

#define DST1bpp4b(i,o)                                                \
    Y = py_1[2*i];                                                    \
    dst_1[2*i]   = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]];   \
    Y = py_1[2*i+1];                                                  \
    dst_1[2*i+1] = r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]];

#define DST2bpp4b(i,o)                                                \
    Y = py_2[2*i];                                                    \
    dst_2[2*i]   =  r[Y+d128[8+o]] + g[Y+d64[8+o]] + b[Y+d128[8+o]];  \
    Y = py_2[2*i+1];                                                  \
    dst_2[2*i+1] =  r[Y+d128[9+o]] + g[Y+d64[9+o]] + b[Y+d128[9+o]];


    RGB(0);
    DST1bpp4b(0,0);
    DST2bpp4b(0,0);

    RGB(1);
    DST2bpp4b(1,2);
    DST1bpp4b(1,2);

    RGB(2);
    DST1bpp4b(2,4);
    DST2bpp4b(2,4);

    RGB(3);
    DST2bpp4b(3,6);
    DST1bpp4b(3,6);
EPILOG(8)

PROLOG(yuv2rgb_c_1_ordered_dither, uint8_t)
        const uint8_t *d128=dither_8x8_220[y&7];
        char out_1=0, out_2=0;
        g= c->table_gU[128] + c->table_gV[128];

#define DST1bpp1(i,o)               \
    Y = py_1[2*i];                  \
    out_1+= out_1 + g[Y+d128[0+o]]; \
    Y = py_1[2*i+1];                \
    out_1+= out_1 + g[Y+d128[1+o]];

#define DST2bpp1(i,o)               \
    Y = py_2[2*i];                  \
    out_2+= out_2 + g[Y+d128[8+o]]; \
    Y = py_2[2*i+1];                \
    out_2+= out_2 + g[Y+d128[9+o]];

    DST1bpp1(0,0);
    DST2bpp1(0,0);

    DST2bpp1(1,2);
    DST1bpp1(1,2);

    DST1bpp1(2,4);
    DST2bpp1(2,4);

    DST2bpp1(3,6);
    DST1bpp1(3,6);

    dst_1[0]= out_1;
    dst_2[0]= out_2;
EPILOG(1)

SwsFunc yuv2rgb_get_func_ptr (SwsContext *c)
{
#if HAVE_MMX2 || HAVE_MMX
    if (c->flags & SWS_CPU_CAPS_MMX2){
        switch(c->dstFormat){
        case PIX_FMT_RGB32:  return yuv420_rgb32_MMX2;
        case PIX_FMT_BGR24:  return yuv420_rgb24_MMX2;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX2;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX2;
        }
    }
    if (c->flags & SWS_CPU_CAPS_MMX){
        switch(c->dstFormat){
        case PIX_FMT_RGB32:  return yuv420_rgb32_MMX;
        case PIX_FMT_BGR24:  return yuv420_rgb24_MMX;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX;
        }
    }
#endif
#if HAVE_VIS
    {
        SwsFunc t= yuv2rgb_init_vis(c);
        if (t) return t;
    }
#endif
#if CONFIG_MLIB
    {
        SwsFunc t= yuv2rgb_init_mlib(c);
        if (t) return t;
    }
#endif
#if HAVE_ALTIVEC
    if (c->flags & SWS_CPU_CAPS_ALTIVEC)
    {
        SwsFunc t = yuv2rgb_init_altivec(c);
        if (t) return t;
    }
#endif

#if ARCH_BFIN
    if (c->flags & SWS_CPU_CAPS_BFIN)
    {
        SwsFunc t = ff_bfin_yuv2rgb_get_func_ptr (c);
        if (t) return t;
    }
#endif

    av_log(c, AV_LOG_WARNING, "No accelerated colorspace conversion found.\n");

    switch(c->dstFormat){
    case PIX_FMT_BGR32_1:
    case PIX_FMT_RGB32_1:
    case PIX_FMT_BGR32:
    case PIX_FMT_RGB32: return yuv2rgb_c_32;
    case PIX_FMT_RGB24: return yuv2rgb_c_24_rgb;
    case PIX_FMT_BGR24: return yuv2rgb_c_24_bgr;
    case PIX_FMT_RGB565:
    case PIX_FMT_BGR565:
    case PIX_FMT_RGB555:
    case PIX_FMT_BGR555: return yuv2rgb_c_16;
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:  return yuv2rgb_c_8_ordered_dither;
    case PIX_FMT_RGB4:
    case PIX_FMT_BGR4:  return yuv2rgb_c_4_ordered_dither;
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:  return yuv2rgb_c_4b_ordered_dither;
    case PIX_FMT_MONOBLACK:  return yuv2rgb_c_1_ordered_dither;
    default:
        assert(0);
    }
    return NULL;
}

static int div_round (int dividend, int divisor)
{
    if (dividend > 0)
        return (dividend + (divisor>>1)) / divisor;
    else
        return -((-dividend + (divisor>>1)) / divisor);
}

int yuv2rgb_c_init_tables (SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation)
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
    int i, base;
    uint8_t table_Y[1024];
    uint32_t *table_32 = 0;
    uint16_t *table_16 = 0;
    uint8_t *table_8 = 0;
    uint8_t *table_332 = 0;
    uint8_t *table_121 = 0;
    uint8_t *table_1 = 0;
    int entry_size = 0;
    void *table_r = 0, *table_g = 0, *table_b = 0;
    void *table_start;

    int64_t crv =  inv_table[0];
    int64_t cbu =  inv_table[1];
    int64_t cgu = -inv_table[2];
    int64_t cgv = -inv_table[3];
    int64_t cy  = 1<<16;
    int64_t oy  = 0;

//printf("%lld %lld %lld %lld %lld\n", cy, crv, cbu, cgu, cgv);
    if (!fullRange){
        cy= (cy*255) / 219;
        oy= 16<<16;
    }else{
        crv= (crv*224) / 255;
        cbu= (cbu*224) / 255;
        cgu= (cgu*224) / 255;
        cgv= (cgv*224) / 255;
    }

    cy = (cy *contrast             )>>16;
    crv= (crv*contrast * saturation)>>32;
    cbu= (cbu*contrast * saturation)>>32;
    cgu= (cgu*contrast * saturation)>>32;
    cgv= (cgv*contrast * saturation)>>32;
//printf("%lld %lld %lld %lld %lld\n", cy, crv, cbu, cgu, cgv);
    oy -= 256*brightness;

    for (i = 0; i < 1024; i++) {
        int j;

        j= (cy*(((i - 384)<<16) - oy) + (1<<31))>>32;
        j = (j < 0) ? 0 : ((j > 255) ? 255 : j);
        table_Y[i] = j;
    }

    switch (bpp) {
    case 32:
        table_start= table_32 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint32_t));
        base= (c->dstFormat == PIX_FMT_RGB32_1 || c->dstFormat == PIX_FMT_BGR32_1) ? 8 : 0;

        entry_size = sizeof (uint32_t);
        table_r = table_32 + 197;
        table_b = table_32 + 197 + 685;
        table_g = table_32 + 197 + 2*682;

        for (i = -197; i < 256+197; i++)
            ((uint32_t *)table_r)[i] = table_Y[i+384] << ((isRgb ? 16 : 0) + base);
        for (i = -132; i < 256+132; i++)
            ((uint32_t *)table_g)[i] = table_Y[i+384] << (8                + base);
        for (i = -232; i < 256+232; i++)
            ((uint32_t *)table_b)[i] = table_Y[i+384] << ((isRgb ? 0 : 16) + base);
        break;

    case 24:
        table_start= table_8 = av_malloc ((256 + 2*232) * sizeof (uint8_t));

        entry_size = sizeof (uint8_t);
        table_r = table_g = table_b = table_8 + 232;

        for (i = -232; i < 256+232; i++)
            ((uint8_t * )table_b)[i] = table_Y[i+384];
        break;

    case 15:
    case 16:
        table_start= table_16 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint16_t));

        entry_size = sizeof (uint16_t);
        table_r = table_16 + 197;
        table_b = table_16 + 197 + 685;
        table_g = table_16 + 197 + 2*682;

        for (i = -197; i < 256+197; i++) {
            int j = table_Y[i+384] >> 3;

            if (isRgb)
                j <<= ((bpp==16) ? 11 : 10);

            ((uint16_t *)table_r)[i] = j;
        }
        for (i = -132; i < 256+132; i++) {
            int j = table_Y[i+384] >> ((bpp==16) ? 2 : 3);

            ((uint16_t *)table_g)[i] = j << 5;
        }
        for (i = -232; i < 256+232; i++) {
            int j = table_Y[i+384] >> 3;

            if (!isRgb)
                j <<= ((bpp==16) ? 11 : 10);

            ((uint16_t *)table_b)[i] = j;
        }
        break;

    case 8:
        table_start= table_332 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint8_t));

        entry_size = sizeof (uint8_t);
        table_r = table_332 + 197;
        table_b = table_332 + 197 + 685;
        table_g = table_332 + 197 + 2*682;

        for (i = -197; i < 256+197; i++) {
            int j = (table_Y[i+384 - 16] + 18)/36;

            if (isRgb)
                j <<= 5;

            ((uint8_t *)table_r)[i] = j;
        }
        for (i = -132; i < 256+132; i++) {
            int j = (table_Y[i+384 - 16] + 18)/36;

            if (!isRgb)
                j <<= 1;

            ((uint8_t *)table_g)[i] = j << 2;
        }
        for (i = -232; i < 256+232; i++) {
            int j = (table_Y[i+384 - 37] + 43)/85;

            if (!isRgb)
                j <<= 6;

            ((uint8_t *)table_b)[i] = j;
        }
        break;
    case 4:
    case 4|128:
        table_start= table_121 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint8_t));

        entry_size = sizeof (uint8_t);
        table_r = table_121 + 197;
        table_b = table_121 + 197 + 685;
        table_g = table_121 + 197 + 2*682;

        for (i = -197; i < 256+197; i++) {
            int j = table_Y[i+384 - 110] >> 7;

            if (isRgb)
                j <<= 3;

            ((uint8_t *)table_r)[i] = j;
        }
        for (i = -132; i < 256+132; i++) {
            int j = (table_Y[i+384 - 37]+ 43)/85;

            ((uint8_t *)table_g)[i] = j << 1;
        }
        for (i = -232; i < 256+232; i++) {
            int j =table_Y[i+384 - 110] >> 7;

            if (!isRgb)
                j <<= 3;

            ((uint8_t *)table_b)[i] = j;
        }
        break;

    case 1:
        table_start= table_1 = av_malloc (256*2 * sizeof (uint8_t));

        entry_size = sizeof (uint8_t);
        table_g = table_1;
        table_r = table_b = NULL;

        for (i = 0; i < 256+256; i++) {
            int j = table_Y[i + 384 - 110]>>7;

            ((uint8_t *)table_g)[i] = j;
        }
        break;

    default:
        table_start= NULL;
        av_log(c, AV_LOG_ERROR, "%ibpp not supported by yuv2rgb\n", bpp);
        //free mem?
        return -1;
    }

    for (i = 0; i < 256; i++) {
        c->table_rV[i] = (uint8_t *)table_r + entry_size * div_round (crv * (i-128), cy);
        c->table_gU[i] = (uint8_t *)table_g + entry_size * div_round (cgu * (i-128), cy);
        c->table_gV[i] = entry_size * div_round (cgv * (i-128), cy);
        c->table_bU[i] = (uint8_t *)table_b + entry_size * div_round (cbu * (i-128), cy);
    }

    av_free(c->yuvTable);
    c->yuvTable= table_start;
    return 0;
}
