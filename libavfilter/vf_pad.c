/*
 * Copyright (c) 2008 vmrsss
 * Copyright (c) 2009 Stefano Sabatini
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
 * video padding filter and color source
 */

#include "avfilter.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"
#include "libavutil/avassert.h"
#include "libavcore/imgutils.h"
#include "libavcore/parseutils.h"

enum { RED = 0, GREEN, BLUE, ALPHA };

static int fill_line_with_color(uint8_t *line[4], int line_step[4], int w, uint8_t color[4],
                                enum PixelFormat pix_fmt, uint8_t rgba_color[4], int *is_packed_rgba)
{
    uint8_t rgba_map[4] = {0};
    int i;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[pix_fmt];
    int hsub = pix_desc->log2_chroma_w;

    *is_packed_rgba = 1;
    switch (pix_fmt) {
    case PIX_FMT_ARGB:  rgba_map[ALPHA] = 0; rgba_map[RED  ] = 1; rgba_map[GREEN] = 2; rgba_map[BLUE ] = 3; break;
    case PIX_FMT_ABGR:  rgba_map[ALPHA] = 0; rgba_map[BLUE ] = 1; rgba_map[GREEN] = 2; rgba_map[RED  ] = 3; break;
    case PIX_FMT_RGBA:
    case PIX_FMT_RGB24: rgba_map[RED  ] = 0; rgba_map[GREEN] = 1; rgba_map[BLUE ] = 2; rgba_map[ALPHA] = 3; break;
    case PIX_FMT_BGRA:
    case PIX_FMT_BGR24: rgba_map[BLUE ] = 0; rgba_map[GREEN] = 1; rgba_map[RED  ] = 2; rgba_map[ALPHA] = 3; break;
    default:
        *is_packed_rgba = 0;
    }

    if (*is_packed_rgba) {
        line_step[0] = (av_get_bits_per_pixel(pix_desc))>>3;
        for (i = 0; i < 4; i++)
            color[rgba_map[i]] = rgba_color[i];

        line[0] = av_malloc(w * line_step[0]);
        for (i = 0; i < w; i++)
            memcpy(line[0] + i * line_step[0], color, line_step[0]);
    } else {
        int plane;

        color[RED  ] = RGB_TO_Y_CCIR(rgba_color[0], rgba_color[1], rgba_color[2]);
        color[GREEN] = RGB_TO_U_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        color[BLUE ] = RGB_TO_V_CCIR(rgba_color[0], rgba_color[1], rgba_color[2], 0);
        color[ALPHA] = rgba_color[3];

        for (plane = 0; plane < 4; plane++) {
            int line_size;
            int hsub1 = (plane == 1 || plane == 2) ? hsub : 0;

            line_step[plane] = 1;
            line_size = (w >> hsub1) * line_step[plane];
            line[plane] = av_malloc(line_size);
            memset(line[plane], color[plane], line_size);
        }
    }

    return 0;
}

static void draw_rectangle(AVFilterBufferRef *outpic, uint8_t *line[4], int line_step[4],
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

static void copy_rectangle(AVFilterBufferRef *outpic,uint8_t *line[4], int line_step[4], int linesize[4],
                           int hsub, int vsub, int x, int y, int y2, int w, int h)
{
    int i, plane;
    uint8_t *p;

    for (plane = 0; plane < 4 && outpic->data[plane]; plane++) {
        int hsub1 = plane == 1 || plane == 2 ? hsub : 0;
        int vsub1 = plane == 1 || plane == 2 ? vsub : 0;

        p = outpic->data[plane] + (y >> vsub1) * outpic->linesize[plane];
        for (i = 0; i < (h >> vsub1); i++) {
            memcpy(p + (x >> hsub1) * line_step[plane], line[plane] + linesize[plane]*(i+(y2>>vsub1)), (w >> hsub1) * line_step[plane]);
            p += outpic->linesize[plane];
        }
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

#if CONFIG_PAD_FILTER

typedef struct {
    int w, h;               ///< output dimensions, a value of 0 will result in the input size
    int x, y;               ///< offsets of the input area with respect to the padded area
    int in_w, in_h;         ///< width and height for the padded input video, which has to be aligned to the chroma values in order to avoid chroma issues

    uint8_t color[4];       ///< color expressed either in YUVA or RGBA colorspace for the padding area
    uint8_t *line[4];
    int      line_step[4];
    int hsub, vsub;         ///< chroma subsampling values
    int needs_copy;
} PadContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    PadContext *pad = ctx->priv;
    char color_string[128] = "black";

    if (args)
        sscanf(args, "%d:%d:%d:%d:%s", &pad->w, &pad->h, &pad->x, &pad->y, color_string);

    if (av_parse_color(pad->color, color_string, -1, ctx) < 0)
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

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PadContext *pad = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];
    uint8_t rgba_color[4];
    int is_packed_rgba;

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
    fill_line_with_color(pad->line, pad->line_step, pad->w, pad->color,
                         inlink->format, rgba_color, &is_packed_rgba);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X[%s]\n",
           inlink->w, inlink->h, pad->w, pad->h, pad->x, pad->y,
           pad->color[0], pad->color[1], pad->color[2], pad->color[3],
           is_packed_rgba ? "rgba" : "yuva");

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

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    PadContext *pad = inlink->dst->priv;

    AVFilterBufferRef *picref = avfilter_get_video_buffer(inlink->dst->outputs[0], perms,
                                                       w + (pad->w - pad->in_w),
                                                       h + (pad->h - pad->in_h));
    int plane;

    picref->video->w = w;
    picref->video->h = h;

    for (plane = 0; plane < 4 && picref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        picref->data[plane] += (pad->x >> hsub) * pad->line_step[plane] +
            (pad->y >> vsub) * picref->linesize[plane];
    }

    return picref;
}

static int does_clip(PadContext *pad, AVFilterBufferRef *outpicref, int plane, int hsub, int vsub, int x, int y)
{
    int64_t x_in_buf, y_in_buf;

    x_in_buf =  outpicref->data[plane] - outpicref->buf->data[plane]
             +  (x >> hsub) * pad      ->line_step[plane]
             +  (y >> vsub) * outpicref->linesize [plane];

    if(x_in_buf < 0 || x_in_buf % pad->line_step[plane])
        return 1;
    x_in_buf /= pad->line_step[plane];

    av_assert0(outpicref->buf->linesize[plane]>0); //while reference can use negative linesize the main buffer should not

    y_in_buf = x_in_buf / outpicref->buf->linesize[plane];
    x_in_buf %= outpicref->buf->linesize[plane];

    if(   y_in_buf<<vsub >= outpicref->buf->h
       || x_in_buf<<hsub >= outpicref->buf->w)
        return 1;
    return 0;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    PadContext *pad = inlink->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);
    int plane;

    for (plane = 0; plane < 4 && outpicref->data[plane]; plane++) {
        int hsub = (plane == 1 || plane == 2) ? pad->hsub : 0;
        int vsub = (plane == 1 || plane == 2) ? pad->vsub : 0;

        av_assert0(outpicref->buf->w>0 && outpicref->buf->h>0);

        if(outpicref->format != outpicref->buf->format) //unsupported currently
            break;

        outpicref->data[plane] -=   (pad->x  >> hsub) * pad      ->line_step[plane]
                                  + (pad->y  >> vsub) * outpicref->linesize [plane];

        if(   does_clip(pad, outpicref, plane, hsub, vsub, 0, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, 0, pad->h-1)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, 0)
           || does_clip(pad, outpicref, plane, hsub, vsub, pad->w-1, pad->h-1)
          )
            break;
    }
    pad->needs_copy= plane < 4 && outpicref->data[plane];
    if(pad->needs_copy){
        av_log(inlink->dst, AV_LOG_DEBUG, "Direct padding impossible allocating new frame\n");
        avfilter_unref_buffer(outpicref);
        outpicref = avfilter_get_video_buffer(inlink->dst->outputs[0], AV_PERM_WRITE | AV_PERM_NEG_LINESIZES,
                                                       FFMAX(inlink->w, pad->w),
                                                       FFMAX(inlink->h, pad->h));
        avfilter_copy_buffer_ref_props(outpicref, inpicref);
    }

    inlink->dst->outputs[0]->out_buf = outpicref;

    outpicref->video->w = pad->w;
    outpicref->video->h = pad->h;

    avfilter_start_frame(inlink->dst->outputs[0], outpicref);
}

static void end_frame(AVFilterLink *link)
{
    avfilter_end_frame(link->dst->outputs[0]);
    avfilter_unref_buffer(link->cur_buf);
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
        draw_rectangle(link->dst->outputs[0]->out_buf,
                       pad->line, pad->line_step, pad->hsub, pad->vsub,
                       0, bar_y, pad->w, bar_h);
        avfilter_draw_slice(link->dst->outputs[0], bar_y, bar_h, slice_dir);
    }
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    PadContext *pad = link->dst->priv;
    AVFilterBufferRef *outpic = link->dst->outputs[0]->out_buf;
    AVFilterBufferRef *inpic = link->cur_buf;

    y += pad->y;

    y &= ~((1 << pad->vsub) - 1);
    h &= ~((1 << pad->vsub) - 1);

    if (!h)
        return;
    draw_send_bar_slice(link, y, h, slice_dir, 1);

    /* left border */
    draw_rectangle(outpic, pad->line, pad->line_step, pad->hsub, pad->vsub,
                   0, y, pad->x, h);

    if(pad->needs_copy){
        copy_rectangle(outpic,
                       inpic->data, pad->line_step, inpic->linesize, pad->hsub, pad->vsub,
                       pad->x, y, y-pad->y, inpic->video->w, h);
    }

    /* right border */
    draw_rectangle(outpic, pad->line, pad->line_step, pad->hsub, pad->vsub,
                   pad->x + pad->in_w, y, pad->w - pad->x - pad->in_w, h);
    avfilter_draw_slice(link->dst->outputs[0], y, h, slice_dir);

    draw_send_bar_slice(link, y, h, slice_dir, -1);
}

AVFilter avfilter_vf_pad = {
    .name          = "pad",
    .description   = NULL_IF_CONFIG_SMALL("Pad input image to width:height[:x:y[:color]] (default x and y: 0, default color: black)."),

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

#endif /* CONFIG_PAD_FILTER */

#if CONFIG_COLOR_FILTER

typedef struct {
    int w, h;
    uint8_t color[4];
    AVRational time_base;
    uint8_t *line[4];
    int      line_step[4];
    int hsub, vsub;         ///< chroma subsampling values
    uint64_t pts;
} ColorContext;

static av_cold int color_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ColorContext *color = ctx->priv;
    char color_string[128] = "black";
    char frame_size  [128] = "320x240";
    char frame_rate  [128] = "25";
    AVRational frame_rate_q;
    int ret;

    if (args)
        sscanf(args, "%127[^:]:%127[^:]:%127s", color_string, frame_size, frame_rate);

    if (av_parse_video_size(&color->w, &color->h, frame_size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", frame_size);
        return AVERROR(EINVAL);
    }

    if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", frame_rate);
        return AVERROR(EINVAL);
    }
    color->time_base.num = frame_rate_q.den;
    color->time_base.den = frame_rate_q.num;

    if ((ret = av_parse_color(color->color, color_string, -1, ctx)) < 0)
        return ret;

    return 0;
}

static av_cold void color_uninit(AVFilterContext *ctx)
{
    ColorContext *color = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_freep(&color->line[i]);
        color->line_step[i] = 0;
    }
}

static int color_config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    ColorContext *color = ctx->priv;
    uint8_t rgba_color[4];
    int is_packed_rgba;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];

    color->hsub = pix_desc->log2_chroma_w;
    color->vsub = pix_desc->log2_chroma_h;

    color->w &= ~((1 << color->hsub) - 1);
    color->h &= ~((1 << color->vsub) - 1);
    if (av_image_check_size(color->w, color->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    memcpy(rgba_color, color->color, sizeof(rgba_color));
    fill_line_with_color(color->line, color->line_step, color->w, color->color,
                         inlink->format, rgba_color, &is_packed_rgba);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d r:%d/%d color:0x%02x%02x%02x%02x[%s]\n",
           color->w, color->h, color->time_base.den, color->time_base.num,
           color->color[0], color->color[1], color->color[2], color->color[3],
           is_packed_rgba ? "rgba" : "yuva");
    inlink->w = color->w;
    inlink->h = color->h;

    return 0;
}

static int color_request_frame(AVFilterLink *link)
{
    ColorContext *color = link->src->priv;
    AVFilterBufferRef *picref = avfilter_get_video_buffer(link, AV_PERM_WRITE, color->w, color->h);
    picref->video->pixel_aspect = (AVRational) {1, 1};
    picref->pts                 = av_rescale_q(color->pts++, color->time_base, AV_TIME_BASE_Q);
    picref->pos                 = 0;

    avfilter_start_frame(link, avfilter_ref_buffer(picref, ~0));
    draw_rectangle(picref,
                   color->line, color->line_step, color->hsub, color->vsub,
                   0, 0, color->w, color->h);
    avfilter_draw_slice(link, 0, color->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(picref);

    return 0;
}

AVFilter avfilter_vsrc_color = {
    .name        = "color",
    .description = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input, syntax is: [color[:size[:rate]]]"),

    .priv_size = sizeof(ColorContext),
    .init      = color_init,
    .uninit    = color_uninit,

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = color_request_frame,
                                    .config_props    = color_config_props },
                                  { .name = NULL}},
};

#endif /* CONFIG_COLOR_FILTER */
