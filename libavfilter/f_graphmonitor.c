/*
 * Copyright (c) 2018 Paul B Mahol
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

#include "config_components.h"

#include "float.h"

#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct CacheItem {
    int64_t previous_pts_us;
} CacheItem;

typedef struct GraphMonitorContext {
    const AVClass *class;

    int w, h;
    float opacity;
    int mode;
    int flags;
    AVRational frame_rate;

    int64_t pts;
    int64_t next_pts;
    uint8_t white[4];
    uint8_t yellow[4];
    uint8_t red[4];
    uint8_t green[4];
    uint8_t blue[4];
    uint8_t bg[4];

    CacheItem *cache;
    unsigned int cache_size;
    unsigned int cache_index;
} GraphMonitorContext;

enum {
    MODE_QUEUE = 1 << 0,
    MODE_FCIN  = 1 << 1,
    MODE_FCOUT = 1 << 2,
    MODE_PTS   = 1 << 3,
    MODE_TIME  = 1 << 4,
    MODE_TB    = 1 << 5,
    MODE_FMT   = 1 << 6,
    MODE_SIZE  = 1 << 7,
    MODE_RATE  = 1 << 8,
    MODE_EOF   = 1 << 9,
    MODE_SCIN  = 1 << 10,
    MODE_SCOUT = 1 << 11,
    MODE_PTS_DELTA = 1 << 12,
    MODE_TIME_DELTA = 1 << 13,
    MODE_FC_DELTA = 1 << 14,
    MODE_SC_DELTA = 1 << 15,
};

#define OFFSET(x) offsetof(GraphMonitorContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption graphmonitor_options[] = {
    { "size", "set monitor size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, VF },
    { "s",    "set monitor size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, VF },
    { "opacity", "set video opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=.9}, 0, 1, VF },
    { "o",       "set video opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=.9}, 0, 1, VF },
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, VF, "mode" },
    { "m",    "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, VF, "mode" },
        { "full",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=0},   0, 0, VF, "mode" },
        { "compact",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1},   0, 0, VF, "mode" },
    { "flags", "set flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=MODE_QUEUE}, 0, INT_MAX, VF, "flags" },
    { "f",     "set flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=MODE_QUEUE}, 0, INT_MAX, VF, "flags" },
        { "queue",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_QUEUE},   0, 0, VF, "flags" },
        { "frame_count_in",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_FCOUT},   0, 0, VF, "flags" },
        { "frame_count_out",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_FCIN},    0, 0, VF, "flags" },
        { "frame_count_delta",NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_FC_DELTA},0, 0, VF, "flags" },
        { "pts",              NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_PTS},     0, 0, VF, "flags" },
        { "pts_delta",        NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_PTS_DELTA},0,0, VF, "flags" },
        { "time",             NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_TIME},    0, 0, VF, "flags" },
        { "time_delta",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_TIME_DELTA},0,0,VF, "flags" },
        { "timebase",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_TB},      0, 0, VF, "flags" },
        { "format",           NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_FMT},     0, 0, VF, "flags" },
        { "size",             NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_SIZE},    0, 0, VF, "flags" },
        { "rate",             NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_RATE},    0, 0, VF, "flags" },
        { "eof",              NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_EOF},     0, 0, VF, "flags" },
        { "sample_count_in",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_SCOUT},   0, 0, VF, "flags" },
        { "sample_count_out", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_SCIN},    0, 0, VF, "flags" },
        { "sample_count_delta",NULL,0, AV_OPT_TYPE_CONST, {.i64=MODE_SC_DELTA},0, 0, VF, "flags" },
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, VF },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, VF },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    GraphMonitorContext *s = ctx->priv;

    s->cache = av_fast_realloc(NULL, &s->cache_size,
                               8192 * sizeof(*(s->cache)));
    if (!s->cache)
        return AVERROR(ENOMEM);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_NONE
    };
    int ret;

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(fmts_list, &outlink->incfg.formats)) < 0)
        return ret;

    return 0;
}

static void clear_image(GraphMonitorContext *s, AVFrame *out, AVFilterLink *outlink)
{
    int bg = AV_RN32(s->bg);

    for (int i = 0; i < out->height; i++)
        for (int j = 0; j < out->width; j++)
            AV_WN32(out->data[0] + i * out->linesize[0] + j * 4, bg);
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt, uint8_t *color)
{
    const uint8_t *font;
    int font_height;
    int i;

    font = avpriv_cga_font,   font_height =  8;

    if (y + 8 >= pic->height ||
        x + strlen(txt) * 8 >= pic->width)
        return;

    for (i = 0; txt[i]; i++) {
        int char_y, mask;

        uint8_t *p = pic->data[0] + y*pic->linesize[0] + (x + i*8)*4;
        for (char_y = 0; char_y < font_height; char_y++) {
            for (mask = 0x80; mask; mask >>= 1) {
                if (font[txt[i] * font_height + char_y] & mask) {
                    p[0] = color[0];
                    p[1] = color[1];
                    p[2] = color[2];
                }
                p += 4;
            }
            p += pic->linesize[0] - 8 * 4;
        }
    }
}

static int filter_have_queued(AVFilterContext *filter)
{
    for (int j = 0; j < filter->nb_inputs; j++) {
        AVFilterLink *l = filter->inputs[j];
        size_t frames = ff_inlink_queued_frames(l);

        if (frames)
            return 1;
    }

    for (int j = 0; j < filter->nb_outputs; j++) {
        AVFilterLink *l = filter->outputs[j];
        size_t frames = ff_inlink_queued_frames(l);

        if (frames)
            return 1;
    }

    return 0;
}

static int draw_items(AVFilterContext *ctx, AVFrame *out,
                      int xpos, int ypos,
                      AVFilterLink *l,
                      size_t frames)
{
    GraphMonitorContext *s = ctx->priv;
    int64_t previous_pts_us = s->cache[s->cache_index].previous_pts_us;
    int64_t current_pts_us = l->current_pts_us;
    char buffer[1024] = { 0 };

    if (s->flags & MODE_FMT) {
        if (l->type == AVMEDIA_TYPE_VIDEO) {
            snprintf(buffer, sizeof(buffer)-1, " | format: %s",
                     av_get_pix_fmt_name(l->format));
        } else if (l->type == AVMEDIA_TYPE_AUDIO) {
            snprintf(buffer, sizeof(buffer)-1, " | format: %s",
                     av_get_sample_fmt_name(l->format));
        }
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_SIZE) {
        if (l->type == AVMEDIA_TYPE_VIDEO) {
            snprintf(buffer, sizeof(buffer)-1, " | size: %dx%d", l->w, l->h);
        } else if (l->type == AVMEDIA_TYPE_AUDIO) {
            snprintf(buffer, sizeof(buffer)-1, " | channels: %d", l->ch_layout.nb_channels);
        }
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_RATE) {
        if (l->type == AVMEDIA_TYPE_VIDEO) {
            snprintf(buffer, sizeof(buffer)-1, " | fps: %d/%d", l->frame_rate.num, l->frame_rate.den);
        } else if (l->type == AVMEDIA_TYPE_AUDIO) {
            snprintf(buffer, sizeof(buffer)-1, " | samplerate: %d", l->sample_rate);
        }
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_TB) {
        snprintf(buffer, sizeof(buffer)-1, " | tb: %d/%d", l->time_base.num, l->time_base.den);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_QUEUE) {
        snprintf(buffer, sizeof(buffer)-1, " | queue: ");
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
        snprintf(buffer, sizeof(buffer)-1, "%"SIZE_SPECIFIER, frames);
        drawtext(out, xpos, ypos, buffer, frames > 0 ? frames >= 10 ? frames >= 50 ? s->red : s->yellow : s->green : s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_FCIN) {
        snprintf(buffer, sizeof(buffer)-1, " | in: %"PRId64, l->frame_count_in);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_FCOUT) {
        snprintf(buffer, sizeof(buffer)-1, " | out: %"PRId64, l->frame_count_out);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_FC_DELTA) {
        snprintf(buffer, sizeof(buffer)-1, " | delta: %"PRId64, l->frame_count_in - l->frame_count_out);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_SCIN) {
        snprintf(buffer, sizeof(buffer)-1, " | sin: %"PRId64, l->sample_count_in);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_SCOUT) {
        snprintf(buffer, sizeof(buffer)-1, " | sout: %"PRId64, l->sample_count_out);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_SC_DELTA) {
        snprintf(buffer, sizeof(buffer)-1, " | sdelta: %"PRId64, l->sample_count_in - l->sample_count_out);
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_PTS) {
        snprintf(buffer, sizeof(buffer)-1, " | pts: %s", av_ts2str(current_pts_us));
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_PTS_DELTA) {
        snprintf(buffer, sizeof(buffer)-1, " | pts_delta: %s", av_ts2str(current_pts_us - previous_pts_us));
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_TIME) {
        snprintf(buffer, sizeof(buffer)-1, " | time: %s", av_ts2timestr(current_pts_us, &AV_TIME_BASE_Q));
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_TIME_DELTA) {
        snprintf(buffer, sizeof(buffer)-1, " | time_delta: %s", av_ts2timestr(current_pts_us - previous_pts_us, &AV_TIME_BASE_Q));
        drawtext(out, xpos, ypos, buffer, s->white);
        xpos += strlen(buffer) * 8;
    }
    if (s->flags & MODE_EOF && ff_outlink_get_status(l)) {
        snprintf(buffer, sizeof(buffer)-1, " | eof");
        drawtext(out, xpos, ypos, buffer, s->blue);
        xpos += strlen(buffer) * 8;
    }

    s->cache[s->cache_index].previous_pts_us = l->current_pts_us;

    if (s->cache_index + 1 >= s->cache_size / sizeof(*(s->cache))) {
        void *ptr = av_fast_realloc(s->cache, &s->cache_size, s->cache_size * 2);

        if (!ptr)
            return AVERROR(ENOMEM);
        s->cache = ptr;
    }
    s->cache_index++;

    return 0;
}

static int create_frame(AVFilterContext *ctx, int64_t pts)
{
    GraphMonitorContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int ret, xpos, ypos = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    clear_image(s, out, outlink);

    s->cache_index = 0;

    for (int i = 0; i < ctx->graph->nb_filters; i++) {
        AVFilterContext *filter = ctx->graph->filters[i];
        char buffer[1024] = { 0 };

        if (s->mode && !filter_have_queued(filter))
            continue;

        xpos = 0;
        drawtext(out, xpos, ypos, filter->name, s->white);
        xpos += strlen(filter->name) * 8 + 10;
        drawtext(out, xpos, ypos, filter->filter->name, s->white);
        ypos += 10;
        for (int j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink *l = filter->inputs[j];
            size_t frames = ff_inlink_queued_frames(l);

            if (s->mode && !frames)
                continue;

            xpos = 10;
            snprintf(buffer, sizeof(buffer)-1, "in%d: ", j);
            drawtext(out, xpos, ypos, buffer, s->white);
            xpos += strlen(buffer) * 8;
            drawtext(out, xpos, ypos, l->src->name, s->white);
            xpos += strlen(l->src->name) * 8 + 10;
            ret = draw_items(ctx, out, xpos, ypos, l, frames);
            if (ret < 0)
                goto error;
            ypos += 10;
        }

        ypos += 2;
        for (int j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink *l = filter->outputs[j];
            size_t frames = ff_inlink_queued_frames(l);

            if (s->mode && !frames)
                continue;

            xpos = 10;
            snprintf(buffer, sizeof(buffer)-1, "out%d: ", j);
            drawtext(out, xpos, ypos, buffer, s->white);
            xpos += strlen(buffer) * 8;
            drawtext(out, xpos, ypos, l->dst->name, s->white);
            xpos += strlen(l->dst->name) * 8 + 10;
            ret = draw_items(ctx, out, xpos, ypos, l, frames);
            if (ret < 0)
                goto error;
            ypos += 10;
        }
        ypos += 5;
    }

    out->pts = pts;
    s->pts = pts + 1;
    return ff_filter_frame(outlink, out);
error:
    av_frame_free(&out);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    GraphMonitorContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    int64_t pts = AV_NOPTS_VALUE;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (ff_inlink_queued_frames(inlink)) {
        AVFrame *frame = NULL;
        int ret;

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            pts = frame->pts;
            av_frame_free(&frame);
        }
    }

    if (pts != AV_NOPTS_VALUE) {
        pts = av_rescale_q(pts, inlink->time_base, outlink->time_base);
        if (s->pts == AV_NOPTS_VALUE)
            s->pts = pts;
        s->next_pts = pts;
    }

    if (s->pts < s->next_pts && ff_outlink_frame_wanted(outlink))
        return create_frame(ctx, s->pts);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_output(AVFilterLink *outlink)
{
    GraphMonitorContext *s = outlink->src->priv;

    s->bg[3] = 255 * s->opacity;
    s->white[0] = s->white[1] = s->white[2] = 255;
    s->yellow[0] = s->yellow[1] = 255;
    s->red[0] = 255;
    s->green[1] = 255;
    s->blue[2] = 255;
    s->pts = AV_NOPTS_VALUE;
    s->next_pts = AV_NOPTS_VALUE;
    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(s->frame_rate);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GraphMonitorContext *s = ctx->priv;

    av_freep(&s->cache);
    s->cache_size = s->cache_index = 0;
}

AVFILTER_DEFINE_CLASS_EXT(graphmonitor, "(a)graphmonitor", graphmonitor_options);

#if CONFIG_GRAPHMONITOR_FILTER

static const AVFilterPad graphmonitor_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad graphmonitor_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_vf_graphmonitor = {
    .name          = "graphmonitor",
    .description   = NULL_IF_CONFIG_SMALL("Show various filtergraph stats."),
    .priv_size     = sizeof(GraphMonitorContext),
    .priv_class    = &graphmonitor_class,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(graphmonitor_inputs),
    FILTER_OUTPUTS(graphmonitor_outputs),
    FILTER_QUERY_FUNC(query_formats),
};

#endif // CONFIG_GRAPHMONITOR_FILTER

#if CONFIG_AGRAPHMONITOR_FILTER

static const AVFilterPad agraphmonitor_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad agraphmonitor_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_avf_agraphmonitor = {
    .name          = "agraphmonitor",
    .description   = NULL_IF_CONFIG_SMALL("Show various filtergraph stats."),
    .priv_class    = &graphmonitor_class,
    .priv_size     = sizeof(GraphMonitorContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(agraphmonitor_inputs),
    FILTER_OUTPUTS(agraphmonitor_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
#endif // CONFIG_AGRAPHMONITOR_FILTER
