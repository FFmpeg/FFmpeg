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

#include "dualinput.h"
#include "libavutil/timestamp.h"

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    FFDualInputContext *s = fs->opaque;
    AVFrame *mainpic = NULL, *secondpic = NULL;
    int ret = 0;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &mainpic,   1)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &secondpic, 0)) < 0) {
        av_frame_free(&mainpic);
        return ret;
    }
    av_assert0(mainpic);
    mainpic->pts = av_rescale_q(s->fs.pts, s->fs.time_base, ctx->outputs[0]->time_base);
    if (secondpic && !ctx->is_disabled)
        mainpic = s->process(ctx, mainpic, secondpic);
    ret = ff_filter_frame(ctx->outputs[0], mainpic);
    av_assert1(ret != AVERROR(EAGAIN));
    return ret;
}

int ff_dualinput_init(AVFilterContext *ctx, FFDualInputContext *s)
{
    FFFrameSyncIn *in;
    int ret;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    s->fs.opaque = s;
    s->fs.on_event = process_frame;
    in[0].time_base = ctx->inputs[0]->time_base;
    in[1].time_base = ctx->inputs[1]->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_NULL;
    in[1].after  = EXT_INFINITY;

    if (s->shortest)
        in[0].after = in[1].after = EXT_STOP;
    if (!s->repeatlast) {
        in[1].after = EXT_NULL;
        in[1].sync  = 0;
    }
    if (s->skip_initial_unpaired) {
        in[1].before = EXT_STOP;
    }

    return ff_framesync_configure(&s->fs);
}

int ff_dualinput_filter_frame(FFDualInputContext *s,
                                   AVFilterLink *inlink, AVFrame *in)
{
    return ff_framesync_filter_frame(&s->fs, inlink, in);
}

int ff_dualinput_request_frame(FFDualInputContext *s, AVFilterLink *outlink)
{
    return ff_framesync_request_frame(&s->fs, outlink);
}

void ff_dualinput_uninit(FFDualInputContext *s)
{
    ff_framesync_uninit(&s->fs);
}
