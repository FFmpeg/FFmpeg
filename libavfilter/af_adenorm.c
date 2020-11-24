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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

enum FilterType {
    DC_TYPE,
    AC_TYPE,
    SQ_TYPE,
    PS_TYPE,
    NB_TYPES,
};

typedef struct ADenormContext {
    const AVClass *class;

    double level;
    double level_db;
    int type;
    int64_t in_samples;

    void (*filter)(AVFilterContext *ctx, void *dst,
                   const void *src, int nb_samples);
} ADenormContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static void dc_denorm_fltp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const float *src = (const float *)srcp;
    float *dst = (float *)dstp;
    const float dc = s->level;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc;
    }
}

static void dc_denorm_dblp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const double *src = (const double *)srcp;
    double *dst = (double *)dstp;
    const double dc = s->level;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc;
    }
}

static void ac_denorm_fltp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const float *src = (const float *)srcp;
    float *dst = (float *)dstp;
    const float dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * (((N + n) & 1) ? -1.f : 1.f);
    }
}

static void ac_denorm_dblp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const double *src = (const double *)srcp;
    double *dst = (double *)dstp;
    const double dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * (((N + n) & 1) ? -1. : 1.);
    }
}

static void sq_denorm_fltp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const float *src = (const float *)srcp;
    float *dst = (float *)dstp;
    const float dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * ((((N + n) >> 8) & 1) ? -1.f : 1.f);
    }
}

static void sq_denorm_dblp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const double *src = (const double *)srcp;
    double *dst = (double *)dstp;
    const double dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * ((((N + n) >> 8) & 1) ? -1. : 1.);
    }
}

static void ps_denorm_fltp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const float *src = (const float *)srcp;
    float *dst = (float *)dstp;
    const float dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * (((N + n) & 255) ? 0.f : 1.f);
    }
}

static void ps_denorm_dblp(AVFilterContext *ctx, void *dstp,
                           const void *srcp, int nb_samples)
{
    ADenormContext *s = ctx->priv;
    const double *src = (const double *)srcp;
    double *dst = (double *)dstp;
    const double dc = s->level;
    const int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = src[n] + dc * (((N + n) & 255) ? 0. : 1.);
    }
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ADenormContext *s = ctx->priv;

    switch (s->type) {
    case DC_TYPE:
        switch (outlink->format) {
        case AV_SAMPLE_FMT_FLTP: s->filter = dc_denorm_fltp; break;
        case AV_SAMPLE_FMT_DBLP: s->filter = dc_denorm_dblp; break;
        }
        break;
    case AC_TYPE:
        switch (outlink->format) {
        case AV_SAMPLE_FMT_FLTP: s->filter = ac_denorm_fltp; break;
        case AV_SAMPLE_FMT_DBLP: s->filter = ac_denorm_dblp; break;
        }
        break;
    case SQ_TYPE:
        switch (outlink->format) {
        case AV_SAMPLE_FMT_FLTP: s->filter = sq_denorm_fltp; break;
        case AV_SAMPLE_FMT_DBLP: s->filter = sq_denorm_dblp; break;
        }
        break;
    case PS_TYPE:
        switch (outlink->format) {
        case AV_SAMPLE_FMT_FLTP: s->filter = ps_denorm_fltp; break;
        case AV_SAMPLE_FMT_DBLP: s->filter = ps_denorm_dblp; break;
        }
        break;
    default:
        av_assert0(0);
    }

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ADenormContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const int start = (in->channels * jobnr) / nb_jobs;
    const int end = (in->channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++) {
        s->filter(ctx, out->extended_data[ch],
                  in->extended_data[ch],
                  in->nb_samples);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ADenormContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    s->level = exp(s->level_db / 20. * M_LN10);
    td.in = in; td.out = out;
    ctx->internal->execute(ctx, filter_channels, &td, NULL, FFMIN(inlink->channels,
                                                            ff_filter_get_nb_threads(ctx)));

    s->in_samples += in->nb_samples;

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_output(outlink);
}

static const AVFilterPad adenorm_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad adenorm_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ADenormContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption adenorm_options[] = {
    { "level", "set level", OFFSET(level_db), AV_OPT_TYPE_DOUBLE, {.dbl=-351},   -451,        -90, FLAGS },
    { "type",  "set type",  OFFSET(type),     AV_OPT_TYPE_INT,    {.i64=DC_TYPE},   0, NB_TYPES-1, FLAGS, "type" },
    { "dc",    NULL,  0, AV_OPT_TYPE_CONST, {.i64=DC_TYPE}, 0, 0, FLAGS, "type"},
    { "ac",    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AC_TYPE}, 0, 0, FLAGS, "type"},
    { "square",NULL,  0, AV_OPT_TYPE_CONST, {.i64=SQ_TYPE}, 0, 0, FLAGS, "type"},
    { "pulse", NULL,  0, AV_OPT_TYPE_CONST, {.i64=PS_TYPE}, 0, 0, FLAGS, "type"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(adenorm);

AVFilter ff_af_adenorm = {
    .name            = "adenorm",
    .description     = NULL_IF_CONFIG_SMALL("Remedy denormals by adding extremely low-level noise."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(ADenormContext),
    .inputs          = adenorm_inputs,
    .outputs         = adenorm_outputs,
    .priv_class      = &adenorm_class,
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                       AVFILTER_FLAG_SLICE_THREADS,
};
