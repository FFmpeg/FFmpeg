/*
 * Copyright (c) 2001 Heikki Leinonen
 * Copyright (c) 2001 Chris Bagwell
 * Copyright (c) 2003 Donnie Smith
 * Copyright (c) 2014 Paul B Mahol
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

#include <float.h> /* DBL_MAX */

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "formats.h"
#include "avfilter.h"
#include "internal.h"

enum SilenceDetect {
    D_PEAK,
    D_RMS,
};

enum ThresholdMode {
    T_ANY,
    T_ALL,
};

enum SilenceMode {
    SILENCE_TRIM,
    SILENCE_TRIM_FLUSH,
    SILENCE_COPY,
    SILENCE_COPY_FLUSH,
    SILENCE_STOP
};

typedef struct SilenceRemoveContext {
    const AVClass *class;

    enum SilenceMode mode;

    int start_periods;
    int64_t start_duration;
    int64_t start_duration_opt;
    double start_threshold;
    int64_t start_silence;
    int64_t start_silence_opt;
    int start_mode;

    int stop_periods;
    int64_t stop_duration;
    int64_t stop_duration_opt;
    double stop_threshold;
    int64_t stop_silence;
    int64_t stop_silence_opt;
    int stop_mode;

    int64_t window_duration_opt;

    AVFrame *start_holdoff;
    AVFrame *start_silence_hold;
    size_t start_holdoff_offset;
    size_t start_holdoff_end;
    size_t start_silence_offset;
    size_t start_silence_end;
    int    start_found_periods;

    AVFrame *stop_holdoff;
    AVFrame *stop_silence_hold;
    size_t stop_holdoff_offset;
    size_t stop_holdoff_end;
    size_t stop_silence_offset;
    size_t stop_silence_end;
    int    stop_found_periods;

    AVFrame *window;
    int window_offset;
    int64_t window_duration;
    double sum;

    int one_period;
    int restart;
    int64_t next_pts;

    int detection;
    void (*update)(struct SilenceRemoveContext *s, AVFrame *frame, int ch, int offset);
    double (*compute)(struct SilenceRemoveContext *s, AVFrame *frame, int ch, int offset);
    void (*copy)(struct SilenceRemoveContext *s, AVFrame *out, AVFrame *in,
                 int ch, int out_offset, int in_offset);

    AVAudioFifo *fifo;
} SilenceRemoveContext;

#define OFFSET(x) offsetof(SilenceRemoveContext, x)
#define AF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM

static const AVOption silenceremove_options[] = {
    { "start_periods",   "set periods of silence parts to skip from start",    OFFSET(start_periods),       AV_OPT_TYPE_INT,      {.i64=0},     0,      9000, AF },
    { "start_duration",  "set start duration of non-silence part",             OFFSET(start_duration_opt),  AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_threshold", "set threshold for start silence detection",          OFFSET(start_threshold),     AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "start_silence",   "set start duration of silence part to keep",         OFFSET(start_silence_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_mode",      "set which channel will trigger trimming from start", OFFSET(start_mode),          AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    {   "any",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ANY}, 0,         0, AF, "mode" },
    {   "all",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ALL}, 0,         0, AF, "mode" },
    { "stop_periods",    "set periods of silence parts to skip from end",      OFFSET(stop_periods),        AV_OPT_TYPE_INT,      {.i64=0}, -9000,      9000, AF },
    { "stop_duration",   "set stop duration of non-silence part",              OFFSET(stop_duration_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_threshold",  "set threshold for stop silence detection",           OFFSET(stop_threshold),      AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "stop_silence",    "set stop duration of silence part to keep",          OFFSET(stop_silence_opt),    AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_mode",       "set which channel will trigger trimming from end",   OFFSET(stop_mode),           AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    { "detection",       "set how silence is detected",                        OFFSET(detection),           AV_OPT_TYPE_INT,      {.i64=D_RMS}, D_PEAK,D_RMS, AF, "detection" },
    {   "peak",          "use absolute values of samples",                     0,                           AV_OPT_TYPE_CONST,    {.i64=D_PEAK},0,         0, AF, "detection" },
    {   "rms",           "use squared values of samples",                      0,                           AV_OPT_TYPE_CONST,    {.i64=D_RMS}, 0,         0, AF, "detection" },
    { "window",          "set duration of window for silence detection",       OFFSET(window_duration_opt), AV_OPT_TYPE_DURATION, {.i64=20000}, 0, 100000000, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silenceremove);

static void copy_double(SilenceRemoveContext *s, AVFrame *out, AVFrame *in,
                        int ch, int out_offset, int in_offset)
{
    const double *srcp = (const double *)in->data[0];
    const double src = srcp[in->ch_layout.nb_channels * in_offset + ch];
    double *dstp = (double *)out->data[0];

    dstp[out->ch_layout.nb_channels * out_offset + ch] = src;
}

static void copy_doublep(SilenceRemoveContext *s, AVFrame *out, AVFrame *in,
                         int ch, int out_offset, int in_offset)
{
    const double *srcp = (const double *)in->extended_data[ch];
    const double src = srcp[in_offset];
    double *dstp = (double *)out->extended_data[ch];

    dstp[out_offset] = src;
}

static void copy_float(SilenceRemoveContext *s, AVFrame *out, AVFrame *in,
                       int ch, int out_offset, int in_offset)
{
    const float *srcp = (const float *)in->data[0];
    const float src = srcp[in->ch_layout.nb_channels * in_offset + ch];
    float *dstp = (float *)out->data[0];

    dstp[out->ch_layout.nb_channels * out_offset + ch] = src;
}

static void copy_floatp(SilenceRemoveContext *s, AVFrame *out, AVFrame *in,
                        int ch, int out_offset, int in_offset)
{
    const float *srcp = (const float *)in->extended_data[ch];
    const float src = srcp[in_offset];
    float *dstp = (float *)out->extended_data[ch];

    dstp[out_offset] = src;
}

static double compute_peak_double(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->data[0];
    const double *wsamples = (const double *)s->window->data[0];
    double sample = samples[frame->ch_layout.nb_channels * offset + ch];
    double wsample = wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];
    double new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmax(new_sum, 0.);
    new_sum += fabs(sample);

    return new_sum / s->window_duration;
}

static void update_peak_double(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->data[0];
    double *wsamples = (double *)s->window->data[0];
    double sample = samples[frame->ch_layout.nb_channels * offset + ch];
    double *wsample = &wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];

    s->sum -= *wsample;
    s->sum  = fmax(s->sum, 0.);
    *wsample = fabs(sample);
    s->sum += *wsample;
}

static double compute_peak_float(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->data[0];
    const float *wsamples = (const float *)s->window->data[0];
    float sample = samples[frame->ch_layout.nb_channels * offset + ch];
    float wsample = wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];
    float new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmaxf(new_sum, 0.f);
    new_sum += fabsf(sample);

    return new_sum / s->window_duration;
}

static void update_peak_float(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->data[0];
    float *wsamples = (float *)s->window->data[0];
    float sample = samples[frame->ch_layout.nb_channels * offset + ch];
    float *wsample = &wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];

    s->sum -= *wsample;
    s->sum  = fmaxf(s->sum, 0.f);
    *wsample = fabsf(sample);
    s->sum += *wsample;
}

static double compute_rms_double(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->data[0];
    const double *wsamples = (const double *)s->window->data[0];
    double sample = samples[frame->ch_layout.nb_channels * offset + ch];
    double wsample = wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];
    double new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmax(new_sum, 0.);
    new_sum += sample * sample;

    av_assert2(new_sum >= 0.);
    return sqrt(new_sum / s->window_duration);
}

static void update_rms_double(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->data[0];
    double *wsamples = (double *)s->window->data[0];
    double sample = samples[frame->ch_layout.nb_channels * offset + ch];
    double *wsample = &wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];

    s->sum -= *wsample;
    s->sum  = fmax(s->sum, 0.);
    *wsample = sample * sample;
    s->sum += *wsample;
}

static double compute_rms_float(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->data[0];
    const float *wsamples = (const float *)s->window->data[0];
    float sample = samples[frame->ch_layout.nb_channels * offset + ch];
    float wsample = wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];
    float new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmaxf(new_sum, 0.f);
    new_sum += sample * sample;

    av_assert2(new_sum >= 0.f);
    return sqrtf(new_sum / s->window_duration);
}

static void update_rms_float(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->data[0];
    float sample = samples[frame->ch_layout.nb_channels * offset + ch];
    float *wsamples = (float *)s->window->data[0];
    float *wsample = &wsamples[frame->ch_layout.nb_channels * s->window_offset + ch];

    s->sum -= *wsample;
    s->sum  = fmaxf(s->sum, 0.f);
    *wsample = sample * sample;
    s->sum += *wsample;
}

static double compute_peak_doublep(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->extended_data[ch];
    const double *wsamples = (const double *)s->window->extended_data[ch];
    double sample = samples[offset];
    double wsample = wsamples[s->window_offset];
    double new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmax(new_sum, 0.);
    new_sum += fabs(sample);

    return new_sum / s->window_duration;
}

static void update_peak_doublep(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->extended_data[ch];
    double *wsamples = (double *)s->window->extended_data[ch];
    double sample = samples[offset];
    double *wsample = &wsamples[s->window_offset];

    s->sum -= *wsample;
    s->sum  = fmax(s->sum, 0.);
    *wsample = fabs(sample);
    s->sum += *wsample;
}

static double compute_peak_floatp(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->extended_data[ch];
    const float *wsamples = (const float *)s->window->extended_data[ch];
    float sample = samples[offset];
    float wsample = wsamples[s->window_offset];
    float new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmaxf(new_sum, 0.f);
    new_sum += fabsf(sample);

    return new_sum / s->window_duration;
}

static void update_peak_floatp(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->extended_data[ch];
    float *wsamples = (float *)s->window->extended_data[ch];
    float sample = samples[offset];
    float *wsample = &wsamples[s->window_offset];

    s->sum -= *wsample;
    s->sum  = fmaxf(s->sum, 0.f);
    *wsample = fabsf(sample);
    s->sum += *wsample;
}

static double compute_rms_doublep(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->extended_data[ch];
    const double *wsamples = (const double *)s->window->extended_data[ch];
    double sample = samples[offset];
    double wsample = wsamples[s->window_offset];
    double new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmax(new_sum, 0.);
    new_sum += sample * sample;

    av_assert2(new_sum >= 0.);
    return sqrt(new_sum / s->window_duration);
}

static void update_rms_doublep(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const double *samples = (const double *)frame->extended_data[ch];
    double *wsamples = (double *)s->window->extended_data[ch];
    double sample = samples[offset];
    double *wsample = &wsamples[s->window_offset];

    s->sum -= *wsample;
    s->sum  = fmax(s->sum, 0.);
    *wsample = sample * sample;
    s->sum += *wsample;
}

static double compute_rms_floatp(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->extended_data[ch];
    const float *wsamples = (const float *)s->window->extended_data[ch];
    float sample = samples[offset];
    float wsample = wsamples[s->window_offset];
    float new_sum;

    new_sum  = s->sum;
    new_sum -= wsample;
    new_sum  = fmaxf(new_sum, 0.f);
    new_sum += sample * sample;

    av_assert2(new_sum >= 0.f);
    return sqrtf(new_sum / s->window_duration);
}

static void update_rms_floatp(SilenceRemoveContext *s, AVFrame *frame, int ch, int offset)
{
    const float *samples = (const float *)frame->extended_data[ch];
    float *wsamples = (float *)s->window->extended_data[ch];
    float sample = samples[offset];
    float *wsample = &wsamples[s->window_offset];

    s->sum -= *wsample;
    s->sum  = fmaxf(s->sum, 0.f);
    *wsample = sample * sample;
    s->sum += *wsample;
}

static av_cold int init(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    if (s->stop_periods < 0) {
        s->stop_periods = -s->stop_periods;
        s->restart = 1;
    }

    return 0;
}

static void clear_window(SilenceRemoveContext *s)
{
    av_samples_set_silence(s->window->extended_data, 0, s->window_duration,
                           s->window->ch_layout.nb_channels, s->window->format);

    s->window_offset = 0;
    s->sum = 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SilenceRemoveContext *s = ctx->priv;

    s->next_pts = AV_NOPTS_VALUE;
    s->window_duration = av_rescale(s->window_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->window_duration = FFMAX(1, s->window_duration);
    s->window = ff_get_audio_buffer(ctx->outputs[0], s->window_duration);
    if (!s->window)
        return AVERROR(ENOMEM);

    clear_window(s);

    s->start_duration = av_rescale(s->start_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->start_silence  = av_rescale(s->start_silence_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->stop_duration  = av_rescale(s->stop_duration_opt, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->stop_silence   = av_rescale(s->stop_silence_opt, inlink->sample_rate,
                                   AV_TIME_BASE);

    s->start_holdoff = ff_get_audio_buffer(ctx->outputs[0],
                                           FFMAX(s->start_duration, 1));
    if (!s->start_holdoff)
        return AVERROR(ENOMEM);

    s->start_silence_hold = ff_get_audio_buffer(ctx->outputs[0],
                                                FFMAX(s->start_silence, 1));
    if (!s->start_silence_hold)
        return AVERROR(ENOMEM);

    s->start_holdoff_offset = 0;
    s->start_holdoff_end    = 0;
    s->start_found_periods  = 0;

    s->stop_holdoff = ff_get_audio_buffer(ctx->outputs[0],
                                          FFMAX(s->stop_duration, 1));
    if (!s->stop_holdoff)
        return AVERROR(ENOMEM);

    s->stop_silence_hold = ff_get_audio_buffer(ctx->outputs[0],
                                               FFMAX(s->stop_silence, 1));
    if (!s->stop_silence_hold)
        return AVERROR(ENOMEM);

    s->stop_holdoff_offset = 0;
    s->stop_holdoff_end    = 0;
    s->stop_found_periods  = 0;

    if (s->start_periods)
        s->mode = SILENCE_TRIM;
    else
        s->mode = SILENCE_COPY;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBL:
        s->copy = copy_double;
        switch (s->detection) {
        case D_PEAK:
            s->update = update_peak_double;
            s->compute = compute_peak_double;
            break;
        case D_RMS:
            s->update = update_rms_double;
            s->compute = compute_rms_double;
            break;
        }
        break;
    case AV_SAMPLE_FMT_FLT:
        s->copy = copy_float;
        switch (s->detection) {
        case D_PEAK:
            s->update = update_peak_float;
            s->compute = compute_peak_float;
            break;
        case D_RMS:
            s->update = update_rms_float;
            s->compute = compute_rms_float;
            break;
        }
        break;
    case AV_SAMPLE_FMT_DBLP:
        s->copy = copy_doublep;
        switch (s->detection) {
        case D_PEAK:
            s->update = update_peak_doublep;
            s->compute = compute_peak_doublep;
            break;
        case D_RMS:
            s->update = update_rms_doublep;
            s->compute = compute_rms_doublep;
            break;
        }
        break;
    case AV_SAMPLE_FMT_FLTP:
        s->copy = copy_floatp;
        switch (s->detection) {
        case D_PEAK:
            s->update = update_peak_floatp;
            s->compute = compute_peak_floatp;
            break;
        case D_RMS:
            s->update = update_rms_floatp;
            s->compute = compute_rms_floatp;
            break;
        }
        break;
    default:
        return AVERROR_BUG;
    }

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->ch_layout.nb_channels, 1024);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    return 0;
}

static void flush(SilenceRemoveContext *s,
                  AVFrame *out, AVFilterLink *outlink,
                  int *nb_samples_written, int flush_silence)
{
    AVFrame *silence;

    if (*nb_samples_written) {
        out->nb_samples = *nb_samples_written;

        av_audio_fifo_write(s->fifo, (void **)out->extended_data, out->nb_samples);
        *nb_samples_written = 0;
    }

    av_frame_free(&out);

    if (s->stop_silence_end <= 0 || !flush_silence)
        return;

    silence = ff_get_audio_buffer(outlink, s->stop_silence_end);
    if (!silence)
        return;

    if (s->stop_silence_offset < s->stop_silence_end) {
        av_samples_copy(silence->extended_data, s->stop_silence_hold->extended_data, 0,
                        s->stop_silence_offset,
                        s->stop_silence_end - s->stop_silence_offset,
                        outlink->ch_layout.nb_channels, outlink->format);
    }

    if (s->stop_silence_offset > 0) {
        av_samples_copy(silence->extended_data, s->stop_silence_hold->extended_data,
                        s->stop_silence_end - s->stop_silence_offset,
                        0, s->stop_silence_offset,
                        outlink->ch_layout.nb_channels, outlink->format);
    }

    s->stop_silence_offset = 0;
    s->stop_silence_end = 0;

    av_audio_fifo_write(s->fifo, (void **)silence->extended_data, silence->nb_samples);
    av_frame_free(&silence);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    SilenceRemoveContext *s = ctx->priv;
    int nbs, nb_samples_read, nb_samples_written;
    int i, j, threshold, ret = 0;
    AVFrame *out;

    nb_samples_read = nb_samples_written = 0;

    if (s->next_pts == AV_NOPTS_VALUE)
        s->next_pts = in->pts;

    switch (s->mode) {
    case SILENCE_TRIM:
silence_trim:
        nbs = in->nb_samples - nb_samples_read;
        if (!nbs)
            break;

        for (i = 0; i < nbs; i++) {
            if (s->start_mode == T_ANY) {
                threshold = 0;
                for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                    threshold |= s->compute(s, in, j, nb_samples_read) > s->start_threshold;
                }
            } else {
                threshold = 1;
                for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                    threshold &= s->compute(s, in, j, nb_samples_read) > s->start_threshold;
                }
            }

            if (threshold) {
                for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                    s->update(s, in, j, nb_samples_read);
                    s->copy(s, s->start_holdoff, in, j, s->start_holdoff_end, nb_samples_read);
                }

                s->window_offset++;
                if (s->window_offset >= s->window_duration)
                    s->window_offset = 0;
                s->start_holdoff_end++;
                nb_samples_read++;

                if (s->start_holdoff_end >= s->start_duration) {
                    s->start_found_periods += s->one_period >= 1;
                    s->one_period = 0;
                    if (s->start_found_periods >= s->start_periods) {
                        s->mode = SILENCE_TRIM_FLUSH;
                        goto silence_trim_flush;
                    }

                    s->start_holdoff_offset = 0;
                    s->start_holdoff_end = 0;
                    s->start_silence_offset = 0;
                    s->start_silence_end = 0;
                }
            } else {
                s->start_holdoff_end = 0;
                s->one_period++;

                for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                    s->update(s, in, j, nb_samples_read);
                    if (s->start_silence)
                        s->copy(s, s->start_silence_hold, in, j, s->start_silence_offset, nb_samples_read);
                }

                s->window_offset++;
                if (s->window_offset >= s->window_duration)
                    s->window_offset = 0;
                nb_samples_read++;
                s->start_silence_offset++;

                if (s->start_silence) {
                    s->start_silence_end = FFMIN(s->start_silence_end + 1, s->start_silence);
                    if (s->start_silence_offset >= s->start_silence)
                        s->start_silence_offset = 0;
                }
            }
        }
        break;

    case SILENCE_TRIM_FLUSH:
silence_trim_flush:
        nbs  = s->start_holdoff_end - s->start_holdoff_offset;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs + s->start_silence_end);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        if (s->start_silence_end > 0) {
            if (s->start_silence_offset < s->start_silence_end) {
                av_samples_copy(out->extended_data, s->start_silence_hold->extended_data, 0,
                                s->start_silence_offset,
                                s->start_silence_end - s->start_silence_offset,
                                outlink->ch_layout.nb_channels, outlink->format);
            }

            if (s->start_silence_offset > 0) {
                av_samples_copy(out->extended_data, s->start_silence_hold->extended_data,
                                s->start_silence_end - s->start_silence_offset,
                                0, s->start_silence_offset,
                                outlink->ch_layout.nb_channels, outlink->format);
            }
        }

        av_samples_copy(out->extended_data, s->start_holdoff->extended_data,
                        s->start_silence_end,
                        s->start_holdoff_offset, nbs,
                        outlink->ch_layout.nb_channels, outlink->format);

        s->start_holdoff_offset += nbs;

        av_audio_fifo_write(s->fifo, (void **)out->extended_data, out->nb_samples);
        av_frame_free(&out);

        if (s->start_holdoff_offset == s->start_holdoff_end) {
            s->start_holdoff_offset = 0;
            s->start_holdoff_end = 0;
            s->start_silence_offset = 0;
            s->start_silence_end = 0;
            s->mode = SILENCE_COPY;
            goto silence_copy;
        }
        break;

    case SILENCE_COPY:
silence_copy:
        nbs = in->nb_samples - nb_samples_read;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        if (s->stop_periods) {
            for (i = 0; i < nbs; i++) {
                if (s->stop_mode == T_ANY) {
                    threshold = 0;
                    for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                        threshold |= s->compute(s, in, j, nb_samples_read) > s->stop_threshold;
                    }
                } else {
                    threshold = 1;
                    for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                        threshold &= s->compute(s, in, j, nb_samples_read) > s->stop_threshold;
                    }
                }

                if (threshold && s->stop_holdoff_end && !s->stop_silence) {
                    s->mode = SILENCE_COPY_FLUSH;
                    flush(s, out, outlink, &nb_samples_written, 0);
                    s->one_period++;
                    goto silence_copy_flush;
                } else if (threshold) {
                    for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                        s->update(s, in, j, nb_samples_read);
                        s->copy(s, out, in, j, nb_samples_written, nb_samples_read);
                    }

                    s->window_offset++;
                    if (s->window_offset >= s->window_duration)
                        s->window_offset = 0;
                    nb_samples_read++;
                    nb_samples_written++;
                    s->one_period++;
                } else if (!threshold) {
                    for (j = 0; j < outlink->ch_layout.nb_channels; j++) {
                        s->update(s, in, j, nb_samples_read);
                        if (s->stop_silence)
                            s->copy(s, s->stop_silence_hold, in, j, s->stop_silence_offset, nb_samples_read);

                        s->copy(s, s->stop_holdoff, in, j, s->stop_holdoff_end, nb_samples_read);
                    }

                    if (s->stop_silence) {
                        s->stop_silence_offset++;
                        s->stop_silence_end = FFMIN(s->stop_silence_end + 1, s->stop_silence);
                        if (s->stop_silence_offset >= s->stop_silence) {
                            s->stop_silence_offset = 0;
                        }
                    }

                    s->window_offset++;
                    if (s->window_offset >= s->window_duration)
                        s->window_offset = 0;
                    nb_samples_read++;
                    s->stop_holdoff_end++;

                    if (s->stop_holdoff_end >= s->stop_duration) {
                        s->stop_found_periods += s->one_period >= 1;
                        s->one_period = 0;
                        if (s->stop_found_periods >= s->stop_periods) {
                            s->stop_holdoff_offset = 0;
                            s->stop_holdoff_end = 0;

                            if (!s->restart) {
                                s->mode = SILENCE_STOP;
                                flush(s, out, outlink, &nb_samples_written, 1);
                                goto silence_stop;
                            } else {
                                s->stop_found_periods = 0;
                                s->start_found_periods = 0;
                                s->start_holdoff_offset = 0;
                                s->start_holdoff_end = 0;
                                s->start_silence_offset = 0;
                                s->start_silence_end = 0;
                                clear_window(s);
                                s->mode = SILENCE_TRIM;
                                flush(s, out, outlink, &nb_samples_written, 1);
                                goto silence_trim;
                            }
                        }
                        s->mode = SILENCE_COPY_FLUSH;
                        flush(s, out, outlink, &nb_samples_written, 0);
                        goto silence_copy_flush;
                    }
                }
            }
            s->one_period++;
            flush(s, out, outlink, &nb_samples_written, 0);
        } else {
            av_samples_copy(out->extended_data, in->extended_data,
                            nb_samples_written,
                            nb_samples_read, nbs,
                            outlink->ch_layout.nb_channels, outlink->format);

            av_audio_fifo_write(s->fifo, (void **)out->extended_data, out->nb_samples);
            av_frame_free(&out);
        }
        break;

    case SILENCE_COPY_FLUSH:
silence_copy_flush:
        nbs  = s->stop_holdoff_end - s->stop_holdoff_offset;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_samples_copy(out->extended_data, s->stop_holdoff->extended_data, 0,
                        s->stop_holdoff_offset, nbs,
                        outlink->ch_layout.nb_channels, outlink->format);

        s->stop_holdoff_offset += nbs;

        av_audio_fifo_write(s->fifo, (void **)out->extended_data, out->nb_samples);
        av_frame_free(&out);

        if (s->stop_holdoff_offset == s->stop_holdoff_end) {
            s->stop_holdoff_offset = 0;
            s->stop_holdoff_end = 0;
            s->stop_silence_offset = 0;
            s->stop_silence_end = 0;
            s->mode = SILENCE_COPY;
            goto silence_copy;
        }
        break;
    case SILENCE_STOP:
silence_stop:
        break;
    default:
        ret = AVERROR_BUG;
    }

    av_frame_free(&in);

    if (av_audio_fifo_size(s->fifo) > 0) {
        out = ff_get_audio_buffer(outlink, av_audio_fifo_size(s->fifo));
        if (!out)
            return AVERROR(ENOMEM);

        av_audio_fifo_read(s->fifo, (void **)out->extended_data, out->nb_samples);
        out->pts = s->next_pts;
        s->next_pts += av_rescale_q(out->nb_samples,
                                    (AVRational){1, outlink->sample_rate},
                                    outlink->time_base);

        ret = ff_filter_frame(outlink, out);
    }

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SilenceRemoveContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && (s->mode == SILENCE_COPY_FLUSH ||
                               s->mode == SILENCE_COPY)) {
        int nbs = s->stop_holdoff_end - s->stop_holdoff_offset;
        if (nbs) {
            AVFrame *frame;

            frame = ff_get_audio_buffer(outlink, nbs);
            if (!frame)
                return AVERROR(ENOMEM);

            av_samples_copy(frame->extended_data, s->stop_holdoff->extended_data, 0,
                            s->stop_holdoff_offset, nbs,
                            outlink->ch_layout.nb_channels, outlink->format);

            frame->pts = s->next_pts;
            s->next_pts += av_rescale_q(frame->nb_samples,
                                        (AVRational){1, outlink->sample_rate},
                                        outlink->time_base);

            ret = ff_filter_frame(outlink, frame);
        }
        s->mode = SILENCE_STOP;
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    av_frame_free(&s->start_holdoff);
    av_frame_free(&s->start_silence_hold);
    av_frame_free(&s->stop_holdoff);
    av_frame_free(&s->stop_silence_hold);
    av_frame_free(&s->window);

    av_audio_fifo_free(s->fifo);
    s->fifo = NULL;
}

static const AVFilterPad silenceremove_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad silenceremove_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_af_silenceremove = {
    .name          = "silenceremove",
    .description   = NULL_IF_CONFIG_SMALL("Remove silence."),
    .priv_size     = sizeof(SilenceRemoveContext),
    .priv_class    = &silenceremove_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(silenceremove_inputs),
    FILTER_OUTPUTS(silenceremove_outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
};
