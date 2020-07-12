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
#include "filters.h"
#include "internal.h"
#include "video.h"
#include "audio.h"

#define TYPE_ALL 2

typedef struct ConcatContext {
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
    } *in;
} ConcatContext;

#define OFFSET(x) offsetof(ConcatContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM

static const AVOption concat_options[] = {
    { "n", "specify the number of segments", OFFSET(nb_segments),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 1, INT_MAX, V|A|F},
    { "v", "specify the number of video streams",
      OFFSET(nb_streams[AVMEDIA_TYPE_VIDEO]),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, V|F },
    { "a", "specify the number of audio streams",
      OFFSET(nb_streams[AVMEDIA_TYPE_AUDIO]),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, A|F},
    { "unsafe", "enable unsafe mode",
      OFFSET(unsafe),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, V|A|F},
    { NULL }
};

AVFILTER_DEFINE_CLASS(concat);

static int query_formats(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned type, nb_str, idx0 = 0, idx, str, seg;
    AVFilterFormats *formats, *rates = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    int ret;

    for (type = 0; type < TYPE_ALL; type++) {
        nb_str = cat->nb_streams[type];
        for (str = 0; str < nb_str; str++) {
            idx = idx0;

            /* Set the output formats */
            formats = ff_all_formats(type);
            if ((ret = ff_formats_ref(formats, &ctx->outputs[idx]->in_formats)) < 0)
                return ret;

            if (type == AVMEDIA_TYPE_AUDIO) {
                rates = ff_all_samplerates();
                if ((ret = ff_formats_ref(rates, &ctx->outputs[idx]->in_samplerates)) < 0)
                    return ret;
                layouts = ff_all_channel_layouts();
                if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[idx]->in_channel_layouts)) < 0)
                    return ret;
            }

            /* Set the same formats for each corresponding input */
            for (seg = 0; seg < cat->nb_segments; seg++) {
                if ((ret = ff_formats_ref(formats, &ctx->inputs[idx]->out_formats)) < 0)
                    return ret;
                if (type == AVMEDIA_TYPE_AUDIO) {
                    if ((ret = ff_formats_ref(rates, &ctx->inputs[idx]->out_samplerates)) < 0 ||
                        (ret = ff_channel_layouts_ref(layouts, &ctx->inputs[idx]->out_channel_layouts)) < 0)
                        return ret;
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
    outlink->frame_rate          = inlink->frame_rate;

    for (seg = 1; seg < cat->nb_segments; seg++) {
        inlink = ctx->inputs[in_no + seg * ctx->nb_outputs];
        if (outlink->frame_rate.num != inlink->frame_rate.num ||
            outlink->frame_rate.den != inlink->frame_rate.den) {
            av_log(ctx, AV_LOG_VERBOSE,
                    "Video inputs have different frame rates, output will be VFR\n");
            outlink->frame_rate = av_make_q(1, 0);
            break;
        }
    }

    for (seg = 1; seg < cat->nb_segments; seg++) {
        inlink = ctx->inputs[in_no + seg * ctx->nb_outputs];
        if (!outlink->sample_aspect_ratio.num)
            outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
        /* possible enhancement: unsafe mode, do not check */
        if (outlink->w                       != inlink->w                       ||
            outlink->h                       != inlink->h                       ||
            outlink->sample_aspect_ratio.num != inlink->sample_aspect_ratio.num &&
                                                inlink->sample_aspect_ratio.num ||
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

static int push_frame(AVFilterContext *ctx, unsigned in_no, AVFrame *buf)
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
        in->pts += av_rescale_q(buf->nb_samples,
                                av_make_q(1, inlink->sample_rate),
                                outlink->time_base);
    else if (in->nb_frames >= 2)
        /* use mean duration */
        in->pts = av_rescale(in->pts, in->nb_frames, in->nb_frames - 1);

    buf->pts += cat->delta_ts;
    return ff_filter_frame(outlink, buf);
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    AVFilterContext *ctx = inlink->dst;
    unsigned in_no = FF_INLINK_IDX(inlink);
    AVFilterLink *outlink = ctx->outputs[in_no % ctx->nb_outputs];

    return ff_get_video_buffer(outlink, w, h);
}

static AVFrame *get_audio_buffer(AVFilterLink *inlink, int nb_samples)
{
    AVFilterContext *ctx = inlink->dst;
    unsigned in_no = FF_INLINK_IDX(inlink);
    AVFilterLink *outlink = ctx->outputs[in_no % ctx->nb_outputs];

    return ff_get_audio_buffer(outlink, nb_samples);
}

static void close_input(AVFilterContext *ctx, unsigned in_no)
{
    ConcatContext *cat = ctx->priv;

    cat->in[in_no].eof = 1;
    cat->nb_in_active--;
    av_log(ctx, AV_LOG_VERBOSE, "EOF on %s, %d streams left in segment.\n",
           ctx->input_pads[in_no].name, cat->nb_in_active);
}

static void find_next_delta_ts(AVFilterContext *ctx, int64_t *seg_delta)
{
    ConcatContext *cat = ctx->priv;
    unsigned i = cat->cur_idx;
    unsigned imax = i + ctx->nb_outputs;
    int64_t pts;

    pts = cat->in[i++].pts;
    for (; i < imax; i++)
        pts = FFMAX(pts, cat->in[i].pts);
    cat->delta_ts += pts;
    *seg_delta = pts;
}

static int send_silence(AVFilterContext *ctx, unsigned in_no, unsigned out_no,
                        int64_t seg_delta)
{
    ConcatContext *cat = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[out_no];
    int64_t base_pts = cat->in[in_no].pts + cat->delta_ts - seg_delta;
    int64_t nb_samples, sent = 0;
    int frame_nb_samples, ret;
    AVRational rate_tb = { 1, ctx->inputs[in_no]->sample_rate };
    AVFrame *buf;

    if (!rate_tb.den)
        return AVERROR_BUG;
    nb_samples = av_rescale_q(seg_delta - cat->in[in_no].pts,
                              outlink->time_base, rate_tb);
    frame_nb_samples = FFMAX(9600, rate_tb.den / 5); /* arbitrary */
    while (nb_samples) {
        frame_nb_samples = FFMIN(frame_nb_samples, nb_samples);
        buf = ff_get_audio_buffer(outlink, frame_nb_samples);
        if (!buf)
            return AVERROR(ENOMEM);
        av_samples_set_silence(buf->extended_data, 0, frame_nb_samples,
                               outlink->channels, outlink->format);
        buf->pts = base_pts + av_rescale_q(sent, rate_tb, outlink->time_base);
        ret = ff_filter_frame(outlink, buf);
        if (ret < 0)
            return ret;
        sent       += frame_nb_samples;
        nb_samples -= frame_nb_samples;
    }
    return 0;
}

static int flush_segment(AVFilterContext *ctx)
{
    int ret;
    ConcatContext *cat = ctx->priv;
    unsigned str, str_max;
    int64_t seg_delta;

    find_next_delta_ts(ctx, &seg_delta);
    cat->cur_idx += ctx->nb_outputs;
    cat->nb_in_active = ctx->nb_outputs;
    av_log(ctx, AV_LOG_VERBOSE, "Segment finished at pts=%"PRId64"\n",
           cat->delta_ts);

    if (cat->cur_idx < ctx->nb_inputs) {
        /* pad audio streams with silence */
        str = cat->nb_streams[AVMEDIA_TYPE_VIDEO];
        str_max = str + cat->nb_streams[AVMEDIA_TYPE_AUDIO];
        for (; str < str_max; str++) {
            ret = send_silence(ctx, cat->cur_idx - ctx->nb_outputs + str, str,
                               seg_delta);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    unsigned seg, type, str;
    int ret;

    /* create input pads */
    for (seg = 0; seg < cat->nb_segments; seg++) {
        for (type = 0; type < TYPE_ALL; type++) {
            for (str = 0; str < cat->nb_streams[type]; str++) {
                AVFilterPad pad = {
                    .type             = type,
                    .get_video_buffer = get_video_buffer,
                    .get_audio_buffer = get_audio_buffer,
                };
                pad.name = av_asprintf("in%d:%c%d", seg, "va"[type], str);
                if ((ret = ff_insert_inpad(ctx, ctx->nb_inputs, &pad)) < 0) {
                    av_freep(&pad.name);
                    return ret;
                }
            }
        }
    }
    /* create output pads */
    for (type = 0; type < TYPE_ALL; type++) {
        for (str = 0; str < cat->nb_streams[type]; str++) {
            AVFilterPad pad = {
                .type          = type,
                .config_props  = config_output,
            };
            pad.name = av_asprintf("out:%c%d", "va"[type], str);
            if ((ret = ff_insert_outpad(ctx, ctx->nb_outputs, &pad)) < 0) {
                av_freep(&pad.name);
                return ret;
            }
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

    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
    av_freep(&cat->in);
}

static int activate(AVFilterContext *ctx)
{
    ConcatContext *cat = ctx->priv;
    AVFrame *frame;
    unsigned i, j;
    int ret, status;
    int64_t pts;

    /* Forward status back */
    for (i = 0; i < ctx->nb_outputs; i++) {
        status = ff_outlink_get_status(ctx->outputs[i]);
        if (!status)
            continue;
        for (j = i; j < ctx->nb_inputs; j += ctx->nb_outputs) {
            if (!cat->in[j].eof) {
                cat->in[j].eof = 1;
                ff_inlink_set_status(ctx->inputs[j], status);
                return 0;
            }
        }

    }

    /* Forward available frames */
    if (cat->cur_idx < ctx->nb_inputs) {
        for (i = 0; i < ctx->nb_outputs; i++) {
            ret = ff_inlink_consume_frame(ctx->inputs[cat->cur_idx + i], &frame);
            if (ret < 0)
                return ret;
            if (ret) {
                ff_filter_set_ready(ctx, 10);
                return push_frame(ctx, cat->cur_idx + i, frame);
            }
        }
    }

    /* Forward status change */
    if (cat->cur_idx < ctx->nb_inputs) {
        for (i = 0; i < ctx->nb_outputs; i++) {
            ret = ff_inlink_acknowledge_status(ctx->inputs[cat->cur_idx + i], &status, &pts);
            /* TODO use pts */
            if (ret > 0) {
                close_input(ctx, cat->cur_idx + i);
                if (cat->cur_idx + ctx->nb_outputs >= ctx->nb_inputs) {
                    ff_outlink_set_status(ctx->outputs[i], status, pts);
                }
                if (!cat->nb_in_active) {
                    ret = flush_segment(ctx);
                    if (ret < 0)
                        return ret;
                }
                ff_filter_set_ready(ctx, 10);
                return 0;
            }
        }
    }

    ret = FFERROR_NOT_READY;
    for (i = 0; i < ctx->nb_outputs; i++) {
        if (ff_outlink_frame_wanted(ctx->outputs[i])) {
            if (cat->in[cat->cur_idx + i].eof) {
                for (j = 0; j < ctx->nb_outputs; j++)
                    if (!cat->in[cat->cur_idx + j].eof)
                        ff_inlink_request_frame(ctx->inputs[cat->cur_idx + j]);
                return 0;
            } else {
                ff_inlink_request_frame(ctx->inputs[cat->cur_idx + i]);
                ret = 0;
            }
        }
    }

    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "next")) {
        av_log(ctx, AV_LOG_VERBOSE, "Command received: next\n");
        return flush_segment(ctx);
    }

    return ret;
}

AVFilter ff_avf_concat = {
    .name          = "concat",
    .description   = NULL_IF_CONFIG_SMALL("Concatenate audio and video streams."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(ConcatContext),
    .inputs        = NULL,
    .outputs       = NULL,
    .priv_class    = &concat_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .process_command = process_command,
};
