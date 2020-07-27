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
#include "imgutils_internal.h"
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

int av_image_fill_plane_sizes(size_t sizes[4], enum AVPixelFormat pix_fmt,
                              int height, const ptrdiff_t linesizes[4])
{
    int i, has_plane[4] = { 0 };

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    memset(sizes    , 0, sizeof(sizes[0])*4);

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return AVERROR(EINVAL);

    if (linesizes[0] > SIZE_MAX / height)
        return AVERROR(EINVAL);
    sizes[0] = linesizes[0] * (size_t)height;

    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & FF_PSEUDOPAL) {
        sizes[1] = 256 * 4; /* palette is stored here as 256 32 bits words */
        return 0;
    }

    for (i = 0; i < 4; i++)
        has_plane[desc->comp[i].plane] = 1;

    for (i = 1; i < 4 && has_plane[i]; i++) {
        int h, s = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        h = (height + (1 << s) - 1) >> s;
        if (linesizes[i] > SIZE_MAX / h)
            return AVERROR(EINVAL);
        sizes[i] = (size_t)h * linesizes[i];
    }

    return 0;
}

int av_image_fill_pointers(uint8_t *data[4], enum AVPixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4])
{
    int i, ret;
    ptrdiff_t linesizes1[4];
    size_t sizes[4];

    memset(data     , 0, sizeof(data[0])*4);

    for (i = 0; i < 4; i++)
        linesizes1[i] = linesizes[i];

    ret = av_image_fill_plane_sizes(sizes, pix_fmt, height, linesizes1);
    if (ret < 0)
        return ret;

    ret = 0;
    for (i = 0; i < 4; i++) {
        if (sizes[i] > INT_MAX - ret)
            return AVERROR(EINVAL);
        ret += sizes[i];
    }

    data[0] = ptr;
    for (i = 1; i < 4 && sizes[i]; i++)
        data[i] = data[i - 1] + sizes[i - 1];

    return ret;
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
    ptrdiff_t linesizes1[4];
    size_t total_size, sizes[4];
    uint8_t *buf;

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(w, h, 0, NULL)) < 0)
        return ret;
    if ((ret = av_image_fill_linesizes(linesizes, pix_fmt, align>7 ? FFALIGN(w, 8) : w)) < 0)
        return ret;

    for (i = 0; i < 4; i++) {
        linesizes[i] = FFALIGN(linesizes[i], align);
        linesizes1[i] = linesizes[i];
    }

    if ((ret = av_image_fill_plane_sizes(sizes, pix_fmt, h, linesizes1)) < 0)
        return ret;
    total_size = align;
    for (i = 0; i < 4; i++) {
        if (total_size > SIZE_MAX - sizes[i])
            return AVERROR(EINVAL);
        total_size += sizes[i];
    }
    buf = av_malloc(total_size);
    if (!buf)
        return AVERROR(ENOMEM);
    if ((ret = av_image_fill_pointers(pointers, pix_fmt, h, buf, linesizes)) < 0) {
        av_free(buf);
        return ret;
    }
    if (desc->flags & AV_PIX_FMT_FLAG_PAL || (desc->flags & FF_PSEUDOPAL && pointers[1])) {
        avpriv_set_systematic_pal2((uint32_t*)pointers[1], pix_fmt);
        if (align < 4) {
            av_log(NULL, AV_LOG_ERROR, "Formats with a palette require a minimum alignment of 4\n");
            av_free(buf);
            return AVERROR(EINVAL);
        }
    }

    if ((desc->flags & AV_PIX_FMT_FLAG_PAL ||
         desc->flags & FF_PSEUDOPAL) && pointers[1] &&
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
    .class_name                = "IMGUTILS",
    .item_name                 = av_default_item_name,
    .option                    = NULL,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = offsetof(ImgUtils, log_offset),
    .parent_log_context_offset = offsetof(ImgUtils, log_ctx),
};

int av_image_check_size2(unsigned int w, unsigned int h, int64_t max_pixels, enum AVPixelFormat pix_fmt, int log_offset, void *log_ctx)
{
    ImgUtils imgutils = {
        .class      = &imgutils_class,
        .log_offset = log_offset,
        .log_ctx    = log_ctx,
    };
    int64_t stride = av_image_get_linesize(pix_fmt, w, 0);
    if (stride <= 0)
        stride = 8LL*w;
    stride += 128*8;

    if ((int)w<=0 || (int)h<=0 || stride >= INT_MAX || stride*(uint64_t)(h+128) >= INT_MAX) {
        av_log(&imgutils, AV_LOG_ERROR, "Picture size %ux%u is invalid\n", w, h);
        return AVERROR(EINVAL);
    }

    if (max_pixels < INT64_MAX) {
        if (w*(int64_t)h > max_pixels) {
            av_log(&imgutils, AV_LOG_ERROR,
                    "Picture size %ux%u exceeds specified max pixel count %"PRId64", see the documentation if you wish to increase it\n",
                    w, h, max_pixels);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

int av_image_check_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx)
{
    return av_image_check_size2(w, h, INT64_MAX, AV_PIX_FMT_NONE, log_offset, log_ctx);
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

static void image_copy_plane(uint8_t       *dst, ptrdiff_t dst_linesize,
                             const uint8_t *src, ptrdiff_t src_linesize,
                             ptrdiff_t bytewidth, int height)
{
    if (!dst || !src)
        return;
    av_assert0(FFABS(src_linesize) >= bytewidth);
    av_assert0(FFABS(dst_linesize) >= bytewidth);
    for (;height > 0; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}

static void image_copy_plane_uc_from(uint8_t       *dst, ptrdiff_t dst_linesize,
                                     const uint8_t *src, ptrdiff_t src_linesize,
                                     ptrdiff_t bytewidth, int height)
{
    int ret = -1;

#if ARCH_X86
    ret = ff_image_copy_plane_uc_from_x86(dst, dst_linesize, src, src_linesize,
                                          bytewidth, height);
#endif

    if (ret < 0)
        image_copy_plane(dst, dst_linesize, src, src_linesize, bytewidth, height);
}

void av_image_copy_plane(uint8_t       *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height)
{
    image_copy_plane(dst, dst_linesize, src, src_linesize, bytewidth, height);
}

static void image_copy(uint8_t *dst_data[4], const ptrdiff_t dst_linesizes[4],
                       const uint8_t *src_data[4], const ptrdiff_t src_linesizes[4],
                       enum AVPixelFormat pix_fmt, int width, int height,
                       void (*copy_plane)(uint8_t *, ptrdiff_t, const uint8_t *,
                                          ptrdiff_t, ptrdiff_t, int))
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return;

    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & FF_PSEUDOPAL) {
        copy_plane(dst_data[0], dst_linesizes[0],
                   src_data[0], src_linesizes[0],
                   width, height);
        /* copy the palette */
        if ((desc->flags & AV_PIX_FMT_FLAG_PAL) || (dst_data[1] && src_data[1]))
            memcpy(dst_data[1], src_data[1], 4*256);
    } else {
        int i, planes_nb = 0;

        for (i = 0; i < desc->nb_components; i++)
            planes_nb = FFMAX(planes_nb, desc->comp[i].plane + 1);

        for (i = 0; i < planes_nb; i++) {
            int h = height;
            ptrdiff_t bwidth = av_image_get_linesize(pix_fmt, width, i);
            if (bwidth < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_image_get_linesize failed\n");
                return;
            }
            if (i == 1 || i == 2) {
                h = AV_CEIL_RSHIFT(height, desc->log2_chroma_h);
            }
            copy_plane(dst_data[i], dst_linesizes[i],
                       src_data[i], src_linesizes[i],
                       bwidth, h);
        }
    }
}

void av_image_copy(uint8_t *dst_data[4], int dst_linesizes[4],
                   const uint8_t *src_data[4], const int src_linesizes[4],
                   enum AVPixelFormat pix_fmt, int width, int height)
{
    ptrdiff_t dst_linesizes1[4], src_linesizes1[4];
    int i;

    for (i = 0; i < 4; i++) {
        dst_linesizes1[i] = dst_linesizes[i];
        src_linesizes1[i] = src_linesizes[i];
    }

    image_copy(dst_data, dst_linesizes1, src_data, src_linesizes1, pix_fmt,
               width, height, image_copy_plane);
}

void av_image_copy_uc_from(uint8_t *dst_data[4], const ptrdiff_t dst_linesizes[4],
                           const uint8_t *src_data[4], const ptrdiff_t src_linesizes[4],
                           enum AVPixelFormat pix_fmt, int width, int height)
{
    image_copy(dst_data, dst_linesizes, src_data, src_linesizes, pix_fmt,
               width, height, image_copy_plane_uc_from);
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
    int ret, i;
    int linesize[4];
    ptrdiff_t aligned_linesize[4];
    size_t sizes[4];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    ret = av_image_check_size(width, height, 0, NULL);
    if (ret < 0)
        return ret;

    // do not include palette for these pseudo-paletted formats
    if (desc->flags & FF_PSEUDOPAL)
        return FFALIGN(width, align) * height;

    ret = av_image_fill_linesizes(linesize, pix_fmt, width);
    if (ret < 0)
        return ret;

    for (i = 0; i < 4; i++)
        aligned_linesize[i] = FFALIGN(linesize[i], align);

    ret = av_image_fill_plane_sizes(sizes, pix_fmt, height, aligned_linesize);
    if (ret < 0)
        return ret;

    ret = 0;
    for (i = 0; i < 4; i++) {
        if (sizes[i] > INT_MAX - ret)
            return AVERROR(EINVAL);
        ret += sizes[i];
    }
    return ret;
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
    int ret;

    if (size > dst_size || size < 0 || !desc)
        return AVERROR(EINVAL);

    for (i = 0; i < desc->nb_components; i++)
        nb_planes = FFMAX(desc->comp[i].plane, nb_planes);

    nb_planes++;

    ret = av_image_fill_linesizes(linesize, pix_fmt, width);
    av_assert0(ret >= 0); // was checked previously

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
        uint32_t *d32 = (uint32_t *)dst;

        for (i = 0; i<256; i++)
            AV_WL32(d32 + i, AV_RN32(src_data[1] + 4*i));
    }

    return size;
}

// Fill dst[0..dst_size] with the bytes in clear[0..clear_size]. The clear
// bytes are repeated until dst_size is reached. If dst_size is unaligned (i.e.
// dst_size%clear_size!=0), the remaining data will be filled with the beginning
// of the clear data only.
static void memset_bytes(uint8_t *dst, size_t dst_size, uint8_t *clear,
                         size_t clear_size)
{
    int same = 1;
    int i;

    if (!clear_size)
        return;

    // Reduce to memset() if possible.
    for (i = 0; i < clear_size; i++) {
        if (clear[i] != clear[0]) {
            same = 0;
            break;
        }
    }
    if (same)
        clear_size = 1;

    if (clear_size == 1) {
        memset(dst, clear[0], dst_size);
    } else {
        if (clear_size > dst_size)
            clear_size = dst_size;
        memcpy(dst, clear, clear_size);
        av_memcpy_backptr(dst + clear_size, clear_size, dst_size - clear_size);
    }
}

// Maximum size in bytes of a plane element (usually a pixel, or multiple pixels
// if it's a subsampled packed format).
#define MAX_BLOCK_SIZE 32

int av_image_fill_black(uint8_t *dst_data[4], const ptrdiff_t dst_linesize[4],
                        enum AVPixelFormat pix_fmt, enum AVColorRange range,
                        int width, int height)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int nb_planes = av_pix_fmt_count_planes(pix_fmt);
    // A pixel or a group of pixels on each plane, with a value that represents black.
    // Consider e.g. AV_PIX_FMT_UYVY422 for non-trivial cases.
    uint8_t clear_block[4][MAX_BLOCK_SIZE] = {{0}}; // clear padding with 0
    int clear_block_size[4] = {0};
    ptrdiff_t plane_line_bytes[4] = {0};
    int rgb, limited;
    int plane, c;

    if (!desc || nb_planes < 1 || nb_planes > 4 || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
        return AVERROR(EINVAL);

    rgb = !!(desc->flags & AV_PIX_FMT_FLAG_RGB);
    limited = !rgb && range != AVCOL_RANGE_JPEG;

    if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) {
        ptrdiff_t bytewidth = av_image_get_linesize(pix_fmt, width, 0);
        uint8_t *data;
        int mono = pix_fmt == AV_PIX_FMT_MONOWHITE || pix_fmt == AV_PIX_FMT_MONOBLACK;
        int fill = pix_fmt == AV_PIX_FMT_MONOWHITE ? 0xFF : 0;
        if (nb_planes != 1 || !(rgb || mono) || bytewidth < 1)
            return AVERROR(EINVAL);

        if (!dst_data)
            return 0;

        data = dst_data[0];

        // (Bitstream + alpha will be handled incorrectly - it'll remain transparent.)
        for (;height > 0; height--) {
            memset(data, fill, bytewidth);
            data += dst_linesize[0];
        }
        return 0;
    }

    for (c = 0; c < desc->nb_components; c++) {
        const AVComponentDescriptor comp = desc->comp[c];

        // We try to operate on entire non-subsampled pixel groups (for
        // AV_PIX_FMT_UYVY422 this would mean two consecutive pixels).
        clear_block_size[comp.plane] = FFMAX(clear_block_size[comp.plane], comp.step);

        if (clear_block_size[comp.plane] > MAX_BLOCK_SIZE)
            return AVERROR(EINVAL);
    }

    // Create a byte array for clearing 1 pixel (sometimes several pixels).
    for (c = 0; c < desc->nb_components; c++) {
        const AVComponentDescriptor comp = desc->comp[c];
        // (Multiple pixels happen e.g. with AV_PIX_FMT_UYVY422.)
        int w = clear_block_size[comp.plane] / comp.step;
        uint8_t *c_data[4];
        const int c_linesize[4] = {0};
        uint16_t src_array[MAX_BLOCK_SIZE];
        uint16_t src = 0;
        int x;

        if (comp.depth > 16)
            return AVERROR(EINVAL);
        if (!rgb && comp.depth < 8)
            return AVERROR(EINVAL);
        if (w < 1)
            return AVERROR(EINVAL);

        if (c == 0 && limited) {
            src = 16 << (comp.depth - 8);
        } else if ((c == 1 || c == 2) && !rgb) {
            src = 128 << (comp.depth - 8);
        } else if (c == 3) {
            // (Assume even limited YUV uses full range alpha.)
            src = (1 << comp.depth) - 1;
        }

        for (x = 0; x < w; x++)
            src_array[x] = src;

        for (x = 0; x < 4; x++)
            c_data[x] = &clear_block[x][0];

        av_write_image_line(src_array, c_data, c_linesize, desc, 0, 0, c, w);
    }

    for (plane = 0; plane < nb_planes; plane++) {
        plane_line_bytes[plane] = av_image_get_linesize(pix_fmt, width, plane);
        if (plane_line_bytes[plane] < 0)
            return AVERROR(EINVAL);
    }

    if (!dst_data)
        return 0;

    for (plane = 0; plane < nb_planes; plane++) {
        size_t bytewidth = plane_line_bytes[plane];
        uint8_t *data = dst_data[plane];
        int chroma_div = plane == 1 || plane == 2 ? desc->log2_chroma_h : 0;
        int plane_h = ((height + ( 1 << chroma_div) - 1)) >> chroma_div;

        for (; plane_h > 0; plane_h--) {
            memset_bytes(data, bytewidth, &clear_block[plane][0], clear_block_size[plane]);
            data += dst_linesize[plane];
        }
    }

    return 0;
}
