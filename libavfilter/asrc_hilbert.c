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

#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"
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
    { "win_func", "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64=WFUNC_BLACKMAN}, 0, NB_WFUNC-1, FLAGS, "win_func" },
    { "w",        "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64=WFUNC_BLACKMAN}, 0, NB_WFUNC-1, FLAGS, "win_func" },
        { "rect",     "Rectangular",      0, AV_OPT_TYPE_CONST, {.i64=WFUNC_RECT},     0, 0, FLAGS, "win_func" },
        { "bartlett", "Bartlett",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BARTLETT}, 0, 0, FLAGS, "win_func" },
        { "hanning",  "Hanning",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, FLAGS, "win_func" },
        { "hamming",  "Hamming",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HAMMING},  0, 0, FLAGS, "win_func" },
        { "blackman", "Blackman",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BLACKMAN}, 0, 0, FLAGS, "win_func" },
        { "welch",    "Welch",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_WELCH},    0, 0, FLAGS, "win_func" },
        { "flattop",  "Flat-top",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_FLATTOP},  0, 0, FLAGS, "win_func" },
        { "bharris",  "Blackman-Harris",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHARRIS},  0, 0, FLAGS, "win_func" },
        { "bnuttall", "Blackman-Nuttall", 0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BNUTTALL}, 0, 0, FLAGS, "win_func" },
        { "bhann",    "Bartlett-Hann",    0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHANN},    0, 0, FLAGS, "win_func" },
        { "sine",     "Sine",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_SINE},     0, 0, FLAGS, "win_func" },
        { "nuttall",  "Nuttall",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_NUTTALL},  0, 0, FLAGS, "win_func" },
        { "lanczos",  "Lanczos",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_LANCZOS},  0, 0, FLAGS, "win_func" },
        { "gauss",    "Gauss",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_GAUSS},    0, 0, FLAGS, "win_func" },
        { "tukey",    "Tukey",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_TUKEY},    0, 0, FLAGS, "win_func" },
        { "dolph",    "Dolph-Chebyshev",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_DOLPH},    0, 0, FLAGS, "win_func" },
        { "cauchy",   "Cauchy",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_CAUCHY},   0, 0, FLAGS, "win_func" },
        { "parzen",   "Parzen",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_PARZEN},   0, 0, FLAGS, "win_func" },
        { "poisson",  "Poisson",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_POISSON},  0, 0, FLAGS, "win_func" },
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

    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats (ctx, formats);
    if (ret < 0)
        return ret;

    layouts = avfilter_make_format64_list(chlayouts);
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
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

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HilbertContext *s = ctx->priv;
    AVFrame *frame;
    int nb_samples;

    nb_samples = FFMIN(s->nb_samples, s->nb_taps - s->pts);
    if (!nb_samples)
        return AVERROR_EOF;

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
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_asrc_hilbert = {
    .name          = "hilbert",
    .description   = NULL_IF_CONFIG_SMALL("Generate a Hilbert transform FIR coefficients."),
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(HilbertContext),
    .inputs        = NULL,
    .outputs       = hilbert_outputs,
    .priv_class    = &hilbert_class,
};
