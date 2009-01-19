/*
 * templates for image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

#ifndef RGB_OUT
#define RGB_OUT(d, r, g, b) RGBA_OUT(d, r, g, b, 0xff)
#endif

static void glue(yuv420p_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
    const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr;
    uint8_t *d, *d1, *d2;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = (width + 1) >> 1;
    for(;height >= 2; height -= 2) {
        d1 = d;
        d2 = d + dst->linesize[0];
        y2_ptr = y1_ptr + src->linesize[0];
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1_CCIR(cb_ptr[0], cr_ptr[0]);
            /* output 4 pixels */
            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[1]);
            RGB_OUT(d1 + BPP, r, g, b);

            YUV_TO_RGB2_CCIR(r, g, b, y2_ptr[0]);
            RGB_OUT(d2, r, g, b);

            YUV_TO_RGB2_CCIR(r, g, b, y2_ptr[1]);
            RGB_OUT(d2 + BPP, r, g, b);

            d1 += 2 * BPP;
            d2 += 2 * BPP;

            y1_ptr += 2;
            y2_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        /* handle odd width */
        if (w) {
            YUV_TO_RGB1_CCIR(cb_ptr[0], cr_ptr[0]);
            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2_CCIR(r, g, b, y2_ptr[0]);
            RGB_OUT(d2, r, g, b);
            d1 += BPP;
            d2 += BPP;
            y1_ptr++;
            y2_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
        d += 2 * dst->linesize[0];
        y1_ptr += 2 * src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
    /* handle odd height */
    if (height) {
        d1 = d;
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1_CCIR(cb_ptr[0], cr_ptr[0]);
            /* output 2 pixels */
            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[1]);
            RGB_OUT(d1 + BPP, r, g, b);

            d1 += 2 * BPP;

            y1_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        /* handle width */
        if (w) {
            YUV_TO_RGB1_CCIR(cb_ptr[0], cr_ptr[0]);
            /* output 2 pixels */
            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            y1_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
    }
}

static void glue(yuvj420p_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                         int width, int height)
{
    const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr;
    uint8_t *d, *d1, *d2;
    int w, y, cb, cr, r_add, g_add, b_add, width2;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    width2 = (width + 1) >> 1;
    for(;height >= 2; height -= 2) {
        d1 = d;
        d2 = d + dst->linesize[0];
        y2_ptr = y1_ptr + src->linesize[0];
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1(cb_ptr[0], cr_ptr[0]);
            /* output 4 pixels */
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2(r, g, b, y1_ptr[1]);
            RGB_OUT(d1 + BPP, r, g, b);

            YUV_TO_RGB2(r, g, b, y2_ptr[0]);
            RGB_OUT(d2, r, g, b);

            YUV_TO_RGB2(r, g, b, y2_ptr[1]);
            RGB_OUT(d2 + BPP, r, g, b);

            d1 += 2 * BPP;
            d2 += 2 * BPP;

            y1_ptr += 2;
            y2_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        /* handle odd width */
        if (w) {
            YUV_TO_RGB1(cb_ptr[0], cr_ptr[0]);
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2(r, g, b, y2_ptr[0]);
            RGB_OUT(d2, r, g, b);
            d1 += BPP;
            d2 += BPP;
            y1_ptr++;
            y2_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
        d += 2 * dst->linesize[0];
        y1_ptr += 2 * src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width2;
        cr_ptr += src->linesize[2] - width2;
    }
    /* handle odd height */
    if (height) {
        d1 = d;
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1(cb_ptr[0], cr_ptr[0]);
            /* output 2 pixels */
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);

            YUV_TO_RGB2(r, g, b, y1_ptr[1]);
            RGB_OUT(d1 + BPP, r, g, b);

            d1 += 2 * BPP;

            y1_ptr += 2;
            cb_ptr++;
            cr_ptr++;
        }
        /* handle width */
        if (w) {
            YUV_TO_RGB1(cb_ptr[0], cr_ptr[0]);
            /* output 2 pixels */
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            y1_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
    }
}

static void glue(RGB_NAME, _to_yuv420p)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
    int wrap, wrap3, width2;
    int r, g, b, r1, g1, b1, w;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;

    lum = dst->data[0];
    cb = dst->data[1];
    cr = dst->data[2];

    width2 = (width + 1) >> 1;
    wrap = dst->linesize[0];
    wrap3 = src->linesize[0];
    p = src->data[0];
    for(;height>=2;height -= 2) {
        for(w = width; w >= 2; w -= 2) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y_CCIR(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y_CCIR(r, g, b);
            p += wrap3;
            lum += wrap;

            RGB_IN(r, g, b, p);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = RGB_TO_Y_CCIR(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y_CCIR(r, g, b);

            cb[0] = RGB_TO_U_CCIR(r1, g1, b1, 2);
            cr[0] = RGB_TO_V_CCIR(r1, g1, b1, 2);

            cb++;
            cr++;
            p += -wrap3 + 2 * BPP;
            lum += -wrap + 2;
        }
        if (w) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y_CCIR(r, g, b);
            p += wrap3;
            lum += wrap;
            RGB_IN(r, g, b, p);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = RGB_TO_Y_CCIR(r, g, b);
            cb[0] = RGB_TO_U_CCIR(r1, g1, b1, 1);
            cr[0] = RGB_TO_V_CCIR(r1, g1, b1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        p += wrap3 + (wrap3 - width * BPP);
        lum += wrap + (wrap - width);
        cb += dst->linesize[1] - width2;
        cr += dst->linesize[2] - width2;
    }
    /* handle odd height */
    if (height) {
        for(w = width; w >= 2; w -= 2) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y_CCIR(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y_CCIR(r, g, b);
            cb[0] = RGB_TO_U_CCIR(r1, g1, b1, 1);
            cr[0] = RGB_TO_V_CCIR(r1, g1, b1, 1);
            cb++;
            cr++;
            p += 2 * BPP;
           lum += 2;
        }
        if (w) {
            RGB_IN(r, g, b, p);
            lum[0] = RGB_TO_Y_CCIR(r, g, b);
            cb[0] = RGB_TO_U_CCIR(r, g, b, 0);
            cr[0] = RGB_TO_V_CCIR(r, g, b, 0);
        }
    }
}

static void glue(RGB_NAME, _to_gray)(AVPicture *dst, const AVPicture *src,
                                     int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int r, g, b, dst_wrap, src_wrap;
    int x, y;

    p = src->data[0];
    src_wrap = src->linesize[0] - BPP * width;

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - width;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            RGB_IN(r, g, b, p);
            q[0] = RGB_TO_Y(r, g, b);
            q++;
            p += BPP;
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

static void glue(gray_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                     int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int r, dst_wrap, src_wrap;
    int x, y;

    p = src->data[0];
    src_wrap = src->linesize[0] - width;

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - BPP * width;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            r = p[0];
            RGB_OUT(q, r, r, r);
            q += BPP;
            p ++;
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

static void glue(pal8_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                     int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int r, g, b, dst_wrap, src_wrap;
    int x, y;
    uint32_t v;
    const uint32_t *palette;

    p = src->data[0];
    src_wrap = src->linesize[0] - width;
    palette = (uint32_t *)src->data[1];

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - BPP * width;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            v = palette[p[0]];
            r = (v >> 16) & 0xff;
            g = (v >> 8) & 0xff;
            b = (v) & 0xff;
#ifdef RGBA_OUT
            {
                int a;
                a = (v >> 24) & 0xff;
                RGBA_OUT(q, r, g, b, a);
            }
#else
            RGB_OUT(q, r, g, b);
#endif
            q += BPP;
            p ++;
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

// RGB24 has optimized routines
#if !defined(FMT_RGB32) && !defined(FMT_RGB24)
/* alpha support */

static void glue(rgb32_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                      int width, int height)
{
    const uint8_t *s;
    uint8_t *d;
    int src_wrap, dst_wrap, j, y;
    unsigned int v, r, g, b;
#ifdef RGBA_OUT
    unsigned int a;
#endif

    s = src->data[0];
    src_wrap = src->linesize[0] - width * 4;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width * BPP;

    for(y=0;y<height;y++) {
        for(j = 0;j < width; j++) {
            v = ((const uint32_t *)(s))[0];
            r = (v >> 16) & 0xff;
            g = (v >> 8) & 0xff;
            b = v & 0xff;
#ifdef RGBA_OUT
            a = (v >> 24) & 0xff;
            RGBA_OUT(d, r, g, b, a);
#else
            RGB_OUT(d, r, g, b);
#endif
            s += 4;
            d += BPP;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void glue(RGB_NAME, _to_rgb32)(AVPicture *dst, const AVPicture *src,
                                       int width, int height)
{
    const uint8_t *s;
    uint8_t *d;
    int src_wrap, dst_wrap, j, y;
    unsigned int r, g, b;
#ifdef RGBA_IN
    unsigned int a;
#endif

    s = src->data[0];
    src_wrap = src->linesize[0] - width * BPP;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width * 4;

    for(y=0;y<height;y++) {
        for(j = 0;j < width; j++) {
#ifdef RGBA_IN
            RGBA_IN(r, g, b, a, s);
            ((uint32_t *)(d))[0] = (a << 24) | (r << 16) | (g << 8) | b;
#else
            RGB_IN(r, g, b, s);
            ((uint32_t *)(d))[0] = (0xff << 24) | (r << 16) | (g << 8) | b;
#endif
            d += 4;
            s += BPP;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

#endif /* !defined(FMT_RGB32) */

#ifndef FMT_RGB24

static void glue(rgb24_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                      int width, int height)
{
    const uint8_t *s;
    uint8_t *d;
    int src_wrap, dst_wrap, j, y;
    unsigned int r, g, b;

    s = src->data[0];
    src_wrap = src->linesize[0] - width * 3;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width * BPP;

    for(y=0;y<height;y++) {
        for(j = 0;j < width; j++) {
            r = s[0];
            g = s[1];
            b = s[2];
            RGB_OUT(d, r, g, b);
            s += 3;
            d += BPP;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void glue(RGB_NAME, _to_rgb24)(AVPicture *dst, const AVPicture *src,
                                      int width, int height)
{
    const uint8_t *s;
    uint8_t *d;
    int src_wrap, dst_wrap, j, y;
    unsigned int r, g , b;

    s = src->data[0];
    src_wrap = src->linesize[0] - width * BPP;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width * 3;

    for(y=0;y<height;y++) {
        for(j = 0;j < width; j++) {
            RGB_IN(r, g, b, s)
            d[0] = r;
            d[1] = g;
            d[2] = b;
            d += 3;
            s += BPP;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

#endif /* !FMT_RGB24 */

#ifdef FMT_RGB24

static void yuv444p_to_rgb24(AVPicture *dst, const AVPicture *src,
                             int width, int height)
{
    const uint8_t *y1_ptr, *cb_ptr, *cr_ptr;
    uint8_t *d, *d1;
    int w, y, cb, cr, r_add, g_add, b_add;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    for(;height > 0; height --) {
        d1 = d;
        for(w = width; w > 0; w--) {
            YUV_TO_RGB1_CCIR(cb_ptr[0], cr_ptr[0]);

            YUV_TO_RGB2_CCIR(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            y1_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
        d += dst->linesize[0];
        y1_ptr += src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width;
        cr_ptr += src->linesize[2] - width;
    }
}

static void yuvj444p_to_rgb24(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *y1_ptr, *cb_ptr, *cr_ptr;
    uint8_t *d, *d1;
    int w, y, cb, cr, r_add, g_add, b_add;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    y1_ptr = src->data[0];
    cb_ptr = src->data[1];
    cr_ptr = src->data[2];
    for(;height > 0; height --) {
        d1 = d;
        for(w = width; w > 0; w--) {
            YUV_TO_RGB1(cb_ptr[0], cr_ptr[0]);

            YUV_TO_RGB2(r, g, b, y1_ptr[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            y1_ptr++;
            cb_ptr++;
            cr_ptr++;
        }
        d += dst->linesize[0];
        y1_ptr += src->linesize[0] - width;
        cb_ptr += src->linesize[1] - width;
        cr_ptr += src->linesize[2] - width;
    }
}

static void rgb24_to_yuv444p(AVPicture *dst, const AVPicture *src,
                             int width, int height)
{
    int src_wrap, x, y;
    int r, g, b;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;

    lum = dst->data[0];
    cb = dst->data[1];
    cr = dst->data[2];

    src_wrap = src->linesize[0] - width * BPP;
    p = src->data[0];
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            RGB_IN(r, g, b, p);
            lum[0] = RGB_TO_Y_CCIR(r, g, b);
            cb[0] = RGB_TO_U_CCIR(r, g, b, 0);
            cr[0] = RGB_TO_V_CCIR(r, g, b, 0);
            p += BPP;
            cb++;
            cr++;
            lum++;
        }
        p += src_wrap;
        lum += dst->linesize[0] - width;
        cb += dst->linesize[1] - width;
        cr += dst->linesize[2] - width;
    }
}

static void rgb24_to_yuvj420p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int wrap, wrap3, width2;
    int r, g, b, r1, g1, b1, w;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;

    lum = dst->data[0];
    cb = dst->data[1];
    cr = dst->data[2];

    width2 = (width + 1) >> 1;
    wrap = dst->linesize[0];
    wrap3 = src->linesize[0];
    p = src->data[0];
    for(;height>=2;height -= 2) {
        for(w = width; w >= 2; w -= 2) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y(r, g, b);
            p += wrap3;
            lum += wrap;

            RGB_IN(r, g, b, p);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = RGB_TO_Y(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y(r, g, b);

            cb[0] = RGB_TO_U(r1, g1, b1, 2);
            cr[0] = RGB_TO_V(r1, g1, b1, 2);

            cb++;
            cr++;
            p += -wrap3 + 2 * BPP;
            lum += -wrap + 2;
        }
        if (w) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y(r, g, b);
            p += wrap3;
            lum += wrap;
            RGB_IN(r, g, b, p);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = RGB_TO_Y(r, g, b);
            cb[0] = RGB_TO_U(r1, g1, b1, 1);
            cr[0] = RGB_TO_V(r1, g1, b1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        p += wrap3 + (wrap3 - width * BPP);
        lum += wrap + (wrap - width);
        cb += dst->linesize[1] - width2;
        cr += dst->linesize[2] - width2;
    }
    /* handle odd height */
    if (height) {
        for(w = width; w >= 2; w -= 2) {
            RGB_IN(r, g, b, p);
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = RGB_TO_Y(r, g, b);

            RGB_IN(r, g, b, p + BPP);
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = RGB_TO_Y(r, g, b);
            cb[0] = RGB_TO_U(r1, g1, b1, 1);
            cr[0] = RGB_TO_V(r1, g1, b1, 1);
            cb++;
            cr++;
            p += 2 * BPP;
           lum += 2;
        }
        if (w) {
            RGB_IN(r, g, b, p);
            lum[0] = RGB_TO_Y(r, g, b);
            cb[0] = RGB_TO_U(r, g, b, 0);
            cr[0] = RGB_TO_V(r, g, b, 0);
        }
    }
}

static void rgb24_to_yuvj444p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int src_wrap, x, y;
    int r, g, b;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;

    lum = dst->data[0];
    cb = dst->data[1];
    cr = dst->data[2];

    src_wrap = src->linesize[0] - width * BPP;
    p = src->data[0];
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            RGB_IN(r, g, b, p);
            lum[0] = RGB_TO_Y(r, g, b);
            cb[0] = RGB_TO_U(r, g, b, 0);
            cr[0] = RGB_TO_V(r, g, b, 0);
            p += BPP;
            cb++;
            cr++;
            lum++;
        }
        p += src_wrap;
        lum += dst->linesize[0] - width;
        cb += dst->linesize[1] - width;
        cr += dst->linesize[2] - width;
    }
}

#endif /* FMT_RGB24 */

#if defined(FMT_RGB24) || defined(FMT_RGB32)

static void glue(RGB_NAME, _to_pal8)(AVPicture *dst, const AVPicture *src,
                                     int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int dst_wrap, src_wrap;
    int x, y, has_alpha;
    unsigned int r, g, b;

    p = src->data[0];
    src_wrap = src->linesize[0] - BPP * width;

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - width;
    has_alpha = 0;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
#ifdef RGBA_IN
            {
                unsigned int a;
                RGBA_IN(r, g, b, a, p);
                /* crude approximation for alpha ! */
                if (a < 0x80) {
                    has_alpha = 1;
                    q[0] = TRANSP_INDEX;
                } else {
                    q[0] = gif_clut_index(r, g, b);
                }
            }
#else
            RGB_IN(r, g, b, p);
            q[0] = gif_clut_index(r, g, b);
#endif
            q++;
            p += BPP;
        }
        p += src_wrap;
        q += dst_wrap;
    }

    build_rgb_palette(dst->data[1], has_alpha);
}

#endif /* defined(FMT_RGB24) || defined(FMT_RGB32) */

#ifdef RGBA_IN

static int glue(get_alpha_info_, RGB_NAME)(const AVPicture *src,
                                           int width, int height)
{
    const unsigned char *p;
    int src_wrap, ret, x, y;
    unsigned int r, g, b, a;

    p = src->data[0];
    src_wrap = src->linesize[0] - BPP * width;
    ret = 0;
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            RGBA_IN(r, g, b, a, p);
            if (a == 0x00) {
                ret |= FF_ALPHA_TRANSP;
            } else if (a != 0xff) {
                ret |= FF_ALPHA_SEMI_TRANSP;
            }
            p += BPP;
        }
        p += src_wrap;
    }
    return ret;
}

#endif /* RGBA_IN */

#undef RGB_IN
#undef RGBA_IN
#undef RGB_OUT
#undef RGBA_OUT
#undef BPP
#undef RGB_NAME
#undef FMT_RGB24
#undef FMT_RGB32
