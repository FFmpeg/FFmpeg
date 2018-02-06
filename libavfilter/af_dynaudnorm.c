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

#define FF_BUFQUEUE_SIZE 302
#include "libavfilter/bufferqueue.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct cqueue {
    double *elements;
    int size;
    int nb_elements;
    int first;
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
    double *prev_amplification_factor;
    double *dc_correction_value;
    double *compress_threshold;
    double *fade_factors[2];
    double *weights;

    int channels;
    int delay;

    cqueue **gain_history_original;
    cqueue **gain_history_minimum;
    cqueue **gain_history_smoothed;
} DynamicAudioNormalizerContext;

#define OFFSET(x) offsetof(DynamicAudioNormalizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption dynaudnorm_options[] = {
    { "f", "set the frame length in msec",     OFFSET(frame_len_msec),    AV_OPT_TYPE_INT,    {.i64 = 500},   10,  8000, FLAGS },
    { "g", "set the filter size",              OFFSET(filter_size),       AV_OPT_TYPE_INT,    {.i64 = 31},     3,   301, FLAGS },
    { "p", "set the peak value",               OFFSET(peak_value),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.95}, 0.0,   1.0, FLAGS },
    { "m", "set the max amplification",        OFFSET(max_amplification), AV_OPT_TYPE_DOUBLE, {.dbl = 10.0}, 1.0, 100.0, FLAGS },
    { "r", "set the target RMS",               OFFSET(target_rms),        AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,   1.0, FLAGS },
    { "n", "set channel coupling",             OFFSET(channels_coupled),  AV_OPT_TYPE_BOOL,   {.i64 = 1},      0,     1, FLAGS },
    { "c", "set DC correction",                OFFSET(dc_correction),     AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "b", "set alternative boundary mode",    OFFSET(alt_boundary_mode), AV_OPT_TYPE_BOOL,   {.i64 = 0},      0,     1, FLAGS },
    { "s", "set the compress factor",          OFFSET(compress_factor),   AV_OPT_TYPE_DOUBLE, {.dbl = 0.0},  0.0,  30.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dynaudnorm);

static av_cold int init(AVFilterContext *ctx)
{
    DynamicAudioNormalizerContext *s = ctx->priv;

    if (!(s->filter_size & 1)) {
        av_log(ctx, AV_LOG_ERROR, "filter size %d is invalid. Must be an odd value.\n", s->filter_size);
        return AVERROR(EINVAL);
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

static void precalculate_fade_factors(double *fade_factors[2], int frame_len)
{
    const double step_size = 1.0 / frame_len;
    int pos;

    for (pos = 0; pos < frame_len; pos++) {
        fade_factors[0][pos] = 1.0 - (step_size * (pos + 1.0));
        fade_factors[1][pos] = 1.0 - fade_factors[0][pos];
    }
}

static cqueue *cqueue_create(int size)
{
    cqueue *q;

    q = av_malloc(sizeof(cqueue));
    if (!q)
        return NULL;

    q->size = size;
    q->nb_elements = 0;
    q->first = 0;

    q->elements = av_malloc_array(size, sizeof(double));
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
    return !q->nb_elements;
}

static int cqueue_enqueue(cqueue *q, double element)
{
    int i;

    av_assert2(q->nb_elements != q->size);

    i = (q->first + q->nb_elements) % q->size;
    q->elements[i] = element;
    q->nb_elements++;

    return 0;
}

static double cqueue_peek(cqueue *q, int index)
{
    av_assert2(index < q->nb_elements);
    return q->elements[(q->first + index) % q->size];
}

static int cqueue_dequeue(cqueue *q, double *element)
{
    av_assert2(!cqueue_empty(q));

    *element = q->elements[q->first];
    q->first = (q->first + 1) % q->size;
    q->nb_elements--;

    return 0;
}

static int cqueue_pop(cqueue *q)
{
    av_assert2(!cqueue_empty(q));

    q->first = (q->first + 1) % q->size;
    q->nb_elements--;

    return 0;
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
    av_freep(&s->fade_factors[0]);
    av_freep(&s->fade_factors[1]);

    for (c = 0; c < s->channels; c++) {
        if (s->gain_history_original)
            cqueue_free(s->gain_history_original[c]);
        if (s->gain_history_minimum)
            cqueue_free(s->gain_history_minimum[c]);
        if (s->gain_history_smoothed)
            cqueue_free(s->gain_history_smoothed[c]);
    }

    av_freep(&s->gain_history_original);
    av_freep(&s->gain_history_minimum);
    av_freep(&s->gain_history_smoothed);

    av_freep(&s->weights);

    ff_bufqueue_discard_all(&s->queue);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DynamicAudioNormalizerContext *s = ctx->priv;
    int c;

    uninit(ctx);

    s->frame_len =
    inlink->min_samples =
    inlink->max_samples =
    inlink->partial_buf_size = frame_size(inlink->sample_rate, s->frame_len_msec);
    av_log(ctx, AV_LOG_DEBUG, "frame len %d\n", s->frame_len);

    s->fade_factors[0] = av_malloc_array(s->frame_len, sizeof(*s->fade_factors[0]));
    s->fade_factors[1] = av_malloc_array(s->frame_len, sizeof(*s->fade_factors[1]));

    s->prev_amplification_factor = av_malloc_array(inlink->channels, sizeof(*s->prev_amplification_factor));
    s->dc_correction_value = av_calloc(inlink->channels, sizeof(*s->dc_correction_value));
    s->compress_threshold = av_calloc(inlink->channels, sizeof(*s->compress_threshold));
    s->gain_history_original = av_calloc(inlink->channels, sizeof(*s->gain_history_original));
    s->gain_history_minimum = av_calloc(inlink->channels, sizeof(*s->gain_history_minimum));
    s->gain_history_smoothed = av_calloc(inlink->channels, sizeof(*s->gain_history_smoothed));
    s->weights = av_malloc_array(s->filter_size, sizeof(*s->weights));
    if (!s->prev_amplification_factor || !s->dc_correction_value ||
        !s->compress_threshold || !s->fade_factors[0] || !s->fade_factors[1] ||
        !s->gain_history_original || !s->gain_history_minimum ||
        !s->gain_history_smoothed || !s->weights)
        return AVERROR(ENOMEM);

    for (c = 0; c < inlink->channels; c++) {
        s->prev_amplification_factor[c] = 1.0;

        s->gain_history_original[c] = cqueue_create(s->filter_size);
        s->gain_history_minimum[c]  = cqueue_create(s->filter_size);
        s->gain_history_smoothed[c] = cqueue_create(s->filter_size);

        if (!s->gain_history_original[c] || !s->gain_history_minimum[c] ||
            !s->gain_history_smoothed[c])
            return AVERROR(ENOMEM);
    }

    precalculate_fade_factors(s->fade_factors, s->frame_len);
    init_gaussian_filter(s);

    s->channels = inlink->channels;
    s->delay = s->filter_size;

    return 0;
}

static inline double fade(double prev, double next, int pos,
                          double *fade_factors[2])
{
    return fade_factors[0][pos] * prev + fade_factors[1][pos] * next;
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

static double get_max_local_gain(DynamicAudioNormalizerContext *s, AVFrame *frame,
                                 int channel)
{
    const double maximum_gain = s->peak_value / find_peak_magnitude(frame, channel);
    const double rms_gain = s->target_rms > DBL_EPSILON ? (s->target_rms / compute_frame_rms(frame, channel)) : DBL_MAX;
    return bound(s->max_amplification, FFMIN(maximum_gain, rms_gain));
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

static double gaussian_filter(DynamicAudioNormalizerContext *s, cqueue *q)
{
    double result = 0.0;
    int i;

    for (i = 0; i < cqueue_size(q); i++) {
        result += cqueue_peek(q, i) * s->weights[i];
    }

    return result;
}

static void update_gain_history(DynamicAudioNormalizerContext *s, int channel,
                                double current_gain_factor)
{
    if (cqueue_empty(s->gain_history_original[channel]) ||
        cqueue_empty(s->gain_history_minimum[channel])) {
        const int pre_fill_size = s->filter_size / 2;
        const double initial_value = s->alt_boundary_mode ? current_gain_factor : 1.0;

        s->prev_amplification_factor[channel] = initial_value;

        while (cqueue_size(s->gain_history_original[channel]) < pre_fill_size) {
            cqueue_enqueue(s->gain_history_original[channel], initial_value);
        }
    }

    cqueue_enqueue(s->gain_history_original[channel], current_gain_factor);

    while (cqueue_size(s->gain_history_original[channel]) >= s->filter_size) {
        double minimum;
        av_assert0(cqueue_size(s->gain_history_original[channel]) == s->filter_size);

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

        cqueue_pop(s->gain_history_original[channel]);
    }

    while (cqueue_size(s->gain_history_minimum[channel]) >= s->filter_size) {
        double smoothed;
        av_assert0(cqueue_size(s->gain_history_minimum[channel]) == s->filter_size);
        smoothed = gaussian_filter(s, s->gain_history_minimum[channel]);

        cqueue_enqueue(s->gain_history_smoothed[channel], smoothed);

        cqueue_pop(s->gain_history_minimum[channel]);
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
            dst_ptr[i] -= fade(prev_value, s->dc_correction_value[c], i, s->fade_factors);
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
                const double localThresh = fade(prev_actual_thresh, curr_actual_thresh, i, s->fade_factors);
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
                const double localThresh = fade(prev_actual_thresh, curr_actual_thresh, i, s->fade_factors);
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
        const double current_gain_factor = get_max_local_gain(s, frame, -1);
        int c;

        for (c = 0; c < s->channels; c++)
            update_gain_history(s, c, current_gain_factor);
    } else {
        int c;

        for (c = 0; c < s->channels; c++)
            update_gain_history(s, c, get_max_local_gain(s, frame, c));
    }
}

static void amplify_frame(DynamicAudioNormalizerContext *s, AVFrame *frame)
{
    int c, i;

    for (c = 0; c < s->channels; c++) {
        double *dst_ptr = (double *)frame->extended_data[c];
        double current_amplification_factor;

        cqueue_dequeue(s->gain_history_smoothed[c], &current_amplification_factor);

        for (i = 0; i < frame->nb_samples; i++) {
            const double amplification_factor = fade(s->prev_amplification_factor[c],
                                                     current_amplification_factor, i,
                                                     s->fade_factors);

            dst_ptr[i] *= amplification_factor;

            if (fabs(dst_ptr[i]) > s->peak_value)
                dst_ptr[i] = copysign(s->peak_value, dst_ptr[i]);
        }

        s->prev_amplification_factor[c] = current_amplification_factor;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    DynamicAudioNormalizerContext *s = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int ret = 0;

    if (!cqueue_empty(s->gain_history_smoothed[0])) {
        AVFrame *out = ff_bufqueue_get(&s->queue);

        amplify_frame(s, out);
        ret = ff_filter_frame(outlink, out);
    }

    analyze_frame(s, in);
    ff_bufqueue_add(ctx, &s->queue, in);

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

    s->delay--;
    return filter_frame(inlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DynamicAudioNormalizerContext *s = ctx->priv;
    int ret = 0;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && !ctx->is_disabled && s->delay) {
        if (!cqueue_empty(s->gain_history_smoothed[0])) {
            ret = flush_buffer(s, ctx->inputs[0], outlink);
        } else if (s->queue.available) {
            AVFrame *out = ff_bufqueue_get(&s->queue);

            ret = ff_filter_frame(outlink, out);
        }
    }

    return ret;
}

static const AVFilterPad avfilter_af_dynaudnorm_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_dynaudnorm_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
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
    .inputs        = avfilter_af_dynaudnorm_inputs,
    .outputs       = avfilter_af_dynaudnorm_outputs,
    .priv_class    = &dynaudnorm_class,
};
