/*
 * Copyright (c) 2008-2009 Rob Sykes <robs@users.sourceforge.net>
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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"

typedef struct SincContext {
    const AVClass *class;

    int sample_rate, nb_samples;
    float att, beta, phase, Fc0, Fc1, tbw0, tbw1;
    int num_taps[2];
    int round;

    int n, rdft_len;
    float *coeffs;
    int64_t pts;

    AVTXContext *tx, *itx;
    av_tx_fn tx_fn, itx_fn;
} SincContext;

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    SincContext *s = ctx->priv;
    const float *coeffs = s->coeffs;
    AVFrame *frame = NULL;
    int nb_samples;

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    nb_samples = FFMIN(s->nb_samples, s->n - s->pts);
    if (nb_samples <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!(frame = ff_get_audio_buffer(outlink, nb_samples)))
        return AVERROR(ENOMEM);

    memcpy(frame->data[0], coeffs + s->pts, nb_samples * sizeof(float));

    frame->pts = s->pts;
    s->pts    += nb_samples;

    return ff_filter_frame(outlink, frame);
}

static int query_formats(AVFilterContext *ctx)
{
    SincContext *s = ctx->priv;
    static const AVChannelLayout chlayouts[] = { AV_CHANNEL_LAYOUT_MONO, { 0 } };
    int sample_rates[] = { s->sample_rate, -1 };
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLT,
                                                       AV_SAMPLE_FMT_NONE };
    int ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list(ctx, chlayouts);
    if (ret < 0)
        return ret;

    return ff_set_common_samplerates_from_list(ctx, sample_rates);
}

static float *make_lpf(int num_taps, float Fc, float beta, float rho,
                       float scale, int dc_norm)
{
    int i, m = num_taps - 1;
    float *h = av_calloc(num_taps, sizeof(*h)), sum = 0;
    float mult = scale / av_bessel_i0(beta), mult1 = 1.f / (.5f * m + rho);

    if (!h)
        return NULL;

    av_assert0(Fc >= 0 && Fc <= 1);

    for (i = 0; i <= m / 2; i++) {
        float z = i - .5f * m, x = z * M_PI, y = z * mult1;
        h[i] = x ? sinf(Fc * x) / x : Fc;
        sum += h[i] *= av_bessel_i0(beta * sqrtf(1.f - y * y)) * mult;
        if (m - i != i) {
            h[m - i] = h[i];
            sum += h[i];
        }
    }

    for (i = 0; dc_norm && i < num_taps; i++)
        h[i] *= scale / sum;

    return h;
}

static float kaiser_beta(float att, float tr_bw)
{
    if (att >= 60.f) {
        static const float coefs[][4] = {
            {-6.784957e-10, 1.02856e-05, 0.1087556, -0.8988365 + .001},
            {-6.897885e-10, 1.027433e-05, 0.10876, -0.8994658 + .002},
            {-1.000683e-09, 1.030092e-05, 0.1087677, -0.9007898 + .003},
            {-3.654474e-10, 1.040631e-05, 0.1087085, -0.8977766 + .006},
            {8.106988e-09, 6.983091e-06, 0.1091387, -0.9172048 + .015},
            {9.519571e-09, 7.272678e-06, 0.1090068, -0.9140768 + .025},
            {-5.626821e-09, 1.342186e-05, 0.1083999, -0.9065452 + .05},
            {-9.965946e-08, 5.073548e-05, 0.1040967, -0.7672778 + .085},
            {1.604808e-07, -5.856462e-05, 0.1185998, -1.34824 + .1},
            {-1.511964e-07, 6.363034e-05, 0.1064627, -0.9876665 + .18},
        };
        float realm = logf(tr_bw / .0005f) / logf(2.f);
        float const *c0 = coefs[av_clip((int)realm, 0, FF_ARRAY_ELEMS(coefs) - 1)];
        float const *c1 = coefs[av_clip(1 + (int)realm, 0, FF_ARRAY_ELEMS(coefs) - 1)];
        float b0 = ((c0[0] * att + c0[1]) * att + c0[2]) * att + c0[3];
        float b1 = ((c1[0] * att + c1[1]) * att + c1[2]) * att + c1[3];

        return b0 + (b1 - b0) * (realm - (int)realm);
    }
    if (att > 50.f)
        return .1102f * (att - 8.7f);
    if (att > 20.96f)
        return .58417f * powf(att - 20.96f, .4f) + .07886f * (att - 20.96f);
    return 0;
}

static void kaiser_params(float att, float Fc, float tr_bw, float *beta, int *num_taps)
{
    *beta = *beta < 0.f ? kaiser_beta(att, tr_bw * .5f / Fc): *beta;
    att = att < 60.f ? (att - 7.95f) / (2.285f * M_PI * 2.f) :
        ((.0007528358f-1.577737e-05 * *beta) * *beta + 0.6248022f) * *beta + .06186902f;
    *num_taps = !*num_taps ? ceilf(att/tr_bw + 1) : *num_taps;
}

static float *lpf(float Fn, float Fc, float tbw, int *num_taps, float att, float *beta, int round)
{
    int n = *num_taps;

    if ((Fc /= Fn) <= 0.f || Fc >= 1.f) {
        *num_taps = 0;
        return NULL;
    }

    att = att ? att : 120.f;

    kaiser_params(att, Fc, (tbw ? tbw / Fn : .05f) * .5f, beta, num_taps);

    if (!n) {
        n = *num_taps;
        *num_taps = av_clip(n, 11, 32767);
        if (round)
            *num_taps = 1 + 2 * (int)((int)((*num_taps / 2) * Fc + .5f) / Fc + .5f);
    }

    return make_lpf(*num_taps |= 1, Fc, *beta, 0.f, 1.f, 0);
}

static void invert(float *h, int n)
{
    for (int i = 0; i < n; i++)
        h[i] = -h[i];

    h[(n - 1) / 2] += 1;
}

#define SQR(a) ((a) * (a))

static float safe_log(float x)
{
    av_assert0(x >= 0);
    if (x)
        return logf(x);
    return -26;
}

static int fir_to_phase(SincContext *s, float **h, int *len, int *post_len, float phase)
{
    float *pi_wraps, *work, phase1 = (phase > 50.f ? 100.f - phase : phase) / 50.f;
    int i, work_len, begin, end, imp_peak = 0, peak = 0, ret;
    float imp_sum = 0, peak_imp_sum = 0, scale = 1.f;
    float prev_angle2 = 0, cum_2pi = 0, prev_angle1 = 0, cum_1pi = 0;

    for (i = *len, work_len = 2 * 2 * 8; i > 1; work_len <<= 1, i >>= 1);

    /* The first part is for work (+2 for (UN)PACK), the latter for pi_wraps. */
    work = av_calloc((work_len + 2) + (work_len / 2 + 1), sizeof(float));
    if (!work)
        return AVERROR(ENOMEM);
    pi_wraps = &work[work_len + 2];

    memcpy(work, *h, *len * sizeof(*work));

    av_tx_uninit(&s->tx);
    av_tx_uninit(&s->itx);
    ret = av_tx_init(&s->tx,  &s->tx_fn,  AV_TX_FLOAT_RDFT, 0, work_len, &scale, AV_TX_INPLACE);
    if (ret < 0)
        goto fail;
    ret = av_tx_init(&s->itx, &s->itx_fn, AV_TX_FLOAT_RDFT, 1, work_len, &scale, AV_TX_INPLACE);
    if (ret < 0)
        goto fail;

    s->tx_fn(s->tx, work, work, sizeof(float));   /* Cepstral: */

    for (i = 0; i <= work_len; i += 2) {
        float angle = atan2f(work[i + 1], work[i]);
        float detect = 2 * M_PI;
        float delta = angle - prev_angle2;
        float adjust = detect * ((delta < -detect * .7f) - (delta > detect * .7f));

        prev_angle2 = angle;
        cum_2pi += adjust;
        angle += cum_2pi;
        detect = M_PI;
        delta = angle - prev_angle1;
        adjust = detect * ((delta < -detect * .7f) - (delta > detect * .7f));
        prev_angle1 = angle;
        cum_1pi += fabsf(adjust);        /* fabs for when 2pi and 1pi have combined */
        pi_wraps[i >> 1] = cum_1pi;

        work[i] = safe_log(sqrtf(SQR(work[i]) + SQR(work[i + 1])));
        work[i + 1] = 0;
    }

    s->itx_fn(s->itx, work, work, sizeof(AVComplexFloat));

    for (i = 0; i < work_len; i++)
        work[i] *= 2.f / work_len;

    for (i = 1; i < work_len / 2; i++) {        /* Window to reject acausal components */
        work[i] *= 2;
        work[i + work_len / 2] = 0;
    }
    s->tx_fn(s->tx, work, work, sizeof(float));

    for (i = 2; i < work_len; i += 2)   /* Interpolate between linear & min phase */
        work[i + 1] = phase1 * i / work_len * pi_wraps[work_len >> 1] + (1 - phase1) * (work[i + 1] + pi_wraps[i >> 1]) - pi_wraps[i >> 1];

    work[0] = exp(work[0]);
    work[1] = exp(work[1]);
    for (i = 2; i < work_len; i += 2) {
        float x = expf(work[i]);

        work[i    ] = x * cosf(work[i + 1]);
        work[i + 1] = x * sinf(work[i + 1]);
    }

    s->itx_fn(s->itx, work, work, sizeof(AVComplexFloat));
    for (i = 0; i < work_len; i++)
        work[i] *= 2.f / work_len;

    /* Find peak pos. */
    for (i = 0; i <= (int) (pi_wraps[work_len >> 1] / M_PI + .5f); i++) {
        imp_sum += work[i];
        if (fabs(imp_sum) > fabs(peak_imp_sum)) {
            peak_imp_sum = imp_sum;
            peak = i;
        }
        if (work[i] > work[imp_peak])   /* For debug check only */
            imp_peak = i;
    }

    while (peak && fabsf(work[peak - 1]) > fabsf(work[peak]) && (work[peak - 1] * work[peak] > 0)) {
        peak--;
    }

    if (!phase1) {
        begin = 0;
    } else if (phase1 == 1) {
        begin = peak - *len / 2;
    } else {
        begin = (.997f - (2 - phase1) * .22f) * *len + .5f;
        end = (.997f + (0 - phase1) * .22f) * *len + .5f;
        begin = peak - (begin & ~3);
        end = peak + 1 + ((end + 3) & ~3);
        *len = end - begin;
        *h = av_realloc_f(*h, *len, sizeof(**h));
        if (!*h) {
            av_free(work);
            return AVERROR(ENOMEM);
        }
    }

    for (i = 0; i < *len; i++) {
        (*h)[i] = work[(begin + (phase > 50.f ? *len - 1 - i : i) + work_len) & (work_len - 1)];
    }
    *post_len = phase > 50 ? peak - begin : begin + *len - (peak + 1);

    av_log(s, AV_LOG_DEBUG, "%d nPI=%g peak-sum@%i=%g (val@%i=%g); len=%i post=%i (%g%%)\n",
           work_len, pi_wraps[work_len >> 1] / M_PI, peak, peak_imp_sum, imp_peak,
           work[imp_peak], *len, *post_len, 100.f - 100.f * *post_len / (*len - 1));

fail:
    av_free(work);

    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SincContext *s = ctx->priv;
    float Fn = s->sample_rate * .5f;
    float *h[2];
    int i, n, post_peak, longer;

    outlink->sample_rate = s->sample_rate;
    s->pts = 0;

    if (s->Fc0 >= Fn || s->Fc1 >= Fn) {
        av_log(ctx, AV_LOG_ERROR,
               "filter frequency must be less than %d/2.\n", s->sample_rate);
        return AVERROR(EINVAL);
    }

    h[0] = lpf(Fn, s->Fc0, s->tbw0, &s->num_taps[0], s->att, &s->beta, s->round);
    h[1] = lpf(Fn, s->Fc1, s->tbw1, &s->num_taps[1], s->att, &s->beta, s->round);

    if (h[0])
        invert(h[0], s->num_taps[0]);

    longer = s->num_taps[1] > s->num_taps[0];
    n = s->num_taps[longer];

    if (h[0] && h[1]) {
        for (i = 0; i < s->num_taps[!longer]; i++)
            h[longer][i + (n - s->num_taps[!longer]) / 2] += h[!longer][i];

        if (s->Fc0 < s->Fc1)
            invert(h[longer], n);

        av_free(h[!longer]);
    }

    if (s->phase != 50.f) {
        int ret = fir_to_phase(s, &h[longer], &n, &post_peak, s->phase);
        if (ret < 0)
            return ret;
    } else {
        post_peak = n >> 1;
    }

    s->n = 1 << (av_log2(n) + 1);
    s->rdft_len = 1 << av_log2(n);
    s->coeffs = av_calloc(s->n, sizeof(*s->coeffs));
    if (!s->coeffs)
        return AVERROR(ENOMEM);

    for (i = 0; i < n; i++)
        s->coeffs[i] = h[longer][i];
    av_free(h[longer]);

    av_tx_uninit(&s->tx);
    av_tx_uninit(&s->itx);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SincContext *s = ctx->priv;

    av_freep(&s->coeffs);
    av_tx_uninit(&s->tx);
    av_tx_uninit(&s->itx);
}

static const AVFilterPad sinc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(SincContext, x)

static const AVOption sinc_options[] = {
    { "sample_rate", "set sample rate",                               OFFSET(sample_rate), AV_OPT_TYPE_INT,   {.i64=44100},  1, INT_MAX, AF },
    { "r",           "set sample rate",                               OFFSET(sample_rate), AV_OPT_TYPE_INT,   {.i64=44100},  1, INT_MAX, AF },
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples),  AV_OPT_TYPE_INT,   {.i64=1024},   1, INT_MAX, AF },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples),  AV_OPT_TYPE_INT,   {.i64=1024},   1, INT_MAX, AF },
    { "hp",          "set high-pass filter frequency",                OFFSET(Fc0),         AV_OPT_TYPE_FLOAT, {.dbl=0},      0, INT_MAX, AF },
    { "lp",          "set low-pass filter frequency",                 OFFSET(Fc1),         AV_OPT_TYPE_FLOAT, {.dbl=0},      0, INT_MAX, AF },
    { "phase",       "set filter phase response",                     OFFSET(phase),       AV_OPT_TYPE_FLOAT, {.dbl=50},     0,     100, AF },
    { "beta",        "set kaiser window beta",                        OFFSET(beta),        AV_OPT_TYPE_FLOAT, {.dbl=-1},    -1,     256, AF },
    { "att",         "set stop-band attenuation",                     OFFSET(att),         AV_OPT_TYPE_FLOAT, {.dbl=120},   40,     180, AF },
    { "round",       "enable rounding",                               OFFSET(round),       AV_OPT_TYPE_BOOL,  {.i64=0},      0,       1, AF },
    { "hptaps",      "set number of taps for high-pass filter",       OFFSET(num_taps[0]), AV_OPT_TYPE_INT,   {.i64=0},      0,   32768, AF },
    { "lptaps",      "set number of taps for low-pass filter",        OFFSET(num_taps[1]), AV_OPT_TYPE_INT,   {.i64=0},      0,   32768, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(sinc);

const AVFilter ff_asrc_sinc = {
    .name          = "sinc",
    .description   = NULL_IF_CONFIG_SMALL("Generate a sinc kaiser-windowed low-pass, high-pass, band-pass, or band-reject FIR coefficients."),
    .priv_size     = sizeof(SincContext),
    .priv_class    = &sinc_class,
    .uninit        = uninit,
    .activate      = activate,
    .inputs        = NULL,
    FILTER_OUTPUTS(sinc_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
