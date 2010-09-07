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

#include "imgutils.h"
#include "libavutil/pixdesc.h"

void av_image_fill_max_pixsteps(int max_pixsteps[4], int max_pixstep_comps[4],
                                const AVPixFmtDescriptor *pixdesc)
{
    int i;
    memset(max_pixsteps, 0, 4*sizeof(max_pixsteps[0]));
    if (max_pixstep_comps)
        memset(max_pixstep_comps, 0, 4*sizeof(max_pixstep_comps[0]));

    for (i = 0; i < 4; i++) {
        const AVComponentDescriptor *comp = &(pixdesc->comp[i]);
        if ((comp->step_minus1+1) > max_pixsteps[comp->plane]) {
            max_pixsteps[comp->plane] = comp->step_minus1+1;
            if (max_pixstep_comps)
                max_pixstep_comps[comp->plane] = i;
        }
    }
}

int av_image_get_linesize(enum PixelFormat pix_fmt, int width, int plane)
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int max_step     [4];       /* max pixel step for each plane */
    int max_step_comp[4];       /* the component for each plane which has the max pixel step */
    int s;

    if (desc->flags & PIX_FMT_BITSTREAM)
        return (width * (desc->comp[0].step_minus1+1) + 7) >> 3;

    av_image_fill_max_pixsteps(max_step, max_step_comp, desc);
    s = (max_step_comp[plane] == 1 || max_step_comp[plane] == 2) ? desc->log2_chroma_w : 0;
    return max_step[plane] * (((width + (1 << s) - 1)) >> s);
}

int av_image_fill_linesizes(int linesizes[4], enum PixelFormat pix_fmt, int width)
{
    int i;
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int max_step     [4];       /* max pixel step for each plane */
    int max_step_comp[4];       /* the component for each plane which has the max pixel step */

    memset(linesizes, 0, 4*sizeof(linesizes[0]));

    if ((unsigned)pix_fmt >= PIX_FMT_NB || desc->flags & PIX_FMT_HWACCEL)
        return AVERROR(EINVAL);

    if (desc->flags & PIX_FMT_BITSTREAM) {
        linesizes[0] = (width * (desc->comp[0].step_minus1+1) + 7) >> 3;
        return 0;
    }

    av_image_fill_max_pixsteps(max_step, max_step_comp, desc);
    for (i = 0; i < 4; i++) {
        int s = (max_step_comp[i] == 1 || max_step_comp[i] == 2) ? desc->log2_chroma_w : 0;
        linesizes[i] = max_step[i] * (((width + (1 << s) - 1)) >> s);
    }

    return 0;
}

int av_image_fill_pointers(uint8_t *data[4], enum PixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4])
{
    int i, total_size, size[4], has_plane[4];

    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    memset(data     , 0, sizeof(data[0])*4);
    memset(size     , 0, sizeof(size));
    memset(has_plane, 0, sizeof(has_plane));

    if ((unsigned)pix_fmt >= PIX_FMT_NB || desc->flags & PIX_FMT_HWACCEL)
        return AVERROR(EINVAL);

    data[0] = ptr;
    size[0] = linesizes[0] * height;

    if (desc->flags & PIX_FMT_PAL) {
        size[0] = (size[0] + 3) & ~3;
        data[1] = ptr + size[0]; /* palette is stored here as 256 32 bits words */
        return size[0] + 256 * 4;
    }

    for (i = 0; i < 4; i++)
        has_plane[desc->comp[i].plane] = 1;

    total_size = size[0];
    for (i = 1; has_plane[i] && i < 4; i++) {
        int h, s = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        data[i] = data[i-1] + size[i-1];
        h = (height + (1 << s) - 1) >> s;
        size[i] = h * linesizes[i];
        total_size += size[i];
    }

    return total_size;
}

typedef struct ImgUtils {
    const AVClass *class;
    int   log_offset;
    void *log_ctx;
} ImgUtils;

static const AVClass imgutils_class = { "IMGUTILS", av_default_item_name, NULL, LIBAVUTIL_VERSION_INT, offsetof(ImgUtils, log_offset), offsetof(ImgUtils, log_ctx) };

int av_image_check_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx)
{
    ImgUtils imgutils = { &imgutils_class, log_offset, log_ctx };

    if ((int)w>0 && (int)h>0 && (w+128)*(uint64_t)(h+128) < INT_MAX/8)
        return 0;

    av_log(&imgutils, AV_LOG_ERROR, "Picture size %ux%u is invalid\n", w, h);
    return AVERROR(EINVAL);
}

void av_image_copy_plane(uint8_t       *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height)
{
    if (!dst || !src)
        return;
    for (;height > 0; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}

void av_image_copy(uint8_t *dst_data[4], int dst_linesizes[4],
                   const uint8_t *src_data[4], const int src_linesizes[4],
                   enum PixelFormat pix_fmt, int width, int height)
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];

    if (desc->flags & PIX_FMT_HWACCEL)
        return;

    if (desc->flags & PIX_FMT_PAL) {
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
            if (i == 1 || i == 2) {
                h= -((-height)>>desc->log2_chroma_h);
            }
            av_image_copy_plane(dst_data[i], dst_linesizes[i],
                                src_data[i], src_linesizes[i],
                                bwidth, h);
        }
    }
}

#if FF_API_OLD_IMAGE_NAMES
void av_fill_image_max_pixsteps(int max_pixsteps[4], int max_pixstep_comps[4],
                                const AVPixFmtDescriptor *pixdesc)
{
    av_image_fill_max_pixsteps(max_pixsteps, max_pixstep_comps, pixdesc);
}

int av_get_image_linesize(enum PixelFormat pix_fmt, int width, int plane)
{
    return av_image_get_linesize(pix_fmt, width, plane);
}

int av_fill_image_linesizes(int linesizes[4], enum PixelFormat pix_fmt, int width)
{
    return av_image_fill_linesizes(linesizes, pix_fmt, width);
}

int av_fill_image_pointers(uint8_t *data[4], enum PixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4])
{
    return av_image_fill_pointers(data, pix_fmt, height, ptr, linesizes);
}

int av_check_image_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx)
{
    return av_image_check_size(w, h, log_offset, log_ctx);
}
#endif
