/*
 *  rgb2rgb.c, Software RGB to RGB convertor
 *  pluralize by Software PAL8 to RGB convertor
 *               Software YUV to YUV convertor
 *               Software YUV to RGB convertor
 *  Written by Nick Kurshev.
 *  palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
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
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * the C code (not assembly, mmx, ...) of this file can be used
 * under the LGPL license too
 */
#include <inttypes.h>
#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"
#include "x86_cpu.h"
#include "bswap.h"

#define FAST_BGR2YV12 // use 7 bit coeffs instead of 15bit

void (*rgb24to32)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb24to16)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb24to15)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb32to24)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb32to16)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb32to15)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb15to16)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb15to24)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb15to32)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb16to15)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb16to24)(const uint8_t *src,uint8_t *dst,long src_size);
void (*rgb16to32)(const uint8_t *src,uint8_t *dst,long src_size);
//void (*rgb24tobgr32)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr15)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32tobgr32)(const uint8_t *src, uint8_t *dst, long src_size);
//void (*rgb32tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32tobgr16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32tobgr15)(const uint8_t *src, uint8_t *dst, long src_size);

void (*yv12toyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                   long width, long height,
                   long lumStride, long chromStride, long dstStride);
void (*yv12touyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                   long width, long height,
                   long lumStride, long chromStride, long dstStride);
void (*yuv422ptoyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                      long width, long height,
                      long lumStride, long chromStride, long dstStride);
void (*yuy2toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                   long width, long height,
                   long lumStride, long chromStride, long srcStride);
void (*rgb24toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                    long width, long height,
                    long lumStride, long chromStride, long srcStride);
void (*planar2x)(const uint8_t *src, uint8_t *dst, long width, long height,
                 long srcStride, long dstStride);
void (*interleaveBytes)(uint8_t *src1, uint8_t *src2, uint8_t *dst,
                        long width, long height, long src1Stride,
                        long src2Stride, long dstStride);
void (*vu9_to_vu12)(const uint8_t *src1, const uint8_t *src2,
                    uint8_t *dst1, uint8_t *dst2,
                    long width, long height,
                    long srcStride1, long srcStride2,
                    long dstStride1, long dstStride2);
void (*yvu9_to_yuy2)(const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
                     uint8_t *dst,
                     long width, long height,
                     long srcStride1, long srcStride2,
                     long srcStride3, long dstStride);

#if defined(ARCH_X86) && defined(CONFIG_GPL)
static const uint64_t mmx_null     __attribute__((aligned(8))) = 0x0000000000000000ULL;
static const uint64_t mmx_one      __attribute__((aligned(8))) = 0xFFFFFFFFFFFFFFFFULL;
static const uint64_t mask32b      attribute_used __attribute__((aligned(8))) = 0x000000FF000000FFULL;
static const uint64_t mask32g      attribute_used __attribute__((aligned(8))) = 0x0000FF000000FF00ULL;
static const uint64_t mask32r      attribute_used __attribute__((aligned(8))) = 0x00FF000000FF0000ULL;
static const uint64_t mask32       __attribute__((aligned(8))) = 0x00FFFFFF00FFFFFFULL;
static const uint64_t mask3216br   __attribute__((aligned(8))) = 0x00F800F800F800F8ULL;
static const uint64_t mask3216g    __attribute__((aligned(8))) = 0x0000FC000000FC00ULL;
static const uint64_t mask3215g    __attribute__((aligned(8))) = 0x0000F8000000F800ULL;
static const uint64_t mul3216      __attribute__((aligned(8))) = 0x2000000420000004ULL;
static const uint64_t mul3215      __attribute__((aligned(8))) = 0x2000000820000008ULL;
static const uint64_t mask24b      attribute_used __attribute__((aligned(8))) = 0x00FF0000FF0000FFULL;
static const uint64_t mask24g      attribute_used __attribute__((aligned(8))) = 0xFF0000FF0000FF00ULL;
static const uint64_t mask24r      attribute_used __attribute__((aligned(8))) = 0x0000FF0000FF0000ULL;
static const uint64_t mask24l      __attribute__((aligned(8))) = 0x0000000000FFFFFFULL;
static const uint64_t mask24h      __attribute__((aligned(8))) = 0x0000FFFFFF000000ULL;
static const uint64_t mask24hh     __attribute__((aligned(8))) = 0xffff000000000000ULL;
static const uint64_t mask24hhh    __attribute__((aligned(8))) = 0xffffffff00000000ULL;
static const uint64_t mask24hhhh   __attribute__((aligned(8))) = 0xffffffffffff0000ULL;
static const uint64_t mask15b      __attribute__((aligned(8))) = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
static const uint64_t mask15rg     __attribute__((aligned(8))) = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
static const uint64_t mask15s      __attribute__((aligned(8))) = 0xFFE0FFE0FFE0FFE0ULL;
static const uint64_t mask15g      __attribute__((aligned(8))) = 0x03E003E003E003E0ULL;
static const uint64_t mask15r      __attribute__((aligned(8))) = 0x7C007C007C007C00ULL;
#define mask16b mask15b
static const uint64_t mask16g      __attribute__((aligned(8))) = 0x07E007E007E007E0ULL;
static const uint64_t mask16r      __attribute__((aligned(8))) = 0xF800F800F800F800ULL;
static const uint64_t red_16mask   __attribute__((aligned(8))) = 0x0000f8000000f800ULL;
static const uint64_t green_16mask __attribute__((aligned(8))) = 0x000007e0000007e0ULL;
static const uint64_t blue_16mask  __attribute__((aligned(8))) = 0x0000001f0000001fULL;
static const uint64_t red_15mask   __attribute__((aligned(8))) = 0x00007c0000007c00ULL;
static const uint64_t green_15mask __attribute__((aligned(8))) = 0x000003e0000003e0ULL;
static const uint64_t blue_15mask  __attribute__((aligned(8))) = 0x0000001f0000001fULL;

#ifdef FAST_BGR2YV12
static const uint64_t bgr2YCoeff   attribute_used __attribute__((aligned(8))) = 0x000000210041000DULL;
static const uint64_t bgr2UCoeff   attribute_used __attribute__((aligned(8))) = 0x0000FFEEFFDC0038ULL;
static const uint64_t bgr2VCoeff   attribute_used __attribute__((aligned(8))) = 0x00000038FFD2FFF8ULL;
#else
static const uint64_t bgr2YCoeff   attribute_used __attribute__((aligned(8))) = 0x000020E540830C8BULL;
static const uint64_t bgr2UCoeff   attribute_used __attribute__((aligned(8))) = 0x0000ED0FDAC23831ULL;
static const uint64_t bgr2VCoeff   attribute_used __attribute__((aligned(8))) = 0x00003831D0E6F6EAULL;
#endif
static const uint64_t bgr2YOffset  attribute_used __attribute__((aligned(8))) = 0x1010101010101010ULL;
static const uint64_t bgr2UVOffset attribute_used __attribute__((aligned(8))) = 0x8080808080808080ULL;
static const uint64_t w1111        attribute_used __attribute__((aligned(8))) = 0x0001000100010001ULL;

#if 0
static volatile uint64_t __attribute__((aligned(8))) b5Dither;
static volatile uint64_t __attribute__((aligned(8))) g5Dither;
static volatile uint64_t __attribute__((aligned(8))) g6Dither;
static volatile uint64_t __attribute__((aligned(8))) r5Dither;

static uint64_t __attribute__((aligned(8))) dither4[2]={
    0x0103010301030103LL,
    0x0200020002000200LL,};

static uint64_t __attribute__((aligned(8))) dither8[2]={
    0x0602060206020602LL,
    0x0004000400040004LL,};
#endif
#endif /* defined(ARCH_X86) */

#define RGB2YUV_SHIFT 8
#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

//Note: we have C, MMX, MMX2, 3DNOW version therse no 3DNOW+MMX2 one
//Plain C versions
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_SSE2
#define RENAME(a) a ## _C
#include "rgb2rgb_template.c"

#if defined(ARCH_X86) && defined(CONFIG_GPL)

//MMX versions
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_SSE2
#define RENAME(a) a ## _MMX
#include "rgb2rgb_template.c"

//MMX2 versions
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_SSE2
#define RENAME(a) a ## _MMX2
#include "rgb2rgb_template.c"

//3DNOW versions
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#undef HAVE_SSE2
#define RENAME(a) a ## _3DNOW
#include "rgb2rgb_template.c"

#endif //ARCH_X86 || ARCH_X86_64

/*
 rgb15->rgb16 Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32bit c version, and and&add trick by Michael Niedermayer
*/

void sws_rgb2rgb_init(int flags){
#if (defined(HAVE_MMX2) || defined(HAVE_3DNOW) || defined(HAVE_MMX))  && defined(CONFIG_GPL)
    if (flags & SWS_CPU_CAPS_MMX2)
        rgb2rgb_init_MMX2();
    else if (flags & SWS_CPU_CAPS_3DNOW)
        rgb2rgb_init_3DNOW();
    else if (flags & SWS_CPU_CAPS_MMX)
        rgb2rgb_init_MMX();
    else
#endif /* defined(HAVE_MMX2) || defined(HAVE_3DNOW) || defined(HAVE_MMX) */
        rgb2rgb_init_C();
}

/**
 * Palette is assumed to contain BGR32.
 */
void palette8torgb32(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;

/*
    for (i=0; i<num_pixels; i++)
        ((unsigned *)dst)[i] = ((unsigned *)palette)[ src[i] ];
*/

    for (i=0; i<num_pixels; i++)
    {
        #ifdef WORDS_BIGENDIAN
            dst[3]= palette[ src[i]*4+2 ];
            dst[2]= palette[ src[i]*4+1 ];
            dst[1]= palette[ src[i]*4+0 ];
        #else
        //FIXME slow?
            dst[0]= palette[ src[i]*4+2 ];
            dst[1]= palette[ src[i]*4+1 ];
            dst[2]= palette[ src[i]*4+0 ];
            //dst[3]= 0; /* do we need this cleansing? */
        #endif
        dst+= 4;
    }
}

void palette8tobgr32(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
    {
        #ifdef WORDS_BIGENDIAN
            dst[3]= palette[ src[i]*4+0 ];
            dst[2]= palette[ src[i]*4+1 ];
            dst[1]= palette[ src[i]*4+2 ];
        #else
            //FIXME slow?
            dst[0]= palette[ src[i]*4+0 ];
            dst[1]= palette[ src[i]*4+1 ];
            dst[2]= palette[ src[i]*4+2 ];
            //dst[3]= 0; /* do we need this cleansing? */
        #endif

        dst+= 4;
    }
}

/**
 * Palette is assumed to contain BGR32.
 */
void palette8torgb24(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
/*
    writes 1 byte o much and might cause alignment issues on some architectures?
    for (i=0; i<num_pixels; i++)
        ((unsigned *)(&dst[i*3])) = ((unsigned *)palette)[ src[i] ];
*/
    for (i=0; i<num_pixels; i++)
    {
        //FIXME slow?
        dst[0]= palette[ src[i]*4+2 ];
        dst[1]= palette[ src[i]*4+1 ];
        dst[2]= palette[ src[i]*4+0 ];
        dst+= 3;
    }
}

void palette8tobgr24(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
/*
    writes 1 byte o much and might cause alignment issues on some architectures?
    for (i=0; i<num_pixels; i++)
        ((unsigned *)(&dst[i*3])) = ((unsigned *)palette)[ src[i] ];
*/
    for (i=0; i<num_pixels; i++)
    {
        //FIXME slow?
        dst[0]= palette[ src[i]*4+0 ];
        dst[1]= palette[ src[i]*4+1 ];
        dst[2]= palette[ src[i]*4+2 ];
        dst+= 3;
    }
}

/**
 * Palette is assumed to contain bgr16, see rgb32to16 to convert the palette
 */
void palette8torgb16(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}
void palette8tobgr16(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = bswap_16(((uint16_t *)palette)[ src[i] ]);
}

/**
 * Palette is assumed to contain BGR15, see rgb32to15 to convert the palette.
 */
void palette8torgb15(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}
void palette8tobgr15(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = bswap_16(((uint16_t *)palette)[ src[i] ]);
}

void rgb32tobgr24(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 2;
    for (i=0; i<num_pixels; i++)
    {
        #ifdef WORDS_BIGENDIAN
            /* RGB32 (= A,B,G,R) -> BGR24 (= B,G,R) */
            dst[3*i + 0] = src[4*i + 1];
            dst[3*i + 1] = src[4*i + 2];
            dst[3*i + 2] = src[4*i + 3];
        #else
            dst[3*i + 0] = src[4*i + 2];
            dst[3*i + 1] = src[4*i + 1];
            dst[3*i + 2] = src[4*i + 0];
        #endif
    }
}

void rgb24tobgr32(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    for (i=0; 3*i<src_size; i++)
    {
        #ifdef WORDS_BIGENDIAN
            /* RGB24 (= R,G,B) -> BGR32 (= A,R,G,B) */
            dst[4*i + 0] = 0;
            dst[4*i + 1] = src[3*i + 0];
            dst[4*i + 2] = src[3*i + 1];
            dst[4*i + 3] = src[3*i + 2];
        #else
            dst[4*i + 0] = src[3*i + 2];
            dst[4*i + 1] = src[3*i + 1];
            dst[4*i + 2] = src[3*i + 0];
            dst[4*i + 3] = 0;
        #endif
    }
}

void rgb16tobgr32(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (uint16_t *)src;
    end = s + src_size/2;
    while (s < end)
    {
        register uint16_t bgr;
        bgr = *s++;
        #ifdef WORDS_BIGENDIAN
            *d++ = 0;
            *d++ = (bgr&0x1F)<<3;
            *d++ = (bgr&0x7E0)>>3;
            *d++ = (bgr&0xF800)>>8;
        #else
            *d++ = (bgr&0xF800)>>8;
            *d++ = (bgr&0x7E0)>>3;
            *d++ = (bgr&0x1F)<<3;
            *d++ = 0;
        #endif
    }
}

void rgb16tobgr24(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end)
    {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0xF800)>>8;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0x1F)<<3;
    }
}

void rgb16tobgr16(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++)
    {
        unsigned b,g,r;
        register uint16_t rgb;
        rgb = src[2*i];
        r = rgb&0x1F;
        g = (rgb&0x7E0)>>5;
        b = (rgb&0xF800)>>11;
        dst[2*i] = (b&0x1F) | ((g&0x3F)<<5) | ((r&0x1F)<<11);
    }
}

void rgb16tobgr15(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++)
    {
        unsigned b,g,r;
        register uint16_t rgb;
        rgb = src[2*i];
        r = rgb&0x1F;
        g = (rgb&0x7E0)>>5;
        b = (rgb&0xF800)>>11;
        dst[2*i] = (b&0x1F) | ((g&0x1F)<<5) | ((r&0x1F)<<10);
    }
}

void rgb15tobgr32(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end)
    {
        register uint16_t bgr;
        bgr = *s++;
        #ifdef WORDS_BIGENDIAN
            *d++ = 0;
            *d++ = (bgr&0x1F)<<3;
            *d++ = (bgr&0x3E0)>>2;
            *d++ = (bgr&0x7C00)>>7;
        #else
            *d++ = (bgr&0x7C00)>>7;
            *d++ = (bgr&0x3E0)>>2;
            *d++ = (bgr&0x1F)<<3;
            *d++ = 0;
        #endif
    }
}

void rgb15tobgr24(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (uint16_t *)src;
    end = s + src_size/2;
    while (s < end)
    {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0x7C00)>>7;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x1F)<<3;
    }
}

void rgb15tobgr16(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++)
    {
        unsigned b,g,r;
        register uint16_t rgb;
        rgb = src[2*i];
        r = rgb&0x1F;
        g = (rgb&0x3E0)>>5;
        b = (rgb&0x7C00)>>10;
        dst[2*i] = (b&0x1F) | ((g&0x3F)<<5) | ((r&0x1F)<<11);
    }
}

void rgb15tobgr15(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++)
    {
        unsigned b,g,r;
        register uint16_t rgb;
        rgb = src[2*i];
        r = rgb&0x1F;
        g = (rgb&0x3E0)>>5;
        b = (rgb&0x7C00)>>10;
        dst[2*i] = (b&0x1F) | ((g&0x1F)<<5) | ((r&0x1F)<<10);
    }
}

void rgb8tobgr8(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size;
    for (i=0; i<num_pixels; i++)
    {
        unsigned b,g,r;
        register uint8_t rgb;
        rgb = src[i];
        r = (rgb&0x07);
        g = (rgb&0x38)>>3;
        b = (rgb&0xC0)>>6;
        dst[i] = ((b<<1)&0x07) | ((g&0x07)<<3) | ((r&0x03)<<6);
    }
}
