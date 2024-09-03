/*
 * Copyright (c) 2022 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

#include <float.h>

typedef struct AudioDialogueEnhancementContext {
    const AVClass *class;

    double original, enhance, voice;

    int fft_size;
    int overlap;

    void *window;
    float *window_float;
    double *window_double;
    float prev_vad_float;
    double prev_vad_double;

    AVFrame *in;
    AVFrame *in_frame;
    AVFrame *out_dist_frame;
    AVFrame *windowed_frame;
    AVFrame *windowed_out;
    AVFrame *windowed_prev;
    AVFrame *center_frame;

    int (*de_stereo)(AVFilterContext *ctx, AVFrame *out);

    AVTXContext *tx_ctx[2], *itx_ctx;
    av_tx_fn tx_fn, itx_fn;
} AudioDialogueEnhanceContext;

#define OFFSET(x) offsetof(AudioDialogueEnhanceContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption dialoguenhance_options[] = {
    { "original", "set original center factor", OFFSET(original), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "enhance",  "set dialogue enhance factor",OFFSET(enhance),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 3, FLAGS },
    { "voice",    "set voice detection factor", OFFSET(voice),    AV_OPT_TYPE_DOUBLE, {.dbl=2}, 2,32, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(dialoguenhance);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVSampleFormat formats[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE,
    };

    AVFilterChannelLayouts *in_layout = NULL, *out_layout = NULL;
    int ret;

    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, formats);
    if (ret < 0)
        return ret;

    if ((ret = ff_add_channel_layout         (&in_layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_channel_layouts_ref(in_layout, &cfg_in[0]->channel_layouts)) < 0 ||
        (ret = ff_add_channel_layout         (&out_layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_SURROUND)) < 0 ||
        (ret = ff_channel_layouts_ref(out_layout, &cfg_out[0]->channel_layouts)) < 0)
        return ret;

    return 0;
}

#define DEPTH 32
#include "dialoguenhance_template.c"

#undef DEPTH
#define DEPTH 64
#include "dialoguenhance_template.c"

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDialogueEnhanceContext *s = ctx->priv;
    int ret;

    s->fft_size = inlink->sample_rate > 100000 ? 8192 : inlink->sample_rate > 50000 ? 4096 : 2048;
    s->overlap = s->fft_size / 4;

    s->in_frame       = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    s->center_frame   = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    s->out_dist_frame = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    s->windowed_frame = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    s->windowed_out   = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    s->windowed_prev  = ff_get_audio_buffer(inlink, (s->fft_size + 2) * 2);
    if (!s->in_frame || !s->windowed_out || !s->windowed_prev ||
        !s->out_dist_frame || !s->windowed_frame || !s->center_frame)
        return AVERROR(ENOMEM);

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP:
        s->de_stereo = de_stereo_float;
        ret = de_tx_init_float(ctx);
        break;
    case AV_SAMPLE_FMT_DBLP:
        s->de_stereo = de_stereo_double;
        ret = de_tx_init_double(ctx);
        break;
    }

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDialogueEnhanceContext *s = ctx->priv;
    AVFrame *out;
    int ret;

    out = ff_get_audio_buffer(outlink, s->overlap);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->in = in;
    s->de_stereo(ctx, out);

    av_frame_copy_props(out, in);
    out->nb_samples = in->nb_samples;
    ret = ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    s->in = NULL;
    return ret < 0 ? ret : 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDialogueEnhanceContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->overlap, s->overlap, &in);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        return filter_frame(inlink, in);
    } else if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    } else {
        if (ff_inlink_queued_samples(inlink) >= s->overlap) {
            ff_filter_set_ready(ctx, 10);
        } else if (ff_outlink_frame_wanted(outlink)) {
            ff_inlink_request_frame(inlink);
        }
        return 0;
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioDialogueEnhanceContext *s = ctx->priv;

    av_freep(&s->window);

    av_frame_free(&s->in_frame);
    av_frame_free(&s->center_frame);
    av_frame_free(&s->out_dist_frame);
    av_frame_free(&s->windowed_frame);
    av_frame_free(&s->windowed_out);
    av_frame_free(&s->windowed_prev);

    av_tx_uninit(&s->tx_ctx[0]);
    av_tx_uninit(&s->tx_ctx[1]);
    av_tx_uninit(&s->itx_ctx);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_dialoguenhance = {
    .name            = "dialoguenhance",
    .description     = NULL_IF_CONFIG_SMALL("Audio Dialogue Enhancement."),
    .priv_size       = sizeof(AudioDialogueEnhanceContext),
    .priv_class      = &dialoguenhance_class,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .activate        = activate,
    .process_command = ff_filter_process_command,
};
