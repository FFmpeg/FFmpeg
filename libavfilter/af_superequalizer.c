/*
 * Copyright (c) 2002 Naoki Shibata
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavcodec/avfft.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

#define NBANDS 17
#define M 15

typedef struct EqParameter {
    float lower, upper, gain;
} EqParameter;

typedef struct SuperEqualizerContext {
    const AVClass *class;

    EqParameter params[NBANDS + 1];

    float gains[NBANDS + 1];

    float fact[M + 1];
    float aa;
    float iza;
    float *ires, *irest;
    float *fsamples;
    int winlen, tabsize;

    AVFrame *in, *out;
    RDFTContext *rdft, *irdft;
} SuperEqualizerContext;

static const float bands[] = {
    65.406392, 92.498606, 130.81278, 184.99721, 261.62557, 369.99442, 523.25113, 739.9884, 1046.5023,
    1479.9768, 2093.0045, 2959.9536, 4186.0091, 5919.9072, 8372.0181, 11839.814, 16744.036
};

static float izero(SuperEqualizerContext *s, float x)
{
    float ret = 1;
    int m;

    for (m = 1; m <= M; m++) {
        float t;

        t = pow(x / 2, m) / s->fact[m];
        ret += t*t;
    }

    return ret;
}

static float hn_lpf(int n, float f, float fs)
{
    float t = 1 / fs;
    float omega = 2 * M_PI * f;

    if (n * omega * t == 0)
        return 2 * f * t;
    return 2 * f * t * sinf(n * omega * t) / (n * omega * t);
}

static float hn_imp(int n)
{
    return n == 0 ? 1.f : 0.f;
}

static float hn(int n, EqParameter *param, float fs)
{
    float ret, lhn;
    int i;

    lhn = hn_lpf(n, param[0].upper, fs);
    ret = param[0].gain*lhn;

    for (i = 1; i < NBANDS + 1 && param[i].upper < fs / 2; i++) {
        float lhn2 = hn_lpf(n, param[i].upper, fs);
        ret += param[i].gain * (lhn2 - lhn);
        lhn = lhn2;
    }

    ret += param[i].gain * (hn_imp(n) - lhn);

    return ret;
}

static float alpha(float a)
{
    if (a <= 21)
        return 0;
    if (a <= 50)
        return .5842f * pow(a - 21, 0.4f) + 0.07886f * (a - 21);
    return .1102f * (a - 8.7f);
}

static float win(SuperEqualizerContext *s, float n, int N)
{
    return izero(s, alpha(s->aa) * sqrtf(1 - 4 * n * n / ((N - 1) * (N - 1)))) / s->iza;
}

static void process_param(float *bc, EqParameter *param, float fs)
{
    int i;

    for (i = 0; i <= NBANDS; i++) {
        param[i].lower = i == 0 ? 0 : bands[i - 1];
        param[i].upper = i == NBANDS ? fs : bands[i];
        param[i].gain  = bc[i];
    }
}

static int equ_init(SuperEqualizerContext *s, int wb)
{
    int i,j;

    s->rdft  = av_rdft_init(wb, DFT_R2C);
    s->irdft = av_rdft_init(wb, IDFT_C2R);
    if (!s->rdft || !s->irdft)
        return AVERROR(ENOMEM);

    s->aa = 96;
    s->winlen = (1 << (wb-1))-1;
    s->tabsize  = 1 << wb;

    s->ires     = av_calloc(s->tabsize, sizeof(float));
    s->irest    = av_calloc(s->tabsize, sizeof(float));
    s->fsamples = av_calloc(s->tabsize, sizeof(float));

    for (i = 0; i <= M; i++) {
        s->fact[i] = 1;
        for (j = 1; j <= i; j++)
            s->fact[i] *= j;
    }

    s->iza = izero(s, alpha(s->aa));

    return 0;
}

static void make_fir(SuperEqualizerContext *s, float *lbc, float *rbc, EqParameter *param, float fs)
{
    const int winlen = s->winlen;
    const int tabsize = s->tabsize;
    float *nires;
    int i;

    if (fs <= 0)
        return;

    process_param(lbc, param, fs);
    for (i = 0; i < winlen; i++)
        s->irest[i] = hn(i - winlen / 2, param, fs) * win(s, i - winlen / 2, winlen);
    for (; i < tabsize; i++)
        s->irest[i] = 0;

    av_rdft_calc(s->rdft, s->irest);
    nires = s->ires;
    for (i = 0; i < tabsize; i++)
        nires[i] = s->irest[i];
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    SuperEqualizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const float *ires = s->ires;
    float *fsamples = s->fsamples;
    int ch, i;

    AVFrame *out = ff_get_audio_buffer(outlink, s->winlen);
    float *src, *dst, *ptr;

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    for (ch = 0; ch < in->channels; ch++) {
        ptr = (float *)out->extended_data[ch];
        dst = (float *)s->out->extended_data[ch];
        src = (float *)in->extended_data[ch];

        for (i = 0; i < s->winlen; i++)
            fsamples[i] = src[i];
        for (; i < s->tabsize; i++)
            fsamples[i] = 0;

        av_rdft_calc(s->rdft, fsamples);

        fsamples[0] = ires[0] * fsamples[0];
        fsamples[1] = ires[1] * fsamples[1];
        for (i = 1; i < s->tabsize / 2; i++) {
            float re, im;

            re = ires[i*2  ] * fsamples[i*2] - ires[i*2+1] * fsamples[i*2+1];
            im = ires[i*2+1] * fsamples[i*2] + ires[i*2  ] * fsamples[i*2+1];

            fsamples[i*2  ] = re;
            fsamples[i*2+1] = im;
        }

        av_rdft_calc(s->irdft, fsamples);

        for (i = 0; i < s->winlen; i++)
            dst[i] += fsamples[i] / s->tabsize * 2;
        for (i = s->winlen; i < s->tabsize; i++)
            dst[i]  = fsamples[i] / s->tabsize * 2;
        for (i = 0; i < s->winlen; i++)
            ptr[i] = dst[i];
        for (i = 0; i < s->winlen; i++)
            dst[i] = dst[i+s->winlen];
    }

    out->pts = in->pts;
    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    SuperEqualizerContext *s = ctx->priv;

    return equ_init(s, 14);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SuperEqualizerContext *s = ctx->priv;

    inlink->partial_buf_size =
    inlink->min_samples =
    inlink->max_samples = s->winlen;

    s->out = ff_get_audio_buffer(inlink, s->tabsize);
    if (!s->out)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SuperEqualizerContext *s = ctx->priv;

    make_fir(s, s->gains, s->gains, s->params, outlink->sample_rate);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SuperEqualizerContext *s = ctx->priv;

    av_frame_free(&s->out);
    av_freep(&s->irest);
    av_freep(&s->ires);
    av_freep(&s->fsamples);
    av_rdft_end(s->rdft);
    av_rdft_end(s->irdft);
}

static const AVFilterPad superequalizer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad superequalizer_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(SuperEqualizerContext, x)

static const AVOption superequalizer_options[] = {
    {  "1b", "set 65Hz band gain",    OFFSET(gains [0]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "2b", "set 92Hz band gain",    OFFSET(gains [1]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "3b", "set 131Hz band gain",   OFFSET(gains [2]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "4b", "set 185Hz band gain",   OFFSET(gains [3]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "5b", "set 262Hz band gain",   OFFSET(gains [4]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "6b", "set 370Hz band gain",   OFFSET(gains [5]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "7b", "set 523Hz band gain",   OFFSET(gains [6]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "8b", "set 740Hz band gain",   OFFSET(gains [7]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    {  "9b", "set 1047Hz band gain",  OFFSET(gains [8]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "10b", "set 1480Hz band gain",  OFFSET(gains [9]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "11b", "set 2093Hz band gain",  OFFSET(gains[10]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "12b", "set 2960Hz band gain",  OFFSET(gains[11]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "13b", "set 4186Hz band gain",  OFFSET(gains[12]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "14b", "set 5920Hz band gain",  OFFSET(gains[13]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "15b", "set 8372Hz band gain",  OFFSET(gains[14]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "16b", "set 11840Hz band gain", OFFSET(gains[15]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "17b", "set 16744Hz band gain", OFFSET(gains[16]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { "18b", "set 20000Hz band gain", OFFSET(gains[17]), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 20, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(superequalizer);

AVFilter ff_af_superequalizer = {
    .name          = "superequalizer",
    .description   = NULL_IF_CONFIG_SMALL("Apply 18 band equalization filter."),
    .priv_size     = sizeof(SuperEqualizerContext),
    .priv_class    = &superequalizer_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = superequalizer_inputs,
    .outputs       = superequalizer_outputs,
};
