/*
 * Misc image convertion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
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

typedef struct PixFmtInfo {
    const char *name;
    UINT8 nb_components;     /* number of components in AVPicture array  */
    UINT8 is_yuv : 1;    /* true if YUV instead of RGB color space */
    UINT8 is_packed : 1; /* true if multiple components in same word */
    UINT8 is_paletted : 1; /* true if paletted */
    UINT8 is_alpha : 1;    /* true if alpha can be specified */
    UINT8 is_gray : 1;     /* true if gray or monochrome format */
    UINT8 x_chroma_shift; /* X chroma subsampling factor is 2 ^ shift */
    UINT8 y_chroma_shift; /* Y chroma subsampling factor is 2 ^ shift */
} PixFmtInfo;

/* this table gives more information about formats */
static PixFmtInfo pix_fmt_info[PIX_FMT_NB] = {
    /* YUV formats */
    [PIX_FMT_YUV420P] = {
        .name = "yuv420p",
        .nb_components = 3, .is_yuv = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1, 
    },
    [PIX_FMT_YUV422P] = {
        .name = "yuv422p",
        .nb_components = 3, .is_yuv = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 0, 
    },
    [PIX_FMT_YUV444P] = {
        .name = "yuv444p",
        .nb_components = 3, .is_yuv = 1,
        .x_chroma_shift = 0, .y_chroma_shift = 0, 
    },
    [PIX_FMT_YUV422] = {
        .name = "yuv422",
        .nb_components = 1, .is_yuv = 1, .is_packed = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUV410P] = {
        .name = "yuv410p",
        .nb_components = 3, .is_yuv = 1,
        .x_chroma_shift = 2, .y_chroma_shift = 2,
    },
    [PIX_FMT_YUV411P] = {
        .name = "yuv411p",
        .nb_components = 3, .is_yuv = 1,
        .x_chroma_shift = 2, .y_chroma_shift = 0,
    },

    /* RGB formats */
    [PIX_FMT_RGB24] = {
        .name = "rgb24",
        .nb_components = 1, .is_packed = 1,
    },
    [PIX_FMT_BGR24] = {
        .name = "bgr24",
        .nb_components = 1, .is_packed = 1,
    },
    [PIX_FMT_RGBA32] = {
        .name = "rgba32",
        .nb_components = 1, .is_packed = 1, .is_alpha = 1,
    },
    [PIX_FMT_RGB565] = {
        .name = "rgb565",
        .nb_components = 1, .is_packed = 1,
    },
    [PIX_FMT_RGB555] = {
        .name = "rgb555",
        .nb_components = 1, .is_packed = 1, .is_alpha = 1,
    },

    /* gray / mono formats */
    [PIX_FMT_GRAY8] = {
        .name = "gray",
        .nb_components = 1, .is_gray = 1,
    },
    [PIX_FMT_MONOWHITE] = {
        .name = "monow",
        .nb_components = 1, .is_packed = 1, .is_gray = 1,
    },
    [PIX_FMT_MONOBLACK] = {
        .name = "monob",
        .nb_components = 1, .is_packed = 1, .is_gray = 1,
    },
};

void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift)
{
    if (pix_fmt_info[pix_fmt].is_yuv) {
        *h_shift = pix_fmt_info[pix_fmt].x_chroma_shift;
        *v_shift = pix_fmt_info[pix_fmt].y_chroma_shift;
    } else {
        *h_shift=0;
        *v_shift=0;
    }
}

const char *avcodec_get_pix_fmt_name(int pix_fmt)
{
    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB)
        return "???";
    else
        return pix_fmt_info[pix_fmt].name;
}

/* Picture field are filled with 'ptr' addresses. Also return size */
int avpicture_fill(AVPicture *picture, UINT8 *ptr,
                   int pix_fmt, int width, int height)
{
    int size;

    size = width * height;
    switch(pix_fmt) {
    case PIX_FMT_YUV420P:
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size / 4;
        picture->linesize[0] = width;
        picture->linesize[1] = width / 2;
        picture->linesize[2] = width / 2;
        return (size * 3) / 2;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->linesize[0] = width * 3;
        return size * 3;
    case PIX_FMT_YUV422P:
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size / 2;
        picture->linesize[0] = width;
        picture->linesize[1] = width / 2;
        picture->linesize[2] = width / 2;
        return (size * 2);
    case PIX_FMT_YUV444P:
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size;
        picture->linesize[0] = width;
        picture->linesize[1] = width;
        picture->linesize[2] = width;
        return size * 3;
    case PIX_FMT_RGBA32:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->linesize[0] = width * 4;
        return size * 4;
    case PIX_FMT_YUV410P:
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size / 16;
        picture->linesize[0] = width;
        picture->linesize[1] = width / 4;
        picture->linesize[2] = width / 4;
        return size + (size / 8);
    case PIX_FMT_YUV411P:
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size / 4;
        picture->linesize[0] = width;
        picture->linesize[1] = width / 4;
        picture->linesize[2] = width / 4;
        return size + (size / 2);
    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
    case PIX_FMT_YUV422:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->linesize[0] = width * 2;
        return size * 2;
    case PIX_FMT_GRAY8:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->linesize[0] = width;
        return size;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->linesize[0] = (width + 7) >> 3;
        return picture->linesize[0] * height;
    default:
        picture->data[0] = NULL;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        return -1;
    }
}

int avpicture_get_size(int pix_fmt, int width, int height)
{
    AVPicture dummy_pict;
    return avpicture_fill(&dummy_pict, NULL, pix_fmt, width, height);
}


/* XXX: totally non optimized */

static void yuv422_to_yuv420p(AVPicture *dst, AVPicture *src,
                              int width, int height)
{
    UINT8 *lum, *cb, *cr;
    int x, y;
    const UINT8 *p;
 
    lum = dst->data[0];
    cb = dst->data[1];
    cr = dst->data[2];
    p = src->data[0];
   
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

/* 1x2 -> 2x1 */
static void conv411(UINT8 *dst, int dst_wrap, 
                    UINT8 *src, int src_wrap,
                    int width, int height)
{
    int w, c;
    UINT8 *s1, *s2, *d;

    for(;height > 0; height--) {
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

#define YUV_TO_RGB2(r, g, b, y1)\
{\
    y = (y1 - 16) * C_Y;\
    r = cm[(y + r_add) >> SCALE_BITS];\
    g = cm[(y + g_add) >> SCALE_BITS];\
    b = cm[(y + b_add) >> SCALE_BITS];\
}

/* XXX: no chroma interpolating is done */
#define RGB_FUNCTIONS(rgb_name)                                         \
                                                                        \
static void yuv420p_to_ ## rgb_name (AVPicture *dst, AVPicture *src,    \
                                     int width, int height)             \
{                                                                       \
    UINT8 *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *d, *d1, *d2;             \
    int w, y, cb, cr, r_add, g_add, b_add, width2;                      \
    UINT8 *cm = cropTbl + MAX_NEG_CROP;                                 \
    unsigned int r, g, b;                                               \
                                                                        \
    d = dst->data[0];                                                   \
    y1_ptr = src->data[0];                                              \
    cb_ptr = src->data[1];                                              \
    cr_ptr = src->data[2];                                              \
    width2 = width >> 1;                                                \
    for(;height > 0; height -= 2) {                                     \
        d1 = d;                                                         \
        d2 = d + dst->linesize[0];                                      \
        y2_ptr = y1_ptr + src->linesize[0];                             \
        for(w = width2; w > 0; w --) {                                  \
            cb = cb_ptr[0] - 128;                                       \
            cr = cr_ptr[0] - 128;                                       \
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));                \
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));  \
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));                \
                                                                        \
            /* output 4 pixels */                                       \
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);                            \
            RGB_OUT(d1, r, g, b);                                       \
                                                                        \
            YUV_TO_RGB2(r, g, b, y1_ptr[1]);                            \
            RGB_OUT(d1 + BPP, r, g, b);                                 \
                                                                        \
            YUV_TO_RGB2(r, g, b, y2_ptr[0]);                            \
            RGB_OUT(d2, r, g, b);                                       \
                                                                        \
            YUV_TO_RGB2(r, g, b, y2_ptr[1]);                            \
            RGB_OUT(d2 + BPP, r, g, b);                                 \
                                                                        \
            d1 += 2 * BPP;                                              \
            d2 += 2 * BPP;                                              \
                                                                        \
            y1_ptr += 2;                                                \
            y2_ptr += 2;                                                \
            cb_ptr++;                                                   \
            cr_ptr++;                                                   \
        }                                                               \
        d += 2 * dst->linesize[0];                                      \
        y1_ptr += 2 * src->linesize[0] - width;                         \
        cb_ptr += src->linesize[1] - width2;                            \
        cr_ptr += src->linesize[2] - width2;                            \
    }                                                                   \
}                                                                       \
                                                                        \
/* XXX: no chroma interpolating is done */                              \
static void yuv422p_to_ ## rgb_name (AVPicture *dst, AVPicture *src,    \
                                    int width, int height)              \
{                                                                       \
    UINT8 *y1_ptr, *cb_ptr, *cr_ptr, *d, *d1;                           \
    int w, y, cb, cr, r_add, g_add, b_add, width2;                      \
    UINT8 *cm = cropTbl + MAX_NEG_CROP;                                 \
    unsigned int r, g, b;                                               \
                                                                        \
    d = dst->data[0];                                                   \
    y1_ptr = src->data[0];                                              \
    cb_ptr = src->data[1];                                              \
    cr_ptr = src->data[2];                                              \
    width2 = width >> 1;                                                \
    for(;height > 0; height --) {                                       \
        d1 = d;                                                         \
        for(w = width2; w > 0; w --) {                                  \
            cb = cb_ptr[0] - 128;                                       \
            cr = cr_ptr[0] - 128;                                       \
            r_add = C_RV * cr + (1 << (SCALE_BITS - 1));                \
            g_add = - C_GU * cb - C_GV * cr + (1 << (SCALE_BITS - 1));  \
            b_add = C_BU * cb + (1 << (SCALE_BITS - 1));                \
                                                                        \
            /* output 2 pixels */                                       \
            YUV_TO_RGB2(r, g, b, y1_ptr[0]);                            \
            RGB_OUT(d, r, g, b);                                        \
                                                                        \
            YUV_TO_RGB2(r, g, b, y1_ptr[1]);                            \
            RGB_OUT(d + BPP, r, g, b);                                  \
                                                                        \
            d += 2 * BPP;                                               \
                                                                        \
            y1_ptr += 2;                                                \
            cb_ptr++;                                                   \
            cr_ptr++;                                                   \
        }                                                               \
        d += dst->linesize[0];                                          \
        y1_ptr += src->linesize[0] - width;                             \
        cb_ptr += src->linesize[1] - width2;                            \
        cr_ptr += src->linesize[2] - width2;                            \
    }                                                                   \
}                                                                       \
                                                                        \
static void rgb_name ## _to_yuv420p(AVPicture *dst, AVPicture *src,     \
                                    int width, int height)              \
{                                                                       \
    int wrap, wrap3, x, y;                                              \
    int r, g, b, r1, g1, b1;                                            \
    UINT8 *lum, *cb, *cr;                                               \
    const UINT8 *p;                                                     \
                                                                        \
    lum = dst->data[0];                                                 \
    cb = dst->data[1];                                                  \
    cr = dst->data[2];                                                  \
                                                                        \
    wrap = width;                                                       \
    wrap3 = width * BPP;                                                \
    p = src->data[0];                                                   \
    for(y=0;y<height;y+=2) {                                            \
        for(x=0;x<width;x+=2) {                                         \
            RGB_IN(r, g, b, p);                                         \
            r1 = r;                                                     \
            g1 = g;                                                     \
            b1 = b;                                                     \
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g +             \
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;        \
            RGB_IN(r, g, b, p + BPP);                                   \
            r1 += r;                                                    \
            g1 += g;                                                    \
            b1 += b;                                                    \
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g +             \
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;        \
            p += wrap3;                                                 \
            lum += wrap;                                                \
                                                                        \
            RGB_IN(r, g, b, p);                                         \
            r1 += r;                                                    \
            g1 += g;                                                    \
            b1 += b;                                                    \
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g +             \
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;        \
                                                                        \
            RGB_IN(r, g, b, p + BPP);                                   \
            r1 += r;                                                    \
            g1 += g;                                                    \
            b1 += b;                                                    \
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g +             \
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;        \
                                                                        \
            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +         \
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >>          \
                     (SCALEBITS + 2)) + 128;                            \
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 -           \
                     FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >>           \
                     (SCALEBITS + 2)) + 128;                            \
                                                                        \
            cb++;                                                       \
            cr++;                                                       \
            p += -wrap3 + 2 * BPP;                                      \
            lum += -wrap + 2;                                           \
        }                                                               \
        p += wrap3;                                                     \
        lum += wrap;                                                    \
    }                                                                   \
}                                                                       \
                                                                        \
static void rgb_name ## _to_gray(AVPicture *dst, AVPicture *src,        \
                                 int width, int height)                 \
{                                                                       \
    const unsigned char *p;                                             \
    unsigned char *q;                                                   \
    int r, g, b, dst_wrap, src_wrap;                                    \
    int x, y;                                                           \
                                                                        \
    p = src->data[0];                                                   \
    src_wrap = src->linesize[0] - BPP * width;                          \
                                                                        \
    q = dst->data[0];                                                   \
    dst_wrap = dst->linesize[0] - width;                                \
                                                                        \
    for(y=0;y<height;y++) {                                             \
        for(x=0;x<width;x++) {                                          \
            RGB_IN(r, g, b, p);                                         \
            q[0] = (FIX(0.29900) * r + FIX(0.58700) * g +               \
                    FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;          \
            q++;                                                        \
            p += BPP;                                                   \
        }                                                               \
        p += src_wrap;                                                  \
        q += dst_wrap;                                                  \
    }                                                                   \
}                                                                       \
                                                                        \
static void gray_to_ ## rgb_name(AVPicture *dst, AVPicture *src,        \
                                 int width, int height)                 \
{                                                                       \
    const unsigned char *p;                                             \
    unsigned char *q;                                                   \
    int r, dst_wrap, src_wrap;                                          \
    int x, y;                                                           \
                                                                        \
    p = src->data[0];                                                   \
    src_wrap = src->linesize[0] - width;                                \
                                                                        \
    q = dst->data[0];                                                   \
    dst_wrap = dst->linesize[0] - BPP * width;                          \
                                                                        \
    for(y=0;y<height;y++) {                                             \
        for(x=0;x<width;x++) {                                          \
            r = p[0];                                                   \
            RGB_OUT(q, r, r, r);                                        \
            q += BPP;                                                   \
            p ++;                                                       \
        }                                                               \
        p += src_wrap;                                                  \
        q += dst_wrap;                                                  \
    }                                                                   \
}

/* copy bit n to bits 0 ... n - 1 */
static inline unsigned int bitcopy_n(unsigned int a, int n)
{
    int mask;
    mask = (1 << n) - 1;
    return (a & (0xff & ~mask)) | ((-((a >> n) & 1)) & mask);
}

/* rgb555 handling */

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((UINT16 *)(s))[0];\
    r = bitcopy_n(v >> (10 - 3), 3);\
    g = bitcopy_n(v >> (5 - 3), 3);\
    b = bitcopy_n(v << 3, 3);\
}

#define RGB_OUT(d, r, g, b)\
{\
    ((UINT16 *)(d))[0] = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | 0x8000;\
}

#define BPP 2

RGB_FUNCTIONS(rgb555)

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* rgb565 handling */

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((UINT16 *)(s))[0];\
    r = bitcopy_n(v >> (11 - 3), 3);\
    g = bitcopy_n(v >> (5 - 2), 2);\
    b = bitcopy_n(v << 3, 3);\
}

#define RGB_OUT(d, r, g, b)\
{\
    ((UINT16 *)(d))[0] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);\
}

#define BPP 2

RGB_FUNCTIONS(rgb565)

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* bgr24 handling */

#define RGB_IN(r, g, b, s)\
{\
    b = (s)[0];\
    g = (s)[1];\
    r = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = b;\
    (d)[1] = g;\
    (d)[2] = r;\
}

#define BPP 3

RGB_FUNCTIONS(bgr24)

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* rgb24 handling */

#define RGB_IN(r, g, b, s)\
{\
    r = (s)[0];\
    g = (s)[1];\
    b = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = r;\
    (d)[1] = g;\
    (d)[2] = b;\
}

#define BPP 3

RGB_FUNCTIONS(rgb24)

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* rgba32 handling */

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((UINT32 *)(s))[0];\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGB_OUT(d, r, g, b)\
{\
    ((UINT32 *)(d))[0] = (0xff << 24) | (r << 16) | (g << 8) | b;\
}

#define BPP 4

RGB_FUNCTIONS(rgba32)

#undef RGB_IN
#undef RGB_OUT
#undef BPP


static void rgb24_to_rgb565(AVPicture *dst, AVPicture *src,
                            int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int r, g, b, dst_wrap, src_wrap;
    int x, y;

    p = src->data[0];
    src_wrap = src->linesize[0] - 3 * width;

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - 2 * width;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            r = p[0];
            g = p[1];
            b = p[2];

            ((unsigned short *)q)[0] = 
                ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            q += 2;
            p += 3;
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

/* NOTE: we also add a dummy alpha bit */
static void rgb24_to_rgb555(AVPicture *dst, AVPicture *src,
                            int width, int height)
{
    const unsigned char *p;
    unsigned char *q;
    int r, g, b, dst_wrap, src_wrap;
    int x, y;

    p = src->data[0];
    src_wrap = src->linesize[0] - 3 * width;

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - 2 * width;

    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            r = p[0];
            g = p[1];
            b = p[2];

            ((unsigned short *)q)[0] = 
                ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | 0x8000;
            q += 2;
            p += 3;
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

static void mono_to_gray(AVPicture *dst, AVPicture *src,
                         int width, int height, int xor_mask)
{
    const unsigned char *p;
    unsigned char *q;
    int v, dst_wrap, src_wrap;
    int y, w;

    p = src->data[0];
    src_wrap = src->linesize[0] - ((width + 7) >> 3);

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - width;
    for(y=0;y<height;y++) {
        w = width; 
        while (w >= 8) {
            v = *p++ ^ xor_mask;
            q[0] = -(v >> 7);
            q[1] = -((v >> 6) & 1);
            q[2] = -((v >> 5) & 1);
            q[3] = -((v >> 4) & 1);
            q[4] = -((v >> 3) & 1);
            q[5] = -((v >> 2) & 1);
            q[6] = -((v >> 1) & 1);
            q[7] = -((v >> 0) & 1);
            w -= 8;
            q += 8;
        }
        if (w > 0) {
            v = *p++ ^ xor_mask;
            do {
                q[0] = -((v >> 7) & 1);
                q++;
                v <<= 1;
            } while (--w);
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

static void monowhite_to_gray(AVPicture *dst, AVPicture *src,
                               int width, int height)
{
    mono_to_gray(dst, src, width, height, 0xff);
}

static void monoblack_to_gray(AVPicture *dst, AVPicture *src,
                               int width, int height)
{
    mono_to_gray(dst, src, width, height, 0x00);
}

static void gray_to_mono(AVPicture *dst, AVPicture *src,
                         int width, int height, int xor_mask)
{
    int n;
    const UINT8 *s;
    UINT8 *d;
    int j, b, v, n1, src_wrap, dst_wrap, y;

    s = src->data[0];
    src_wrap = src->linesize[0] - width;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - ((width + 7) >> 3);
    printf("%d %d\n", width, height);

    for(y=0;y<height;y++) {
        n = width;
        while (n >= 8) {
            v = 0;
            for(j=0;j<8;j++) {
                b = s[0];
                s++;
                v = (v << 1) | (b >> 7);
            }
            d[0] = v ^ xor_mask;
            d++;
            n -= 8;
        }
        if (n > 0) {
            n1 = n;
            v = 0;
            while (n > 0) {
                b = s[0];
                s++;
                v = (v << 1) | (b >> 7);
                n--;
            }
            d[0] = (v << (8 - (n1 & 7))) ^ xor_mask;
            d++;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void gray_to_monowhite(AVPicture *dst, AVPicture *src,
                              int width, int height)
{
    gray_to_mono(dst, src, width, height, 0xff);
}

static void gray_to_monoblack(AVPicture *dst, AVPicture *src,
                              int width, int height)
{
    gray_to_mono(dst, src, width, height, 0x00);
}

typedef struct ConvertEntry {
    void (*convert)(AVPicture *dst, AVPicture *src, int width, int height);
} ConvertEntry;

/* add each new convertion function in this table */
/* constraints;
   - all non YUV modes must convert at least to and from PIX_FMT_RGB24
*/
static ConvertEntry convert_table[PIX_FMT_NB][PIX_FMT_NB] = {
    [PIX_FMT_YUV420P] = {
        [PIX_FMT_RGB555] = { 
            .convert = yuv420p_to_rgb555
        },
        [PIX_FMT_RGB565] = { 
            .convert = yuv420p_to_rgb565
        },
        [PIX_FMT_BGR24] = { 
            .convert = yuv420p_to_bgr24
        },
        [PIX_FMT_RGB24] = { 
            .convert = yuv420p_to_rgb24
        },
        [PIX_FMT_RGBA32] = { 
            .convert = yuv420p_to_rgba32
        },
    },
    [PIX_FMT_YUV422P] = {
        [PIX_FMT_RGB555] = { 
            .convert = yuv422p_to_rgb555
        },
        [PIX_FMT_RGB565] = { 
            .convert = yuv422p_to_rgb565
        },
        [PIX_FMT_BGR24] = { 
            .convert = yuv422p_to_bgr24
        },
        [PIX_FMT_RGB24] = { 
            .convert = yuv422p_to_rgb24
        },
        [PIX_FMT_RGBA32] = { 
            .convert = yuv422p_to_rgba32
        },
    },
    [PIX_FMT_YUV422] = { 
        [PIX_FMT_YUV420P] = { 
            .convert = yuv422_to_yuv420p,
        },
    },

    [PIX_FMT_RGB24] = {
        [PIX_FMT_YUV420P] = { 
            .convert = rgb24_to_yuv420p
        },
        [PIX_FMT_RGB565] = { 
            .convert = rgb24_to_rgb565
        },
        [PIX_FMT_RGB555] = { 
            .convert = rgb24_to_rgb555
        },
        [PIX_FMT_GRAY8] = { 
            .convert = rgb24_to_gray
        },
    },
    [PIX_FMT_RGBA32] = {
        [PIX_FMT_YUV420P] = { 
            .convert = rgba32_to_yuv420p
        },
        [PIX_FMT_GRAY8] = { 
            .convert = rgba32_to_gray
        },
    },
    [PIX_FMT_BGR24] = {
        [PIX_FMT_YUV420P] = { 
            .convert = bgr24_to_yuv420p
        },
        [PIX_FMT_GRAY8] = { 
            .convert = bgr24_to_gray
        },
    },
    [PIX_FMT_RGB555] = {
        [PIX_FMT_YUV420P] = { 
            .convert = rgb555_to_yuv420p
        },
        [PIX_FMT_GRAY8] = { 
            .convert = rgb555_to_gray
        },
    },
    [PIX_FMT_RGB565] = {
        [PIX_FMT_YUV420P] = { 
            .convert = rgb565_to_yuv420p
        },
        [PIX_FMT_GRAY8] = { 
            .convert = rgb565_to_gray
        },
    },
    [PIX_FMT_GRAY8] = {
        [PIX_FMT_RGB555] = { 
            .convert = gray_to_rgb555
        },
        [PIX_FMT_RGB565] = { 
            .convert = gray_to_rgb565
        },
        [PIX_FMT_RGB24] = { 
            .convert = gray_to_rgb24
        },
        [PIX_FMT_BGR24] = { 
            .convert = gray_to_bgr24
        },
        [PIX_FMT_RGBA32] = { 
            .convert = gray_to_rgba32
        },
        [PIX_FMT_MONOWHITE] = { 
            .convert = gray_to_monowhite
        },
        [PIX_FMT_MONOBLACK] = { 
            .convert = gray_to_monoblack
        },
    },
    [PIX_FMT_MONOWHITE] = {
        [PIX_FMT_GRAY8] = { 
            .convert = monowhite_to_gray
        },
    },
    [PIX_FMT_MONOBLACK] = {
        [PIX_FMT_GRAY8] = { 
            .convert = monoblack_to_gray
        },
    },
};

static int avpicture_alloc(AVPicture *picture,
                           int pix_fmt, int width, int height)
{
    int size;
    void *ptr;

    size = avpicture_get_size(pix_fmt, width, height);
    if (size < 0)
        goto fail;
    ptr = av_malloc(size);
    if (!ptr)
        goto fail;
    avpicture_fill(picture, ptr, pix_fmt, width, height);
    return 0;
 fail:
    memset(picture, 0, sizeof(AVPicture));
    return -1;
}

static void avpicture_free(AVPicture *picture)
{
    av_free(picture->data[0]);
}

/* XXX: always use linesize. Return -1 if not supported */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                AVPicture *src, int src_pix_fmt, 
                int src_width, int src_height)
{
    int i, ret, dst_width, dst_height, int_pix_fmt;
    PixFmtInfo *src_pix, *dst_pix;
    ConvertEntry *ce;
    AVPicture tmp1, *tmp = &tmp1;

    if (src_pix_fmt < 0 || src_pix_fmt >= PIX_FMT_NB ||
        dst_pix_fmt < 0 || dst_pix_fmt >= PIX_FMT_NB)
        return -1;
    if (src_width <= 0 || src_height <= 0)
        return 0;

    dst_width = src_width;
    dst_height = src_height;

    dst_pix = &pix_fmt_info[dst_pix_fmt];
    src_pix = &pix_fmt_info[src_pix_fmt];
    if (src_pix_fmt == dst_pix_fmt) {
        /* XXX: incorrect */
        /* same format: just copy */
        for(i = 0; i < dst_pix->nb_components; i++) {
            int w, h;
            w = dst_width;
            h = dst_height;
            if (dst_pix->is_yuv && (i == 1 || i == 2)) {
                w >>= dst_pix->x_chroma_shift;
                h >>= dst_pix->y_chroma_shift;
            }
            img_copy(dst->data[i], dst->linesize[i],
                     src->data[i], src->linesize[i],
                     w, h);
        }
        return 0;
    }

    ce = &convert_table[src_pix_fmt][dst_pix_fmt];
    if (ce->convert) {
        /* specific convertion routine */
        ce->convert(dst, src, dst_width, dst_height);
        return 0;
    }

    /* gray to YUV */
    if (dst_pix->is_yuv && src_pix_fmt == PIX_FMT_GRAY8) {
        int w, h, y;
        uint8_t *d;

        img_copy(dst->data[0], dst->linesize[0],
                 src->data[0], src->linesize[0],
                 dst_width, dst_height);
        /* fill U and V with 128 */
        w = dst_width;
        h = dst_height;
        w >>= dst_pix->x_chroma_shift;
        h >>= dst_pix->y_chroma_shift;
        for(i = 1; i <= 2; i++) {
            d = dst->data[i];
            for(y = 0; y< h; y++) {
                memset(d, 128, w);
                d += dst->linesize[i];
            }
        }
        return 0;
    }

    /* YUV to gray */
    if (src_pix->is_yuv && dst_pix_fmt == PIX_FMT_GRAY8) {
        img_copy(dst->data[0], dst->linesize[0],
                 src->data[0], src->linesize[0],
                 dst_width, dst_height);
        return 0;
    }

    /* YUV to YUV */
    if (dst_pix->is_yuv && src_pix->is_yuv) {
        int x_shift, y_shift, w, h;
        void (*resize_func)(UINT8 *dst, int dst_wrap, 
                            UINT8 *src, int src_wrap,
                            int width, int height);

        /* compute chroma size of the smallest dimensions */
        w = dst_width;
        h = dst_height;
        if (dst_pix->x_chroma_shift >= src_pix->x_chroma_shift)
            w >>= dst_pix->x_chroma_shift;
        else
            w >>= src_pix->x_chroma_shift;
        if (dst_pix->y_chroma_shift >= src_pix->y_chroma_shift)
            h >>= dst_pix->y_chroma_shift;
        else
            h >>= src_pix->y_chroma_shift;

        x_shift = (dst_pix->x_chroma_shift - src_pix->x_chroma_shift);
        y_shift = (dst_pix->y_chroma_shift - src_pix->y_chroma_shift);
        if (x_shift == 0 && y_shift == 0) {
            resize_func = img_copy; /* should never happen */
        } else if (x_shift == 0 && y_shift == 1) {
            resize_func = shrink2;
        } else if (x_shift == 1 && y_shift == 1) {
            resize_func = shrink22;
        } else if (x_shift == -1 && y_shift == -1) {
            resize_func = grow22;
        } else if (x_shift == -1 && y_shift == 1) {
            resize_func = conv411;
        } else {
            /* currently not handled */
            return -1;
        }

        img_copy(dst->data[0], dst->linesize[0],
                 src->data[0], src->linesize[0],
                 dst_width, dst_height);

        for(i = 1;i <= 2; i++)
            resize_func(dst->data[i], dst->linesize[i],
                        src->data[i], src->linesize[i],
                        w, h);
       return 0;
    }

    /* try to use an intermediate format */
    if (src_pix_fmt == PIX_FMT_MONOWHITE ||
        src_pix_fmt == PIX_FMT_MONOBLACK ||
        dst_pix_fmt == PIX_FMT_MONOWHITE ||
        dst_pix_fmt == PIX_FMT_MONOBLACK) {
        int_pix_fmt = PIX_FMT_GRAY8;
    } else {
        int_pix_fmt = PIX_FMT_RGB24;
    }
    if (avpicture_alloc(tmp, int_pix_fmt, dst_width, dst_height) < 0)
        return -1;
    ret = -1;
    if (img_convert(tmp, int_pix_fmt,
                    src, src_pix_fmt, src_width, src_height) < 0)
        goto fail1;
    if (img_convert(dst, dst_pix_fmt,
                    tmp, int_pix_fmt, dst_width, dst_height) < 0)
        goto fail1;
    ret = 0;
 fail1:
    avpicture_free(tmp);
    return ret;
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

    {
        mmx_t rounder;
        rounder.uw[0]=4;
        rounder.uw[1]=4;
        rounder.uw[2]=4;
        rounder.uw[3]=4;
        pxor_r2r(mm7,mm7);
        movq_m2r(rounder,mm6);
    }
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

    {
        mmx_t rounder;
        rounder.uw[0]=4;
        rounder.uw[1]=4;
        rounder.uw[2]=4;
        rounder.uw[3]=4;
        pxor_r2r(mm7,mm7);
        movq_m2r(rounder,mm6);
    }
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
