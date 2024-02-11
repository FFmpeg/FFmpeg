/*
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>

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

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "internal.h"
#include "video.h"
#include "yadif.h"

static int return_frame(AVFilterContext *ctx, int is_second)
{
    YADIFContext *yadif = ctx->priv;
    AVFilterLink *link  = ctx->outputs[0];
    int tff, ret;

    if (yadif->parity == -1) {
        tff = (yadif->cur->flags & AV_FRAME_FLAG_INTERLACED) ?
              !!(yadif->cur->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) : 1;
    } else {
        tff = yadif->parity ^ 1;
    }

    if (is_second) {
        yadif->out = ff_get_video_buffer(link, link->w, link->h);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        av_frame_copy_props(yadif->out, yadif->cur);
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        yadif->out->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        yadif->out->flags &= ~AV_FRAME_FLAG_INTERLACED;
        if (yadif->current_field == YADIF_FIELD_BACK_END)
            yadif->current_field = YADIF_FIELD_END;
    }

    yadif->filter(ctx, yadif->out, tff ^ !is_second, tff);

    if (is_second) {
        int64_t cur_pts  = yadif->cur->pts;
        int64_t next_pts = yadif->next->pts;

        if (next_pts != AV_NOPTS_VALUE && cur_pts != AV_NOPTS_VALUE) {
            yadif->out->pts = cur_pts + next_pts;
            if (yadif->pts_multiplier == 1) {
                yadif->out->pts >>= 1;
                yadif->out->duration >>= 1;
            }
        } else {
            yadif->out->pts = AV_NOPTS_VALUE;
        }
    }

    ff_ccfifo_inject(&yadif->cc_fifo, yadif->out);
    ret = ff_filter_frame(ctx->outputs[0], yadif->out);

    yadif->frame_pending = (yadif->mode&1) && !is_second;
    return ret;
}

static int checkstride(YADIFContext *yadif, const AVFrame *a, const AVFrame *b)
{
    int i;
    for (i = 0; i < yadif->csp->nb_components; i++)
        if (a->linesize[i] != b->linesize[i])
            return 1;
    return 0;
}

static void fixstride(AVFilterLink *link, AVFrame *f)
{
    AVFrame *dst = ff_default_get_video_buffer(link, f->width, f->height);
    if(!dst)
        return;
    av_frame_copy_props(dst, f);
    av_image_copy2(dst->data, dst->linesize,
                   f->data, f->linesize,
                   dst->format, dst->width, dst->height);
    av_frame_unref(f);
    av_frame_move_ref(f, dst);
    av_frame_free(&dst);
}

int ff_yadif_filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    av_assert0(frame);

    ff_ccfifo_extract(&yadif->cc_fifo, frame);

    if (yadif->frame_pending)
        return_frame(ctx, 1);

    if (yadif->prev)
        av_frame_free(&yadif->prev);
    yadif->prev = yadif->cur;
    yadif->cur  = yadif->next;
    yadif->next = frame;

    if (!yadif->cur) {
        yadif->cur = av_frame_clone(yadif->next);
        if (!yadif->cur)
            return AVERROR(ENOMEM);
        yadif->current_field = YADIF_FIELD_END;
    }

    if (checkstride(yadif, yadif->next, yadif->cur)) {
        av_log(ctx, AV_LOG_VERBOSE, "Reallocating frame due to differing stride\n");
        fixstride(link, yadif->next);
    }
    if (checkstride(yadif, yadif->next, yadif->cur))
        fixstride(link, yadif->cur);
    if (yadif->prev && checkstride(yadif, yadif->next, yadif->prev))
        fixstride(link, yadif->prev);
    if (checkstride(yadif, yadif->next, yadif->cur) || (yadif->prev && checkstride(yadif, yadif->next, yadif->prev))) {
        av_log(ctx, AV_LOG_ERROR, "Failed to reallocate frame\n");
        return -1;
    }

    if (!yadif->prev)
        return 0;

    if ((yadif->deint && !(yadif->cur->flags & AV_FRAME_FLAG_INTERLACED)) ||
        ctx->is_disabled ||
        (yadif->deint && !(yadif->prev->flags & AV_FRAME_FLAG_INTERLACED) && yadif->prev->repeat_pict) ||
        (yadif->deint && !(yadif->next->flags & AV_FRAME_FLAG_INTERLACED) && yadif->next->repeat_pict)
    ) {
        yadif->out  = av_frame_clone(yadif->cur);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        ff_ccfifo_inject(&yadif->cc_fifo, yadif->out);
        av_frame_free(&yadif->prev);
        if (yadif->out->pts != AV_NOPTS_VALUE)
            yadif->out->pts *= yadif->pts_multiplier;
        yadif->out->duration *= yadif->pts_multiplier;
        return ff_filter_frame(ctx->outputs[0], yadif->out);
    }

    yadif->out = ff_get_video_buffer(ctx->outputs[0], link->w, link->h);
    if (!yadif->out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(yadif->out, yadif->cur);
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    yadif->out->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    yadif->out->flags &= ~AV_FRAME_FLAG_INTERLACED;

    if (yadif->out->pts != AV_NOPTS_VALUE)
        yadif->out->pts *= yadif->pts_multiplier;
    if (!(yadif->mode & 1))
        yadif->out->duration *= yadif->pts_multiplier;
    else if (yadif->pts_multiplier == 1)
        yadif->out->duration >>= 1;

    return return_frame(ctx, 0);
}

int ff_yadif_request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    YADIFContext *yadif = ctx->priv;
    int ret;

    if (yadif->frame_pending) {
        return_frame(ctx, 1);
        return 0;
    }

    if (yadif->eof)
        return AVERROR_EOF;

    ret  = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && yadif->cur) {
        AVFrame *next = av_frame_clone(yadif->next);

        if (!next)
            return AVERROR(ENOMEM);

        yadif->current_field = YADIF_FIELD_BACK_END;
        next->pts = yadif->next->pts * 2 - yadif->cur->pts;

        ff_yadif_filter_frame(ctx->inputs[0], next);
        yadif->eof = 1;
    } else if (ret < 0) {
        return ret;
    }

    return 0;
}

int ff_yadif_config_output_common(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    YADIFContext *yadif = ctx->priv;
    AVRational tb = ctx->inputs[0]->time_base;
    int ret;

    if (av_reduce(&outlink->time_base.num, &outlink->time_base.den, tb.num, tb.den * 2LL, INT_MAX)) {
        yadif->pts_multiplier = 2;
    } else {
        av_log(ctx, AV_LOG_WARNING, "Cannot use exact output timebase\n");
        outlink->time_base = tb;
        yadif->pts_multiplier = 1;
    }

    outlink->w             = ctx->inputs[0]->w;
    outlink->h             = ctx->inputs[0]->h;

    if (outlink->w < 3 || outlink->h < 3) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 columns or lines is not supported\n");
        return AVERROR(EINVAL);
    }

    if(yadif->mode & 1)
        outlink->frame_rate = av_mul_q(ctx->inputs[0]->frame_rate,
                                    (AVRational){2, 1});
    else
        outlink->frame_rate = ctx->inputs[0]->frame_rate;

    ret = ff_ccfifo_init(&yadif->cc_fifo, outlink->frame_rate, ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failure to setup CC FIFO queue\n");
        return ret;
    }

    return 0;
}

void ff_yadif_uninit(AVFilterContext *ctx)
{
    YADIFContext *yadif = ctx->priv;

    av_frame_free(&yadif->prev);
    av_frame_free(&yadif->cur );
    av_frame_free(&yadif->next);
    ff_ccfifo_uninit(&yadif->cc_fifo);
}

#define OFFSET(x) offsetof(YADIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define CONST(name, help, val, u) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, .unit = u }

const AVOption ff_yadif_options[] = {
    { "mode",   "specify the interlacing mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=YADIF_MODE_SEND_FRAME}, 0, 3, FLAGS, .unit = "mode"},
    CONST("send_frame",           "send one frame for each frame",                                     YADIF_MODE_SEND_FRAME,           "mode"),
    CONST("send_field",           "send one frame for each field",                                     YADIF_MODE_SEND_FIELD,           "mode"),
    CONST("send_frame_nospatial", "send one frame for each frame, but skip spatial interlacing check", YADIF_MODE_SEND_FRAME_NOSPATIAL, "mode"),
    CONST("send_field_nospatial", "send one frame for each field, but skip spatial interlacing check", YADIF_MODE_SEND_FIELD_NOSPATIAL, "mode"),

    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=YADIF_PARITY_AUTO}, -1, 1, FLAGS, .unit = "parity" },
    CONST("tff",  "assume top field first",    YADIF_PARITY_TFF,  "parity"),
    CONST("bff",  "assume bottom field first", YADIF_PARITY_BFF,  "parity"),
    CONST("auto", "auto detect parity",        YADIF_PARITY_AUTO, "parity"),

    { "deint", "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=YADIF_DEINT_ALL}, 0, 1, FLAGS, .unit = "deint" },
    CONST("all",        "deinterlace all frames",                       YADIF_DEINT_ALL,         "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", YADIF_DEINT_INTERLACED,  "deint"),

    { NULL }
};
