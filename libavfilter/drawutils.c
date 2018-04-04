/*
 * Copyright 2011 Stefano Sabatini <stefano.sabatini-lala poste it>
 * Copyright 2012 Nicolas George <nicolas.george normalesup org>
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

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/colorspace.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "drawutils.h"
#include "formats.h"

enum { RED = 0, GREEN, BLUE, ALPHA };

int ff_fill_rgba_map(uint8_t *rgba_map, enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_ARGB:  rgba_map[ALPHA] = 0; rgba_map[RED  ] = 1; rgba_map[GREEN] = 2; rgba_map[BLUE ] = 3; break;
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_ABGR:  rgba_map[ALPHA] = 0; rgba_map[BLUE ] = 1; rgba_map[GREEN] = 2; rgba_map[RED  ] = 3; break;
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGB48BE:
    case AV_PIX_FMT_RGBA64BE:
    case AV_PIX_FMT_RGBA64LE:
    case AV_PIX_FMT_RGB0:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_RGB24: rgba_map[RED  ] = 0; rgba_map[GREEN] = 1; rgba_map[BLUE ] = 2; rgba_map[ALPHA] = 3; break;
    case AV_PIX_FMT_BGR48LE:
    case AV_PIX_FMT_BGR48BE:
    case AV_PIX_FMT_BGRA64BE:
    case AV_PIX_FMT_BGRA64LE:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_BGR24: rgba_map[BLUE ] = 0; rgba_map[GREEN] = 1; rgba_map[RED  ] = 2; rgba_map[ALPHA] = 3; break;
    case AV_PIX_FMT_GBRP9LE:
    case AV_PIX_FMT_GBRP9BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP14LE:
    case AV_PIX_FMT_GBRP14BE:
    case AV_PIX_FMT_GBRP16LE:
    case AV_PIX_FMT_GBRP16BE:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRAP10LE:
    case AV_PIX_FMT_GBRAP10BE:
    case AV_PIX_FMT_GBRAP12LE:
    case AV_PIX_FMT_GBRAP12BE:
    case AV_PIX_FMT_GBRAP16LE:
    case AV_PIX_FMT_GBRAP16BE:
    case AV_PIX_FMT_GBRP:  rgba_map[GREEN] = 0; rgba_map[BLUE ] = 1; rgba_map[RED  ] = 2; rgba_map[ALPHA] = 3; break;
    default:                    /* unsupported */
        return AVERROR(EINVAL);
    }
    return 0;
}

int ff_fill_line_with_color(uint8_t *line[4], int pixel_step[4], int w, uint8_t dst_color[4],
                            enum AVPixelFormat pix_fmt, uint8_t rgba_color[4],
                            int *is_packed_rgba, uint8_t rgba_map_ptr[4])
{
    uint8_t rgba_map[4] = {0};
    int i;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(pix_fmt);
    int hsub;

    av_assert0(pix_desc);

    hsub = pix_desc->log2_chroma_w;

    *is_packed_rgba = ff_fill_rgba_map(rgba_map, pix_fmt) >= 0;

    if (*is_packed_rgba) {
        pixel_step[0] = (av_get_bits_per_pixel(pix_desc))>>3;
        for (i = 0; i < 4; i++)
            dst_color[rgba_map[i]] = rgba_color[i];

        line[0] = av_malloc_array(w, pixel_step[0]);
        if (!line[0])
            return AVERROR(ENOMEM);
        for (i = 0; i < w; i++)
            memcpy(line[0] + i * pixel_step[0], dst_color, pixel_step[0]);
        if (rgba_map_ptr)
            memcpy(rgba_map_ptr, rgba_map, sizeof(rgba_map[0]) * 4);
    } else {
        int plane;

        dst_color[0] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
        dst_color[1] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        dst_color[2] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        dst_color[3] = rgba_color[3];

        for (plane = 0; plane < 4; plane++) {
            int line_size;
            int hsub1 = (plane == 1 || plane == 2) ? hsub : 0;

            pixel_step[plane] = 1;
            line_size = AV_CEIL_RSHIFT(w, hsub1) * pixel_step[plane];
            line[plane] = av_malloc(line_size);
            if (!line[plane]) {
                while(plane && line[plane-1])
                    av_freep(&line[--plane]);
                return AVERROR(ENOMEM);
            }
            memset(line[plane], dst_color[plane], line_size);
        }
    }

    return 0;
}

void ff_draw_rectangle(uint8_t *dst[4], int dst_linesize[4],
                       uint8_t *src[4], int pixelstep[4],
                       int hsub, int vsub, int x, int y, int w, int h)
{
    int i, plane;
    uint8_t *p;

    for (plane = 0; plane < 4 && dst[plane]; plane++) {
        int hsub1 = plane == 1 || plane == 2 ? hsub : 0;
        int vsub1 = plane == 1 || plane == 2 ? vsub : 0;
        int width  = AV_CEIL_RSHIFT(w, hsub1);
        int height = AV_CEIL_RSHIFT(h, vsub1);

        p = dst[plane] + (y >> vsub1) * dst_linesize[plane];
        for (i = 0; i < height; i++) {
            memcpy(p + (x >> hsub1) * pixelstep[plane],
                   src[plane], width * pixelstep[plane]);
            p += dst_linesize[plane];
        }
    }
}

void ff_copy_rectangle(uint8_t *dst[4], int dst_linesize[4],
                       uint8_t *src[4], int src_linesize[4], int pixelstep[4],
                       int hsub, int vsub, int x, int y, int y2, int w, int h)
{
    int i, plane;
    uint8_t *p;

    for (plane = 0; plane < 4 && dst[plane]; plane++) {
        int hsub1 = plane == 1 || plane == 2 ? hsub : 0;
        int vsub1 = plane == 1 || plane == 2 ? vsub : 0;
        int width  = AV_CEIL_RSHIFT(w, hsub1);
        int height = AV_CEIL_RSHIFT(h, vsub1);

        p = dst[plane] + (y >> vsub1) * dst_linesize[plane];
        for (i = 0; i < height; i++) {
            memcpy(p + (x >> hsub1) * pixelstep[plane],
                   src[plane] + src_linesize[plane]*(i+(y2>>vsub1)), width * pixelstep[plane]);
            p += dst_linesize[plane];
        }
    }
}

int ff_draw_init(FFDrawContext *draw, enum AVPixelFormat format, unsigned flags)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
    const AVComponentDescriptor *c;
    unsigned i, nb_planes = 0;
    int pixelstep[MAX_PLANES] = { 0 };

    if (!desc || !desc->name)
        return AVERROR(EINVAL);
    if (desc->flags & ~(AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB | FF_PSEUDOPAL | AV_PIX_FMT_FLAG_ALPHA))
        return AVERROR(ENOSYS);
    if (format == AV_PIX_FMT_P010LE || format == AV_PIX_FMT_P010BE || format == AV_PIX_FMT_P016LE || format == AV_PIX_FMT_P016BE)
        return AVERROR(ENOSYS);
    for (i = 0; i < desc->nb_components; i++) {
        c = &desc->comp[i];
        /* for now, only 8-16 bits formats */
        if (c->depth < 8 || c->depth > 16)
            return AVERROR(ENOSYS);
        if (desc->flags & AV_PIX_FMT_FLAG_BE)
            return AVERROR(ENOSYS);
        if (c->plane >= MAX_PLANES)
            return AVERROR(ENOSYS);
        /* strange interleaving */
        if (pixelstep[c->plane] != 0 &&
            pixelstep[c->plane] != c->step)
            return AVERROR(ENOSYS);
        if (pixelstep[c->plane] == 6 &&
            c->depth == 16)
            return AVERROR(ENOSYS);
        pixelstep[c->plane] = c->step;
        if (pixelstep[c->plane] >= 8)
            return AVERROR(ENOSYS);
        nb_planes = FFMAX(nb_planes, c->plane + 1);
    }
    memset(draw, 0, sizeof(*draw));
    draw->desc      = desc;
    draw->format    = format;
    draw->nb_planes = nb_planes;
    draw->flags     = flags;
    memcpy(draw->pixelstep, pixelstep, sizeof(draw->pixelstep));
    draw->hsub[1] = draw->hsub[2] = draw->hsub_max = desc->log2_chroma_w;
    draw->vsub[1] = draw->vsub[2] = draw->vsub_max = desc->log2_chroma_h;
    for (i = 0; i < (desc->nb_components - !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA && !(flags & FF_DRAW_PROCESS_ALPHA))); i++)
        draw->comp_mask[desc->comp[i].plane] |=
            1 << desc->comp[i].offset;
    return 0;
}

void ff_draw_color(FFDrawContext *draw, FFDrawColor *color, const uint8_t rgba[4])
{
    unsigned i;
    uint8_t rgba_map[4];

    if (rgba != color->rgba)
        memcpy(color->rgba, rgba, sizeof(color->rgba));
    if ((draw->desc->flags & AV_PIX_FMT_FLAG_RGB) &&
        ff_fill_rgba_map(rgba_map, draw->format) >= 0) {
        if (draw->nb_planes == 1) {
            for (i = 0; i < 4; i++) {
                color->comp[0].u8[rgba_map[i]] = rgba[i];
                if (draw->desc->comp[rgba_map[i]].depth > 8) {
                    color->comp[0].u16[rgba_map[i]] = color->comp[0].u8[rgba_map[i]] << 8;
                }
            }
        } else {
            for (i = 0; i < 4; i++) {
                color->comp[rgba_map[i]].u8[0] = rgba[i];
                if (draw->desc->comp[rgba_map[i]].depth > 8)
                    color->comp[rgba_map[i]].u16[0] = color->comp[rgba_map[i]].u8[0] << (draw->desc->comp[rgba_map[i]].depth - 8);
            }
        }
    } else if (draw->nb_planes >= 2) {
        /* assume YUV */
        const AVPixFmtDescriptor *desc = draw->desc;
        color->comp[desc->comp[0].plane].u8[desc->comp[0].offset] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        color->comp[desc->comp[1].plane].u8[desc->comp[1].offset] = RGB_TO_U_CCIR(rgba[0], rgba[1], rgba[2], 0);
        color->comp[desc->comp[2].plane].u8[desc->comp[2].offset] = RGB_TO_V_CCIR(rgba[0], rgba[1], rgba[2], 0);
        color->comp[3].u8[0] = rgba[3];
#define EXPAND(compn) \
        if (desc->comp[compn].depth > 8) \
            color->comp[desc->comp[compn].plane].u16[desc->comp[compn].offset] = \
            color->comp[desc->comp[compn].plane].u8[desc->comp[compn].offset] << \
                (draw->desc->comp[compn].depth + draw->desc->comp[compn].shift - 8)
        EXPAND(3);
        EXPAND(2);
        EXPAND(1);
        EXPAND(0);
    } else if (draw->format == AV_PIX_FMT_GRAY8 || draw->format == AV_PIX_FMT_GRAY8A ||
               draw->format == AV_PIX_FMT_GRAY16LE || draw->format == AV_PIX_FMT_YA16LE ||
               draw->format == AV_PIX_FMT_GRAY9LE  ||
               draw->format == AV_PIX_FMT_GRAY10LE ||
               draw->format == AV_PIX_FMT_GRAY12LE) {
        const AVPixFmtDescriptor *desc = draw->desc;
        color->comp[0].u8[0] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        EXPAND(0);
        color->comp[1].u8[0] = rgba[3];
        EXPAND(1);
    } else {
        av_log(NULL, AV_LOG_WARNING,
               "Color conversion not implemented for %s\n", draw->desc->name);
        memset(color, 128, sizeof(*color));
    }
}

static uint8_t *pointer_at(FFDrawContext *draw, uint8_t *data[], int linesize[],
                           int plane, int x, int y)
{
    return data[plane] +
           (y >> draw->vsub[plane]) * linesize[plane] +
           (x >> draw->hsub[plane]) * draw->pixelstep[plane];
}

void ff_copy_rectangle2(FFDrawContext *draw,
                        uint8_t *dst[], int dst_linesize[],
                        uint8_t *src[], int src_linesize[],
                        int dst_x, int dst_y, int src_x, int src_y,
                        int w, int h)
{
    int plane, y, wp, hp;
    uint8_t *p, *q;

    for (plane = 0; plane < draw->nb_planes; plane++) {
        p = pointer_at(draw, src, src_linesize, plane, src_x, src_y);
        q = pointer_at(draw, dst, dst_linesize, plane, dst_x, dst_y);
        wp = AV_CEIL_RSHIFT(w, draw->hsub[plane]) * draw->pixelstep[plane];
        hp = AV_CEIL_RSHIFT(h, draw->vsub[plane]);
        for (y = 0; y < hp; y++) {
            memcpy(q, p, wp);
            p += src_linesize[plane];
            q += dst_linesize[plane];
        }
    }
}

void ff_fill_rectangle(FFDrawContext *draw, FFDrawColor *color,
                       uint8_t *dst[], int dst_linesize[],
                       int dst_x, int dst_y, int w, int h)
{
    int plane, x, y, wp, hp;
    uint8_t *p0, *p;
    FFDrawColor color_tmp = *color;

    for (plane = 0; plane < draw->nb_planes; plane++) {
        p0 = pointer_at(draw, dst, dst_linesize, plane, dst_x, dst_y);
        wp = AV_CEIL_RSHIFT(w, draw->hsub[plane]);
        hp = AV_CEIL_RSHIFT(h, draw->vsub[plane]);
        if (!hp)
            return;
        p = p0;

        if (HAVE_BIGENDIAN && draw->desc->comp[0].depth > 8) {
            for (x = 0; 2*x < draw->pixelstep[plane]; x++)
                color_tmp.comp[plane].u16[x] = av_bswap16(color_tmp.comp[plane].u16[x]);
        }

        /* copy first line from color */
        for (x = 0; x < wp; x++) {
            memcpy(p, color_tmp.comp[plane].u8, draw->pixelstep[plane]);
            p += draw->pixelstep[plane];
        }
        wp *= draw->pixelstep[plane];
        /* copy next lines from first line */
        p = p0 + dst_linesize[plane];
        for (y = 1; y < hp; y++) {
            memcpy(p, p0, wp);
            p += dst_linesize[plane];
        }
    }
}

/**
 * Clip interval [x; x+w[ within [0; wmax[.
 * The resulting w may be negative if the final interval is empty.
 * dx, if not null, return the difference between in and out value of x.
 */
static void clip_interval(int wmax, int *x, int *w, int *dx)
{
    if (dx)
        *dx = 0;
    if (*x < 0) {
        if (dx)
            *dx = -*x;
        *w += *x;
        *x = 0;
    }
    if (*x + *w > wmax)
        *w = wmax - *x;
}

/**
 * Decompose w pixels starting at x
 * into start + (w starting at x) + end
 * with x and w aligned on multiples of 1<<sub.
 */
static void subsampling_bounds(int sub, int *x, int *w, int *start, int *end)
{
    int mask = (1 << sub) - 1;

    *start = (-*x) & mask;
    *x += *start;
    *start = FFMIN(*start, *w);
    *w -= *start;
    *end = *w & mask;
    *w >>= sub;
}

static int component_used(FFDrawContext *draw, int plane, int comp)
{
    return (draw->comp_mask[plane] >> comp) & 1;
}

/* If alpha is in the [ 0 ; 0x1010101 ] range,
   then alpha * value is in the [ 0 ; 0xFFFFFFFF ] range,
   and >> 24 gives a correct rounding. */
static void blend_line(uint8_t *dst, unsigned src, unsigned alpha,
                       int dx, int w, unsigned hsub, int left, int right)
{
    unsigned asrc = alpha * src;
    unsigned tau = 0x1010101 - alpha;
    int x;

    if (left) {
        unsigned suba = (left * alpha) >> hsub;
        *dst = (*dst * (0x1010101 - suba) + src * suba) >> 24;
        dst += dx;
    }
    for (x = 0; x < w; x++) {
        *dst = (*dst * tau + asrc) >> 24;
        dst += dx;
    }
    if (right) {
        unsigned suba = (right * alpha) >> hsub;
        *dst = (*dst * (0x1010101 - suba) + src * suba) >> 24;
    }
}

static void blend_line16(uint8_t *dst, unsigned src, unsigned alpha,
                         int dx, int w, unsigned hsub, int left, int right)
{
    unsigned asrc = alpha * src;
    unsigned tau = 0x10001 - alpha;
    int x;

    if (left) {
        unsigned suba = (left * alpha) >> hsub;
        uint16_t value = AV_RL16(dst);
        AV_WL16(dst, (value * (0x10001 - suba) + src * suba) >> 16);
        dst += dx;
    }
    for (x = 0; x < w; x++) {
        uint16_t value = AV_RL16(dst);
        AV_WL16(dst, (value * tau + asrc) >> 16);
        dst += dx;
    }
    if (right) {
        unsigned suba = (right * alpha) >> hsub;
        uint16_t value = AV_RL16(dst);
        AV_WL16(dst, (value * (0x10001 - suba) + src * suba) >> 16);
    }
}

void ff_blend_rectangle(FFDrawContext *draw, FFDrawColor *color,
                        uint8_t *dst[], int dst_linesize[],
                        int dst_w, int dst_h,
                        int x0, int y0, int w, int h)
{
    unsigned alpha, nb_planes, nb_comp, plane, comp;
    int w_sub, h_sub, x_sub, y_sub, left, right, top, bottom, y;
    uint8_t *p0, *p;

    /* TODO optimize if alpha = 0xFF */
    clip_interval(dst_w, &x0, &w, NULL);
    clip_interval(dst_h, &y0, &h, NULL);
    if (w <= 0 || h <= 0 || !color->rgba[3])
        return;
    if (draw->desc->comp[0].depth <= 8) {
        /* 0x10203 * alpha + 2 is in the [ 2 ; 0x1010101 - 2 ] range */
        alpha = 0x10203 * color->rgba[3] + 0x2;
    } else {
        /* 0x101 * alpha is in the [ 2 ; 0x1001] range */
        alpha = 0x101 * color->rgba[3] + 0x2;
    }
    nb_planes = draw->nb_planes - !!(draw->desc->flags & AV_PIX_FMT_FLAG_ALPHA && !(draw->flags & FF_DRAW_PROCESS_ALPHA));
    nb_planes += !nb_planes;
    for (plane = 0; plane < nb_planes; plane++) {
        nb_comp = draw->pixelstep[plane];
        p0 = pointer_at(draw, dst, dst_linesize, plane, x0, y0);
        w_sub = w;
        h_sub = h;
        x_sub = x0;
        y_sub = y0;
        subsampling_bounds(draw->hsub[plane], &x_sub, &w_sub, &left, &right);
        subsampling_bounds(draw->vsub[plane], &y_sub, &h_sub, &top, &bottom);
        for (comp = 0; comp < nb_comp; comp++) {
            const int depth = draw->desc->comp[comp].depth;

            if (!component_used(draw, plane, comp))
                continue;
            p = p0 + comp;
            if (top) {
                if (depth <= 8) {
                    blend_line(p, color->comp[plane].u8[comp], alpha >> 1,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                } else {
                    blend_line16(p, color->comp[plane].u16[comp], alpha >> 1,
                                 draw->pixelstep[plane], w_sub,
                                 draw->hsub[plane], left, right);
                }
                p += dst_linesize[plane];
            }
            if (depth <= 8) {
                for (y = 0; y < h_sub; y++) {
                    blend_line(p, color->comp[plane].u8[comp], alpha,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                    p += dst_linesize[plane];
                }
            } else {
                for (y = 0; y < h_sub; y++) {
                    blend_line16(p, color->comp[plane].u16[comp], alpha,
                                 draw->pixelstep[plane], w_sub,
                                 draw->hsub[plane], left, right);
                    p += dst_linesize[plane];
                }
            }
            if (bottom) {
                if (depth <= 8) {
                    blend_line(p, color->comp[plane].u8[comp], alpha >> 1,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                } else {
                    blend_line16(p, color->comp[plane].u16[comp], alpha >> 1,
                                 draw->pixelstep[plane], w_sub,
                                 draw->hsub[plane], left, right);
                }
            }
        }
    }
}

static void blend_pixel16(uint8_t *dst, unsigned src, unsigned alpha,
                          const uint8_t *mask, int mask_linesize, int l2depth,
                          unsigned w, unsigned h, unsigned shift, unsigned xm0)
{
    unsigned xm, x, y, t = 0;
    unsigned xmshf = 3 - l2depth;
    unsigned xmmod = 7 >> l2depth;
    unsigned mbits = (1 << (1 << l2depth)) - 1;
    unsigned mmult = 255 / mbits;
    uint16_t value = AV_RL16(dst);

    for (y = 0; y < h; y++) {
        xm = xm0;
        for (x = 0; x < w; x++) {
            t += ((mask[xm >> xmshf] >> ((~xm & xmmod) << l2depth)) & mbits)
                 * mmult;
            xm++;
        }
        mask += mask_linesize;
    }
    alpha = (t >> shift) * alpha;
    AV_WL16(dst, ((0x10001 - alpha) * value + alpha * src) >> 16);
}

static void blend_pixel(uint8_t *dst, unsigned src, unsigned alpha,
                        const uint8_t *mask, int mask_linesize, int l2depth,
                        unsigned w, unsigned h, unsigned shift, unsigned xm0)
{
    unsigned xm, x, y, t = 0;
    unsigned xmshf = 3 - l2depth;
    unsigned xmmod = 7 >> l2depth;
    unsigned mbits = (1 << (1 << l2depth)) - 1;
    unsigned mmult = 255 / mbits;

    for (y = 0; y < h; y++) {
        xm = xm0;
        for (x = 0; x < w; x++) {
            t += ((mask[xm >> xmshf] >> ((~xm & xmmod) << l2depth)) & mbits)
                 * mmult;
            xm++;
        }
        mask += mask_linesize;
    }
    alpha = (t >> shift) * alpha;
    *dst = ((0x1010101 - alpha) * *dst + alpha * src) >> 24;
}

static void blend_line_hv16(uint8_t *dst, int dst_delta,
                            unsigned src, unsigned alpha,
                            const uint8_t *mask, int mask_linesize, int l2depth, int w,
                            unsigned hsub, unsigned vsub,
                            int xm, int left, int right, int hband)
{
    int x;

    if (left) {
        blend_pixel16(dst, src, alpha, mask, mask_linesize, l2depth,
                      left, hband, hsub + vsub, xm);
        dst += dst_delta;
        xm += left;
    }
    for (x = 0; x < w; x++) {
        blend_pixel16(dst, src, alpha, mask, mask_linesize, l2depth,
                      1 << hsub, hband, hsub + vsub, xm);
        dst += dst_delta;
        xm += 1 << hsub;
    }
    if (right)
        blend_pixel16(dst, src, alpha, mask, mask_linesize, l2depth,
                      right, hband, hsub + vsub, xm);
}

static void blend_line_hv(uint8_t *dst, int dst_delta,
                          unsigned src, unsigned alpha,
                          const uint8_t *mask, int mask_linesize, int l2depth, int w,
                          unsigned hsub, unsigned vsub,
                          int xm, int left, int right, int hband)
{
    int x;

    if (left) {
        blend_pixel(dst, src, alpha, mask, mask_linesize, l2depth,
                    left, hband, hsub + vsub, xm);
        dst += dst_delta;
        xm += left;
    }
    for (x = 0; x < w; x++) {
        blend_pixel(dst, src, alpha, mask, mask_linesize, l2depth,
                    1 << hsub, hband, hsub + vsub, xm);
        dst += dst_delta;
        xm += 1 << hsub;
    }
    if (right)
        blend_pixel(dst, src, alpha, mask, mask_linesize, l2depth,
                    right, hband, hsub + vsub, xm);
}

void ff_blend_mask(FFDrawContext *draw, FFDrawColor *color,
                   uint8_t *dst[], int dst_linesize[], int dst_w, int dst_h,
                   const uint8_t *mask,  int mask_linesize, int mask_w, int mask_h,
                   int l2depth, unsigned endianness, int x0, int y0)
{
    unsigned alpha, nb_planes, nb_comp, plane, comp;
    int xm0, ym0, w_sub, h_sub, x_sub, y_sub, left, right, top, bottom, y;
    uint8_t *p0, *p;
    const uint8_t *m;

    clip_interval(dst_w, &x0, &mask_w, &xm0);
    clip_interval(dst_h, &y0, &mask_h, &ym0);
    mask += ym0 * mask_linesize;
    if (mask_w <= 0 || mask_h <= 0 || !color->rgba[3])
        return;
    if (draw->desc->comp[0].depth <= 8) {
        /* alpha is in the [ 0 ; 0x10203 ] range,
           alpha * mask is in the [ 0 ; 0x1010101 - 4 ] range */
        alpha = (0x10307 * color->rgba[3] + 0x3) >> 8;
    } else {
        alpha = (0x101 * color->rgba[3] + 0x2) >> 8;
    }
    nb_planes = draw->nb_planes - !!(draw->desc->flags & AV_PIX_FMT_FLAG_ALPHA && !(draw->flags & FF_DRAW_PROCESS_ALPHA));
    nb_planes += !nb_planes;
    for (plane = 0; plane < nb_planes; plane++) {
        nb_comp = draw->pixelstep[plane];
        p0 = pointer_at(draw, dst, dst_linesize, plane, x0, y0);
        w_sub = mask_w;
        h_sub = mask_h;
        x_sub = x0;
        y_sub = y0;
        subsampling_bounds(draw->hsub[plane], &x_sub, &w_sub, &left, &right);
        subsampling_bounds(draw->vsub[plane], &y_sub, &h_sub, &top, &bottom);
        for (comp = 0; comp < nb_comp; comp++) {
            const int depth = draw->desc->comp[comp].depth;

            if (!component_used(draw, plane, comp))
                continue;
            p = p0 + comp;
            m = mask;
            if (top) {
                if (depth <= 8) {
                    blend_line_hv(p, draw->pixelstep[plane],
                                  color->comp[plane].u8[comp], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, top);
                } else {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[comp], alpha,
                                    m, mask_linesize, l2depth, w_sub,
                                    draw->hsub[plane], draw->vsub[plane],
                                    xm0, left, right, top);
                }
                p += dst_linesize[plane];
                m += top * mask_linesize;
            }
            if (depth <= 8) {
                for (y = 0; y < h_sub; y++) {
                    blend_line_hv(p, draw->pixelstep[plane],
                                  color->comp[plane].u8[comp], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, 1 << draw->vsub[plane]);
                    p += dst_linesize[plane];
                    m += mask_linesize << draw->vsub[plane];
                }
            } else {
                for (y = 0; y < h_sub; y++) {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[comp], alpha,
                                    m, mask_linesize, l2depth, w_sub,
                                    draw->hsub[plane], draw->vsub[plane],
                                    xm0, left, right, 1 << draw->vsub[plane]);
                    p += dst_linesize[plane];
                    m += mask_linesize << draw->vsub[plane];
                }
            }
            if (bottom) {
                if (depth <= 8) {
                    blend_line_hv(p, draw->pixelstep[plane],
                                  color->comp[plane].u8[comp], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, bottom);
                } else {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[comp], alpha,
                                    m, mask_linesize, l2depth, w_sub,
                                    draw->hsub[plane], draw->vsub[plane],
                                    xm0, left, right, bottom);
                }
            }
        }
    }
}

int ff_draw_round_to_sub(FFDrawContext *draw, int sub_dir, int round_dir,
                         int value)
{
    unsigned shift = sub_dir ? draw->vsub_max : draw->hsub_max;

    if (!shift)
        return value;
    if (round_dir >= 0)
        value += round_dir ? (1 << shift) - 1 : 1 << (shift - 1);
    return (value >> shift) << shift;
}

AVFilterFormats *ff_draw_supported_pixel_formats(unsigned flags)
{
    enum AVPixelFormat i;
    FFDrawContext draw;
    AVFilterFormats *fmts = NULL;
    int ret;

    for (i = 0; av_pix_fmt_desc_get(i); i++)
        if (ff_draw_init(&draw, i, flags) >= 0 &&
            (ret = ff_add_format(&fmts, i)) < 0)
            return NULL;
    return fmts;
}
