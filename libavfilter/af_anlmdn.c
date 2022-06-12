/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "filters.h"

#include "af_anlmdndsp.h"

#define WEIGHT_LUT_NBITS 20
#define WEIGHT_LUT_SIZE  (1<<WEIGHT_LUT_NBITS)

typedef struct AudioNLMeansContext {
    const AVClass *class;

    float a;
    int64_t pd;
    int64_t rd;
    float m;
    int om;

    float pdiff_lut_scale;
    float weight_lut[WEIGHT_LUT_SIZE];

    int K;
    int S;
    int N;
    int H;

    AVFrame *in;
    AVFrame *cache;
    AVFrame *window;

    AudioNLMDNDSPContext dsp;
} AudioNLMeansContext;

enum OutModes {
    IN_MODE,
    OUT_MODE,
    NOISE_MODE,
    NB_MODES
};

#define OFFSET(x) offsetof(AudioNLMeansContext, x)
#define AFT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption anlmdn_options[] = {
    { "strength", "set denoising strength", OFFSET(a),  AV_OPT_TYPE_FLOAT,    {.dbl=0.00001},0.00001, 10000, AFT },
    { "s", "set denoising strength", OFFSET(a),  AV_OPT_TYPE_FLOAT,    {.dbl=0.00001},0.00001, 10000, AFT },
    { "patch", "set patch duration", OFFSET(pd), AV_OPT_TYPE_DURATION, {.i64=2000}, 1000, 100000, AFT },
    { "p", "set patch duration",     OFFSET(pd), AV_OPT_TYPE_DURATION, {.i64=2000}, 1000, 100000, AFT },
    { "research", "set research duration",  OFFSET(rd), AV_OPT_TYPE_DURATION, {.i64=6000}, 2000, 300000, AFT },
    { "r", "set research duration",  OFFSET(rd), AV_OPT_TYPE_DURATION, {.i64=6000}, 2000, 300000, AFT },
    { "output", "set output mode",   OFFSET(om), AV_OPT_TYPE_INT,      {.i64=OUT_MODE},  0, NB_MODES-1, AFT, "mode" },
    { "o", "set output mode",        OFFSET(om), AV_OPT_TYPE_INT,      {.i64=OUT_MODE},  0, NB_MODES-1, AFT, "mode" },
    {  "i", "input",                 0,          AV_OPT_TYPE_CONST,    {.i64=IN_MODE},   0,  0, AFT, "mode" },
    {  "o", "output",                0,          AV_OPT_TYPE_CONST,    {.i64=OUT_MODE},  0,  0, AFT, "mode" },
    {  "n", "noise",                 0,          AV_OPT_TYPE_CONST,    {.i64=NOISE_MODE},0,  0, AFT, "mode" },
    { "smooth", "set smooth factor", OFFSET(m),  AV_OPT_TYPE_FLOAT,    {.dbl=11.},       1, 1000, AFT },
    { "m", "set smooth factor",      OFFSET(m),  AV_OPT_TYPE_FLOAT,    {.dbl=11.},       1, 1000, AFT },
    { NULL }
};

AVFILTER_DEFINE_CLASS(anlmdn);

static inline float sqrdiff(float x, float y)
{
    const float diff = x - y;

    return diff * diff;
}

static float compute_distance_ssd_c(const float *f1, const float *f2, ptrdiff_t K)
{
    float distance = 0.;

    for (int k = -K; k <= K; k++)
        distance += sqrdiff(f1[k], f2[k]);

    return distance;
}

static void compute_cache_c(float *cache, const float *f,
                            ptrdiff_t S, ptrdiff_t K,
                            ptrdiff_t i, ptrdiff_t jj)
{
    int v = 0;

    for (int j = jj; j < jj + S; j++, v++)
        cache[v] += -sqrdiff(f[i - K - 1], f[j - K - 1]) + sqrdiff(f[i + K], f[j + K]);
}

void ff_anlmdn_init(AudioNLMDNDSPContext *dsp)
{
    dsp->compute_distance_ssd = compute_distance_ssd_c;
    dsp->compute_cache        = compute_cache_c;

#if ARCH_X86
    ff_anlmdn_init_x86(dsp);
#endif
}

static int config_filter(AVFilterContext *ctx)
{
    AudioNLMeansContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int newK, newS, newH, newN;

    newK = av_rescale(s->pd, outlink->sample_rate, AV_TIME_BASE);
    newS = av_rescale(s->rd, outlink->sample_rate, AV_TIME_BASE);

    newH = newK * 2 + 1;
    newN = newH + (newK + newS) * 2;

    av_log(ctx, AV_LOG_DEBUG, "K:%d S:%d H:%d N:%d\n", newK, newS, newH, newN);

    if (!s->cache || s->cache->nb_samples < newS * 2) {
        AVFrame *new_cache = ff_get_audio_buffer(outlink, newS * 2);
        if (new_cache) {
            if (s->cache)
                av_samples_copy(new_cache->extended_data, s->cache->extended_data, 0, 0,
                                s->cache->nb_samples, new_cache->ch_layout.nb_channels, new_cache->format);
            av_frame_free(&s->cache);
            s->cache = new_cache;
        } else {
            return AVERROR(ENOMEM);
        }
    }
    if (!s->cache)
        return AVERROR(ENOMEM);

    if (!s->window || s->window->nb_samples < newN) {
        AVFrame *new_window = ff_get_audio_buffer(outlink, newN);
        if (new_window) {
            if (s->window)
                av_samples_copy(new_window->extended_data, s->window->extended_data, 0, 0,
                                s->window->nb_samples, new_window->ch_layout.nb_channels, new_window->format);
            av_frame_free(&s->window);
            s->window = new_window;
        } else {
            return AVERROR(ENOMEM);
        }
    }
    if (!s->window)
        return AVERROR(ENOMEM);

    s->pdiff_lut_scale = 1.f / s->m * WEIGHT_LUT_SIZE;
    for (int i = 0; i < WEIGHT_LUT_SIZE; i++) {
        float w = -i / s->pdiff_lut_scale;

        s->weight_lut[i] = expf(w);
    }

    s->K = newK;
    s->S = newS;
    s->H = newH;
    s->N = newN;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioNLMeansContext *s = ctx->priv;
    int ret;

    ret = config_filter(ctx);
    if (ret < 0)
        return ret;

    ff_anlmdn_init(&s->dsp);

    return 0;
}

static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *out = arg;
    const int S = s->S;
    const int K = s->K;
    const int N = s->N;
    const int H = s->H;
    const int om = s->om;
    const float *f = (const float *)(s->window->extended_data[ch]) + K;
    float *cache = (float *)s->cache->extended_data[ch];
    const float sw = (65536.f / (4 * K + 2)) / sqrtf(s->a);
    float *dst = (float *)out->extended_data[ch];
    const float *const weight_lut = s->weight_lut;
    const float pdiff_lut_scale = s->pdiff_lut_scale;
    const float smooth = fminf(s->m, WEIGHT_LUT_SIZE / pdiff_lut_scale);
    const int offset = N - H;
    float *src = (float *)s->window->extended_data[ch];
    const AVFrame *const in = s->in;

    memmove(src, &src[H], offset * sizeof(float));
    memcpy(&src[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
    memset(&src[offset + in->nb_samples], 0, (H - in->nb_samples) * sizeof(float));

    for (int i = S; i < H + S; i++) {
        float P = 0.f, Q = 0.f;
        int v = 0;

        if (i == S) {
            for (int j = i - S; j <= i + S; j++) {
                if (i == j)
                    continue;
                cache[v++] = s->dsp.compute_distance_ssd(f + i, f + j, K);
            }
        } else {
            s->dsp.compute_cache(cache, f, S, K, i, i - S);
            s->dsp.compute_cache(cache + S, f, S, K, i, i + 1);
        }

        for (int j = 0; j < 2 * S && !ctx->is_disabled; j++) {
            float distance = cache[j];
            unsigned weight_lut_idx;
            float w;

            if (distance < 0.f)
                cache[j] = distance = 0.f;
            w = distance * sw;
            if (w >= smooth)
                continue;
            weight_lut_idx = w * pdiff_lut_scale;
            av_assert2(weight_lut_idx < WEIGHT_LUT_SIZE);
            w = weight_lut[weight_lut_idx];
            P += w * f[i - S + j + (j >= S)];
            Q += w;
        }

        P += f[i];
        Q += 1.f;

        switch (om) {
        case IN_MODE:    dst[i - S] = f[i];           break;
        case OUT_MODE:   dst[i - S] = P / Q;          break;
        case NOISE_MODE: dst[i - S] = f[i] - (P / Q); break;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        out->pts = in->pts;
    }

    s->in = in;
    ff_filter_execute(ctx, filter_channel, out, NULL, inlink->ch_layout.nb_channels);

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->H, s->H, &in);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        return filter_frame(inlink, in);
    } else if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    } else {
        if (ff_inlink_queued_samples(inlink) >= s->H) {
            ff_filter_set_ready(ctx, 10);
        } else if (ff_outlink_frame_wanted(outlink)) {
            ff_inlink_request_frame(inlink);
        }
        return 0;
    }
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_filter(ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioNLMeansContext *s = ctx->priv;

    av_frame_free(&s->cache);
    av_frame_free(&s->window);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

const AVFilter ff_af_anlmdn = {
    .name          = "anlmdn",
    .description   = NULL_IF_CONFIG_SMALL("Reduce broadband noise from stream using Non-Local Means."),
    .priv_size     = sizeof(AudioNLMeansContext),
    .priv_class    = &anlmdn_class,
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};
