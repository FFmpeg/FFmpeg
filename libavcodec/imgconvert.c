/*
 * Misc image convertion routines
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "avcodec.h"

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

int img_convert_to_yuv420(UINT8 *img_out, UINT8 *img, 
                          int pix_fmt, int width, int height)
{
    UINT8 *pict;
    int size, size_out;
    UINT8 *picture[3];

    pict = img_out;
    size = width * height;
    size_out = (size * 3) / 2;
    picture[0] = pict;
    picture[1] = pict + size;
    picture[2] = picture[1] + (size / 4);

    switch(pix_fmt) {
    case PIX_FMT_YUV420P:
        memcpy(pict, img, size_out);
        break;
    case PIX_FMT_YUV422:
        yuv422_to_yuv420p(picture[0], picture[1], picture[2], 
                          img, width, height);
        break;
    case PIX_FMT_RGB24:
        rgb24_to_yuv420p(picture[0], picture[1], picture[2], 
                         img, width, height);
        break;
    case PIX_FMT_BGR24:
        bgr24_to_yuv420p(picture[0], picture[1], picture[2], 
                         img, width, height);
        break;
    }
    return size_out;
}
