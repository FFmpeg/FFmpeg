/*
 * Misc image conversion routines
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

/**
 * @file
 * misc image conversion routines
 */

#include "avcodec.h"
#include "internal.h"
#include "mathops.h"
#include "libavutil/avassert.h"
#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"

#if FF_API_GETCHROMA
void avcodec_get_chroma_sub_sample(enum AVPixelFormat pix_fmt, int *h_shift, int *v_shift)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    *h_shift = desc->log2_chroma_w;
    *v_shift = desc->log2_chroma_h;
}
#endif

#if FF_API_AVCODEC_PIX_FMT
int avcodec_get_pix_fmt_loss(enum AVPixelFormat dst_pix_fmt,
                             enum AVPixelFormat src_pix_fmt,
                             int has_alpha)
{
    return av_get_pix_fmt_loss(dst_pix_fmt, src_pix_fmt, has_alpha);
}

enum AVPixelFormat avcodec_find_best_pix_fmt_of_2(enum AVPixelFormat dst_pix_fmt1, enum AVPixelFormat dst_pix_fmt2,
                                            enum AVPixelFormat src_pix_fmt, int has_alpha, int *loss_ptr)
{
    return av_find_best_pix_fmt_of_2(dst_pix_fmt1, dst_pix_fmt2, src_pix_fmt, has_alpha, loss_ptr);
}

enum AVPixelFormat avcodec_find_best_pix_fmt2(enum AVPixelFormat dst_pix_fmt1, enum AVPixelFormat dst_pix_fmt2,
                                            enum AVPixelFormat src_pix_fmt, int has_alpha, int *loss_ptr)
{
    return av_find_best_pix_fmt_of_2(dst_pix_fmt1, dst_pix_fmt2, src_pix_fmt, has_alpha, loss_ptr);
}

#endif
enum AVPixelFormat avcodec_find_best_pix_fmt_of_list(const enum AVPixelFormat *pix_fmt_list,
                                            enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr){
    int i;

    enum AVPixelFormat best = AV_PIX_FMT_NONE;
    int loss;

    for (i=0; pix_fmt_list[i] != AV_PIX_FMT_NONE; i++) {
        loss = loss_ptr ? *loss_ptr : 0;
        best = av_find_best_pix_fmt_of_2(best, pix_fmt_list[i], src_pix_fmt, has_alpha, &loss);
    }

    if (loss_ptr)
        *loss_ptr = loss;
    return best;
}

#if FF_API_AVPICTURE
FF_DISABLE_DEPRECATION_WARNINGS
/* return true if yuv planar */
static inline int is_yuv_planar(const AVPixFmtDescriptor *desc)
{
    int i;
    int planes[4] = { 0 };

    if (     desc->flags & AV_PIX_FMT_FLAG_RGB
        || !(desc->flags & AV_PIX_FMT_FLAG_PLANAR))
        return 0;

    /* set the used planes */
    for (i = 0; i < desc->nb_components; i++)
        planes[desc->comp[i].plane] = 1;

    /* if there is an unused plane, the format is not planar */
    for (i = 0; i < desc->nb_components; i++)
        if (!planes[i])
            return 0;
    return 1;
}

int av_picture_crop(AVPicture *dst, const AVPicture *src,
                    enum AVPixelFormat pix_fmt, int top_band, int left_band)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int y_shift;
    int x_shift;
    int max_step[4];

    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB)
        return -1;

    y_shift = desc->log2_chroma_h;
    x_shift = desc->log2_chroma_w;
    av_image_fill_max_pixsteps(max_step, NULL, desc);

    if (is_yuv_planar(desc)) {
    dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
    dst->data[1] = src->data[1] + ((top_band >> y_shift) * src->linesize[1]) + (left_band >> x_shift);
    dst->data[2] = src->data[2] + ((top_band >> y_shift) * src->linesize[2]) + (left_band >> x_shift);
    } else{
        if(top_band % (1<<y_shift) || left_band % (1<<x_shift))
            return -1;
        dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + (left_band * max_step[0]);
    }

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
    int max_step[4];

    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB)
        return -1;

    if (!is_yuv_planar(desc)) {
        if (src)
            return -1; //TODO: Not yet implemented

        av_image_fill_max_pixsteps(max_step, NULL, desc);

        if (padtop || padleft) {
            memset(dst->data[0], color[0],
                    dst->linesize[0] * padtop + (padleft * max_step[0]));
        }

        if (padleft || padright) {
            optr = dst->data[0] + dst->linesize[0] * padtop +
                    (dst->linesize[0] - (padright * max_step[0]));
            yheight = height - 1 - (padtop + padbottom);
            for (y = 0; y < yheight; y++) {
                memset(optr, color[0], (padleft + padright) * max_step[0]);
                optr += dst->linesize[0];
            }
        }

        if (padbottom || padright) {
            optr = dst->data[0] + dst->linesize[0] * (height - padbottom) -
                    (padright * max_step[0]);
            memset(optr, color[0], dst->linesize[0] * padbottom +
                    (padright * max_step[0]));
        }

        return 0;
    }

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
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_AVPICTURE */
