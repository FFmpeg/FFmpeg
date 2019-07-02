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

    double *start_holdoff;
    double *start_silence_hold;
    size_t start_holdoff_offset;
    size_t start_holdoff_end;
    size_t start_silence_offset;
    size_t start_silence_end;
    int    start_found_periods;

    double *stop_holdoff;
    double *stop_silence_hold;
    size_t stop_holdoff_offset;
    size_t stop_holdoff_end;
    size_t stop_silence_offset;
    size_t stop_silence_end;
    int    stop_found_periods;

    double window_ratio;
    double *window;
    double *window_current;
    double *window_end;
    int window_size;
    double sum;

    int restart;
    int64_t next_pts;

    int detection;
    void (*update)(struct SilenceRemoveContext *s, double sample);
    double(*compute)(struct SilenceRemoveContext *s, double sample);
} SilenceRemoveContext;

#define OFFSET(x) offsetof(SilenceRemoveContext, x)
#define AF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM

static const AVOption silenceremove_options[] = {
    { "start_periods",   NULL,                                                 OFFSET(start_periods),       AV_OPT_TYPE_INT,      {.i64=0},     0,      9000, AF },
    { "start_duration",  "set start duration of non-silence part",             OFFSET(start_duration_opt),  AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_threshold", "set threshold for start silence detection",          OFFSET(start_threshold),     AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "start_silence",   "set start duration of silence part to keep",         OFFSET(start_silence_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "start_mode",      "set which channel will trigger trimming from start", OFFSET(start_mode),          AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    {   "any",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ANY}, 0,         0, AF, "mode" },
    {   "all",           0,                                                    0,                           AV_OPT_TYPE_CONST,    {.i64=T_ALL}, 0,         0, AF, "mode" },
    { "stop_periods",    NULL,                                                 OFFSET(stop_periods),        AV_OPT_TYPE_INT,      {.i64=0}, -9000,      9000, AF },
    { "stop_duration",   "set stop duration of non-silence part",              OFFSET(stop_duration_opt),   AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_threshold",  "set threshold for stop silence detection",           OFFSET(stop_threshold),      AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0,   DBL_MAX, AF },
    { "stop_silence",    "set stop duration of silence part to keep",          OFFSET(stop_silence_opt),    AV_OPT_TYPE_DURATION, {.i64=0},     0, INT32_MAX, AF },
    { "stop_mode",       "set which channel will trigger trimming from end",   OFFSET(stop_mode),           AV_OPT_TYPE_INT,      {.i64=T_ANY}, T_ANY, T_ALL, AF, "mode" },
    { "detection",       "set how silence is detected",                        OFFSET(detection),           AV_OPT_TYPE_INT,      {.i64=D_RMS}, D_PEAK,D_RMS, AF, "detection" },
    {   "peak",          "use absolute values of samples",                     0,                           AV_OPT_TYPE_CONST,    {.i64=D_PEAK},0,         0, AF, "detection" },
    {   "rms",           "use squared values of samples",                      0,                           AV_OPT_TYPE_CONST,    {.i64=D_RMS}, 0,         0, AF, "detection" },
    { "window",          "set duration of window in seconds",                  OFFSET(window_ratio),        AV_OPT_TYPE_DOUBLE,   {.dbl=0.02},  0,        10, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silenceremove);

static double compute_peak(SilenceRemoveContext *s, double sample)
{
    double new_sum;

    new_sum  = s->sum;
    new_sum -= *s->window_current;
    new_sum += fabs(sample);

    return new_sum / s->window_size;
}

static void update_peak(SilenceRemoveContext *s, double sample)
{
    s->sum -= *s->window_current;
    *s->window_current = fabs(sample);
    s->sum += *s->window_current;

    s->window_current++;
    if (s->window_current >= s->window_end)
        s->window_current = s->window;
}

static double compute_rms(SilenceRemoveContext *s, double sample)
{
    double new_sum;

    new_sum  = s->sum;
    new_sum -= *s->window_current;
    new_sum += sample * sample;

    return sqrt(new_sum / s->window_size);
}

static void update_rms(SilenceRemoveContext *s, double sample)
{
    s->sum -= *s->window_current;
    *s->window_current = sample * sample;
    s->sum += *s->window_current;

    s->window_current++;
    if (s->window_current >= s->window_end)
        s->window_current = s->window;
}

static av_cold int init(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    if (s->stop_periods < 0) {
        s->stop_periods = -s->stop_periods;
        s->restart = 1;
    }

    switch (s->detection) {
    case D_PEAK:
        s->update = update_peak;
        s->compute = compute_peak;
        break;
    case D_RMS:
        s->update = update_rms;
        s->compute = compute_rms;
        break;
    }

    return 0;
}

static void clear_window(SilenceRemoveContext *s)
{
    memset(s->window, 0, s->window_size * sizeof(*s->window));

    s->window_current = s->window;
    s->window_end = s->window + s->window_size;
    s->sum = 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SilenceRemoveContext *s = ctx->priv;

    s->next_pts = AV_NOPTS_VALUE;
    s->window_size = FFMAX((inlink->sample_rate * s->window_ratio), 1) * inlink->channels;
    s->window = av_malloc_array(s->window_size, sizeof(*s->window));
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

    s->start_holdoff = av_malloc_array(FFMAX(s->start_duration, 1),
                                       sizeof(*s->start_holdoff) *
                                       inlink->channels);
    if (!s->start_holdoff)
        return AVERROR(ENOMEM);

    s->start_silence_hold = av_malloc_array(FFMAX(s->start_silence, 1),
                                            sizeof(*s->start_silence_hold) *
                                            inlink->channels);
    if (!s->start_silence_hold)
        return AVERROR(ENOMEM);

    s->start_holdoff_offset = 0;
    s->start_holdoff_end    = 0;
    s->start_found_periods  = 0;

    s->stop_holdoff = av_malloc_array(FFMAX(s->stop_duration, 1),
                                      sizeof(*s->stop_holdoff) *
                                      inlink->channels);
    if (!s->stop_holdoff)
        return AVERROR(ENOMEM);

    s->stop_silence_hold = av_malloc_array(FFMAX(s->stop_silence, 1),
                                           sizeof(*s->stop_silence_hold) *
                                           inlink->channels);
    if (!s->stop_silence_hold)
        return AVERROR(ENOMEM);

    s->stop_holdoff_offset = 0;
    s->stop_holdoff_end    = 0;
    s->stop_found_periods  = 0;

    if (s->start_periods)
        s->mode = SILENCE_TRIM;
    else
        s->mode = SILENCE_COPY;

    return 0;
}

static void flush(SilenceRemoveContext *s,
                  AVFrame *out, AVFilterLink *outlink,
                  int *nb_samples_written, int *ret, int flush_silence)
{
    AVFrame *silence;

    if (*nb_samples_written) {
        out->nb_samples = *nb_samples_written / outlink->channels;

        out->pts = s->next_pts;
        s->next_pts += av_rescale_q(out->nb_samples,
                                    (AVRational){1, outlink->sample_rate},
                                    outlink->time_base);

        *ret = ff_filter_frame(outlink, out);
        if (*ret < 0)
            return;
        *nb_samples_written = 0;
    } else {
        av_frame_free(&out);
    }

    if (s->stop_silence_end <= 0 || !flush_silence)
        return;

    silence = ff_get_audio_buffer(outlink, s->stop_silence_end / outlink->channels);
    if (!silence) {
        *ret = AVERROR(ENOMEM);
        return;
    }

    if (s->stop_silence_offset < s->stop_silence_end) {
        memcpy(silence->data[0],
               &s->stop_silence_hold[s->stop_silence_offset],
               (s->stop_silence_end - s->stop_silence_offset) * sizeof(double));
    }

    if (s->stop_silence_offset > 0) {
        memcpy(silence->data[0] + (s->stop_silence_end - s->stop_silence_offset) * sizeof(double),
               &s->stop_silence_hold[0],
               s->stop_silence_offset * sizeof(double));
    }

    s->stop_silence_offset = 0;
    s->stop_silence_end = 0;

    silence->pts = s->next_pts;
    s->next_pts += av_rescale_q(silence->nb_samples,
                                (AVRational){1, outlink->sample_rate},
                                outlink->time_base);

    *ret = ff_filter_frame(outlink, silence);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    SilenceRemoveContext *s = ctx->priv;
    int i, j, threshold, ret = 0;
    int nbs, nb_samples_read, nb_samples_written;
    double *obuf, *ibuf = (double *)in->data[0];
    AVFrame *out;

    nb_samples_read = nb_samples_written = 0;

    if (s->next_pts == AV_NOPTS_VALUE)
        s->next_pts = in->pts;

    switch (s->mode) {
    case SILENCE_TRIM:
silence_trim:
        nbs = in->nb_samples - nb_samples_read / outlink->channels;
        if (!nbs)
            break;

        for (i = 0; i < nbs; i++) {
            if (s->start_mode == T_ANY) {
                threshold = 0;
                for (j = 0; j < outlink->channels; j++) {
                    threshold |= s->compute(s, ibuf[j]) > s->start_threshold;
                }
            } else {
                threshold = 1;
                for (j = 0; j < outlink->channels; j++) {
                    threshold &= s->compute(s, ibuf[j]) > s->start_threshold;
                }
            }

            if (threshold) {
                for (j = 0; j < outlink->channels; j++) {
                    s->update(s, *ibuf);
                    s->start_holdoff[s->start_holdoff_end++] = *ibuf++;
                }
                nb_samples_read += outlink->channels;

                if (s->start_holdoff_end >= s->start_duration * outlink->channels) {
                    if (++s->start_found_periods >= s->start_periods) {
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

                for (j = 0; j < outlink->channels; j++) {
                    s->update(s, ibuf[j]);
                    if (s->start_silence) {
                        s->start_silence_hold[s->start_silence_offset++] = ibuf[j];
                        s->start_silence_end = FFMIN(s->start_silence_end + 1, outlink->channels * s->start_silence);
                        if (s->start_silence_offset >= outlink->channels * s->start_silence) {
                            s->start_silence_offset = 0;
                        }
                    }
                }

                ibuf += outlink->channels;
                nb_samples_read += outlink->channels;
            }
        }
        break;

    case SILENCE_TRIM_FLUSH:
silence_trim_flush:
        nbs  = s->start_holdoff_end - s->start_holdoff_offset;
        nbs -= nbs % outlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs / outlink->channels + s->start_silence_end / outlink->channels);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        if (s->start_silence_end > 0) {
            if (s->start_silence_offset < s->start_silence_end) {
                memcpy(out->data[0],
                       &s->start_silence_hold[s->start_silence_offset],
                       (s->start_silence_end - s->start_silence_offset) * sizeof(double));
            }

            if (s->start_silence_offset > 0) {
                memcpy(out->data[0] + (s->start_silence_end - s->start_silence_offset) * sizeof(double),
                       &s->start_silence_hold[0],
                       s->start_silence_offset * sizeof(double));
            }
        }

        memcpy(out->data[0] + s->start_silence_end * sizeof(double),
               &s->start_holdoff[s->start_holdoff_offset],
               nbs * sizeof(double));

        out->pts = s->next_pts;
        s->next_pts += av_rescale_q(out->nb_samples,
                                    (AVRational){1, outlink->sample_rate},
                                    outlink->time_base);

        s->start_holdoff_offset += nbs;

        ret = ff_filter_frame(outlink, out);

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
        nbs = in->nb_samples - nb_samples_read / outlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        obuf = (double *)out->data[0];

        if (s->stop_periods) {
            for (i = 0; i < nbs; i++) {
                if (s->stop_mode == T_ANY) {
                    threshold = 0;
                    for (j = 0; j < outlink->channels; j++) {
                        threshold |= s->compute(s, ibuf[j]) > s->stop_threshold;
                    }
                } else {
                    threshold = 1;
                    for (j = 0; j < outlink->channels; j++) {
                        threshold &= s->compute(s, ibuf[j]) > s->stop_threshold;
                    }
                }

                if (threshold && s->stop_holdoff_end && !s->stop_silence) {
                    s->mode = SILENCE_COPY_FLUSH;
                    flush(s, out, outlink, &nb_samples_written, &ret, 0);
                    goto silence_copy_flush;
                } else if (threshold) {
                    for (j = 0; j < outlink->channels; j++) {
                        s->update(s, *ibuf);
                        *obuf++ = *ibuf++;
                    }
                    nb_samples_read    += outlink->channels;
                    nb_samples_written += outlink->channels;
                } else if (!threshold) {
                    for (j = 0; j < outlink->channels; j++) {
                        s->update(s, *ibuf);
                        if (s->stop_silence) {
                            s->stop_silence_hold[s->stop_silence_offset++] = *ibuf;
                            s->stop_silence_end = FFMIN(s->stop_silence_end + 1, outlink->channels * s->stop_silence);
                            if (s->stop_silence_offset >= outlink->channels * s->stop_silence) {
                                s->stop_silence_offset = 0;
                            }
                        }

                        s->stop_holdoff[s->stop_holdoff_end++] = *ibuf++;
                    }
                    nb_samples_read += outlink->channels;

                    if (s->stop_holdoff_end >= s->stop_duration * outlink->channels) {
                        if (++s->stop_found_periods >= s->stop_periods) {
                            s->stop_holdoff_offset = 0;
                            s->stop_holdoff_end = 0;

                            if (!s->restart) {
                                s->mode = SILENCE_STOP;
                                flush(s, out, outlink, &nb_samples_written, &ret, 1);
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
                                flush(s, out, outlink, &nb_samples_written, &ret, 1);
                                goto silence_trim;
                            }
                        }
                        s->mode = SILENCE_COPY_FLUSH;
                        flush(s, out, outlink, &nb_samples_written, &ret, 0);
                        goto silence_copy_flush;
                    }
                }
            }
            flush(s, out, outlink, &nb_samples_written, &ret, 0);
        } else {
            memcpy(obuf, ibuf, sizeof(double) * nbs * outlink->channels);

            out->pts = s->next_pts;
            s->next_pts += av_rescale_q(out->nb_samples,
                                        (AVRational){1, outlink->sample_rate},
                                        outlink->time_base);

            ret = ff_filter_frame(outlink, out);
        }
        break;

    case SILENCE_COPY_FLUSH:
silence_copy_flush:
        nbs  = s->stop_holdoff_end - s->stop_holdoff_offset;
        nbs -= nbs % outlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(outlink, nbs / outlink->channels);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        memcpy(out->data[0], &s->stop_holdoff[s->stop_holdoff_offset],
               nbs * sizeof(double));
        s->stop_holdoff_offset += nbs;

        out->pts = s->next_pts;
        s->next_pts += av_rescale_q(out->nb_samples,
                                    (AVRational){1, outlink->sample_rate},
                                    outlink->time_base);

        ret = ff_filter_frame(outlink, out);

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
    }

    av_frame_free(&in);

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

            frame = ff_get_audio_buffer(outlink, nbs / outlink->channels);
            if (!frame)
                return AVERROR(ENOMEM);

            memcpy(frame->data[0], &s->stop_holdoff[s->stop_holdoff_offset],
                   nbs * sizeof(double));

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

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_NONE
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
    SilenceRemoveContext *s = ctx->priv;

    av_freep(&s->start_holdoff);
    av_freep(&s->start_silence_hold);
    av_freep(&s->stop_holdoff);
    av_freep(&s->stop_silence_hold);
    av_freep(&s->window);
}

static const AVFilterPad silenceremove_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad silenceremove_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_af_silenceremove = {
    .name          = "silenceremove",
    .description   = NULL_IF_CONFIG_SMALL("Remove silence."),
    .priv_size     = sizeof(SilenceRemoveContext),
    .priv_class    = &silenceremove_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = silenceremove_inputs,
    .outputs       = silenceremove_outputs,
};
