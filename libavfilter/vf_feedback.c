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
 * feedback video filter
 */

#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/internal.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct FeedbackContext {
    const AVClass *class;

    int x, y;
    int w, h;

    int max_step[4];
    int hsub, vsub;

    AVFrame *feed;

    AVFifo *fifo;
} FeedbackContext;

static void adjust_pos(AVFilterContext *ctx, FeedbackContext *s)
{
    if (s->x + s->w > ctx->inputs[0]->w)
        s->x = ctx->inputs[0]->w - s->w;
    if (s->y + s->h > ctx->inputs[0]->h)
        s->y = ctx->inputs[0]->h - s->h;
}

static void adjust_parameters(AVFilterContext *ctx, FeedbackContext *s)
{
    if (s->x >= ctx->inputs[0]->w)
        s->x = 0;
    if (s->y >= ctx->inputs[0]->h)
        s->y = 0;

    if (s->w <= 0)
        s->w = ctx->inputs[0]->w - s->x;
    if (s->h <= 0)
        s->h = ctx->inputs[0]->h - s->y;

    if (s->w > ctx->inputs[0]->w)
        s->w = ctx->inputs[0]->w;
    if (s->h > ctx->inputs[0]->h)
        s->h = ctx->inputs[0]->h;

    adjust_pos(ctx, s);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    FeedbackContext *s = ctx->priv;

    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;

    av_image_fill_max_pixsteps(s->max_step, NULL, pix_desc);

    adjust_parameters(ctx, s);

    ctx->inputs[1]->w = s->w;
    ctx->inputs[1]->h = s->h;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FeedbackContext *s = ctx->priv;

    adjust_parameters(ctx, s);

    ctx->outputs[0]->w = ctx->inputs[0]->w;
    ctx->outputs[0]->h = ctx->inputs[0]->h;
    ctx->outputs[1]->w = s->w;
    ctx->outputs[1]->h = s->h;

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_formats_pixdesc_filter(0, AV_PIX_FMT_FLAG_BITSTREAM |
                                                               AV_PIX_FMT_FLAG_HWACCEL |
                                                               AV_PIX_FMT_FLAG_PAL));
}

static int activate(AVFilterContext *ctx)
{
    FeedbackContext *s = ctx->priv;
    int status, ret;
    int64_t pts;

    adjust_pos(ctx, s);

    for (int i = 0; i < ctx->nb_outputs; i++)
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[i], ctx);

    if (!s->feed) {
        ret = ff_inlink_consume_frame(ctx->inputs[1], &s->feed);
        if (ret < 0)
            return ret;
    }

    if (s->feed && av_fifo_can_read(s->fifo)) {
        AVFrame *src = s->feed;
        AVFrame *dst = NULL;

        av_fifo_read(s->fifo, &dst, 1);
        if (!dst)
            return AVERROR_BUG;

        if (!av_frame_is_writable(dst)) {
            AVFrame *tmp = ff_get_video_buffer(ctx->outputs[0], ctx->outputs[0]->w, ctx->outputs[0]->h);

            if (!tmp) {
                av_frame_free(&dst);
                return AVERROR(ENOMEM);
            }

            ret = av_frame_copy(tmp, dst);
            if (ret < 0) {
                av_frame_free(&dst);
                av_frame_free(&tmp);
                return ret;
            }

            av_frame_copy_props(tmp, dst);
            av_frame_free(&dst);
            dst = tmp;
        }

        for (int y = 0; y < src->height; y++) {
            memmove(dst->data[0] + (s->y + y) * dst->linesize[0] + s->x * s->max_step[0],
                    src->data[0] + y * src->linesize[0], src->width * s->max_step[0]);
        }

        for (int i = 1; i < 3; i++) {
            if (dst->data[i]) {
                for (int y = 0; y < src->height; y++) {
                    memmove(dst->data[i] + ((s->y + y) >> s->vsub) * dst->linesize[i] + (s->x >> s->hsub) * s->max_step[i],
                            src->data[i] + (y >> s->vsub) * src->linesize[i], (src->width >> s->hsub) * s->max_step[i]);
                }
            }
        }

        if (dst->data[3]) {
            for (int y = 0; y < src->height; y++) {
                memmove(dst->data[3] + (s->y + y) * dst->linesize[3] + s->x * s->max_step[3],
                        src->data[3] + y * src->linesize[3], src->width * s->max_step[3]);
            }
        }

        ret = ff_filter_frame(ctx->outputs[0], dst);
        av_frame_free(&s->feed);
        return ret;
    }

    if (!s->feed || ctx->is_disabled) {
        AVFrame *in = NULL;

        ret = ff_inlink_consume_frame(ctx->inputs[0], &in);
        if (ret < 0)
            return ret;

        if (ret > 0 && ctx->is_disabled)
            return ff_filter_frame(ctx->outputs[0], in);

        if (ret > 0) {
            AVFrame *frame;

            ret = av_fifo_write(s->fifo, &in, 1);
            if (ret < 0) {
                av_frame_free(&in);
                return ret;
            }

            frame = av_frame_clone(in);
            if (!frame)
                return AVERROR(ENOMEM);

            frame->width  = s->w;
            frame->height = s->h;

            frame->data[0] += s->y * frame->linesize[0];
            frame->data[0] += s->x * s->max_step[0];

            for (int i = 1; i < 3; i ++) {
                if (frame->data[i]) {
                    frame->data[i] += (s->y >> s->vsub) * frame->linesize[i];
                    frame->data[i] += (s->x >> s->hsub) * s->max_step[i];
                }
            }

            if (frame->data[3]) {
                frame->data[3] += s->y * frame->linesize[3];
                frame->data[3] += s->x * s->max_step[3];
            }

            return ff_filter_frame(ctx->outputs[1], frame);
        }
    }

    if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
        ff_outlink_set_status(ctx->outputs[0], status, pts);
        ff_outlink_set_status(ctx->outputs[1], status, pts);
        return 0;
    }

    if (ff_inlink_acknowledge_status(ctx->inputs[1], &status, &pts)) {
        ff_outlink_set_status(ctx->outputs[0], status, pts);
        ff_outlink_set_status(ctx->outputs[1], status, pts);
        return 0;
    }

    if (!s->feed || ctx->is_disabled) {
        if (ff_outlink_frame_wanted(ctx->outputs[0])) {
            ff_inlink_request_frame(ctx->inputs[0]);
            if (!ctx->is_disabled)
                ff_inlink_request_frame(ctx->inputs[1]);
            return 0;
        }
    }

    return FFERROR_NOT_READY;
}

static av_cold int init(AVFilterContext *ctx)
{
    FeedbackContext *s = ctx->priv;

    s->fifo = av_fifo_alloc2(8, sizeof(AVFrame *), AV_FIFO_FLAG_AUTO_GROW);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FeedbackContext *s = ctx->priv;
    if (s->fifo) {
        size_t size = av_fifo_can_read(s->fifo);

        for (size_t n = 0; n < size; n++) {
            AVFrame *frame = NULL;

            av_fifo_read(s->fifo, &frame, 1);

            av_frame_free(&frame);
        }

        av_fifo_freep2(&s->fifo);
    }
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "feedin",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    {
        .name         = "feedout",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

#define OFFSET(x) offsetof(FeedbackContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
#define TFLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM)

static const AVOption feedback_options[] = {
    { "x", "set top left crop position", OFFSET(x), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, TFLAGS },
    { "y", "set top left crop position", OFFSET(y), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, TFLAGS },
    { "w", "set crop size",              OFFSET(w), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "h", "set crop size",              OFFSET(h), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(feedback);

const FFFilter ff_vf_feedback = {
    .p.name        = "feedback",
    .p.description = NULL_IF_CONFIG_SMALL("Apply feedback video filter."),
    .p.priv_class  = &feedback_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size   = sizeof(FeedbackContext),
    .activate    = activate,
    .init        = init,
    .uninit      = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = ff_filter_process_command,
};
