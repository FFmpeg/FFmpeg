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
#include "generate_wave_table.h"

typedef struct VibratoContext {
    const AVClass *class;
    double freq;
    double depth;
    int channels;

    double **buf;
    int buf_index;
    int buf_size;

    double *wave_table;
    int wave_table_index;
    int wave_table_size;
} VibratoContext;

#define OFFSET(x) offsetof(VibratoContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption vibrato_options[] = {
    { "f", "set frequency in hertz",    OFFSET(freq),    AV_OPT_TYPE_DOUBLE,   {.dbl = 5.0},   0.1,   20000.0, FLAGS },
    { "d", "set depth as percentage",   OFFSET(depth),   AV_OPT_TYPE_DOUBLE,   {.dbl = 0.5},   0.00,  1.0,     FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vibrato);

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    VibratoContext *s = ctx->priv;
    const int wave_table_size = s->wave_table_size;
    const double *wave_table = s->wave_table;
    AVFilterLink *outlink = ctx->outputs[0];
    const int channels = s->channels;
    const int buf_size = s->buf_size;
    const double depth = s->depth;
    int wave_table_index = s->wave_table_index;
    int buf_index = s->buf_index;
    AVFrame *out;
    const double *src;
    double *dst;

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

    for (int n = 0; n < in->nb_samples; n++) {
        int samp1_index, samp2_index;
        double integer, decimal;
        decimal = modf(depth * wave_table[wave_table_index], &integer);

        wave_table_index++;
        if (wave_table_index >= wave_table_size)
            wave_table_index -= wave_table_size;

        samp1_index = buf_index + integer;
        if (samp1_index >= buf_size)
            samp1_index -= buf_size;
        samp2_index = samp1_index + 1;
        if (samp2_index >= buf_size)
            samp2_index -= buf_size;

        for (int c = 0; c < channels; c++) {
            double *buf, this_samp;

            src = (const double *)in->extended_data[c];
            dst = (double *)out->extended_data[c];
            buf = s->buf[c];

            this_samp = src[n];
            dst[n] = buf[samp1_index] + (decimal * (buf[samp2_index] - buf[samp1_index]));
            buf[buf_index] = this_samp;
        }
        buf_index++;
        if (buf_index >= buf_size)
            buf_index -= buf_size;
    }

    s->wave_table_index = wave_table_index;
    s->buf_index = buf_index;
    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VibratoContext *s = ctx->priv;
    int c;

    av_freep(&s->wave_table);
    for (c = 0; c < s->channels; c++)
        av_freep(&s->buf[c]);
    av_freep(&s->buf);
}

static int config_input(AVFilterLink *inlink)
{
    int c;
    AVFilterContext *ctx = inlink->dst;
    VibratoContext *s = ctx->priv;

    s->buf = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->buf));
    if (!s->buf)
        return AVERROR(ENOMEM);
    s->channels = inlink->ch_layout.nb_channels;
    s->buf_size = lrint(inlink->sample_rate * 0.005 + 0.5);
    for (c = 0; c < s->channels; c++) {
        s->buf[c] = av_malloc_array(s->buf_size, sizeof(*s->buf[c]));
        if (!s->buf[c])
            return AVERROR(ENOMEM);
    }
    s->buf_index = 0;

    s->wave_table_size = lrint(inlink->sample_rate / s->freq + 0.5);
    s->wave_table = av_malloc_array(s->wave_table_size, sizeof(*s->wave_table));
    if (!s->wave_table)
        return AVERROR(ENOMEM);
    ff_generate_wave_table(WAVE_SIN, AV_SAMPLE_FMT_DBL, s->wave_table, s->wave_table_size, 0.0, s->buf_size - 1, 3.0 * M_PI_2);
    s->wave_table_index = 0;

    return 0;
}

static const AVFilterPad avfilter_af_vibrato_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_vibrato = {
    .name          = "vibrato",
    .description   = NULL_IF_CONFIG_SMALL("Apply vibrato effect."),
    .priv_size     = sizeof(VibratoContext),
    .priv_class    = &vibrato_class,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_af_vibrato_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
