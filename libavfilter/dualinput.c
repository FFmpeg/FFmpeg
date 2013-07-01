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

#define MAIN  0
#define SECOND 1

#include "dualinput.h"
#include "libavutil/timestamp.h"

static int try_filter_frame(FFDualInputContext *s,
                            AVFilterContext *ctx, AVFrame *mainpic)
{
    int ret;

    /* Discard obsolete second frames: if there is a next second frame with pts
     * before the main frame, we can drop the current second. */
    while (1) {
        AVFrame *next_overpic = ff_bufqueue_peek(&s->queue[SECOND], 0);
        if (!next_overpic && s->second_eof && !s->repeatlast) {
            av_frame_free(&s->second_frame);
            break;
        }
        if (!next_overpic || av_compare_ts(next_overpic->pts, ctx->inputs[SECOND]->time_base,
                                           mainpic->pts,      ctx->inputs[MAIN]->time_base) > 0)
            break;
        ff_bufqueue_get(&s->queue[SECOND]);
        av_frame_free(&s->second_frame);
        s->second_frame = next_overpic;
    }

    /* If there is no next frame and no EOF and the second frame is before
     * the main frame, we can not know yet if it will be superseded. */
    if (!s->queue[SECOND].available && !s->second_eof &&
        (!s->second_frame || av_compare_ts(s->second_frame->pts, ctx->inputs[SECOND]->time_base,
                                           mainpic->pts,         ctx->inputs[MAIN]->time_base) < 0))
        return AVERROR(EAGAIN);

    /* At this point, we know that the current second frame extends to the
     * time of the main frame. */
    av_dlog(ctx, "main_pts:%s main_pts_time:%s",
            av_ts2str(mainpic->pts), av_ts2timestr(mainpic->pts, &ctx->inputs[MAIN]->time_base));
    if (s->second_frame)
        av_dlog(ctx, " second_pts:%s second_pts_time:%s",
                av_ts2str(s->second_frame->pts), av_ts2timestr(s->second_frame->pts, &ctx->inputs[SECOND]->time_base));
    av_dlog(ctx, "\n");

    if (s->second_frame && !ctx->is_disabled)
        mainpic = s->process(ctx, mainpic, s->second_frame);
    ret = ff_filter_frame(ctx->outputs[0], mainpic);
    av_assert1(ret != AVERROR(EAGAIN));
    s->frame_requested = 0;
    return ret;
}

static int try_filter_next_frame(FFDualInputContext *s, AVFilterContext *ctx)
{
    AVFrame *next_mainpic = ff_bufqueue_peek(&s->queue[MAIN], 0);
    int ret;

    if (!next_mainpic)
        return AVERROR(EAGAIN);
    if ((ret = try_filter_frame(s, ctx, next_mainpic)) == AVERROR(EAGAIN))
        return ret;
    ff_bufqueue_get(&s->queue[MAIN]);
    return ret;
}

static int flush_frames(FFDualInputContext *s, AVFilterContext *ctx)
{
    int ret;

    while (!(ret = try_filter_next_frame(s, ctx)));
    return ret == AVERROR(EAGAIN) ? 0 : ret;
}

int ff_dualinput_filter_frame_main(FFDualInputContext *s,
                                   AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    int ret;

    if ((ret = flush_frames(s, ctx)) < 0)
        return ret;
    if ((ret = try_filter_frame(s, ctx, in)) < 0) {
        if (ret != AVERROR(EAGAIN))
            return ret;
        ff_bufqueue_add(ctx, &s->queue[MAIN], in);
    }

    if (!s->second_frame)
        return 0;
    flush_frames(s, ctx);

    return 0;
}

int ff_dualinput_filter_frame_second(FFDualInputContext *s,
                                     AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    int ret;

    if ((ret = flush_frames(s, ctx)) < 0)
        return ret;
    ff_bufqueue_add(ctx, &s->queue[SECOND], in);
    ret = try_filter_next_frame(s, ctx);
    return ret == AVERROR(EAGAIN) ? 0 : ret;
}

int ff_dualinput_request_frame(FFDualInputContext *s, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int input, ret;

    if (!try_filter_next_frame(s, ctx))
        return 0;
    s->frame_requested = 1;
    while (s->frame_requested) {
        /* TODO if we had a frame duration, we could guess more accurately */
        input = !s->second_eof && (s->queue[MAIN].available ||
                                   s->queue[SECOND].available < 2) ?
                SECOND : MAIN;
        ret = ff_request_frame(ctx->inputs[input]);
        /* EOF on main is reported immediately */
        if (ret == AVERROR_EOF && input == SECOND) {
            s->second_eof = 1;
            if (s->shortest)
                return ret;
            if ((ret = try_filter_next_frame(s, ctx)) != AVERROR(EAGAIN))
                return ret;
            ret = 0; /* continue requesting frames on main */
        }
        if (ret < 0)
            return ret;
    }
    return 0;
}

void ff_dualinput_uninit(FFDualInputContext *s)
{
    av_frame_free(&s->second_frame);
    ff_bufqueue_discard_all(&s->queue[MAIN]);
    ff_bufqueue_discard_all(&s->queue[SECOND]);
}
