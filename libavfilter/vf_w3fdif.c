/*
 * Copyright (C) 2012 British Broadcasting Corporation, All Rights Reserved
 * Author of de-interlace algorithm: Jim Easterbrook for BBC R&D
 * Based on the process described by Martin Weston for BBC R&D
 * Author of FFmpeg filter: Mark Himsley for BBC Broadcast Systems Development
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct W3FDIFContext {
    const AVClass *class;
    int filter;           ///< 0 is simple, 1 is more complex
    int deint;            ///< which frames to deinterlace
    int linesize[4];      ///< bytes of pixel data per line for each plane
    int planeheight[4];   ///< height of each plane
    int field;            ///< which field are we on, 0 or 1
    int eof;
    int nb_planes;
    AVFrame *prev, *cur, *next;  ///< previous, current, next frames
    int32_t *work_line;   ///< line we are calculating
} W3FDIFContext;

#define OFFSET(x) offsetof(W3FDIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, unit }

static const AVOption w3fdif_options[] = {
    { "filter", "specify the filter", OFFSET(filter), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "filter" },
    CONST("simple",  NULL, 0, "filter"),
    CONST("complex", NULL, 1, "filter"),
    { "deint",  "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "deint" },
    CONST("all",        "deinterlace all frames",                       0, "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", 1, "deint"),
    { NULL }
};

AVFILTER_DEFINE_CLASS(w3fdif);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    W3FDIFContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->work_line = av_calloc(s->linesize[0], sizeof(*s->work_line));
    if (!s->work_line)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[0];

    outlink->time_base.num = inlink->time_base.num;
    outlink->time_base.den = inlink->time_base.den * 2;
    outlink->frame_rate.num = inlink->frame_rate.num * 2;
    outlink->frame_rate.den = inlink->frame_rate.den;
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    return 0;
}

/*
 * Filter coefficients from PH-2071, scaled by 256 * 256.
 * Each set of coefficients has a set for low-frequencies and high-frequencies.
 * n_coef_lf[] and n_coef_hf[] are the number of coefs for simple and more-complex.
 * It is important for later that n_coef_lf[] is even and n_coef_hf[] is odd.
 * coef_lf[][] and coef_hf[][] are the coefficients for low-frequencies
 * and high-frequencies for simple and more-complex mode.
 */
static const int8_t   n_coef_lf[2] = { 2, 4 };
static const int32_t coef_lf[2][4] = {{ 32768, 32768,     0,     0},
                                      { -1704, 34472, 34472, -1704}};
static const int8_t   n_coef_hf[2] = { 3, 5 };
static const int32_t coef_hf[2][5] = {{ -4096,  8192, -4096,     0,     0},
                                      {  2032, -7602, 11140, -7602,  2032}};

static void deinterlace_plane(AVFilterContext *ctx, AVFrame *out,
                              const AVFrame *cur, const AVFrame *adj,
                              const int filter, const int plane)
{
    W3FDIFContext *s = ctx->priv;
    uint8_t *in_line, *in_lines_cur[5], *in_lines_adj[5];
    uint8_t *out_line, *out_pixel;
    int32_t *work_line, *work_pixel;
    uint8_t *cur_data = cur->data[plane];
    uint8_t *adj_data = adj->data[plane];
    uint8_t *dst_data = out->data[plane];
    const int linesize = s->linesize[plane];
    const int height   = s->planeheight[plane];
    const int cur_line_stride = cur->linesize[plane];
    const int adj_line_stride = adj->linesize[plane];
    const int dst_line_stride = out->linesize[plane];
    int i, j, y_in, y_out;

    /* copy unchanged the lines of the field */
    y_out = s->field == cur->top_field_first;

    in_line  = cur_data + (y_out * cur_line_stride);
    out_line = dst_data + (y_out * dst_line_stride);

    while (y_out < height) {
        memcpy(out_line, in_line, linesize);
        y_out += 2;
        in_line  += cur_line_stride * 2;
        out_line += dst_line_stride * 2;
    }

    /* interpolate other lines of the field */
    y_out = s->field != cur->top_field_first;

    out_line = dst_data + (y_out * dst_line_stride);

    while (y_out < height) {
        /* clear workspace */
        memset(s->work_line, 0, sizeof(*s->work_line) * linesize);

        /* get low vertical frequencies from current field */
        for (j = 0; j < n_coef_lf[filter]; j++) {
            y_in = (y_out + 1) + (j * 2) - n_coef_lf[filter];

            while (y_in < 0)
                y_in += 2;
            while (y_in >= height)
                y_in -= 2;

            in_lines_cur[j] = cur_data + (y_in * cur_line_stride);
        }

        work_line = s->work_line;
        switch (n_coef_lf[filter]) {
        case 2:
            for (i = 0; i < linesize; i++) {
                *work_line   += *in_lines_cur[0]++ * coef_lf[filter][0];
                *work_line++ += *in_lines_cur[1]++ * coef_lf[filter][1];
            }
            break;
        case 4:
            for (i = 0; i < linesize; i++) {
                *work_line   += *in_lines_cur[0]++ * coef_lf[filter][0];
                *work_line   += *in_lines_cur[1]++ * coef_lf[filter][1];
                *work_line   += *in_lines_cur[2]++ * coef_lf[filter][2];
                *work_line++ += *in_lines_cur[3]++ * coef_lf[filter][3];
            }
        }

        /* get high vertical frequencies from adjacent fields */
        for (j = 0; j < n_coef_hf[filter]; j++) {
            y_in = (y_out + 1) + (j * 2) - n_coef_hf[filter];

            while (y_in < 0)
                y_in += 2;
            while (y_in >= height)
                y_in -= 2;

            in_lines_cur[j] = cur_data + (y_in * cur_line_stride);
            in_lines_adj[j] = adj_data + (y_in * adj_line_stride);
        }

        work_line = s->work_line;
        switch (n_coef_hf[filter]) {
        case 3:
            for (i = 0; i < linesize; i++) {
                *work_line   += *in_lines_cur[0]++ * coef_hf[filter][0];
                *work_line   += *in_lines_adj[0]++ * coef_hf[filter][0];
                *work_line   += *in_lines_cur[1]++ * coef_hf[filter][1];
                *work_line   += *in_lines_adj[1]++ * coef_hf[filter][1];
                *work_line   += *in_lines_cur[2]++ * coef_hf[filter][2];
                *work_line++ += *in_lines_adj[2]++ * coef_hf[filter][2];
            }
            break;
        case 5:
            for (i = 0; i < linesize; i++) {
                *work_line   += *in_lines_cur[0]++ * coef_hf[filter][0];
                *work_line   += *in_lines_adj[0]++ * coef_hf[filter][0];
                *work_line   += *in_lines_cur[1]++ * coef_hf[filter][1];
                *work_line   += *in_lines_adj[1]++ * coef_hf[filter][1];
                *work_line   += *in_lines_cur[2]++ * coef_hf[filter][2];
                *work_line   += *in_lines_adj[2]++ * coef_hf[filter][2];
                *work_line   += *in_lines_cur[3]++ * coef_hf[filter][3];
                *work_line   += *in_lines_adj[3]++ * coef_hf[filter][3];
                *work_line   += *in_lines_cur[4]++ * coef_hf[filter][4];
                *work_line++ += *in_lines_adj[4]++ * coef_hf[filter][4];
            }
        }

        /* save scaled result to the output frame, scaling down by 256 * 256 */
        work_pixel = s->work_line;
        out_pixel = out_line;

        for (j = 0; j < linesize; j++, out_pixel++, work_pixel++)
             *out_pixel = av_clip(*work_pixel, 0, 255 * 256 * 256) >> 16;

        /* move on to next line */
        y_out += 2;
        out_line += dst_line_stride * 2;
    }
}

static int filter(AVFilterContext *ctx, int is_second)
{
    W3FDIFContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *adj;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(out, s->cur);
    out->interlaced_frame = 0;

    if (!is_second) {
        if (out->pts != AV_NOPTS_VALUE)
            out->pts *= 2;
    } else {
        int64_t cur_pts  = s->cur->pts;
        int64_t next_pts = s->next->pts;

        if (next_pts != AV_NOPTS_VALUE && cur_pts != AV_NOPTS_VALUE) {
            out->pts = cur_pts + next_pts;
        } else {
            out->pts = AV_NOPTS_VALUE;
        }
    }

    adj = s->field ? s->next : s->prev;
    for (plane = 0; plane < s->nb_planes; plane++)
        deinterlace_plane(ctx, out, s->cur, adj, s->filter, plane);

    s->field = !s->field;

    return ff_filter_frame(outlink, out);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    W3FDIFContext *s = ctx->priv;
    int ret;

    av_frame_free(&s->prev);
    s->prev = s->cur;
    s->cur  = s->next;
    s->next = frame;

    if (!s->cur) {
        s->cur = av_frame_clone(s->next);
        if (!s->cur)
            return AVERROR(ENOMEM);
    }

    if ((s->deint && !s->cur->interlaced_frame) || ctx->is_disabled) {
        AVFrame *out = av_frame_clone(s->cur);
        if (!out)
            return AVERROR(ENOMEM);

        av_frame_free(&s->prev);
        if (out->pts != AV_NOPTS_VALUE)
            out->pts *= 2;
        return ff_filter_frame(ctx->outputs[0], out);
    }

    if (!s->prev)
        return 0;

    ret = filter(ctx, 0);
    if (ret < 0)
        return ret;

    return filter(ctx, 1);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    W3FDIFContext *s = ctx->priv;

    do {
        int ret;

        if (s->eof)
            return AVERROR_EOF;

        ret = ff_request_frame(ctx->inputs[0]);

        if (ret == AVERROR_EOF && s->cur) {
            AVFrame *next = av_frame_clone(s->next);
            if (!next)
                return AVERROR(ENOMEM);
            next->pts = s->next->pts * 2 - s->cur->pts;
            filter_frame(ctx->inputs[0], next);
            s->eof = 1;
        } else if (ret < 0) {
            return ret;
        }
    } while (!s->cur);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    W3FDIFContext *s = ctx->priv;

    av_frame_free(&s->prev);
    av_frame_free(&s->cur );
    av_frame_free(&s->next);
    av_freep(&s->work_line);
}

static const AVFilterPad w3fdif_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
    { NULL }
};

static const AVFilterPad w3fdif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_w3fdif = {
    .name          = "w3fdif",
    .description   = NULL_IF_CONFIG_SMALL("Apply Martin Weston three field deinterlace."),
    .priv_size     = sizeof(W3FDIFContext),
    .priv_class    = &w3fdif_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = w3fdif_inputs,
    .outputs       = w3fdif_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
