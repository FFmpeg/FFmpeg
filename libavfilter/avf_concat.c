/*
 * Copyright (c) 2012 Nicolas George
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * concat audio-video filter
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#define FF_BUFQUEUE_SIZE 256
#include "bufferqueue.h"
#include "internal.h"
#include "video.h"
#include "audio.h"

#define TYPE_ALL 2

typedef struct {
    const AVClass *class;
    unsigned nb_streams[TYPE_ALL]; /**< number of out streams of each type */
    unsigned nb_segments;
    unsigned cur_idx; /**< index of the first input of current segment */
    int64_t delta_ts; /**< timestamp to add to produce output timestamps */
    unsigned nb_in_active; /**< number of active inputs in current segment */
    unsigned unsafe;
    struct concat_in {
        int64_t pts;
        int64_t nb_frames;
        unsigned eof;
        struct FFBufQueue queue;
    } *in;
} ConcatContext;

#define OFFSET(x) offsetof(ConcatContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM

static const AVOption concat_options[] = {
    { "n", "specify the number of segments", OFFSET(nb_segments),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 2, INT_MAX, V|A|F},
    { "v", "specify the number of video streams",
      OFFSET(nb_streams[AVMEDIA_TYPE_VIDEO]),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, V|F },
    { "a", "specify the number of audio streams",
      OFFSET(nb_streams[AVMEDIA_TYPE_AUDIO]),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, A|F},
    { "unsafe", "enable unsafe mode",
      OFFSET(unsafe),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, A|A|F},
    { 0 }
};

AVFILTER_DEFINE_CLASS(concat);

static int query_formats(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned type, nb_str, idx0 = 0, idx, str, seg;
    AVFilterFormats *formats, *rates = NULL;
    AVFilterChannelLayouts *layouts = NULL;

    for (type = 0; type < TYPE_ALL; type++) {
        nb_str = cat->nb_streams[type];
        for (str = 0; str < nb_str; str++) {
            idx = idx0;

            /* Set the output formats */
            formats = ff_all_formats(type);
            if (!formats)
                return AVERROR(ENOMEM);
            ff_formats_ref(formats, &ctx->outputs[idx]->in_formats);
            if (type == AVMEDIA_TYPE_AUDIO) {
                rates = ff_all_samplerates();
                if (!rates)
                    return AVERROR(ENOMEM);
                ff_formats_ref(rates, &ctx->outputs[idx]->in_samplerates);
                layouts = ff_all_channel_layouts();
                if (!layouts)
                    return AVERROR(ENOMEM);
                ff_channel_layouts_ref(layouts, &ctx->outputs[idx]->in_channel_layouts);
            }

            /* Set the same formats for each corresponding input */
            for (seg = 0; seg < cat->nb_segments; seg++) {
                ff_formats_ref(formats, &ctx->inputs[idx]->out_formats);
                if (type == AVMEDIA_TYPE_AUDIO) {
                    ff_formats_ref(rates, &ctx->inputs[idx]->out_samplerates);
                    ff_channel_layouts_ref(layouts, &ctx->inputs[idx]->out_channel_layouts);
                }
                idx += ctx->nb_outputs;
            }

            idx0++;
        }
    }
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ConcatContext *cat   = ctx->priv;
    unsigned out_no = FF_OUTLINK_IDX(outlink);
    unsigned in_no  = out_no, seg;
    AVFilterLink *inlink = ctx->inputs[in_no];

    /* enhancement: find a common one */
    outlink->time_base           = AV_TIME_BASE_Q;
    outlink->w                   = inlink->w;
    outlink->h                   = inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->format              = inlink->format;
    for (seg = 1; seg < cat->nb_segments; seg++) {
        inlink = ctx->inputs[in_no += ctx->nb_outputs];
        /* possible enhancement: unsafe mode, do not check */
        if (outlink->w                       != inlink->w                       ||
            outlink->h                       != inlink->h                       ||
            outlink->sample_aspect_ratio.num != inlink->sample_aspect_ratio.num ||
            outlink->sample_aspect_ratio.den != inlink->sample_aspect_ratio.den) {
            av_log(ctx, AV_LOG_ERROR, "Input link %s parameters "
                   "(size %dx%d, SAR %d:%d) do not match the corresponding "
                   "output link %s parameters (%dx%d, SAR %d:%d)\n",
                   ctx->input_pads[in_no].name, inlink->w, inlink->h,
                   inlink->sample_aspect_ratio.num,
                   inlink->sample_aspect_ratio.den,
                   ctx->input_pads[out_no].name, outlink->w, outlink->h,
                   outlink->sample_aspect_ratio.num,
                   outlink->sample_aspect_ratio.den);
            if (!cat->unsafe)
                return AVERROR(EINVAL);
        }
    }

    return 0;
}

static void push_frame(AVFilterContext *ctx, unsigned in_no,
                       AVFilterBufferRef *buf)
{
    ConcatContext *cat = ctx->priv;
    unsigned out_no = in_no % ctx->nb_outputs;
    AVFilterLink * inlink = ctx-> inputs[ in_no];
    AVFilterLink *outlink = ctx->outputs[out_no];
    struct concat_in *in = &cat->in[in_no];

    buf->pts = av_rescale_q(buf->pts, inlink->time_base, outlink->time_base);
    in->pts = buf->pts;
    in->nb_frames++;
    /* add duration to input PTS */
    if (inlink->sample_rate)
        /* use number of audio samples */
        in->pts += av_rescale_q(buf->audio->nb_samples,
                                (AVRational){ 1, inlink->sample_rate },
                                outlink->time_base);
    else if (in->nb_frames >= 2)
        /* use mean duration */
        in->pts = av_rescale(in->pts, in->nb_frames, in->nb_frames - 1);

    buf->pts += cat->delta_ts;
    ff_filter_frame(outlink, buf);
}

static void process_frame(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext *ctx  = inlink->dst;
    ConcatContext *cat    = ctx->priv;
    unsigned in_no = FF_INLINK_IDX(inlink);

    if (in_no < cat->cur_idx) {
        av_log(ctx, AV_LOG_ERROR, "Frame after EOF on input %s\n",
               ctx->input_pads[in_no].name);
        avfilter_unref_buffer(buf);
    } else if (in_no >= cat->cur_idx + ctx->nb_outputs) {
        ff_bufqueue_add(ctx, &cat->in[in_no].queue, buf);
    } else {
        push_frame(ctx, in_no, buf);
    }
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms,
                                           int w, int h)
{
    AVFilterContext *ctx = inlink->dst;
    unsigned in_no = FF_INLINK_IDX(inlink);
    AVFilterLink *outlink = ctx->outputs[in_no % ctx->nb_outputs];

    return ff_get_video_buffer(outlink, perms, w, h);
}

static AVFilterBufferRef *get_audio_buffer(AVFilterLink *inlink, int perms,
                                           int nb_samples)
{
    AVFilterContext *ctx = inlink->dst;
    unsigned in_no = FF_INLINK_IDX(inlink);
    AVFilterLink *outlink = ctx->outputs[in_no % ctx->nb_outputs];

    return ff_get_audio_buffer(outlink, perms, nb_samples);
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    process_frame(inlink, buf);
    return 0; /* enhancement: handle error return */
}

static void close_input(AVFilterContext *ctx, unsigned in_no)
{
    ConcatContext *cat = ctx->priv;

    cat->in[in_no].eof = 1;
    cat->nb_in_active--;
    av_log(ctx, AV_LOG_VERBOSE, "EOF on %s, %d streams left in segment.\n",
           ctx->input_pads[in_no].name, cat->nb_in_active);
}

static void find_next_delta_ts(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned i = cat->cur_idx;
    unsigned imax = i + ctx->nb_outputs;
    int64_t pts;

    pts = cat->in[i++].pts;
    for (; i < imax; i++)
        pts = FFMAX(pts, cat->in[i].pts);
    cat->delta_ts += pts;
}

static void send_silence(AVFilterContext *ctx, unsigned in_no, unsigned out_no)
{
    ConcatContext *cat = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[out_no];
    int64_t base_pts = cat->in[in_no].pts + cat->delta_ts;
    int64_t nb_samples, sent = 0;
    int frame_nb_samples;
    AVRational rate_tb = { 1, ctx->inputs[in_no]->sample_rate };
    AVFilterBufferRef *buf;
    int nb_channels = av_get_channel_layout_nb_channels(outlink->channel_layout);

    if (!rate_tb.den)
        return;
    nb_samples = av_rescale_q(cat->delta_ts - base_pts,
                              outlink->time_base, rate_tb);
    frame_nb_samples = FFMAX(9600, rate_tb.den / 5); /* arbitrary */
    while (nb_samples) {
        frame_nb_samples = FFMIN(frame_nb_samples, nb_samples);
        buf = ff_get_audio_buffer(outlink, AV_PERM_WRITE, frame_nb_samples);
        if (!buf)
            return;
        av_samples_set_silence(buf->extended_data, 0, frame_nb_samples,
                               nb_channels, outlink->format);
        buf->pts = base_pts + av_rescale_q(sent, rate_tb, outlink->time_base);
        ff_filter_frame(outlink, buf);
        sent       += frame_nb_samples;
        nb_samples -= frame_nb_samples;
    }
}

static void flush_segment(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned str, str_max;

    find_next_delta_ts(ctx);
    cat->cur_idx += ctx->nb_outputs;
    cat->nb_in_active = ctx->nb_outputs;
    av_log(ctx, AV_LOG_VERBOSE, "Segment finished at pts=%"PRId64"\n",
           cat->delta_ts);

    if (cat->cur_idx < ctx->nb_inputs) {
        /* pad audio streams with silence */
        str = cat->nb_streams[AVMEDIA_TYPE_VIDEO];
        str_max = str + cat->nb_streams[AVMEDIA_TYPE_AUDIO];
        for (; str < str_max; str++)
            send_silence(ctx, cat->cur_idx - ctx->nb_outputs + str, str);
        /* flush queued buffers */
        /* possible enhancement: flush in PTS order */
        str_max = cat->cur_idx + ctx->nb_outputs;
        for (str = cat->cur_idx; str < str_max; str++)
            while (cat->in[str].queue.available)
                push_frame(ctx, str, ff_bufqueue_get(&cat->in[str].queue));
    }
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ConcatContext *cat   = ctx->priv;
    unsigned out_no = FF_OUTLINK_IDX(outlink);
    unsigned in_no  = out_no + cat->cur_idx;
    unsigned str, str_max;
    int ret;

    while (1) {
        if (in_no >= ctx->nb_inputs)
            return AVERROR_EOF;
        if (!cat->in[in_no].eof) {
            ret = ff_request_frame(ctx->inputs[in_no]);
            if (ret != AVERROR_EOF)
                return ret;
            close_input(ctx, in_no);
        }
        /* cycle on all inputs to finish the segment */
        /* possible enhancement: request in PTS order */
        str_max = cat->cur_idx + ctx->nb_outputs - 1;
        for (str = cat->cur_idx; cat->nb_in_active;
             str = str == str_max ? cat->cur_idx : str + 1) {
            if (cat->in[str].eof)
                continue;
            ret = ff_request_frame(ctx->inputs[str]);
            if (ret == AVERROR_EOF)
                close_input(ctx, str);
            else if (ret < 0)
                return ret;
        }
        flush_segment(ctx);
        in_no += ctx->nb_outputs;
    }
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ConcatContext *cat = ctx->priv;
    int ret;
    unsigned seg, type, str;

    cat->class = &concat_class;
    av_opt_set_defaults(cat);
    ret = av_set_options_string(cat, args, "=", ":");
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options: '%s'\n", args);
        return ret;
    }

    /* create input pads */
    for (seg = 0; seg < cat->nb_segments; seg++) {
        for (type = 0; type < TYPE_ALL; type++) {
            for (str = 0; str < cat->nb_streams[type]; str++) {
                AVFilterPad pad = {
                    .type             = type,
                    .min_perms        = AV_PERM_READ | AV_PERM_PRESERVE,
                    .get_video_buffer = get_video_buffer,
                    .get_audio_buffer = get_audio_buffer,
                    .filter_frame     = filter_frame,
                };
                pad.name = av_asprintf("in%d:%c%d", seg, "va"[type], str);
                ff_insert_inpad(ctx, ctx->nb_inputs, &pad);
            }
        }
    }
    /* create output pads */
    for (type = 0; type < TYPE_ALL; type++) {
        for (str = 0; str < cat->nb_streams[type]; str++) {
            AVFilterPad pad = {
                .type          = type,
                .config_props  = config_output,
                .request_frame = request_frame,
            };
            pad.name = av_asprintf("out:%c%d", "va"[type], str);
            ff_insert_outpad(ctx, ctx->nb_outputs, &pad);
        }
    }

    cat->in = av_calloc(ctx->nb_inputs, sizeof(*cat->in));
    if (!cat->in)
        return AVERROR(ENOMEM);
    cat->nb_in_active = ctx->nb_outputs;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        av_freep(&ctx->input_pads[i].name);
        ff_bufqueue_discard_all(&cat->in[i].queue);
    }
    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
    av_free(cat->in);
}

AVFilter avfilter_avf_concat = {
    .name          = "concat",
    .description   = NULL_IF_CONFIG_SMALL("Concatenate audio and video streams."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ConcatContext),
    .inputs        = NULL,
    .outputs       = NULL,
    .priv_class    = &concat_class,
};
