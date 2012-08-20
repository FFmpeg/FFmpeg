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
#include "libavutil/audioconvert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int w, h;
    AVFilterBufferRef *outpicref;
    int req_fullfilled;
    int xpos;                   ///< x position (current column)
    RDFTContext *rdft;          ///< Real Discrete Fourier Transform context
    int rdft_bits;              ///< number of bits (RDFT window size = 1<<rdft_bits)
    FFTSample *rdft_data;       ///< bins holder for each (displayed) channels
    int filled;                 ///< number of samples (per channel) filled in current rdft_buffer
    int consumed;               ///< number of samples (per channel) consumed from the input frame
    float *window_func_lut;     ///< Window function LUT
} ShowSpectrumContext;

#define OFFSET(x) offsetof(ShowSpectrumContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showspectrum_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x480"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x480"}, 0, 0, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(showspectrum);

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

    av_rdft_end(showspectrum->rdft);
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
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16P, -1 };
    static const enum PixelFormat pix_fmts[] = { PIX_FMT_RGB24, -1 };

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
    ShowSpectrumContext *showspectrum = ctx->priv;
    int i, rdft_bits, win_size;

    outlink->w = showspectrum->w;
    outlink->h = showspectrum->h;

    /* RDFT window size (precision) according to the requested output frame height */
    for (rdft_bits = 1; 1<<rdft_bits < 2*outlink->h; rdft_bits++);
    win_size = 1 << rdft_bits;

    /* (re-)configuration if the video output changed (or first init) */
    if (rdft_bits != showspectrum->rdft_bits) {
        AVFilterBufferRef *outpicref;

        av_rdft_end(showspectrum->rdft);
        showspectrum->rdft = av_rdft_init(rdft_bits, DFT_R2C);
        showspectrum->rdft_bits = rdft_bits;

        /* RDFT buffers: x2 for each (display) channel buffer */
        showspectrum->rdft_data =
            av_realloc_f(showspectrum->rdft_data, 2 * win_size,
                         sizeof(*showspectrum->rdft_data));
        if (!showspectrum->rdft_data)
            return AVERROR(ENOMEM);
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

    ff_start_frame(outlink, avfilter_ref_buffer(showspectrum->outpicref, ~AV_PERM_WRITE));
    ff_draw_slice(outlink, 0, outlink->h, 1);
    ff_end_frame(outlink);
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
    const int nb_channels = av_get_channel_layout_nb_channels(insamples->audio->channel_layout);

    /* nb_freq contains the power of two superior or equal to the output image
     * height (or half the RDFT window size) */
    const int nb_freq = 1 << (showspectrum->rdft_bits - 1);
    const int win_size = nb_freq << 1;

    int ch, n, y;
    FFTSample *data[2];
    const int nb_display_channels = FFMIN(nb_channels, 2);
    const int start = showspectrum->filled;
    const int add_samples = FFMIN(win_size - start, nb_samples);

    /* fill RDFT input with the number of samples available */
    for (ch = 0; ch < nb_display_channels; ch++) {
        const int16_t *p = (int16_t *)insamples->extended_data[ch];

        p += showspectrum->consumed;
        data[ch] = showspectrum->rdft_data + win_size * ch; // select channel buffer
        for (n = 0; n < add_samples; n++)
            data[ch][start + n] = p[n] * showspectrum->window_func_lut[start + n];
    }
    showspectrum->filled += add_samples;

    /* complete RDFT window size? */
    if (showspectrum->filled == win_size) {

        /* run RDFT on each samples set */
        for (ch = 0; ch < nb_display_channels; ch++)
            av_rdft_calc(showspectrum->rdft, data[ch]);

        /* fill a new spectrum column */
#define RE(ch) data[ch][2*y + 0]
#define IM(ch) data[ch][2*y + 1]
#define MAGNITUDE(re, im) sqrt((re)*(re) + (im)*(im))

        for (y = 0; y < outlink->h; y++) {
            // FIXME: bin[0] contains first and last bins
            const int pos = showspectrum->xpos * 3 + (outlink->h - y - 1) * outpicref->linesize[0];
            const double w = 1. / sqrt(nb_freq);
            int a =                           sqrt(w * MAGNITUDE(RE(0), IM(0)));
            int b = nb_display_channels > 1 ? sqrt(w * MAGNITUDE(RE(1), IM(1))) : a;

            a = FFMIN(a, 255);
            b = FFMIN(b, 255);
            outpicref->data[0][pos]   = a;
            outpicref->data[0][pos+1] = b;
            outpicref->data[0][pos+2] = (a + b) / 2;
        }
        outpicref->pts = insamples->pts +
            av_rescale_q(showspectrum->consumed,
                         (AVRational){ 1, inlink->sample_rate },
                         outlink->time_base);
        push_frame(outlink);
    }

    return add_samples;
}

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
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

AVFilter avfilter_avf_showspectrum = {
    .name           = "showspectrum",
    .description    = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output."),
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .priv_size      = sizeof(ShowSpectrumContext),

    .inputs  = (const AVFilterPad[]) {
        {
            .name           = "default",
            .type           = AVMEDIA_TYPE_AUDIO,
            .filter_samples = filter_samples,
            .min_perms      = AV_PERM_READ,
        },
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        {
            .name           = "default",
            .type           = AVMEDIA_TYPE_VIDEO,
            .config_props   = config_output,
            .request_frame  = request_frame,
        },
        { .name = NULL }
    },

    .priv_class = &showspectrum_class,
};
