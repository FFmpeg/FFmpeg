/*
 * Copyright (c) 2016 Paul B Mahol
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
 * SpectrumSynth filter
 * @todo support float pixel format
 */

#include "libavcodec/avfft.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"
#include "window_func.h"

enum MagnitudeScale { LINEAR, LOG, NB_SCALES };
enum SlideMode      { REPLACE, SCROLL, FULLFRAME, RSCROLL, NB_SLIDES };
enum Orientation    { VERTICAL, HORIZONTAL, NB_ORIENTATIONS };

typedef struct SpectrumSynthContext {
    const AVClass *class;
    int sample_rate;
    int channels;
    int scale;
    int sliding;
    int win_func;
    float overlap;
    int orientation;

    AVFrame *magnitude, *phase;
    FFTContext *fft;            ///< Fast Fourier Transform context
    int fft_bits;               ///< number of bits (FFT window size = 1<<fft_bits)
    FFTComplex **fft_data;      ///< bins holder for each (displayed) channels
    int win_size;
    int size;
    int nb_freq;
    int hop_size;
    int start, end;
    int xpos;
    int xend;
    int64_t pts;
    float factor;
    AVFrame *buffer;
    float *window_func_lut;     ///< Window function LUT
} SpectrumSynthContext;

#define OFFSET(x) offsetof(SpectrumSynthContext, x)
#define A AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption spectrumsynth_options[] = {
    { "sample_rate", "set sample rate",  OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64 = 44100}, 15,  INT_MAX, A },
    { "channels",    "set channels",     OFFSET(channels), AV_OPT_TYPE_INT, {.i64 = 1}, 1, 8, A },
    { "scale",       "set input amplitude scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64 = LOG}, 0, NB_SCALES-1, V, "scale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, V, "scale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, V, "scale" },
    { "slide", "set input sliding mode", OFFSET(sliding), AV_OPT_TYPE_INT, {.i64 = FULLFRAME}, 0, NB_SLIDES-1, V, "slide" },
        { "replace",   "consume old columns with new",   0, AV_OPT_TYPE_CONST, {.i64=REPLACE},   0, 0, V, "slide" },
        { "scroll",    "consume only most right column", 0, AV_OPT_TYPE_CONST, {.i64=SCROLL},    0, 0, V, "slide" },
        { "fullframe", "consume full frames",            0, AV_OPT_TYPE_CONST, {.i64=FULLFRAME}, 0, 0, V, "slide" },
        { "rscroll",   "consume only most left column",  0, AV_OPT_TYPE_CONST, {.i64=RSCROLL},   0, 0, V, "slide" },
    { "win_func", "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64 = 0}, 0, NB_WFUNC-1, A, "win_func" },
        { "rect",     "Rectangular",      0, AV_OPT_TYPE_CONST, {.i64=WFUNC_RECT},     0, 0, A, "win_func" },
        { "bartlett", "Bartlett",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BARTLETT}, 0, 0, A, "win_func" },
        { "hann",     "Hann",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, A, "win_func" },
        { "hanning",  "Hanning",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, A, "win_func" },
        { "hamming",  "Hamming",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HAMMING},  0, 0, A, "win_func" },
        { "sine",     "Sine",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_SINE},     0, 0, A, "win_func" },
    { "overlap", "set window overlap",  OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0,  1, A },
    { "orientation", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=VERTICAL}, 0, NB_ORIENTATIONS-1, V, "orientation" },
        { "vertical",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=VERTICAL},   0, 0, V, "orientation" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, {.i64=HORIZONTAL}, 0, 0, V, "orientation" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(spectrumsynth);

static int query_formats(AVFilterContext *ctx)
{
    SpectrumSynthContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    AVFilterLink *magnitude = ctx->inputs[0];
    AVFilterLink *phase = ctx->inputs[1];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16,
                                                   AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
                                                   AV_PIX_FMT_YUV444P16, AV_PIX_FMT_NONE };
    int ret, sample_rates[] = { 48000, -1 };

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref         (formats, &outlink->in_formats        )) < 0 ||
        (ret = ff_add_channel_layout  (&layout, FF_COUNT2LAYOUT(s->channels))) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &outlink->in_channel_layouts)) < 0)
        return ret;

    sample_rates[0] = s->sample_rate;
    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(formats, &outlink->in_samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(formats, &magnitude->out_formats)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(formats, &phase->out_formats)) < 0)
        return ret;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SpectrumSynthContext *s = ctx->priv;
    int width = ctx->inputs[0]->w;
    int height = ctx->inputs[0]->h;
    AVRational time_base  = ctx->inputs[0]->time_base;
    AVRational frame_rate = ctx->inputs[0]->frame_rate;
    int i, ch, fft_bits;
    float factor, overlap;

    outlink->sample_rate = s->sample_rate;
    outlink->time_base = (AVRational){1, s->sample_rate};

    if (width  != ctx->inputs[1]->w ||
        height != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Magnitude and Phase sizes differ (%dx%d vs %dx%d).\n",
               width, height,
               ctx->inputs[1]->w, ctx->inputs[1]->h);
        return AVERROR_INVALIDDATA;
    } else if (av_cmp_q(time_base, ctx->inputs[1]->time_base) != 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Magnitude and Phase time bases differ (%d/%d vs %d/%d).\n",
               time_base.num, time_base.den,
               ctx->inputs[1]->time_base.num,
               ctx->inputs[1]->time_base.den);
        return AVERROR_INVALIDDATA;
    } else if (av_cmp_q(frame_rate, ctx->inputs[1]->frame_rate) != 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Magnitude and Phase framerates differ (%d/%d vs %d/%d).\n",
               frame_rate.num, frame_rate.den,
               ctx->inputs[1]->frame_rate.num,
               ctx->inputs[1]->frame_rate.den);
        return AVERROR_INVALIDDATA;
    }

    s->size = s->orientation == VERTICAL ? height / s->channels : width / s->channels;
    s->xend = s->orientation == VERTICAL ? width : height;

    for (fft_bits = 1; 1 << fft_bits < 2 * s->size; fft_bits++);

    s->win_size = 1 << fft_bits;
    s->nb_freq = 1 << (fft_bits - 1);

    s->fft = av_fft_init(fft_bits, 1);
    if (!s->fft) {
        av_log(ctx, AV_LOG_ERROR, "Unable to create FFT context. "
               "The window size might be too high.\n");
        return AVERROR(EINVAL);
    }
    s->fft_data = av_calloc(s->channels, sizeof(*s->fft_data));
    if (!s->fft_data)
        return AVERROR(ENOMEM);
    for (ch = 0; ch < s->channels; ch++) {
        s->fft_data[ch] = av_calloc(s->win_size, sizeof(**s->fft_data));
        if (!s->fft_data[ch])
            return AVERROR(ENOMEM);
    }

    s->buffer = ff_get_audio_buffer(outlink, s->win_size * 2);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    /* pre-calc windowing function */
    s->window_func_lut = av_realloc_f(s->window_func_lut, s->win_size,
                                      sizeof(*s->window_func_lut));
    if (!s->window_func_lut)
        return AVERROR(ENOMEM);
    ff_generate_window_func(s->window_func_lut, s->win_size, s->win_func, &overlap);
    if (s->overlap == 1)
        s->overlap = overlap;
    s->hop_size = (1 - s->overlap) * s->win_size;
    for (factor = 0, i = 0; i < s->win_size; i++) {
        factor += s->window_func_lut[i] * s->window_func_lut[i];
    }
    s->factor = (factor / s->win_size) / FFMAX(1 / (1 - s->overlap) - 1, 1);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SpectrumSynthContext *s = ctx->priv;
    int ret;

    if (!s->magnitude) {
        ret = ff_request_frame(ctx->inputs[0]);
        if (ret < 0)
            return ret;
    }
    if (!s->phase) {
        ret = ff_request_frame(ctx->inputs[1]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void read16_fft_bin(SpectrumSynthContext *s,
                           int x, int y, int f, int ch)
{
    const int m_linesize = s->magnitude->linesize[0];
    const int p_linesize = s->phase->linesize[0];
    const uint16_t *m = (uint16_t *)(s->magnitude->data[0] + y * m_linesize);
    const uint16_t *p = (uint16_t *)(s->phase->data[0] + y * p_linesize);
    float magnitude, phase;

    switch (s->scale) {
    case LINEAR:
        magnitude = m[x] / (double)UINT16_MAX;
        break;
    case LOG:
        magnitude = ff_exp10(((m[x] / (double)UINT16_MAX) - 1.) * 6.);
        break;
    }
    phase = ((p[x] / (double)UINT16_MAX) * 2. - 1.) * M_PI;

    s->fft_data[ch][f].re = magnitude * cos(phase);
    s->fft_data[ch][f].im = magnitude * sin(phase);
}

static void read8_fft_bin(SpectrumSynthContext *s,
                          int x, int y, int f, int ch)
{
    const int m_linesize = s->magnitude->linesize[0];
    const int p_linesize = s->phase->linesize[0];
    const uint8_t *m = (uint8_t *)(s->magnitude->data[0] + y * m_linesize);
    const uint8_t *p = (uint8_t *)(s->phase->data[0] + y * p_linesize);
    float magnitude, phase;

    switch (s->scale) {
    case LINEAR:
        magnitude = m[x] / (double)UINT8_MAX;
        break;
    case LOG:
        magnitude = ff_exp10(((m[x] / (double)UINT8_MAX) - 1.) * 6.);
        break;
    }
    phase = ((p[x] / (double)UINT8_MAX) * 2. - 1.) * M_PI;

    s->fft_data[ch][f].re = magnitude * cos(phase);
    s->fft_data[ch][f].im = magnitude * sin(phase);
}

static void read_fft_data(AVFilterContext *ctx, int x, int h, int ch)
{
    SpectrumSynthContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int start = h * (s->channels - ch) - 1;
    int end = h * (s->channels - ch - 1);
    int y, f;

    switch (s->orientation) {
    case VERTICAL:
        switch (inlink->format) {
        case AV_PIX_FMT_YUV444P16:
        case AV_PIX_FMT_GRAY16:
            for (y = start, f = 0; y >= end; y--, f++) {
                read16_fft_bin(s, x, y, f, ch);
            }
            break;
        case AV_PIX_FMT_YUVJ444P:
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_GRAY8:
            for (y = start, f = 0; y >= end; y--, f++) {
                read8_fft_bin(s, x, y, f, ch);
            }
            break;
        }
        break;
    case HORIZONTAL:
        switch (inlink->format) {
        case AV_PIX_FMT_YUV444P16:
        case AV_PIX_FMT_GRAY16:
            for (y = end, f = 0; y <= start; y++, f++) {
                read16_fft_bin(s, y, x, f, ch);
            }
            break;
        case AV_PIX_FMT_YUVJ444P:
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_GRAY8:
            for (y = end, f = 0; y <= start; y++, f++) {
                read8_fft_bin(s, y, x, f, ch);
            }
            break;
        }
        break;
    }
}

static void synth_window(AVFilterContext *ctx, int x)
{
    SpectrumSynthContext *s = ctx->priv;
    const int h = s->size;
    int nb = s->win_size;
    int y, f, ch;

    for (ch = 0; ch < s->channels; ch++) {
        read_fft_data(ctx, x, h, ch);

        for (y = h; y <= s->nb_freq; y++) {
            s->fft_data[ch][y].re = 0;
            s->fft_data[ch][y].im = 0;
        }

        for (y = s->nb_freq + 1, f = s->nb_freq - 1; y < nb; y++, f--) {
            s->fft_data[ch][y].re =  s->fft_data[ch][f].re;
            s->fft_data[ch][y].im = -s->fft_data[ch][f].im;
        }

        av_fft_permute(s->fft, s->fft_data[ch]);
        av_fft_calc(s->fft, s->fft_data[ch]);
    }
}

static int try_push_frame(AVFilterContext *ctx, int x)
{
    SpectrumSynthContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const float factor = s->factor;
    int ch, n, i, ret;
    int start, end;
    AVFrame *out;

    synth_window(ctx, x);

    for (ch = 0; ch < s->channels; ch++) {
        float *buf = (float *)s->buffer->extended_data[ch];
        int j, k;

        start = s->start;
        end = s->end;
        k = end;
        for (i = 0, j = start; j < k && i < s->win_size; i++, j++) {
            buf[j] += s->fft_data[ch][i].re;
        }

        for (; i < s->win_size; i++, j++) {
            buf[j] = s->fft_data[ch][i].re;
        }

        start += s->hop_size;
        end = j;

        if (start >= s->win_size) {
            start -= s->win_size;
            end -= s->win_size;

            if (ch == s->channels - 1) {
                float *dst;
                int c;

                out = ff_get_audio_buffer(outlink, s->win_size);
                if (!out) {
                    av_frame_free(&s->magnitude);
                    av_frame_free(&s->phase);
                    return AVERROR(ENOMEM);
                }

                out->pts = s->pts;
                s->pts += s->win_size;
                for (c = 0; c < s->channels; c++) {
                    dst = (float *)out->extended_data[c];
                    buf = (float *)s->buffer->extended_data[c];

                    for (n = 0; n < s->win_size; n++) {
                        dst[n] = buf[n] * factor;
                    }
                    memmove(buf, buf + s->win_size, s->win_size * 4);
                }

                ret = ff_filter_frame(outlink, out);
            }
        }
    }

    s->start = start;
    s->end = end;

    return 0;
}

static int try_push_frames(AVFilterContext *ctx)
{
    SpectrumSynthContext *s = ctx->priv;
    int ret, x;

    if (!(s->magnitude && s->phase))
        return 0;

    switch (s->sliding) {
    case REPLACE:
        ret = try_push_frame(ctx, s->xpos);
        s->xpos++;
        if (s->xpos >= s->xend)
            s->xpos = 0;
        break;
    case SCROLL:
        s->xpos = s->xend - 1;
        ret = try_push_frame(ctx, s->xpos);
        break;
    case RSCROLL:
        s->xpos = 0;
        ret = try_push_frame(ctx, s->xpos);
        break;
    case FULLFRAME:
        for (x = 0; x < s->xend; x++) {
            ret = try_push_frame(ctx, x);
            if (ret < 0)
                break;
        }
        break;
    }

    av_frame_free(&s->magnitude);
    av_frame_free(&s->phase);
    return ret;
}

static int filter_frame_magnitude(AVFilterLink *inlink, AVFrame *magnitude)
{
    AVFilterContext *ctx = inlink->dst;
    SpectrumSynthContext *s = ctx->priv;

    s->magnitude = magnitude;
    return try_push_frames(ctx);
}

static int filter_frame_phase(AVFilterLink *inlink, AVFrame *phase)
{
    AVFilterContext *ctx = inlink->dst;
    SpectrumSynthContext *s = ctx->priv;

    s->phase = phase;
    return try_push_frames(ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SpectrumSynthContext *s = ctx->priv;
    int i;

    av_frame_free(&s->magnitude);
    av_frame_free(&s->phase);
    av_frame_free(&s->buffer);
    av_fft_end(s->fft);
    if (s->fft_data) {
        for (i = 0; i < s->channels; i++)
            av_freep(&s->fft_data[i]);
    }
    av_freep(&s->fft_data);
    av_freep(&s->window_func_lut);
}

static const AVFilterPad spectrumsynth_inputs[] = {
    {
        .name         = "magnitude",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame_magnitude,
        .needs_fifo   = 1,
    },
    {
        .name         = "phase",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame_phase,
        .needs_fifo   = 1,
    },
    { NULL }
};

static const AVFilterPad spectrumsynth_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vaf_spectrumsynth = {
    .name          = "spectrumsynth",
    .description   = NULL_IF_CONFIG_SMALL("Convert input spectrum videos to audio output."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(SpectrumSynthContext),
    .inputs        = spectrumsynth_inputs,
    .outputs       = spectrumsynth_outputs,
    .priv_class    = &spectrumsynth_class,
};
