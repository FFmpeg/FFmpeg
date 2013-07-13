/*
 * Copyright (c) 2013 Paul B Mahol
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
 *
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct AudioEchoContext {
    const AVClass *class;
    float in_gain, out_gain;
    char *delays, *decays;
    float *delay, *decay;
    int nb_echoes;
    int delay_index;
    uint8_t **delayptrs;
    int max_samples, fade_out;
    int *samples;
    int64_t next_pts;

    void (*echo_samples)(struct AudioEchoContext *ctx, uint8_t **delayptrs,
                         uint8_t * const *src, uint8_t **dst,
                         int nb_samples, int channels);
} AudioEchoContext;

#define OFFSET(x) offsetof(AudioEchoContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aecho_options[] = {
    { "in_gain",  "set signal input gain",  OFFSET(in_gain),  AV_OPT_TYPE_FLOAT,  {.dbl=0.6}, 0, 1, A },
    { "out_gain", "set signal output gain", OFFSET(out_gain), AV_OPT_TYPE_FLOAT,  {.dbl=0.3}, 0, 1, A },
    { "delays",   "set list of signal delays", OFFSET(delays), AV_OPT_TYPE_STRING, {.str="1000"}, 0, 0, A },
    { "decays",   "set list of signal decays", OFFSET(decays), AV_OPT_TYPE_STRING, {.str="0.5"}, 0, 0, A },
    { NULL },
};

AVFILTER_DEFINE_CLASS(aecho);

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
        new_nb_items += sscanf(tstr, "%f", &items[i]) == 1;
    }

    *nb_items = new_nb_items;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioEchoContext *s = ctx->priv;

    av_freep(&s->delay);
    av_freep(&s->decay);
    av_freep(&s->samples);

    if (s->delayptrs)
        av_freep(&s->delayptrs[0]);
    av_freep(&s->delayptrs);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioEchoContext *s = ctx->priv;
    int nb_delays, nb_decays, i;

    if (!s->delays || !s->decays) {
        av_log(ctx, AV_LOG_ERROR, "Missing delays and/or decays.\n");
        return AVERROR(EINVAL);
    }

    count_items(s->delays, &nb_delays);
    count_items(s->decays, &nb_decays);

    s->delay = av_realloc_f(s->delay, nb_delays, sizeof(*s->delay));
    s->decay = av_realloc_f(s->decay, nb_decays, sizeof(*s->decay));
    if (!s->delay || !s->decay)
        return AVERROR(ENOMEM);

    fill_items(s->delays, &nb_delays, s->delay);
    fill_items(s->decays, &nb_decays, s->decay);

    if (nb_delays != nb_decays) {
        av_log(ctx, AV_LOG_ERROR, "Number of delays %d differs from number of decays %d.\n", nb_delays, nb_decays);
        return AVERROR(EINVAL);
    }

    s->nb_echoes = nb_delays;
    if (!s->nb_echoes) {
        av_log(ctx, AV_LOG_ERROR, "At least one decay & delay must be set.\n");
        return AVERROR(EINVAL);
    }

    s->samples = av_realloc_f(s->samples, nb_delays, sizeof(*s->samples));
    if (!s->samples)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_delays; i++) {
        if (s->delay[i] <= 0 || s->delay[i] > 90000) {
            av_log(ctx, AV_LOG_ERROR, "delay[%d]: %f is out of allowed range: (0, 90000]\n", i, s->delay[i]);
            return AVERROR(EINVAL);
        }
        if (s->decay[i] <= 0 || s->decay[i] > 1) {
            av_log(ctx, AV_LOG_ERROR, "decay[%d]: %f is out of allowed range: (0, 1]\n", i, s->decay[i]);
            return AVERROR(EINVAL);
        }
    }

    s->next_pts = AV_NOPTS_VALUE;

    av_log(ctx, AV_LOG_DEBUG, "nb_echoes:%d\n", s->nb_echoes);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

#define MOD(a, b) (((a) >= (b)) ? (a) - (b) : (a))

#define ECHO(name, type, min, max)                                          \
static void echo_samples_## name ##p(AudioEchoContext *ctx,                 \
                                     uint8_t **delayptrs,                   \
                                     uint8_t * const *src, uint8_t **dst,   \
                                     int nb_samples, int channels)          \
{                                                                           \
    const double out_gain = ctx->out_gain;                                  \
    const double in_gain = ctx->in_gain;                                    \
    const int nb_echoes = ctx->nb_echoes;                                   \
    const int max_samples = ctx->max_samples;                               \
    int i, j, chan, index;                                                  \
                                                                            \
    for (chan = 0; chan < channels; chan++) {                               \
        const type *s = (type *)src[chan];                                  \
        type *d = (type *)dst[chan];                                        \
        type *dbuf = (type *)delayptrs[chan];                               \
                                                                            \
        index = ctx->delay_index;                                           \
        for (i = 0; i < nb_samples; i++, s++, d++) {                        \
            double out, in;                                                 \
                                                                            \
            in = *s;                                                        \
            out = in * in_gain;                                             \
            for (j = 0; j < nb_echoes; j++) {                               \
                int ix = index + max_samples - ctx->samples[j];             \
                ix = MOD(ix, max_samples);                                  \
                out += dbuf[ix] * ctx->decay[j];                            \
            }                                                               \
            out *= out_gain;                                                \
                                                                            \
            *d = av_clipd(out, min, max);                                   \
            dbuf[index] = in;                                               \
                                                                            \
            index = MOD(index + 1, max_samples);                            \
        }                                                                   \
    }                                                                       \
    ctx->delay_index = index;                                               \
}

ECHO(dbl, double,  -1.0,      1.0      )
ECHO(flt, float,   -1.0,      1.0      )
ECHO(s16, int16_t, INT16_MIN, INT16_MAX)
ECHO(s32, int32_t, INT32_MIN, INT32_MAX)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioEchoContext *s = ctx->priv;
    float volume = 1.0;
    int i;

    for (i = 0; i < s->nb_echoes; i++) {
        s->samples[i] = s->delay[i] * outlink->sample_rate / 1000.0;
        s->max_samples = FFMAX(s->max_samples, s->samples[i]);
        volume += s->decay[i];
    }

    if (s->max_samples <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Nothing to echo - missing delay samples.\n");
        return AVERROR(EINVAL);
    }
    s->fade_out = s->max_samples;

    if (volume * s->in_gain * s->out_gain > 1.0)
        av_log(ctx, AV_LOG_WARNING,
               "out_gain %f can cause saturation of output\n", s->out_gain);

    switch (outlink->format) {
    case AV_SAMPLE_FMT_DBLP: s->echo_samples = echo_samples_dblp; break;
    case AV_SAMPLE_FMT_FLTP: s->echo_samples = echo_samples_fltp; break;
    case AV_SAMPLE_FMT_S16P: s->echo_samples = echo_samples_s16p; break;
    case AV_SAMPLE_FMT_S32P: s->echo_samples = echo_samples_s32p; break;
    }


    if (s->delayptrs)
        av_freep(&s->delayptrs[0]);
    av_freep(&s->delayptrs);

    return av_samples_alloc_array_and_samples(&s->delayptrs, NULL,
                                              outlink->channels,
                                              s->max_samples,
                                              outlink->format, 0);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AudioEchoContext *s = ctx->priv;
    AVFrame *out_frame;

    if (av_frame_is_writable(frame)) {
        out_frame = frame;
    } else {
        out_frame = ff_get_audio_buffer(inlink, frame->nb_samples);
        if (!out_frame)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_frame, frame);
    }

    s->echo_samples(s, s->delayptrs, frame->data, out_frame->data,
                    frame->nb_samples, inlink->channels);

    if (frame != out_frame)
        av_frame_free(&frame);

    s->next_pts = frame->pts + av_rescale_q(frame->nb_samples, (AVRational){1, inlink->sample_rate}, inlink->time_base);
    return ff_filter_frame(ctx->outputs[0], out_frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioEchoContext *s = ctx->priv;
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
                               outlink->channels,
                               frame->format);

        s->echo_samples(s, s->delayptrs, frame->data, frame->data,
                        frame->nb_samples, outlink->channels);

        frame->pts = s->next_pts;
        if (s->next_pts != AV_NOPTS_VALUE)
            s->next_pts += av_rescale_q(nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);

        return ff_filter_frame(outlink, frame);
    }

    return ret;
}

static const AVFilterPad aecho_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL },
};

static const AVFilterPad aecho_outputs[] = {
    {
        .name          = "default",
        .request_frame = request_frame,
        .config_props  = config_output,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL },
};

AVFilter avfilter_af_aecho = {
    .name          = "aecho",
    .description   = NULL_IF_CONFIG_SMALL("Add echoing to the audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioEchoContext),
    .priv_class    = &aecho_class,
    .init          = init,
    .uninit        = uninit,
    .inputs        = aecho_inputs,
    .outputs       = aecho_outputs,
};
