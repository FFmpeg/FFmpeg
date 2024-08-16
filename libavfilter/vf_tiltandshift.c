/*
 * Copyright (c) 2014 Vittorio Giovara
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
 * @file vf_tiltandshift.c
 * Simple time and space inverter.
 */

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

enum PaddingOption {
    TILT_NONE,
    TILT_FRAME,
    TILT_BLACK,
    TILT_OPT_MAX,
};

typedef struct TiltandshiftContext {
    const AVClass *class;

    /* set when all input frames have been processed and we have to
     * empty buffers, pad and then return */
    int eof_recv;

    /* live or static sliding */
    int tilt;

    /* initial or final actions to perform (pad/hold a frame/black/nothing) */
    enum PaddingOption start;
    enum PaddingOption end;

    /* columns to hold or pad at the beginning or at the end (respectively) */
    int hold;
    int pad;

    /* buffers for black columns */
    uint8_t *black_buffers[4];
    int black_linesizes[4];

    /* list containing all input frames */
    size_t input_size;
    AVFrame *input;
    AVFrame *prev;

    const AVPixFmtDescriptor *desc;
} TiltandshiftContext;

static int list_add_frame(TiltandshiftContext *s, AVFrame *frame)
{
    if (s->input == NULL) {
        s->input = frame;
    } else {
        AVFrame *head = s->input;
        while (head->opaque)
            head = head->opaque;
        head->opaque = frame;
    }
    s->input_size++;
    return 0;
}

static void list_remove_head(TiltandshiftContext *s)
{
    AVFrame *head = s->input;
    if (head) {
        s->input = head->opaque;
        av_frame_free(&head);
    }
    s->input_size--;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_NONE
};

static av_cold void uninit(AVFilterContext *ctx)
{
    TiltandshiftContext *s = ctx->priv;
    while (s->input)
        list_remove_head(s);
    av_freep(&s->black_buffers);
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TiltandshiftContext *s = ctx->priv;

    outlink->w = ctx->inputs[0]->w;
    outlink->h = ctx->inputs[0]->h;
    outlink->format = ctx->inputs[0]->format;

    // when we have to pad black or a frame at the start, skip navigating
    // the list and use either the frame or black for the requested value
    if (s->start != TILT_NONE && !s->hold)
        s->hold = outlink->w;

    // Init black buffers if we pad with black at the start or at the end.
    // For the end, we always have to init on NONE and BLACK because we never
    // know if there are going to be enough input frames to fill an output one.
    if (s->start == TILT_BLACK || s->end != TILT_FRAME) {
        int i, j, ret;
        uint8_t black_data[] = { 0x10, 0x80, 0x80, 0x10 };
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
        if (!desc)
            return AVERROR_BUG;

        if (outlink->format == AV_PIX_FMT_YUVJ420P ||
            outlink->format == AV_PIX_FMT_YUVJ422P ||
            outlink->format == AV_PIX_FMT_YUVJ444P ||
            outlink->format == AV_PIX_FMT_YUVJ440P ||
            outlink->color_range == AVCOL_RANGE_JPEG)
            black_data[0] = black_data[3] = 0;

        ret = av_image_alloc(s->black_buffers, s->black_linesizes, 1,
                             outlink->h, outlink->format, 1);
        if (ret < 0)
            return ret;

        for (i = 0; i < FFMIN(desc->nb_components, 4); i++)
            for (j = 0; j < (!i ? outlink->h
                                : -((-outlink->h) >> desc->log2_chroma_h)); j++)
                memset(s->black_buffers[i] + j * s->black_linesizes[i],
                       black_data[i], 1);

        av_log(ctx, AV_LOG_VERBOSE, "Padding buffers initialized.\n");
    }

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;

    return 0;
}


static void copy_column(AVFilterLink *outlink,
                        uint8_t *dst_data[4], int dst_linesizes[4],
                        const uint8_t *src_data[4], const int src_linesizes[4],
                        int ncol, int tilt)
{
    AVFilterContext *ctx = outlink->src;
    TiltandshiftContext *s = ctx->priv;
    uint8_t *dst[4];
    const uint8_t *src[4];

    dst[0] = dst_data[0] + ncol;
    dst[1] = dst_data[1] + (ncol >> s->desc->log2_chroma_w);
    dst[2] = dst_data[2] + (ncol >> s->desc->log2_chroma_w);

    if (!tilt)
        ncol = 0;
    src[0] = src_data[0] + ncol;
    src[1] = src_data[1] + (ncol >> s->desc->log2_chroma_w);
    src[2] = src_data[2] + (ncol >> s->desc->log2_chroma_w);

    av_image_copy(dst, dst_linesizes, src, src_linesizes, outlink->format, 1, outlink->h);
}

static int output_frame(AVFilterLink *outlink)
{
    TiltandshiftContext *s = outlink->src->priv;
    AVFrame *head;
    int ret;

    int ncol = 0;
    AVFrame *dst = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!dst)
        return AVERROR(ENOMEM);

    // in case we have to do any initial black padding
    if (s->start == TILT_BLACK) {
        for ( ; ncol < s->hold; ncol++)
            copy_column(outlink, dst->data, dst->linesize,
                        (const uint8_t **)s->black_buffers, s->black_linesizes,
                        ncol, 0);
    }

    head = s->input;
    // copy a column from each input frame
    for ( ; ncol < s->input_size; ncol++) {
        AVFrame *src = head;

        copy_column(outlink, dst->data, dst->linesize,
                    (const uint8_t **)src->data, src->linesize,
                    ncol, s->tilt);

        // keep track of the last known frame in case we need it below
        s->prev = head;
        // advance to the next frame unless we have to hold it
        if (s->hold <= ncol)
            head = head->opaque;
    }

    // pad any remaining space with black or last frame
    if (s->end == TILT_FRAME) {
        for ( ; ncol < outlink->w; ncol++)
            copy_column(outlink, dst->data, dst->linesize,
                        (const uint8_t **)s->prev->data,
                        s->prev->linesize, ncol, 1);
    } else { // TILT_BLACK and TILT_NONE
        for ( ; ncol < outlink->w; ncol++)
            copy_column(outlink, dst->data, dst->linesize,
                        (const uint8_t **)s->black_buffers, s->black_linesizes,
                        ncol, 0);
    }

    // set correct timestamps and props as long as there is proper input
    ret = av_frame_copy_props(dst, s->input);
    if (ret < 0) {
        av_frame_free(&dst);
        return ret;
    }

    // discard frame at the top of the list since it has been fully processed
    list_remove_head(s);
    // and it is safe to reduce the hold value (even if unused)
    s->hold--;

    // output
    return ff_filter_frame(outlink, dst);
}

// This function just polls for new frames and queues them on a list
static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterContext *ctx = outlink->src;
    TiltandshiftContext *s = inlink->dst->priv;

    int ret = list_add_frame(s, frame);
    if (ret < 0) {
        return ret;
    }

    // load up enough frames to fill a frame and keep the queue filled on subsequent
    // calls, until we receive EOF, and then we either pad or end
    if (!s->eof_recv && s->input_size < outlink->w - s->pad) {
        av_log(ctx, AV_LOG_DEBUG, "Not enough frames in the list (%zu/%d), waiting for more.\n", s->input_size, outlink->w - s->pad);
        return 0;
    }

    return output_frame(outlink);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TiltandshiftContext *s = ctx->priv;
    int ret;

    // signal job finished when list is empty or when padding is either
    // limited or disabled and eof was received
    if ((s->input_size <= 0 || s->input_size == outlink->w - s->pad || s->end == TILT_NONE) && s->eof_recv) {
        return AVERROR_EOF;
    }

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        s->eof_recv = 1;
    } else if (ret < 0) {
        return ret;
    }

    if (s->eof_recv) {
        while (s->input_size) {
            av_log(ctx, AV_LOG_DEBUG, "Emptying buffers (%zu/%d).\n", s->input_size, outlink->w - s->pad);
            ret = output_frame(outlink);
            if (ret < 0) {
                return ret;
            }
        }
    }

    return 0;
}

#define OFFSET(x) offsetof(TiltandshiftContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tiltandshift_options[] = {
    { "tilt", "Tilt the video horizontally while shifting", OFFSET(tilt), AV_OPT_TYPE_INT,
        { .i64 = 1 }, 0, 1, .flags = V, .unit = "tilt" },

    { "start", "Action at the start of input", OFFSET(start), AV_OPT_TYPE_INT,
        { .i64 = TILT_NONE }, 0, TILT_OPT_MAX, .flags = V, .unit = "start" },
    { "none", "Start immediately (default)", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_NONE }, INT_MIN, INT_MAX, .flags = V, .unit = "start" },
    { "frame", "Use the first frames", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_FRAME }, INT_MIN, INT_MAX, .flags = V, .unit = "start" },
    { "black", "Fill with black", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_BLACK }, INT_MIN, INT_MAX, .flags = V, .unit = "start" },

    { "end", "Action at the end of input", OFFSET(end), AV_OPT_TYPE_INT,
        { .i64 = TILT_NONE }, 0, TILT_OPT_MAX, .flags = V, .unit = "end" },
    { "none", "Do not pad at the end (default)", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_NONE }, INT_MIN, INT_MAX, .flags = V, .unit = "end" },
    { "frame", "Use the last frame", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_FRAME }, INT_MIN, INT_MAX, .flags = V, .unit = "end" },
    { "black", "Fill with black", 0, AV_OPT_TYPE_CONST,
        { .i64 = TILT_BLACK }, INT_MIN, INT_MAX, .flags = V, .unit = "end" },

    { "hold", "Number of columns to hold at the start of the video", OFFSET(hold), AV_OPT_TYPE_INT,
        { .i64 = 0 }, 0, INT_MAX, .flags = V, .unit = "hold" },
    { "pad", "Number of columns to pad at the end of the video", OFFSET(pad), AV_OPT_TYPE_INT,
        { .i64 = 0 }, 0, INT_MAX, .flags = V, .unit = "pad" },

    { NULL },
};

AVFILTER_DEFINE_CLASS(tiltandshift);

static const AVFilterPad tiltandshift_inputs[] = {
    {
        .name         = "in",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tiltandshift_outputs[] = {
    {
        .name          = "out",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tiltandshift = {
    .name          = "tiltandshift",
    .description   = NULL_IF_CONFIG_SMALL("Generate a tilt-and-shift'd video."),
    .priv_size     = sizeof(TiltandshiftContext),
    .priv_class    = &tiltandshift_class,
    .uninit        = uninit,
    FILTER_INPUTS(tiltandshift_inputs),
    FILTER_OUTPUTS(tiltandshift_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
