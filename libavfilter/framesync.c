/*
 * Copyright (c) 2013 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "framesync.h"
#include "internal.h"

#define OFFSET(member) offsetof(FFFrameSync, member)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const char *framesync_name(void *ptr)
{
    return "framesync";
}

static const AVOption framesync_options[] = {
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, "eof_action" },
    { "shortest", "force termination when the shortest input terminates", OFFSET(opt_shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "repeatlast", "extend last frame of secondary streams beyond EOF", OFFSET(opt_repeatlast), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "ts_sync_mode", "How strictly to sync streams based on secondary input timestamps",
        OFFSET(opt_ts_sync_mode), AV_OPT_TYPE_INT, { .i64 = TS_DEFAULT },
        TS_DEFAULT, TS_NEAREST, .flags = FLAGS, "ts_sync_mode" },
        { "default", "Frame from secondary input with the nearest lower or equal timestamp to the primary input frame",
            0, AV_OPT_TYPE_CONST, { .i64 = TS_DEFAULT }, .flags = FLAGS, "ts_sync_mode" },
        { "nearest", "Frame from secondary input with the absolute nearest timestamp to the primary input frame",
            0, AV_OPT_TYPE_CONST, { .i64 = TS_NEAREST }, .flags = FLAGS, "ts_sync_mode" },
    { NULL }
};
static const AVClass framesync_class = {
    .version                   = LIBAVUTIL_VERSION_INT,
    .class_name                = "framesync",
    .item_name                 = framesync_name,
    .category                  = AV_CLASS_CATEGORY_FILTER,
    .option                    = framesync_options,
    .parent_log_context_offset = OFFSET(parent),
};

const AVClass *ff_framesync_child_class_iterate(void **iter)
{
    const AVClass *c = *iter ? NULL : &framesync_class;
    *iter = (void *)(uintptr_t)c;
    return c;
}

enum {
    STATE_BOF,
    STATE_RUN,
    STATE_EOF,
};

static int consume_from_fifos(FFFrameSync *fs);

void ff_framesync_preinit(FFFrameSync *fs)
{
    if (fs->class)
        return;
    fs->class  = &framesync_class;
    av_opt_set_defaults(fs);
}

int ff_framesync_init(FFFrameSync *fs, AVFilterContext *parent, unsigned nb_in)
{
    /* For filters with several outputs, we will not be able to assume which
       output is relevant for ff_outlink_frame_wanted() and
       ff_outlink_set_status(). To be designed when needed. */
    av_assert0(parent->nb_outputs == 1);

    ff_framesync_preinit(fs);
    fs->parent = parent;
    fs->nb_in  = nb_in;

    fs->in = av_calloc(nb_in, sizeof(*fs->in));
    if (!fs->in)
        return AVERROR(ENOMEM);
    return 0;
}

static void framesync_eof(FFFrameSync *fs)
{
    fs->eof = 1;
    fs->frame_ready = 0;
    ff_outlink_set_status(fs->parent->outputs[0], AVERROR_EOF, AV_NOPTS_VALUE);
}

static void framesync_sync_level_update(FFFrameSync *fs)
{
    unsigned i, level = 0;

    for (i = 0; i < fs->nb_in; i++)
        if (fs->in[i].state != STATE_EOF)
            level = FFMAX(level, fs->in[i].sync);
    av_assert0(level <= fs->sync_level);
    if (level < fs->sync_level)
        av_log(fs, AV_LOG_VERBOSE, "Sync level %u\n", level);
    if (fs->opt_ts_sync_mode > TS_DEFAULT) {
        for (i = 0; i < fs->nb_in; i++) {
            if (fs->in[i].sync < level)
                fs->in[i].ts_mode = fs->opt_ts_sync_mode;
            else
                fs->in[i].ts_mode = TS_DEFAULT;
        }
    }
    if (level)
        fs->sync_level = level;
    else
        framesync_eof(fs);
}

int ff_framesync_configure(FFFrameSync *fs)
{
    unsigned i;

    if (!fs->opt_repeatlast || fs->opt_eof_action == EOF_ACTION_PASS) {
        fs->opt_repeatlast = 0;
        fs->opt_eof_action = EOF_ACTION_PASS;
    }
    if (fs->opt_shortest || fs->opt_eof_action == EOF_ACTION_ENDALL) {
        fs->opt_shortest = 1;
        fs->opt_eof_action = EOF_ACTION_ENDALL;
    }
    if (!fs->opt_repeatlast) {
        for (i = 1; i < fs->nb_in; i++) {
            fs->in[i].after = EXT_NULL;
            fs->in[i].sync  = 0;
        }
    }
    if (fs->opt_shortest) {
        for (i = 0; i < fs->nb_in; i++)
            fs->in[i].after = EXT_STOP;
    }

    if (!fs->time_base.num) {
        for (i = 0; i < fs->nb_in; i++) {
            if (fs->in[i].sync) {
                if (fs->time_base.num) {
                    fs->time_base = av_gcd_q(fs->time_base, fs->in[i].time_base,
                                             AV_TIME_BASE / 2, AV_TIME_BASE_Q);
                } else {
                    fs->time_base = fs->in[i].time_base;
                }
            }
        }
        if (!fs->time_base.num) {
            av_log(fs, AV_LOG_ERROR, "Impossible to set time base\n");
            return AVERROR(EINVAL);
        }
        av_log(fs, AV_LOG_VERBOSE, "Selected %d/%d time base\n",
               fs->time_base.num, fs->time_base.den);
    }

    for (i = 0; i < fs->nb_in; i++)
        fs->in[i].pts = fs->in[i].pts_next = AV_NOPTS_VALUE;
    fs->sync_level = UINT_MAX;
    framesync_sync_level_update(fs);

    return 0;
}

static int framesync_advance(FFFrameSync *fs)
{
    unsigned i;
    int64_t pts;
    int ret;

    while (!(fs->frame_ready || fs->eof)) {
        ret = consume_from_fifos(fs);
        if (ret <= 0)
            return ret;

        pts = INT64_MAX;
        for (i = 0; i < fs->nb_in; i++)
            if (fs->in[i].have_next && fs->in[i].pts_next < pts)
                pts = fs->in[i].pts_next;
        if (pts == INT64_MAX) {
            framesync_eof(fs);
            break;
        }
        for (i = 0; i < fs->nb_in; i++) {
            if (fs->in[i].pts_next == pts ||
                (fs->in[i].ts_mode == TS_NEAREST &&
                 fs->in[i].have_next &&
                 fs->in[i].pts_next != INT64_MAX && fs->in[i].pts != AV_NOPTS_VALUE &&
                 fs->in[i].pts_next - pts < pts - fs->in[i].pts) ||
                (fs->in[i].before == EXT_INFINITY &&
                 fs->in[i].state == STATE_BOF)) {
                av_frame_free(&fs->in[i].frame);
                fs->in[i].frame      = fs->in[i].frame_next;
                fs->in[i].pts        = fs->in[i].pts_next;
                fs->in[i].frame_next = NULL;
                fs->in[i].pts_next   = AV_NOPTS_VALUE;
                fs->in[i].have_next  = 0;
                fs->in[i].state      = fs->in[i].frame ? STATE_RUN : STATE_EOF;
                if (fs->in[i].sync == fs->sync_level && fs->in[i].frame)
                    fs->frame_ready = 1;
                if (fs->in[i].state == STATE_EOF &&
                    fs->in[i].after == EXT_STOP)
                    framesync_eof(fs);
            }
        }
        if (fs->frame_ready)
            for (i = 0; i < fs->nb_in; i++)
                if ((fs->in[i].state == STATE_BOF &&
                     fs->in[i].before == EXT_STOP))
                    fs->frame_ready = 0;
        fs->pts = pts;
    }
    return 0;
}

static int64_t framesync_pts_extrapolate(FFFrameSync *fs, unsigned in,
                                         int64_t pts)
{
    /* Possible enhancement: use the link's frame rate */
    return pts + 1;
}

static void framesync_inject_frame(FFFrameSync *fs, unsigned in, AVFrame *frame)
{
    int64_t pts;

    av_assert0(!fs->in[in].have_next);
    av_assert0(frame);
    pts = av_rescale_q(frame->pts, fs->in[in].time_base, fs->time_base);
    frame->pts = pts;
    fs->in[in].frame_next = frame;
    fs->in[in].pts_next   = pts;
    fs->in[in].have_next  = 1;
}

static void framesync_inject_status(FFFrameSync *fs, unsigned in, int status, int64_t pts)
{
    av_assert0(!fs->in[in].have_next);
    pts = fs->in[in].state != STATE_RUN || fs->in[in].after == EXT_INFINITY
        ? INT64_MAX : framesync_pts_extrapolate(fs, in, fs->in[in].pts);
    fs->in[in].sync = 0;
    framesync_sync_level_update(fs);
    fs->in[in].frame_next = NULL;
    fs->in[in].pts_next   = pts;
    fs->in[in].have_next  = 1;
}

int ff_framesync_get_frame(FFFrameSync *fs, unsigned in, AVFrame **rframe,
                            unsigned get)
{
    AVFrame *frame;
    unsigned need_copy = 0, i;
    int64_t pts_next;
    int ret;

    if (!fs->in[in].frame) {
        *rframe = NULL;
        return 0;
    }
    frame = fs->in[in].frame;
    if (get) {
        /* Find out if we need to copy the frame: is there another sync
           stream, and do we know if its current frame will outlast this one? */
        pts_next = fs->in[in].have_next ? fs->in[in].pts_next : INT64_MAX;
        for (i = 0; i < fs->nb_in && !need_copy; i++)
            if (i != in && fs->in[i].sync &&
                (!fs->in[i].have_next || fs->in[i].pts_next < pts_next))
                need_copy = 1;
        if (need_copy) {
            if (!(frame = av_frame_clone(frame)))
                return AVERROR(ENOMEM);
            if ((ret = ff_inlink_make_frame_writable(fs->parent->inputs[in], &frame) < 0)) {
                av_frame_free(&frame);
                return ret;
            }
        } else {
            fs->in[in].frame = NULL;
        }
        fs->frame_ready = 0;
    }
    *rframe = frame;
    return 0;
}

void ff_framesync_uninit(FFFrameSync *fs)
{
    unsigned i;

    for (i = 0; i < fs->nb_in; i++) {
        av_frame_free(&fs->in[i].frame);
        av_frame_free(&fs->in[i].frame_next);
    }

    av_freep(&fs->in);
}

static int consume_from_fifos(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFrame *frame = NULL;
    int64_t pts;
    unsigned i, nb_active, nb_miss;
    int ret, status;

    nb_active = nb_miss = 0;
    for (i = 0; i < fs->nb_in; i++) {
        if (fs->in[i].have_next || fs->in[i].state == STATE_EOF)
            continue;
        nb_active++;
        ret = ff_inlink_consume_frame(ctx->inputs[i], &frame);
        if (ret < 0)
            return ret;
        if (ret) {
            av_assert0(frame);
            framesync_inject_frame(fs, i, frame);
        } else {
            ret = ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts);
            if (ret > 0) {
                framesync_inject_status(fs, i, status, pts);
            } else if (!ret) {
                nb_miss++;
            }
        }
    }
    if (nb_miss) {
        if (nb_miss == nb_active && !ff_outlink_frame_wanted(ctx->outputs[0]))
            return FFERROR_NOT_READY;
        for (i = 0; i < fs->nb_in; i++)
            if (!fs->in[i].have_next && fs->in[i].state != STATE_EOF)
                ff_inlink_request_frame(ctx->inputs[i]);
        return 0;
    }
    return 1;
}

int ff_framesync_activate(FFFrameSync *fs)
{
    int ret;

    ret = framesync_advance(fs);
    if (ret < 0)
        return ret;
    if (fs->eof || !fs->frame_ready)
        return 0;
    ret = fs->on_event(fs);
    if (ret < 0)
        return ret;
    fs->frame_ready = 0;

    return 0;
}

int ff_framesync_init_dualinput(FFFrameSync *fs, AVFilterContext *parent)
{
    int ret;

    ret = ff_framesync_init(fs, parent, 2);
    if (ret < 0)
        return ret;
    fs->in[0].time_base = parent->inputs[0]->time_base;
    fs->in[1].time_base = parent->inputs[1]->time_base;
    fs->in[0].sync   = 2;
    fs->in[0].before = EXT_STOP;
    fs->in[0].after  = EXT_INFINITY;
    fs->in[1].sync   = 1;
    fs->in[1].before = EXT_NULL;
    fs->in[1].after  = EXT_INFINITY;
    return 0;
}

int ff_framesync_dualinput_get(FFFrameSync *fs, AVFrame **f0, AVFrame **f1)
{
    AVFilterContext *ctx = fs->parent;
    AVFrame *mainpic = NULL, *secondpic = NULL;
    int ret;

    if ((ret = ff_framesync_get_frame(fs, 0, &mainpic,   1)) < 0 ||
        (ret = ff_framesync_get_frame(fs, 1, &secondpic, 0)) < 0) {
        av_frame_free(&mainpic);
        return ret;
    }
    av_assert0(mainpic);
    mainpic->pts = av_rescale_q(fs->pts, fs->time_base, ctx->outputs[0]->time_base);
    if (ctx->is_disabled)
        secondpic = NULL;
    *f0 = mainpic;
    *f1 = secondpic;
    return 0;
}

int ff_framesync_dualinput_get_writable(FFFrameSync *fs, AVFrame **f0, AVFrame **f1)
{
    int ret;

    ret = ff_framesync_dualinput_get(fs, f0, f1);
    if (ret < 0)
        return ret;
    ret = ff_inlink_make_frame_writable(fs->parent->inputs[0], f0);
    if (ret < 0) {
        av_frame_free(f0);
        *f1 = NULL;
        return ret;
    }
    return 0;
}
