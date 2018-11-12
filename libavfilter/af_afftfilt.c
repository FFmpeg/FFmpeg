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
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libavutil/eval.h"
#include "audio.h"
#include "window_func.h"

typedef struct AFFTFiltContext {
    const AVClass *class;
    char *real_str;
    char *img_str;
    int fft_bits;

    FFTContext *fft, *ifft;
    FFTComplex **fft_data;
    FFTComplex **fft_temp;
    int nb_exprs;
    int window_size;
    AVExpr **real;
    AVExpr **imag;
    AVAudioFifo *fifo;
    int64_t pts;
    int hop_size;
    float overlap;
    AVFrame *buffer;
    int start, end;
    int win_func;
    float win_scale;
    float *window_func_lut;
} AFFTFiltContext;

static const char *const var_names[] = {            "sr",     "b",       "nb",        "ch",        "chs",   "pts",     "re",     "im", NULL };
enum                                   { VAR_SAMPLE_RATE, VAR_BIN, VAR_NBBINS, VAR_CHANNEL, VAR_CHANNELS, VAR_PTS, VAR_REAL, VAR_IMAG, VAR_VARS_NB };

#define OFFSET(x) offsetof(AFFTFiltContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption afftfilt_options[] = {
    { "real", "set channels real expressions",       OFFSET(real_str), AV_OPT_TYPE_STRING, {.str = "re" }, 0, 0, A },
    { "imag", "set channels imaginary expressions",  OFFSET(img_str),  AV_OPT_TYPE_STRING, {.str = "im" }, 0, 0, A },
    { "win_size", "set window size", OFFSET(fft_bits), AV_OPT_TYPE_INT, {.i64=12}, 4, 17, A, "fft" },
        { "w16",    0, 0, AV_OPT_TYPE_CONST, {.i64=4},  0, 0, A, "fft" },
        { "w32",    0, 0, AV_OPT_TYPE_CONST, {.i64=5},  0, 0, A, "fft" },
        { "w64",    0, 0, AV_OPT_TYPE_CONST, {.i64=6},  0, 0, A, "fft" },
        { "w128",   0, 0, AV_OPT_TYPE_CONST, {.i64=7},  0, 0, A, "fft" },
        { "w256",   0, 0, AV_OPT_TYPE_CONST, {.i64=8},  0, 0, A, "fft" },
        { "w512",   0, 0, AV_OPT_TYPE_CONST, {.i64=9},  0, 0, A, "fft" },
        { "w1024",  0, 0, AV_OPT_TYPE_CONST, {.i64=10}, 0, 0, A, "fft" },
        { "w2048",  0, 0, AV_OPT_TYPE_CONST, {.i64=11}, 0, 0, A, "fft" },
        { "w4096",  0, 0, AV_OPT_TYPE_CONST, {.i64=12}, 0, 0, A, "fft" },
        { "w8192",  0, 0, AV_OPT_TYPE_CONST, {.i64=13}, 0, 0, A, "fft" },
        { "w16384", 0, 0, AV_OPT_TYPE_CONST, {.i64=14}, 0, 0, A, "fft" },
        { "w32768", 0, 0, AV_OPT_TYPE_CONST, {.i64=15}, 0, 0, A, "fft" },
        { "w65536", 0, 0, AV_OPT_TYPE_CONST, {.i64=16}, 0, 0, A, "fft" },
        { "w131072",0, 0, AV_OPT_TYPE_CONST, {.i64=17}, 0, 0, A, "fft" },
    { "win_func", "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64 = WFUNC_HANNING}, 0, NB_WFUNC-1, A, "win_func" },
        { "rect",     "Rectangular",      0, AV_OPT_TYPE_CONST, {.i64=WFUNC_RECT},     0, 0, A, "win_func" },
        { "bartlett", "Bartlett",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BARTLETT}, 0, 0, A, "win_func" },
        { "hann",     "Hann",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, A, "win_func" },
        { "hanning",  "Hanning",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, A, "win_func" },
        { "hamming",  "Hamming",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HAMMING},  0, 0, A, "win_func" },
        { "blackman", "Blackman",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BLACKMAN}, 0, 0, A, "win_func" },
        { "welch",    "Welch",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_WELCH},    0, 0, A, "win_func" },
        { "flattop",  "Flat-top",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_FLATTOP},  0, 0, A, "win_func" },
        { "bharris",  "Blackman-Harris",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHARRIS},  0, 0, A, "win_func" },
        { "bnuttall", "Blackman-Nuttall", 0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BNUTTALL}, 0, 0, A, "win_func" },
        { "bhann",    "Bartlett-Hann",    0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHANN},    0, 0, A, "win_func" },
        { "sine",     "Sine",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_SINE},     0, 0, A, "win_func" },
        { "nuttall",  "Nuttall",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_NUTTALL},  0, 0, A, "win_func" },
        { "lanczos",  "Lanczos",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_LANCZOS},  0, 0, A, "win_func" },
        { "gauss",    "Gauss",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_GAUSS},    0, 0, A, "win_func" },
        { "tukey",    "Tukey",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_TUKEY},    0, 0, A, "win_func" },
        { "dolph",    "Dolph-Chebyshev",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_DOLPH},    0, 0, A, "win_func" },
        { "cauchy",   "Cauchy",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_CAUCHY},   0, 0, A, "win_func" },
        { "parzen",   "Parzen",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_PARZEN},   0, 0, A, "win_func" },
        { "poisson",  "Poisson",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_POISSON},  0, 0, A, "win_func" },
        { "bohman",   "Bohman",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BOHMAN},   0, 0, A, "win_func" },
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

    return s->fft_data[ich][ix].re;
}

static inline double getimag(void *priv, double x, double ch)
{
    AFFTFiltContext *s = priv;
    int ich, ix;

    ich = av_clip(ch, 0, s->nb_exprs - 1);
    ix = av_clip(x, 0, s->window_size / 2);

    return s->fft_data[ich][ix].im;
}

static double realf(void *priv, double x, double ch) { return getreal(priv, x, ch); }
static double imagf(void *priv, double x, double ch) { return getimag(priv, x, ch); }

static const char *const func2_names[]    = { "real", "imag", NULL };
double (*func2[])(void *, double, double) = {  realf,  imagf, NULL };

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AFFTFiltContext *s = ctx->priv;
    char *saveptr = NULL;
    int ret = 0, ch, i;
    float overlap;
    char *args;
    const char *last_expr = "1";

    s->pts  = AV_NOPTS_VALUE;
    s->fft  = av_fft_init(s->fft_bits, 0);
    s->ifft = av_fft_init(s->fft_bits, 1);
    if (!s->fft || !s->ifft)
        return AVERROR(ENOMEM);

    s->window_size = 1 << s->fft_bits;

    s->fft_data = av_calloc(inlink->channels, sizeof(*s->fft_data));
    if (!s->fft_data)
        return AVERROR(ENOMEM);

    s->fft_temp = av_calloc(inlink->channels, sizeof(*s->fft_temp));
    if (!s->fft_temp)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < inlink->channels; ch++) {
        s->fft_data[ch] = av_calloc(s->window_size, sizeof(**s->fft_data));
        if (!s->fft_data[ch])
            return AVERROR(ENOMEM);
    }

    for (ch = 0; ch < inlink->channels; ch++) {
        s->fft_temp[ch] = av_calloc(s->window_size, sizeof(**s->fft_temp));
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
            break;
        if (arg)
            last_expr = arg;
        s->nb_exprs++;
    }

    av_free(args);

    args = av_strdup(s->img_str ? s->img_str : s->real_str);
    if (!args)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < inlink->channels; ch++) {
        char *arg = av_strtok(ch == 0 ? args : NULL, "|", &saveptr);

        ret = av_expr_parse(&s->imag[ch], arg ? arg : last_expr, var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            break;
        if (arg)
            last_expr = arg;
    }

    av_free(args);

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

    for (s->win_scale = 0, i = 0; i < s->window_size; i++) {
        s->win_scale += s->window_func_lut[i] * s->window_func_lut[i];
    }

    s->hop_size = s->window_size * (1 - s->overlap);
    if (s->hop_size <= 0)
        return AVERROR(EINVAL);

    s->buffer = ff_get_audio_buffer(inlink, s->window_size * 2);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AFFTFiltContext *s = ctx->priv;
    const int window_size = s->window_size;
    const float f = 1. / s->win_scale;
    double values[VAR_VARS_NB];
    AVFrame *out, *in = NULL;
    int ch, n, ret, i, j, k;
    int start = s->start, end = s->end;

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = frame->pts;

    ret = av_audio_fifo_write(s->fifo, (void **)frame->extended_data, frame->nb_samples);
    av_frame_free(&frame);
    if (ret < 0)
        return ret;

    while (av_audio_fifo_size(s->fifo) >= window_size) {
        if (!in) {
            in = ff_get_audio_buffer(outlink, window_size);
            if (!in)
                return AVERROR(ENOMEM);
        }

        ret = av_audio_fifo_peek(s->fifo, (void **)in->extended_data, window_size);
        if (ret < 0)
            break;

        for (ch = 0; ch < inlink->channels; ch++) {
            const float *src = (float *)in->extended_data[ch];
            FFTComplex *fft_data = s->fft_data[ch];

            for (n = 0; n < in->nb_samples; n++) {
                fft_data[n].re = src[n] * s->window_func_lut[n];
                fft_data[n].im = 0;
            }

            for (; n < window_size; n++) {
                fft_data[n].re = 0;
                fft_data[n].im = 0;
            }
        }

        values[VAR_PTS]         = s->pts;
        values[VAR_SAMPLE_RATE] = inlink->sample_rate;
        values[VAR_NBBINS]      = window_size / 2;
        values[VAR_CHANNELS]    = inlink->channels;

        for (ch = 0; ch < inlink->channels; ch++) {
            FFTComplex *fft_data = s->fft_data[ch];

            av_fft_permute(s->fft, fft_data);
            av_fft_calc(s->fft, fft_data);
        }

        for (ch = 0; ch < inlink->channels; ch++) {
            FFTComplex *fft_data = s->fft_data[ch];
            FFTComplex *fft_temp = s->fft_temp[ch];
            float *buf = (float *)s->buffer->extended_data[ch];
            int x;
            values[VAR_CHANNEL] = ch;

            for (n = 0; n <= window_size / 2; n++) {
                float fr, fi;

                values[VAR_BIN] = n;
                values[VAR_REAL] = fft_data[n].re;
                values[VAR_IMAG] = fft_data[n].im;

                fr = av_expr_eval(s->real[ch], values, s);
                fi = av_expr_eval(s->imag[ch], values, s);

                fft_temp[n].re = fr;
                fft_temp[n].im = fi;
            }

            for (n = window_size / 2 + 1, x = window_size / 2 - 1; n < window_size; n++, x--) {
                fft_temp[n].re =  fft_temp[x].re;
                fft_temp[n].im = -fft_temp[x].im;
            }

            av_fft_permute(s->ifft, fft_temp);
            av_fft_calc(s->ifft, fft_temp);

            start = s->start;
            end = s->end;
            k = end;
            for (i = 0, j = start; j < k && i < window_size; i++, j++) {
                buf[j] += s->fft_temp[ch][i].re * f;
            }

            for (; i < window_size; i++, j++) {
                buf[j] = s->fft_temp[ch][i].re * f;
            }

            start += s->hop_size;
            end = j;
        }

        s->start = start;
        s->end = end;

        if (start >= window_size) {
            float *dst, *buf;

            start -= window_size;
            end   -= window_size;

            s->start = start;
            s->end = end;

            out = ff_get_audio_buffer(outlink, window_size);
            if (!out) {
                ret = AVERROR(ENOMEM);
                break;
            }

            out->pts = s->pts;
            s->pts += window_size;

            for (ch = 0; ch < inlink->channels; ch++) {
                dst = (float *)out->extended_data[ch];
                buf = (float *)s->buffer->extended_data[ch];

                for (n = 0; n < window_size; n++) {
                    dst[n] = buf[n] * (1 - s->overlap);
                }
                memmove(buf, buf + window_size, window_size * 4);
            }

            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                break;
        }

        av_audio_fifo_drain(s->fifo, s->hop_size);
    }

    av_frame_free(&in);
    return ret < 0 ? ret : 0;
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
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AFFTFiltContext *s = ctx->priv;
    int i;

    av_fft_end(s->fft);
    av_fft_end(s->ifft);

    for (i = 0; i < s->nb_exprs; i++) {
        if (s->fft_data)
            av_freep(&s->fft_data[i]);
        if (s->fft_temp)
            av_freep(&s->fft_temp[i]);
    }
    av_freep(&s->fft_data);
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
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_afftfilt = {
    .name            = "afftfilt",
    .description     = NULL_IF_CONFIG_SMALL("Apply arbitrary expressions to samples in frequency domain."),
    .priv_size       = sizeof(AFFTFiltContext),
    .priv_class      = &afftfilt_class,
    .inputs          = inputs,
    .outputs         = outputs,
    .query_formats   = query_formats,
    .uninit          = uninit,
};
