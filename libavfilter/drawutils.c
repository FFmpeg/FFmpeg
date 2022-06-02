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
#include "libavutil/csp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "colorspace.h"
#include "drawutils.h"
#include "formats.h"

enum { RED = 0, GREEN, BLUE, ALPHA };

int ff_fill_rgba_map(uint8_t *rgba_map, enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB))
        return AVERROR(EINVAL);
    if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM)
        return AVERROR(EINVAL);
    av_assert0(desc->nb_components == 3 + !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA));
    if (desc->flags & AV_PIX_FMT_FLAG_PLANAR) {
        rgba_map[RED]   = desc->comp[0].plane;
        rgba_map[GREEN] = desc->comp[1].plane;
        rgba_map[BLUE]  = desc->comp[2].plane;
        rgba_map[ALPHA] = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? desc->comp[3].plane : 3;
    } else {
        int had0 = 0;
        unsigned depthb = 0;
        unsigned i;
        for (i = 0; i < desc->nb_components; i++) {
            /* all components must have same depth in bytes */
            unsigned db = (desc->comp[i].depth + 7) / 8;
            unsigned pos = desc->comp[i].offset / db;
            if (depthb && (depthb != db))
                return AVERROR(ENOSYS);

            if (desc->comp[i].offset % db)
                return AVERROR(ENOSYS);

            had0 |= pos == 0;
            rgba_map[i] = pos;
        }

        if (desc->nb_components == 3)
            rgba_map[ALPHA] = had0 ? 3 : 0;
    }

    av_assert0(rgba_map[RED]   != rgba_map[GREEN]);
    av_assert0(rgba_map[GREEN] != rgba_map[BLUE]);
    av_assert0(rgba_map[BLUE]  != rgba_map[RED]);
    av_assert0(rgba_map[RED]   != rgba_map[ALPHA]);
    av_assert0(rgba_map[GREEN] != rgba_map[ALPHA]);
    av_assert0(rgba_map[BLUE]  != rgba_map[ALPHA]);

    return 0;
}

int ff_draw_init2(FFDrawContext *draw, enum AVPixelFormat format, enum AVColorSpace csp,
                  enum AVColorRange range, unsigned flags)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
    const AVLumaCoefficients *luma = NULL;
    const AVComponentDescriptor *c;
    unsigned i, nb_planes = 0;
    int pixelstep[MAX_PLANES] = { 0 };
    int depthb = 0;

    if (!desc || !desc->name)
        return AVERROR(EINVAL);
    if (desc->flags & AV_PIX_FMT_FLAG_BE)
        return AVERROR(ENOSYS);
    if (desc->flags & ~(AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA))
        return AVERROR(ENOSYS);
    if (csp == AVCOL_SPC_UNSPECIFIED)
        csp = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? AVCOL_SPC_RGB : AVCOL_SPC_SMPTE170M;
    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && !(luma = av_csp_luma_coeffs_from_avcsp(csp)))
        return AVERROR(EINVAL);
    if (range == AVCOL_RANGE_UNSPECIFIED)
        range = (format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P ||
                 format == AV_PIX_FMT_YUVJ444P || format == AV_PIX_FMT_YUVJ411P ||
                 format == AV_PIX_FMT_YUVJ440P || csp == AVCOL_SPC_RGB)
                ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    if (range != AVCOL_RANGE_JPEG && range != AVCOL_RANGE_MPEG)
        return AVERROR(EINVAL);
    for (i = 0; i < desc->nb_components; i++) {
        int db;
        c = &desc->comp[i];
        /* for now, only 8-16 bits formats */
        if (c->depth < 8 || c->depth > 16)
            return AVERROR(ENOSYS);
        if (c->plane >= MAX_PLANES)
            return AVERROR(ENOSYS);
        /* data must either be in the high or low bits, never middle */
        if (c->shift && ((c->shift + c->depth) & 0x7))
            return AVERROR(ENOSYS);
        /* mixed >8 and <=8 depth */
        db = (c->depth + 7) / 8;
        if (depthb && (depthb != db))
            return AVERROR(ENOSYS);
        depthb = db;
        if (db * (c->offset + 1) > 16)
            return AVERROR(ENOSYS);
        if (c->offset % db)
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
    draw->range     = range;
    draw->csp       = csp;
    draw->flags     = flags;
    if (luma)
        ff_fill_rgb2yuv_table(luma, draw->rgb2yuv);
    memcpy(draw->pixelstep, pixelstep, sizeof(draw->pixelstep));
    draw->hsub[1] = draw->hsub[2] = draw->hsub_max = desc->log2_chroma_w;
    draw->vsub[1] = draw->vsub[2] = draw->vsub_max = desc->log2_chroma_h;
    return 0;
}

int ff_draw_init(FFDrawContext *draw, enum AVPixelFormat format, unsigned flags)
{
    return ff_draw_init2(draw, format, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, flags);
}

void ff_draw_color(FFDrawContext *draw, FFDrawColor *color, const uint8_t rgba[4])
{
    unsigned i;
    double yuvad[4];
    double rgbad[4];
    const AVPixFmtDescriptor *desc = draw->desc;

    if (rgba != color->rgba)
        memcpy(color->rgba, rgba, sizeof(color->rgba));

    memset(color->comp, 0, sizeof(color->comp));

    for (int i = 0; i < 4; i++)
        rgbad[i] = color->rgba[i] / 255.;

    if (draw->desc->flags & AV_PIX_FMT_FLAG_RGB)
        memcpy(yuvad, rgbad, sizeof(double) * 3);
    else
        ff_matrix_mul_3x3_vec(yuvad, rgbad, draw->rgb2yuv);

    yuvad[3] = rgbad[3];

    for (int i = 0; i < 3; i++) {
        int chroma = (!(draw->desc->flags & AV_PIX_FMT_FLAG_RGB) && i > 0);
        if (draw->range == AVCOL_RANGE_MPEG) {
            yuvad[i] *= (chroma ? 224. : 219.) / 255.;
            yuvad[i] += (chroma ? 128. :  16.) / 255.;
        } else if (chroma) {
            yuvad[i] += 0.5;
        }
    }

    // Ensure we place the alpha appropriately for gray formats
    if (desc->nb_components <= 2)
        yuvad[1] = yuvad[3];

    for (i = 0; i < desc->nb_components; i++) {
        unsigned val = yuvad[i] * ((1 << (draw->desc->comp[i].depth + draw->desc->comp[i].shift)) - 1) + 0.5;
        if (desc->comp[i].depth > 8)
            color->comp[desc->comp[i].plane].u16[desc->comp[i].offset / 2] = val;
        else
            color->comp[desc->comp[i].plane].u8[desc->comp[i].offset] = val;
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

    nb_comp = draw->desc->nb_components -
        !!(draw->desc->flags & AV_PIX_FMT_FLAG_ALPHA && !(draw->flags & FF_DRAW_PROCESS_ALPHA));

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
        p0 = pointer_at(draw, dst, dst_linesize, plane, x0, y0);
        w_sub = w;
        h_sub = h;
        x_sub = x0;
        y_sub = y0;
        subsampling_bounds(draw->hsub[plane], &x_sub, &w_sub, &left, &right);
        subsampling_bounds(draw->vsub[plane], &y_sub, &h_sub, &top, &bottom);
        for (comp = 0; comp < nb_comp; comp++) {
            const int depth = draw->desc->comp[comp].depth;
            const int offset = draw->desc->comp[comp].offset;
            const int index = offset / ((depth + 7) / 8);

            if (draw->desc->comp[comp].plane != plane)
                continue;
            p = p0 + offset;
            if (top) {
                if (depth <= 8) {
                    blend_line(p, color->comp[plane].u8[index], alpha >> 1,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                } else {
                    blend_line16(p, color->comp[plane].u16[index], alpha >> 1,
                                 draw->pixelstep[plane], w_sub,
                                 draw->hsub[plane], left, right);
                }
                p += dst_linesize[plane];
            }
            if (depth <= 8) {
                for (y = 0; y < h_sub; y++) {
                    blend_line(p, color->comp[plane].u8[index], alpha,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                    p += dst_linesize[plane];
                }
            } else {
                for (y = 0; y < h_sub; y++) {
                    blend_line16(p, color->comp[plane].u16[index], alpha,
                                 draw->pixelstep[plane], w_sub,
                                 draw->hsub[plane], left, right);
                    p += dst_linesize[plane];
                }
            }
            if (bottom) {
                if (depth <= 8) {
                    blend_line(p, color->comp[plane].u8[index], alpha >> 1,
                               draw->pixelstep[plane], w_sub,
                               draw->hsub[plane], left, right);
                } else {
                    blend_line16(p, color->comp[plane].u16[index], alpha >> 1,
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

    nb_comp = draw->desc->nb_components -
        !!(draw->desc->flags & AV_PIX_FMT_FLAG_ALPHA && !(draw->flags & FF_DRAW_PROCESS_ALPHA));

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
        p0 = pointer_at(draw, dst, dst_linesize, plane, x0, y0);
        w_sub = mask_w;
        h_sub = mask_h;
        x_sub = x0;
        y_sub = y0;
        subsampling_bounds(draw->hsub[plane], &x_sub, &w_sub, &left, &right);
        subsampling_bounds(draw->vsub[plane], &y_sub, &h_sub, &top, &bottom);
        for (comp = 0; comp < nb_comp; comp++) {
            const int depth = draw->desc->comp[comp].depth;
            const int offset = draw->desc->comp[comp].offset;
            const int index = offset / ((depth + 7) / 8);

            if (draw->desc->comp[comp].plane != plane)
                continue;
            p = p0 + offset;
            m = mask;
            if (top) {
                if (depth <= 8) {
                    blend_line_hv(p, draw->pixelstep[plane],
                                  color->comp[plane].u8[index], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, top);
                } else {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[index], alpha,
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
                                  color->comp[plane].u8[index], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, 1 << draw->vsub[plane]);
                    p += dst_linesize[plane];
                    m += mask_linesize << draw->vsub[plane];
                }
            } else {
                for (y = 0; y < h_sub; y++) {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[index], alpha,
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
                                  color->comp[plane].u8[index], alpha,
                                  m, mask_linesize, l2depth, w_sub,
                                  draw->hsub[plane], draw->vsub[plane],
                                  xm0, left, right, bottom);
                } else {
                    blend_line_hv16(p, draw->pixelstep[plane],
                                    color->comp[plane].u16[index], alpha,
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
