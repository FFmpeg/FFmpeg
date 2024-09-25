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

#include <math.h>

#include "libavutil/mem.h"
#include "libavutil/tx.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "formats.h"
#include "video.h"
#include "avfilter.h"
#include "filters.h"
#include "window_func.h"

typedef struct ShowSpatialContext {
    const AVClass *class;
    int w, h;
    AVRational frame_rate;
    AVTXContext *fft[2];          ///< Fast Fourier Transform context
    AVComplexFloat *fft_data[2];  ///< bins holder for each (displayed) channels
    AVComplexFloat *fft_tdata[2]; ///< bins holder for each (displayed) channels
    float *window_func_lut;       ///< Window function LUT
    av_tx_fn tx_fn[2];
    int win_func;
    int win_size;
    int buf_size;
    int consumed;
    int hop_size;
    AVAudioFifo *fifo;
    int64_t pts;
} ShowSpatialContext;

#define OFFSET(x) offsetof(ShowSpatialContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showspatial_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "512x512"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "512x512"}, 0, 0, FLAGS },
    { "win_size", "set window size", OFFSET(win_size), AV_OPT_TYPE_INT, {.i64 = 4096}, 1024, 65536, FLAGS },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), FLAGS, WFUNC_HANNING),
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showspatial);

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowSpatialContext *s = ctx->priv;

    for (int i = 0; i < 2; i++)
        av_tx_uninit(&s->fft[i]);
    for (int i = 0; i < 2; i++) {
        av_freep(&s->fft_data[i]);
        av_freep(&s->fft_tdata[i]);
    }
    av_freep(&s->window_func_lut);
    av_audio_fifo_free(s->fifo);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    AVFilterFormats *formats = NULL;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GBRP, AV_PIX_FMT_NONE };
    static const AVChannelLayout layouts[] = { AV_CHANNEL_LAYOUT_STEREO, { .nb_channels = 0 } };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_in[0]->formats)) < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->formats)) < 0)
        return ret;

    return 0;
}

static int run_channel_fft(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpatialContext *s = ctx->priv;
    const float *window_func_lut = s->window_func_lut;
    AVFrame *fin = arg;
    const int ch = jobnr;
    const float *p = (float *)fin->extended_data[ch];

    for (int n = 0; n < fin->nb_samples; n++) {
        s->fft_tdata[ch][n].re = p[n] * window_func_lut[n];
        s->fft_tdata[ch][n].im = 0.f;
    }

    s->tx_fn[ch](s->fft[ch], s->fft_data[ch], s->fft_tdata[ch], sizeof(AVComplexFloat));

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowSpatialContext *s = ctx->priv;
    float overlap;
    int ret;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    l->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(l->frame_rate);

    /* (re-)configuration if the video output changed (or first init) */
    if (s->win_size != s->buf_size) {
        s->buf_size = s->win_size;

        /* FFT buffers: x2 for each channel buffer.
         * Note: we use free and malloc instead of a realloc-like function to
         * make sure the buffer is aligned in memory for the FFT functions. */
        for (int i = 0; i < 2; i++) {
            av_tx_uninit(&s->fft[i]);
            av_freep(&s->fft_data[i]);
            av_freep(&s->fft_tdata[i]);
        }
        for (int i = 0; i < 2; i++) {
            float scale = 1.f;
            ret = av_tx_init(&s->fft[i], &s->tx_fn[i], AV_TX_FLOAT_FFT,
                             0, s->win_size, &scale, 0);
            if (ret < 0)
                return ret;
        }

        for (int i = 0; i < 2; i++) {
            s->fft_tdata[i] = av_calloc(s->buf_size, sizeof(**s->fft_tdata));
            if (!s->fft_tdata[i])
                return AVERROR(ENOMEM);

            s->fft_data[i] = av_calloc(s->buf_size, sizeof(**s->fft_data));
            if (!s->fft_data[i])
                return AVERROR(ENOMEM);
        }

        /* pre-calc windowing function */
        s->window_func_lut =
            av_realloc_f(s->window_func_lut, s->win_size,
                         sizeof(*s->window_func_lut));
        if (!s->window_func_lut)
            return AVERROR(ENOMEM);
        generate_window_func(s->window_func_lut, s->win_size, s->win_func, &overlap);

        s->hop_size = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
    }

    av_audio_fifo_free(s->fifo);
    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->ch_layout.nb_channels, s->win_size);
    if (!s->fifo)
        return AVERROR(ENOMEM);
    return 0;
}

#define RE(y, ch) s->fft_data[ch][y].re
#define IM(y, ch) s->fft_data[ch][y].im

static void draw_dot(uint8_t *dst, int linesize, int value)
{
    dst[0] = value;
    dst[1] = value;
    dst[-1] = value;
    dst[linesize] = value;
    dst[-linesize] = value;
}

static int draw_spatial(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpatialContext *s = ctx->priv;
    AVFrame *outpicref;
    int h = s->h - 2;
    int w = s->w - 2;
    int z = s->win_size / 2;
    int64_t pts = av_rescale_q(insamples->pts, inlink->time_base, outlink->time_base);

    outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpicref)
        return AVERROR(ENOMEM);

    outpicref->sample_aspect_ratio = (AVRational){1,1};
    for (int i = 0; i < outlink->h; i++) {
        memset(outpicref->data[0] + i * outpicref->linesize[0], 0, outlink->w);
        memset(outpicref->data[1] + i * outpicref->linesize[1], 0, outlink->w);
        memset(outpicref->data[2] + i * outpicref->linesize[2], 0, outlink->w);
    }

    for (int j = 0; j < z; j++) {
        const int idx = z - 1 - j;
        float l = hypotf(RE(idx, 0), IM(idx, 0));
        float r = hypotf(RE(idx, 1), IM(idx, 1));
        float sum = l + r;
        float lp = atan2f(IM(idx, 0), RE(idx, 0));
        float rp = atan2f(IM(idx, 1), RE(idx, 1));
        float diffp = ((rp - lp) / (2.f * M_PI) + 1.f) * 0.5f;
        float diff = (sum < 0.000001f ? 0.f : (r - l) / sum) * 0.5f + 0.5f;
        float cr = av_clipf(cbrtf(l / sum), 0, 1) * 255.f;
        float cb = av_clipf(cbrtf(r / sum), 0, 1) * 255.f;
        float cg;
        int x, y;

        cg = diffp * 255.f;
        x = av_clip(w * diff,  0, w - 2) + 1;
        y = av_clip(h * diffp, 0, h - 2) + 1;

        draw_dot(outpicref->data[0] + outpicref->linesize[0] * y + x, outpicref->linesize[0], cg);
        draw_dot(outpicref->data[1] + outpicref->linesize[1] * y + x, outpicref->linesize[1], cb);
        draw_dot(outpicref->data[2] + outpicref->linesize[2] * y + x, outpicref->linesize[2], cr);
    }

    outpicref->pts = pts;
    outpicref->duration = 1;

    return ff_filter_frame(outlink, outpicref);
}

static int spatial_activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpatialContext *s = ctx->priv;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (av_audio_fifo_size(s->fifo) < s->win_size) {
        AVFrame *frame = NULL;

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            s->pts = frame->pts;
            s->consumed = 0;

            av_audio_fifo_write(s->fifo, (void **)frame->extended_data, frame->nb_samples);
            av_frame_free(&frame);
        }
    }

    if (av_audio_fifo_size(s->fifo) >= s->win_size) {
        AVFrame *fin = ff_get_audio_buffer(inlink, s->win_size);
        if (!fin)
            return AVERROR(ENOMEM);

        fin->pts = s->pts + s->consumed;
        s->consumed += s->hop_size;
        ret = av_audio_fifo_peek(s->fifo, (void **)fin->extended_data,
                                 FFMIN(s->win_size, av_audio_fifo_size(s->fifo)));
        if (ret < 0) {
            av_frame_free(&fin);
            return ret;
        }

        av_assert0(fin->nb_samples == s->win_size);

        ff_filter_execute(ctx, run_channel_fft, fin, NULL, 2);

        ret = draw_spatial(inlink, fin);

        av_frame_free(&fin);
        av_audio_fifo_drain(s->fifo, s->hop_size);
        if (ret <= 0)
            return ret;
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    if (ff_outlink_frame_wanted(outlink) && av_audio_fifo_size(s->fifo) < s->win_size) {
        ff_inlink_request_frame(inlink);
        return 0;
    }

    if (av_audio_fifo_size(s->fifo) >= s->win_size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }
    return FFERROR_NOT_READY;
}

static const AVFilterPad showspatial_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_avf_showspatial = {
    .name          = "showspatial",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a spatial video output."),
    .uninit        = uninit,
    .priv_size     = sizeof(ShowSpatialContext),
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(showspatial_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .activate      = spatial_activate,
    .priv_class    = &showspatial_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
