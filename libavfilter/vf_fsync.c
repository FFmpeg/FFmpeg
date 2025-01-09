/*
 * Copyright (c) 2023 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * Filter for syncing video frames from external source
 *
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavformat/avio.h"
#include "video.h"
#include "filters.h"

#define BUF_SIZE 256

typedef struct FsyncContext {
    const AVClass *class;
    AVIOContext *avio_ctx; // reading the map file
    AVFrame *last_frame;   // buffering the last frame for duplicating eventually
    char *filename;        // user-specified map file
    char *buf;             // line buffer for the map file
    char *cur;             // current position in the line buffer
    char *end;             // end pointer of the line buffer
    int64_t ptsi;          // input pts to map to [0-N] output pts
    int64_t pts;           // output pts
    int tb_num;            // output timebase num
    int tb_den;            // output timebase den
} FsyncContext;

#define OFFSET(x) offsetof(FsyncContext, x)

static const AVOption fsync_options[] = {
    { "file",   "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags= AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { "f",      "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags= AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

/**
 * Fills the buffer from cur to end, add \0 at EOF
 */
static int buf_fill(FsyncContext *ctx)
{
    int ret;
    int num = ctx->end - ctx->cur;

    ret = avio_read(ctx->avio_ctx, ctx->cur, num);
    if (ret < 0)
        return ret;
    if (ret < num) {
        *(ctx->cur + ret) = '\0';
    }

    return ret;
}

/**
 * Copies cur to end to the beginning and fills the rest
 */
static int buf_reload(FsyncContext *ctx)
{
    int i, ret;
    int num = ctx->end - ctx->cur;

    for (i = 0; i < num; i++) {
        ctx->buf[i] = *ctx->cur++;
    }

    ctx->cur = ctx->buf + i;
    ret = buf_fill(ctx);
    if (ret < 0)
        return ret;
    ctx->cur = ctx->buf;

    return ret;
}

/**
 * Skip from cur over eol
 */
static void buf_skip_eol(FsyncContext *ctx)
{
    char *i;
    for (i = ctx->cur; i < ctx->end; i++) {
        if (*i != '\n')// && *i != '\r')
            break;
    }
    ctx->cur = i;
}

/**
 * Get number of bytes from cur until eol
 *
 * @return >= 0 in case of success,
 *         -1 in case there is no line ending before end of buffer
 */
static int buf_get_line_count(FsyncContext *ctx)
{
    int ret = 0;
    char *i;
    for (i = ctx->cur; i < ctx->end; i++, ret++) {
        if (*i == '\0' || *i == '\n')
            return ret;
    }

    return -1;
}

/**
 * Get number of bytes from cur to '\0'
 */
static int buf_get_zero(FsyncContext *ctx)
{
    return av_strnlen(ctx->cur, ctx->end - ctx->cur);
}

static int activate(AVFilterContext *ctx)
{
    FsyncContext *s       = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    int ret, line_count;
    AVFrame *frame;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    buf_skip_eol(s);
    line_count = buf_get_line_count(s);
    if (line_count < 0) {
        line_count = buf_reload(s);
        if (line_count < 0)
            return line_count;
        line_count = buf_get_line_count(s);
        if (line_count < 0)
            return line_count;
    }

    if (avio_feof(s->avio_ctx) && buf_get_zero(s) < 3) {
        av_log(ctx, AV_LOG_DEBUG, "End of file. To zero = %i\n", buf_get_zero(s));
        goto end;
    }

    if (s->last_frame) {
        ret = av_sscanf(s->cur, "%"PRId64" %"PRId64" %d/%d", &s->ptsi, &s->pts, &s->tb_num, &s->tb_den);
        if (ret != 4) {
            av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i / 4).\n", ret);
            ff_outlink_set_status(outlink, AVERROR_INVALIDDATA, AV_NOPTS_VALUE);
            return AVERROR_INVALIDDATA;
        }

        av_log(ctx, AV_LOG_DEBUG, "frame %"PRId64" ", s->last_frame->pts);

        if (s->last_frame->pts >= s->ptsi) {
            av_log(ctx, AV_LOG_DEBUG, ">= %"PRId64": DUP LAST with pts = %"PRId64"\n", s->ptsi, s->pts);

            // clone frame
            frame = av_frame_clone(s->last_frame);
            if (!frame) {
                ff_outlink_set_status(outlink, AVERROR(ENOMEM), AV_NOPTS_VALUE);
                return AVERROR(ENOMEM);
            }

            // set output pts and timebase
            frame->pts = s->pts;
            frame->time_base = av_make_q((int)s->tb_num, (int)s->tb_den);

            // advance cur to eol, skip over eol in the next call
            s->cur += line_count;

            // call again
            if (ff_inoutlink_check_flow(inlink, outlink))
                ff_filter_set_ready(ctx, 100);

            // filter frame
            return ff_filter_frame(outlink, frame);
        } else if (s->last_frame->pts < s->ptsi) {
            av_log(ctx, AV_LOG_DEBUG, "<  %"PRId64": DROP\n", s->ptsi);
            av_frame_free(&s->last_frame);

            // call again
            if (ff_inoutlink_check_flow(inlink, outlink))
                ff_filter_set_ready(ctx, 100);

            return 0;
        }
    }

end:
    if (s->last_frame)
        av_frame_free(&s->last_frame);

    ret = ff_inlink_consume_frame(inlink, &s->last_frame);
    if (ret < 0)
        return ret;

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int fsync_config_props(AVFilterLink* outlink)
{
    AVFilterContext *ctx = outlink->src;
    FilterLink      *l   = ff_filter_link(outlink);
    FsyncContext    *s   = ctx->priv;
    int ret;

    // read first line to get output timebase
    ret = av_sscanf(s->cur, "%"PRId64" %"PRId64" %d/%d", &s->ptsi, &s->pts, &s->tb_num, &s->tb_den);
    if (ret != 4) {
        av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i of 4).\n", ret);
        ff_outlink_set_status(outlink, AVERROR_INVALIDDATA, AV_NOPTS_VALUE);
        return AVERROR_INVALIDDATA;
    }

    l->frame_rate = av_make_q(1, 0); // unknown or dynamic
    outlink->time_base  = av_make_q(s->tb_num, s->tb_den);

    return 0;
}

static av_cold int fsync_init(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;
    int ret;

    av_log(ctx, AV_LOG_DEBUG, "filename: %s\n", s->filename);

    s->buf = av_malloc(BUF_SIZE + 1);
    if (!s->buf)
        return AVERROR(ENOMEM);

    ret = avio_open(&s->avio_ctx, s->filename, AVIO_FLAG_READ);
    if (ret < 0)
        return ret;

    s->cur = s->buf;
    s->end = s->buf + BUF_SIZE;
    s->buf[BUF_SIZE] = '\0';

    ret = buf_fill(s);
    if (ret < 0)
        return ret;


    return 0;
}

static av_cold void fsync_uninit(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;

    avio_closep(&s->avio_ctx);
    av_freep(&s->buf);
    av_frame_free(&s->last_frame);
}

AVFILTER_DEFINE_CLASS(fsync);

static const AVFilterPad fsync_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = fsync_config_props,
    },
};

const FFFilter ff_vf_fsync = {
    .p.name        = "fsync",
    .p.description = NULL_IF_CONFIG_SMALL("Synchronize video frames from external source."),
    .p.priv_class  = &fsync_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,
    .init          = fsync_init,
    .uninit        = fsync_uninit,
    .priv_size     = sizeof(FsyncContext),
    .activate      = activate,
    .formats_state = FF_FILTER_FORMATS_PASSTHROUGH,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(fsync_outputs),
};
