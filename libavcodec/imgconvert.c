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
#include "avcodec.h"
#include "dsputil.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
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
        case PIX_FMT_BGR24:
            bgr24_to_yuv420p(dst->data[0], dst->data[1], dst->data[2], 
                             src->data[0], width, height);
            break;
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
    } else {
        return -1;
    }
    return 0;
}

/* filter parameters: [-1 4 2 4 -1] // 8 */
static void deinterlace_line(UINT8 *dst, UINT8 *src, int src_wrap,
                             int size)
{
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    int sum;
    UINT8 *s;

    for(;size > 0;size--) {
        s = src;
        sum = -s[0];
        s += src_wrap;
        sum += s[0] << 2;
        s += src_wrap;
        sum += s[0] << 1;
        s += src_wrap;
        sum += s[0] << 2;
        s += src_wrap;
        sum += -s[0];
        dst[0] = cm[(sum + 4) >> 3];
        dst++;
        src++;
    }
}

/* deinterlacing : 2 temporal taps, 3 spatial taps linear filter. The
   top field is copied as is, but the bottom field is deinterlaced
   against the top field. */
static void deinterlace_bottom_field(UINT8 *dst, int dst_wrap,
                                     UINT8 *src1, int src_wrap,
                                     int width, int height)
{
    UINT8 *src, *ptr;
    int y, y1, i;
    UINT8 *buf;

    buf= (UINT8*) malloc(5 * width);

    src = src1;
    for(y=0;y<height;y+=2) {
        /* copy top field line */
        memcpy(dst, src, width);
        dst += dst_wrap;
        src += (1 - 2) * src_wrap;
        y1 = y - 2;
        if (y1 >= 0 && (y1 + 4) < height) {
            /* fast case : no edges */
            deinterlace_line(dst, src, src_wrap, width);
        } else {
            /* in order to use the same function, we use an intermediate buffer */
            ptr = buf;
            for(i=0;i<5;i++) {
                if (y1 < 0)
                    memcpy(ptr, src1, width);
                else if (y1 >= height) 
                    memcpy(ptr, src1 + (height - 1) * src_wrap, width);
                else
                    memcpy(ptr, src1 + y1 * src_wrap, width);
                y1++;
                ptr += width;
            }
            deinterlace_line(dst, buf, width, width);
        }
        dst += dst_wrap;
        src += (2 + 1) * src_wrap;
    }
    free(buf);
}


/* deinterlace, return -1 if format not handled */
int avpicture_deinterlace(AVPicture *dst, AVPicture *src,
                          int pix_fmt, int width, int height)
{
    int i;

    if (pix_fmt != PIX_FMT_YUV420P &&
        pix_fmt != PIX_FMT_YUV422P &&
        pix_fmt != PIX_FMT_YUV444P)
        return -1;
    if ((width & 1) != 0 || (height & 3) != 0)
        return -1;
    
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
        deinterlace_bottom_field(dst->data[i], dst->linesize[i],
                                 src->data[i], src->linesize[i],
                                 width, height);
    }
    return 0;
}
