/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2001-2007 Michael Niedermayer
 *           (c) 2010 Konstantin Shishkov
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

#undef MOVNTQ
#undef EMMS
#undef SFENCE

#if HAVE_AMD3DNOW
/* On K6 femms is faster than emms. On K7 femms is directly mapped to emms. */
#define EMMS   "femms"
#else
#define EMMS   "emms"
#endif

#if HAVE_MMX2
#define MOVNTQ "movntq"
#define SFENCE "sfence"
#else
#define MOVNTQ "movq"
#define SFENCE " # nop"
#endif

#define REG_BLUE  "0"
#define REG_RED   "1"
#define REG_GREEN "2"
#define REG_ALPHA "3"

#define YUV2RGB_LOOP(depth)                                          \
    h_size = (c->dstW + 7) & ~7;                                     \
    if (h_size * depth > FFABS(dstStride[0]))                        \
        h_size -= 8;                                                 \
                                                                     \
    if (c->srcFormat == PIX_FMT_YUV422P) {                           \
        srcStride[1] *= 2;                                           \
        srcStride[2] *= 2;                                           \
    }                                                                \
                                                                     \
    __asm__ volatile ("pxor %mm4, %mm4\n\t");                        \
    for (y = 0; y < srcSliceH; y++) {                                \
        uint8_t *image    = dst[0] + (y + srcSliceY) * dstStride[0]; \
        const uint8_t *py = src[0] +               y * srcStride[0]; \
        const uint8_t *pu = src[1] +        (y >> 1) * srcStride[1]; \
        const uint8_t *pv = src[2] +        (y >> 1) * srcStride[2]; \
        x86_reg index = -h_size / 2;                                 \

#define YUV2RGB_INITIAL_LOAD          \
    __asm__ volatile (                \
        "movq (%5, %0, 2), %%mm6\n\t" \
        "movd    (%2, %0), %%mm0\n\t" \
        "movd    (%3, %0), %%mm1\n\t" \
        "1: \n\t"                     \

/* YUV2RGB core
 * Conversion is performed in usual way:
 * R = Y' * Ycoef + Vred * V'
 * G = Y' * Ycoef + Vgreen * V' + Ugreen * U'
 * B = Y' * Ycoef               + Ublue * U'
 *
 * where X' = X * 8 - Xoffset (multiplication is performed to increase
 * precision a bit).
 * Since it operates in YUV420 colorspace, Y component is additionally
 * split into Y1 and Y2 for even and odd pixels.
 *
 * Input:
 * mm0 - U (4 elems), mm1 - V (4 elems), mm6 - Y (8 elems), mm4 - zero register
 * Output:
 * mm1 - R, mm2 - G, mm0 - B
 */
#define YUV2RGB                                  \
    /* convert Y, U, V into Y1', Y2', U', V' */  \
    "movq      %%mm6, %%mm7\n\t"                 \
    "punpcklbw %%mm4, %%mm0\n\t"                 \
    "punpcklbw %%mm4, %%mm1\n\t"                 \
    "pand     "MANGLE(mmx_00ffw)", %%mm6\n\t"    \
    "psrlw     $8,    %%mm7\n\t"                 \
    "psllw     $3,    %%mm0\n\t"                 \
    "psllw     $3,    %%mm1\n\t"                 \
    "psllw     $3,    %%mm6\n\t"                 \
    "psllw     $3,    %%mm7\n\t"                 \
    "psubsw   "U_OFFSET"(%4), %%mm0\n\t"         \
    "psubsw   "V_OFFSET"(%4), %%mm1\n\t"         \
    "psubw    "Y_OFFSET"(%4), %%mm6\n\t"         \
    "psubw    "Y_OFFSET"(%4), %%mm7\n\t"         \
\
     /* multiply by coefficients */              \
    "movq      %%mm0, %%mm2\n\t"                 \
    "movq      %%mm1, %%mm3\n\t"                 \
    "pmulhw   "UG_COEFF"(%4), %%mm2\n\t"         \
    "pmulhw   "VG_COEFF"(%4), %%mm3\n\t"         \
    "pmulhw   "Y_COEFF" (%4), %%mm6\n\t"         \
    "pmulhw   "Y_COEFF" (%4), %%mm7\n\t"         \
    "pmulhw   "UB_COEFF"(%4), %%mm0\n\t"         \
    "pmulhw   "VR_COEFF"(%4), %%mm1\n\t"         \
    "paddsw    %%mm3, %%mm2\n\t"                 \
    /* now: mm0 = UB, mm1 = VR, mm2 = CG */      \
    /*      mm6 = Y1, mm7 = Y2 */                \
\
    /* produce RGB */                            \
    "movq      %%mm7, %%mm3\n\t"                 \
    "movq      %%mm7, %%mm5\n\t"                 \
    "paddsw    %%mm0, %%mm3\n\t"                 \
    "paddsw    %%mm1, %%mm5\n\t"                 \
    "paddsw    %%mm2, %%mm7\n\t"                 \
    "paddsw    %%mm6, %%mm0\n\t"                 \
    "paddsw    %%mm6, %%mm1\n\t"                 \
    "paddsw    %%mm6, %%mm2\n\t"                 \
\
    /* pack and interleave even/odd pixels */    \
    "packuswb  %%mm0, %%mm0\n\t"                 \
    "packuswb  %%mm1, %%mm1\n\t"                 \
    "packuswb  %%mm2, %%mm2\n\t"                 \
    "packuswb  %%mm3, %%mm3\n\t"                 \
    "packuswb  %%mm5, %%mm5\n\t"                 \
    "packuswb  %%mm7, %%mm7\n\t"                 \
    "punpcklbw %%mm3, %%mm0\n\t"                 \
    "punpcklbw %%mm5, %%mm1\n\t"                 \
    "punpcklbw %%mm7, %%mm2\n\t"                 \

#define YUV2RGB_ENDLOOP(depth)                   \
    "movq 8 (%5, %0, 2), %%mm6\n\t"              \
    "movd 4 (%3, %0),    %%mm1\n\t"              \
    "movd 4 (%2, %0),    %%mm0\n\t"              \
    "add $"AV_STRINGIFY(depth * 8)", %1\n\t"     \
    "add  $4, %0\n\t"                            \
    "js   1b\n\t"                                \

#define YUV2RGB_OPERANDS                                          \
        : "+r" (index), "+r" (image)                              \
        : "r" (pu - index), "r" (pv - index), "r"(&c->redDither), \
          "r" (py - 2*index)                                      \
        );                                                        \
    }                                                             \

#define YUV2RGB_OPERANDS_ALPHA                                    \
        : "+r" (index), "+r" (image)                              \
        : "r" (pu - index), "r" (pv - index), "r"(&c->redDither), \
          "r" (py - 2*index), "r" (pa - 2*index)                  \
        );                                                        \
    }                                                             \

#define YUV2RGB_ENDFUNC                          \
    __asm__ volatile (SFENCE"\n\t"EMMS);         \
    return srcSliceH;                            \


#define RGB_PACK16(gmask, gshift, rshift)        \
    "pand      "MANGLE(mmx_redmask)", %%mm0\n\t" \
    "pand      "MANGLE(mmx_redmask)", %%mm1\n\t" \
    "psrlw     $3,        %%mm0\n\t"             \
    "pand      "MANGLE(gmask)",       %%mm2\n\t" \
    "movq      %%mm0,     %%mm5\n\t"             \
    "movq      %%mm1,     %%mm6\n\t"             \
    "movq      %%mm2,     %%mm7\n\t"             \
    "punpcklbw %%mm4,     %%mm0\n\t"             \
    "punpcklbw %%mm4,     %%mm1\n\t"             \
    "punpcklbw %%mm4,     %%mm2\n\t"             \
    "punpckhbw %%mm4,     %%mm5\n\t"             \
    "punpckhbw %%mm4,     %%mm6\n\t"             \
    "punpckhbw %%mm4,     %%mm7\n\t"             \
    "psllw     $"rshift", %%mm1\n\t"             \
    "psllw     $"rshift", %%mm6\n\t"             \
    "psllw     $"gshift", %%mm2\n\t"             \
    "psllw     $"gshift", %%mm7\n\t"             \
    "por       %%mm1,     %%mm0\n\t"             \
    "por       %%mm6,     %%mm5\n\t"             \
    "por       %%mm2,     %%mm0\n\t"             \
    "por       %%mm7,     %%mm5\n\t"             \
    MOVNTQ "   %%mm0,      (%1)\n\t"             \
    MOVNTQ "   %%mm5,     8(%1)\n\t"             \

#define DITHER_RGB                               \
    "paddusb "BLUE_DITHER"(%4),  %%mm0\n\t"      \
    "paddusb "GREEN_DITHER"(%4), %%mm2\n\t"      \
    "paddusb "RED_DITHER"(%4),   %%mm1\n\t"      \

static inline int RENAME(yuv420_rgb15)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(2)

#ifdef DITHER1XBPP
        c->blueDither  = ff_dither8[y       & 1];
        c->greenDither = ff_dither8[y       & 1];
        c->redDither   = ff_dither8[(y + 1) & 1];
#endif

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
#ifdef DITHER1XBPP
        DITHER_RGB
#endif
        RGB_PACK16(mmx_redmask, "2", "7")

    YUV2RGB_ENDLOOP(2)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}

static inline int RENAME(yuv420_rgb16)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(2)

#ifdef DITHER1XBPP
        c->blueDither  = ff_dither8[y       & 1];
        c->greenDither = ff_dither4[y       & 1];
        c->redDither   = ff_dither8[(y + 1) & 1];
#endif

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
#ifdef DITHER1XBPP
        DITHER_RGB
#endif
        RGB_PACK16(mmx_grnmask, "3", "8")

    YUV2RGB_ENDLOOP(2)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}


#define RGB_PACK24(red, blue)              \
    /* generate first packed RGB octet */  \
    "movq      %%mm2,      %%mm5\n\t"      \
    "movq      %%mm"blue", %%mm6\n\t"      \
    "movq      %%mm"red",  %%mm7\n\t"      \
    "punpcklbw %%mm5,      %%mm6\n\t"      \
    "punpcklbw %%mm4,      %%mm7\n\t"      \
    "movq      %%mm6,      %%mm3\n\t"      \
    "punpcklwd %%mm7,      %%mm6\n\t"      \
    "psrlq     $32,        %%mm3\n\t"      \
    "movq      %%mm6,      %%mm5\n\t"      \
    "psllq     $40,        %%mm6\n\t"      \
    "psllq     $48,        %%mm3\n\t"      \
    "psrlq     $32,        %%mm5\n\t"      \
    "psrlq     $40,        %%mm6\n\t"      \
    "psllq     $24,        %%mm5\n\t"      \
    "por       %%mm3,      %%mm6\n\t"      \
    "por       %%mm5,      %%mm6\n\t"      \
    MOVNTQ "   %%mm6,      (%1)\n\t"       \
\
    /* generate second packed RGB octet */ \
    "movq      %%mm"red",  %%mm7\n\t"      \
    "movq      %%mm2,      %%mm5\n\t"      \
    "movq      %%mm"blue", %%mm6\n\t"      \
    "punpcklbw %%mm4,      %%mm7\n\t"      \
    "punpcklbw %%mm5,      %%mm6\n\t"      \
    "movq      %%mm7,      %%mm3\n\t"      \
    "punpckhwd %%mm7,      %%mm6\n\t"      \
    "psllq     $16,        %%mm3\n\t"      \
    "psrlq     $32,        %%mm6\n\t"      \
    "psrlq     $48,        %%mm3\n\t"      \
    "psllq     $8,         %%mm6\n\t"      \
    "movq      %%mm"red",  %%mm7\n\t"      \
    "por       %%mm6,      %%mm3\n\t"      \
    "movq      %%mm"blue", %%mm6\n\t"      \
    "movq      %%mm2,      %%mm5\n\t"      \
    "punpckhbw %%mm4,      %%mm7\n\t"      \
    "punpckhbw %%mm5,      %%mm6\n\t"      \
    "movq      %%mm6,      %%mm5\n\t"      \
    "punpcklwd %%mm7,      %%mm6\n\t"      \
    "psrlq     $16,        %%mm5\n\t"      \
    "psllq     $56,        %%mm5\n\t"      \
    "por       %%mm5,      %%mm3\n\t"      \
    "psllq     $32,        %%mm6\n\t"      \
    "por       %%mm6,      %%mm3\n\t"      \
    MOVNTQ "   %%mm3,      8(%1)\n\t"      \
\
    /* generate third packed RGB octet */  \
    "movq      %%mm"red",  %%mm7\n\t"      \
    "movq      %%mm2,      %%mm5\n\t"      \
    "movq      %%mm2,      %%mm3\n\t"      \
    "movq      %%mm"blue", %%mm6\n\t"      \
    "punpckhbw %%mm"red",  %%mm3\n\t"      \
    "punpckhbw %%mm4,      %%mm7\n\t"      \
    "psllq     $32,        %%mm3\n\t"      \
    "punpckhbw %%mm5,      %%mm6\n\t"      \
    "psrlq     $48,        %%mm3\n\t"      \
    "punpckhwd %%mm7,      %%mm6\n\t"      \
    "movq      %%mm6,      %%mm7\n\t"      \
    "psrlq     $32,        %%mm6\n\t"      \
    "psllq     $32,        %%mm7\n\t"      \
    "psllq     $40,        %%mm6\n\t"      \
    "psrlq     $16,        %%mm7\n\t"      \
    "por       %%mm6,      %%mm3\n\t"      \
    "por       %%mm7,      %%mm3\n\t"      \
    MOVNTQ "   %%mm3,      16(%1)\n\t"     \

static inline int RENAME(yuv420_rgb24)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(3)

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        RGB_PACK24(REG_BLUE, REG_RED)

    YUV2RGB_ENDLOOP(3)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}

static inline int RENAME(yuv420_bgr24)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(3)

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        RGB_PACK24(REG_RED, REG_BLUE)

    YUV2RGB_ENDLOOP(3)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}


#define SET_EMPTY_ALPHA                                                      \
    "pcmpeqd   %%mm"REG_ALPHA", %%mm"REG_ALPHA"\n\t" /* set alpha to 0xFF */ \

#define LOAD_ALPHA                                   \
    "movq      (%6, %0, 2),     %%mm"REG_ALPHA"\n\t" \

#define RGB_PACK32(red, green, blue, alpha)  \
    "movq      %%mm"blue",  %%mm5\n\t"       \
    "movq      %%mm"red",   %%mm6\n\t"       \
    "punpckhbw %%mm"green", %%mm5\n\t"       \
    "punpcklbw %%mm"green", %%mm"blue"\n\t"  \
    "punpckhbw %%mm"alpha", %%mm6\n\t"       \
    "punpcklbw %%mm"alpha", %%mm"red"\n\t"   \
    "movq      %%mm"blue",  %%mm"green"\n\t" \
    "movq      %%mm5,       %%mm"alpha"\n\t" \
    "punpcklwd %%mm"red",   %%mm"blue"\n\t"  \
    "punpckhwd %%mm"red",   %%mm"green"\n\t" \
    "punpcklwd %%mm6,       %%mm5\n\t"       \
    "punpckhwd %%mm6,       %%mm"alpha"\n\t" \
    MOVNTQ "   %%mm"blue",   0(%1)\n\t"      \
    MOVNTQ "   %%mm"green",  8(%1)\n\t"      \
    MOVNTQ "   %%mm5,       16(%1)\n\t"      \
    MOVNTQ "   %%mm"alpha", 24(%1)\n\t"      \

static inline int RENAME(yuv420_rgb32)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(4)

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        SET_EMPTY_ALPHA
        RGB_PACK32(REG_RED, REG_GREEN, REG_BLUE, REG_ALPHA)

    YUV2RGB_ENDLOOP(4)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}

static inline int RENAME(yuva420_rgb32)(SwsContext *c, const uint8_t *src[],
                                        int srcStride[],
                                        int srcSliceY, int srcSliceH,
                                        uint8_t *dst[], int dstStride[])
{
#if HAVE_7REGS
    int y, h_size;

    YUV2RGB_LOOP(4)

        const uint8_t *pa = src[3] + y * srcStride[3];
        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        LOAD_ALPHA
        RGB_PACK32(REG_RED, REG_GREEN, REG_BLUE, REG_ALPHA)

    YUV2RGB_ENDLOOP(4)
    YUV2RGB_OPERANDS_ALPHA
    YUV2RGB_ENDFUNC
#endif
}

static inline int RENAME(yuv420_bgr32)(SwsContext *c, const uint8_t *src[],
                                       int srcStride[],
                                       int srcSliceY, int srcSliceH,
                                       uint8_t *dst[], int dstStride[])
{
    int y, h_size;

    YUV2RGB_LOOP(4)

        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        SET_EMPTY_ALPHA
        RGB_PACK32(REG_BLUE, REG_GREEN, REG_RED, REG_ALPHA)

    YUV2RGB_ENDLOOP(4)
    YUV2RGB_OPERANDS
    YUV2RGB_ENDFUNC
}

static inline int RENAME(yuva420_bgr32)(SwsContext *c, const uint8_t *src[],
                                        int srcStride[],
                                        int srcSliceY, int srcSliceH,
                                        uint8_t *dst[], int dstStride[])
{
#if HAVE_7REGS
    int y, h_size;

    YUV2RGB_LOOP(4)

        const uint8_t *pa = src[3] + y * srcStride[3];
        YUV2RGB_INITIAL_LOAD
        YUV2RGB
        LOAD_ALPHA
        RGB_PACK32(REG_BLUE, REG_GREEN, REG_RED, REG_ALPHA)

    YUV2RGB_ENDLOOP(4)
    YUV2RGB_OPERANDS_ALPHA
    YUV2RGB_ENDFUNC
#endif
}
