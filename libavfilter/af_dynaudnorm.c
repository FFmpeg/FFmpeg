/*
 * Dynamic Audio Normalizer
 * Copyright (c) 2015 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.
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
 * Dynamic Audio Normalizer
 */

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/opt.h"

#define MIN_FILTER_SIZE 3
#define MAX_FILTER_SIZE 301

#define FF_BUFQUEUE_SIZE (MAX_FILTER_SIZE + 1)
#include "libavfilter/bufferqueue.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

typedef struct local_gain {
    double max_gain;
    double threshold;
} local_gain;

typedef struct cqueue {
    double *elements;
    int size;
    int max_size;
    int nb_elements;
} cqueue;

typedef struct DynamicAudioNormalizerContext {
    const AVClass *class;

    struct FFBufQueue queue;

    int frame_len;
    int frame_len_msec;
    int filter_size;
    int dc_correction;
    int channels_coupled;
    int alt_boundary_mode;

    double peak_value;
    double max_amplification;
    double target_rms;
    double compress_factor;
    double threshold;
    double *prev_amplification_factor;
    double *dc_correction_value;
    double *compress_threshold;
    double *weights;

    int channels;
    int eof;
    int64_t pts;

    cqueue **gain_history_original;
    cqueue **gain_history_minimum;
    cqueue **gain_history_smoothed;
    cqueue **threshold_history;

    cqueue *is_enabled;
} DynamicAudioNormalizerContext;

#define OFFSET(x) offsetof(DynamicAudioNormalizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption dynaudnorm_options[] = {
    { "framelen",    "set the frame length in msec",     OFFSET(frame_len_msec),    AV_OPT_TYPE_INT,    {.i64 = 500},   10,  8000, FLAGS },
    { "f",           "set the frame length in msec",     OFFSET(frame_len_msec),    AV_OPT_TYPE_INT,    {.i64 = 500},   10,  8000, FLAGS },
    { "gausssize",   "set the filter size",              OFFSET(filter_size),       AV_OPT_TYPE_INT,    {.i64 = 31},     3,   301, FLAGS },
    { "g",           "set the filter size",              OFFSET(filter_size),       AV_OPT_TYPE_INT,    {.i64 = 31},     3,   301, FLAGS },
    { "peak",        "set the peak value",               OFFSET(peak_value),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.95}, 0.0,   1.0, FLAGS },
    { "p",           "set the peak value",               OFFSET(peak_value),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.95}, 0.0,   1.0, FLAGS },
    { "maxgain",     "set the max amplification",        OFFSET(max_amplification), AV_OPT_TYPE_DOUBLE, {.dbl = 10.0}, 1.0, 100.0, FLAGS },
    { "m",           "set the max amplification",        OFFSET(max_amplification), AV_OPT_TYPE_DOUBLE, {.dbl = 10.0}, 1.0, 100.0, FLAGS },
    { "targetrms",   "set the target RMS",               OFFSET(target_rms),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,   1.0, FLAGS },
    { "r",           "set the target RMS",               OFFSET(target_rms),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,   1.0, FLAGS },
    { "coupling",    "set channel coupling",             OFFSET(channels_coupled),  AV_OPT_TYPE_BOOL,   {.i64 = 1},      0,     1, FLAGS },
    { "n",           "set channel coupling",             OFFSET(channels_coupled),  AV_OPT_TYPE_BOOL,   {.i64 = 1},      0,     1, FLAGS },
    { "correctdc",   "set DC correction",                OFFSET(dc_correction),     AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "c",           "set DC correction",                OFFSET(dc_correction),     AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "altboundary", "set alternative boundary mode",    OFFSET(alt_boundary_mode), AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "b",           "set alternative boundary mode",    OFFSET(alt_boundary_mode), AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "compress",    "set the compress factor",          OFFSET(compress_factor),   AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,  30.0, FLAGS },
    { "s",           "set the compress factor",          OFFSET(compress_factor),   AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,  30.0, FLAGS },
    { "threshold",   "set the threshold value",          OFFSET(threshold),         AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,   1.0, FLAGS },
    { "t",           "set the threshold value",          OFFSET(threshold),         AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,   1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dynaudnorm);

static av_cold int init(AVFilterContext *ctx)
{
    DynamicAudioNormalizerContext *s = ctx->priv;

    if (!(s->filter_size & 1)) {
        av_log(ctx, AV_LOG_WARNING, "filter size %d is invalid. Changing to an odd value.\n", s->filter_size);
        s->filter_size |= 1;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
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

static inline int frame_size(int sample_rate, int frame_len_msec)
{
    const int frame_size = lrint((double)sample_rate * (frame_len_msec / 1000.0));
    return frame_size + (frame_size % 2);
}

static cqueue *cqueue_create(int size, int max_size)
{
    cqueue *q;

    if (max_size < size)
        return NULL;

    q = av_malloc(sizeof(cqueue));
    if (!q)
        return NULL;

    q->max_size = max_size;
    q->size = size;
    q->nb_elements = 0;

    q->elements = av_malloc_array(max_size, sizeof(double));
    if (!q->elements) {
        av_free(q);
        return NULL;
    }

    return q;
}

static void cqueue_free(cqueue *q)
{
    if (q)
        av_free(q->elements);
    av_free(q);
}

static int cqueue_size(cqueue *q)
{
    return q->nb_elements;
}

static int cqueue_empty(cqueue *q)
{
    return q->nb_elements <= 0;
}

static int cqueue_enqueue(cqueue *q, double element)
{
    av_assert2(q->nb_elements < q->max_size);

    q->elements[q->nb_elements] = element;
    q->nb_elements++;

    return 0;
}

static double cqueue_peek(cqueue *q, int index)
{
    av_assert2(index < q->nb_elements);
    return q->elements[index];
}

static int cqueue_dequeue(cqueue *q, double *element)
{
    av_assert2(!cqueue_empty(q));

    *element = q->elements[0];
    memmove(&q->elements[0], &q->elements[1], (q->nb_elements - 1) * sizeof(double));
    q->nb_elements--;

    return 0;
}

static int cqueue_pop(cqueue *q)
{
    av_assert2(!cqueue_empty(q));

    memmove(&q->elements[0], &q->elements[1], (q->nb_elements - 1) * sizeof(double));
    q->nb_elements--;

    return 0;
}

static void cqueue_resize(cqueue *q, int new_size)
{
    av_assert2(q->max_size >= new_size);
    av_assert2(MIN_FILTER_SIZE <= new_size);

    if (new_size > q->nb_elements) {
        const int side = (new_size - q->nb_elements) / 2;

        memmove(q->elements + side, q->elements, sizeof(double) * q->nb_elements);
        for (int i = 0; i < side; i++)
            q->elements[i] = q->elements[side];
        q->nb_elements = new_size - 1 - side;
    } else {
        int count = (q->size - new_size + 1) / 2;

        while (count-- > 0)
            cqueue_pop(q);
    }

    q->size = new_size;
}

static void init_gaussian_filter(DynamicAudioNormalizerContext *s)
{
    double total_weight = 0.0;
    const double sigma = (((s->filter_size / 2.0) - 1.0) / 3.0) + (1.0 / 3.0);
    double adjust;
    int i;

    // Pre-compute constants
    const int offset = s->filter_size / 2;
    const double c1 = 1.0 / (sigma * sqrt(2.0 * M_PI));
    const double c2 = 2.0 * sigma * sigma;

    // Compute weights
    for (i = 0; i < s->filter_size; i++) {
        const int x = i - offset;

        s->weights[i] = c1 * exp(-x * x / c2);
        total_weight += s->weights[i];
    }

    // Adjust weights
    adjust = 1.0 / total_weight;
    for (i = 0; i < s->filter_size; i++) {
        s->weights[i] *= adjust;
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DynamicAudioNormalizerContext *s = ctx->priv;
    int c;

    av_freep(&s->prev_amplification_factor);
    av_freep(&s->dc_correction_value);
    av_freep(&s->compress_threshold);

    for (c = 0; c < s->channels; c++) {
        if (s->gain_history_original)
            cqueue_free(s->gain_history_original[c]);
        if (s->gain_history_minimum)
            cqueue_free(s->gain_history_minimum[c]);
        if (s->gain_history_smoothed)
            cqueue_free(s->gain_history_smoothed[c]);
        if (s->threshold_history)
            cqueue_free(s->threshold_history[c]);
    }

    av_freep(&s->gain_history_original);
    av_freep(&s->gain_history_minimum);
    av_freep(&s->gain_history_smoothed);
    av_freep(&s->threshold_history);

    cqueue_free(s->is_enabled);
    s->is_enabled = NULL;

    av_freep(&s->weights);

    ff_bufqueue_discard_all(&s->queue);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DynamicAudioNormalizerContext *s = ctx->priv;
    int c;

    uninit(ctx);

    s->channels = inlink->channels;
    s->frame_len = frame_size(inlink->sample_rate, s->frame_len_msec);
    av_log(ctx, AV_LOG_DEBUG, "frame len %d\n", s->frame_len);

    s->prev_amplification_factor = av_malloc_array(inlink->channels, sizeof(*s->prev_amplification_factor));
    s->dc_correction_value = av_calloc(inlink->channels, sizeof(*s->dc_correction_value));
    s->compress_threshold = av_calloc(inlink->channels, sizeof(*s->compress_threshold));
    s->gain_history_original = av_calloc(inlink->channels, sizeof(*s->gain_history_original));
    s->gain_history_minimum = av_calloc(inlink->channels, sizeof(*s->gain_history_minimum));
    s->gain_history_smoothed = av_calloc(inlink->channels, sizeof(*s->gain_history_smoothed));
    s->threshold_history = av_calloc(inlink->channels, sizeof(*s->threshold_history));
    s->weights = av_malloc_array(MAX_FILTER_SIZE, sizeof(*s->weights));
    s->is_enabled = cqueue_create(s->filter_size, MAX_FILTER_SIZE);
    if (!s->prev_amplification_factor || !s->dc_correction_value ||
        !s->compress_threshold ||
        !s->gain_history_original || !s->gain_history_minimum ||
        !s->gain_history_smoothed || !s->threshold_history ||
        !s->is_enabled || !s->weights)
        return AVERROR(ENOMEM);

    for (c = 0; c < inlink->channels; c++) {
        s->prev_amplification_factor[c] = 1.0;

        s->gain_history_original[c] = cqueue_create(s->filter_size, MAX_FILTER_SIZE);
        s->gain_history_minimum[c]  = cqueue_create(s->filter_size, MAX_FILTER_SIZE);
        s->gain_history_smoothed[c] = cqueue_create(s->filter_size, MAX_FILTER_SIZE);
        s->threshold_history[c]     = cqueue_create(s->filter_size, MAX_FILTER_SIZE);

        if (!s->gain_history_original[c] || !s->gain_history_minimum[c] ||
            !s->gain_history_smoothed[c] || !s->threshold_history[c])
            return AVERROR(ENOMEM);
    }

    init_gaussian_filter(s);

    return 0;
}

static inline double fade(double prev, double next, int pos, int length)
{
    const double step_size = 1.0 / length;
    const double f0 = 1.0 - (step_size * (pos + 1.0));
    const double f1 = 1.0 - f0;
    return f0 * prev + f1 * next;
}

static inline double pow_2(const double value)
{
    return value * value;
}

static inline double bound(const double threshold, const double val)
{
    const double CONST = 0.8862269254527580136490837416705725913987747280611935; //sqrt(PI) / 2.0
    return erf(CONST * (val / threshold)) * threshold;
}

static double find_peak_magnitude(AVFrame *frame, int channel)
{
    double max = DBL_EPSILON;
    int c, i;

    if (channel == -1) {
        for (c = 0; c < frame->channels; c++) {
            double *data_ptr = (double *)frame->extended_data[c];

            for (i = 0; i < frame->nb_samples; i++)
                max = FFMAX(max, fabs(data_ptr[i]));
        }
    } else {
        double *data_ptr = (double *)frame->extended_data[channel];

        for (i = 0; i < frame->nb_samples; i++)
            max = FFMAX(max, fabs(data_ptr[i]));
    }

    return max;
}

static double compute_frame_rms(AVFrame *frame, int channel)
{
    double rms_value = 0.0;
    int c, i;

    if (channel == -1) {
        for (c = 0; c < frame->channels; c++) {
            const double *data_ptr = (double *)frame->extended_data[c];

            for (i = 0; i < frame->nb_samples; i++) {
                rms_value += pow_2(data_ptr[i]);
            }
        }

        rms_value /= frame->nb_samples * frame->channels;
    } else {
        const double *data_ptr = (double *)frame->extended_data[channel];
        for (i = 0; i < frame->nb_samples; i++) {
            rms_value += pow_2(data_ptr[i]);
        }

        rms_value /= frame->nb_samples;
    }

    return FFMAX(sqrt(rms_value), DBL_EPSILON);
}

static local_gain get_max_local_gain(DynamicAudioNormalizerContext *s, AVFrame *frame,
                                     int channel)
{
    const double peak_magnitude = find_peak_magnitude(frame, channel);
    const double maximum_gain = s->peak_value / peak_magnitude;
    const double rms_gain = s->target_rms > DBL_EPSILON ? (s->target_rms / compute_frame_rms(frame, channel)) : DBL_MAX;
    local_gain gain;

    gain.threshold = peak_magnitude > s->threshold;
    gain.max_gain  = bound(s->max_amplification, FFMIN(maximum_gain, rms_gain));

    return gain;
}

static double minimum_filter(cqueue *q)
{
    double min = DBL_MAX;
    int i;

    for (i = 0; i < cqueue_size(q); i++) {
        min = FFMIN(min, cqueue_peek(q, i));
    }

    return min;
}

static double gaussian_filter(DynamicAudioNormalizerContext *s, cqueue *q, cqueue *tq)
{
    double result = 0.0, tsum = 0.0;
    int i;

    for (i = 0; i < cqueue_size(q); i++) {
        tsum += cqueue_peek(tq, i) * s->weights[i];
        result += cqueue_peek(q, i) * s->weights[i] * cqueue_peek(tq, i);
    }

    if (tsum == 0.0)
        result = 1.0;

    return result;
}

static void update_gain_history(DynamicAudioNormalizerContext *s, int channel,
                                local_gain gain)
{
    if (cqueue_empty(s->gain_history_original[channel])) {
        const int pre_fill_size = s->filter_size / 2;
        const double initial_value = s->alt_boundary_mode ? gain.max_gain : s->peak_value;

        s->prev_amplification_factor[channel] = initial_value;

        while (cqueue_size(s->gain_history_original[channel]) < pre_fill_size) {
            cqueue_enqueue(s->gain_history_original[channel], initial_value);
            cqueue_enqueue(s->threshold_history[channel], gain.threshold);
        }
    }

    cqueue_enqueue(s->gain_history_original[channel], gain.max_gain);

    while (cqueue_size(s->gain_history_original[channel]) >= s->filter_size) {
        double minimum;

        if (cqueue_empty(s->gain_history_minimum[channel])) {
            const int pre_fill_size = s->filter_size / 2;
            double initial_value = s->alt_boundary_mode ? cqueue_peek(s->gain_history_original[channel], 0) : 1.0;
            int input = pre_fill_size;

            while (cqueue_size(s->gain_history_minimum[channel]) < pre_fill_size) {
                input++;
                initial_value = FFMIN(initial_value, cqueue_peek(s->gain_history_original[channel], input));
                cqueue_enqueue(s->gain_history_minimum[channel], initial_value);
            }
        }

        minimum = minimum_filter(s->gain_history_original[channel]);

        cqueue_enqueue(s->gain_history_minimum[channel], minimum);

        cqueue_enqueue(s->threshold_history[channel], gain.threshold);

        cqueue_pop(s->gain_history_original[channel]);
    }

    while (cqueue_size(s->gain_history_minimum[channel]) >= s->filter_size) {
        double smoothed, limit;

        smoothed = gaussian_filter(s, s->gain_history_minimum[channel], s->threshold_history[channel]);
        limit    = cqueue_peek(s->gain_history_original[channel], 0);
        smoothed = FFMIN(smoothed, limit);

        cqueue_enqueue(s->gain_history_smoothed[channel], smoothed);

        cqueue_pop(s->gain_history_minimum[channel]);
        cqueue_pop(s->threshold_history[channel]);
    }
}

static inline double update_value(double new, double old, double aggressiveness)
{
    av_assert0((aggressiveness >= 0.0) && (aggressiveness <= 1.0));
    return aggressiveness * new + (1.0 - aggressiveness) * old;
}

static void perform_dc_correction(DynamicAudioNormalizerContext *s, AVFrame *frame)
{
    const double diff = 1.0 / frame->nb_samples;
    int is_first_frame = cqueue_empty(s->gain_history_original[0]);
    int c, i;

    for (c = 0; c < s->channels; c++) {
        double *dst_ptr = (double *)frame->extended_data[c];
        double current_average_value = 0.0;
        double prev_value;

        for (i = 0; i < frame->nb_samples; i++)
            current_average_value += dst_ptr[i] * diff;

        prev_value = is_first_frame ? current_average_value : s->dc_correction_value[c];
        s->dc_correction_value[c] = is_first_frame ? current_average_value : update_value(current_average_value, s->dc_correction_value[c], 0.1);

        for (i = 0; i < frame->nb_samples; i++) {
            dst_ptr[i] -= fade(prev_value, s->dc_correction_value[c], i, frame->nb_samples);
        }
    }
}

static double setup_compress_thresh(double threshold)
{
    if ((threshold > DBL_EPSILON) && (threshold < (1.0 - DBL_EPSILON))) {
        double current_threshold = threshold;
        double step_size = 1.0;

        while (step_size > DBL_EPSILON) {
            while ((llrint((current_threshold + step_size) * (UINT64_C(1) << 63)) >
                    llrint(current_threshold * (UINT64_C(1) << 63))) &&
                   (bound(current_threshold + step_size, 1.0) <= threshold)) {
                current_threshold += step_size;
            }

            step_size /= 2.0;
        }

        return current_threshold;
    } else {
        return threshold;
    }
}

static double compute_frame_std_dev(DynamicAudioNormalizerContext *s,
                                    AVFrame *frame, int channel)
{
    double variance = 0.0;
    int i, c;

    if (channel == -1) {
        for (c = 0; c < s->channels; c++) {
            const double *data_ptr = (double *)frame->extended_data[c];

            for (i = 0; i < frame->nb_samples; i++) {
                variance += pow_2(data_ptr[i]);  // Assume that MEAN is *zero*
            }
        }
        variance /= (s->channels * frame->nb_samples) - 1;
    } else {
        const double *data_ptr = (double *)frame->extended_data[channel];

        for (i = 0; i < frame->nb_samples; i++) {
            variance += pow_2(data_ptr[i]);      // Assume that MEAN is *zero*
        }
        variance /= frame->nb_samples - 1;
    }

    return FFMAX(sqrt(variance), DBL_EPSILON);
}

static void perform_compression(DynamicAudioNormalizerContext *s, AVFrame *frame)
{
    int is_first_frame = cqueue_empty(s->gain_history_original[0]);
    int c, i;

    if (s->channels_coupled) {
        const double standard_deviation = compute_frame_std_dev(s, frame, -1);
        const double current_threshold  = FFMIN(1.0, s->compress_factor * standard_deviation);

        const double prev_value = is_first_frame ? current_threshold : s->compress_threshold[0];
        double prev_actual_thresh, curr_actual_thresh;
        s->compress_threshold[0] = is_first_frame ? current_threshold : update_value(current_threshold, s->compress_threshold[0], (1.0/3.0));

        prev_actual_thresh = setup_compress_thresh(prev_value);
        curr_actual_thresh = setup_compress_thresh(s->compress_threshold[0]);

        for (c = 0; c < s->channels; c++) {
            double *const dst_ptr = (double *)frame->extended_data[c];
            for (i = 0; i < frame->nb_samples; i++) {
                const double localThresh = fade(prev_actual_thresh, curr_actual_thresh, i, frame->nb_samples);
                dst_ptr[i] = copysign(bound(localThresh, fabs(dst_ptr[i])), dst_ptr[i]);
            }
        }
    } else {
        for (c = 0; c < s->channels; c++) {
            const double standard_deviation = compute_frame_std_dev(s, frame, c);
            const double current_threshold  = setup_compress_thresh(FFMIN(1.0, s->compress_factor * standard_deviation));

            const double prev_value = is_first_frame ? current_threshold : s->compress_threshold[c];
            double prev_actual_thresh, curr_actual_thresh;
            double *dst_ptr;
            s->compress_threshold[c] = is_first_frame ? current_threshold : update_value(current_threshold, s->compress_threshold[c], 1.0/3.0);

            prev_actual_thresh = setup_compress_thresh(prev_value);
            curr_actual_thresh = setup_compress_thresh(s->compress_threshold[c]);

            dst_ptr = (double *)frame->extended_data[c];
            for (i = 0; i < frame->nb_samples; i++) {
                const double localThresh = fade(prev_actual_thresh, curr_actual_thresh, i, frame->nb_samples);
                dst_ptr[i] = copysign(bound(localThresh, fabs(dst_ptr[i])), dst_ptr[i]);
            }
        }
    }
}

static void analyze_frame(DynamicAudioNormalizerContext *s, AVFrame *frame)
{
    if (s->dc_correction) {
        perform_dc_correction(s, frame);
    }

    if (s->compress_factor > DBL_EPSILON) {
        perform_compression(s, frame);
    }

    if (s->channels_coupled) {
        const local_gain gain = get_max_local_gain(s, frame, -1);
        int c;

        for (c = 0; c < s->channels; c++)
            update_gain_history(s, c, gain);
    } else {
        int c;

        for (c = 0; c < s->channels; c++)
            update_gain_history(s, c, get_max_local_gain(s, frame, c));
    }
}

static void amplify_frame(DynamicAudioNormalizerContext *s, AVFrame *frame, int enabled)
{
    int c, i;

    for (c = 0; c < s->channels; c++) {
        double *dst_ptr = (double *)frame->extended_data[c];
        double current_amplification_factor;

        cqueue_dequeue(s->gain_history_smoothed[c], &current_amplification_factor);

        for (i = 0; i < frame->nb_samples && enabled; i++) {
            const double amplification_factor = fade(s->prev_amplification_factor[c],
                                                     current_amplification_factor, i,
                                                     frame->nb_samples);

            dst_ptr[i] *= amplification_factor;
        }

        s->prev_amplification_factor[c] = current_amplification_factor;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    DynamicAudioNormalizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 1;

    while (((s->queue.available >= s->filter_size) ||
            (s->eof && s->queue.available)) &&
           !cqueue_empty(s->gain_history_smoothed[0])) {
        AVFrame *out = ff_bufqueue_get(&s->queue);
        double is_enabled;

        cqueue_dequeue(s->is_enabled, &is_enabled);

        amplify_frame(s, out, is_enabled > 0.);
        ret = ff_filter_frame(outlink, out);
    }

    av_frame_make_writable(in);
    analyze_frame(s, in);
    if (!s->eof) {
        ff_bufqueue_add(ctx, &s->queue, in);
        cqueue_enqueue(s->is_enabled, !ctx->is_disabled);
    } else {
        av_frame_free(&in);
    }

    return ret;
}

static int flush_buffer(DynamicAudioNormalizerContext *s, AVFilterLink *inlink,
                        AVFilterLink *outlink)
{
    AVFrame *out = ff_get_audio_buffer(outlink, s->frame_len);
    int c, i;

    if (!out)
        return AVERROR(ENOMEM);

    for (c = 0; c < s->channels; c++) {
        double *dst_ptr = (double *)out->extended_data[c];

        for (i = 0; i < out->nb_samples; i++) {
            dst_ptr[i] = s->alt_boundary_mode ? DBL_EPSILON : ((s->target_rms > DBL_EPSILON) ? FFMIN(s->peak_value, s->target_rms) : s->peak_value);
            if (s->dc_correction) {
                dst_ptr[i] *= ((i % 2) == 1) ? -1 : 1;
                dst_ptr[i] += s->dc_correction_value[c];
            }
        }
    }

    return filter_frame(inlink, out);
}

static int flush(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DynamicAudioNormalizerContext *s = ctx->priv;
    int ret = 0;

    if (!cqueue_empty(s->gain_history_smoothed[0])) {
        ret = flush_buffer(s, ctx->inputs[0], outlink);
    } else if (s->queue.available) {
        AVFrame *out = ff_bufqueue_get(&s->queue);

        s->pts = out->pts;
        ret = ff_filter_frame(outlink, out);
    }

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    DynamicAudioNormalizerContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof) {
        ret = ff_inlink_consume_samples(inlink, s->frame_len, s->frame_len, &in);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            ret = filter_frame(inlink, in);
            if (ret <= 0)
                return ret;
        }

        if (ff_inlink_queued_samples(inlink) >= s->frame_len) {
            ff_filter_set_ready(ctx, 10);
            return 0;
        }
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF)
            s->eof = 1;
    }

    if (s->eof && s->queue.available)
        return flush(outlink);

    if (s->eof && !s->queue.available) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!s->eof)
        FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    DynamicAudioNormalizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int prev_filter_size = s->filter_size;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    s->filter_size |= 1;
    if (prev_filter_size != s->filter_size) {
        init_gaussian_filter(s);

        for (int c = 0; c < s->channels; c++) {
            cqueue_resize(s->gain_history_original[c], s->filter_size);
            cqueue_resize(s->gain_history_minimum[c], s->filter_size);
            cqueue_resize(s->threshold_history[c], s->filter_size);
        }
    }

    s->frame_len = frame_size(inlink->sample_rate, s->frame_len_msec);

    return 0;
}

static const AVFilterPad avfilter_af_dynaudnorm_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_dynaudnorm_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_dynaudnorm = {
    .name          = "dynaudnorm",
    .description   = NULL_IF_CONFIG_SMALL("Dynamic Audio Normalizer."),
    .query_formats = query_formats,
    .priv_size     = sizeof(DynamicAudioNormalizerContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .inputs        = avfilter_af_dynaudnorm_inputs,
    .outputs       = avfilter_af_dynaudnorm_outputs,
    .priv_class    = &dynaudnorm_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = process_command,
};
