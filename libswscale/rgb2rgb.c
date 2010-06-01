/*
 * software RGB to RGB converter
 * pluralize by software PAL8 to RGB converter
 *              software YUV to YUV converter
 *              software YUV to RGB converter
 * Written by Nick Kurshev.
 * palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
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
#include <inttypes.h>
#include "config.h"
#include "libavutil/x86_cpu.h"
#include "libavutil/bswap.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

#define FAST_BGR2YV12 // use 7-bit instead of 15-bit coefficients

void (*rgb24tobgr32)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr15)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32to16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32to15)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb15to16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb15tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb15to32)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb16to15)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb16tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb16to32)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24tobgr24)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24to16)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb24to15)(const uint8_t *src, uint8_t *dst, long src_size);
void (*rgb32tobgr32)(const uint8_t *src, uint8_t *dst, long src_size);
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
void (*yuv422ptouyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
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
void (*interleaveBytes)(const uint8_t *src1, const uint8_t *src2, uint8_t *dst,
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
void (*uyvytoyuv420)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                     long width, long height,
                     long lumStride, long chromStride, long srcStride);
void (*uyvytoyuv422)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                     long width, long height,
                     long lumStride, long chromStride, long srcStride);
void (*yuyvtoyuv420)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                     long width, long height,
                     long lumStride, long chromStride, long srcStride);
void (*yuyvtoyuv422)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                     long width, long height,
                     long lumStride, long chromStride, long srcStride);


#if ARCH_X86
DECLARE_ASM_CONST(8, uint64_t, mmx_null)     = 0x0000000000000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_one)      = 0xFFFFFFFFFFFFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask32b)      = 0x000000FF000000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mask32g)      = 0x0000FF000000FF00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32r)      = 0x00FF000000FF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32a)      = 0xFF000000FF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32)       = 0x00FFFFFF00FFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216br)   = 0x00F800F800F800F8ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216g)    = 0x0000FC000000FC00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3215g)    = 0x0000F8000000F800ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3216)      = 0x2000000420000004ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3215)      = 0x2000000820000008ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24b)      = 0x00FF0000FF0000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24g)      = 0xFF0000FF0000FF00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24r)      = 0x0000FF0000FF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24l)      = 0x0000000000FFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24h)      = 0x0000FFFFFF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hh)     = 0xffff000000000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hhh)    = 0xffffffff00000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hhhh)   = 0xffffffffffff0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15b)      = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
DECLARE_ASM_CONST(8, uint64_t, mask15rg)     = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
DECLARE_ASM_CONST(8, uint64_t, mask15s)      = 0xFFE0FFE0FFE0FFE0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15g)      = 0x03E003E003E003E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15r)      = 0x7C007C007C007C00ULL;
#define mask16b mask15b
DECLARE_ASM_CONST(8, uint64_t, mask16g)      = 0x07E007E007E007E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask16r)      = 0xF800F800F800F800ULL;
DECLARE_ASM_CONST(8, uint64_t, red_16mask)   = 0x0000f8000000f800ULL;
DECLARE_ASM_CONST(8, uint64_t, green_16mask) = 0x000007e0000007e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_16mask)  = 0x0000001f0000001fULL;
DECLARE_ASM_CONST(8, uint64_t, red_15mask)   = 0x00007c0000007c00ULL;
DECLARE_ASM_CONST(8, uint64_t, green_15mask) = 0x000003e0000003e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_15mask)  = 0x0000001f0000001fULL;
#endif /* ARCH_X86 */

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

//Note: We have C, MMX, MMX2, 3DNOW versions, there is no 3DNOW + MMX2 one.
//plain C versions
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_AMD3DNOW
#undef HAVE_SSE2
#define HAVE_MMX 0
#define HAVE_MMX2 0
#define HAVE_AMD3DNOW 0
#define HAVE_SSE2 0
#define RENAME(a) a ## _C
#include "rgb2rgb_template.c"

#if ARCH_X86

//MMX versions
#undef RENAME
#undef HAVE_MMX
#define HAVE_MMX 1
#define RENAME(a) a ## _MMX
#include "rgb2rgb_template.c"

//MMX2 versions
#undef RENAME
#undef HAVE_MMX2
#define HAVE_MMX2 1
#define RENAME(a) a ## _MMX2
#include "rgb2rgb_template.c"

//3DNOW versions
#undef RENAME
#undef HAVE_MMX2
#undef HAVE_AMD3DNOW
#define HAVE_MMX2 0
#define HAVE_AMD3DNOW 1
#define RENAME(a) a ## _3DNOW
#include "rgb2rgb_template.c"

#endif //ARCH_X86 || ARCH_X86_64

/*
 RGB15->RGB16 original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32-bit C version, and and&add trick by Michael Niedermayer
*/

void sws_rgb2rgb_init(int flags)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW || HAVE_MMX
    if (flags & SWS_CPU_CAPS_MMX2)
        rgb2rgb_init_MMX2();
    else if (flags & SWS_CPU_CAPS_3DNOW)
        rgb2rgb_init_3DNOW();
    else if (flags & SWS_CPU_CAPS_MMX)
        rgb2rgb_init_MMX();
    else
#endif /* HAVE_MMX2 || HAVE_AMD3DNOW || HAVE_MMX */
        rgb2rgb_init_C();
}

#if LIBSWSCALE_VERSION_MAJOR < 1
void palette8topacked32(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    sws_convertPalette8ToPacked32(src, dst, num_pixels, palette);
}

void palette8topacked24(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    sws_convertPalette8ToPacked24(src, dst, num_pixels, palette);
}

/**
 * Palette is assumed to contain BGR16, see rgb32to16 to convert the palette.
 */
void palette8torgb16(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = ((const uint16_t *)palette)[src[i]];
}
void palette8tobgr16(const uint8_t *src, uint8_t *dst, long num_pixels, const uint8_t *palette)
{
    long i;
    for (i=0; i<num_pixels; i++)
        ((uint16_t *)dst)[i] = bswap_16(((const uint16_t *)palette)[src[i]]);
}
#endif

void rgb32to24(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 2;
    for (i=0; i<num_pixels; i++) {
#if HAVE_BIGENDIAN
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

void rgb24to32(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    for (i=0; 3*i<src_size; i++) {
#if HAVE_BIGENDIAN
        /* RGB24 (= R,G,B) -> BGR32 (= A,R,G,B) */
        dst[4*i + 0] = 255;
        dst[4*i + 1] = src[3*i + 0];
        dst[4*i + 2] = src[3*i + 1];
        dst[4*i + 3] = src[3*i + 2];
#else
        dst[4*i + 0] = src[3*i + 2];
        dst[4*i + 1] = src[3*i + 1];
        dst[4*i + 2] = src[3*i + 0];
        dst[4*i + 3] = 255;
#endif
    }
}

void rgb16tobgr32(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
#if HAVE_BIGENDIAN
        *d++ = 255;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0xF800)>>8;
#else
        *d++ = (bgr&0xF800)>>8;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0x1F)<<3;
        *d++ = 255;
#endif
    }
}

void rgb16to24(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end) {
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

    for (i=0; i<num_pixels; i++) {
        unsigned rgb = ((const uint16_t*)src)[i];
        ((uint16_t*)dst)[i] = (rgb>>11) | (rgb&0x7E0) | (rgb<<11);
    }
}

void rgb16tobgr15(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++) {
        unsigned rgb = ((const uint16_t*)src)[i];
        ((uint16_t*)dst)[i] = (rgb>>11) | ((rgb&0x7C0)>>1) | ((rgb&0x1F)<<10);
    }
}

void rgb15tobgr32(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
#if HAVE_BIGENDIAN
        *d++ = 255;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x7C00)>>7;
#else
        *d++ = (bgr&0x7C00)>>7;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x1F)<<3;
        *d++ = 255;
#endif
    }
}

void rgb15to24(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end) {
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

    for (i=0; i<num_pixels; i++) {
        unsigned rgb = ((const uint16_t*)src)[i];
        ((uint16_t*)dst)[i] = ((rgb&0x7C00)>>10) | ((rgb&0x3E0)<<1) | (rgb<<11);
    }
}

void rgb15tobgr15(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size >> 1;

    for (i=0; i<num_pixels; i++) {
        unsigned br;
        unsigned rgb = ((const uint16_t*)src)[i];
        br = rgb&0x7c1F;
        ((uint16_t*)dst)[i] = (br>>10) | (rgb&0x3E0) | (br<<10);
    }
}

void bgr8torgb8(const uint8_t *src, uint8_t *dst, long src_size)
{
    long i;
    long num_pixels = src_size;
    for (i=0; i<num_pixels; i++) {
        unsigned b,g,r;
        register uint8_t rgb;
        rgb = src[i];
        r = (rgb&0x07);
        g = (rgb&0x38)>>3;
        b = (rgb&0xC0)>>6;
        dst[i] = ((b<<1)&0x07) | ((g&0x07)<<3) | ((r&0x03)<<6);
    }
}

#define DEFINE_SHUFFLE_BYTES(a, b, c, d)                                \
void shuffle_bytes_##a##b##c##d(const uint8_t *src, uint8_t *dst, long src_size) \
{                                                                       \
    long i;                                                             \
                                                                        \
    for (i = 0; i < src_size; i+=4) {                                   \
        dst[i + 0] = src[i + a];                                        \
        dst[i + 1] = src[i + b];                                        \
        dst[i + 2] = src[i + c];                                        \
        dst[i + 3] = src[i + d];                                        \
    }                                                                   \
}

DEFINE_SHUFFLE_BYTES(0, 3, 2, 1);
DEFINE_SHUFFLE_BYTES(1, 2, 3, 0);
DEFINE_SHUFFLE_BYTES(2, 1, 0, 3);
DEFINE_SHUFFLE_BYTES(3, 0, 1, 2);
DEFINE_SHUFFLE_BYTES(3, 2, 1, 0);

