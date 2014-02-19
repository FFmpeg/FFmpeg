/*
 * Copyright (c) 2003 Michael Zucchi <notzed@ximian.com>
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2013 Vittorio Giovara <vittorio.giovara@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * progressive to interlaced content filter, inspired by heavy debugging of tinterlace filter
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#include "formats.h"
#include "avfilter.h"
#include "internal.h"
#include "version.h"
#include "video.h"

enum ScanMode {
    MODE_TFF = 0,
    MODE_BFF = 1,
};

enum FieldType {
    FIELD_UPPER = 0,
    FIELD_LOWER = 1,
};

typedef struct {
    const AVClass *class;
    enum ScanMode scan;    // top or bottom field first scanning
#if FF_API_INTERLACE_LOWPASS_SET
    int lowpass;           // enable or disable low pass filterning
#endif
    AVFrame *cur, *next;   // the two frames from which the new one is obtained
} InterlaceContext;

#define OFFSET(x) offsetof(InterlaceContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
static const AVOption interlace_options[] = {
    { "scan", "scanning mode", OFFSET(scan),
        AV_OPT_TYPE_INT,   {.i64 = MODE_TFF }, 0, 1, .flags = V, .unit = "scan" },
    { "tff", "top field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_TFF }, INT_MIN, INT_MAX, .flags = V, .unit = "scan" },
    { "bff", "bottom field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_BFF }, INT_MIN, INT_MAX, .flags = V, .unit = "scan" },
#if FF_API_INTERLACE_LOWPASS_SET
    { "lowpass", "(deprecated, this option is always set)", OFFSET(lowpass),
        AV_OPT_TYPE_INT,   {.i64 = 1 },        0, 1, .flags = V },
#endif
    { NULL }
};

AVFILTER_DEFINE_CLASS(interlace);

static const enum AVPixelFormat formats_supported[] = {
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_GRAY8,    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_make_format_list(formats_supported));
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    InterlaceContext *s = ctx->priv;

    av_frame_free(&s->cur);
    av_frame_free(&s->next);
}

static int config_out_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    InterlaceContext *s = ctx->priv;

#if FF_API_INTERLACE_LOWPASS_SET
    if (!s->lowpass)
        av_log(ctx, AV_LOG_WARNING, "This option is deprecated and always set.\n");
#endif

    if (inlink->h < 2) {
        av_log(ctx, AV_LOG_ERROR, "input video height is too small\n");
        return AVERROR_INVALIDDATA;
    }
    // same input size
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;
    // half framerate
    outlink->time_base.num *= 2;
    outlink->frame_rate.den *= 2;
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    av_log(ctx, AV_LOG_VERBOSE, "%s interlacing\n",
           s->scan == MODE_TFF ? "tff" : "bff");

    return 0;
}

static void copy_picture_field(AVFrame *src_frame, AVFrame *dst_frame,
                               AVFilterLink *inlink, enum FieldType field_type)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub = desc->log2_chroma_h;
    int plane, i, j;

    for (plane = 0; plane < desc->nb_components; plane++) {
        int lines = (plane == 1 || plane == 2) ? FF_CEIL_RSHIFT(inlink->h, vsub) : inlink->h;
        int linesize = av_image_get_linesize(inlink->format, inlink->w, plane);
        uint8_t *dstp = dst_frame->data[plane];
        const uint8_t *srcp = src_frame->data[plane];
        int srcp_linesize;
        int dstp_linesize;

        av_assert0(linesize >= 0);

        lines = (lines + (field_type == FIELD_UPPER)) / 2;
        if (field_type == FIELD_LOWER)
            srcp += src_frame->linesize[plane];
        if (field_type == FIELD_LOWER)
            dstp += dst_frame->linesize[plane];

        srcp_linesize = src_frame->linesize[plane] * 2;
        dstp_linesize = dst_frame->linesize[plane] * 2;
        for (j = lines; j > 0; j--) {
            const uint8_t *srcp_above = srcp - src_frame->linesize[plane];
            const uint8_t *srcp_below = srcp + src_frame->linesize[plane];
            if (j == lines)
                srcp_above = srcp; // there is no line above
            if (j == 1)
                srcp_below = srcp; // there is no line below
            for (i = 0; i < linesize; i++) {
                // this calculation is an integer representation of
                // '0.5 * current + 0.25 * above + 0.25 * below'
                // '1 +' is for rounding.
                dstp[i] = (1 + srcp[i] + srcp[i] + srcp_above[i] + srcp_below[i]) >> 2;
            }
            dstp += dstp_linesize;
            srcp += srcp_linesize;
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    InterlaceContext *s = ctx->priv;
    AVFrame *out;
    int tff, ret;

    av_frame_free(&s->cur);
    s->cur  = s->next;
    s->next = buf;

    /* we need at least two frames */
    if (!s->cur || !s->next)
        return 0;

    if (s->cur->interlaced_frame) {
        av_log(ctx, AV_LOG_WARNING,
               "video is already interlaced, adjusting framerate only\n");
        out = av_frame_clone(s->cur);
        if (!out)
            return AVERROR(ENOMEM);
        out->pts /= 2;  // adjust pts to new framerate
        ret = ff_filter_frame(outlink, out);
        return ret;
    }

    tff = (s->scan == MODE_TFF);
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, s->cur);
    out->interlaced_frame = 1;
    out->top_field_first  = tff;
    out->pts             /= 2;  // adjust pts to new framerate

    /* copy upper/lower field from cur */
    copy_picture_field(s->cur, out, inlink, tff ? FIELD_UPPER : FIELD_LOWER);
    av_frame_free(&s->cur);

    /* copy lower/upper field from next */
    copy_picture_field(s->next, out, inlink, tff ? FIELD_LOWER : FIELD_UPPER);
    av_frame_free(&s->next);

    ret = ff_filter_frame(outlink, out);

    return ret;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_out_props,
    },
    { NULL }
};

AVFilter ff_vf_interlace = {
    .name          = "interlace",
    .description   = NULL_IF_CONFIG_SMALL("Convert progressive video into interlaced."),
    .uninit        = uninit,
    .priv_class    = &interlace_class,
    .priv_size     = sizeof(InterlaceContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
};
