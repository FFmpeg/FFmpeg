/*
 * copyright (c) 2008 vmrsss
 * copyright (c) 2009 Stefano Sabatini
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
 * video padding filter
 */

#include "avfilter.h"
#include "parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/colorspace.h"

typedef struct {
    int w, h;               ///< output dimensions, a value of 0 will result in the input size
    int x, y;               ///< offsets of the input area with respect to the padded area
    int in_w, in_h;         ///< width and height for the padded input video, which has to be aligned to the chroma values in order to avoid chroma issues

    uint8_t color[4];       ///< color expressed either in YUVA or RGBA colorspace for the padding area
    uint8_t *line[4];
    int      line_step[4];
    int hsub, vsub;         ///< chroma subsampling values
} PadContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    PadContext *pad = ctx->priv;
    char color_string[128] = "black";

    if (args)
        sscanf(args, "%d:%d:%d:%d:%s", &pad->w, &pad->h, &pad->x, &pad->y, color_string);

    if (av_parse_color(pad->color, color_string, ctx) < 0)
        return AVERROR(EINVAL);

    /* sanity check params */
    if (pad->w < 0 || pad->h < 0) {
        av_log(ctx, AV_LOG_ERROR, "Negative size values are not acceptable.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PadContext *pad = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_freep(&pad->line[i]);
        pad->line_step[i] = 0;
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_ARGB,         PIX_FMT_RGBA,
        PIX_FMT_ABGR,         PIX_FMT_BGRA,
        PIX_FMT_RGB24,        PIX_FMT_BGR24,

        PIX_FMT_YUV444P,      PIX_FMT_YUV422P,
        PIX_FMT_YUV420P,      PIX_FMT_YUV411P,
        PIX_FMT_YUV410P,      PIX_FMT_YUV440P,
        PIX_FMT_YUVJ444P,     PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ420P,     PIX_FMT_YUVJ440P,
        PIX_FMT_YUVA420P,

        PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

enum { RED = 0, GREEN, BLUE, ALPHA };

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PadContext *pad = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];
    uint8_t rgba_color[4];
    uint8_t rgba_map[4] = {0};
    int i, is_packed_rgb = 1;

    switch (inlink->format) {
    case PIX_FMT_ARGB:
        rgba_map[ALPHA] = 0; rgba_map[RED] = 1; rgba_map[GREEN] = 2; rgba_map[BLUE] = 3;
        break;
    case PIX_FMT_ABGR:
        rgba_map[ALPHA] = 0; rgba_map[BLUE] = 1; rgba_map[GREEN] = 2; rgba_map[RED] = 3;
        break;
    case PIX_FMT_RGBA:
    case PIX_FMT_RGB24:
        rgba_map[RED] = 0; rgba_map[GREEN] = 1; rgba_map[BLUE] = 2; rgba_map[ALPHA] = 3;
        break;
    case PIX_FMT_BGRA:
    case PIX_FMT_BGR24:
        rgba_map[BLUE] = 0; rgba_map[GREEN] = 1; rgba_map[RED] = 2; rgba_map[ALPHA] = 3;
        break;
    default:
        is_packed_rgb = 0;
    }

    pad->hsub = pix_desc->log2_chroma_w;
    pad->vsub = pix_desc->log2_chroma_h;

    if (!pad->w)
        pad->w = inlink->w;
    if (!pad->h)
        pad->h = inlink->h;

    pad->w &= ~((1 << pad->hsub) - 1);
    pad->h &= ~((1 << pad->vsub) - 1);
    pad->x &= ~((1 << pad->hsub) - 1);
    pad->y &= ~((1 << pad->vsub) - 1);

    pad->in_w = inlink->w & ~((1 << pad->hsub) - 1);
    pad->in_h = inlink->h & ~((1 << pad->vsub) - 1);

    memcpy(rgba_color, pad->color, sizeof(rgba_color));
    if (is_packed_rgb) {
        pad->line_step[0] = (av_get_bits_per_pixel(&av_pix_fmt_descriptors[inlink->format]))>>3;
        for (i = 0; i < 4; i++)
            pad->color[rgba_map[i]] = rgba_color[i];

        pad->line[0] = av_malloc(pad->w * pad->line_step[0]);
        for (i = 0; i < pad->w; i++)
            memcpy(pad->line[0] + i * pad->line_step[0], pad->color, pad->line_step[0]);
    } else {
        int plane;

        pad->color[0] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
        pad->color[1] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        pad->color[2] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        pad->color[3] = rgba_color[3];

        for (plane = 0; plane < 4; plane++) {
            int line_size;
            int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;

            pad->line_step[plane] = 1;
            line_size = (pad->w >> hsub) * pad->line_step[plane];
            pad->line[plane] = av_malloc(line_size);
            memset(pad->line[plane], pad->color[plane], line_size);
        }
    }

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X[%s]\n",
           pad->w, pad->h, pad->x, pad->y,
           pad->color[0], pad->color[1], pad->color[2], pad->color[3],
           is_packed_rgb ? "rgba" : "yuva");

    if (pad->x <  0 || pad->y <  0                      ||
        pad->w <= 0 || pad->h <= 0                      ||
        (unsigned)pad->x + (unsigned)inlink->w > pad->w ||
        (unsigned)pad->y + (unsigned)inlink->h > pad->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input area %d:%d:%d:%d not within the padded area 0:0:%d:%d or zero-sized\n",
               pad->x, pad->y, pad->x + inlink->w, pad->y + inlink->h, pad->w, pad->h);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    PadContext *pad = outlink->src->priv;

    outlink->w = pad->w;
    outlink->h = pad->h;
    return 0;
}

static AVFilterPicRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    PadContext *pad = inlink->dst->priv;

    AVFilterPicRef *picref = avfilter_get_video_buffer(inlink->dst->outputs[0], perms,
                                                       w + (pad->w - pad->in_w),
                                                       h + (pad->h - pad->in_h));
    int plane;

    for (plane = 0; plane < 4 && picref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        picref->data[plane] += (pad->x >> hsub) * pad->line_step[plane] +
            (pad->y >> vsub) * picref->linesize[plane];
    }

    return picref;
}

static void start_frame(AVFilterLink *inlink, AVFilterPicRef *inpicref)
{
    PadContext *pad = inlink->dst->priv;
    AVFilterPicRef *outpicref = avfilter_ref_pic(inpicref, ~0);
    int plane;

    inlink->dst->outputs[0]->outpic = outpicref;

    for (plane = 0; plane < 4 && outpicref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        outpicref->data[plane] -= (pad->x >> hsub) * pad->line_step[plane] +
            (pad->y >> vsub) * outpicref->linesize[plane];
    }

    avfilter_start_frame(inlink->dst->outputs[0], outpicref);
}

static void end_frame(AVFilterLink *link)
{
    avfilter_end_frame(link->dst->outputs[0]);
    avfilter_unref_pic(link->cur_pic);
}

static void draw_rectangle(AVFilterPicRef *outpic, uint8_t *line[4], int line_step[4],
                           int hsub, int vsub, int x, int y, int w, int h)
{
    int i, plane;
    uint8_t *p;

    for (plane = 0; plane < 4 && outpic->data[plane]; plane++) {
        int hsub1 = plane == 1 || plane == 2 ? hsub : 0;
        int vsub1 = plane == 1 || plane == 2 ? vsub : 0;

        p = outpic->data[plane] + (y >> vsub1) * outpic->linesize[plane];
        for (i = 0; i < (h >> vsub1); i++) {
            memcpy(p + (x >> hsub1) * line_step[plane], line[plane], (w >> hsub1) * line_step[plane]);
            p += outpic->linesize[plane];
        }
    }
}

static void draw_send_bar_slice(AVFilterLink *link, int y, int h, int slice_dir, int before_slice)
{
    PadContext *pad = link->dst->priv;
    int bar_y, bar_h = 0;

    if        (slice_dir * before_slice ==  1 && y == pad->y) {
        /* top bar */
        bar_y = 0;
        bar_h = pad->y;
    } else if (slice_dir * before_slice == -1 && (y + h) == (pad->y + pad->in_h)) {
        /* bottom bar */
        bar_y = pad->y + pad->in_h;
        bar_h = pad->h - pad->in_h - pad->y;
    }

    if (bar_h) {
        draw_rectangle(link->dst->outputs[0]->outpic,
                       pad->line, pad->line_step, pad->hsub, pad->vsub,
                       0, bar_y, pad->w, bar_h);
        avfilter_draw_slice(link->dst->outputs[0], bar_y, bar_h, slice_dir);
    }
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    PadContext *pad = link->dst->priv;
    AVFilterPicRef *outpic = link->dst->outputs[0]->outpic;

    y += pad->y;

    y &= ~((1 << pad->vsub) - 1);
    h &= ~((1 << pad->vsub) - 1);

    if (!h)
        return;
    draw_send_bar_slice(link, y, h, slice_dir, 1);

    /* left border */
    draw_rectangle(outpic, pad->line, pad->line_step, pad->hsub, pad->vsub,
                   0, y, pad->x, h);
    /* right border */
    draw_rectangle(outpic, pad->line, pad->line_step, pad->hsub, pad->vsub,
                   pad->x + pad->in_w, y, pad->w - pad->x - pad->in_w, h);
    avfilter_draw_slice(link->dst->outputs[0], y, h, slice_dir);

    draw_send_bar_slice(link, y, h, slice_dir, -1);
}

AVFilter avfilter_vf_pad = {
    .name          = "pad",
    .description   = "Add pads to the input image.",

    .priv_size     = sizeof(PadContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_input,
                                    .get_video_buffer = get_video_buffer,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_output, },
                                  { .name = NULL}},
};
