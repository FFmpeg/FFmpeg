/*
 * Copyright (c) 2018 Paul B Mahol
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

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"
#include "filters.h"
#include "window_func.h"

typedef struct HilbertContext {
    const AVClass *class;

    int sample_rate;
    int nb_taps;
    int nb_samples;
    int win_func;

    float *taps;
    int64_t pts;
} HilbertContext;

#define OFFSET(x) offsetof(HilbertContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption hilbert_options[] = {
    { "sample_rate", "set sample rate",    OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100},  1, INT_MAX,    FLAGS },
    { "r",           "set sample rate",    OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100},  1, INT_MAX,    FLAGS },
    { "taps",        "set number of taps", OFFSET(nb_taps),     AV_OPT_TYPE_INT, {.i64=22051}, 11, UINT16_MAX, FLAGS },
    { "t",           "set number of taps", OFFSET(nb_taps),     AV_OPT_TYPE_INT, {.i64=22051}, 11, UINT16_MAX, FLAGS },
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, INT_MAX, FLAGS },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, INT_MAX, FLAGS },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), FLAGS, WFUNC_BLACKMAN),
    WIN_FUNC_OPTION("w",        OFFSET(win_func), FLAGS, WFUNC_BLACKMAN),
    {NULL}
};

AVFILTER_DEFINE_CLASS(hilbert);

static av_cold int init(AVFilterContext *ctx)
{
    HilbertContext *s = ctx->priv;

    if (!(s->nb_taps & 1)) {
        av_log(s, AV_LOG_ERROR, "Number of taps %d must be odd length.\n", s->nb_taps);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HilbertContext *s = ctx->priv;

    av_freep(&s->taps);
}

static av_cold int query_formats(AVFilterContext *ctx)
{
    HilbertContext *s = ctx->priv;
    static const int64_t chlayouts[] = { AV_CH_LAYOUT_MONO, -1 };
    int sample_rates[] = { s->sample_rate, -1 };
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_NONE
    };
    int ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list(ctx, chlayouts);
    if (ret < 0)
        return ret;

    return ff_set_common_samplerates_from_list(ctx, sample_rates);
}

static av_cold int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HilbertContext *s = ctx->priv;
    float overlap;
    int i;

    s->taps = av_malloc_array(s->nb_taps, sizeof(*s->taps));
    if (!s->taps)
        return AVERROR(ENOMEM);

    generate_window_func(s->taps, s->nb_taps, s->win_func, &overlap);

    for (i = 0; i < s->nb_taps; i++) {
        int k = -(s->nb_taps / 2) + i;

        if (k & 1) {
            float pk = M_PI * k;

            s->taps[i] *= (1.f - cosf(pk)) / pk;
        } else {
            s->taps[i] = 0.f;
        }
    }

    s->pts = 0;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    HilbertContext *s = ctx->priv;
    AVFrame *frame;
    int nb_samples;

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    nb_samples = FFMIN(s->nb_samples, s->nb_taps - s->pts);
    if (nb_samples <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!(frame = ff_get_audio_buffer(outlink, nb_samples)))
        return AVERROR(ENOMEM);

    memcpy(frame->data[0], s->taps + s->pts, nb_samples * sizeof(float));

    frame->pts = s->pts;
    s->pts    += nb_samples;
    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad hilbert_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
    },
};

const AVFilter ff_asrc_hilbert = {
    .name          = "hilbert",
    .description   = NULL_IF_CONFIG_SMALL("Generate a Hilbert transform FIR coefficients."),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(HilbertContext),
    .inputs        = NULL,
    FILTER_OUTPUTS(hilbert_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &hilbert_class,
};
