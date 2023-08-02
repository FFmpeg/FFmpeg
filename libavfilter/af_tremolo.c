/*
 * Copyright (c) 2015 Kyle Swanson <k@ylo.ph>.
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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

typedef struct TremoloContext {
    const AVClass *class;
    double freq;
    double depth;
    double *table;
    int table_size;
    int index;
} TremoloContext;

#define OFFSET(x) offsetof(TremoloContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption tremolo_options[] = {
    { "f", "set frequency in hertz",    OFFSET(freq),    AV_OPT_TYPE_DOUBLE,   {.dbl = 5.0},   0.1,   20000.0, FLAGS },
    { "d", "set depth as percentage",   OFFSET(depth),   AV_OPT_TYPE_DOUBLE,   {.dbl = 0.5},   0.0,   1.0,     FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tremolo);

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    TremoloContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const int channels = inlink->ch_layout.nb_channels;
    const int nb_samples = in->nb_samples;
    AVFrame *out;
    double *dst;
    int n, c;

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
    dst = (double *)out->data[0];

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++)
            dst[c] = src[c] * s->table[s->index];
        dst += channels;
        src += channels;
        s->index++;
        if (s->index >= s->table_size)
            s->index = 0;
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TremoloContext *s = ctx->priv;
    av_freep(&s->table);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    TremoloContext *s = ctx->priv;
    const double offset = 1. - s->depth / 2.;
    int i;

    s->table_size = lrint(inlink->sample_rate / s->freq + 0.5);
    s->table = av_malloc_array(s->table_size, sizeof(*s->table));
    if (!s->table)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->table_size; i++) {
        double env = s->freq * i / inlink->sample_rate;
        env = sin(2 * M_PI * fmod(env + 0.25, 1.0));
        s->table[i] = env * (1 - fabs(offset)) + offset;
    }

    s->index = 0;

    return 0;
}

static const AVFilterPad avfilter_af_tremolo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_tremolo = {
    .name          = "tremolo",
    .description   = NULL_IF_CONFIG_SMALL("Apply tremolo effect."),
    .priv_size     = sizeof(TremoloContext),
    .priv_class    = &tremolo_class,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_af_tremolo_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBL),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
