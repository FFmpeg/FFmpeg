/*
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
 * misc image utilities
 */

#include "avassert.h"
#include "common.h"
#include "imgutils.h"
#include "internal.h"
#include "intreadwrite.h"
#include "log.h"
#include "mathematics.h"
#include "pixdesc.h"
#include "rational.h"

void av_image_fill_max_pixsteps(int max_pixsteps[4], int max_pixstep_comps[4],
                                const AVPixFmtDescriptor *pixdesc)
{
    int i;
    memset(max_pixsteps, 0, 4*sizeof(max_pixsteps[0]));
    if (max_pixstep_comps)
        memset(max_pixstep_comps, 0, 4*sizeof(max_pixstep_comps[0]));

    for (i = 0; i < 4; i++) {
        const AVComponentDescriptor *comp = &(pixdesc->comp[i]);
        if (comp->step > max_pixsteps[comp->plane]) {
            max_pixsteps[comp->plane] = comp->step;
            if (max_pixstep_comps)
                max_pixstep_comps[comp->plane] = i;
        }
    }
}

static inline
int image_get_linesize(int width, int plane,
                       int max_step, int max_step_comp,
                       const AVPixFmtDescriptor *desc)
{
    int s, shifted_w, linesize;

    if (!desc)
        return AVERROR(EINVAL);

    if (width < 0)
        return AVERROR(EINVAL);
    s = (max_step_comp == 1 || max_step_comp == 2) ? desc->log2_chroma_w : 0;
    shifted_w = ((width + (1 << s) - 1)) >> s;
    if (shifted_w && max_step > INT_MAX / shifted_w)
        return AVERROR(EINVAL);
    linesize = max_step * shifted_w;

    if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM)
        linesize = (linesize + 7) >> 3;
    return linesize;
}

int av_image_get_linesize(enum AVPixelFormat pix_fmt, int width, int plane)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int max_step     [4];       /* max pixel step for each plane */
    int max_step_comp[4];       /* the component for each plane which has the max pixel step */

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return AVERROR(EINVAL);

    av_image_fill_max_pixsteps(max_step, max_step_comp, desc);
    return image_get_linesize(width, plane, max_step[plane], max_step_comp[plane], desc);
}

int av_image_fill_linesizes(int linesizes[4], enum AVPixelFormat pix_fmt, int width)
{
    int i, ret;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int max_step     [4];       /* max pixel step for each plane */
    int max_step_comp[4];       /* the component for each plane which has the max pixel step */

    memset(linesizes, 0, 4*sizeof(linesizes[0]));

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return AVERROR(EINVAL);

    av_image_fill_max_pixsteps(max_step, max_step_comp, desc);
    for (i = 0; i < 4; i++) {
        if ((ret = image_get_linesize(width, i, max_step[i], max_step_comp[i], desc)) < 0)
            return ret;
        linesizes[i] = ret;
    }

    return 0;
}

int av_image_fill_pointers(uint8_t *data[4], enum AVPixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4])
{
    int i, total_size, size[4] = { 0 }, has_plane[4] = { 0 };

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    memset(data     , 0, sizeof(data[0])*4);

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return AVERROR(EINVAL);

    data[0] = ptr;
    if (linesizes[0] > (INT_MAX - 1024) / height)
        return AVERROR(EINVAL);
    size[0] = linesizes[0] * height;

    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) {
        size[0] = (size[0] + 3) & ~3;
        data[1] = ptr + size[0]; /* palette is stored here as 256 32 bits words */
        return size[0] + 256 * 4;
    }

    for (i = 0; i < 4; i++)
        has_plane[desc->comp[i].plane] = 1;

    total_size = size[0];
    for (i = 1; i < 4 && has_plane[i]; i++) {
        int h, s = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        data[i] = data[i-1] + size[i-1];
        h = (height + (1 << s) - 1) >> s;
        if (linesizes[i] > INT_MAX / h)
            return AVERROR(EINVAL);
        size[i] = h * linesizes[i];
        if (total_size > INT_MAX - size[i])
            return AVERROR(EINVAL);
        total_size += size[i];
    }

    return total_size;
}

int avpriv_set_systematic_pal2(uint32_t pal[256], enum AVPixelFormat pix_fmt)
{
    int i;

    for (i = 0; i < 256; i++) {
        int r, g, b;

        switch (pix_fmt) {
        case AV_PIX_FMT_RGB8:
            r = (i>>5    )*36;
            g = ((i>>2)&7)*36;
            b = (i&3     )*85;
            break;
        case AV_PIX_FMT_BGR8:
            b = (i>>6    )*85;
            g = ((i>>3)&7)*36;
            r = (i&7     )*36;
            break;
        case AV_PIX_FMT_RGB4_BYTE:
            r = (i>>3    )*255;
            g = ((i>>1)&3)*85;
            b = (i&1     )*255;
            break;
        case AV_PIX_FMT_BGR4_BYTE:
            b = (i>>3    )*255;
            g = ((i>>1)&3)*85;
            r = (i&1     )*255;
            break;
        case AV_PIX_FMT_GRAY8:
            r = b = g = i;
            break;
        default:
            return AVERROR(EINVAL);
        }
        pal[i] = b + (g << 8) + (r << 16) + (0xFFU << 24);
    }

    return 0;
}

int av_image_alloc(uint8_t *pointers[4], int linesizes[4],
                   int w, int h, enum AVPixelFormat pix_fmt, int align)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i, ret;
    uint8_t *buf;

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(w, h, 0, NULL)) < 0)
        return ret;
    if ((ret = av_image_fill_linesizes(linesizes, pix_fmt, align>7 ? FFALIGN(w, 8) : w)) < 0)
        return ret;

    for (i = 0; i < 4; i++)
        linesizes[i] = FFALIGN(linesizes[i], align);

    if ((ret = av_image_fill_pointers(pointers, pix_fmt, h, NULL, linesizes)) < 0)
        return ret;
    buf = av_malloc(ret + align);
    if (!buf)
        return AVERROR(ENOMEM);
    if ((ret = av_image_fill_pointers(pointers, pix_fmt, h, buf, linesizes)) < 0) {
        av_free(buf);
        return ret;
    }
    if (desc->flags & AV_PIX_FMT_FLAG_PAL || desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        avpriv_set_systematic_pal2((uint32_t*)pointers[1], pix_fmt);

    if ((desc->flags & AV_PIX_FMT_FLAG_PAL ||
         desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) &&
        pointers[1] - pointers[0] > linesizes[0] * h) {
        /* zero-initialize the padding before the palette */
        memset(pointers[0] + linesizes[0] * h, 0,
               pointers[1] - pointers[0] - linesizes[0] * h);
    }

    return ret;
}

typedef struct ImgUtils {
    const AVClass *class;
    int   log_offset;
    void *log_ctx;
} ImgUtils;

static const AVClass imgutils_class = {
    .class_name = "IMGUTILS",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = offsetof(ImgUtils, log_offset),
    .parent_log_context_offset = offsetof(ImgUtils, log_ctx),
};

int av_image_check_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx)
{
    ImgUtils imgutils = {
        .class      = &imgutils_class,
        .log_offset = log_offset,
        .log_ctx    = log_ctx,
    };

    if ((int)w>0 && (int)h>0 && (w+128)*(uint64_t)(h+128) < INT_MAX/8)
        return 0;

    av_log(&imgutils, AV_LOG_ERROR, "Picture size %ux%u is invalid\n", w, h);
    return AVERROR(EINVAL);
}

int av_image_check_sar(unsigned int w, unsigned int h, AVRational sar)
{
    int64_t scaled_dim;

    if (sar.den <= 0 || sar.num < 0)
        return AVERROR(EINVAL);

    if (!sar.num || sar.num == sar.den)
        return 0;

    if (sar.num < sar.den)
        scaled_dim = av_rescale_rnd(w, sar.num, sar.den, AV_ROUND_ZERO);
    else
        scaled_dim = av_rescale_rnd(h, sar.den, sar.num, AV_ROUND_ZERO);

    if (scaled_dim > 0)
        return 0;

    return AVERROR(EINVAL);
}

void av_image_copy_plane(uint8_t       *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height)
{
    if (!dst || !src)
        return;
    av_assert0(abs(src_linesize) >= bytewidth);
    av_assert0(abs(dst_linesize) >= bytewidth);
    for (;height > 0; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}

void av_image_copy(uint8_t *dst_data[4], int dst_linesizes[4],
                   const uint8_t *src_data[4], const int src_linesizes[4],
                   enum AVPixelFormat pix_fmt, int width, int height)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return;

    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) {
        av_image_copy_plane(dst_data[0], dst_linesizes[0],
                            src_data[0], src_linesizes[0],
                            width, height);
        /* copy the palette */
        memcpy(dst_data[1], src_data[1], 4*256);
    } else {
        int i, planes_nb = 0;

        for (i = 0; i < desc->nb_components; i++)
            planes_nb = FFMAX(planes_nb, desc->comp[i].plane + 1);

        for (i = 0; i < planes_nb; i++) {
            int h = height;
            int bwidth = av_image_get_linesize(pix_fmt, width, i);
            if (bwidth < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_image_get_linesize failed\n");
                return;
            }
            if (i == 1 || i == 2) {
                h = FF_CEIL_RSHIFT(height, desc->log2_chroma_h);
            }
            av_image_copy_plane(dst_data[i], dst_linesizes[i],
                                src_data[i], src_linesizes[i],
                                bwidth, h);
        }
    }
}

int av_image_fill_arrays(uint8_t *dst_data[4], int dst_linesize[4],
                         const uint8_t *src, enum AVPixelFormat pix_fmt,
                         int width, int height, int align)
{
    int ret, i;

    ret = av_image_check_size(width, height, 0, NULL);
    if (ret < 0)
        return ret;

    ret = av_image_fill_linesizes(dst_linesize, pix_fmt, width);
    if (ret < 0)
        return ret;

    for (i = 0; i < 4; i++)
        dst_linesize[i] = FFALIGN(dst_linesize[i], align);

    return av_image_fill_pointers(dst_data, pix_fmt, height, (uint8_t *)src, dst_linesize);
}

int av_image_get_buffer_size(enum AVPixelFormat pix_fmt,
                             int width, int height, int align)
{
    uint8_t *data[4];
    int linesize[4];
    int ret;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    ret = av_image_check_size(width, height, 0, NULL);
    if (ret < 0)
        return ret;

    // do not include palette for these pseudo-paletted formats
    if (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        return width * height;

    return av_image_fill_arrays(data, linesize, NULL, pix_fmt,
                                width, height, align);
}

int av_image_copy_to_buffer(uint8_t *dst, int dst_size,
                            const uint8_t * const src_data[4],
                            const int src_linesize[4],
                            enum AVPixelFormat pix_fmt,
                            int width, int height, int align)
{
    int i, j, nb_planes = 0, linesize[4];
    int size = av_image_get_buffer_size(pix_fmt, width, height, align);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    if (size > dst_size || size < 0 || !desc)
        return AVERROR(EINVAL);

    for (i = 0; i < desc->nb_components; i++)
        nb_planes = FFMAX(desc->comp[i].plane, nb_planes);

    nb_planes++;

    av_image_fill_linesizes(linesize, pix_fmt, width);
    for (i = 0; i < nb_planes; i++) {
        int h, shift = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        const uint8_t *src = src_data[i];
        h = (height + (1 << shift) - 1) >> shift;

        for (j = 0; j < h; j++) {
            memcpy(dst, src, linesize[i]);
            dst += FFALIGN(linesize[i], align);
            src += src_linesize[i];
        }
    }

    if (desc->flags & AV_PIX_FMT_FLAG_PAL) {
        uint32_t *d32 = (uint32_t *)(((size_t)dst + 3) & ~3);
        for (i = 0; i<256; i++)
            AV_WL32(d32 + i, AV_RN32(src_data[1] + 4*i));
    }

    return size;
}
