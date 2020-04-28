/*
 * Audio Mix Filter
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
 * Audio Mix Filter
 *
 * Mixes audio from multiple sources into a single output. The channel layout,
 * sample rate, and sample format will be the same for all inputs and the
 * output.
 */

#include "libavutil/attributes.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"

#define INPUT_ON       1    /**< input is active */
#define INPUT_EOF      2    /**< input has reached EOF (may still be active) */

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
            av_assert0(info);
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
        av_assert0(frame_list->end);
        frame_list->end->next = info;
        frame_list->end       = info;
    }
    frame_list->nb_frames++;
    frame_list->nb_samples += nb_samples;

    return 0;
}

/* FIXME: use directly links fifo */

typedef struct MixContext {
    const AVClass *class;       /**< class for AVOptions */
    AVFloatDSPContext *fdsp;

    int nb_inputs;              /**< number of inputs */
    int active_inputs;          /**< number of input currently active */
    int duration_mode;          /**< mode for determining duration */
    float dropout_transition;   /**< transition time when an input drops out */
    char *weights_str;          /**< string for custom weights for every input */

    int nb_channels;            /**< number of channels */
    int sample_rate;            /**< sample rate */
    int planar;
    AVAudioFifo **fifos;        /**< audio fifo for each input */
    uint8_t *input_state;       /**< current state of each input */
    float *input_scale;         /**< mixing scale factor for each input */
    float *weights;             /**< custom weights for every input */
    float weight_sum;           /**< sum of custom weights for every input */
    float *scale_norm;          /**< normalization factor for every input */
    int64_t next_pts;           /**< calculated pts for next output frame */
    FrameList *frame_list;      /**< list of frame info for the first input */
} MixContext;

#define OFFSET(x) offsetof(MixContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define T AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption amix_options[] = {
    { "inputs", "Number of inputs.",
            OFFSET(nb_inputs), AV_OPT_TYPE_INT, { .i64 = 2 }, 1, INT16_MAX, A|F },
    { "duration", "How to determine the end-of-stream.",
            OFFSET(duration_mode), AV_OPT_TYPE_INT, { .i64 = DURATION_LONGEST }, 0,  2, A|F, "duration" },
        { "longest",  "Duration of longest input.",  0, AV_OPT_TYPE_CONST, { .i64 = DURATION_LONGEST  }, 0, 0, A|F, "duration" },
        { "shortest", "Duration of shortest input.", 0, AV_OPT_TYPE_CONST, { .i64 = DURATION_SHORTEST }, 0, 0, A|F, "duration" },
        { "first",    "Duration of first input.",    0, AV_OPT_TYPE_CONST, { .i64 = DURATION_FIRST    }, 0, 0, A|F, "duration" },
    { "dropout_transition", "Transition time, in seconds, for volume "
                            "renormalization when an input stream ends.",
            OFFSET(dropout_transition), AV_OPT_TYPE_FLOAT, { .dbl = 2.0 }, 0, INT_MAX, A|F },
    { "weights", "Set weight for each input.",
            OFFSET(weights_str), AV_OPT_TYPE_STRING, {.str="1 1"}, 0, 0, A|F|T },
    { NULL }
};

AVFILTER_DEFINE_CLASS(amix);

/**
 * Update the scaling factors to apply to each input during mixing.
 *
 * This balances the full volume range between active inputs and handles
 * volume transitions when EOF is encountered on an input but mixing continues
 * with the remaining inputs.
 */
static void calculate_scales(MixContext *s, int nb_samples)
{
    float weight_sum = 0.f;
    int i;

    for (i = 0; i < s->nb_inputs; i++)
        if (s->input_state[i] & INPUT_ON)
            weight_sum += FFABS(s->weights[i]);

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->input_state[i] & INPUT_ON) {
            if (s->scale_norm[i] > weight_sum / FFABS(s->weights[i])) {
                s->scale_norm[i] -= ((s->weight_sum / FFABS(s->weights[i])) / s->nb_inputs) *
                                    nb_samples / (s->dropout_transition * s->sample_rate);
                s->scale_norm[i] = FFMAX(s->scale_norm[i], weight_sum / FFABS(s->weights[i]));
            }
        }
    }

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->input_state[i] & INPUT_ON)
            s->input_scale[i] = 1.0f / s->scale_norm[i] * FFSIGN(s->weights[i]);
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

    s->planar          = av_sample_fmt_is_planar(outlink->format);
    s->sample_rate     = outlink->sample_rate;
    outlink->time_base = (AVRational){ 1, outlink->sample_rate };
    s->next_pts        = AV_NOPTS_VALUE;

    s->frame_list = av_mallocz(sizeof(*s->frame_list));
    if (!s->frame_list)
        return AVERROR(ENOMEM);

    s->fifos = av_mallocz_array(s->nb_inputs, sizeof(*s->fifos));
    if (!s->fifos)
        return AVERROR(ENOMEM);

    s->nb_channels = outlink->channels;
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

    s->input_scale = av_mallocz_array(s->nb_inputs, sizeof(*s->input_scale));
    s->scale_norm  = av_mallocz_array(s->nb_inputs, sizeof(*s->scale_norm));
    if (!s->input_scale || !s->scale_norm)
        return AVERROR(ENOMEM);
    for (i = 0; i < s->nb_inputs; i++)
        s->scale_norm[i] = s->weight_sum / FFABS(s->weights[i]);
    calculate_scales(s, 0);

    av_get_channel_layout_string(buf, sizeof(buf), -1, outlink->channel_layout);

    av_log(ctx, AV_LOG_VERBOSE,
           "inputs:%d fmt:%s srate:%d cl:%s\n", s->nb_inputs,
           av_get_sample_fmt_name(outlink->format), outlink->sample_rate, buf);

    return 0;
}

/**
 * Read samples from the input FIFOs, mix, and write to the output link.
 */
static int output_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MixContext      *s = ctx->priv;
    AVFrame *out_buf, *in_buf;
    int nb_samples, ns, i;

    if (s->input_state[0] & INPUT_ON) {
        /* first input live: use the corresponding frame size */
        nb_samples = frame_list_next_frame_size(s->frame_list);
        for (i = 1; i < s->nb_inputs; i++) {
            if (s->input_state[i] & INPUT_ON) {
                ns = av_audio_fifo_size(s->fifos[i]);
                if (ns < nb_samples) {
                    if (!(s->input_state[i] & INPUT_EOF))
                        /* unclosed input with not enough samples */
                        return 0;
                    /* closed input to drain */
                    nb_samples = ns;
                }
            }
        }
    } else {
        /* first input closed: use the available samples */
        nb_samples = INT_MAX;
        for (i = 1; i < s->nb_inputs; i++) {
            if (s->input_state[i] & INPUT_ON) {
                ns = av_audio_fifo_size(s->fifos[i]);
                nb_samples = FFMIN(nb_samples, ns);
            }
        }
        if (nb_samples == INT_MAX) {
            ff_outlink_set_status(outlink, AVERROR_EOF, s->next_pts);
            return 0;
        }
    }

    s->next_pts = frame_list_next_pts(s->frame_list);
    frame_list_remove_samples(s->frame_list, nb_samples);

    calculate_scales(s, nb_samples);

    if (nb_samples == 0)
        return 0;

    out_buf = ff_get_audio_buffer(outlink, nb_samples);
    if (!out_buf)
        return AVERROR(ENOMEM);

    in_buf = ff_get_audio_buffer(outlink, nb_samples);
    if (!in_buf) {
        av_frame_free(&out_buf);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->input_state[i] & INPUT_ON) {
            int planes, plane_size, p;

            av_audio_fifo_read(s->fifos[i], (void **)in_buf->extended_data,
                               nb_samples);

            planes     = s->planar ? s->nb_channels : 1;
            plane_size = nb_samples * (s->planar ? 1 : s->nb_channels);
            plane_size = FFALIGN(plane_size, 16);

            if (out_buf->format == AV_SAMPLE_FMT_FLT ||
                out_buf->format == AV_SAMPLE_FMT_FLTP) {
                for (p = 0; p < planes; p++) {
                    s->fdsp->vector_fmac_scalar((float *)out_buf->extended_data[p],
                                                (float *) in_buf->extended_data[p],
                                                s->input_scale[i], plane_size);
                }
            } else {
                for (p = 0; p < planes; p++) {
                    s->fdsp->vector_dmac_scalar((double *)out_buf->extended_data[p],
                                                (double *) in_buf->extended_data[p],
                                                s->input_scale[i], plane_size);
                }
            }
        }
    }
    av_frame_free(&in_buf);

    out_buf->pts = s->next_pts;
    if (s->next_pts != AV_NOPTS_VALUE)
        s->next_pts += nb_samples;

    return ff_filter_frame(outlink, out_buf);
}

/**
 * Requests a frame, if needed, from each input link other than the first.
 */
static int request_samples(AVFilterContext *ctx, int min_samples)
{
    MixContext *s = ctx->priv;
    int i;

    av_assert0(s->nb_inputs > 1);

    for (i = 1; i < s->nb_inputs; i++) {
        if (!(s->input_state[i] & INPUT_ON) ||
             (s->input_state[i] & INPUT_EOF))
            continue;
        if (av_audio_fifo_size(s->fifos[i]) >= min_samples)
            continue;
        ff_inlink_request_frame(ctx->inputs[i]);
    }
    return output_frame(ctx->outputs[0]);
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
        active_inputs += !!(s->input_state[i] & INPUT_ON);
    s->active_inputs = active_inputs;

    if (!active_inputs ||
        (s->duration_mode == DURATION_FIRST && !(s->input_state[0] & INPUT_ON)) ||
        (s->duration_mode == DURATION_SHORTEST && active_inputs != s->nb_inputs))
        return AVERROR_EOF;
    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    MixContext *s = ctx->priv;
    AVFrame *buf = NULL;
    int i, ret;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        if ((ret = ff_inlink_consume_frame(ctx->inputs[i], &buf)) > 0) {
            if (i == 0) {
                int64_t pts = av_rescale_q(buf->pts, inlink->time_base,
                                           outlink->time_base);
                ret = frame_list_add_frame(s->frame_list, buf->nb_samples, pts);
                if (ret < 0) {
                    av_frame_free(&buf);
                    return ret;
                }
            }

            ret = av_audio_fifo_write(s->fifos[i], (void **)buf->extended_data,
                                      buf->nb_samples);
            if (ret < 0) {
                av_frame_free(&buf);
                return ret;
            }

            av_frame_free(&buf);

            ret = output_frame(outlink);
            if (ret < 0)
                return ret;
        }
    }

    for (i = 0; i < s->nb_inputs; i++) {
        int64_t pts;
        int status;

        if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
            if (status == AVERROR_EOF) {
                if (i == 0) {
                    s->input_state[i] = 0;
                    if (s->nb_inputs == 1) {
                        ff_outlink_set_status(outlink, status, pts);
                        return 0;
                    }
                } else {
                    s->input_state[i] |= INPUT_EOF;
                    if (av_audio_fifo_size(s->fifos[i]) == 0) {
                        s->input_state[i] = 0;
                    }
                }
            }
        }
    }

    if (calc_active_inputs(s)) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->next_pts);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        int wanted_samples;

        if (!(s->input_state[0] & INPUT_ON))
            return request_samples(ctx, 1);

        if (s->frame_list->nb_frames == 0) {
            ff_inlink_request_frame(ctx->inputs[0]);
            return 0;
        }
        av_assert0(s->frame_list->nb_frames > 0);

        wanted_samples = frame_list_next_frame_size(s->frame_list);

        return request_samples(ctx, wanted_samples);
    }

    return 0;
}

static void parse_weights(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    float last_weight = 1.f;
    char *p;
    int i;

    s->weight_sum = 0.f;
    p = s->weights_str;
    for (i = 0; i < s->nb_inputs; i++) {
        last_weight = av_strtod(p, &p);
        s->weights[i] = last_weight;
        s->weight_sum += FFABS(last_weight);
        if (p && *p) {
            p++;
        } else {
            i++;
            break;
        }
    }

    for (; i < s->nb_inputs; i++) {
        s->weights[i] = last_weight;
        s->weight_sum += FFABS(last_weight);
    }
}

static av_cold int init(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type           = AVMEDIA_TYPE_AUDIO;
        pad.name           = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    s->weights = av_mallocz_array(s->nb_inputs, sizeof(*s->weights));
    if (!s->weights)
        return AVERROR(ENOMEM);

    parse_weights(ctx);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
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
    av_freep(&s->scale_norm);
    av_freep(&s->weights);
    av_freep(&s->fdsp);

    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = ff_add_format(&formats, AV_SAMPLE_FMT_FLT ))          < 0 ||
        (ret = ff_add_format(&formats, AV_SAMPLE_FMT_FLTP))          < 0 ||
        (ret = ff_add_format(&formats, AV_SAMPLE_FMT_DBL ))          < 0 ||
        (ret = ff_add_format(&formats, AV_SAMPLE_FMT_DBLP))          < 0 ||
        (ret = ff_set_common_formats        (ctx, formats))          < 0 ||
        (ret = ff_set_common_channel_layouts(ctx, layouts))          < 0 ||
        (ret = ff_set_common_samplerates(ctx, ff_all_samplerates())) < 0)
        goto fail;
    return 0;
fail:
    if (layouts)
        av_freep(&layouts->channel_layouts);
    av_freep(&layouts);
    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    MixContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    parse_weights(ctx);
    for (int i = 0; i < s->nb_inputs; i++)
        s->scale_norm[i] = s->weight_sum / FFABS(s->weights[i]);
    calculate_scales(s, 0);

    return 0;
}

static const AVFilterPad avfilter_af_amix_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_af_amix = {
    .name           = "amix",
    .description    = NULL_IF_CONFIG_SMALL("Audio mixing."),
    .priv_size      = sizeof(MixContext),
    .priv_class     = &amix_class,
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    .query_formats  = query_formats,
    .inputs         = NULL,
    .outputs        = avfilter_af_amix_outputs,
    .process_command = process_command,
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
