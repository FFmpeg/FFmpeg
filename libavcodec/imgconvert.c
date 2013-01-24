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

/* TODO:
 * - write 'ffimg' program to test all the image related stuff
 * - move all api to slice based system
 * - integrate deinterlacing, postprocessing and scaling in the conversion process
 */

#include "avcodec.h"
#include "dsputil.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"

#if HAVE_MMX_EXTERNAL
#include "x86/dsputil_mmx.h"
#endif

#define FF_COLOR_NA      -1
#define FF_COLOR_RGB      0 /**< RGB color space */
#define FF_COLOR_GRAY     1 /**< gray color space */
#define FF_COLOR_YUV      2 /**< YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3 /**< YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#if HAVE_MMX_EXTERNAL
#define deinterlace_line_inplace ff_deinterlace_line_inplace_mmx
#define deinterlace_line         ff_deinterlace_line_mmx
#else
#define deinterlace_line_inplace deinterlace_line_inplace_c
#define deinterlace_line         deinterlace_line_c
#endif

#define pixdesc_has_alpha(pixdesc) \
    ((pixdesc)->nb_components == 2 || (pixdesc)->nb_components == 4 || (pixdesc)->flags & PIX_FMT_PAL)


void avcodec_get_chroma_sub_sample(enum AVPixelFormat pix_fmt, int *h_shift, int *v_shift)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    *h_shift = desc->log2_chroma_w;
    *v_shift = desc->log2_chroma_h;
}

static int get_color_type(const AVPixFmtDescriptor *desc) {
    if(desc->nb_components == 1 || desc->nb_components == 2)
        return FF_COLOR_GRAY;

    if(desc->name && !strncmp(desc->name, "yuvj", 4))
        return FF_COLOR_YUV_JPEG;

    if(desc->flags & PIX_FMT_RGB)
        return  FF_COLOR_RGB;

    if(desc->nb_components == 0)
        return FF_COLOR_NA;

    return FF_COLOR_YUV;
}

static int get_pix_fmt_depth(int *min, int *max, enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i;

    if (!desc || !desc->nb_components) {
        *min = *max = 0;
        return AVERROR(EINVAL);
    }

    *min = INT_MAX, *max = -INT_MAX;
    for (i = 0; i < desc->nb_components; i++) {
        *min = FFMIN(desc->comp[i].depth_minus1+1, *min);
        *max = FFMAX(desc->comp[i].depth_minus1+1, *max);
    }
    return 0;
}

int avcodec_get_pix_fmt_loss(enum AVPixelFormat dst_pix_fmt,
                             enum AVPixelFormat src_pix_fmt,
                             int has_alpha)
{
    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);
    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
    int src_color, dst_color;
    int src_min_depth, src_max_depth, dst_min_depth, dst_max_depth;
    int ret, loss, i, nb_components;

    if (dst_pix_fmt >= AV_PIX_FMT_NB || dst_pix_fmt <= AV_PIX_FMT_NONE)
        return ~0;

    /* compute loss */
    loss = 0;

    if (dst_pix_fmt == src_pix_fmt)
        return 0;

    if ((ret = get_pix_fmt_depth(&src_min_depth, &src_max_depth, src_pix_fmt)) < 0)
        return ret;
    if ((ret = get_pix_fmt_depth(&dst_min_depth, &dst_max_depth, dst_pix_fmt)) < 0)
        return ret;

    src_color = get_color_type(src_desc);
    dst_color = get_color_type(dst_desc);
    nb_components = FFMIN(src_desc->nb_components, dst_desc->nb_components);

    for (i = 0; i < nb_components; i++)
        if (src_desc->comp[i].depth_minus1 > dst_desc->comp[i].depth_minus1)
            loss |= FF_LOSS_DEPTH;

    if (dst_desc->log2_chroma_w > src_desc->log2_chroma_w ||
        dst_desc->log2_chroma_h > src_desc->log2_chroma_h)
        loss |= FF_LOSS_RESOLUTION;

    switch(dst_color) {
    case FF_COLOR_RGB:
        if (src_color != FF_COLOR_RGB &&
            src_color != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_GRAY:
        if (src_color != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV:
        if (src_color != FF_COLOR_YUV)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV_JPEG:
        if (src_color != FF_COLOR_YUV_JPEG &&
            src_color != FF_COLOR_YUV &&
            src_color != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    default:
        /* fail safe test */
        if (src_color != dst_color)
            loss |= FF_LOSS_COLORSPACE;
        break;
    }
    if (dst_color == FF_COLOR_GRAY &&
        src_color != FF_COLOR_GRAY)
        loss |= FF_LOSS_CHROMA;
    if (!pixdesc_has_alpha(dst_desc) && (pixdesc_has_alpha(src_desc) && has_alpha))
        loss |= FF_LOSS_ALPHA;
    if (dst_pix_fmt == AV_PIX_FMT_PAL8 &&
        (src_pix_fmt != AV_PIX_FMT_PAL8 && (src_color != FF_COLOR_GRAY || (pixdesc_has_alpha(src_desc) && has_alpha))))
        loss |= FF_LOSS_COLORQUANT;

    return loss;
}

#if FF_API_FIND_BEST_PIX_FMT
enum AVPixelFormat avcodec_find_best_pix_fmt(int64_t pix_fmt_mask, enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr)
{
    enum AVPixelFormat dst_pix_fmt;
    int i;

    if (loss_ptr) /* all losses count (for backward compatibility) */
        *loss_ptr = 0;

    dst_pix_fmt = AV_PIX_FMT_NONE; /* so first iteration doesn't have to be treated special */
    for(i = 0; i< FFMIN(AV_PIX_FMT_NB, 64); i++){
        if (pix_fmt_mask & (1ULL << i))
            dst_pix_fmt = avcodec_find_best_pix_fmt_of_2(dst_pix_fmt, i, src_pix_fmt, has_alpha, loss_ptr);
    }
    return dst_pix_fmt;
}
#endif /* FF_API_FIND_BEST_PIX_FMT */

enum AVPixelFormat avcodec_find_best_pix_fmt_of_2(enum AVPixelFormat dst_pix_fmt1, enum AVPixelFormat dst_pix_fmt2,
                                            enum AVPixelFormat src_pix_fmt, int has_alpha, int *loss_ptr)
{
    enum AVPixelFormat dst_pix_fmt;
    int loss1, loss2, loss_order1, loss_order2, i, loss_mask;
    const AVPixFmtDescriptor *desc1 = av_pix_fmt_desc_get(dst_pix_fmt1);
    const AVPixFmtDescriptor *desc2 = av_pix_fmt_desc_get(dst_pix_fmt2);
    static const int loss_mask_order[] = {
        ~0, /* no loss first */
        ~FF_LOSS_ALPHA,
        ~FF_LOSS_RESOLUTION,
        ~FF_LOSS_COLORSPACE,
        ~(FF_LOSS_COLORSPACE | FF_LOSS_RESOLUTION),
        ~FF_LOSS_COLORQUANT,
        ~FF_LOSS_DEPTH,
        ~(FF_LOSS_DEPTH|FF_LOSS_COLORSPACE),
        ~(FF_LOSS_RESOLUTION | FF_LOSS_DEPTH | FF_LOSS_COLORSPACE | FF_LOSS_ALPHA |
          FF_LOSS_COLORQUANT | FF_LOSS_CHROMA),
        0x80000, //non zero entry that combines all loss variants including future additions
        0,
    };

    loss_mask= loss_ptr?~*loss_ptr:~0; /* use loss mask if provided */
    dst_pix_fmt = AV_PIX_FMT_NONE;
    loss1 = avcodec_get_pix_fmt_loss(dst_pix_fmt1, src_pix_fmt, has_alpha) & loss_mask;
    loss2 = avcodec_get_pix_fmt_loss(dst_pix_fmt2, src_pix_fmt, has_alpha) & loss_mask;

    /* try with successive loss */
    for(i = 0;loss_mask_order[i] != 0 && dst_pix_fmt == AV_PIX_FMT_NONE;i++) {
        loss_order1 = loss1 & loss_mask_order[i];
        loss_order2 = loss2 & loss_mask_order[i];

        if (loss_order1 == 0 && loss_order2 == 0 && dst_pix_fmt2 != AV_PIX_FMT_NONE && dst_pix_fmt1 != AV_PIX_FMT_NONE){ /* use format with smallest depth */
            if(av_get_padded_bits_per_pixel(desc2) != av_get_padded_bits_per_pixel(desc1)) {
                dst_pix_fmt = av_get_padded_bits_per_pixel(desc2) < av_get_padded_bits_per_pixel(desc1) ? dst_pix_fmt2 : dst_pix_fmt1;
            } else {
                dst_pix_fmt = desc2->nb_components < desc1->nb_components ? dst_pix_fmt2 : dst_pix_fmt1;
            }
        } else if (loss_order1 == 0 || loss_order2 == 0) { /* use format with no loss */
            dst_pix_fmt = loss_order2 ? dst_pix_fmt1 : dst_pix_fmt2;
        }
    }

    if (loss_ptr)
        *loss_ptr = avcodec_get_pix_fmt_loss(dst_pix_fmt, src_pix_fmt, has_alpha);
    return dst_pix_fmt;
}

#if AV_HAVE_INCOMPATIBLE_FORK_ABI
enum AVPixelFormat avcodec_find_best_pix_fmt2(enum AVPixelFormat *pix_fmt_list,
                                            enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr){
    return avcodec_find_best_pix_fmt_of_list(pix_fmt_list, src_pix_fmt, has_alpha, loss_ptr);
}
#else
enum AVPixelFormat avcodec_find_best_pix_fmt2(enum AVPixelFormat dst_pix_fmt1, enum AVPixelFormat dst_pix_fmt2,
                                            enum AVPixelFormat src_pix_fmt, int has_alpha, int *loss_ptr)
{
    return avcodec_find_best_pix_fmt_of_2(dst_pix_fmt1, dst_pix_fmt2, src_pix_fmt, has_alpha, loss_ptr);
}
#endif

enum AVPixelFormat avcodec_find_best_pix_fmt_of_list(enum AVPixelFormat *pix_fmt_list,
                                            enum AVPixelFormat src_pix_fmt,
                                            int has_alpha, int *loss_ptr){
    int i;

    enum AVPixelFormat best = AV_PIX_FMT_NONE;

    for(i=0; pix_fmt_list[i] != AV_PIX_FMT_NONE; i++)
        best = avcodec_find_best_pix_fmt_of_2(best, pix_fmt_list[i], src_pix_fmt, has_alpha, loss_ptr);

    return best;
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
    int i;
    int planes[4] = { 0 };

    if (     desc->flags & PIX_FMT_RGB
        || !(desc->flags & PIX_FMT_PLANAR))
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

    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB)
        return -1;

    y_shift = desc->log2_chroma_h;
    x_shift = desc->log2_chroma_w;

    if (is_yuv_planar(desc)) {
    dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
    dst->data[1] = src->data[1] + ((top_band >> y_shift) * src->linesize[1]) + (left_band >> x_shift);
    dst->data[2] = src->data[2] + ((top_band >> y_shift) * src->linesize[2]) + (left_band >> x_shift);
    } else{
        if(top_band % (1<<y_shift) || left_band % (1<<x_shift))
            return -1;
        if(left_band) //FIXME add support for this too
            return -1;
        dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
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

#if !HAVE_MMX_EXTERNAL
/* filter parameters: [-1 4 2 4 -1] // 8 */
static void deinterlace_line_c(uint8_t *dst,
                             const uint8_t *lum_m4, const uint8_t *lum_m3,
                             const uint8_t *lum_m2, const uint8_t *lum_m1,
                             const uint8_t *lum,
                             int size)
{
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
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
}

static void deinterlace_line_inplace_c(uint8_t *lum_m4, uint8_t *lum_m3,
                                       uint8_t *lum_m2, uint8_t *lum_m1,
                                       uint8_t *lum, int size)
{
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
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
}
#endif /* !HAVE_MMX_EXTERNAL */

/* deinterlacing : 2 temporal taps, 3 spatial taps linear filter. The
   top field is copied as is, but the bottom field is deinterlaced
   against the top field. */
static void deinterlace_bottom_field(uint8_t *dst, int dst_wrap,
                                    const uint8_t *src1, int src_wrap,
                                    int width, int height)
{
    const uint8_t *src_m2, *src_m1, *src_0, *src_p1, *src_p2;
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

static void deinterlace_bottom_field_inplace(uint8_t *src1, int src_wrap,
                                             int width, int height)
{
    uint8_t *src_m1, *src_0, *src_p1, *src_p2;
    int y;
    uint8_t *buf;
    buf = av_malloc(width);

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

int avpicture_deinterlace(AVPicture *dst, const AVPicture *src,
                          enum AVPixelFormat pix_fmt, int width, int height)
{
    int i;

    if (pix_fmt != AV_PIX_FMT_YUV420P &&
        pix_fmt != AV_PIX_FMT_YUVJ420P &&
        pix_fmt != AV_PIX_FMT_YUV422P &&
        pix_fmt != AV_PIX_FMT_YUVJ422P &&
        pix_fmt != AV_PIX_FMT_YUV444P &&
        pix_fmt != AV_PIX_FMT_YUV411P &&
        pix_fmt != AV_PIX_FMT_GRAY8)
        return -1;
    if ((width & 3) != 0 || (height & 3) != 0)
        return -1;

    for(i=0;i<3;i++) {
        if (i == 1) {
            switch(pix_fmt) {
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUV420P:
                width >>= 1;
                height >>= 1;
                break;
            case AV_PIX_FMT_YUV422P:
            case AV_PIX_FMT_YUVJ422P:
                width >>= 1;
                break;
            case AV_PIX_FMT_YUV411P:
                width >>= 2;
                break;
            default:
                break;
            }
            if (pix_fmt == AV_PIX_FMT_GRAY8) {
                break;
            }
        }
        if (src == dst) {
            deinterlace_bottom_field_inplace(dst->data[i], dst->linesize[i],
                                 width, height);
        } else {
            deinterlace_bottom_field(dst->data[i],dst->linesize[i],
                                        src->data[i], src->linesize[i],
                                        width, height);
        }
    }
    emms_c();
    return 0;
}

#ifdef TEST

int main(void){
    int i;
    int err=0;
    int skip = 0;

    for (i=0; i<AV_PIX_FMT_NB*2; i++) {
        AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if(!desc || !desc->name) {
            skip ++;
            continue;
        }
        if (skip) {
            av_log(NULL, AV_LOG_INFO, "%3d unused pixel format values\n", skip);
            skip = 0;
        }
        av_log(NULL, AV_LOG_INFO, "pix fmt %s yuv_plan:%d avg_bpp:%d colortype:%d\n", desc->name, is_yuv_planar(desc), av_get_padded_bits_per_pixel(desc), get_color_type(desc));
        if ((!(desc->flags & PIX_FMT_ALPHA)) != (desc->nb_components != 2 && desc->nb_components != 4)) {
            av_log(NULL, AV_LOG_ERROR, "Alpha flag mismatch\n");
            err = 1;
        }
    }
    return err;
}

#endif
