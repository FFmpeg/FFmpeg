/*
 * Copyright (c) 2012 Clément Bœsch
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
 * audio to spectrum (video) transmedia filter, based on ffplay rdft showmode
 * (by Michael Niedermayer) and lavfi/avf_showwaves (by Stefano Sabatini).
 */

#include <math.h>

#include "libavcodec/avfft.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"

enum DisplayMode  { COMBINED, SEPARATE, NB_MODES };
enum DisplayScale { LINEAR, SQRT, CBRT, LOG, NB_SCALES };
enum ColorMode    { CHANNEL, INTENSITY, NB_CLMODES };

typedef struct {
    const AVClass *class;
    int w, h;
    AVFilterBufferRef *outpicref;
    int req_fullfilled;
    int nb_display_channels;
    int channel_height;
    int sliding;                ///< 1 if sliding mode, 0 otherwise
    enum DisplayMode mode;      ///< channel display mode
    enum ColorMode color_mode;  ///< display color scheme
    enum DisplayScale scale;
    float saturation;           ///< color saturation multiplier
    int xpos;                   ///< x position (current column)
    RDFTContext *rdft;          ///< Real Discrete Fourier Transform context
    int rdft_bits;              ///< number of bits (RDFT window size = 1<<rdft_bits)
    FFTSample **rdft_data;      ///< bins holder for each (displayed) channels
    int filled;                 ///< number of samples (per channel) filled in current rdft_buffer
    int consumed;               ///< number of samples (per channel) consumed from the input frame
    float *window_func_lut;     ///< Window function LUT
    float *combine_buffer;      ///< color combining buffer (3 * h items)
} ShowSpectrumContext;

#define OFFSET(x) offsetof(ShowSpectrumContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showspectrum_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "slide", "set sliding mode", OFFSET(sliding), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS },
    { "mode", "set channel display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=COMBINED}, COMBINED, NB_MODES-1, FLAGS, "mode" },
    { "combined", "combined mode", 0, AV_OPT_TYPE_CONST, {.i64=COMBINED}, 0, 0, FLAGS, "mode" },
    { "separate", "separate mode", 0, AV_OPT_TYPE_CONST, {.i64=SEPARATE}, 0, 0, FLAGS, "mode" },
    { "color", "set channel coloring", OFFSET(color_mode), AV_OPT_TYPE_INT, {.i64=CHANNEL}, CHANNEL, NB_CLMODES-1, FLAGS, "color" },
    { "channel",   "separate color for each channel", 0, AV_OPT_TYPE_CONST, {.i64=CHANNEL},   0, 0, FLAGS, "color" },
    { "intensity", "intensity based coloring",        0, AV_OPT_TYPE_CONST, {.i64=INTENSITY}, 0, 0, FLAGS, "color" },
    { "scale", "set display scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=SQRT}, LINEAR, NB_SCALES-1, FLAGS, "scale" },
    { "sqrt", "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT},   0, 0, FLAGS, "scale" },
    { "cbrt", "cubic root",  0, AV_OPT_TYPE_CONST, {.i64=CBRT},   0, 0, FLAGS, "scale" },
    { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, FLAGS, "scale" },
    { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, "scale" },
    { "saturation", "color saturation multiplier", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1}, -10, 10, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(showspectrum);

static const struct {
    float a, y, u, v;
} intensity_color_table[] = {
    {    0,                  0,                  0,                   0 },
    { 0.13, .03587126228984074,  .1573300977624594, -.02548747583751842 },
    { 0.30, .18572281794568020,  .1772436246393981,  .17475554840414750 },
    { 0.60, .28184980583656130, -.1593064119945782,  .47132074554608920 },
    { 0.73, .65830621175547810, -.3716070802232764,  .24352759331252930 },
    { 0.78, .76318535758242900, -.4307467689263783,  .16866496622310430 },
    { 0.91, .95336363636363640, -.2045454545454546,  .03313636363636363 },
    {    1,                  1,                  0,                   0 }
};

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ShowSpectrumContext *showspectrum = ctx->priv;
    int err;

    showspectrum->class = &showspectrum_class;
    av_opt_set_defaults(showspectrum);

    if ((err = av_set_options_string(showspectrum, args, "=", ":")) < 0)
        return err;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowSpectrumContext *showspectrum = ctx->priv;
    int i;

    av_freep(&showspectrum->combine_buffer);
    av_rdft_end(showspectrum->rdft);
    for (i = 0; i < showspectrum->nb_display_channels; i++)
        av_freep(&showspectrum->rdft_data[i]);
    av_freep(&showspectrum->rdft_data);
    av_freep(&showspectrum->window_func_lut);
    avfilter_unref_bufferp(&showspectrum->outpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_NONE };

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_formats);

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_samplerates);

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &outlink->in_formats);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowSpectrumContext *showspectrum = ctx->priv;
    int i, rdft_bits, win_size, h;

    outlink->w = showspectrum->w;
    outlink->h = showspectrum->h;

    h = (showspectrum->mode == COMBINED) ? outlink->h : outlink->h / inlink->channels;
    showspectrum->channel_height = h;

    /* RDFT window size (precision) according to the requested output frame height */
    for (rdft_bits = 1; 1 << rdft_bits < 2 * h; rdft_bits++);
    win_size = 1 << rdft_bits;

    /* (re-)configuration if the video output changed (or first init) */
    if (rdft_bits != showspectrum->rdft_bits) {
        size_t rdft_size, rdft_listsize;
        AVFilterBufferRef *outpicref;

        av_rdft_end(showspectrum->rdft);
        showspectrum->rdft = av_rdft_init(rdft_bits, DFT_R2C);
        showspectrum->rdft_bits = rdft_bits;

        /* RDFT buffers: x2 for each (display) channel buffer.
         * Note: we use free and malloc instead of a realloc-like function to
         * make sure the buffer is aligned in memory for the FFT functions. */
        for (i = 0; i < showspectrum->nb_display_channels; i++)
            av_freep(&showspectrum->rdft_data[i]);
        av_freep(&showspectrum->rdft_data);
        showspectrum->nb_display_channels = inlink->channels;

        if (av_size_mult(sizeof(*showspectrum->rdft_data),
                         showspectrum->nb_display_channels, &rdft_listsize) < 0)
            return AVERROR(EINVAL);
        if (av_size_mult(sizeof(**showspectrum->rdft_data),
                         win_size, &rdft_size) < 0)
            return AVERROR(EINVAL);
        showspectrum->rdft_data = av_malloc(rdft_listsize);
        if (!showspectrum->rdft_data)
            return AVERROR(ENOMEM);
        for (i = 0; i < showspectrum->nb_display_channels; i++) {
            showspectrum->rdft_data[i] = av_malloc(rdft_size);
            if (!showspectrum->rdft_data[i])
                return AVERROR(ENOMEM);
        }
        showspectrum->filled = 0;

        /* pre-calc windowing function (hann here) */
        showspectrum->window_func_lut =
            av_realloc_f(showspectrum->window_func_lut, win_size,
                         sizeof(*showspectrum->window_func_lut));
        if (!showspectrum->window_func_lut)
            return AVERROR(ENOMEM);
        for (i = 0; i < win_size; i++)
            showspectrum->window_func_lut[i] = .5f * (1 - cos(2*M_PI*i / (win_size-1)));

        /* prepare the initial picref buffer (black frame) */
        avfilter_unref_bufferp(&showspectrum->outpicref);
        showspectrum->outpicref = outpicref =
            ff_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_PRESERVE|AV_PERM_REUSE2,
                                outlink->w, outlink->h);
        if (!outpicref)
            return AVERROR(ENOMEM);
        outlink->sample_aspect_ratio = (AVRational){1,1};
        memset(outpicref->data[0], 0, outlink->h * outpicref->linesize[0]);
    }

    if (showspectrum->xpos >= outlink->w)
        showspectrum->xpos = 0;

    showspectrum->combine_buffer =
        av_realloc_f(showspectrum->combine_buffer, outlink->h * 3,
                     sizeof(*showspectrum->combine_buffer));

    av_log(ctx, AV_LOG_VERBOSE, "s:%dx%d RDFT window size:%d\n",
           showspectrum->w, showspectrum->h, win_size);
    return 0;
}

inline static void push_frame(AVFilterLink *outlink)
{
    ShowSpectrumContext *showspectrum = outlink->src->priv;

    showspectrum->xpos++;
    if (showspectrum->xpos >= outlink->w)
        showspectrum->xpos = 0;
    showspectrum->filled = 0;
    showspectrum->req_fullfilled = 1;

    ff_filter_frame(outlink, avfilter_ref_buffer(showspectrum->outpicref, ~AV_PERM_WRITE));
}

static int request_frame(AVFilterLink *outlink)
{
    ShowSpectrumContext *showspectrum = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    showspectrum->req_fullfilled = 0;
    do {
        ret = ff_request_frame(inlink);
    } while (!showspectrum->req_fullfilled && ret >= 0);

    if (ret == AVERROR_EOF && showspectrum->outpicref)
        push_frame(outlink);
    return ret;
}

static int plot_spectrum_column(AVFilterLink *inlink, AVFilterBufferRef *insamples, int nb_samples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpectrumContext *showspectrum = ctx->priv;
    AVFilterBufferRef *outpicref = showspectrum->outpicref;

    /* nb_freq contains the power of two superior or equal to the output image
     * height (or half the RDFT window size) */
    const int nb_freq = 1 << (showspectrum->rdft_bits - 1);
    const int win_size = nb_freq << 1;
    const double w = 1. / (sqrt(nb_freq) * 32768.);

    int ch, plane, n, y;
    const int start = showspectrum->filled;
    const int add_samples = FFMIN(win_size - start, nb_samples);

    /* fill RDFT input with the number of samples available */
    for (ch = 0; ch < showspectrum->nb_display_channels; ch++) {
        const int16_t *p = (int16_t *)insamples->extended_data[ch];

        p += showspectrum->consumed;
        for (n = 0; n < add_samples; n++)
            showspectrum->rdft_data[ch][start + n] = p[n] * showspectrum->window_func_lut[start + n];
    }
    showspectrum->filled += add_samples;

    /* complete RDFT window size? */
    if (showspectrum->filled == win_size) {

        /* channel height */
        int h = showspectrum->channel_height;

        /* run RDFT on each samples set */
        for (ch = 0; ch < showspectrum->nb_display_channels; ch++)
            av_rdft_calc(showspectrum->rdft, showspectrum->rdft_data[ch]);

        /* fill a new spectrum column */
#define RE(y, ch) showspectrum->rdft_data[ch][2 * y + 0]
#define IM(y, ch) showspectrum->rdft_data[ch][2 * y + 1]
#define MAGNITUDE(y, ch) hypot(RE(y, ch), IM(y, ch))

        /* initialize buffer for combining to black */
        for (y = 0; y < outlink->h; y++) {
            showspectrum->combine_buffer[3 * y    ] = 0;
            showspectrum->combine_buffer[3 * y + 1] = 127.5;
            showspectrum->combine_buffer[3 * y + 2] = 127.5;
        }

        for (ch = 0; ch < showspectrum->nb_display_channels; ch++) {
            float yf, uf, vf;

            /* decide color range */
            switch (showspectrum->mode) {
            case COMBINED:
                // reduce range by channel count
                yf = 256.0f / showspectrum->nb_display_channels;
                switch (showspectrum->color_mode) {
                case INTENSITY:
                    uf = yf;
                    vf = yf;
                    break;
                case CHANNEL:
                    /* adjust saturation for mixed UV coloring */
                    /* this factor is correct for infinite channels, an approximation otherwise */
                    uf = yf * M_PI;
                    vf = yf * M_PI;
                    break;
                default:
                    av_assert0(0);
                }
                break;
            case SEPARATE:
                // full range
                yf = 256.0f;
                uf = 256.0f;
                vf = 256.0f;
                break;
            default:
                av_assert0(0);
            }

            if (showspectrum->color_mode == CHANNEL) {
                if (showspectrum->nb_display_channels > 1) {
                    uf *= 0.5 * sin((2 * M_PI * ch) / showspectrum->nb_display_channels);
                    vf *= 0.5 * cos((2 * M_PI * ch) / showspectrum->nb_display_channels);
                } else {
                    uf = 0.0f;
                    vf = 0.0f;
                }
            }
            uf *= showspectrum->saturation;
            vf *= showspectrum->saturation;

            /* draw the channel */
            for (y = 0; y < h; y++) {
                int row = (showspectrum->mode == COMBINED) ? y : ch * h + y;
                float *out = &showspectrum->combine_buffer[3 * row];

                /* get magnitude */
                float a = w * MAGNITUDE(y, ch);

                /* apply scale */
                switch (showspectrum->scale) {
                case LINEAR:
                    break;
                case SQRT:
                    a = sqrt(a);
                    break;
                case CBRT:
                    a = cbrt(a);
                    break;
                case LOG:
                    a = 1 - log(FFMAX(FFMIN(1, a), 1e-6)) / log(1e-6); // zero = -120dBFS
                    break;
                default:
                    av_assert0(0);
                }

                if (showspectrum->color_mode == INTENSITY) {
                    float y, u, v;
                    int i;

                    for (i = 1; i < sizeof(intensity_color_table) / sizeof(*intensity_color_table) - 1; i++)
                        if (intensity_color_table[i].a >= a)
                            break;
                    // i now is the first item >= the color
                    // now we know to interpolate between item i - 1 and i
                    if (a <= intensity_color_table[i - 1].a) {
                        y = intensity_color_table[i - 1].y;
                        u = intensity_color_table[i - 1].u;
                        v = intensity_color_table[i - 1].v;
                    } else if (a >= intensity_color_table[i].a) {
                        y = intensity_color_table[i].y;
                        u = intensity_color_table[i].u;
                        v = intensity_color_table[i].v;
                    } else {
                        float start = intensity_color_table[i - 1].a;
                        float end = intensity_color_table[i].a;
                        float lerpfrac = (a - start) / (end - start);
                        y = intensity_color_table[i - 1].y * (1.0f - lerpfrac)
                          + intensity_color_table[i].y * lerpfrac;
                        u = intensity_color_table[i - 1].u * (1.0f - lerpfrac)
                          + intensity_color_table[i].u * lerpfrac;
                        v = intensity_color_table[i - 1].v * (1.0f - lerpfrac)
                          + intensity_color_table[i].v * lerpfrac;
                    }

                    out[0] += y * yf;
                    out[1] += u * uf;
                    out[2] += v * vf;
                } else {
                    out[0] += a * yf;
                    out[1] += a * uf;
                    out[2] += a * vf;
                }
            }
        }

        /* copy to output */
        if (showspectrum->sliding) {
            for (plane = 0; plane < 3; plane++) {
                for (y = 0; y < outlink->h; y++) {
                    uint8_t *p = outpicref->data[plane] +
                                 y * outpicref->linesize[plane];
                    memmove(p, p + 1, outlink->w - 1);
                }
            }
            showspectrum->xpos = outlink->w - 1;
        }
        for (plane = 0; plane < 3; plane++) {
            uint8_t *p = outpicref->data[plane] +
                         (outlink->h - 1) * outpicref->linesize[plane] +
                         showspectrum->xpos;
            for (y = 0; y < outlink->h; y++) {
                *p = rint(FFMAX(0, FFMIN(showspectrum->combine_buffer[3 * y + plane], 255)));
                p -= outpicref->linesize[plane];
            }
        }

        outpicref->pts = insamples->pts +
            av_rescale_q(showspectrum->consumed,
                         (AVRational){ 1, inlink->sample_rate },
                         outlink->time_base);
        push_frame(outlink);
    }

    return add_samples;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    ShowSpectrumContext *showspectrum = ctx->priv;
    int left_samples = insamples->audio->nb_samples;

    showspectrum->consumed = 0;
    while (left_samples) {
        const int added_samples = plot_spectrum_column(inlink, insamples, left_samples);
        showspectrum->consumed += added_samples;
        left_samples -= added_samples;
    }

    avfilter_unref_buffer(insamples);
    return 0;
}

static const AVFilterPad showspectrum_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .min_perms    = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad showspectrum_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_avf_showspectrum = {
    .name           = "showspectrum",
    .description    = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output."),
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .priv_size      = sizeof(ShowSpectrumContext),
    .inputs         = showspectrum_inputs,
    .outputs        = showspectrum_outputs,
    .priv_class     = &showspectrum_class,
};
