/*
 * Misc image convertion routines
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avcodec.h"
#include "dsputil.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#ifdef HAVE_MMX
#include "i386/mmx.h"
#endif
/* XXX: totally non optimized */

static void yuv422_to_yuv420p(UINT8 *lum, UINT8 *cb, UINT8 *cr,
                              UINT8 *src, int width, int height)
{
    int x, y;
    UINT8 *p = src;

    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            lum[0] = p[0];
            cb[0] = p[1];
            lum[1] = p[2];
            cr[0] = p[3];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        for(x=0;x<width;x+=2) {
            lum[0] = p[0];
            lum[1] = p[2];
            p += 4;
            lum += 2;
        }
    }
}

#define SCALEBITS 8
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)		((int) ((x) * (1L<<SCALEBITS) + 0.5))

static void rgb24_to_yuv420p(UINT8 *lum, UINT8 *cb, UINT8 *cr,
                              UINT8 *src, int width, int height)
{
    int wrap, wrap3, x, y;
    int r, g, b, r1, g1, b1;
    UINT8 *p;

    wrap = width;
    wrap3 = width * 3;
    p = src;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            r = p[0];
            g = p[1];
            b = p[2];
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[3];
            g = p[4];
            b = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p += wrap3;
            lum += wrap;

            r = p[0];
            g = p[1];
            b = p[2];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[3];
            g = p[4];
            b = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 + 
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 - 
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p += -wrap3 + 2 * 3;
            lum += -wrap + 2;
        }
        p += wrap3;
        lum += wrap;
    }
}

static void rgba32_to_yuv420p(UINT8 *lum, UINT8 *cb, UINT8 *cr,
                              UINT8 *src, int width, int height)
{
    int wrap, wrap4, x, y;
    int r, g, b, r1, g1, b1;
    UINT8 *p;

    wrap = width;
    wrap4 = width * 4;
    p = src;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            r = p[0];
            g = p[1];
            b = p[2];
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[4];
            g = p[5];
            b = p[6];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p += wrap4;
            lum += wrap;

            r = p[0];
            g = p[1];
            b = p[2];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[4];
            g = p[5];
            b = p[6];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 + 
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 - 
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p += -wrap4 + 2 * 4;
            lum += -wrap + 2;
        }
        p += wrap4;
        lum += wrap;
    }
}

#define rgb565_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0800,31, 0x0020,63,0x0001,31)
#define rgb555_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0400,31, 0x0020,31,0x0001,31)
#define rgb5551_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0800,31, 0x0040,31,0x0002,31)
#define bgr565_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0001,31, 0x0020,63,0x0800,31)
#define bgr555_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0001,31, 0x0020,31,0x0400,31)
#define gbr565_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0001,31, 0x0800,31,0x0040,63)
#define gbr555_to_yuv420p(lum,cb,cr,src,width,height) rgbmisc_to_yuv420p((lum),(cb),(cr),(src),(width),(height),0x0001,31, 0x0400,31,0x0020,31)

static void rgbmisc_to_yuv420p
  (UINT8 *lum, UINT8 *cb, UINT8 *cr,
   UINT8 *src, int width, int height,
   
   UINT16 R_LOWMASK, UINT16 R_MAX,
   UINT16 G_LOWMASK, UINT16 G_MAX,
   UINT16 B_LOWMASK, UINT16 B_MAX
  )
{
    int wrap, wrap2, x, y;
    int r, g, b, r1, g1, b1;
    UINT8 *p;
    UINT16 pixel;

    wrap = width;
    wrap2 = width * 2;
    p = src;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            pixel = p[0] | (p[1]<<8);
            r = (((pixel/R_LOWMASK) & R_MAX) * (0x100 / (R_MAX+1)));
            g = (((pixel/G_LOWMASK) & G_MAX) * (0x100 / (G_MAX+1)));
            b = (((pixel/B_LOWMASK) & B_MAX) * (0x100 / (B_MAX+1)));
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;

            pixel = p[2] | (p[3]<<8);
            r = (((pixel/R_LOWMASK) & R_MAX) * (0x100 / (R_MAX+1)));
            g = (((pixel/G_LOWMASK) & G_MAX) * (0x100 / (G_MAX+1)));
            b = (((pixel/B_LOWMASK) & B_MAX) * (0x100 / (B_MAX+1)));
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p += wrap2;
            lum += wrap;

            pixel = p[0] | (p[1]<<8);
            r = (((pixel/R_LOWMASK) & R_MAX) * (0x100 / (R_MAX+1)));
            g = (((pixel/G_LOWMASK) & G_MAX) * (0x100 / (G_MAX+1)));
            b = (((pixel/B_LOWMASK) & B_MAX) * (0x100 / (B_MAX+1)));
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            pixel = p[2] | (p[3]<<8);
            r = (((pixel/R_LOWMASK) & R_MAX) * (0x100 / (R_MAX+1)));
            g = (((pixel/G_LOWMASK) & G_MAX) * (0x100 / (G_MAX+1)));
            b = (((pixel/B_LOWMASK) & B_MAX) * (0x100 / (B_MAX+1)));
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 + 
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 - 
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p += -wrap2 + 2 * 2;
            lum += -wrap + 2;
        }
        p += wrap2;
        lum += wrap;
    }
}


static void bgr24_to_yuv420p(UINT8 *lum, UINT8 *cb, UINT8 *cr,
                              UINT8 *src, int width, int height)
{
    int wrap, wrap3, x, y;
    int r, g, b, r1, g1, b1;
    UINT8 *p;

    wrap = width;
    wrap3 = width * 3;
    p = src;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            b = p[0];
            g = p[1];
            r = p[2];
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            b = p[3];
            g = p[4];
            r = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p += wrap3;
            lum += wrap;

            b = p[0];
            g = p[1];
            r = p[2];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            b = p[3];
            g = p[4];
            r = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 + 
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 - 
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p += -wrap3 + 2 * 3;
            lum += -wrap + 2;
        }
        p += wrap3;
        lum += wrap;
    }
}

static void bgra32_to_yuv420p(UINT8 *lum, UINT8 *cb, UINT8 *cr,
                              UINT8 *src, int width, int height)
{
    int wrap, wrap4, x, y;
    int r, g, b, r1, g1, b1;
    UINT8 *p;

    wrap = width;
    wrap4 = width * 4;
    p = src;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            b = p[0];
            g = p[1];
            r = p[2];
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            b = p[4];
            g = p[5];
            r = p[6];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p += wrap4;
            lum += wrap;

            b = p[0];
            g = p[1];
            r = p[2];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            b = p[4];
            g = p[5];
            r = p[6];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g + 
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 + 
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 - 
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p += -wrap4 + 2 * 4;
            lum += -wrap + 2;
        }
        p += wrap4;
        lum += wrap;
    }
}

/* XXX: use generic filter ? */
/* 1x2 -> 1x1 */
static void shrink2(UINT8 *dst, int dst_wrap, 
                    UINT8 *src, int src_wrap,
                    int width, int height)
{
    int w;
    UINT8 *s1, *s2, *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[0] = (s1[0] + s2[0]) >> 1;
            d[1] = (s1[1] + s2[1]) >> 1;
            d[2] = (s1[2] + s2[2]) >> 1;
            d[3] = (s1[3] + s2[3]) >> 1;
            s1 += 4;
            s2 += 4;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = (s1[0] + s2[0]) >> 1;
            s1++;
            s2++;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

/* 2x2 -> 1x1 */
static void shrink22(UINT8 *dst, int dst_wrap, 
                     UINT8 *src, int src_wrap,
                     int width, int height)
{
    int w;
    UINT8 *s1, *s2, *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 1;
            d[1] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 1;
            d[2] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 1;
            d[3] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 1;
            s1 += 8;
            s2 += 8;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 1;
            s1 += 2;
            s2 += 2;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

/* 1x1 -> 2x2 */
static void grow22(UINT8 *dst, int dst_wrap,
                     UINT8 *src, int src_wrap,
                     int width, int height)
{
    int w;
    UINT8 *s1, *d;

    for(;height > 0; height--) {
        s1 = src;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[1] = d[0] = s1[0];
            d[3] = d[2] = s1[1];
            s1 += 2;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = s1[0];
            s1 ++;
            d++;
        }
        if (height%2)
            src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x2 -> 2x1. width and height are given for the source picture */
static void conv411(UINT8 *dst, int dst_wrap, 
                    UINT8 *src, int src_wrap,
                    int width, int height)
{
    int w, c;
    UINT8 *s1, *s2, *d;

    for(;height > 0; height -= 2) {
        s1 = src;
        s2 = src + src_wrap;
        d = dst;
        for(w = width;w > 0; w--) {
            c = (s1[0] + s2[0]) >> 1;
            d[0] = c;
            d[1] = c;
            s1++;
            s2++;
            d += 2;
        }
        src += src_wrap * 2;
        dst += dst_wrap;
    }
}

static void img_copy(UINT8 *dst, int dst_wrap, 
                     UINT8 *src, int src_wrap,
                     int width, int height)
{
    for(;height > 0; height--) {
        memcpy(dst, src, width);
        dst += dst_wrap;
        src += src_wrap;
    }
}

#define SCALE_BITS 10

#define C_Y  (76309 >> (16 - SCALE_BITS))
#define C_RV (117504 >> (16 - SCALE_BITS))
#define C_BU (138453 >> (16 - SCALE_BITS))
#define C_GU (13954 >> (16 - SCALE_BITS))
#define C_GV (34903 >> (16 - SCALE_BITS))

#define RGBOUT(r, g, b, y1)\
{\
    y = (y1 - 16) * C_Y;\
    r = cm[(y + r_add) >> SCALE_BITS];\
    g = cm[(y + g_add) >> SCALE_BITS];\
    b = cm[(y + b_add) >> SCALE_BITS];\
}

/* XXX: no chroma interpolating is done */
static void yuv420p_to_bgra32(AVPicture *dst, AVPicture *src, 
                             int width, int height)
{
    UINT8 *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *d, *d1, *d2;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = width >> 1;
    for(;height > 0; height -= 2) {
        d1 = d;
        d2 = d + dst->linesize[0];
        y2_ptr = y1_ptr + src->linesize[0];
        for(w = width2; w > 0; w --) {
            cb = cb_ptr[0] - 128;
            cr = cr_ptr[0] - 128;
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));
            
            /* output 4 pixels */
            RGBOUT(d1[2], d1[1], d1[0], y1_ptr[0]);
            RGBOUT(d1[6], d1[5], d1[4], y1_ptr[1]);
            RGBOUT(d2[2], d2[1], d2[0], y2_ptr[0]);
            RGBOUT(d2[6], d2[5], d2[4], y2_ptr[1]);

            d1[3] = d1[7] = d2[3] = d2[7] = 255;

            d1 += 8;
            d2 += 8;
            y1_ptr += 2;
            y2_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        d += 2 * dst->linesize[0];
        y1_ptr += 2 * src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
}

/* XXX: no chroma interpolating is done */
static void yuv420p_to_rgba32(AVPicture *dst, AVPicture *src, 
                             int width, int height)
{
    UINT8 *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *d, *d1, *d2;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = width >> 1;
    for(;height > 0; height -= 2) {
        d1 = d;
        d2 = d + dst->linesize[0];
        y2_ptr = y1_ptr + src->linesize[0];
        for(w = width2; w > 0; w --) {
            cb = cb_ptr[0] - 128;
            cr = cr_ptr[0] - 128;
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));
            
            /* output 4 pixels */
            RGBOUT(d1[0], d1[1], d1[2], y1_ptr[0]);
            RGBOUT(d1[4], d1[5], d1[6], y1_ptr[1]);
            RGBOUT(d2[0], d2[1], d2[2], y2_ptr[0]);
            RGBOUT(d2[4], d2[5], d2[6], y2_ptr[1]);

            d1[3] = d1[7] = d2[3] = d2[7] = 255;

            d1 += 8;
            d2 += 8;
            y1_ptr += 2;
            y2_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        d += 2 * dst->linesize[0];
        y1_ptr += 2 * src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
}

/* XXX: no chroma interpolating is done */
static void yuv420p_to_rgb24(AVPicture *dst, AVPicture *src, 
                             int width, int height)
{
    UINT8 *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *d, *d1, *d2;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = width >> 1;
    for(;height > 0; height -= 2) {
        d1 = d;
        d2 = d + dst->linesize[0];
        y2_ptr = y1_ptr + src->linesize[0];
        for(w = width2; w > 0; w --) {
            cb = cb_ptr[0] - 128;
            cr = cr_ptr[0] - 128;
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));
            
            /* output 4 pixels */
            RGBOUT(d1[0], d1[1], d1[2], y1_ptr[0]);
            RGBOUT(d1[3], d1[4], d1[5], y1_ptr[1]);
            RGBOUT(d2[0], d2[1], d2[2], y2_ptr[0]);
            RGBOUT(d2[3], d2[4], d2[5], y2_ptr[1]);

            d1 += 6;
            d2 += 6;
            y1_ptr += 2;
            y2_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        d += 2 * dst->linesize[0];
        y1_ptr += 2 * src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
}

/* XXX: no chroma interpolating is done */
static void yuv422p_to_rgb24(AVPicture *dst, AVPicture *src, 
                             int width, int height)
{
    UINT8 *y1_ptr, *cb_ptr, *cr_ptr, *d, *d1;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = width >> 1;
    for(;height > 0; height --) {
        d1 = d;
        for(w = width2; w > 0; w --) {
            cb = cb_ptr[0] - 128;
            cr = cr_ptr[0] - 128;
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));
            
            /* output 2 pixels */
            RGBOUT(d1[0], d1[1], d1[2], y1_ptr[0]);
            RGBOUT(d1[3], d1[4], d1[5], y1_ptr[1]);

            d1 += 6;
            y1_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        d += dst->linesize[0];
        y1_ptr += src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
}

/* XXX: always use linesize. Return -1 if not supported */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                AVPicture *src, int pix_fmt, 
                int width, int height)
{
    int i;

    if (dst_pix_fmt == pix_fmt) {
        switch(pix_fmt) {
        case PIX_FMT_YUV420P:
            for(i=0;i<3;i++) {
                if (i == 1) {
                    width >>= 1;
                    height >>= 1;
                }
                img_copy(dst->data[i], dst->linesize[i],
                         src->data[i], src->linesize[i],
                         width, height);
            }
            break;
        default:
            return -1;
        }
    } else if (dst_pix_fmt == PIX_FMT_YUV420P) {
        
        switch(pix_fmt) {
        case PIX_FMT_YUV411P:
            img_copy(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     width, height);
            conv411(dst->data[1], dst->linesize[1],
                    src->data[1], src->linesize[1],
                    width / 4, height);
            conv411(dst->data[2], dst->linesize[2],
                    src->data[2], src->linesize[2],
                    width / 4, height);
            break;
        case PIX_FMT_YUV410P:
            img_copy(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     width, height);
            grow22(dst->data[1], dst->linesize[1],
                     src->data[1], src->linesize[1],
                     width/2, height/2);
            grow22(dst->data[2], dst->linesize[2],
                     src->data[2], src->linesize[2],
                     width/2, height/2);
            break;
        case PIX_FMT_YUV420P:
            for(i=0;i<3;i++) {
                img_copy(dst->data[i], dst->linesize[i],
                         src->data[i], src->linesize[i],
                         width, height);
            }
            break;
        case PIX_FMT_YUV422P:
            img_copy(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     width, height);
            width >>= 1;
            height >>= 1;
            for(i=1;i<3;i++) {
                shrink2(dst->data[i], dst->linesize[i],
                        src->data[i], src->linesize[i],
                        width, height);
            }
            break;
        case PIX_FMT_YUV444P:
            img_copy(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     width, height);
            width >>= 1;
            height >>= 1;
            for(i=1;i<3;i++) {
                shrink22(dst->data[i], dst->linesize[i],
                         src->data[i], src->linesize[i],
                         width, height);
            }
            break;
        case PIX_FMT_YUV422:
            yuv422_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                              src->data[0], width, height);
            break;
        case PIX_FMT_RGB24:
            rgb24_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_RGBA32:
            rgba32_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_BGR24:
            bgr24_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_BGRA32:
            bgra32_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_RGB565:
            rgb565_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_RGB555:
            rgb555_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
/*        case PIX_FMT_RGB5551:
            rgb5551_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;*/
        case PIX_FMT_BGR565:
            bgr565_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_BGR555:
            bgr555_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
/*        case PIX_FMT_GBR565:
            gbr565_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
        case PIX_FMT_GBR555:
            gbr555_to_yuv420p(dst->data[0], dst->data[1], dst->data[2],
                             src->data[0], width, height);
            break;*/
        default:
            return -1;
        }
    } else if (dst_pix_fmt == PIX_FMT_RGB24) {
        switch(pix_fmt) {
        case PIX_FMT_YUV420P:
            yuv420p_to_rgb24(dst, src, width, height);
            break;
        case PIX_FMT_YUV422P:
            yuv422p_to_rgb24(dst, src, width, height);
            break;
        default:
            return -1;
        }
    } else if (dst_pix_fmt == PIX_FMT_RGBA32) {
        switch(pix_fmt) {
        case PIX_FMT_YUV420P:
            yuv420p_to_rgba32(dst, src, width, height);
            break;
        default:
            return -1;
        }
    } else if (dst_pix_fmt == PIX_FMT_BGRA32) {
        switch(pix_fmt) {
        case PIX_FMT_YUV420P:
            yuv420p_to_bgra32(dst, src, width, height);
            break;
        default:
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}


#ifdef HAVE_MMX
#define DEINT_INPLACE_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    movd_r2m(mm2,lum_m4[0]);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum_m2[0]);

#define DEINT_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,dst[0]);
#endif

/* filter parameters: [-1 4 2 4 -1] // 8 */
static void deinterlace_line(UINT8 *dst, UINT8 *lum_m4, UINT8 *lum_m3, UINT8 *lum_m2, UINT8 *lum_m1, UINT8 *lum,
                                int size)
{
#ifndef HAVE_MMX
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    int sum;

    for(;size > 0;size--) {
        sum = -lum_m4[0];
        sum += lum_m3[0] << 2;
        sum += lum_m2[0] << 1;
        sum += lum_m1[0] << 2;
        sum += -lum[0];
        dst[0] = cm[(sum + 4) >> 3];
        lum_m4++;
        lum_m3++;
        lum_m2++;
        lum_m1++;
        lum++;
        dst++;
    }
#else

    for (;size > 3; size-=4) {
        DEINT_LINE_LUM
        lum_m4+=4;
        lum_m3+=4;
        lum_m2+=4;
        lum_m1+=4;
        lum+=4;
        dst+=4;
    }
#endif
}
static void deinterlace_line_inplace(UINT8 *lum_m4, UINT8 *lum_m3, UINT8 *lum_m2, UINT8 *lum_m1, UINT8 *lum,
                             int size)
{
#ifndef HAVE_MMX
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    int sum;

    for(;size > 0;size--) {
        sum = -lum_m4[0];
        sum += lum_m3[0] << 2;
        sum += lum_m2[0] << 1;
        lum_m4[0]=lum_m2[0];
        sum += lum_m1[0] << 2;
        sum += -lum[0];
        lum_m2[0] = cm[(sum + 4) >> 3];
        lum_m4++;
        lum_m3++;
        lum_m2++;
        lum_m1++;
        lum++;
    }
#else

    for (;size > 3; size-=4) {
        DEINT_INPLACE_LINE_LUM
        lum_m4+=4;
        lum_m3+=4;
        lum_m2+=4;
        lum_m1+=4;
        lum+=4;
    }
#endif
}

/* deinterlacing : 2 temporal taps, 3 spatial taps linear filter. The
   top field is copied as is, but the bottom field is deinterlaced
   against the top field. */
static void deinterlace_bottom_field(UINT8 *dst, int dst_wrap,
                                    UINT8 *src1, int src_wrap,
                                    int width, int height)
{
    UINT8 *src_m2, *src_m1, *src_0, *src_p1, *src_p2;
    int y;

    src_m2 = src1;
    src_m1 = src1;
    src_0=&src_m1[src_wrap];
    src_p1=&src_0[src_wrap];
    src_p2=&src_p1[src_wrap];
    for(y=0;y<(height-2);y+=2) {
        memcpy(dst,src_m1,width);
        dst += dst_wrap;
        deinterlace_line(dst,src_m2,src_m1,src_0,src_p1,src_p2,width);
        src_m2 = src_0;
        src_m1 = src_p1;
        src_0 = src_p2;
        src_p1 += 2*src_wrap;
        src_p2 += 2*src_wrap;
        dst += dst_wrap;
    }
    memcpy(dst,src_m1,width);
    dst += dst_wrap;
    /* do last line */
    deinterlace_line(dst,src_m2,src_m1,src_0,src_0,src_0,width);
}

static void deinterlace_bottom_field_inplace(UINT8 *src1, int src_wrap,
                                     int width, int height)
{
    UINT8 *src_m1, *src_0, *src_p1, *src_p2;
    int y;
    UINT8 *buf;
    buf = (UINT8*)av_malloc(width);

    src_m1 = src1;
    memcpy(buf,src_m1,width);
    src_0=&src_m1[src_wrap];
    src_p1=&src_0[src_wrap];
    src_p2=&src_p1[src_wrap];
    for(y=0;y<(height-2);y+=2) {
        deinterlace_line_inplace(buf,src_m1,src_0,src_p1,src_p2,width);
        src_m1 = src_p1;
        src_0 = src_p2;
        src_p1 += 2*src_wrap;
        src_p2 += 2*src_wrap;
    }
    /* do last line */
    deinterlace_line_inplace(buf,src_m1,src_0,src_0,src_0,width);
    av_free(buf);
}


/* deinterlace - if not supported return -1 */
int avpicture_deinterlace(AVPicture *dst, AVPicture *src,
                          int pix_fmt, int width, int height)
{
    int i;

    if (pix_fmt != PIX_FMT_YUV420P &&
        pix_fmt != PIX_FMT_YUV422P &&
        pix_fmt != PIX_FMT_YUV444P)
        return -1;
    if ((width & 3) != 0 || (height & 3) != 0)
        return -1;

#ifdef HAVE_MMX
    {
        mmx_t rounder;
        rounder.uw[0]=4;
        rounder.uw[1]=4;
        rounder.uw[2]=4;
        rounder.uw[3]=4;
        pxor_r2r(mm7,mm7);
        movq_m2r(rounder,mm6);
    }
#endif

    
    for(i=0;i<3;i++) {
        if (i == 1) {
            switch(pix_fmt) {
            case PIX_FMT_YUV420P:
                width >>= 1;
                height >>= 1;
                break;
            case PIX_FMT_YUV422P:
                width >>= 1;
                break;
            default:
                break;
            }
        }
        if (src == dst) {
            deinterlace_bottom_field_inplace(src->data[i], src->linesize[i],
                                 width, height);
        } else {
            deinterlace_bottom_field(dst->data[i],dst->linesize[i],
                                        src->data[i], src->linesize[i],
                                        width, height);
        }
    }
#ifdef HAVE_MMX
    emms();
#endif
    return 0;
}

#undef FIX
