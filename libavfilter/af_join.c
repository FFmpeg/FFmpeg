/*
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
 * Audio join filter
 *
 * Join multiple audio inputs as different channels in
 * a single output
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "filters.h"
#include "internal.h"

typedef struct ChannelMap {
    int input;                ///< input stream index
    int       in_channel_idx; ///< index of in_channel in the input stream data
    uint64_t  in_channel;     ///< layout describing the input channel
    uint64_t out_channel;     ///< layout describing the output channel
} ChannelMap;

typedef struct JoinContext {
    const AVClass *class;

    int inputs;
    char *map;
    char    *channel_layout_str;
    uint64_t channel_layout;

    int      nb_channels;
    ChannelMap *channels;

    /**
     * Temporary storage for input frames, until we get one on each input.
     */
    AVFrame **input_frames;

    /**
     *  Temporary storage for buffer references, for assembling the output frame.
     */
    AVBufferRef **buffers;
} JoinContext;

#define OFFSET(x) offsetof(JoinContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption join_options[] = {
    { "inputs",         "Number of input streams.", OFFSET(inputs),             AV_OPT_TYPE_INT,    { .i64 = 2 }, 1, INT_MAX,       A|F },
    { "channel_layout", "Channel layout of the "
                        "output stream.",           OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, {.str = "stereo"}, 0, 0, A|F },
    { "map",            "A comma-separated list of channels maps in the format "
                        "'input_stream.input_channel-output_channel.",
                                                    OFFSET(map),                AV_OPT_TYPE_STRING,                 .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(join);

static int parse_maps(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    char separator = '|';
    char *cur      = s->map;

    while (cur && *cur) {
        char *sep, *next, *p;
        uint64_t in_channel = 0, out_channel = 0;
        int input_idx, out_ch_idx, in_ch_idx;

        next = strchr(cur, separator);
        if (next)
            *next++ = 0;

        /* split the map into input and output parts */
        if (!(sep = strchr(cur, '-'))) {
            av_log(ctx, AV_LOG_ERROR, "Missing separator '-' in channel "
                   "map '%s'\n", cur);
            return AVERROR(EINVAL);
        }
        *sep++ = 0;

#define PARSE_CHANNEL(str, var, inout)                                         \
        if (!(var = av_get_channel_layout(str))) {                             \
            av_log(ctx, AV_LOG_ERROR, "Invalid " inout " channel: %s.\n", str);\
            return AVERROR(EINVAL);                                            \
        }                                                                      \
        if (av_get_channel_layout_nb_channels(var) != 1) {                     \
            av_log(ctx, AV_LOG_ERROR, "Channel map describes more than one "   \
                   inout " channel.\n");                                       \
            return AVERROR(EINVAL);                                            \
        }

        /* parse output channel */
        PARSE_CHANNEL(sep, out_channel, "output");
        if (!(out_channel & s->channel_layout)) {
            av_log(ctx, AV_LOG_ERROR, "Output channel '%s' is not present in "
                   "requested channel layout.\n", sep);
            return AVERROR(EINVAL);
        }

        out_ch_idx = av_get_channel_layout_channel_index(s->channel_layout,
                                                         out_channel);
        if (s->channels[out_ch_idx].input >= 0) {
            av_log(ctx, AV_LOG_ERROR, "Multiple maps for output channel "
                   "'%s'.\n", sep);
            return AVERROR(EINVAL);
        }

        /* parse input channel */
        input_idx = strtol(cur, &cur, 0);
        if (input_idx < 0 || input_idx >= s->inputs) {
            av_log(ctx, AV_LOG_ERROR, "Invalid input stream index: %d.\n",
                   input_idx);
            return AVERROR(EINVAL);
        }

        if (*cur)
            cur++;

        in_ch_idx = strtol(cur, &p, 0);
        if (p == cur) {
            /* channel specifier is not a number,
             * try to parse as channel name */
            PARSE_CHANNEL(cur, in_channel, "input");
        }

        s->channels[out_ch_idx].input      = input_idx;
        if (in_channel)
            s->channels[out_ch_idx].in_channel = in_channel;
        else
            s->channels[out_ch_idx].in_channel_idx = in_ch_idx;

        cur = next;
    }
    return 0;
}

static av_cold int join_init(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    int ret, i;

    if (!(s->channel_layout = av_get_channel_layout(s->channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout '%s'.\n",
               s->channel_layout_str);
        return AVERROR(EINVAL);
    }

    s->nb_channels  = av_get_channel_layout_nb_channels(s->channel_layout);
    s->channels     = av_mallocz_array(s->nb_channels, sizeof(*s->channels));
    s->buffers      = av_mallocz_array(s->nb_channels, sizeof(*s->buffers));
    s->input_frames = av_mallocz_array(s->inputs, sizeof(*s->input_frames));
    if (!s->channels || !s->buffers|| !s->input_frames)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_channels; i++) {
        s->channels[i].out_channel = av_channel_layout_extract_channel(s->channel_layout, i);
        s->channels[i].input       = -1;
    }

    if ((ret = parse_maps(ctx)) < 0)
        return ret;

    for (i = 0; i < s->inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_AUDIO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static av_cold void join_uninit(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    int i;

    for (i = 0; i < s->inputs && s->input_frames; i++) {
        av_frame_free(&s->input_frames[i]);
    }

    for (i = 0; i < ctx->nb_inputs; i++) {
        av_freep(&ctx->input_pads[i].name);
    }

    av_freep(&s->channels);
    av_freep(&s->buffers);
    av_freep(&s->input_frames);
}

static int join_query_formats(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    AVFilterChannelLayouts *layouts = NULL;
    int i, ret;

    if ((ret = ff_add_channel_layout(&layouts, s->channel_layout)) < 0 ||
        (ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts)) < 0)
        return ret;

    for (i = 0; i < ctx->nb_inputs; i++) {
        layouts = ff_all_channel_layouts();
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts)) < 0)
            return ret;
    }

    if ((ret = ff_set_common_formats(ctx, ff_planar_sample_fmts())) < 0 ||
        (ret = ff_set_common_samplerates(ctx, ff_all_samplerates())) < 0)
        return ret;

    return 0;
}

static void guess_map_matching(AVFilterContext *ctx, ChannelMap *ch,
                               uint64_t *inputs)
{
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink *link = ctx->inputs[i];

        if (ch->out_channel & link->channel_layout &&
            !(ch->out_channel & inputs[i])) {
            ch->input      = i;
            ch->in_channel = ch->out_channel;
            inputs[i]     |= ch->out_channel;
            return;
        }
    }
}

static void guess_map_any(AVFilterContext *ctx, ChannelMap *ch,
                          uint64_t *inputs)
{
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink *link = ctx->inputs[i];

        if ((inputs[i] & link->channel_layout) != link->channel_layout) {
            uint64_t unused = link->channel_layout & ~inputs[i];

            ch->input      = i;
            ch->in_channel = av_channel_layout_extract_channel(unused, 0);
            inputs[i]     |= ch->in_channel;
            return;
        }
    }
}

static int join_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    JoinContext       *s = ctx->priv;
    uint64_t *inputs;   // nth element tracks which channels are used from nth input
    int i, ret = 0;

    /* initialize inputs to user-specified mappings */
    if (!(inputs = av_mallocz_array(ctx->nb_inputs, sizeof(*inputs))))
        return AVERROR(ENOMEM);
    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        AVFilterLink *inlink;

        if (ch->input < 0)
            continue;

        inlink = ctx->inputs[ch->input];

        if (!ch->in_channel)
            ch->in_channel = av_channel_layout_extract_channel(inlink->channel_layout,
                                                               ch->in_channel_idx);

        if (!(ch->in_channel & inlink->channel_layout)) {
            av_log(ctx, AV_LOG_ERROR, "Requested channel %s is not present in "
                   "input stream #%d.\n", av_get_channel_name(ch->in_channel),
                   ch->input);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        inputs[ch->input] |= ch->in_channel;
    }

    /* guess channel maps when not explicitly defined */
    /* first try unused matching channels */
    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];

        if (ch->input < 0)
            guess_map_matching(ctx, ch, inputs);
    }

    /* if the above failed, try to find _any_ unused input channel */
    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];

        if (ch->input < 0)
            guess_map_any(ctx, ch, inputs);

        if (ch->input < 0) {
            av_log(ctx, AV_LOG_ERROR, "Could not find input channel for "
                   "output channel '%s'.\n",
                   av_get_channel_name(ch->out_channel));
            goto fail;
        }

        ch->in_channel_idx = av_get_channel_layout_channel_index(ctx->inputs[ch->input]->channel_layout,
                                                                 ch->in_channel);
    }

    /* print mappings */
    av_log(ctx, AV_LOG_VERBOSE, "mappings: ");
    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        av_log(ctx, AV_LOG_VERBOSE, "%d.%s => %s ", ch->input,
               av_get_channel_name(ch->in_channel),
               av_get_channel_name(ch->out_channel));
    }
    av_log(ctx, AV_LOG_VERBOSE, "\n");

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (!inputs[i])
            av_log(ctx, AV_LOG_WARNING, "No channels are used from input "
                   "stream %d.\n", i);
    }

fail:
    av_freep(&inputs);
    return ret;
}

static int try_push_frame(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    JoinContext *s       = ctx->priv;
    AVFrame *frame;
    int linesize   = INT_MAX;
    int nb_samples = INT_MAX;
    int nb_buffers = 0;
    int i, j, ret;

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (!s->input_frames[i])
            return 0;
        nb_samples = FFMIN(nb_samples, s->input_frames[i]->nb_samples);
    }
    if (!nb_samples)
        return 0;

    /* setup the output frame */
    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);
    if (s->nb_channels > FF_ARRAY_ELEMS(frame->data)) {
        frame->extended_data = av_mallocz_array(s->nb_channels,
                                          sizeof(*frame->extended_data));
        if (!frame->extended_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* copy the data pointers */
    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        AVFrame *cur   = s->input_frames[ch->input];
        AVBufferRef *buf;

        frame->extended_data[i] = cur->extended_data[ch->in_channel_idx];
        linesize = FFMIN(linesize, cur->linesize[0]);

        /* add the buffer where this plan is stored to the list if it's
         * not already there */
        buf = av_frame_get_plane_buffer(cur, ch->in_channel_idx);
        if (!buf) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
        for (j = 0; j < nb_buffers; j++)
            if (s->buffers[j]->buffer == buf->buffer)
                break;
        if (j == i)
            s->buffers[nb_buffers++] = buf;
    }

    /* create references to the buffers we copied to output */
    if (nb_buffers > FF_ARRAY_ELEMS(frame->buf)) {
        frame->nb_extended_buf = nb_buffers - FF_ARRAY_ELEMS(frame->buf);
        frame->extended_buf = av_mallocz_array(frame->nb_extended_buf,
                                               sizeof(*frame->extended_buf));
        if (!frame->extended_buf) {
            frame->nb_extended_buf = 0;
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }
    for (i = 0; i < FFMIN(FF_ARRAY_ELEMS(frame->buf), nb_buffers); i++) {
        frame->buf[i] = av_buffer_ref(s->buffers[i]);
        if (!frame->buf[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        frame->extended_buf[i] = av_buffer_ref(s->buffers[i +
                                               FF_ARRAY_ELEMS(frame->buf)]);
        if (!frame->extended_buf[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    frame->nb_samples     = nb_samples;
    frame->channel_layout = outlink->channel_layout;
    frame->channels       = outlink->channels;
    frame->sample_rate    = outlink->sample_rate;
    frame->format         = outlink->format;
    frame->pts            = s->input_frames[0]->pts;
    frame->linesize[0]    = linesize;
    if (frame->data != frame->extended_data) {
        memcpy(frame->data, frame->extended_data, sizeof(*frame->data) *
               FFMIN(FF_ARRAY_ELEMS(frame->data), s->nb_channels));
    }

    ret = ff_filter_frame(outlink, frame);

    for (i = 0; i < ctx->nb_inputs; i++)
        av_frame_free(&s->input_frames[i]);

    return ret;

fail:
    av_frame_free(&frame);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    int i, ret, status;
    int nb_samples = 0;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    if (!s->input_frames[0]) {
        ret = ff_inlink_consume_frame(ctx->inputs[0], &s->input_frames[0]);
        if (ret < 0) {
            return ret;
        } else if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        } else {
            if (ff_outlink_frame_wanted(ctx->outputs[0]) && !s->input_frames[0]) {
                ff_inlink_request_frame(ctx->inputs[0]);
                return 0;
            }
        }
        if (!s->input_frames[0]) {
            return 0;
        }
    }

    nb_samples = s->input_frames[0]->nb_samples;

    for (i = 1; i < ctx->nb_inputs && nb_samples > 0; i++) {
        if (s->input_frames[i])
            continue;

        if (ff_inlink_check_available_samples(ctx->inputs[i], nb_samples) > 0) {
            ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &s->input_frames[i]);
            if (ret < 0) {
                return ret;
            } else if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
                ff_outlink_set_status(ctx->outputs[0], status, pts);
                return 0;
            }
        } else {
            if (ff_outlink_frame_wanted(ctx->outputs[0])) {
                ff_inlink_request_frame(ctx->inputs[i]);
                return 0;
            }
        }
    }

    return try_push_frame(ctx);
}

static const AVFilterPad avfilter_af_join_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = join_config_output,
    },
    { NULL }
};

AVFilter ff_af_join = {
    .name           = "join",
    .description    = NULL_IF_CONFIG_SMALL("Join multiple audio streams into "
                                           "multi-channel output."),
    .priv_size      = sizeof(JoinContext),
    .priv_class     = &join_class,
    .init           = join_init,
    .uninit         = join_uninit,
    .activate       = activate,
    .query_formats  = join_query_formats,
    .inputs         = NULL,
    .outputs        = avfilter_af_join_outputs,
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
