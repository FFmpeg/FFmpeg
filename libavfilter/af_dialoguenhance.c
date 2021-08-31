/*
 * Copyright (c) 2022 Paul B Mahol
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
#include "libavutil/tx.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "window_func.h"

#include <float.h>

typedef struct AudioDialogueEnhancementContext {
    const AVClass *class;

    double original, enhance, voice;

    int fft_size;
    int overlap;

    float *window;
    float prev_vad;

    AVFrame *in;
    AVFrame *in_frame;
    AVFrame *out_dist_frame;
    AVFrame *windowed_frame;
    AVFrame *windowed_out;
    AVFrame *windowed_prev;
    AVFrame *center_frame;

    AVTXContext *tx_ctx[2], *itx_ctx;
    av_tx_fn tx_fn, itx_fn;
} AudioDialogueEnhanceContext;

#define OFFSET(x) offsetof(AudioDialogueEnhanceContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption dialoguenhance_options[] = {
    { "original", "set original center factor", OFFSET(original), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "enhance",  "set dialogue enhance factor",OFFSET(enhance),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 3, FLAGS },
    { "voice",    "set voice detection factor", OFFSET(voice),    AV_OPT_TYPE_DOUBLE, {.dbl=2}, 2,32, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(dialoguenhance);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *in_layout = NULL, *out_layout = NULL;
    int ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_FLTP )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&in_layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_channel_layouts_ref(in_layout, &ctx->inputs[0]->outcfg.channel_layouts)) < 0 ||
        (ret = ff_add_channel_layout         (&out_layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_SURROUND)) < 0 ||
        (ret = ff_channel_layouts_ref(out_layout, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDialogueEnhanceContext *s = ctx->priv;
    float scale = 1.f, iscale, overlap;
    int ret;

    s->fft_size = inlink->sample_rate > 100000 ? 8192 : inlink->sample_rate > 50000 ? 4096 : 2048;
    s->overlap = s->fft_size / 4;

    s->window = av_calloc(s->fft_size, sizeof(*s->window));
    if (!s->window)
        return AVERROR(ENOMEM);

    s->in_frame       = ff_get_audio_buffer(inlink, s->fft_size * 4);
    s->center_frame   = ff_get_audio_buffer(inlink, s->fft_size * 4);
    s->out_dist_frame = ff_get_audio_buffer(inlink, s->fft_size * 4);
    s->windowed_frame = ff_get_audio_buffer(inlink, s->fft_size * 4);
    s->windowed_out   = ff_get_audio_buffer(inlink, s->fft_size * 4);
    s->windowed_prev  = ff_get_audio_buffer(inlink, s->fft_size * 4);
    if (!s->in_frame || !s->windowed_out || !s->windowed_prev ||
        !s->out_dist_frame || !s->windowed_frame || !s->center_frame)
        return AVERROR(ENOMEM);

    generate_window_func(s->window, s->fft_size, WFUNC_SINE, &overlap);

    iscale = 1.f / s->fft_size;

    ret = av_tx_init(&s->tx_ctx[0], &s->tx_fn, AV_TX_FLOAT_RDFT, 0, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    ret = av_tx_init(&s->tx_ctx[1], &s->tx_fn, AV_TX_FLOAT_RDFT, 0, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    ret = av_tx_init(&s->itx_ctx, &s->itx_fn, AV_TX_FLOAT_RDFT, 1, s->fft_size, &iscale, 0);
    if (ret < 0)
        return ret;

    return 0;
}

static void apply_window(AudioDialogueEnhanceContext *s,
                         const float *in_frame, float *out_frame, const int add_to_out_frame)
{
    const float *window = s->window;

    if (add_to_out_frame) {
        for (int i = 0; i < s->fft_size; i++)
            out_frame[i] += in_frame[i] * window[i];
    } else {
        for (int i = 0; i < s->fft_size; i++)
            out_frame[i] = in_frame[i] * window[i];
    }
}

static float sqrf(float x)
{
    return x * x;
}

static void get_centere(AVComplexFloat *left, AVComplexFloat *right,
                        AVComplexFloat *center, int N)
{
    for (int i = 0; i < N; i++) {
        const float l_re = left[i].re;
        const float l_im = left[i].im;
        const float r_re = right[i].re;
        const float r_im = right[i].im;
        const float a = 0.5f * (1.f - sqrtf((sqrf(l_re - r_re) + sqrf(l_im - r_im))/
                                            (sqrf(l_re + r_re) + sqrf(l_im + r_im) + FLT_EPSILON)));

        center[i].re = a * (l_re + r_re);
        center[i].im = a * (l_im + r_im);
    }
}

static float flux(float *curf, float *prevf, int N)
{
    AVComplexFloat *cur  = (AVComplexFloat *)curf;
    AVComplexFloat *prev = (AVComplexFloat *)prevf;
    float sum = 0.f;

    for (int i = 0; i < N; i++) {
        float c_re = cur[i].re;
        float c_im = cur[i].im;
        float p_re = prev[i].re;
        float p_im = prev[i].im;

        sum += sqrf(hypotf(c_re, c_im) - hypotf(p_re, p_im));
    }

    return sum;
}

static float fluxlr(float *lf, float *lpf,
                    float *rf, float *rpf,
                    int N)
{
    AVComplexFloat *l  = (AVComplexFloat *)lf;
    AVComplexFloat *lp = (AVComplexFloat *)lpf;
    AVComplexFloat *r  = (AVComplexFloat *)rf;
    AVComplexFloat *rp = (AVComplexFloat *)rpf;
    float sum = 0.f;

    for (int i = 0; i < N; i++) {
        float c_re = l[i].re - r[i].re;
        float c_im = l[i].im - r[i].im;
        float p_re = lp[i].re - rp[i].re;
        float p_im = lp[i].im - rp[i].im;

        sum += sqrf(hypotf(c_re, c_im) - hypotf(p_re, p_im));
    }

    return sum;
}

static float calc_vad(float fc, float flr, float a)
{
    const float vad = a * (fc / (fc + flr) - 0.5f);

    return av_clipf(vad, 0.f, 1.f);
}

static void get_final(float *c, float *l,
                      float *r, float vad, int N,
                      float original, float enhance)
{
    AVComplexFloat *center = (AVComplexFloat *)c;
    AVComplexFloat *left   = (AVComplexFloat *)l;
    AVComplexFloat *right  = (AVComplexFloat *)r;

    for (int i = 0; i < N; i++) {
        float cP = sqrf(center[i].re) + sqrf(center[i].im);
        float lrP = sqrf(left[i].re - right[i].re) + sqrf(left[i].im - right[i].im);
        float G = cP / (cP + lrP + FLT_EPSILON);
        float re, im;

        re = center[i].re * (original + vad * G * enhance);
        im = center[i].im * (original + vad * G * enhance);

        center[i].re = re;
        center[i].im = im;
    }
}

static int de_stereo(AVFilterContext *ctx, AVFrame *out)
{
    AudioDialogueEnhanceContext *s = ctx->priv;
    float *center          = (float *)s->center_frame->extended_data[0];
    float *center_prev     = (float *)s->center_frame->extended_data[1];
    float *left_in         = (float *)s->in_frame->extended_data[0];
    float *right_in        = (float *)s->in_frame->extended_data[1];
    float *left_out        = (float *)s->out_dist_frame->extended_data[0];
    float *right_out       = (float *)s->out_dist_frame->extended_data[1];
    float *left_samples    = (float *)s->in->extended_data[0];
    float *right_samples   = (float *)s->in->extended_data[1];
    float *windowed_left   = (float *)s->windowed_frame->extended_data[0];
    float *windowed_right  = (float *)s->windowed_frame->extended_data[1];
    float *windowed_oleft  = (float *)s->windowed_out->extended_data[0];
    float *windowed_oright = (float *)s->windowed_out->extended_data[1];
    float *windowed_pleft  = (float *)s->windowed_prev->extended_data[0];
    float *windowed_pright = (float *)s->windowed_prev->extended_data[1];
    float *left_osamples   = (float *)out->extended_data[0];
    float *right_osamples  = (float *)out->extended_data[1];
    float *center_osamples = (float *)out->extended_data[2];
    const int offset = s->fft_size - s->overlap;
    float vad;

    // shift in/out buffers
    memmove(left_in, &left_in[s->overlap], offset * sizeof(float));
    memmove(right_in, &right_in[s->overlap], offset * sizeof(float));
    memmove(left_out, &left_out[s->overlap], offset * sizeof(float));
    memmove(right_out, &right_out[s->overlap], offset * sizeof(float));

    memcpy(&left_in[offset], left_samples, s->overlap * sizeof(float));
    memcpy(&right_in[offset], right_samples, s->overlap * sizeof(float));
    memset(&left_out[offset], 0, s->overlap * sizeof(float));
    memset(&right_out[offset], 0, s->overlap * sizeof(float));

    apply_window(s, left_in,  windowed_left,  0);
    apply_window(s, right_in, windowed_right, 0);

    s->tx_fn(s->tx_ctx[0], windowed_oleft,  windowed_left,  sizeof(float));
    s->tx_fn(s->tx_ctx[1], windowed_oright, windowed_right, sizeof(float));

    get_centere((AVComplexFloat *)windowed_oleft,
                (AVComplexFloat *)windowed_oright,
                (AVComplexFloat *)center,
                s->fft_size / 2 + 1);

    vad = calc_vad(flux(center, center_prev, s->fft_size / 2 + 1),
                   fluxlr(windowed_oleft, windowed_pleft,
                          windowed_oright, windowed_pright, s->fft_size / 2 + 1), s->voice);
    vad = vad * 0.1 + 0.9 * s->prev_vad;
    s->prev_vad = vad;

    memcpy(center_prev,     center,          s->fft_size * sizeof(float));
    memcpy(windowed_pleft,  windowed_oleft,  s->fft_size * sizeof(float));
    memcpy(windowed_pright, windowed_oright, s->fft_size * sizeof(float));

    get_final(center, windowed_oleft, windowed_oright, vad, s->fft_size / 2 + 1,
              s->original, s->enhance);

    s->itx_fn(s->itx_ctx, windowed_oleft, center, sizeof(float));

    apply_window(s, windowed_oleft, left_out,  1);

    for (int i = 0; i < s->overlap; i++) {
        // 4 times overlap with squared hanning window results in 1.5 time increase in amplitude
        if (!ctx->is_disabled)
            center_osamples[i] = left_out[i] / 1.5f;
        else
            center_osamples[i] = 0.f;
        left_osamples[i]  = left_in[i];
        right_osamples[i] = right_in[i];
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDialogueEnhanceContext *s = ctx->priv;
    AVFrame *out;
    int ret;

    out = ff_get_audio_buffer(outlink, s->overlap);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->in = in;
    de_stereo(ctx, out);

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
    AudioDialogueEnhanceContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->overlap, s->overlap, &in);
    if (ret < 0)
        return ret;

    if (ret > 0) {
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
    AudioDialogueEnhanceContext *s = ctx->priv;

    av_freep(&s->window);

    av_frame_free(&s->in_frame);
    av_frame_free(&s->center_frame);
    av_frame_free(&s->out_dist_frame);
    av_frame_free(&s->windowed_frame);
    av_frame_free(&s->windowed_out);
    av_frame_free(&s->windowed_prev);

    av_tx_uninit(&s->tx_ctx[0]);
    av_tx_uninit(&s->tx_ctx[1]);
    av_tx_uninit(&s->itx_ctx);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_dialoguenhance = {
    .name            = "dialoguenhance",
    .description     = NULL_IF_CONFIG_SMALL("Audio Dialogue Enhancement."),
    .priv_size       = sizeof(AudioDialogueEnhanceContext),
    .priv_class      = &dialoguenhance_class,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .activate        = activate,
    .process_command = ff_filter_process_command,
};
