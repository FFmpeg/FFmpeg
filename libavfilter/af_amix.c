/*
 * Audio Mix Filter
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Audio Mix Filter
 *
 * Mixes audio from multiple sources into a single output. The channel layout,
 * sample rate, and sample format will be the same for all inputs and the
 * output.
 */

#include "libavutil/audioconvert.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define INPUT_OFF      0    /**< input has reached EOF */
#define INPUT_ON       1    /**< input is active */
#define INPUT_INACTIVE 2    /**< input is on, but is currently inactive */

#define DURATION_LONGEST  0
#define DURATION_SHORTEST 1
#define DURATION_FIRST    2


typedef struct FrameInfo {
    int nb_samples;
    int64_t pts;
    struct FrameInfo *next;
} FrameInfo;

/**
 * Linked list used to store timestamps and frame sizes of all frames in the
 * FIFO for the first input.
 *
 * This is needed to keep timestamps synchronized for the case where multiple
 * input frames are pushed to the filter for processing before a frame is
 * requested by the output link.
 */
typedef struct FrameList {
    int nb_frames;
    int nb_samples;
    FrameInfo *list;
    FrameInfo *end;
} FrameList;

static void frame_list_clear(FrameList *frame_list)
{
    if (frame_list) {
        while (frame_list->list) {
            FrameInfo *info = frame_list->list;
            frame_list->list = info->next;
            av_free(info);
        }
        frame_list->nb_frames  = 0;
        frame_list->nb_samples = 0;
        frame_list->end        = NULL;
    }
}

static int frame_list_next_frame_size(FrameList *frame_list)
{
    if (!frame_list->list)
        return 0;
    return frame_list->list->nb_samples;
}

static int64_t frame_list_next_pts(FrameList *frame_list)
{
    if (!frame_list->list)
        return AV_NOPTS_VALUE;
    return frame_list->list->pts;
}

static void frame_list_remove_samples(FrameList *frame_list, int nb_samples)
{
    if (nb_samples >= frame_list->nb_samples) {
        frame_list_clear(frame_list);
    } else {
        int samples = nb_samples;
        while (samples > 0) {
            FrameInfo *info = frame_list->list;
            av_assert0(info != NULL);
            if (info->nb_samples <= samples) {
                samples -= info->nb_samples;
                frame_list->list = info->next;
                if (!frame_list->list)
                    frame_list->end = NULL;
                frame_list->nb_frames--;
                frame_list->nb_samples -= info->nb_samples;
                av_free(info);
            } else {
                info->nb_samples       -= samples;
                info->pts              += samples;
                frame_list->nb_samples -= samples;
                samples = 0;
            }
        }
    }
}

static int frame_list_add_frame(FrameList *frame_list, int nb_samples, int64_t pts)
{
    FrameInfo *info = av_malloc(sizeof(*info));
    if (!info)
        return AVERROR(ENOMEM);
    info->nb_samples = nb_samples;
    info->pts        = pts;
    info->next       = NULL;

    if (!frame_list->list) {
        frame_list->list = info;
        frame_list->end  = info;
    } else {
        av_assert0(frame_list->end != NULL);
        frame_list->end->next = info;
        frame_list->end       = info;
    }
    frame_list->nb_frames++;
    frame_list->nb_samples += nb_samples;

    return 0;
}


typedef struct MixContext {
    const AVClass *class;       /**< class for AVOptions */

    int nb_inputs;              /**< number of inputs */
    int active_inputs;          /**< number of input currently active */
    int duration_mode;          /**< mode for determining duration */
    float dropout_transition;   /**< transition time when an input drops out */

    int nb_channels;            /**< number of channels */
    int sample_rate;            /**< sample rate */
    AVAudioFifo **fifos;        /**< audio fifo for each input */
    uint8_t *input_state;       /**< current state of each input */
    float *input_scale;         /**< mixing scale factor for each input */
    float scale_norm;           /**< normalization factor for all inputs */
    int64_t next_pts;           /**< calculated pts for next output frame */
    FrameList *frame_list;      /**< list of frame info for the first input */
} MixContext;

#define OFFSET(x) offsetof(MixContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    { "inputs", "Number of inputs.",
            OFFSET(nb_inputs), AV_OPT_TYPE_INT, { 2 }, 1, 32, A },
    { "duration", "How to determine the end-of-stream.",
            OFFSET(duration_mode), AV_OPT_TYPE_INT, { DURATION_LONGEST }, 0,  2, A, "duration" },
        { "longest",  "Duration of longest input.",  0, AV_OPT_TYPE_CONST, { DURATION_LONGEST  }, INT_MIN, INT_MAX, A, "duration" },
        { "shortest", "Duration of shortest input.", 0, AV_OPT_TYPE_CONST, { DURATION_SHORTEST }, INT_MIN, INT_MAX, A, "duration" },
        { "first",    "Duration of first input.",    0, AV_OPT_TYPE_CONST, { DURATION_FIRST    }, INT_MIN, INT_MAX, A, "duration" },
    { "dropout_transition", "Transition time, in seconds, for volume "
                            "renormalization when an input stream ends.",
            OFFSET(dropout_transition), AV_OPT_TYPE_FLOAT, { 2.0 }, 0, INT_MAX, A },
    { NULL },
};

static const AVClass amix_class = {
    .class_name = "amix filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


/**
 * Update the scaling factors to apply to each input during mixing.
 *
 * This balances the full volume range between active inputs and handles
 * volume transitions when EOF is encountered on an input but mixing continues
 * with the remaining inputs.
 */
static void calculate_scales(MixContext *s, int nb_samples)
{
    int i;

    if (s->scale_norm > s->active_inputs) {
        s->scale_norm -= nb_samples / (s->dropout_transition * s->sample_rate);
        s->scale_norm = FFMAX(s->scale_norm, s->active_inputs);
    }

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->input_state[i] == INPUT_ON)
            s->input_scale[i] = 1.0f / s->scale_norm;
        else
            s->input_scale[i] = 0.0f;
    }
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MixContext *s      = ctx->priv;
    int i;
    char buf[64];

    s->sample_rate     = outlink->sample_rate;
    outlink->time_base = (AVRational){ 1, outlink->sample_rate };
    s->next_pts        = AV_NOPTS_VALUE;

    s->frame_list = av_mallocz(sizeof(*s->frame_list));
    if (!s->frame_list)
        return AVERROR(ENOMEM);

    s->fifos = av_mallocz(s->nb_inputs * sizeof(*s->fifos));
    if (!s->fifos)
        return AVERROR(ENOMEM);

    s->nb_channels = av_get_channel_layout_nb_channels(outlink->channel_layout);
    for (i = 0; i < s->nb_inputs; i++) {
        s->fifos[i] = av_audio_fifo_alloc(outlink->format, s->nb_channels, 1024);
        if (!s->fifos[i])
            return AVERROR(ENOMEM);
    }

    s->input_state = av_malloc(s->nb_inputs);
    if (!s->input_state)
        return AVERROR(ENOMEM);
    memset(s->input_state, INPUT_ON, s->nb_inputs);
    s->active_inputs = s->nb_inputs;

    s->input_scale = av_mallocz(s->nb_inputs * sizeof(*s->input_scale));
    if (!s->input_scale)
        return AVERROR(ENOMEM);
    s->scale_norm = s->active_inputs;
    calculate_scales(s, 0);

    av_get_channel_layout_string(buf, sizeof(buf), -1, outlink->channel_layout);

    av_log(ctx, AV_LOG_VERBOSE,
           "inputs:%d fmt:%s srate:%"PRId64" cl:%s\n", s->nb_inputs,
           av_get_sample_fmt_name(outlink->format), outlink->sample_rate, buf);

    return 0;
}

/* TODO: move optimized version from DSPContext to libavutil */
static void vector_fmac_scalar(float *dst, const float *src, float mul, int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] += src[i] * mul;
}

/**
 * Read samples from the input FIFOs, mix, and write to the output link.
 */
static int output_frame(AVFilterLink *outlink, int nb_samples)
{
    AVFilterContext *ctx = outlink->src;
    MixContext      *s = ctx->priv;
    AVFilterBufferRef *out_buf, *in_buf;
    int i;

    calculate_scales(s, nb_samples);

    out_buf = ff_get_audio_buffer(outlink, AV_PERM_WRITE, nb_samples);
    if (!out_buf)
        return AVERROR(ENOMEM);

    in_buf = ff_get_audio_buffer(outlink, AV_PERM_WRITE, nb_samples);
    if (!in_buf)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->input_state[i] == INPUT_ON) {
            av_audio_fifo_read(s->fifos[i], (void **)in_buf->extended_data,
                               nb_samples);
            vector_fmac_scalar((float *)out_buf->extended_data[0],
                               (float *) in_buf->extended_data[0],
                               s->input_scale[i], nb_samples * s->nb_channels);
        }
    }
    avfilter_unref_buffer(in_buf);

    out_buf->pts = s->next_pts;
    if (s->next_pts != AV_NOPTS_VALUE)
        s->next_pts += nb_samples;

    ff_filter_samples(outlink, out_buf);

    return 0;
}

/**
 * Returns the smallest number of samples available in the input FIFOs other
 * than that of the first input.
 */
static int get_available_samples(MixContext *s)
{
    int i;
    int available_samples = INT_MAX;

    av_assert0(s->nb_inputs > 1);

    for (i = 1; i < s->nb_inputs; i++) {
        int nb_samples;
        if (s->input_state[i] == INPUT_OFF)
            continue;
        nb_samples = av_audio_fifo_size(s->fifos[i]);
        available_samples = FFMIN(available_samples, nb_samples);
    }
    if (available_samples == INT_MAX)
        return 0;
    return available_samples;
}

/**
 * Requests a frame, if needed, from each input link other than the first.
 */
static int request_samples(AVFilterContext *ctx, int min_samples)
{
    MixContext *s = ctx->priv;
    int i, ret;

    av_assert0(s->nb_inputs > 1);

    for (i = 1; i < s->nb_inputs; i++) {
        ret = 0;
        if (s->input_state[i] == INPUT_OFF)
            continue;
        while (!ret && av_audio_fifo_size(s->fifos[i]) < min_samples)
            ret = ff_request_frame(ctx->inputs[i]);
        if (ret == AVERROR_EOF) {
            if (av_audio_fifo_size(s->fifos[i]) == 0) {
                s->input_state[i] = INPUT_OFF;
                continue;
            }
        } else if (ret)
            return ret;
    }
    return 0;
}

/**
 * Calculates the number of active inputs and determines EOF based on the
 * duration option.
 *
 * @return 0 if mixing should continue, or AVERROR_EOF if mixing should stop.
 */
static int calc_active_inputs(MixContext *s)
{
    int i;
    int active_inputs = 0;
    for (i = 0; i < s->nb_inputs; i++)
        active_inputs += !!(s->input_state[i] != INPUT_OFF);
    s->active_inputs = active_inputs;

    if (!active_inputs ||
        (s->duration_mode == DURATION_FIRST && s->input_state[0] == INPUT_OFF) ||
        (s->duration_mode == DURATION_SHORTEST && active_inputs != s->nb_inputs))
        return AVERROR_EOF;
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MixContext      *s = ctx->priv;
    int ret;
    int wanted_samples, available_samples;

    ret = calc_active_inputs(s);
    if (ret < 0)
        return ret;

    if (s->input_state[0] == INPUT_OFF) {
        ret = request_samples(ctx, 1);
        if (ret < 0)
            return ret;

        ret = calc_active_inputs(s);
        if (ret < 0)
            return ret;

        available_samples = get_available_samples(s);
        if (!available_samples)
            return 0;

        return output_frame(outlink, available_samples);
    }

    if (s->frame_list->nb_frames == 0) {
        ret = ff_request_frame(ctx->inputs[0]);
        if (ret == AVERROR_EOF) {
            s->input_state[0] = INPUT_OFF;
            if (s->nb_inputs == 1)
                return AVERROR_EOF;
            else
                return AVERROR(EAGAIN);
        } else if (ret)
            return ret;
    }
    av_assert0(s->frame_list->nb_frames > 0);

    wanted_samples = frame_list_next_frame_size(s->frame_list);

    if (s->active_inputs > 1) {
        ret = request_samples(ctx, wanted_samples);
        if (ret < 0)
            return ret;

        ret = calc_active_inputs(s);
        if (ret < 0)
            return ret;

        available_samples = get_available_samples(s);
        if (!available_samples)
            return 0;
        available_samples = FFMIN(available_samples, wanted_samples);
    } else {
        available_samples = wanted_samples;
    }

    s->next_pts = frame_list_next_pts(s->frame_list);
    frame_list_remove_samples(s->frame_list, available_samples);

    return output_frame(outlink, available_samples);
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    MixContext       *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int i;

    for (i = 0; i < ctx->input_count; i++)
        if (ctx->inputs[i] == inlink)
            break;
    if (i >= ctx->input_count) {
        av_log(ctx, AV_LOG_ERROR, "unknown input link\n");
        return;
    }

    if (i == 0) {
        int64_t pts = av_rescale_q(buf->pts, inlink->time_base,
                                   outlink->time_base);
        frame_list_add_frame(s->frame_list, buf->audio->nb_samples, pts);
    }

    av_audio_fifo_write(s->fifos[i], (void **)buf->extended_data,
                        buf->audio->nb_samples);

    avfilter_unref_buffer(buf);
}

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    MixContext *s = ctx->priv;
    int i, ret;

    s->class = &amix_class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string '%s'.\n", args);
        return ret;
    }
    av_opt_free(s);

    for (i = 0; i < s->nb_inputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "input%d", i);
        pad.type           = AVMEDIA_TYPE_AUDIO;
        pad.name           = av_strdup(name);
        pad.filter_samples = filter_samples;

        ff_insert_inpad(ctx, i, &pad);
    }

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    int i;
    MixContext *s = ctx->priv;

    if (s->fifos) {
        for (i = 0; i < s->nb_inputs; i++)
            av_audio_fifo_free(s->fifos[i]);
        av_freep(&s->fifos);
    }
    frame_list_clear(s->frame_list);
    av_freep(&s->frame_list);
    av_freep(&s->input_state);
    av_freep(&s->input_scale);

    for (i = 0; i < ctx->input_count; i++)
        av_freep(&ctx->input_pads[i].name);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    ff_add_format(&formats, AV_SAMPLE_FMT_FLT);
    ff_set_common_formats(ctx, formats);
    ff_set_common_channel_layouts(ctx, ff_all_channel_layouts());
    ff_set_common_samplerates(ctx, ff_all_samplerates());
    return 0;
}

AVFilter avfilter_af_amix = {
    .name          = "amix",
    .description   = NULL_IF_CONFIG_SMALL("Audio mixing."),
    .priv_size     = sizeof(MixContext),

    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name          = "default",
                                          .type          = AVMEDIA_TYPE_AUDIO,
                                          .config_props  = config_output,
                                          .request_frame = request_frame },
                                        { .name = NULL}},
};
