/*
 * Copyright (c) 2013 Clément Bœsch
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
#include "audio.h"
#include "video.h"

enum mode {
    MODE_NONE,
    MODE_RO,
    MODE_RW,
    MODE_TOGGLE,
    MODE_RANDOM,
    NB_MODES
};

typedef struct {
    const AVClass *class;
    AVLFG lfg;
    int64_t random_seed;
    int mode;
} PermsContext;

#define OFFSET(x) offsetof(PermsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    { "mode", "select permissions mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_NONE}, MODE_NONE, NB_MODES-1, FLAGS, "mode" },
        { "none",   "do nothing",                       0, AV_OPT_TYPE_CONST, {.i64 = MODE_NONE},       INT_MIN, INT_MAX, FLAGS, "mode" },
        { "ro",     "set all output frames read-only",  0, AV_OPT_TYPE_CONST, {.i64 = MODE_RO},         INT_MIN, INT_MAX, FLAGS, "mode" },
        { "rw",     "set all output frames writable",   0, AV_OPT_TYPE_CONST, {.i64 = MODE_RW},         INT_MIN, INT_MAX, FLAGS, "mode" },
        { "toggle", "switch permissions",               0, AV_OPT_TYPE_CONST, {.i64 = MODE_TOGGLE},     INT_MIN, INT_MAX, FLAGS, "mode" },
        { "random", "set permissions randomly",         0, AV_OPT_TYPE_CONST, {.i64 = MODE_RANDOM},     INT_MIN, INT_MAX, FLAGS, "mode" },
    { "seed", "set the seed for the random mode", OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    PermsContext *s = ctx->priv;

    if (s->mode == MODE_RANDOM) {
        uint32_t seed;

        if (s->random_seed == -1)
            s->random_seed = av_get_random_seed();
        seed = s->random_seed;
        av_log(ctx, AV_LOG_INFO, "random seed: 0x%08x\n", seed);
        av_lfg_init(&s->lfg, seed);
    }

    return 0;
}

enum perm                        {  RO,   RW  };
static const char * const perm_str[2] = { "RO", "RW" };

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    int ret;
    AVFilterContext *ctx = inlink->dst;
    PermsContext *s = ctx->priv;
    AVFrame *out = frame;
    enum perm in_perm = av_frame_is_writable(frame) ? RW : RO;
    enum perm out_perm;

    switch (s->mode) {
    case MODE_TOGGLE:   out_perm = in_perm == RO ? RW : RO;                 break;
    case MODE_RANDOM:   out_perm = av_lfg_get(&s->lfg) & 1 ? RW : RO;       break;
    case MODE_RO:       out_perm = RO;                                      break;
    case MODE_RW:       out_perm = RW;                                      break;
    default:            out_perm = in_perm;                                 break;
    }

    av_log(ctx, AV_LOG_VERBOSE, "%s -> %s%s\n",
           perm_str[in_perm], perm_str[out_perm],
           in_perm == out_perm ? " (no-op)" : "");

    if (in_perm == RO && out_perm == RW) {
        if ((ret = av_frame_make_writable(frame)) < 0)
            return ret;
    } else if (in_perm == RW && out_perm == RO) {
        out = av_frame_clone(frame);
        if (!out)
            return AVERROR(ENOMEM);
    }

    ret = ff_filter_frame(ctx->outputs[0], out);

    if (in_perm == RW && out_perm == RO)
        av_frame_free(&frame);
    return ret;
}

#if CONFIG_APERMS_FILTER

#define aperms_options options
AVFILTER_DEFINE_CLASS(aperms);

static const AVFilterPad aperms_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aperms_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_aperms = {
    .name        = "aperms",
    .description = NULL_IF_CONFIG_SMALL("Set permissions for the output audio frame."),
    .init        = init,
    .priv_size   = sizeof(PermsContext),
    .inputs      = aperms_inputs,
    .outputs     = aperms_outputs,
    .priv_class  = &aperms_class,
};
#endif /* CONFIG_APERMS_FILTER */

#if CONFIG_PERMS_FILTER

#define perms_options options
AVFILTER_DEFINE_CLASS(perms);

static const AVFilterPad perms_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad perms_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_perms = {
    .name        = "perms",
    .description = NULL_IF_CONFIG_SMALL("Set permissions for the output video frame."),
    .init        = init,
    .priv_size   = sizeof(PermsContext),
    .inputs      = perms_inputs,
    .outputs     = perms_outputs,
    .priv_class  = &perms_class,
};
#endif /* CONFIG_PERMS_FILTER */
