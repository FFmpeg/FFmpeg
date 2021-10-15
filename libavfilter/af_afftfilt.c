/*
 * Copyright (c) 2016 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
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

#include "libavutil/audio_fifo.h"
#include "libavutil/avstring.h"
#include "libavfilter/internal.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/tx.h"
#include "audio.h"
#include "filters.h"
#include "window_func.h"

typedef struct AFFTFiltContext {
    const AVClass *class;
    char *real_str;
    char *img_str;
    int fft_size;

    AVTXContext *fft, *ifft;
    av_tx_fn  tx_fn, itx_fn;
    AVComplexFloat **fft_in;
    AVComplexFloat **fft_out;
    AVComplexFloat **fft_temp;
    int nb_exprs;
    int channels;
    int window_size;
    AVExpr **real;
    AVExpr **imag;
    AVAudioFifo *fifo;
    int64_t pts;
    int hop_size;
    float overlap;
    AVFrame *buffer;
    int eof;
    int win_func;
    float *window_func_lut;
} AFFTFiltContext;

static const char *const var_names[] = {            "sr",     "b",       "nb",        "ch",        "chs",   "pts",     "re",     "im", NULL };
enum                                   { VAR_SAMPLE_RATE, VAR_BIN, VAR_NBBINS, VAR_CHANNEL, VAR_CHANNELS, VAR_PTS, VAR_REAL, VAR_IMAG, VAR_VARS_NB };

#define OFFSET(x) offsetof(AFFTFiltContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption afftfilt_options[] = {
    { "real", "set channels real expressions",       OFFSET(real_str), AV_OPT_TYPE_STRING, {.str = "re" }, 0, 0, A },
    { "imag", "set channels imaginary expressions",  OFFSET(img_str),  AV_OPT_TYPE_STRING, {.str = "im" }, 0, 0, A },
    { "win_size", "set window size", OFFSET(fft_size), AV_OPT_TYPE_INT, {.i64=4096}, 16, 131072, A },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), A, WFUNC_HANNING),
    { "overlap", "set window overlap", OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0,  1, A },
    { NULL },
};

AVFILTER_DEFINE_CLASS(afftfilt);

static inline double getreal(void *priv, double x, double ch)
{
    AFFTFiltContext *s = priv;
    int ich, ix;

    ich = av_clip(ch, 0, s->nb_exprs - 1);
    ix = av_clip(x, 0, s->window_size / 2);

    return s->fft_out[ich][ix].re;
}

static inline double getimag(void *priv, double x, double ch)
{
    AFFTFiltContext *s = priv;
    int ich, ix;

    ich = av_clip(ch, 0, s->nb_exprs - 1);
    ix = av_clip(x, 0, s->window_size / 2);

    return s->fft_out[ich][ix].im;
}

static double realf(void *priv, double x, double ch) { return getreal(priv, x, ch); }
static double imagf(void *priv, double x, double ch) { return getimag(priv, x, ch); }

static const char *const func2_names[]    = { "real", "imag", NULL };
static double (*const func2[])(void *, double, double) = {  realf,  imagf, NULL };

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AFFTFiltContext *s = ctx->priv;
    char *saveptr = NULL;
    int ret = 0, ch;
    float overlap, scale;
    char *args;
    const char *last_expr = "1";
    int buf_size;

    s->channels = inlink->channels;
    s->pts  = AV_NOPTS_VALUE;
    ret = av_tx_init(&s->fft, &s->tx_fn, AV_TX_FLOAT_FFT, 0, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    ret = av_tx_init(&s->ifft, &s->itx_fn, AV_TX_FLOAT_FFT, 1, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    s->window_size = s->fft_size;
    buf_size = FFALIGN(s->window_size, av_cpu_max_align());

    s->fft_in = av_calloc(inlink->channels, sizeof(*s->fft_in));
    if (!s->fft_in)
        return AVERROR(ENOMEM);

    s->fft_out = av_calloc(inlink->channels, sizeof(*s->fft_out));
    if (!s->fft_out)
        return AVERROR(ENOMEM);

    s->fft_temp = av_calloc(inlink->channels, sizeof(*s->fft_temp));
    if (!s->fft_temp)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < inlink->channels; ch++) {
        s->fft_in[ch] = av_calloc(buf_size, sizeof(**s->fft_in));
        if (!s->fft_in[ch])
            return AVERROR(ENOMEM);

        s->fft_out[ch] = av_calloc(buf_size, sizeof(**s->fft_out));
        if (!s->fft_out[ch])
            return AVERROR(ENOMEM);

        s->fft_temp[ch] = av_calloc(buf_size, sizeof(**s->fft_temp));
        if (!s->fft_temp[ch])
            return AVERROR(ENOMEM);
    }

    s->real = av_calloc(inlink->channels, sizeof(*s->real));
    if (!s->real)
        return AVERROR(ENOMEM);

    s->imag = av_calloc(inlink->channels, sizeof(*s->imag));
    if (!s->imag)
        return AVERROR(ENOMEM);

    args = av_strdup(s->real_str);
    if (!args)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < inlink->channels; ch++) {
        char *arg = av_strtok(ch == 0 ? args : NULL, "|", &saveptr);

        ret = av_expr_parse(&s->real[ch], arg ? arg : last_expr, var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            goto fail;
        if (arg)
            last_expr = arg;
        s->nb_exprs++;
    }

    av_freep(&args);

    args = av_strdup(s->img_str ? s->img_str : s->real_str);
    if (!args)
        return AVERROR(ENOMEM);

    saveptr = NULL;
    last_expr = "1";
    for (ch = 0; ch < inlink->channels; ch++) {
        char *arg = av_strtok(ch == 0 ? args : NULL, "|", &saveptr);

        ret = av_expr_parse(&s->imag[ch], arg ? arg : last_expr, var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            goto fail;
        if (arg)
            last_expr = arg;
    }

    av_freep(&args);

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->channels, s->window_size);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    s->window_func_lut = av_realloc_f(s->window_func_lut, s->window_size,
                                      sizeof(*s->window_func_lut));
    if (!s->window_func_lut)
        return AVERROR(ENOMEM);
    generate_window_func(s->window_func_lut, s->window_size, s->win_func, &overlap);
    if (s->overlap == 1)
        s->overlap = overlap;

    s->hop_size = s->window_size * (1 - s->overlap);
    if (s->hop_size <= 0)
        return AVERROR(EINVAL);

    s->buffer = ff_get_audio_buffer(inlink, s->window_size * 2);
    if (!s->buffer)
        return AVERROR(ENOMEM);

fail:
    av_freep(&args);

    return ret;
}

static int filter_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AFFTFiltContext *s = ctx->priv;
    const int window_size = s->window_size;
    const float f = 1. / (s->window_size / 2);
    double values[VAR_VARS_NB];
    AVFrame *out, *in = NULL;
    int ch, n, ret, i;

    if (!in) {
        in = ff_get_audio_buffer(outlink, window_size);
        if (!in)
            return AVERROR(ENOMEM);
    }

    ret = av_audio_fifo_peek(s->fifo, (void **)in->extended_data, window_size);
    if (ret < 0)
        goto fail;

    for (ch = 0; ch < inlink->channels; ch++) {
        const float *src = (float *)in->extended_data[ch];
        AVComplexFloat *fft_in = s->fft_in[ch];

        for (n = 0; n < in->nb_samples; n++) {
            fft_in[n].re = src[n] * s->window_func_lut[n];
            fft_in[n].im = 0;
        }

        for (; n < window_size; n++) {
            fft_in[n].re = 0;
            fft_in[n].im = 0;
        }
    }

    values[VAR_PTS]         = s->pts;
    values[VAR_SAMPLE_RATE] = inlink->sample_rate;
    values[VAR_NBBINS]      = window_size / 2;
    values[VAR_CHANNELS]    = inlink->channels;

    for (ch = 0; ch < inlink->channels; ch++) {
        AVComplexFloat *fft_in = s->fft_in[ch];
        AVComplexFloat *fft_out = s->fft_out[ch];

        s->tx_fn(s->fft, fft_out, fft_in, sizeof(float));
    }

    for (ch = 0; ch < inlink->channels; ch++) {
        AVComplexFloat *fft_out = s->fft_out[ch];
        AVComplexFloat *fft_temp = s->fft_temp[ch];
        float *buf = (float *)s->buffer->extended_data[ch];
        int x;
        values[VAR_CHANNEL] = ch;

        if (ctx->is_disabled) {
            for (n = 0; n <= window_size / 2; n++) {
                fft_temp[n].re = fft_out[n].re;
                fft_temp[n].im = fft_out[n].im;
            }
        } else {
            for (n = 0; n <= window_size / 2; n++) {
                float fr, fi;

                values[VAR_BIN] = n;
                values[VAR_REAL] = fft_out[n].re;
                values[VAR_IMAG] = fft_out[n].im;

                fr = av_expr_eval(s->real[ch], values, s);
                fi = av_expr_eval(s->imag[ch], values, s);

                fft_temp[n].re = fr;
                fft_temp[n].im = fi;
            }
        }

        for (n = window_size / 2 + 1, x = window_size / 2 - 1; n < window_size; n++, x--) {
            fft_temp[n].re =  fft_temp[x].re;
            fft_temp[n].im = -fft_temp[x].im;
        }

        s->itx_fn(s->ifft, fft_out, fft_temp, sizeof(float));

        for (i = 0; i < window_size; i++) {
            buf[i] += s->fft_out[ch][i].re * f;
        }
    }

    out = ff_get_audio_buffer(outlink, s->hop_size);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out->pts = s->pts;
    s->pts += av_rescale_q(s->hop_size, (AVRational){1, outlink->sample_rate}, outlink->time_base);

    for (ch = 0; ch < inlink->channels; ch++) {
        float *dst = (float *)out->extended_data[ch];
        float *buf = (float *)s->buffer->extended_data[ch];

        for (n = 0; n < s->hop_size; n++)
            dst[n] = buf[n] * (1.f - s->overlap);
        memmove(buf, buf + s->hop_size, window_size * 4);
    }

    ret = ff_filter_frame(outlink, out);
    if (ret < 0)
        goto fail;

    av_audio_fifo_drain(s->fifo, s->hop_size);

fail:
    av_frame_free(&in);
    return ret < 0 ? ret : 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AFFTFiltContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof && av_audio_fifo_size(s->fifo) < s->window_size) {
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;

        if (ret > 0) {
            ret = av_audio_fifo_write(s->fifo, (void **)in->extended_data,
                                      in->nb_samples);
            if (ret >= 0 && s->pts == AV_NOPTS_VALUE)
                s->pts = in->pts;

            av_frame_free(&in);
            if (ret < 0)
                return ret;
        }
    }

    if ((av_audio_fifo_size(s->fifo) >= s->window_size) ||
        (av_audio_fifo_size(s->fifo) > 0 && s->eof)) {
        ret = filter_frame(inlink);
        if (av_audio_fifo_size(s->fifo) >= s->window_size)
            ff_filter_set_ready(ctx, 100);
        return ret;
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            s->eof = 1;
            if (av_audio_fifo_size(s->fifo) >= 0) {
                ff_filter_set_ready(ctx, 100);
                return 0;
            }
        }
    }

    if (s->eof && av_audio_fifo_size(s->fifo) <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!s->eof)
        FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AFFTFiltContext *s = ctx->priv;
    int i;

    av_tx_uninit(&s->fft);
    av_tx_uninit(&s->ifft);

    for (i = 0; i < s->channels; i++) {
        if (s->fft_in)
            av_freep(&s->fft_in[i]);
        if (s->fft_out)
            av_freep(&s->fft_out[i]);
        if (s->fft_temp)
            av_freep(&s->fft_temp[i]);
    }
    av_freep(&s->fft_in);
    av_freep(&s->fft_out);
    av_freep(&s->fft_temp);

    for (i = 0; i < s->nb_exprs; i++) {
        av_expr_free(s->real[i]);
        av_expr_free(s->imag[i]);
    }

    av_freep(&s->real);
    av_freep(&s->imag);
    av_frame_free(&s->buffer);
    av_freep(&s->window_func_lut);

    av_audio_fifo_free(s->fifo);
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

const AVFilter ff_af_afftfilt = {
    .name            = "afftfilt",
    .description     = NULL_IF_CONFIG_SMALL("Apply arbitrary expressions to samples in frequency domain."),
    .priv_size       = sizeof(AFFTFiltContext),
    .priv_class      = &afftfilt_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .activate        = activate,
    .uninit          = uninit,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
