/*
 * Copyright (c) 2022 Paul B Mahol
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
#include <math.h>

#include "libavutil/tx.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/cpu.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "audio.h"
#include "video.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

enum FrequencyScale {
    FSCALE_LINEAR,
    FSCALE_LOG2,
    FSCALE_BARK,
    FSCALE_MEL,
    FSCALE_ERBS,
    NB_FSCALE
};

enum DirectionMode {
    DIRECTION_LR,
    DIRECTION_RL,
    DIRECTION_UD,
    DIRECTION_DU,
    NB_DIRECTION
};

enum SlideMode {
    SLIDE_REPLACE,
    SLIDE_SCROLL,
    SLIDE_FRAME,
    NB_SLIDE
};

typedef struct ShowCWTContext {
    const AVClass *class;
    int w, h;
    int mode;
    char *rate_str;
    AVRational auto_frame_rate;
    AVRational frame_rate;
    AVTXContext **fft;
    AVTXContext **ifft;
    av_tx_fn tx_fn;
    av_tx_fn itx_fn;
    int fft_in_size;
    int fft_out_size;
    int ifft_in_size;
    int ifft_out_size;
    int pos;
    int64_t in_pts;
    int64_t old_pts;
    int64_t eof_pts;
    float *frequency_band;
    AVFrame *kernel;
    unsigned *index;
    int *kernel_start;
    int *kernel_stop;
    AVFrame *cache[2];
    AVFrame *outpicref;
    AVFrame *fft_in;
    AVFrame *fft_out;
    AVFrame *ifft_in;
    AVFrame *ifft_out;
    AVFrame *ch_out;
    int nb_threads;
    int nb_channels;
    int nb_consumed_samples;
    int pps;
    int eof;
    int slide;
    int new_frame;
    int direction;
    int hop_size;
    int hop_index;
    int ihop_size;
    int ihop_index;
    int input_padding_size;
    int input_sample_count;
    int output_padding_size;
    int output_sample_count;
    int frequency_band_count;
    float logarithmic_basis;
    int frequency_scale;
    float minimum_frequency;
    float maximum_frequency;
    float deviation;
} ShowCWTContext;

#define OFFSET(x) offsetof(ShowCWTContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showcwt_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "rate", "set video rate",  OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "r",    "set video rate",  OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "scale", "set frequency scale", OFFSET(frequency_scale), AV_OPT_TYPE_INT,  {.i64=0}, 0, NB_FSCALE-1, FLAGS, "scale" },
    {  "linear",  "linear",           0,                       AV_OPT_TYPE_CONST,{.i64=FSCALE_LINEAR}, 0, 0, FLAGS, "scale" },
    {  "log2",    "logarithmic",      0,                       AV_OPT_TYPE_CONST,{.i64=FSCALE_LOG2},   0, 0, FLAGS, "scale" },
    {  "bark",    "bark",             0,                       AV_OPT_TYPE_CONST,{.i64=FSCALE_BARK},   0, 0, FLAGS, "scale" },
    {  "mel",     "mel",              0,                       AV_OPT_TYPE_CONST,{.i64=FSCALE_MEL},    0, 0, FLAGS, "scale" },
    {  "erbs",    "erbs",             0,                       AV_OPT_TYPE_CONST,{.i64=FSCALE_ERBS},   0, 0, FLAGS, "scale" },
    { "min",  "set minimum frequency", OFFSET(minimum_frequency), AV_OPT_TYPE_FLOAT, {.dbl = 20.}, 1, 2000, FLAGS },
    { "max",  "set maximum frequency", OFFSET(maximum_frequency), AV_OPT_TYPE_FLOAT, {.dbl = 20000.}, 0, 192000, FLAGS },
    { "logb", "set logarithmic basis", OFFSET(logarithmic_basis), AV_OPT_TYPE_FLOAT, {.dbl = 0.0001}, 0, 1, FLAGS },
    { "deviation", "set frequency deviation", OFFSET(deviation), AV_OPT_TYPE_FLOAT, {.dbl = 1.}, 0, 10, FLAGS },
    { "pps",  "set pixels per second", OFFSET(pps), AV_OPT_TYPE_INT, {.i64 = 64}, 1, 1024, FLAGS },
    { "mode", "set output mode", OFFSET(mode), AV_OPT_TYPE_INT,  {.i64=0}, 0, 4, FLAGS, "mode" },
    {  "magnitude", "magnitude",         0, AV_OPT_TYPE_CONST,{.i64=0}, 0, 0, FLAGS, "mode" },
    {  "phase",     "phase",             0, AV_OPT_TYPE_CONST,{.i64=1}, 0, 0, FLAGS, "mode" },
    {  "magphase",  "magnitude+phase",   0, AV_OPT_TYPE_CONST,{.i64=2}, 0, 0, FLAGS, "mode" },
    {  "channel",   "color per channel", 0, AV_OPT_TYPE_CONST,{.i64=3}, 0, 0, FLAGS, "mode" },
    {  "stereo",    "stereo difference", 0, AV_OPT_TYPE_CONST,{.i64=4}, 0, 0, FLAGS, "mode" },
    { "slide", "set slide mode", OFFSET(slide), AV_OPT_TYPE_INT,  {.i64=0}, 0, NB_SLIDE-1, FLAGS, "slide" },
    {  "replace", "replace", 0, AV_OPT_TYPE_CONST,{.i64=SLIDE_REPLACE},0, 0, FLAGS, "slide" },
    {  "scroll",  "scroll",  0, AV_OPT_TYPE_CONST,{.i64=SLIDE_SCROLL}, 0, 0, FLAGS, "slide" },
    {  "frame",   "frame",   0, AV_OPT_TYPE_CONST,{.i64=SLIDE_FRAME},  0, 0, FLAGS, "slide" },
    { "direction", "set direction mode", OFFSET(direction), AV_OPT_TYPE_INT,  {.i64=0}, 0, NB_DIRECTION-1, FLAGS, "direction" },
    {  "lr", "left to right", 0, AV_OPT_TYPE_CONST,{.i64=DIRECTION_LR}, 0, 0, FLAGS, "direction" },
    {  "rl", "right to left", 0, AV_OPT_TYPE_CONST,{.i64=DIRECTION_RL}, 0, 0, FLAGS, "direction" },
    {  "ud", "up to down",    0, AV_OPT_TYPE_CONST,{.i64=DIRECTION_UD}, 0, 0, FLAGS, "direction" },
    {  "du", "down to up",    0, AV_OPT_TYPE_CONST,{.i64=DIRECTION_DU}, 0, 0, FLAGS, "direction" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showcwt);

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowCWTContext *s = ctx->priv;

    av_freep(&s->frequency_band);
    av_freep(&s->kernel_start);
    av_freep(&s->kernel_stop);
    av_freep(&s->index);

    av_frame_free(&s->kernel);
    av_frame_free(&s->cache[0]);
    av_frame_free(&s->cache[1]);
    av_frame_free(&s->outpicref);
    av_frame_free(&s->fft_in);
    av_frame_free(&s->fft_out);
    av_frame_free(&s->ifft_in);
    av_frame_free(&s->ifft_out);
    av_frame_free(&s->ch_out);

    if (s->fft) {
        for (int n = 0; n < s->nb_threads; n++)
            av_tx_uninit(&s->fft[n]);
        av_freep(&s->fft);
    }

    if (s->ifft) {
        for (int n = 0; n < s->nb_threads; n++)
            av_tx_uninit(&s->ifft[n]);
        av_freep(&s->ifft);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.formats)) < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
        return ret;

    return 0;
}

static void frequency_band(float *frequency_band,
                           int frequency_band_count,
                           float frequency_range,
                           float frequency_offset,
                           int frequency_scale, float deviation)
{
    deviation *= sqrtf(1.f / (4.f * M_PI)); // Heisenberg Gabor Limit
    for (int y = 0; y < frequency_band_count; y++) {
        float frequency = frequency_range * (1.f - (float)y / frequency_band_count) + frequency_offset;
        float frequency_derivative = frequency_range / frequency_band_count;

        switch (frequency_scale) {
        case FSCALE_LOG2:
            frequency = powf(2.f, frequency);
            frequency_derivative *= logf(2.f) * frequency;
            break;
        case FSCALE_BARK:
            frequency = 600.f * sinhf(frequency / 6.f);
            frequency_derivative *= sqrtf(frequency * frequency + 360000.f) / 6.f;
            break;
        case FSCALE_MEL:
            frequency = 700.f * (powf(10.f, frequency / 2595.f) - 1.f);
            frequency_derivative *= (frequency + 700.f) * logf(10.f) / 2595.f;
            break;
        case FSCALE_ERBS:
            frequency = 676170.4f / (47.06538f - expf(frequency * 0.08950404f)) - 14678.49f;
            frequency_derivative *= (frequency * frequency + 14990.4 * frequency + 4577850.f) / 160514.f;
            break;
        }

        frequency_band[y*2  ] = frequency;
        frequency_band[y*2+1] = frequency_derivative * deviation;
    }
}

static float remap_log(float value, float log_factor)
{
    float sign = (0 < value) - (value < 0);

    value = logf(value * sign) * log_factor;

    return 1.f - av_clipf(value, 0.f, 1.f);
}

static int run_channel_cwt_prepare(AVFilterContext *ctx, void *arg, int jobnr, int ch)
{
    ShowCWTContext *s = ctx->priv;
    const int hop_size = s->hop_size;
    AVFrame *fin = arg;
    float *cache0 = (float *)s->cache[0]->extended_data[ch];
    float *cache = (float *)s->cache[1]->extended_data[ch];
    AVComplexFloat *src = (AVComplexFloat *)s->fft_in->extended_data[ch];
    AVComplexFloat *dst = (AVComplexFloat *)s->fft_out->extended_data[ch];

    if (fin) {
        const int offset = s->hop_index;
        const float *input = (const float *)fin->extended_data[ch];

        memcpy(&cache[offset], input,
               fin->nb_samples * sizeof(float));
    }

    if (fin == NULL) {
        memset(&cache[s->hop_index], 0,
               (hop_size - s->hop_index) * sizeof(float));
    } else if (s->hop_index + fin->nb_samples < hop_size) {
        return 0;
    }

    for (int n = 0; n < hop_size; n++) {
        src[n].re = cache0[n];
        src[n].im = 0.f;
        src[n + hop_size].re = cache[n];
        src[n + hop_size].im = 0.f;
    }

    s->tx_fn(s->fft[jobnr], dst, src, sizeof(*src));

    return 0;
}

static int draw(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowCWTContext *s = ctx->priv;
    const ptrdiff_t ylinesize = s->outpicref->linesize[0];
    const ptrdiff_t ulinesize = s->outpicref->linesize[1];
    const ptrdiff_t vlinesize = s->outpicref->linesize[2];
    const ptrdiff_t alinesize = s->outpicref->linesize[3];
    const float log_factor = 1.f/logf(s->logarithmic_basis);
    const int count = s->frequency_band_count;
    const int start = (count * jobnr) / nb_jobs;
    const int end = (count * (jobnr+1)) / nb_jobs;
    const int ihop_index = s->ihop_index;
    const int ihop_size = s->ihop_size;
    const int direction = s->direction;
    uint8_t *dstY, *dstU, *dstV, *dstA;
    const int mode = s->mode;
    const int w_1 = s->w - 1;
    const int x = s->pos;
    float Y, U, V;

    for (int y = start; y < end; y++) {
        const AVComplexFloat *src = ((const AVComplexFloat *)s->ch_out->extended_data[0]) +
                                                    y * ihop_size + ihop_index;

        switch (direction) {
        case DIRECTION_LR:
        case DIRECTION_RL:
            dstY = s->outpicref->data[0] + y * ylinesize;
            dstU = s->outpicref->data[1] + y * ulinesize;
            dstV = s->outpicref->data[2] + y * vlinesize;
            dstA = s->outpicref->data[3] ? s->outpicref->data[3] + y * alinesize : NULL;
            break;
        case DIRECTION_UD:
        case DIRECTION_DU:
            dstY = s->outpicref->data[0] + x * ylinesize + w_1 - y;
            dstU = s->outpicref->data[1] + x * ulinesize + w_1 - y;
            dstV = s->outpicref->data[2] + x * vlinesize + w_1 - y;
            dstA = s->outpicref->data[3] ? s->outpicref->data[3] + x * alinesize + w_1 - y : NULL;
            break;
        }

        switch (s->slide) {
        case SLIDE_REPLACE:
        case SLIDE_FRAME:
            /* nothing to do here */
            break;
        case SLIDE_SCROLL:
            switch (s->direction) {
            case DIRECTION_RL:
                memmove(dstY, dstY + 1, w_1);
                memmove(dstU, dstU + 1, w_1);
                memmove(dstV, dstV + 1, w_1);
                if (dstA != NULL)
                    memmove(dstA, dstA + 1, w_1);
                break;
            case DIRECTION_LR:
                memmove(dstY + 1, dstY, w_1);
                memmove(dstU + 1, dstU, w_1);
                memmove(dstV + 1, dstV, w_1);
                if (dstA != NULL)
                    memmove(dstA + 1, dstA, w_1);
                break;
            }
            break;
        }

        if (direction == DIRECTION_RL ||
            direction == DIRECTION_LR) {
            dstY += x;
            dstU += x;
            dstV += x;
            if (dstA != NULL)
                dstA += x;
        }

        switch (mode) {
        case 4:
            {
                const AVComplexFloat *src2 = ((const AVComplexFloat *)s->ch_out->extended_data[FFMIN(1, s->nb_channels - 1)]) +
                                               y * ihop_size + ihop_index;
                float z, u, v;

                z = hypotf(src[0].re + src2[0].re, src[0].im + src2[0].im);
                u = hypotf(src[0].re, src[0].im);
                v = hypotf(src2[0].re, src2[0].im);

                z  = remap_log(z, log_factor);
                u  = remap_log(u, log_factor);
                v  = remap_log(v, log_factor);

                Y  = z;
                U  = 0.5f + z * sinf((v - u) * M_PI_2);
                V  = 0.5f + z * sinf((u - v) * M_PI_2);

                dstY[0] = av_clip_uint8(lrintf(Y * 255.f));
                dstU[0] = av_clip_uint8(lrintf(U * 255.f));
                dstV[0] = av_clip_uint8(lrintf(V * 255.f));
                if (dstA)
                    dstA[0] = dstY[0];
            }
            break;
        case 3:
            {
                const int nb_channels = s->nb_channels;
                const float yf = 1.f / nb_channels;

                Y = 0.f;
                U = V = 0.5f;
                for (int ch = 0; ch < nb_channels; ch++) {
                    const AVComplexFloat *src = ((const AVComplexFloat *)s->ch_out->extended_data[ch]) +
                                                    y * ihop_size + ihop_index;
                    float z;

                    z = hypotf(src[0].re, src[0].im);
                    z = remap_log(z, log_factor);

                    Y += z * yf;
                    U += z * yf * sinf(2.f * M_PI * ch * yf);
                    V += z * yf * cosf(2.f * M_PI * ch * yf);
                }

                dstY[0] = av_clip_uint8(lrintf(Y * 255.f));
                dstU[0] = av_clip_uint8(lrintf(U * 255.f));
                dstV[0] = av_clip_uint8(lrintf(V * 255.f));
                if (dstA)
                    dstA[0] = dstY[0];
            }
            break;
        case 2:
            Y = hypotf(src[0].re, src[0].im);
            Y = remap_log(Y, log_factor);
            U = atan2f(src[0].im, src[0].re);
            U = 0.5f + 0.5f * U * Y / M_PI;
            V = 1.f - U;

            dstY[0] = av_clip_uint8(lrintf(Y * 255.f));
            dstU[0] = av_clip_uint8(lrintf(U * 255.f));
            dstV[0] = av_clip_uint8(lrintf(V * 255.f));
            if (dstA)
                dstA[0] = dstY[0];
            break;
        case 1:
            Y = atan2f(src[0].im, src[0].re);
            Y = 0.5f + 0.5f * Y / M_PI;

            dstY[0] = av_clip_uint8(lrintf(Y * 255.f));
            if (dstA)
                dstA[0] = dstY[0];
            break;
        case 0:
            Y = hypotf(src[0].re, src[0].im);
            Y = remap_log(Y, log_factor);

            dstY[0] = av_clip_uint8(lrintf(Y * 255.f));
            if (dstA)
                dstA[0] = dstY[0];
            break;
        }
    }

    return 0;
}

static int run_channel_cwt(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowCWTContext *s = ctx->priv;
    const int ch = *(int *)arg;
    AVComplexFloat *dst = (AVComplexFloat *)s->fft_out->extended_data[ch];
    const int output_sample_count = s->output_sample_count;
    const int ihop_size = s->ihop_size;
    const int ioffset = (s->output_padding_size - ihop_size) >> 1;
    const int count = s->frequency_band_count;
    const int start = (count * jobnr) / nb_jobs;
    const int end = (count * (jobnr+1)) / nb_jobs;

    for (int y = start; y < end; y++) {
        AVComplexFloat *isrc = (AVComplexFloat *)s->ifft_in->extended_data[y];
        AVComplexFloat *idst = (AVComplexFloat *)s->ifft_out->extended_data[y];
        AVComplexFloat *chout = ((AVComplexFloat *)s->ch_out->extended_data[ch]) + y * ihop_size;
        const float *kernel = (const float *)s->kernel->extended_data[y];
        const unsigned *index = (const unsigned *)s->index;
        const int kernel_start = s->kernel_start[y];
        const int kernel_stop = s->kernel_stop[y];

        memset(isrc, 0, sizeof(*isrc) * output_sample_count);
        for (int i = kernel_start; i < kernel_stop; i++) {
            const unsigned n = index[i];
            const float ff = kernel[i];

            isrc[n].re += ff * dst[i].re;
            isrc[n].im += ff * dst[i].im;
        }

        s->itx_fn(s->ifft[jobnr], idst, isrc, sizeof(*isrc));

        memcpy(chout, idst + ioffset, sizeof(*chout) * ihop_size);
    }

    return 0;
}

static void compute_kernel(AVFilterContext *ctx)
{
    ShowCWTContext *s = ctx->priv;
    const int size = s->input_sample_count;
    const float scale_factor = 1.f/(float)size;
    const int output_sample_count = s->output_sample_count;
    const int fsize = s->frequency_band_count;
    unsigned *index = s->index;

    for (int y = 0; y < fsize; y++) {
        float *kernel = (float *)s->kernel->extended_data[y];
        int *kernel_start = s->kernel_start;
        int *kernel_stop = s->kernel_stop;
        float frequency = s->frequency_band[y*2];
        float deviation = 1.f / (s->frequency_band[y*2+1] *
                                 output_sample_count);

        for (int n = 0; n < size; n++) {
            float ff, f = fabsf(n-frequency);

            f = size - fabsf(f - size);
            ff = expf(-f*f*deviation) * scale_factor;
            kernel[n] = ff;
        }

        for (int n = 0; n < size; n++) {
            if (kernel[n] != 0.f) {
                kernel_start[y] = n;
                break;
            }
        }

        for (int n = 0; n < size; n++) {
            if (kernel[size - n - 1] != 0.f) {
                kernel_stop[y] = size - n;
                break;
            }
        }
    }

    for (int n = 0; n < size; n++)
        index[n] = n % output_sample_count;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowCWTContext *s = ctx->priv;
    float maximum_frequency = fminf(s->maximum_frequency, inlink->sample_rate * 0.5f);
    float minimum_frequency = s->minimum_frequency;
    float scale = 1.f, factor;
    int ret;

    uninit(ctx);

    switch (s->direction) {
    case DIRECTION_LR:
    case DIRECTION_RL:
        s->frequency_band_count = s->h;
        break;
    case DIRECTION_UD:
    case DIRECTION_DU:
        s->frequency_band_count = s->w;
        break;
    }

    s->new_frame = 1;
    s->nb_threads = FFMIN(s->frequency_band_count, ff_filter_get_nb_threads(ctx));
    s->nb_channels = inlink->ch_layout.nb_channels;
    s->old_pts = AV_NOPTS_VALUE;
    s->eof_pts = AV_NOPTS_VALUE;
    s->nb_consumed_samples = 65536;

    s->input_sample_count = s->nb_consumed_samples;
    s->hop_size = s->nb_consumed_samples >> 1;
    s->input_padding_size = 65536;
    s->output_padding_size = FFMAX(16, s->input_padding_size * s->pps / inlink->sample_rate);

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    s->fft_in_size  = FFALIGN(s->input_padding_size, av_cpu_max_align());
    s->fft_out_size = FFALIGN(s->input_padding_size, av_cpu_max_align());

    s->output_sample_count = s->output_padding_size;

    s->ifft_in_size = FFALIGN(s->output_padding_size, av_cpu_max_align());
    s->ifft_out_size = FFALIGN(s->output_padding_size, av_cpu_max_align());
    s->ihop_size = s->output_padding_size >> 1;

    s->fft = av_calloc(s->nb_threads, sizeof(*s->fft));
    if (!s->fft)
        return AVERROR(ENOMEM);

    for (int n = 0; n < s->nb_threads; n++) {
        ret = av_tx_init(&s->fft[n], &s->tx_fn, AV_TX_FLOAT_FFT, 0, s->input_padding_size, &scale, 0);
        if (ret < 0)
            return ret;
    }

    s->ifft = av_calloc(s->nb_threads, sizeof(*s->ifft));
    if (!s->ifft)
        return AVERROR(ENOMEM);

    for (int n = 0; n < s->nb_threads; n++) {
        ret = av_tx_init(&s->ifft[n], &s->itx_fn, AV_TX_FLOAT_FFT, 1, s->output_padding_size, &scale, 0);
        if (ret < 0)
            return ret;
    }

    s->frequency_band = av_calloc(s->frequency_band_count,
                                  sizeof(*s->frequency_band) * 2);
    s->outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    s->fft_in = ff_get_audio_buffer(inlink, s->fft_in_size * 2);
    s->fft_out = ff_get_audio_buffer(inlink, s->fft_out_size * 2);
    s->cache[0] = ff_get_audio_buffer(inlink, s->hop_size);
    s->cache[1] = ff_get_audio_buffer(inlink, s->hop_size);
    s->ch_out = ff_get_audio_buffer(inlink, s->frequency_band_count * 2 * s->ihop_size);
    s->ifft_in = av_frame_alloc();
    s->ifft_out = av_frame_alloc();
    s->kernel = av_frame_alloc();
    s->index = av_calloc(s->input_padding_size, sizeof(*s->index));
    s->kernel_start = av_calloc(s->frequency_band_count, sizeof(*s->kernel_start));
    s->kernel_stop = av_calloc(s->frequency_band_count, sizeof(*s->kernel_stop));
    if (!s->outpicref || !s->fft_in || !s->fft_out ||
        !s->ifft_in || !s->ifft_out || !s->kernel_start || !s->kernel_stop ||
        !s->frequency_band || !s->kernel || !s->cache[0] || !s->cache[1] || !s->index)
        return AVERROR(ENOMEM);

    s->ifft_in->format     = inlink->format;
    s->ifft_in->nb_samples = s->ifft_in_size * 2;
    s->ifft_in->ch_layout.nb_channels = s->frequency_band_count;
    ret = av_frame_get_buffer(s->ifft_in, 0);
    if (ret < 0)
        return ret;

    s->ifft_out->format     = inlink->format;
    s->ifft_out->nb_samples = s->ifft_out_size * 2;
    s->ifft_out->ch_layout.nb_channels = s->frequency_band_count;
    ret = av_frame_get_buffer(s->ifft_out, 0);
    if (ret < 0)
        return ret;

    s->kernel->format     = inlink->format;
    s->kernel->nb_samples = s->input_padding_size;
    s->kernel->ch_layout.nb_channels = s->frequency_band_count;
    ret = av_frame_get_buffer(s->kernel, 0);
    if (ret < 0)
        return ret;

    s->outpicref->sample_aspect_ratio = (AVRational){1,1};

    for (int y = 0; y < outlink->h; y++) {
        memset(s->outpicref->data[0] + y * s->outpicref->linesize[0],   0, outlink->w);
        memset(s->outpicref->data[1] + y * s->outpicref->linesize[1], 128, outlink->w);
        memset(s->outpicref->data[2] + y * s->outpicref->linesize[2], 128, outlink->w);
        if (s->outpicref->data[3])
            memset(s->outpicref->data[3] + y * s->outpicref->linesize[3], 0, outlink->w);
    }

    s->outpicref->color_range = AVCOL_RANGE_JPEG;

    factor = s->nb_consumed_samples / (float)inlink->sample_rate;
    minimum_frequency *= factor;
    maximum_frequency *= factor;

    switch (s->frequency_scale) {
    case FSCALE_LOG2:
        minimum_frequency = logf(minimum_frequency) / logf(2.f);
        maximum_frequency = logf(maximum_frequency) / logf(2.f);
        break;
    case FSCALE_BARK:
        minimum_frequency = 6.f * asinhf(minimum_frequency / 600.f);
        maximum_frequency = 6.f * asinhf(maximum_frequency / 600.f);
        break;
    case FSCALE_MEL:
        minimum_frequency = 2595.f * log10f(1.f + minimum_frequency / 700.f);
        maximum_frequency = 2595.f * log10f(1.f + maximum_frequency / 700.f);
        break;
    case FSCALE_ERBS:
        minimum_frequency = 11.17268f * log(1.f + (46.06538f * minimum_frequency) / (minimum_frequency + 14678.49f));
        maximum_frequency = 11.17268f * log(1.f + (46.06538f * maximum_frequency) / (maximum_frequency + 14678.49f));
        break;
    }

    frequency_band(s->frequency_band,
                   s->frequency_band_count, maximum_frequency - minimum_frequency,
                   minimum_frequency, s->frequency_scale, s->deviation);

    av_log(ctx, AV_LOG_DEBUG, "input_sample_count: %d\n", s->input_sample_count);
    av_log(ctx, AV_LOG_DEBUG, "output_sample_count: %d\n", s->output_sample_count);

    switch (s->direction) {
    case DIRECTION_LR:
        s->pos = 0;
        break;
    case DIRECTION_RL:
        s->pos = s->w - 1;
        break;
    case DIRECTION_UD:
        s->pos = 0;
        break;
    case DIRECTION_DU:
        s->pos = s->h - 1;
        break;
    }

    s->auto_frame_rate = av_make_q(inlink->sample_rate, s->hop_size);
    if (strcmp(s->rate_str, "auto")) {
        ret = av_parse_video_rate(&s->frame_rate, s->rate_str);
    } else {
        s->frame_rate = s->auto_frame_rate;
    }
    outlink->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(outlink->frame_rate);

    compute_kernel(ctx);

    return 0;
}

static int output_frame(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    ShowCWTContext *s = ctx->priv;
    const int nb_planes = 3 + (s->outpicref->data[3] != NULL);
    int ret;

    switch (s->slide) {
    case SLIDE_SCROLL:
        switch (s->direction) {
        case DIRECTION_UD:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];

                for (int y = s->h - 1; y > 0; y--) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize;

                    memmove(dst, dst - linesize, s->w);
                }
            }
            break;
        case DIRECTION_DU:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];

                for (int y = 0; y < s->h - 1; y++) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize;

                    memmove(dst, dst + linesize, s->w);
                }
            }
            break;
        }
        break;
    }

    ff_filter_execute(ctx, draw, NULL, NULL, s->nb_threads);

    switch (s->slide) {
    case SLIDE_REPLACE:
    case SLIDE_FRAME:
        switch (s->direction) {
        case DIRECTION_LR:
            s->pos++;
            if (s->pos >= s->w) {
                s->pos = 0;
                s->new_frame = 1;
            }
            break;
        case DIRECTION_RL:
            s->pos--;
            if (s->pos < 0) {
                s->pos = s->w - 1;
                s->new_frame = 1;
            }
            break;
        case DIRECTION_UD:
            s->pos++;
            if (s->pos >= s->h) {
                s->pos = 0;
                s->new_frame = 1;
            }
            break;
        case DIRECTION_DU:
            s->pos--;
            if (s->pos < 0) {
                s->pos = s->h - 1;
                s->new_frame = 1;
            }
            break;
        }
        break;
    case SLIDE_SCROLL:
        switch (s->direction) {
        case DIRECTION_UD:
        case DIRECTION_LR:
            s->pos = 0;
            break;
        case DIRECTION_RL:
            s->pos = s->w - 1;
            break;
        case DIRECTION_DU:
            s->pos = s->h - 1;
            break;
        }
        break;
    }

    if (s->slide == SLIDE_FRAME && s->eof) {
        switch (s->direction) {
        case DIRECTION_LR:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];
                const int size = s->w - s->pos;
                const int fill = p > 0 && p < 3 ? 128 : 0;
                const int x = s->pos;

                for (int y = 0; y < s->h; y++) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize + x;

                    memset(dst, fill, size);
                }
            }
            break;
        case DIRECTION_RL:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];
                const int size = s->w - s->pos;
                const int fill = p > 0 && p < 3 ? 128 : 0;

                for (int y = 0; y < s->h; y++) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize;

                    memset(dst, fill, size);
                }
            }
            break;
        case DIRECTION_UD:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];
                const int fill = p > 0 && p < 3 ? 128 : 0;

                for (int y = s->pos; y < s->h; y++) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize;

                    memset(dst, fill, s->w);
                }
            }
            break;
        case DIRECTION_DU:
            for (int p = 0; p < nb_planes; p++) {
                ptrdiff_t linesize = s->outpicref->linesize[p];
                const int fill = p > 0 && p < 3 ? 128 : 0;

                for (int y = s->h - s->pos; y >= 0; y--) {
                    uint8_t *dst = s->outpicref->data[p] + y * linesize;

                    memset(dst, fill, s->w);
                }
            }
            break;
        }
    }

    s->new_frame = s->slide == SLIDE_FRAME && (s->new_frame || s->eof);

    if (s->slide != SLIDE_FRAME || s->new_frame == 1) {
        int64_t pts_offset = s->new_frame ? 0LL : av_rescale(s->ihop_index, s->hop_size, s->ihop_size);

        s->outpicref->pts = av_rescale_q(s->in_pts + pts_offset, inlink->time_base, outlink->time_base);
        s->outpicref->duration = 1;
    }

    s->ihop_index++;
    if (s->ihop_index >= s->ihop_size)
        s->ihop_index = 0;

    if (s->slide == SLIDE_FRAME && s->new_frame == 0)
        return 1;

    if (s->old_pts < s->outpicref->pts) {
        AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        ret = av_frame_copy_props(out, s->outpicref);
        if (ret < 0)
            goto fail;
        ret = av_frame_copy(out, s->outpicref);
        if (ret < 0)
            goto fail;
        s->old_pts = s->outpicref->pts;
        s->new_frame = 0;
        ret = ff_filter_frame(outlink, out);
        if (ret <= 0)
            return ret;
fail:
        av_frame_free(&out);
        return ret;
    }

    return 1;
}

static int run_channels_cwt_prepare(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowCWTContext *s = ctx->priv;
    const int count = s->nb_channels;
    const int start = (count * jobnr) / nb_jobs;
    const int end = (count * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++)
        run_channel_cwt_prepare(ctx, arg, jobnr, ch);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    ShowCWTContext *s = ctx->priv;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->outpicref) {
        AVFrame *fin = NULL;

        if (s->ihop_index == 0) {
            if (!s->eof) {
                ret = ff_inlink_consume_samples(inlink, 1, s->hop_size - s->hop_index, &fin);
                if (ret < 0)
                    return ret;
            }

            if (ret > 0 || s->eof) {
                ff_filter_execute(ctx, run_channels_cwt_prepare, fin, NULL,
                                  FFMIN(s->nb_threads, s->nb_channels));
                if (fin) {
                    if ((s->hop_index == 0 && s->slide != SLIDE_FRAME) || s->new_frame) {
                        s->in_pts = fin->pts;
                        s->new_frame = 0;
                    }
                    s->hop_index += fin->nb_samples;
                    av_frame_free(&fin);
                } else {
                    s->hop_index = s->hop_size;
                }
            }
        }

        if (s->hop_index >= s->hop_size || s->ihop_index > 0) {
            if (s->hop_index) {
                FFSWAP(AVFrame *, s->cache[0], s->cache[1]);
                s->hop_index = 0;
            }

            for (int ch = 0; ch < s->nb_channels && s->ihop_index == 0; ch++) {
                ff_filter_execute(ctx, run_channel_cwt, (void *)&ch, NULL,
                                  s->nb_threads);
            }

            ret = output_frame(ctx);
            if (ret != 1)
                return ret;
        }
    }

    if (s->eof && s->eof_pts != AV_NOPTS_VALUE &&
        (s->old_pts + 1 >= s->eof_pts || (s->slide == SLIDE_FRAME))) {
        if (s->slide == SLIDE_FRAME)
            ret = output_frame(ctx);
        ff_outlink_set_status(outlink, AVERROR_EOF, s->eof_pts);
        return ret;
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            s->eof = 1;
            ff_filter_set_ready(ctx, 10);
            s->eof_pts = av_rescale_q(pts, inlink->time_base, outlink->time_base);
            return 0;
        }
    }

    if (ff_inlink_queued_samples(inlink) > 0 || s->ihop_index ||
        s->hop_index >= s->hop_size || s->eof) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        ff_inlink_request_frame(inlink);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad showcwt_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad showcwt_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_avf_showcwt = {
    .name          = "showcwt",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a CWT (Continuous Wavelet Transform) spectrum video output."),
    .uninit        = uninit,
    .priv_size     = sizeof(ShowCWTContext),
    FILTER_INPUTS(showcwt_inputs),
    FILTER_OUTPUTS(showcwt_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .activate      = activate,
    .priv_class    = &showcwt_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
