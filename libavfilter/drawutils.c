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

#include "libavutil/avutil.h"
#include "libavutil/colorspace.h"
#include "libavutil/pixdesc.h"
#include "drawutils.h"

enum { RED = 0, GREEN, BLUE, ALPHA };

int ff_fill_rgba_map(uint8_t *rgba_map, enum PixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case PIX_FMT_0RGB:
    case PIX_FMT_ARGB:  rgba_map[ALPHA] = 0; rgba_map[RED  ] = 1; rgba_map[GREEN] = 2; rgba_map[BLUE ] = 3; break;
    case PIX_FMT_0BGR:
    case PIX_FMT_ABGR:  rgba_map[ALPHA] = 0; rgba_map[BLUE ] = 1; rgba_map[GREEN] = 2; rgba_map[RED  ] = 3; break;
    case PIX_FMT_RGB0:
    case PIX_FMT_RGBA:
    case PIX_FMT_RGB24: rgba_map[RED  ] = 0; rgba_map[GREEN] = 1; rgba_map[BLUE ] = 2; rgba_map[ALPHA] = 3; break;
    case PIX_FMT_BGRA:
    case PIX_FMT_BGR0:
    case PIX_FMT_BGR24: rgba_map[BLUE ] = 0; rgba_map[GREEN] = 1; rgba_map[RED  ] = 2; rgba_map[ALPHA] = 3; break;
    default:                    /* unsupported */
        return AVERROR(EINVAL);
    }
    return 0;
}

int ff_fill_line_with_color(uint8_t *line[4], int pixel_step[4], int w, uint8_t dst_color[4],
                            enum PixelFormat pix_fmt, uint8_t rgba_color[4],
                            int *is_packed_rgba, uint8_t rgba_map_ptr[4])
{
    uint8_t rgba_map[4] = {0};
    int i;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[pix_fmt];
    int hsub = pix_desc->log2_chroma_w;

    *is_packed_rgba = ff_fill_rgba_map(rgba_map, pix_fmt) >= 0;

    if (*is_packed_rgba) {
        pixel_step[0] = (av_get_bits_per_pixel(pix_desc))>>3;
        for (i = 0; i < 4; i++)
            dst_color[rgba_map[i]] = rgba_color[i];

        line[0] = av_malloc(w * pixel_step[0]);
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
            line_size = (w >> hsub1) * pixel_step[plane];
            line[plane] = av_malloc(line_size);
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

        p = dst[plane] + (y >> vsub1) * dst_linesize[plane];
        for (i = 0; i < (h >> vsub1); i++) {
            memcpy(p + (x >> hsub1) * pixelstep[plane],
                   src[plane], (w >> hsub1) * pixelstep[plane]);
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

        p = dst[plane] + (y >> vsub1) * dst_linesize[plane];
        for (i = 0; i < (h >> vsub1); i++) {
            memcpy(p + (x >> hsub1) * pixelstep[plane],
                   src[plane] + src_linesize[plane]*(i+(y2>>vsub1)), (w >> hsub1) * pixelstep[plane]);
            p += dst_linesize[plane];
        }
    }
}

int ff_draw_init(FFDrawContext *draw, enum PixelFormat format, unsigned flags)
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[format];
    const AVComponentDescriptor *c;
    unsigned i, nb_planes = 0;
    int pixelstep[MAX_PLANES] = { 0 };

    if (desc->flags & ~(PIX_FMT_PLANAR | PIX_FMT_RGB))
        return AVERROR(ENOSYS);
    for (i = 0; i < desc->nb_components; i++) {
        c = &desc->comp[i];
        /* for now, only 8-bits formats */
        if (c->depth_minus1 != 8 - 1)
            return AVERROR(ENOSYS);
        if (c->plane >= MAX_PLANES)
            return AVERROR(ENOSYS);
        /* strange interleaving */
        if (pixelstep[c->plane] != 0 &&
            pixelstep[c->plane] != c->step_minus1 + 1)
            return AVERROR(ENOSYS);
        pixelstep[c->plane] = c->step_minus1 + 1;
        nb_planes = FFMAX(nb_planes, c->plane + 1);
    }
    if ((desc->log2_chroma_w || desc->log2_chroma_h) && nb_planes < 3)
        return AVERROR(ENOSYS); /* exclude NV12 and NV21 */
    memset(draw, 0, sizeof(*draw));
    draw->desc      = desc;
    draw->format    = format;
    draw->nb_planes = nb_planes;
    memcpy(draw->pixelstep, pixelstep, sizeof(draw->pixelstep));
    if (nb_planes >= 3 && !(desc->flags & PIX_FMT_RGB)) {
        draw->hsub[1] = draw->hsub[2] = draw->hsub_max = desc->log2_chroma_w;
        draw->vsub[1] = draw->vsub[2] = draw->vsub_max = desc->log2_chroma_h;
    }
    return 0;
}

void ff_draw_color(FFDrawContext *draw, FFDrawColor *color, uint8_t rgba[4])
{
    unsigned i;
    uint8_t rgba_map[4];

    if ((draw->desc->flags & PIX_FMT_RGB) && draw->nb_planes == 1 &&
        ff_fill_rgba_map(rgba_map, draw->format) >= 0) {
        for (i = 0; i < 4; i++)
            color->comp[0].u8[rgba_map[i]] = rgba[i];
    } else if (draw->nb_planes == 3 || draw->nb_planes == 4) {
        /* assume YUV */
        color->comp[0].u8[0] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        color->comp[1].u8[0] = RGB_TO_U_CCIR(rgba[0], rgba[1], rgba[2], 0);
        color->comp[2].u8[0] = RGB_TO_V_CCIR(rgba[0], rgba[1], rgba[2], 0);
        color->comp[3].u8[0] = rgba[3];
    } else if (draw->format == PIX_FMT_GRAY8 || draw->format == PIX_FMT_GRAY8A) {
        color->comp[0].u8[0] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        color->comp[1].u8[0] = rgba[3];
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
        wp = (w >> draw->hsub[plane]) * draw->pixelstep[plane];
        hp = (h >> draw->vsub[plane]);
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

    for (plane = 0; plane < draw->nb_planes; plane++) {
        p0 = pointer_at(draw, dst, dst_linesize, plane, dst_x, dst_y);
        wp = (w >> draw->hsub[plane]);
        hp = (h >> draw->vsub[plane]);
        if (!hp)
            return;
        p = p0;
        /* copy first line from color */
        for (x = 0; x < wp; x++) {
            memcpy(p, color->comp[plane].u8, draw->pixelstep[plane]);
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
    enum PixelFormat i, pix_fmts[PIX_FMT_NB + 1];
    unsigned n = 0;
    FFDrawContext draw;

    for (i = 0; i < PIX_FMT_NB; i++)
        if (ff_draw_init(&draw, i, flags) >= 0)
            pix_fmts[n++] = i;
    pix_fmts[n++] = PIX_FMT_NONE;
    return avfilter_make_format_list(pix_fmts);
}

#ifdef TEST

#undef printf

int main(void)
{
    enum PixelFormat f;
    const AVPixFmtDescriptor *desc;
    FFDrawContext draw;
    FFDrawColor color;
    int r, i;

    for (f = 0; f < PIX_FMT_NB; f++) {
        desc = &av_pix_fmt_descriptors[f];
        if (!desc->name)
            continue;
        printf("Testing %s...%*s", desc->name,
               (int)(16 - strlen(desc->name)), "");
        r = ff_draw_init(&draw, f, 0);
        if (r < 0) {
            char buf[128];
            av_strerror(r, buf, sizeof(buf));
            printf("no: %s\n", buf);
            continue;
        }
        ff_draw_color(&draw, &color, (uint8_t[]) { 1, 0, 0, 1 });
        for (i = 0; i < sizeof(color); i++)
            if (((uint8_t *)&color)[i] != 128)
                break;
        if (i == sizeof(color)) {
            printf("fallback color\n");
            continue;
        }
        printf("ok\n");
    }
    return 0;
}

#endif
