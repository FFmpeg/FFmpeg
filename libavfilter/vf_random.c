/*
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define MAX_FRAMES 512

typedef struct RandomContext {
    const AVClass *class;

    AVLFG lfg;
    int nb_frames;
    int64_t random_seed;
    int nb_frames_filled;
    AVFrame *frames[MAX_FRAMES];
    int64_t pts[MAX_FRAMES];
    int flush_idx;
} RandomContext;

#define OFFSET(x) offsetof(RandomContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption random_options[] = {
    { "frames", "set number of frames in cache", OFFSET(nb_frames),   AV_OPT_TYPE_INT,   {.i64=30},  2, MAX_FRAMES, FLAGS },
    { "seed",   "set the seed",                  OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64=-1}, -1, UINT32_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(random);

static av_cold int init(AVFilterContext *ctx)
{
    RandomContext *s = ctx->priv;
    uint32_t seed;

    if (s->random_seed < 0)
        s->random_seed = av_get_random_seed();
    seed = s->random_seed;
    av_lfg_init(&s->lfg, seed);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    RandomContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int idx;

    if (s->nb_frames_filled < s->nb_frames) {
        s->frames[s->nb_frames_filled] = in;
        s->pts[s->nb_frames_filled++] = in->pts;
        return 0;
    }

    idx = av_lfg_get(&s->lfg) % s->nb_frames;

    out = s->frames[idx];
    out->pts = s->pts[0];
    memmove(&s->pts[0], &s->pts[1], (s->nb_frames - 1) * sizeof(s->pts[0]));
    s->frames[idx] = in;
    s->pts[s->nb_frames - 1] = in->pts;

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    RandomContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

next:
    if (ret == AVERROR_EOF && !ctx->is_disabled && s->nb_frames > 0) {
        AVFrame *out = s->frames[s->nb_frames - 1];
        if (!out) {
            s->nb_frames--;
            goto next;
        }
        out->pts = s->pts[s->flush_idx++];
        ret = ff_filter_frame(outlink, out);
        s->frames[s->nb_frames - 1] = NULL;
        s->nb_frames--;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    RandomContext *s = ctx->priv;

    for (int i = 0; i < s->nb_frames; i++)
        av_frame_free(&s->frames[i]);
}

static const AVFilterPad random_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad random_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_random = {
    .name        = "random",
    .description = NULL_IF_CONFIG_SMALL("Return random frames."),
    .priv_size   = sizeof(RandomContext),
    .priv_class  = &random_class,
    .init        = init,
    .uninit      = uninit,
    FILTER_INPUTS(random_inputs),
    FILTER_OUTPUTS(random_outputs),
};
