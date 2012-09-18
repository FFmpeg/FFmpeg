/*
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
 * Audio join filter
 *
 * Join multiple audio inputs as different channels in
 * a single output
 */

#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
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
    AVFilterBufferRef **input_frames;

    /**
     *  Temporary storage for data pointers, for assembling the output buffer.
     */
    uint8_t **data;
} JoinContext;

/**
 * To avoid copying the data from input buffers, this filter creates
 * a custom output buffer that stores references to all inputs and
 * unrefs them on free.
 */
typedef struct JoinBufferPriv {
    AVFilterBufferRef **in_buffers;
    int              nb_in_buffers;
} JoinBufferPriv;

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
    { NULL },
};

static const AVClass join_class = {
    .class_name = "join filter",
    .item_name  = av_default_item_name,
    .option     = join_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int filter_samples(AVFilterLink *link, AVFilterBufferRef *buf)
{
    AVFilterContext *ctx = link->dst;
    JoinContext       *s = ctx->priv;
    int i;

    for (i = 0; i < ctx->nb_inputs; i++)
        if (link == ctx->inputs[i])
            break;
    av_assert0(i < ctx->nb_inputs);
    av_assert0(!s->input_frames[i]);
    s->input_frames[i] = buf;

    return 0;
}

static int parse_maps(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    char *cur      = s->map;

    while (cur && *cur) {
        char *sep, *next, *p;
        uint64_t in_channel = 0, out_channel = 0;
        int input_idx, out_ch_idx, in_ch_idx;

        next = strchr(cur, ',');
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

static int join_init(AVFilterContext *ctx, const char *args)
{
    JoinContext *s = ctx->priv;
    int ret, i;

    s->class = &join_class;
    av_opt_set_defaults(s);
    if ((ret = av_set_options_string(s, args, "=", ":")) < 0)
        return ret;

    if (!(s->channel_layout = av_get_channel_layout(s->channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout '%s'.\n",
               s->channel_layout_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    s->nb_channels  = av_get_channel_layout_nb_channels(s->channel_layout);
    s->channels     = av_mallocz(sizeof(*s->channels) * s->nb_channels);
    s->data         = av_mallocz(sizeof(*s->data)     * s->nb_channels);
    s->input_frames = av_mallocz(sizeof(*s->input_frames) * s->inputs);
    if (!s->channels || !s->data || !s->input_frames) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < s->nb_channels; i++) {
        s->channels[i].out_channel = av_channel_layout_extract_channel(s->channel_layout, i);
        s->channels[i].input       = -1;
    }

    if ((ret = parse_maps(ctx)) < 0)
        goto fail;

    for (i = 0; i < s->inputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "input%d", i);
        pad.type           = AVMEDIA_TYPE_AUDIO;
        pad.name           = av_strdup(name);
        pad.filter_samples = filter_samples;

        pad.needs_fifo = 1;

        ff_insert_inpad(ctx, i, &pad);
    }

fail:
    av_opt_free(s);
    return ret;
}

static void join_uninit(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        av_freep(&ctx->input_pads[i].name);
        avfilter_unref_bufferp(&s->input_frames[i]);
    }

    av_freep(&s->channels);
    av_freep(&s->data);
    av_freep(&s->input_frames);
}

static int join_query_formats(AVFilterContext *ctx)
{
    JoinContext *s = ctx->priv;
    AVFilterChannelLayouts *layouts = NULL;
    int i;

    ff_add_channel_layout(&layouts, s->channel_layout);
    ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);

    for (i = 0; i < ctx->nb_inputs; i++)
        ff_channel_layouts_ref(ff_all_channel_layouts(),
                               &ctx->inputs[i]->out_channel_layouts);

    ff_set_common_formats    (ctx, ff_planar_sample_fmts());
    ff_set_common_samplerates(ctx, ff_all_samplerates());

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
    if (!(inputs = av_mallocz(sizeof(*inputs) * ctx->nb_inputs)))
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

static void join_free_buffer(AVFilterBuffer *buf)
{
    JoinBufferPriv *priv = buf->priv;

    if (priv) {
        int i;

        for (i = 0; i < priv->nb_in_buffers; i++)
            avfilter_unref_bufferp(&priv->in_buffers[i]);

        av_freep(&priv->in_buffers);
        av_freep(&buf->priv);
    }

    if (buf->extended_data != buf->data)
        av_freep(&buf->extended_data);
    av_freep(&buf);
}

static int join_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    JoinContext *s       = ctx->priv;
    AVFilterBufferRef *buf;
    JoinBufferPriv *priv;
    int linesize   = INT_MAX;
    int perms      = ~0;
    int nb_samples = 0;
    int i, j, ret;

    /* get a frame on each input */
    for (i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        if (!s->input_frames[i] &&
            (ret = ff_request_frame(inlink)) < 0)
            return ret;

        /* request the same number of samples on all inputs */
        if (i == 0) {
            nb_samples = s->input_frames[0]->audio->nb_samples;

            for (j = 1; !i && j < ctx->nb_inputs; j++)
                ctx->inputs[j]->request_samples = nb_samples;
        }
    }

    for (i = 0; i < s->nb_channels; i++) {
        ChannelMap *ch = &s->channels[i];
        AVFilterBufferRef *cur_buf = s->input_frames[ch->input];

        s->data[i] = cur_buf->extended_data[ch->in_channel_idx];
        linesize   = FFMIN(linesize, cur_buf->linesize[0]);
        perms     &= cur_buf->perms;
    }

    av_assert0(nb_samples > 0);
    buf = avfilter_get_audio_buffer_ref_from_arrays(s->data, linesize, perms,
                                                    nb_samples, outlink->format,
                                                    outlink->channel_layout);
    if (!buf)
        return AVERROR(ENOMEM);

    buf->buf->free = join_free_buffer;
    buf->pts       = s->input_frames[0]->pts;

    if (!(priv = av_mallocz(sizeof(*priv))))
        goto fail;
    if (!(priv->in_buffers = av_mallocz(sizeof(*priv->in_buffers) * ctx->nb_inputs)))
        goto fail;

    for (i = 0; i < ctx->nb_inputs; i++)
        priv->in_buffers[i] = s->input_frames[i];
    priv->nb_in_buffers = ctx->nb_inputs;
    buf->buf->priv      = priv;

    ret = ff_filter_samples(outlink, buf);

    memset(s->input_frames, 0, sizeof(*s->input_frames) * ctx->nb_inputs);

    return ret;

fail:
    avfilter_unref_buffer(buf);
    if (priv)
        av_freep(&priv->in_buffers);
    av_freep(&priv);
    return AVERROR(ENOMEM);
}

AVFilter avfilter_af_join = {
    .name           = "join",
    .description    = NULL_IF_CONFIG_SMALL("Join multiple audio streams into "
                                           "multi-channel output"),
    .priv_size      = sizeof(JoinContext),

    .init           = join_init,
    .uninit         = join_uninit,
    .query_formats  = join_query_formats,

    .inputs  = NULL,
    .outputs = (const AVFilterPad[]){{ .name          = "default",
                                       .type          = AVMEDIA_TYPE_AUDIO,
                                       .config_props  = join_config_output,
                                       .request_frame = join_request_frame, },
                                     { NULL }},
    .priv_class = &join_class,
};
