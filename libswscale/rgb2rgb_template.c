/*
 * software RGB to RGB converter
 * pluralize by software PAL8 to RGB converter
 *              software YUV to YUV converter
 *              software YUV to RGB converter
 * Written by Nick Kurshev.
 * palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
 * lot of big-endian byte order fixes by Alex Beregszaszi
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stddef.h>

static inline void rgb24tobgr32_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    uint8_t *dest = dst;
    const uint8_t *s = src;
    const uint8_t *end;
    end = s + src_size;

    while (s < end) {
#if HAVE_BIGENDIAN
        /* RGB24 (= R,G,B) -> RGB32 (= A,B,G,R) */
        *dest++ = 255;
        *dest++ = s[2];
        *dest++ = s[1];
        *dest++ = s[0];
        s+=3;
#else
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = 255;
#endif
    }
}

static inline void rgb32tobgr24_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    uint8_t *dest = dst;
    const uint8_t *s = src;
    const uint8_t *end;

    end = s + src_size;

    while (s < end) {
#if HAVE_BIGENDIAN
        /* RGB32 (= A,B,G,R) -> RGB24 (= R,G,B) */
        s++;
        dest[2] = *s++;
        dest[1] = *s++;
        dest[0] = *s++;
        dest += 3;
#else
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = *s++;
        s++;
#endif
    }
}

/*
 original by Strepto/Astral
 ported to gcc & bugfixed: A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32-bit C version, and and&add trick by Michael Niedermayer
*/
static inline void rgb15to16_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    register const uint8_t* s=src;
    register uint8_t* d=dst;
    register const uint8_t *end;
    const uint8_t *mm_end;
    end = s + src_size;
    mm_end = end - 3;
    while (s < mm_end) {
        register unsigned x= *((const uint32_t *)s);
        *((uint32_t *)d) = (x&0x7FFF7FFF) + (x&0x7FE07FE0);
        d+=4;
        s+=4;
    }
    if (s < end) {
        register unsigned short x= *((const uint16_t *)s);
        *((uint16_t *)d) = (x&0x7FFF) + (x&0x7FE0);
    }
}

static inline void rgb16to15_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    register const uint8_t* s=src;
    register uint8_t* d=dst;
    register const uint8_t *end;
    const uint8_t *mm_end;
    end = s + src_size;

    mm_end = end - 3;
    while (s < mm_end) {
        register uint32_t x= *((const uint32_t*)s);
        *((uint32_t *)d) = ((x>>1)&0x7FE07FE0) | (x&0x001F001F);
        s+=4;
        d+=4;
    }
    if (s < end) {
        register uint16_t x= *((const uint16_t*)s);
        *((uint16_t *)d) = ((x>>1)&0x7FE0) | (x&0x001F);
    }
}

static inline void rgb32to16_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;

    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xFF)>>3) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>8);
    }
}

static inline void rgb32tobgr16_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xF8)<<8) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>19);
    }
}

static inline void rgb32to15_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xFF)>>3) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>9);
    }
}

static inline void rgb32tobgr15_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xF8)<<7) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>19);
    }
}

static inline void rgb24tobgr16_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        const int b = *s++;
        const int g = *s++;
        const int r = *s++;
        *d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
    }
}

static inline void rgb24to16_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        const int r = *s++;
        const int g = *s++;
        const int b = *s++;
        *d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
    }
}

static inline void rgb24tobgr15_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        const int b = *s++;
        const int g = *s++;
        const int r = *s++;
        *d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
    }
}

static inline void rgb24to15_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
    while (s < end) {
        const int r = *s++;
        const int g = *s++;
        const int b = *s++;
        *d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
    }
}

/*
  I use less accurate approximation here by simply left-shifting the input
  value and filling the low order bits with zeroes. This method improves PNG
  compression but this scheme cannot reproduce white exactly, since it does
  not generate an all-ones maximum value; the net effect is to darken the
  image slightly.

  The better method should be "left bit replication":

   4 3 2 1 0
   ---------
   1 1 0 1 1

   7 6 5 4 3  2 1 0
   ----------------
   1 1 0 1 1  1 1 0
   |=======|  |===|
       |      leftmost bits repeated to fill open bits
       |
   original bits
*/
static inline void rgb15tobgr24_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t*)src;
    end = s + src_size/2;
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x7C00)>>7;
    }
}

static inline void rgb16tobgr24_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint16_t *end;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0xF800)>>8;
    }
}

static inline void rgb15to32_c(const uint8_t *src, uint8_t *dst, int src_size)
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
        *d++ = (bgr&0x7C00)>>7;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x1F)<<3;
#else
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x7C00)>>7;
        *d++ = 255;
#endif
    }
}

static inline void rgb16to32_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    const uint16_t *end;
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t*)src;
    end = s + src_size/2;
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
#if HAVE_BIGENDIAN
        *d++ = 255;
        *d++ = (bgr&0xF800)>>8;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0x1F)<<3;
#else
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0xF800)>>8;
        *d++ = 255;
#endif
    }
}

static inline void shuffle_bytes_2103_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    int idx = 15 - src_size;
    const uint8_t *s = src-idx;
    uint8_t *d = dst-idx;
    for (; idx<15; idx+=4) {
        register int v = *(const uint32_t *)&s[idx], g = v & 0xff00ff00;
        v &= 0xff00ff;
        *(uint32_t *)&d[idx] = (v>>16) + g + (v<<16);
    }
}

static inline void rgb24tobgr24_c(const uint8_t *src, uint8_t *dst, int src_size)
{
    unsigned i;
    for (i=0; i<src_size; i+=3) {
        register uint8_t x;
        x          = src[i + 2];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 0];
        dst[i + 0] = x;
    }
}

static inline void yuvPlanartoyuy2_c(const uint8_t *ysrc, const uint8_t *usrc,
                                     const uint8_t *vsrc, uint8_t *dst,
                                     int width, int height,
                                     int lumStride, int chromStride,
                                     int dstStride, int vertLumPerChroma)
{
    int y;
    const int chromWidth = width >> 1;
    for (y=0; y<height; y++) {
#if HAVE_FAST_64BIT
        int i;
        uint64_t *ldst = (uint64_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i += 2) {
            uint64_t k, l;
            k = yc[0] + (uc[0] << 8) +
                (yc[1] << 16) + (vc[0] << 24);
            l = yc[2] + (uc[1] << 8) +
                (yc[3] << 16) + (vc[1] << 24);
            *ldst++ = k + (l << 32);
            yc += 4;
            uc += 2;
            vc += 2;
        }

#else
        int i, *idst = (int32_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i++) {
#if HAVE_BIGENDIAN
            *idst++ = (yc[0] << 24)+ (uc[0] << 16) +
                (yc[1] << 8) + (vc[0] << 0);
#else
            *idst++ = yc[0] + (uc[0] << 8) +
                (yc[1] << 16) + (vc[0] << 24);
#endif
            yc += 2;
            uc++;
            vc++;
        }
#endif
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst  += dstStride;
    }
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void yv12toyuy2_c(const uint8_t *ysrc, const uint8_t *usrc,
                                const uint8_t *vsrc, uint8_t *dst,
                                int width, int height,
                                int lumStride, int chromStride,
                                int dstStride)
{
    //FIXME interpolate chroma
    yuvPlanartoyuy2_c(ysrc, usrc, vsrc, dst, width, height, lumStride,
                      chromStride, dstStride, 2);
}

static inline void yuvPlanartouyvy_c(const uint8_t *ysrc, const uint8_t *usrc,
                                     const uint8_t *vsrc, uint8_t *dst,
                                     int width, int height,
                                     int lumStride, int chromStride,
                                     int dstStride, int vertLumPerChroma)
{
    int y;
    const int chromWidth = width >> 1;
    for (y=0; y<height; y++) {
#if HAVE_FAST_64BIT
        int i;
        uint64_t *ldst = (uint64_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i += 2) {
            uint64_t k, l;
            k = uc[0] + (yc[0] << 8) +
                (vc[0] << 16) + (yc[1] << 24);
            l = uc[1] + (yc[2] << 8) +
                (vc[1] << 16) + (yc[3] << 24);
            *ldst++ = k + (l << 32);
            yc += 4;
            uc += 2;
            vc += 2;
        }

#else
        int i, *idst = (int32_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i++) {
#if HAVE_BIGENDIAN
            *idst++ = (uc[0] << 24)+ (yc[0] << 16) +
                (vc[0] << 8) + (yc[1] << 0);
#else
            *idst++ = uc[0] + (yc[0] << 8) +
               (vc[0] << 16) + (yc[1] << 24);
#endif
            yc += 2;
            uc++;
            vc++;
        }
#endif
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void yv12touyvy_c(const uint8_t *ysrc, const uint8_t *usrc,
                                const uint8_t *vsrc, uint8_t *dst,
                                int width, int height,
                                int lumStride, int chromStride,
                                int dstStride)
{
    //FIXME interpolate chroma
    yuvPlanartouyvy_c(ysrc, usrc, vsrc, dst, width, height, lumStride,
                      chromStride, dstStride, 2);
}

/**
 * Width should be a multiple of 16.
 */
static inline void yuv422ptouyvy_c(const uint8_t *ysrc, const uint8_t *usrc,
                                   const uint8_t *vsrc, uint8_t *dst,
                                   int width, int height,
                                   int lumStride, int chromStride,
                                   int dstStride)
{
    yuvPlanartouyvy_c(ysrc, usrc, vsrc, dst, width, height, lumStride,
                      chromStride, dstStride, 1);
}

/**
 * Width should be a multiple of 16.
 */
static inline void yuv422ptoyuy2_c(const uint8_t *ysrc, const uint8_t *usrc,
                                   const uint8_t *vsrc, uint8_t *dst,
                                   int width, int height,
                                   int lumStride, int chromStride,
                                   int dstStride)
{
    yuvPlanartoyuy2_c(ysrc, usrc, vsrc, dst, width, height, lumStride,
                      chromStride, dstStride, 1);
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void yuy2toyv12_c(const uint8_t *src, uint8_t *ydst,
                                uint8_t *udst, uint8_t *vdst,
                                int width, int height,
                                int lumStride, int chromStride,
                                int srcStride)
{
    int y;
    const int chromWidth = width >> 1;
    for (y=0; y<height; y+=2) {
        int i;
        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0]     = src[4*i+0];
            udst[i]     = src[4*i+1];
            ydst[2*i+1]     = src[4*i+2];
            vdst[i]     = src[4*i+3];
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0]     = src[4*i+0];
            ydst[2*i+1]     = src[4*i+2];
        }
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
}

static inline void planar2x_c(const uint8_t *src, uint8_t *dst, int srcWidth,
                              int srcHeight, int srcStride, int dstStride)
{
    int x,y;

    dst[0]= src[0];

    // first line
    for (x=0; x<srcWidth-1; x++) {
        dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
        dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
    }
    dst[2*srcWidth-1]= src[srcWidth-1];

    dst+= dstStride;

    for (y=1; y<srcHeight; y++) {
        const int mmxSize = 1;

        dst[0        ]= (3*src[0] +   src[srcStride])>>2;
        dst[dstStride]= (  src[0] + 3*src[srcStride])>>2;

        for (x=mmxSize-1; x<srcWidth-1; x++) {
            dst[2*x          +1]= (3*src[x+0] +   src[x+srcStride+1])>>2;
            dst[2*x+dstStride+2]= (  src[x+0] + 3*src[x+srcStride+1])>>2;
            dst[2*x+dstStride+1]= (  src[x+1] + 3*src[x+srcStride  ])>>2;
            dst[2*x          +2]= (3*src[x+1] +   src[x+srcStride  ])>>2;
        }
        dst[srcWidth*2 -1            ]= (3*src[srcWidth-1] +   src[srcWidth-1 + srcStride])>>2;
        dst[srcWidth*2 -1 + dstStride]= (  src[srcWidth-1] + 3*src[srcWidth-1 + srcStride])>>2;

        dst+=dstStride*2;
        src+=srcStride;
    }

    // last line
    dst[0]= src[0];

    for (x=0; x<srcWidth-1; x++) {
        dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
        dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
    }
    dst[2*srcWidth-1]= src[srcWidth-1];
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 * Chrominance data is only taken from every second line, others are ignored.
 * FIXME: Write HQ version.
 */
static inline void uyvytoyv12_c(const uint8_t *src, uint8_t *ydst,
                                uint8_t *udst, uint8_t *vdst,
                                int width, int height,
                                int lumStride, int chromStride,
                                int srcStride)
{
    int y;
    const int chromWidth = width >> 1;
    for (y=0; y<height; y+=2) {
        int i;
        for (i=0; i<chromWidth; i++) {
            udst[i]     = src[4*i+0];
            ydst[2*i+0] = src[4*i+1];
            vdst[i]     = src[4*i+2];
            ydst[2*i+1] = src[4*i+3];
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0] = src[4*i+1];
            ydst[2*i+1] = src[4*i+3];
        }
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 2.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 * Chrominance data is only taken from every second line,
 * others are ignored in the C version.
 * FIXME: Write HQ version.
 */
void rgb24toyv12_c(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                   uint8_t *vdst, int width, int height, int lumStride,
                   int chromStride, int srcStride)
{
    int y;
    const int chromWidth = width >> 1;
    y=0;
    for (; y<height; y+=2) {
        int i;
        for (i=0; i<chromWidth; i++) {
            unsigned int b = src[6*i+0];
            unsigned int g = src[6*i+1];
            unsigned int r = src[6*i+2];

            unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            unsigned int V  =  ((RV*r + GV*g + BV*b)>>RGB2YUV_SHIFT) + 128;
            unsigned int U  =  ((RU*r + GU*g + BU*b)>>RGB2YUV_SHIFT) + 128;

            udst[i]     = U;
            vdst[i]     = V;
            ydst[2*i]   = Y;

            b = src[6*i+3];
            g = src[6*i+4];
            r = src[6*i+5];

            Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            ydst[2*i+1]     = Y;
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            unsigned int b = src[6*i+0];
            unsigned int g = src[6*i+1];
            unsigned int r = src[6*i+2];

            unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;

            ydst[2*i]     = Y;

            b = src[6*i+3];
            g = src[6*i+4];
            r = src[6*i+5];

            Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            ydst[2*i+1]     = Y;
        }
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
}

static void interleaveBytes_c(const uint8_t *src1, const uint8_t *src2,
                              uint8_t *dest, int width,
                              int height, int src1Stride,
                              int src2Stride, int dstStride)
{
    int h;

    for (h=0; h < height; h++) {
        int w;
        for (w=0; w < width; w++) {
            dest[2*w+0] = src1[w];
            dest[2*w+1] = src2[w];
        }
        dest += dstStride;
        src1 += src1Stride;
        src2 += src2Stride;
    }
}

static inline void vu9_to_vu12_c(const uint8_t *src1, const uint8_t *src2,
                                 uint8_t *dst1, uint8_t *dst2,
                                 int width, int height,
                                 int srcStride1, int srcStride2,
                                 int dstStride1, int dstStride2)
{
    int y;
    int x,w,h;
    w=width/2; h=height/2;
    for (y=0;y<h;y++) {
        const uint8_t* s1=src1+srcStride1*(y>>1);
        uint8_t* d=dst1+dstStride1*y;
        x=0;
        for (;x<w;x++) d[2*x]=d[2*x+1]=s1[x];
    }
    for (y=0;y<h;y++) {
        const uint8_t* s2=src2+srcStride2*(y>>1);
        uint8_t* d=dst2+dstStride2*y;
        x=0;
        for (;x<w;x++) d[2*x]=d[2*x+1]=s2[x];
    }
}

static inline void yvu9_to_yuy2_c(const uint8_t *src1, const uint8_t *src2,
                                  const uint8_t *src3, uint8_t *dst,
                                  int width, int height,
                                  int srcStride1, int srcStride2,
                                  int srcStride3, int dstStride)
{
    int x;
    int y,w,h;
    w=width/2; h=height;
    for (y=0;y<h;y++) {
        const uint8_t* yp=src1+srcStride1*y;
        const uint8_t* up=src2+srcStride2*(y>>2);
        const uint8_t* vp=src3+srcStride3*(y>>2);
        uint8_t* d=dst+dstStride*y;
        x=0;
        for (; x<w; x++) {
            const int x2 = x<<2;
            d[8*x+0] = yp[x2];
            d[8*x+1] = up[x];
            d[8*x+2] = yp[x2+1];
            d[8*x+3] = vp[x];
            d[8*x+4] = yp[x2+2];
            d[8*x+5] = up[x];
            d[8*x+6] = yp[x2+3];
            d[8*x+7] = vp[x];
        }
    }
}

static void extract_even_c(const uint8_t *src, uint8_t *dst, int count)
{
    dst +=   count;
    src += 2*count;
    count= - count;

    while(count<0) {
        dst[count]= src[2*count];
        count++;
    }
}

static void extract_even2_c(const uint8_t *src, uint8_t *dst0, uint8_t *dst1,
                            int count)
{
    dst0+=   count;
    dst1+=   count;
    src += 4*count;
    count= - count;
    while(count<0) {
        dst0[count]= src[4*count+0];
        dst1[count]= src[4*count+2];
        count++;
    }
}

static void extract_even2avg_c(const uint8_t *src0, const uint8_t *src1,
                               uint8_t *dst0, uint8_t *dst1, int count)
{
    dst0 +=   count;
    dst1 +=   count;
    src0 += 4*count;
    src1 += 4*count;
    count= - count;
    while(count<0) {
        dst0[count]= (src0[4*count+0]+src1[4*count+0])>>1;
        dst1[count]= (src0[4*count+2]+src1[4*count+2])>>1;
        count++;
    }
}

static void extract_odd2_c(const uint8_t *src, uint8_t *dst0, uint8_t *dst1,
                           int count)
{
    dst0+=   count;
    dst1+=   count;
    src += 4*count;
    count= - count;
    src++;
    while(count<0) {
        dst0[count]= src[4*count+0];
        dst1[count]= src[4*count+2];
        count++;
    }
}

static void extract_odd2avg_c(const uint8_t *src0, const uint8_t *src1,
                              uint8_t *dst0, uint8_t *dst1, int count)
{
    dst0 +=   count;
    dst1 +=   count;
    src0 += 4*count;
    src1 += 4*count;
    count= - count;
    src0++;
    src1++;
    while(count<0) {
        dst0[count]= (src0[4*count+0]+src1[4*count+0])>>1;
        dst1[count]= (src0[4*count+2]+src1[4*count+2])>>1;
        count++;
    }
}

static void yuyvtoyuv420_c(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                           const uint8_t *src, int width, int height,
                           int lumStride, int chromStride, int srcStride)
{
    int y;
    const int chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        extract_even_c(src, ydst, width);
        if(y&1) {
            extract_odd2avg_c(src - srcStride, src, udst, vdst, chromWidth);
            udst+= chromStride;
            vdst+= chromStride;
        }

        src += srcStride;
        ydst+= lumStride;
    }
}

static void yuyvtoyuv422_c(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                           const uint8_t *src, int width, int height,
                           int lumStride, int chromStride, int srcStride)
{
    int y;
    const int chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        extract_even_c(src, ydst, width);
        extract_odd2_c(src, udst, vdst, chromWidth);

        src += srcStride;
        ydst+= lumStride;
        udst+= chromStride;
        vdst+= chromStride;
    }
}

static void uyvytoyuv420_c(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                           const uint8_t *src, int width, int height,
                           int lumStride, int chromStride, int srcStride)
{
    int y;
    const int chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        extract_even_c(src + 1, ydst, width);
        if(y&1) {
            extract_even2avg_c(src - srcStride, src, udst, vdst, chromWidth);
            udst+= chromStride;
            vdst+= chromStride;
        }

        src += srcStride;
        ydst+= lumStride;
    }
}

static void uyvytoyuv422_c(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                           const uint8_t *src, int width, int height,
                           int lumStride, int chromStride, int srcStride)
{
    int y;
    const int chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        extract_even_c(src + 1, ydst, width);
        extract_even2_c(src, udst, vdst, chromWidth);

        src += srcStride;
        ydst+= lumStride;
        udst+= chromStride;
        vdst+= chromStride;
    }
}

static inline void rgb2rgb_init_c(void)
{
    rgb15to16          = rgb15to16_c;
    rgb15tobgr24       = rgb15tobgr24_c;
    rgb15to32          = rgb15to32_c;
    rgb16tobgr24       = rgb16tobgr24_c;
    rgb16to32          = rgb16to32_c;
    rgb16to15          = rgb16to15_c;
    rgb24tobgr16       = rgb24tobgr16_c;
    rgb24tobgr15       = rgb24tobgr15_c;
    rgb24tobgr32       = rgb24tobgr32_c;
    rgb32to16          = rgb32to16_c;
    rgb32to15          = rgb32to15_c;
    rgb32tobgr24       = rgb32tobgr24_c;
    rgb24to15          = rgb24to15_c;
    rgb24to16          = rgb24to16_c;
    rgb24tobgr24       = rgb24tobgr24_c;
    shuffle_bytes_2103 = shuffle_bytes_2103_c;
    rgb32tobgr16       = rgb32tobgr16_c;
    rgb32tobgr15       = rgb32tobgr15_c;
    yv12toyuy2         = yv12toyuy2_c;
    yv12touyvy         = yv12touyvy_c;
    yuv422ptoyuy2      = yuv422ptoyuy2_c;
    yuv422ptouyvy      = yuv422ptouyvy_c;
    yuy2toyv12         = yuy2toyv12_c;
    planar2x           = planar2x_c;
    rgb24toyv12        = rgb24toyv12_c;
    interleaveBytes    = interleaveBytes_c;
    vu9_to_vu12        = vu9_to_vu12_c;
    yvu9_to_yuy2       = yvu9_to_yuy2_c;

    uyvytoyuv420       = uyvytoyuv420_c;
    uyvytoyuv422       = uyvytoyuv422_c;
    yuyvtoyuv420       = yuyvtoyuv420_c;
    yuyvtoyuv422       = yuyvtoyuv422_c;
}
