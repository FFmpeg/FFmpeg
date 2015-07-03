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
 * Bauer stereo-to-binaural filter
 */

#include <bs2b.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct Bs2bContext {
    const AVClass *class;

    int profile;
    int fcut;
    int feed;

    t_bs2bdp bs2bp;

    void (*filter)(t_bs2bdp bs2bdp, uint8_t *sample, int n);
} Bs2bContext;

#define OFFSET(x) offsetof(Bs2bContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM

static const AVOption bs2b_options[] = {
    { "profile", "Apply a pre-defined crossfeed level",
            OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = BS2B_DEFAULT_CLEVEL }, 0, INT_MAX, A, "profile" },
        { "default", "default profile", 0, AV_OPT_TYPE_CONST, { .i64 = BS2B_DEFAULT_CLEVEL }, 0, 0, A, "profile" },
        { "cmoy",    "Chu Moy circuit", 0, AV_OPT_TYPE_CONST, { .i64 = BS2B_CMOY_CLEVEL    }, 0, 0, A, "profile" },
        { "jmeier",  "Jan Meier circuit", 0, AV_OPT_TYPE_CONST, { .i64 = BS2B_JMEIER_CLEVEL  }, 0, 0, A, "profile" },
    { "fcut", "Set cut frequency (in Hz)",
            OFFSET(fcut), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BS2B_MAXFCUT, A },
    { "feed", "Set feed level (in Hz)",
            OFFSET(feed), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, BS2B_MAXFEED, A },
    { NULL },
};

AVFILTER_DEFINE_CLASS(bs2b);

static av_cold int init(AVFilterContext *ctx)
{
    Bs2bContext *bs2b = ctx->priv;

    if (!(bs2b->bs2bp = bs2b_open()))
        return AVERROR(ENOMEM);

    bs2b_set_level(bs2b->bs2bp, bs2b->profile);

    if (bs2b->fcut)
        bs2b_set_level_fcut(bs2b->bs2bp, bs2b->fcut);

    if (bs2b->feed)
        bs2b_set_level_feed(bs2b->bs2bp, bs2b->feed);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    Bs2bContext *bs2b = ctx->priv;

    if (bs2b->bs2bp)
        bs2b_close(bs2b->bs2bp);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;

    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_U8,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE,
    };
    int ret;

    if (ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO) != 0)
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

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    int ret;
    AVFrame *out_frame;

    Bs2bContext     *bs2b = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];

    if (av_frame_is_writable(frame)) {
        out_frame = frame;
    } else {
        out_frame = ff_get_audio_buffer(inlink, frame->nb_samples);
        if (!out_frame)
            return AVERROR(ENOMEM);
        av_frame_copy(out_frame, frame);
        ret = av_frame_copy_props(out_frame, frame);
        if (ret < 0) {
            av_frame_free(&out_frame);
            av_frame_free(&frame);
            return ret;
        }
    }

    bs2b->filter(bs2b->bs2bp, out_frame->extended_data[0], out_frame->nb_samples);

    if (frame != out_frame)
        av_frame_free(&frame);

    return ff_filter_frame(outlink, out_frame);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    Bs2bContext    *bs2b = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    int srate = inlink->sample_rate;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_U8:
        bs2b->filter = bs2b_cross_feed_u8;
        break;
    case AV_SAMPLE_FMT_S16:
        bs2b->filter = (void*)bs2b_cross_feed_s16;
        break;
    case AV_SAMPLE_FMT_S32:
        bs2b->filter = (void*)bs2b_cross_feed_s32;
        break;
    case AV_SAMPLE_FMT_FLT:
        bs2b->filter = (void*)bs2b_cross_feed_f;
        break;
    case AV_SAMPLE_FMT_DBL:
        bs2b->filter = (void*)bs2b_cross_feed_d;
        break;
    default:
        return AVERROR_BUG;
    }

    if ((srate < BS2B_MINSRATE) || (srate > BS2B_MAXSRATE))
        return AVERROR(ENOSYS);

    bs2b_set_srate(bs2b->bs2bp, srate);

    return 0;
}

static const AVFilterPad bs2b_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad bs2b_outputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .config_props   = config_output,
    },
    { NULL }
};

AVFilter ff_af_bs2b = {
    .name           = "bs2b",
    .description    = NULL_IF_CONFIG_SMALL("Bauer stereo-to-binaural filter."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(Bs2bContext),
    .priv_class     = &bs2b_class,
    .init           = init,
    .uninit         = uninit,
    .inputs         = bs2b_inputs,
    .outputs        = bs2b_outputs,
};
