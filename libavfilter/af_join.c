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
    int input;                     ///< input stream index
    int            in_channel_idx; ///< index of in_channel in the input stream data
    enum AVChannel in_channel;
    enum AVChannel out_channel;
} ChannelMap;

typedef struct JoinContext {
    const AVClass *class;

    int inputs;
    char *map;
    char    *channel_layout_str;
    AVChannelLayout ch_layout;

    int64_t  eof_pts;

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

#define MAP_SEPARATOR '|'

AVFILTER_DEFINE_CLASS(join);

static int parse_maps(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    char *cur      = s->map;

    while (cur && *cur) {
        ChannelMap *map;
        char *sep, *next, *p;
        int input_idx, out_ch_idx;

        next = strchr(cur, MAP_SEPARATOR);
        if (next)
            *next++ = 0;

        /* split the map into input and output parts */
        if (!(sep = strchr(cur, '-'))) {
            av_log(ctx, AV_LOG_ERROR, "Missing separator '-' in channel "
                   "map '%s'\n", cur);
            return AVERROR(EINVAL);
        }
        *sep++ = 0;

        /* parse output channel */
        out_ch_idx = av_channel_layout_index_from_string(&s->ch_layout, sep);
        if (out_ch_idx < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid output channel: %s.\n", sep);
            return AVERROR(EINVAL);
        }

        map = &s->channels[out_ch_idx];

        if (map->input >= 0) {
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

        map->input          = input_idx;
        map->in_channel     = AV_CHAN_NONE;
        map->in_channel_idx = strtol(cur, &p, 0);
        if (p == cur) {
            /* channel specifier is not a number, handle as channel name */
            map->in_channel = av_channel_from_string(cur);
            if (map->in_channel < 0) {
                av_log(ctx, AV_LOG_ERROR, "Invalid input channel: %s.\n", cur);
                return AVERROR(EINVAL);
            }
        } else if (map->in_channel_idx < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid input channel index: %d\n", map->in_channel_idx);
            return AVERROR(EINVAL);
        }

        cur = next;
    }
    return 0;
}

static av_cold int join_init(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    int ret, i;

    ret = av_channel_layout_from_string(&s->ch_layout, s->channel_layout_str);
    if (ret < 0) {
#if FF_API_OLD_CHANNEL_LAYOUT
        uint64_t mask;
FF_DISABLE_DEPRECATION_WARNINGS
        mask = av_get_channel_layout(s->channel_layout_str);
        if (!mask) {
#endif
            av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout '%s'.\n",
                   s->channel_layout_str);
            return AVERROR(EINVAL);
#if FF_API_OLD_CHANNEL_LAYOUT
        }
FF_ENABLE_DEPRECATION_WARNINGS
        av_log(ctx, AV_LOG_WARNING, "Channel layout '%s' uses a deprecated syntax.\n",
               s->channel_layout_str);
        av_channel_layout_from_mask(&s->ch_layout, mask);
#endif
    }

    s->channels     = av_calloc(s->ch_layout.nb_channels, sizeof(*s->channels));
    s->buffers      = av_calloc(s->ch_layout.nb_channels, sizeof(*s->buffers));
    s->input_frames = av_calloc(s->inputs, sizeof(*s->input_frames));
    if (!s->channels || !s->buffers|| !s->input_frames)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->ch_layout.nb_channels; i++) {
        s->channels[i].out_channel    = av_channel_layout_channel_from_index(&s->ch_layout, i);
        s->channels[i].input          = -1;
        s->channels[i].in_channel_idx = -1;
        s->channels[i].in_channel     = AV_CHAN_NONE;
    }

    if ((ret = parse_maps(ctx)) < 0)
        return ret;

    for (i = 0; i < s->inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_AUDIO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
            return ret;
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

    av_freep(&s->channels);
    av_freep(&s->buffers);
    av_freep(&s->input_frames);
}

static int join_query_formats(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    AVFilterChannelLayouts *layouts = NULL;
    int i, ret;

    if ((ret = ff_add_channel_layout(&layouts, &s->ch_layout)) < 0 ||
        (ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
        return ret;

    for (i = 0; i < ctx->nb_inputs; i++) {
        layouts = ff_all_channel_layouts();
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->outcfg.channel_layouts)) < 0)
            return ret;
    }

    if ((ret = ff_set_common_formats(ctx, ff_planar_sample_fmts())) < 0 ||
        (ret = ff_set_common_all_samplerates(ctx)) < 0)
        return ret;

    return 0;
}

typedef struct ChannelList {
    enum AVChannel *ch;
    int          nb_ch;
} ChannelList;

static enum AVChannel channel_list_pop(ChannelList *chl, int idx)
{
    enum AVChannel ret = chl->ch[idx];
    memmove(chl->ch + idx, chl->ch + idx + 1,
            (chl->nb_ch - idx - 1) * sizeof(*chl->ch));
    chl->nb_ch--;
    return ret;
}

/*
 * If ch is present in chl, remove it from the list and return it.
 * Otherwise return AV_CHAN_NONE.
 */
static enum AVChannel channel_list_pop_ch(ChannelList *chl, enum AVChannel ch)
{
    for (int i = 0; i < chl->nb_ch; i++)
        if (chl->ch[i] == ch)
            return channel_list_pop(chl, i);
    return AV_CHAN_NONE;
}

static void guess_map_matching(AVFilterContext *ctx, ChannelMap *ch,
                               ChannelList *inputs)
{
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (channel_list_pop_ch(&inputs[i], ch->out_channel) != AV_CHAN_NONE) {
            ch->input      = i;
            ch->in_channel = ch->out_channel;
            return;
        }
    }
}

static void guess_map_any(AVFilterContext *ctx, ChannelMap *ch,
                          ChannelList *inputs)
{
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (inputs[i].nb_ch) {
            ch->input      = i;
            ch->in_channel = channel_list_pop(&inputs[i], 0);
            return;
        }
    }
}

static int join_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    JoinContext       *s = ctx->priv;
    // unused channels from each input
    ChannelList *inputs_unused;
    char inbuf[64], outbuf[64];
    int i, ret = 0;

    /* initialize unused channel list for each input */
    inputs_unused = av_calloc(ctx->nb_inputs, sizeof(*inputs_unused));
    if (!inputs_unused)
        return AVERROR(ENOMEM);
    for (i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        AVChannelLayout *chl = &inlink->ch_layout;
        ChannelList      *iu = &inputs_unused[i];

        iu->nb_ch = chl->nb_channels;
        iu->ch    = av_malloc_array(iu->nb_ch, sizeof(*iu->ch));
        if (!iu->ch) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        for (int ch_idx = 0; ch_idx < iu->nb_ch; ch_idx++) {
            iu->ch[ch_idx] = av_channel_layout_channel_from_index(chl, ch_idx);
            if (iu->ch[ch_idx] < 0) {
                /* no channel ordering information in this input,
                 * so don't auto-map from it */
                iu->nb_ch = 0;
                break;
            }
        }
    }

    /* process user-specified maps */
    for (i = 0; i < s->ch_layout.nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        AVFilterLink *inlink;
        AVChannelLayout *ichl;
        ChannelList     *iu;

        if (ch->input < 0)
            continue;

        inlink = ctx->inputs[ch->input];
        ichl   = &inlink->ch_layout;
        iu     = &inputs_unused[ch->input];

        /* get the index for the channels defined by name */
        if (ch->in_channel != AV_CHAN_NONE) {
            ch->in_channel_idx = av_channel_layout_index_from_channel(ichl, ch->in_channel);
            if (ch->in_channel_idx < 0) {
                av_channel_name(inbuf, sizeof(inbuf), ch->in_channel);
                av_log(ctx, AV_LOG_ERROR, "Requested channel %s is not present in "
                       "input stream #%d.\n", inbuf,
                       ch->input);
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

        /* make sure channels specified by index actually exist */
        if (ch->in_channel_idx >= ichl->nb_channels) {
            av_log(ctx, AV_LOG_ERROR, "Requested channel with index %d is not "
                   "present in input stream #%d.\n", ch->in_channel_idx, ch->input);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        channel_list_pop_ch(iu, av_channel_layout_channel_from_index(ichl, ch->in_channel_idx));
    }

    /* guess channel maps when not explicitly defined */
    /* first try unused matching channels */
    for (i = 0; i < s->ch_layout.nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];

        if (ch->input < 0)
            guess_map_matching(ctx, ch, inputs_unused);
    }

    /* if the above failed, try to find _any_ unused input channel */
    for (i = 0; i < s->ch_layout.nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];

        if (ch->input < 0)
            guess_map_any(ctx, ch, inputs_unused);

        if (ch->input < 0) {
            av_channel_name(outbuf, sizeof(outbuf), ch->out_channel);
            av_log(ctx, AV_LOG_ERROR, "Could not find input channel for "
                   "output channel '%s'.\n",
                   outbuf);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (ch->in_channel != AV_CHAN_NONE) {
            ch->in_channel_idx = av_channel_layout_index_from_channel(
                &ctx->inputs[ch->input]->ch_layout, ch->in_channel);
        }

        av_assert0(ch->in_channel_idx >= 0);
    }

    /* print mappings */
    av_log(ctx, AV_LOG_VERBOSE, "mappings: ");
    for (i = 0; i < s->ch_layout.nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        AVFilterLink  *inlink = ctx->inputs[ch->input];
        AVChannelLayout *ichl = &inlink->ch_layout;
        enum AVChannel  in_ch = av_channel_layout_channel_from_index(
                                    ichl, ch->in_channel_idx);

        av_channel_name(inbuf, sizeof(inbuf), in_ch);
        av_channel_name(outbuf, sizeof(outbuf), ch->out_channel);
        av_log(ctx, AV_LOG_VERBOSE, "%d.%s(%d) => %s(%d) ", ch->input,
               inbuf, ch->in_channel_idx,
               outbuf, i);
    }
    av_log(ctx, AV_LOG_VERBOSE, "\n");

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (inputs_unused[i].nb_ch == ctx->inputs[i]->ch_layout.nb_channels)
            av_log(ctx, AV_LOG_WARNING, "No channels are used from input "
                   "stream %d.\n", i);
    }

fail:
    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&inputs_unused[i].ch);
    av_freep(&inputs_unused);
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
        if (!s->input_frames[i]) {
            nb_samples = 0;
            break;
        } else {
            nb_samples = FFMIN(nb_samples, s->input_frames[i]->nb_samples);
        }
    }
    if (!nb_samples)
        goto eof;

    /* setup the output frame */
    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);
    if (s->ch_layout.nb_channels > FF_ARRAY_ELEMS(frame->data)) {
        frame->extended_data = av_calloc(s->ch_layout.nb_channels,
                                          sizeof(*frame->extended_data));
        if (!frame->extended_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* copy the data pointers */
    for (i = 0; i < s->ch_layout.nb_channels; i++) {
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
        frame->extended_buf = av_calloc(frame->nb_extended_buf,
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
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    frame->channel_layout = outlink->channel_layout;
    frame->channels       = outlink->ch_layout.nb_channels;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if ((ret = av_channel_layout_copy(&frame->ch_layout, &outlink->ch_layout)) < 0)
        return ret;
    frame->sample_rate    = outlink->sample_rate;
    frame->format         = outlink->format;
    frame->pts            = s->input_frames[0]->pts;
    frame->linesize[0]    = linesize;
    if (frame->data != frame->extended_data) {
        memcpy(frame->data, frame->extended_data, sizeof(*frame->data) *
               FFMIN(FF_ARRAY_ELEMS(frame->data), s->ch_layout.nb_channels));
    }

    s->eof_pts = frame->pts + av_rescale_q(frame->nb_samples,
                                           av_make_q(1, outlink->sample_rate),
                                           outlink->time_base);
    ret = ff_filter_frame(outlink, frame);

    for (i = 0; i < ctx->nb_inputs; i++)
        av_frame_free(&s->input_frames[i]);

    return ret;

fail:
    av_frame_free(&frame);
    return ret;
eof:
    for (i = 0; i < ctx->nb_inputs; i++) {
        if (ff_outlink_get_status(ctx->inputs[i]) &&
            ff_inlink_queued_samples(ctx->inputs[i]) <= 0 &&
            !s->input_frames[i]) {
            ff_outlink_set_status(outlink, AVERROR_EOF, s->eof_pts);
        }
    }

    return 0;
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
        } else if (ret == 0 && ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, s->eof_pts);
            return 0;
        }

        if (!s->input_frames[0] && ff_outlink_frame_wanted(ctx->outputs[0])) {
            ff_inlink_request_frame(ctx->inputs[0]);
            return 0;
        }
    }

    if (s->input_frames[0])
        nb_samples = s->input_frames[0]->nb_samples;

    for (i = 1; i < ctx->nb_inputs && nb_samples > 0; i++) {
        if (s->input_frames[i])
            continue;
        ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &s->input_frames[i]);
        if (ret < 0) {
            return ret;
        } else if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        }

        if (!s->input_frames[i]) {
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
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
};

const AVFilter ff_af_join = {
    .name           = "join",
    .description    = NULL_IF_CONFIG_SMALL("Join multiple audio streams into "
                                           "multi-channel output."),
    .priv_size      = sizeof(JoinContext),
    .priv_class     = &join_class,
    .init           = join_init,
    .uninit         = join_uninit,
    .activate       = activate,
    .inputs         = NULL,
    FILTER_OUTPUTS(avfilter_af_join_outputs),
    FILTER_QUERY_FUNC(join_query_formats),
    .flags          = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
