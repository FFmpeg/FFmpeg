/*
 * Misc image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

/**
 * @file
 * misc image conversion routines
 */

/* TODO:
 * - write 'ffimg' program to test all the image related stuff
 * - move all api to slice based system
 * - integrate deinterlacing, postprocessing and scaling in the conversion process
 */

#include "avcodec.h"
#include "imgconvert.h"
#include "internal.h"
#include "mathops.h"
#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"

void avcodec_get_chroma_sub_sample(enum AVPixelFormat pix_fmt, int *h_shift, int *v_shift)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    *h_shift = desc->log2_chroma_w;
    *v_shift = desc->log2_chroma_h;
}

static int is_gray(const AVPixFmtDescriptor *desc)
{
    return desc->nb_components - (desc->flags & AV_PIX_FMT_FLAG_ALPHA) == 1;
}

int avcodec_get_pix_fmt_loss(enum AVPixelFormat dst_pix_fmt,
                             enum AVPixelFormat src_pix_fmt,
                             int has_alpha)
{
    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
    int loss, i, nb_components = FFMIN(src_desc->nb_components,
                                       dst_desc->nb_components);

    /* compute loss */
    loss = 0;

    if (dst_pix_fmt == src_pix_fmt)
        return 0;

    for (i = 0; i < nb_components; i++)
        if (src_desc->comp[i].depth > dst_desc->comp[i].depth)
            loss |= FF_LOSS_DEPTH;

    if (dst_desc->log2_chroma_w > src_desc->log2_chroma_w ||
        dst_desc->log2_chroma_h > src_desc->log2_chroma_h)
        loss |= FF_LOSS_RESOLUTION;

    if ((src_desc->flags & AV_PIX_FMT_FLAG_RGB) != (dst_desc->flags & AV_PIX_FMT_FLAG_RGB))
        loss |= FF_LOSS_COLORSPACE;

    if (has_alpha && !(dst_desc->flags & AV_PIX_FMT_FLAG_ALPHA) &&
         (src_desc->flags & AV_PIX_FMT_FLAG_ALPHA))
        loss |= FF_LOSS_ALPHA;

    if (dst_pix_fmt == AV_PIX_FMT_PAL8 && !is_gray(src_desc))
        return loss | FF_LOSS_COLORQUANT;

    if (src_desc->nb_components > dst_desc->nb_components)
        if (is_gray(dst_desc))
            loss |= FF_LOSS_CHROMA;

    return loss;
}

static enum AVPixelFormat avcodec_find_best_pix_fmt1(enum AVPixelFormat *pix_fmt_list,
                                      enum AVPixelFormat src_pix_fmt,
                                      int has_alpha,
                                      int loss_mask)
{
    int dist, i, loss, min_dist;
    enum AVPixelFormat dst_pix_fmt;

    /* find exact color match with smallest size */
    dst_pix_fmt = AV_PIX_FMT_NONE;
    min_dist = 0x7fffffff;
    i = 0;
    while (pix_fmt_list[i] != AV_PIX_FMT_NONE) {
        enum AVPixelFormat pix_fmt = pix_fmt_list[i];

        if (i > AV_PIX_FMT_NB) {
            av_log(NULL, AV_LOG_ERROR, "Pixel format list longer than expected, "
                   "it is either not properly terminated or contains duplicates\n");
            return AV_PIX_FMT_NONE;
        }

        loss = avcodec_get_pix_fmt_loss(pix_fmt, src_pix_fmt, has_alpha) & loss_mask;
        if (loss == 0) {
            dist = av_get_bits_per_pixel(av_pix_fmt_desc_get(pix_fmt));
            if (dist < min_dist) {
                min_dist = dist;
                dst_pix_fmt = pix_fmt;
            }
        }
        i++;
    }
    return dst_pix_fmt;
}

enum AVPixelFormat avcodec_find_best_pix_fmt2(enum AVPixelFormat *pix_fmt_list,
                                            enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr)
{
    enum AVPixelFormat dst_pix_fmt;
    int loss_mask, i;
    static const int loss_mask_order[] = {
        ~0, /* no loss first */
        ~FF_LOSS_ALPHA,
        ~FF_LOSS_RESOLUTION,
        ~(FF_LOSS_COLORSPACE | FF_LOSS_RESOLUTION),
        ~FF_LOSS_COLORQUANT,
        ~FF_LOSS_DEPTH,
        0,
    };

    /* try with successive loss */
    i = 0;
    for(;;) {
        loss_mask = loss_mask_order[i++];
        dst_pix_fmt = avcodec_find_best_pix_fmt1(pix_fmt_list, src_pix_fmt,
                                                 has_alpha, loss_mask);
        if (dst_pix_fmt >= 0)
            goto found;
        if (loss_mask == 0)
            break;
    }
    return AV_PIX_FMT_NONE;
 found:
    if (loss_ptr)
        *loss_ptr = avcodec_get_pix_fmt_loss(dst_pix_fmt, src_pix_fmt, has_alpha);
    return dst_pix_fmt;
}

/* 2x2 -> 1x1 */
void ff_shrink22(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2;
    uint8_t *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            d[1] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
            d[2] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
            d[3] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
            s1 += 8;
            s2 += 8;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            s1 += 2;
            s2 += 2;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

/* 4x4 -> 1x1 */
void ff_shrink44(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2, *s3, *s4;
    uint8_t *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        s3 = s2 + src_wrap;
        s4 = s3 + src_wrap;
        d = dst;
        for(w = width;w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s1[2] + s1[3] +
                    s2[0] + s2[1] + s2[2] + s2[3] +
                    s3[0] + s3[1] + s3[2] + s3[3] +
                    s4[0] + s4[1] + s4[2] + s4[3] + 8) >> 4;
            s1 += 4;
            s2 += 4;
            s3 += 4;
            s4 += 4;
            d++;
        }
        src += 4 * src_wrap;
        dst += dst_wrap;
    }
}

/* 8x8 -> 1x1 */
void ff_shrink88(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w, i;

    for(;height > 0; height--) {
        for(w = width;w > 0; w--) {
            int tmp=0;
            for(i=0; i<8; i++){
                tmp += src[0] + src[1] + src[2] + src[3] + src[4] + src[5] + src[6] + src[7];
                src += src_wrap;
            }
            *(dst++) = (tmp + 32)>>6;
            src += 8 - 8*src_wrap;
        }
        src += 8*src_wrap - 8*width;
        dst += dst_wrap - width;
    }
}

/* return true if yuv planar */
static inline int is_yuv_planar(const AVPixFmtDescriptor *desc)
{
    return (!(desc->flags & AV_PIX_FMT_FLAG_RGB) &&
             (desc->flags & AV_PIX_FMT_FLAG_PLANAR));
}

#if FF_API_AVPICTURE
FF_DISABLE_DEPRECATION_WARNINGS

int av_picture_crop(AVPicture *dst, const AVPicture *src,
                    enum AVPixelFormat pix_fmt, int top_band, int left_band)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int y_shift;
    int x_shift;

    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB || !is_yuv_planar(desc))
        return -1;

    y_shift = desc->log2_chroma_h;
    x_shift = desc->log2_chroma_w;

    dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
    dst->data[1] = src->data[1] + ((top_band >> y_shift) * src->linesize[1]) + (left_band >> x_shift);
    dst->data[2] = src->data[2] + ((top_band >> y_shift) * src->linesize[2]) + (left_band >> x_shift);

    dst->linesize[0] = src->linesize[0];
    dst->linesize[1] = src->linesize[1];
    dst->linesize[2] = src->linesize[2];
    return 0;
}

int av_picture_pad(AVPicture *dst, const AVPicture *src, int height, int width,
                   enum AVPixelFormat pix_fmt, int padtop, int padbottom, int padleft, int padright,
            int *color)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    uint8_t *optr;
    int y_shift;
    int x_shift;
    int yheight;
    int i, y;

    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB ||
        !is_yuv_planar(desc)) return -1;

    for (i = 0; i < 3; i++) {
        x_shift = i ? desc->log2_chroma_w : 0;
        y_shift = i ? desc->log2_chroma_h : 0;

        if (padtop || padleft) {
            memset(dst->data[i], color[i],
                dst->linesize[i] * (padtop >> y_shift) + (padleft >> x_shift));
        }

        if (padleft || padright) {
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                (dst->linesize[i] - (padright >> x_shift));
            yheight = (height - 1 - (padtop + padbottom)) >> y_shift;
            for (y = 0; y < yheight; y++) {
                memset(optr, color[i], (padleft + padright) >> x_shift);
                optr += dst->linesize[i];
            }
        }

        if (src) { /* first line */
            uint8_t *iptr = src->data[i];
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                    (padleft >> x_shift);
            memcpy(optr, iptr, (width - padleft - padright) >> x_shift);
            iptr += src->linesize[i];
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                (dst->linesize[i] - (padright >> x_shift));
            yheight = (height - 1 - (padtop + padbottom)) >> y_shift;
            for (y = 0; y < yheight; y++) {
                memset(optr, color[i], (padleft + padright) >> x_shift);
                memcpy(optr + ((padleft + padright) >> x_shift), iptr,
                       (width - padleft - padright) >> x_shift);
                iptr += src->linesize[i];
                optr += dst->linesize[i];
            }
        }

        if (padbottom || padright) {
            optr = dst->data[i] + dst->linesize[i] *
                ((height - padbottom) >> y_shift) - (padright >> x_shift);
            memset(optr, color[i],dst->linesize[i] *
                (padbottom >> y_shift) + (padright >> x_shift));
        }
    }
    return 0;
}

FF_DISABLE_DEPRECATION_WARNINGS
#endif /* FF_API_AVPICTURE */
