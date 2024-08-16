/*
 * Copyright (c) 2022 Paul B Mahol
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

#include <float.h>

#include "libavutil/eval.h"
#include "libavutil/ffmath.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"

static const char * const var_names[] = {
    "ch",           ///< the value of the current channel
    "sn",           ///< number of samples
    "nb_channels",
    "t",            ///< timestamp expressed in seconds
    "sr",           ///< sample rate
    "p",            ///< input power in dB for frequency bin
    "f",            ///< frequency in Hz
    NULL
};

enum var_name {
    VAR_CH,
    VAR_SN,
    VAR_NB_CHANNELS,
    VAR_T,
    VAR_SR,
    VAR_P,
    VAR_F,
    VAR_VARS_NB
};

typedef struct AudioDRCContext {
    const AVClass *class;

    double attack_ms;
    double release_ms;
    char *expr_str;

    double attack;
    double release;

    int fft_size;
    int overlap;
    int channels;

    float fx;
    float *window;

    AVFrame *drc_frame;
    AVFrame *energy;
    AVFrame *envelope;
    AVFrame *factors;
    AVFrame *in;
    AVFrame *in_buffer;
    AVFrame *in_frame;
    AVFrame *out_dist_frame;
    AVFrame *spectrum_buf;
    AVFrame *target_gain;
    AVFrame *windowed_frame;

    char *channels_to_filter;
    AVChannelLayout ch_layout;

    AVTXContext **tx_ctx;
    av_tx_fn tx_fn;
    AVTXContext **itx_ctx;
    av_tx_fn itx_fn;

    AVExpr *expr;
    double var_values[VAR_VARS_NB];
} AudioDRCContext;

#define OFFSET(x) offsetof(AudioDRCContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption adrc_options[] = {
    { "transfer",    "set the transfer expression", OFFSET(expr_str),   AV_OPT_TYPE_STRING, {.str="p"},  0,    0, FLAGS },
    { "attack",      "set the attack",              OFFSET(attack_ms),  AV_OPT_TYPE_DOUBLE, {.dbl=50.},  1, 1000, FLAGS },
    { "release",     "set the release",             OFFSET(release_ms), AV_OPT_TYPE_DOUBLE, {.dbl=100.}, 5, 2000, FLAGS },
    { "channels",    "set channels to filter",OFFSET(channels_to_filter),AV_OPT_TYPE_STRING,{.str="all"},0,    0, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(adrc);

static void generate_hann_window(float *window, int size)
{
    for (int i = 0; i < size; i++) {
        float value = 0.5f * (1.f - cosf(2.f * M_PI * i / size));

        window[i] = value;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDRCContext *s = ctx->priv;
    float scale;
    int ret;

    s->fft_size = inlink->sample_rate > 100000 ? 1024 : inlink->sample_rate > 50000 ? 512 : 256;
    s->fx = inlink->sample_rate * 0.5f / (s->fft_size / 2 + 1);
    s->overlap = s->fft_size / 4;

    s->window = av_calloc(s->fft_size, sizeof(*s->window));
    if (!s->window)
        return AVERROR(ENOMEM);

    s->drc_frame      = ff_get_audio_buffer(inlink, s->fft_size * 2);
    s->energy         = ff_get_audio_buffer(inlink, s->fft_size / 2 + 1);
    s->envelope       = ff_get_audio_buffer(inlink, s->fft_size / 2 + 1);
    s->factors        = ff_get_audio_buffer(inlink, s->fft_size / 2 + 1);
    s->in_buffer      = ff_get_audio_buffer(inlink, s->fft_size * 2);
    s->in_frame       = ff_get_audio_buffer(inlink, s->fft_size * 2);
    s->out_dist_frame = ff_get_audio_buffer(inlink, s->fft_size * 2);
    s->spectrum_buf   = ff_get_audio_buffer(inlink, s->fft_size * 2);
    s->target_gain    = ff_get_audio_buffer(inlink, s->fft_size / 2 + 1);
    s->windowed_frame = ff_get_audio_buffer(inlink, s->fft_size * 2);
    if (!s->in_buffer || !s->in_frame || !s->target_gain ||
        !s->out_dist_frame || !s->windowed_frame || !s->envelope ||
        !s->drc_frame || !s->spectrum_buf || !s->energy || !s->factors)
        return AVERROR(ENOMEM);

    generate_hann_window(s->window, s->fft_size);

    s->channels = inlink->ch_layout.nb_channels;

    s->tx_ctx = av_calloc(s->channels, sizeof(*s->tx_ctx));
    s->itx_ctx = av_calloc(s->channels, sizeof(*s->itx_ctx));
    if (!s->tx_ctx || !s->itx_ctx)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < s->channels; ch++) {
        scale = 1.f / s->fft_size;
        ret = av_tx_init(&s->tx_ctx[ch], &s->tx_fn, AV_TX_FLOAT_RDFT, 0, s->fft_size, &scale, 0);
        if (ret < 0)
            return ret;

        scale = 1.f;
        ret = av_tx_init(&s->itx_ctx[ch], &s->itx_fn, AV_TX_FLOAT_RDFT, 1, s->fft_size, &scale, 0);
        if (ret < 0)
            return ret;
    }

    s->var_values[VAR_SR] = inlink->sample_rate;
    s->var_values[VAR_NB_CHANNELS] = s->channels;

    return av_expr_parse(&s->expr, s->expr_str, var_names, NULL, NULL,
                         NULL, NULL, 0, ctx);
}

static void apply_window(AudioDRCContext *s,
                         const float *in_frame, float *out_frame, const int add_to_out_frame)
{
    const float *window = s->window;
    const int fft_size = s->fft_size;

    if (add_to_out_frame) {
        for (int i = 0; i < fft_size; i++)
            out_frame[i] += in_frame[i] * window[i];
    } else {
        for (int i = 0; i < fft_size; i++)
            out_frame[i] = in_frame[i] * window[i];
    }
}

static float sqrf(float x)
{
    return x * x;
}

static void get_energy(AVFilterContext *ctx,
                       int len,
                       float *energy,
                       const float *spectral)
{
    for (int n = 0; n < len; n++) {
        energy[n] = 10.f * log10f(sqrf(spectral[2 * n]) + sqrf(spectral[2 * n + 1]));
        if (!isnormal(energy[n]))
            energy[n] = -351.f;
    }
}

static void get_target_gain(AVFilterContext *ctx,
                            int len,
                            float *gain,
                            const float *energy,
                            double *var_values,
                            float fx, int bypass)
{
    AudioDRCContext *s = ctx->priv;

    if (bypass) {
        memcpy(gain, energy, sizeof(*gain) * len);
        return;
    }

    for (int n = 0; n < len; n++) {
        const float Xg = energy[n];

        var_values[VAR_P] = Xg;
        var_values[VAR_F] = n * fx;

        gain[n] = av_expr_eval(s->expr, var_values, s);
    }
}

static void get_envelope(AVFilterContext *ctx,
                         int len,
                         float *envelope,
                         const float *energy,
                         const float *gain)
{
    AudioDRCContext *s = ctx->priv;
    const float release = s->release;
    const float attack = s->attack;

    for (int n = 0; n < len; n++) {
        const float Bg = gain[n] - energy[n];
        const float Vg = envelope[n];

        if (Bg > Vg) {
            envelope[n] = attack  * Vg + (1.f - attack)  * Bg;
        } else if (Bg <= Vg)  {
            envelope[n] = release * Vg + (1.f - release) * Bg;
        } else {
            envelope[n] = 0.f;
        }
    }
}

static void get_factors(AVFilterContext *ctx,
                        int len,
                        float *factors,
                        const float *envelope)
{
    for (int n = 0; n < len; n++)
        factors[n] = sqrtf(ff_exp10f(envelope[n] / 10.f));
}

static void apply_factors(AVFilterContext *ctx,
                          int len,
                          float *spectrum,
                          const float *factors)
{
    for (int n = 0; n < len; n++) {
        spectrum[2*n+0] *= factors[n];
        spectrum[2*n+1] *= factors[n];
    }
}

static void feed(AVFilterContext *ctx, int ch,
                 const float *in_samples, float *out_samples,
                 float *in_frame, float *out_dist_frame,
                 float *windowed_frame, float *drc_frame,
                 float *spectrum_buf, float *energy,
                 float *target_gain, float *envelope,
                 float *factors)
{
    AudioDRCContext *s = ctx->priv;
    double var_values[VAR_VARS_NB];
    const int fft_size = s->fft_size;
    const int nb_coeffs = s->fft_size / 2 + 1;
    const int overlap = s->overlap;
    enum AVChannel channel = av_channel_layout_channel_from_index(&ctx->inputs[0]->ch_layout, ch);
    const int bypass = av_channel_layout_index_from_channel(&s->ch_layout, channel) < 0;

    memcpy(var_values, s->var_values, sizeof(var_values));

    var_values[VAR_CH] = ch;

    // shift in/out buffers
    memmove(in_frame, in_frame + overlap, (fft_size - overlap) * sizeof(*in_frame));
    memmove(out_dist_frame, out_dist_frame + overlap, (fft_size - overlap) * sizeof(*out_dist_frame));

    memcpy(in_frame + fft_size - overlap, in_samples, sizeof(*in_frame) * overlap);
    memset(out_dist_frame + fft_size - overlap, 0, sizeof(*out_dist_frame) * overlap);

    apply_window(s, in_frame, windowed_frame, 0);
    s->tx_fn(s->tx_ctx[ch], spectrum_buf, windowed_frame, sizeof(float));

    get_energy(ctx, nb_coeffs, energy, spectrum_buf);
    get_target_gain(ctx, nb_coeffs, target_gain, energy, var_values, s->fx, bypass);
    get_envelope(ctx, nb_coeffs, envelope, energy, target_gain);
    get_factors(ctx, nb_coeffs, factors, envelope);
    apply_factors(ctx, nb_coeffs, spectrum_buf, factors);

    s->itx_fn(s->itx_ctx[ch], drc_frame, spectrum_buf, sizeof(AVComplexFloat));

    apply_window(s, drc_frame, out_dist_frame, 1);

    // 4 times overlap with squared hanning window results in 1.5 time increase in amplitude
    if (!ctx->is_disabled) {
        for (int i = 0; i < overlap; i++)
            out_samples[i] = out_dist_frame[i] / 1.5f;
    } else {
        memcpy(out_samples, in_frame, sizeof(*out_samples) * overlap);
    }
}

static int drc_channel(AVFilterContext *ctx, AVFrame *in, AVFrame *out, int ch)
{
    AudioDRCContext *s = ctx->priv;
    const float *src = (const float *)in->extended_data[ch];
    float *in_buffer = (float *)s->in_buffer->extended_data[ch];
    float *dst = (float *)out->extended_data[ch];

    memcpy(in_buffer, src, sizeof(*in_buffer) * s->overlap);

    feed(ctx, ch, in_buffer, dst,
         (float *)(s->in_frame->extended_data[ch]),
         (float *)(s->out_dist_frame->extended_data[ch]),
         (float *)(s->windowed_frame->extended_data[ch]),
         (float *)(s->drc_frame->extended_data[ch]),
         (float *)(s->spectrum_buf->extended_data[ch]),
         (float *)(s->energy->extended_data[ch]),
         (float *)(s->target_gain->extended_data[ch]),
         (float *)(s->envelope->extended_data[ch]),
         (float *)(s->factors->extended_data[ch]));

    return 0;
}

static int drc_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioDRCContext *s = ctx->priv;
    AVFrame *in = s->in;
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++)
        drc_channel(ctx, in, out, ch);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *outl = ff_filter_link(outlink);
    AudioDRCContext *s = ctx->priv;
    AVFrame *out;
    int ret;

    out = ff_get_audio_buffer(outlink, s->overlap);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->var_values[VAR_SN] = outl->sample_count_in;
    s->var_values[VAR_T] = s->var_values[VAR_SN] * (double)1/outlink->sample_rate;

    s->in = in;
    av_frame_copy_props(out, in);
    ff_filter_execute(ctx, drc_channels, out, NULL,
                      FFMIN(outlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    out->pts = in->pts;
    out->nb_samples = in->nb_samples;
    ret = ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    s->in = NULL;
    return ret < 0 ? ret : 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDRCContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    ret = av_channel_layout_copy(&s->ch_layout, &inlink->ch_layout);
    if (ret < 0)
        return ret;
    if (strcmp(s->channels_to_filter, "all"))
        av_channel_layout_from_string(&s->ch_layout, s->channels_to_filter);

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->overlap, s->overlap, &in);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        s->attack  = expf(-1.f / (s->attack_ms  * inlink->sample_rate / 1000.f));
        s->release = expf(-1.f / (s->release_ms * inlink->sample_rate / 1000.f));

        return filter_frame(inlink, in);
    } else if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    } else {
        if (ff_inlink_queued_samples(inlink) >= s->overlap) {
            ff_filter_set_ready(ctx, 10);
        } else if (ff_outlink_frame_wanted(outlink)) {
            ff_inlink_request_frame(inlink);
        }
        return 0;
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioDRCContext *s = ctx->priv;

    av_channel_layout_uninit(&s->ch_layout);

    av_expr_free(s->expr);
    s->expr = NULL;

    av_freep(&s->window);

    av_frame_free(&s->drc_frame);
    av_frame_free(&s->energy);
    av_frame_free(&s->envelope);
    av_frame_free(&s->factors);
    av_frame_free(&s->in_buffer);
    av_frame_free(&s->in_frame);
    av_frame_free(&s->out_dist_frame);
    av_frame_free(&s->spectrum_buf);
    av_frame_free(&s->target_gain);
    av_frame_free(&s->windowed_frame);

    for (int ch = 0; ch < s->channels; ch++) {
        if (s->tx_ctx)
            av_tx_uninit(&s->tx_ctx[ch]);
        if (s->itx_ctx)
            av_tx_uninit(&s->itx_ctx[ch]);
    }

    av_freep(&s->tx_ctx);
    av_freep(&s->itx_ctx);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioDRCContext *s = ctx->priv;
    char *old_expr_str = av_strdup(s->expr_str);
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret >= 0 && strcmp(old_expr_str, s->expr_str)) {
        ret = av_expr_parse(&s->expr, s->expr_str, var_names, NULL, NULL,
                            NULL, NULL, 0, ctx);
    }
    av_free(old_expr_str);
    return ret;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_adrc = {
    .name            = "adrc",
    .description     = NULL_IF_CONFIG_SMALL("Audio Spectral Dynamic Range Controller."),
    .priv_size       = sizeof(AudioDRCContext),
    .priv_class      = &adrc_class,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
    .activate        = activate,
    .process_command = process_command,
};
