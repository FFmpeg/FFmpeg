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
#include "imgconvert.h"
#include "libavutil/colorspace.h"
#include "libavutil/pixdesc.h"
#include "libavcore/imgutils.h"
#include "libavcore/internal.h"

#if HAVE_MMX && HAVE_YASM
#include "x86/dsputil_mmx.h"
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

#define FF_COLOR_RGB      0 /**< RGB color space */
#define FF_COLOR_GRAY     1 /**< gray color space */
#define FF_COLOR_YUV      2 /**< YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3 /**< YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0 /**< each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1 /**< only one components containing all the channels */
#define FF_PIXEL_PALETTE  2  /**< one components containing indexes for a palette */

#if HAVE_MMX && HAVE_YASM
#define deinterlace_line_inplace ff_deinterlace_line_inplace_mmx
#define deinterlace_line         ff_deinterlace_line_mmx
#else
#define deinterlace_line_inplace deinterlace_line_inplace_c
#define deinterlace_line         deinterlace_line_c
#endif

typedef struct PixFmtInfo {
    uint8_t nb_channels;     /**< number of channels (including alpha) */
    uint8_t color_type;      /**< color type (see FF_COLOR_xxx constants) */
    uint8_t pixel_type;      /**< pixel storage type (see FF_PIXEL_xxx constants) */
    uint8_t is_alpha : 1;    /**< true if alpha can be specified */
    uint8_t depth;           /**< bit depth of the color components */
} PixFmtInfo;

/* this table gives more information about formats */
static const PixFmtInfo pix_fmt_info[PIX_FMT_NB] = {
    /* YUV formats */
    [PIX_FMT_YUV420P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUV422P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUV444P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUYV422] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_UYVY422] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_YUV410P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUV411P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUV440P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUV420P16LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_YUV422P16LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_YUV444P16LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_YUV420P16BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_YUV422P16BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_YUV444P16BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },


    /* YUV formats with alpha plane */
    [PIX_FMT_YUVA420P] = {
        .nb_channels = 4,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },

    /* JPEG YUV */
    [PIX_FMT_YUVJ420P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUVJ422P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUVJ444P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_YUVJ440P] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },

    /* RGB formats */
    [PIX_FMT_RGB24] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_BGR24] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_ARGB] = {
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_RGB48BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 16,
    },
    [PIX_FMT_RGB48LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 16,
    },
    [PIX_FMT_RGB565BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_RGB565LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_RGB555BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_RGB555LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_RGB444BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },
    [PIX_FMT_RGB444LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },

    /* gray / mono formats */
    [PIX_FMT_GRAY16BE] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_GRAY16LE] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_GRAY8] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_MONOWHITE] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 1,
    },
    [PIX_FMT_MONOBLACK] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 1,
    },

    /* paletted formats */
    [PIX_FMT_PAL8] = {
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PALETTE,
        .depth = 8,
    },
    [PIX_FMT_UYYVYY411] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_ABGR] = {
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_BGR565BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_BGR565LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_BGR555BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_BGR555LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
    },
    [PIX_FMT_BGR444BE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },
    [PIX_FMT_BGR444LE] = {
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },
    [PIX_FMT_RGB8] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_RGB4] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },
    [PIX_FMT_RGB4_BYTE] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_BGR8] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_BGR4] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
    },
    [PIX_FMT_BGR4_BYTE] = {
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_NV12] = {
        .nb_channels = 2,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_NV21] = {
        .nb_channels = 2,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },

    [PIX_FMT_BGRA] = {
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
    [PIX_FMT_RGBA] = {
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
    },
};

void avcodec_get_chroma_sub_sample(enum PixelFormat pix_fmt, int *h_shift, int *v_shift)
{
    *h_shift = av_pix_fmt_descriptors[pix_fmt].log2_chroma_w;
    *v_shift = av_pix_fmt_descriptors[pix_fmt].log2_chroma_h;
}

const char *avcodec_get_pix_fmt_name(enum PixelFormat pix_fmt)
{
    if ((unsigned)pix_fmt >= PIX_FMT_NB)
        return NULL;
    else
        return av_pix_fmt_descriptors[pix_fmt].name;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
enum PixelFormat avcodec_get_pix_fmt(const char *name)
{
    return av_get_pix_fmt(name);
}

void avcodec_pix_fmt_string (char *buf, int buf_size, enum PixelFormat pix_fmt)
{
    av_get_pix_fmt_string(buf, buf_size, pix_fmt);
}
#endif

int ff_is_hwaccel_pix_fmt(enum PixelFormat pix_fmt)
{
    return av_pix_fmt_descriptors[pix_fmt].flags & PIX_FMT_HWACCEL;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
int ff_set_systematic_pal(uint32_t pal[256], enum PixelFormat pix_fmt){
    return ff_set_systematic_pal2(pal, pix_fmt);
}

int ff_fill_linesize(AVPicture *picture, enum PixelFormat pix_fmt, int width)
{
    return av_image_fill_linesizes(picture->linesize, pix_fmt, width);
}

int ff_fill_pointer(AVPicture *picture, uint8_t *ptr, enum PixelFormat pix_fmt,
                    int height)
{
    return av_image_fill_pointers(picture->data, pix_fmt, height, ptr, picture->linesize);
}
#endif

int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   enum PixelFormat pix_fmt, int width, int height)
{
    int ret;

    if ((ret = av_image_check_size(width, height, 0, NULL)) < 0)
        return ret;

    if ((ret = av_image_fill_linesizes(picture->linesize, pix_fmt, width)) < 0)
        return ret;

    return av_image_fill_pointers(picture->data, pix_fmt, height, ptr, picture->linesize);
}

int avpicture_layout(const AVPicture* src, enum PixelFormat pix_fmt, int width, int height,
                     unsigned char *dest, int dest_size)
{
    int i, j, nb_planes = 0, linesizes[4];
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int size = avpicture_get_size(pix_fmt, width, height);

    if (size > dest_size || size < 0)
        return AVERROR(EINVAL);

    for (i = 0; i < desc->nb_components; i++)
        nb_planes = FFMAX(desc->comp[i].plane, nb_planes);
    nb_planes++;

    av_image_fill_linesizes(linesizes, pix_fmt, width);
    for (i = 0; i < nb_planes; i++) {
        int h, shift = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        const unsigned char *s = src->data[i];
        h = (height + (1 << shift) - 1) >> shift;

        for (j = 0; j < h; j++) {
            memcpy(dest, s, linesizes[i]);
            dest += linesizes[i];
            s += src->linesize[i];
        }
    }

    if (desc->flags & PIX_FMT_PAL)
        memcpy((unsigned char *)(((size_t)dest + 3) & ~3), src->data[1], 256 * 4);

    return size;
}

int avpicture_get_size(enum PixelFormat pix_fmt, int width, int height)
{
    AVPicture dummy_pict;
    if(av_image_check_size(width, height, 0, NULL))
        return -1;
    switch (pix_fmt) {
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_GRAY8:
        // do not include palette for these pseudo-paletted formats
        return width * height;
    }
    return avpicture_fill(&dummy_pict, NULL, pix_fmt, width, height);
}

int avcodec_get_pix_fmt_loss(enum PixelFormat dst_pix_fmt, enum PixelFormat src_pix_fmt,
                             int has_alpha)
{
    const PixFmtInfo *pf, *ps;
    const AVPixFmtDescriptor *src_desc = &av_pix_fmt_descriptors[src_pix_fmt];
    const AVPixFmtDescriptor *dst_desc = &av_pix_fmt_descriptors[dst_pix_fmt];
    int loss;

    ps = &pix_fmt_info[src_pix_fmt];

    /* compute loss */
    loss = 0;
    pf = &pix_fmt_info[dst_pix_fmt];
    if (pf->depth < ps->depth ||
        ((dst_pix_fmt == PIX_FMT_RGB555BE || dst_pix_fmt == PIX_FMT_RGB555LE ||
          dst_pix_fmt == PIX_FMT_BGR555BE || dst_pix_fmt == PIX_FMT_BGR555LE) &&
         (src_pix_fmt == PIX_FMT_RGB565BE || src_pix_fmt == PIX_FMT_RGB565LE ||
          src_pix_fmt == PIX_FMT_BGR565BE || src_pix_fmt == PIX_FMT_BGR565LE)))
        loss |= FF_LOSS_DEPTH;
    if (dst_desc->log2_chroma_w > src_desc->log2_chroma_w ||
        dst_desc->log2_chroma_h > src_desc->log2_chroma_h)
        loss |= FF_LOSS_RESOLUTION;
    switch(pf->color_type) {
    case FF_COLOR_RGB:
        if (ps->color_type != FF_COLOR_RGB &&
            ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_GRAY:
        if (ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV:
        if (ps->color_type != FF_COLOR_YUV)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV_JPEG:
        if (ps->color_type != FF_COLOR_YUV_JPEG &&
            ps->color_type != FF_COLOR_YUV &&
            ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    default:
        /* fail safe test */
        if (ps->color_type != pf->color_type)
            loss |= FF_LOSS_COLORSPACE;
        break;
    }
    if (pf->color_type == FF_COLOR_GRAY &&
        ps->color_type != FF_COLOR_GRAY)
        loss |= FF_LOSS_CHROMA;
    if (!pf->is_alpha && (ps->is_alpha && has_alpha))
        loss |= FF_LOSS_ALPHA;
    if (pf->pixel_type == FF_PIXEL_PALETTE &&
        (ps->pixel_type != FF_PIXEL_PALETTE && ps->color_type != FF_COLOR_GRAY))
        loss |= FF_LOSS_COLORQUANT;
    return loss;
}

static int avg_bits_per_pixel(enum PixelFormat pix_fmt)
{
    int bits;
    const PixFmtInfo *pf;
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];

    pf = &pix_fmt_info[pix_fmt];
    switch(pf->pixel_type) {
    case FF_PIXEL_PACKED:
        switch(pix_fmt) {
        case PIX_FMT_YUYV422:
        case PIX_FMT_UYVY422:
        case PIX_FMT_RGB565BE:
        case PIX_FMT_RGB565LE:
        case PIX_FMT_RGB555BE:
        case PIX_FMT_RGB555LE:
        case PIX_FMT_RGB444BE:
        case PIX_FMT_RGB444LE:
        case PIX_FMT_BGR565BE:
        case PIX_FMT_BGR565LE:
        case PIX_FMT_BGR555BE:
        case PIX_FMT_BGR555LE:
        case PIX_FMT_BGR444BE:
        case PIX_FMT_BGR444LE:
            bits = 16;
            break;
        case PIX_FMT_UYYVYY411:
            bits = 12;
            break;
        default:
            bits = pf->depth * pf->nb_channels;
            break;
        }
        break;
    case FF_PIXEL_PLANAR:
        if (desc->log2_chroma_w == 0 && desc->log2_chroma_h == 0) {
            bits = pf->depth * pf->nb_channels;
        } else {
            bits = pf->depth + ((2 * pf->depth) >>
                                (desc->log2_chroma_w + desc->log2_chroma_h));
        }
        break;
    case FF_PIXEL_PALETTE:
        bits = 8;
        break;
    default:
        bits = -1;
        break;
    }
    return bits;
}

static enum PixelFormat avcodec_find_best_pix_fmt1(int64_t pix_fmt_mask,
                                      enum PixelFormat src_pix_fmt,
                                      int has_alpha,
                                      int loss_mask)
{
    int dist, i, loss, min_dist;
    enum PixelFormat dst_pix_fmt;

    /* find exact color match with smallest size */
    dst_pix_fmt = PIX_FMT_NONE;
    min_dist = 0x7fffffff;
    for(i = 0;i < PIX_FMT_NB; i++) {
        if (pix_fmt_mask & (1ULL << i)) {
            loss = avcodec_get_pix_fmt_loss(i, src_pix_fmt, has_alpha) & loss_mask;
            if (loss == 0) {
                dist = avg_bits_per_pixel(i);
                if (dist < min_dist) {
                    min_dist = dist;
                    dst_pix_fmt = i;
                }
            }
        }
    }
    return dst_pix_fmt;
}

enum PixelFormat avcodec_find_best_pix_fmt(int64_t pix_fmt_mask, enum PixelFormat src_pix_fmt,
                              int has_alpha, int *loss_ptr)
{
    enum PixelFormat dst_pix_fmt;
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
        dst_pix_fmt = avcodec_find_best_pix_fmt1(pix_fmt_mask, src_pix_fmt,
                                                 has_alpha, loss_mask);
        if (dst_pix_fmt >= 0)
            goto found;
        if (loss_mask == 0)
            break;
    }
    return PIX_FMT_NONE;
 found:
    if (loss_ptr)
        *loss_ptr = avcodec_get_pix_fmt_loss(dst_pix_fmt, src_pix_fmt, has_alpha);
    return dst_pix_fmt;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
void ff_img_copy_plane(uint8_t *dst, int dst_wrap,
                           const uint8_t *src, int src_wrap,
                           int width, int height)
{
    av_image_copy_plane(dst, dst_wrap, src, src_wrap, width, height);
}

int ff_get_plane_bytewidth(enum PixelFormat pix_fmt, int width, int plane)
{
    return av_image_get_linesize(pix_fmt, width, plane);
}

void av_picture_data_copy(uint8_t *dst_data[4], int dst_linesize[4],
                          uint8_t *src_data[4], int src_linesize[4],
                          enum PixelFormat pix_fmt, int width, int height)
{
    av_image_copy(dst_data, dst_linesize, src_data, src_linesize,
                  pix_fmt, width, height);
}
#endif

void av_picture_copy(AVPicture *dst, const AVPicture *src,
                     enum PixelFormat pix_fmt, int width, int height)
{
    av_image_copy(dst->data, dst->linesize, src->data,
                  src->linesize, pix_fmt, width, height);
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


int avpicture_alloc(AVPicture *picture,
                    enum PixelFormat pix_fmt, int width, int height)
{
    int ret;

    if ((ret = av_image_alloc(picture->data, picture->linesize, width, height, pix_fmt, 1)) < 0) {
        memset(picture, 0, sizeof(AVPicture));
        return ret;
    }

    return 0;
}

void avpicture_free(AVPicture *picture)
{
    av_free(picture->data[0]);
}

/* return true if yuv planar */
static inline int is_yuv_planar(const PixFmtInfo *ps)
{
    return (ps->color_type == FF_COLOR_YUV ||
            ps->color_type == FF_COLOR_YUV_JPEG) &&
        ps->pixel_type == FF_PIXEL_PLANAR;
}

int av_picture_crop(AVPicture *dst, const AVPicture *src,
                    enum PixelFormat pix_fmt, int top_band, int left_band)
{
    int y_shift;
    int x_shift;

    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB || !is_yuv_planar(&pix_fmt_info[pix_fmt]))
        return -1;

    y_shift = av_pix_fmt_descriptors[pix_fmt].log2_chroma_h;
    x_shift = av_pix_fmt_descriptors[pix_fmt].log2_chroma_w;

    dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
    dst->data[1] = src->data[1] + ((top_band >> y_shift) * src->linesize[1]) + (left_band >> x_shift);
    dst->data[2] = src->data[2] + ((top_band >> y_shift) * src->linesize[2]) + (left_band >> x_shift);

    dst->linesize[0] = src->linesize[0];
    dst->linesize[1] = src->linesize[1];
    dst->linesize[2] = src->linesize[2];
    return 0;
}

int av_picture_pad(AVPicture *dst, const AVPicture *src, int height, int width,
                   enum PixelFormat pix_fmt, int padtop, int padbottom, int padleft, int padright,
            int *color)
{
    uint8_t *optr;
    int y_shift;
    int x_shift;
    int yheight;
    int i, y;

    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB ||
        !is_yuv_planar(&pix_fmt_info[pix_fmt])) return -1;

    for (i = 0; i < 3; i++) {
        x_shift = i ? av_pix_fmt_descriptors[pix_fmt].log2_chroma_w : 0;
        y_shift = i ? av_pix_fmt_descriptors[pix_fmt].log2_chroma_h : 0;

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

/* NOTE: we scan all the pixels to have an exact information */
static int get_alpha_info_pal8(const AVPicture *src, int width, int height)
{
    const unsigned char *p;
    int src_wrap, ret, x, y;
    unsigned int a;
    uint32_t *palette = (uint32_t *)src->data[1];

    p = src->data[0];
    src_wrap = src->linesize[0] - width;
    ret = 0;
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            a = palette[p[0]] >> 24;
            if (a == 0x00) {
                ret |= FF_ALPHA_TRANSP;
            } else if (a != 0xff) {
                ret |= FF_ALPHA_SEMI_TRANSP;
            }
            p++;
        }
        p += src_wrap;
    }
    return ret;
}

int img_get_alpha_info(const AVPicture *src,
                       enum PixelFormat pix_fmt, int width, int height)
{
    const PixFmtInfo *pf = &pix_fmt_info[pix_fmt];
    int ret;

    /* no alpha can be represented in format */
    if (!pf->is_alpha)
        return 0;
    switch(pix_fmt) {
    case PIX_FMT_PAL8:
        ret = get_alpha_info_pal8(src, width, height);
        break;
    default:
        /* we do not know, so everything is indicated */
        ret = FF_ALPHA_TRANSP | FF_ALPHA_SEMI_TRANSP;
        break;
    }
    return ret;
}

#if !(HAVE_MMX && HAVE_YASM)
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
#endif

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
    buf = (uint8_t*)av_malloc(width);

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
                          enum PixelFormat pix_fmt, int width, int height)
{
    int i;

    if (pix_fmt != PIX_FMT_YUV420P &&
        pix_fmt != PIX_FMT_YUVJ420P &&
        pix_fmt != PIX_FMT_YUV422P &&
        pix_fmt != PIX_FMT_YUVJ422P &&
        pix_fmt != PIX_FMT_YUV444P &&
        pix_fmt != PIX_FMT_YUV411P &&
        pix_fmt != PIX_FMT_GRAY8)
        return -1;
    if ((width & 3) != 0 || (height & 3) != 0)
        return -1;

    for(i=0;i<3;i++) {
        if (i == 1) {
            switch(pix_fmt) {
            case PIX_FMT_YUVJ420P:
            case PIX_FMT_YUV420P:
                width >>= 1;
                height >>= 1;
                break;
            case PIX_FMT_YUV422P:
            case PIX_FMT_YUVJ422P:
                width >>= 1;
                break;
            case PIX_FMT_YUV411P:
                width >>= 2;
                break;
            default:
                break;
            }
            if (pix_fmt == PIX_FMT_GRAY8) {
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

