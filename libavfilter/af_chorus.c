/*
 * Copyright (c) 1998 Juergen Mueller And Sundry Contributors
 * This source code is freely redistributable and may be used for
 * any purpose.  This copyright notice must be maintained.
 * Juergen Mueller And Sundry Contributors are not responsible for
 * the consequences of using this software.
 *
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

/**
 * @file
 * chorus audio filter
 */

#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "generate_wave_table.h"

typedef struct ChorusContext {
    const AVClass *class;
    float in_gain, out_gain;
    char *delays_str;
    char *decays_str;
    char *speeds_str;
    char *depths_str;
    float *delays;
    float *decays;
    float *speeds;
    float *depths;
    uint8_t **chorusbuf;
    int **phase;
    int *length;
    int32_t **lookup_table;
    int *counter;
    int num_chorus;
    int max_samples;
    int channels;
    int modulation;
    int fade_out;
    int64_t next_pts;
} ChorusContext;

#define OFFSET(x) offsetof(ChorusContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption chorus_options[] = {
    { "in_gain",  "set input gain",  OFFSET(in_gain),    AV_OPT_TYPE_FLOAT,  {.dbl=.4}, 0, 1, A },
    { "out_gain", "set output gain", OFFSET(out_gain),   AV_OPT_TYPE_FLOAT,  {.dbl=.4}, 0, 1, A },
    { "delays",   "set delays",      OFFSET(delays_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A },
    { "decays",   "set decays",      OFFSET(decays_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A },
    { "speeds",   "set speeds",      OFFSET(speeds_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A },
    { "depths",   "set depths",      OFFSET(depths_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(chorus);

static void count_items(char *item_str, int *nb_items)
{
    char *p;

    *nb_items = 1;
    for (p = item_str; *p; p++) {
        if (*p == '|')
            (*nb_items)++;
    }

}

static void fill_items(char *item_str, int *nb_items, float *items)
{
    char *p, *saveptr = NULL;
    int i, new_nb_items = 0;

    p = item_str;
    for (i = 0; i < *nb_items; i++) {
        char *tstr = av_strtok(p, "|", &saveptr);
        p = NULL;
        if (tstr)
            new_nb_items += sscanf(tstr, "%f", &items[new_nb_items]) == 1;
    }

    *nb_items = new_nb_items;
}

static av_cold int init(AVFilterContext *ctx)
{
    ChorusContext *s = ctx->priv;
    int nb_delays, nb_decays, nb_speeds, nb_depths;

    if (!s->delays_str || !s->decays_str || !s->speeds_str || !s->depths_str) {
        av_log(ctx, AV_LOG_ERROR, "Both delays & decays & speeds & depths must be set.\n");
        return AVERROR(EINVAL);
    }

    count_items(s->delays_str, &nb_delays);
    count_items(s->decays_str, &nb_decays);
    count_items(s->speeds_str, &nb_speeds);
    count_items(s->depths_str, &nb_depths);

    s->delays = av_realloc_f(s->delays, nb_delays, sizeof(*s->delays));
    s->decays = av_realloc_f(s->decays, nb_decays, sizeof(*s->decays));
    s->speeds = av_realloc_f(s->speeds, nb_speeds, sizeof(*s->speeds));
    s->depths = av_realloc_f(s->depths, nb_depths, sizeof(*s->depths));

    if (!s->delays || !s->decays || !s->speeds || !s->depths)
        return AVERROR(ENOMEM);

    fill_items(s->delays_str, &nb_delays, s->delays);
    fill_items(s->decays_str, &nb_decays, s->decays);
    fill_items(s->speeds_str, &nb_speeds, s->speeds);
    fill_items(s->depths_str, &nb_depths, s->depths);

    if (nb_delays != nb_decays && nb_delays != nb_speeds && nb_delays != nb_depths) {
        av_log(ctx, AV_LOG_ERROR, "Number of delays & decays & speeds & depths given must be same.\n");
        return AVERROR(EINVAL);
    }

    s->num_chorus = nb_delays;

    if (s->num_chorus < 1) {
        av_log(ctx, AV_LOG_ERROR, "At least one delay & decay & speed & depth must be set.\n");
        return AVERROR(EINVAL);
    }

    s->length = av_calloc(s->num_chorus, sizeof(*s->length));
    s->lookup_table = av_calloc(s->num_chorus, sizeof(*s->lookup_table));

    if (!s->length || !s->lookup_table)
        return AVERROR(ENOMEM);

    s->next_pts = AV_NOPTS_VALUE;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ChorusContext *s = ctx->priv;
    float sum_in_volume = 1.0;
    int n;

    s->channels = outlink->ch_layout.nb_channels;

    for (n = 0; n < s->num_chorus; n++) {
        int samples = (int) ((s->delays[n] + s->depths[n]) * outlink->sample_rate / 1000.0);
        int depth_samples = (int) (s->depths[n] * outlink->sample_rate / 1000.0);

        s->length[n] = outlink->sample_rate / s->speeds[n];

        s->lookup_table[n] = av_malloc(sizeof(int32_t) * s->length[n]);
        if (!s->lookup_table[n])
            return AVERROR(ENOMEM);

        ff_generate_wave_table(WAVE_SIN, AV_SAMPLE_FMT_S32, s->lookup_table[n],
                               s->length[n], 0., depth_samples, 0);
        s->max_samples = FFMAX(s->max_samples, samples);
    }

    for (n = 0; n < s->num_chorus; n++)
        sum_in_volume += s->decays[n];

    if (s->in_gain * (sum_in_volume) > 1.0 / s->out_gain)
        av_log(ctx, AV_LOG_WARNING, "output gain can cause saturation or clipping of output\n");

    s->counter = av_calloc(outlink->ch_layout.nb_channels, sizeof(*s->counter));
    if (!s->counter)
        return AVERROR(ENOMEM);

    s->phase = av_calloc(outlink->ch_layout.nb_channels, sizeof(*s->phase));
    if (!s->phase)
        return AVERROR(ENOMEM);

    for (n = 0; n < outlink->ch_layout.nb_channels; n++) {
        s->phase[n] = av_calloc(s->num_chorus, sizeof(int));
        if (!s->phase[n])
            return AVERROR(ENOMEM);
    }

    s->fade_out = s->max_samples;

    return av_samples_alloc_array_and_samples(&s->chorusbuf, NULL,
                                              outlink->ch_layout.nb_channels,
                                              s->max_samples,
                                              outlink->format, 0);
}

#define MOD(a, b) (((a) >= (b)) ? (a) - (b) : (a))

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ChorusContext *s = ctx->priv;
    AVFrame *out_frame;
    int c, i, n;

    if (av_frame_is_writable(frame)) {
        out_frame = frame;
    } else {
        out_frame = ff_get_audio_buffer(ctx->outputs[0], frame->nb_samples);
        if (!out_frame) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out_frame, frame);
    }

    for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
        const float *src = (const float *)frame->extended_data[c];
        float *dst = (float *)out_frame->extended_data[c];
        float *chorusbuf = (float *)s->chorusbuf[c];
        int *phase = s->phase[c];

        for (i = 0; i < frame->nb_samples; i++) {
            float out, in = src[i];

            out = in * s->in_gain;

            for (n = 0; n < s->num_chorus; n++) {
                out += chorusbuf[MOD(s->max_samples + s->counter[c] -
                                     s->lookup_table[n][phase[n]],
                                     s->max_samples)] * s->decays[n];
                phase[n] = MOD(phase[n] + 1, s->length[n]);
            }

            out *= s->out_gain;

            dst[i] = out;

            chorusbuf[s->counter[c]] = in;
            s->counter[c] = MOD(s->counter[c] + 1, s->max_samples);
        }
    }

    s->next_pts = frame->pts + av_rescale_q(frame->nb_samples, (AVRational){1, inlink->sample_rate}, inlink->time_base);

    if (frame != out_frame)
        av_frame_free(&frame);

    return ff_filter_frame(ctx->outputs[0], out_frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ChorusContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && !ctx->is_disabled && s->fade_out) {
        int nb_samples = FFMIN(s->fade_out, 2048);
        AVFrame *frame;

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        s->fade_out -= nb_samples;

        av_samples_set_silence(frame->extended_data, 0,
                               frame->nb_samples,
                               outlink->ch_layout.nb_channels,
                               frame->format);

        frame->pts = s->next_pts;
        if (s->next_pts != AV_NOPTS_VALUE)
            s->next_pts += av_rescale_q(nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);

        ret = filter_frame(ctx->inputs[0], frame);
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ChorusContext *s = ctx->priv;
    int n;

    av_freep(&s->delays);
    av_freep(&s->decays);
    av_freep(&s->speeds);
    av_freep(&s->depths);

    if (s->chorusbuf)
        av_freep(&s->chorusbuf[0]);
    av_freep(&s->chorusbuf);

    if (s->phase)
        for (n = 0; n < s->channels; n++)
            av_freep(&s->phase[n]);
    av_freep(&s->phase);

    av_freep(&s->counter);
    av_freep(&s->length);

    if (s->lookup_table)
        for (n = 0; n < s->num_chorus; n++)
            av_freep(&s->lookup_table[n]);
    av_freep(&s->lookup_table);
}

static const AVFilterPad chorus_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad chorus_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
        .config_props  = config_output,
    },
};

const AVFilter ff_af_chorus = {
    .name          = "chorus",
    .description   = NULL_IF_CONFIG_SMALL("Add a chorus effect to the audio."),
    .priv_size     = sizeof(ChorusContext),
    .priv_class    = &chorus_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(chorus_inputs),
    FILTER_OUTPUTS(chorus_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
};
