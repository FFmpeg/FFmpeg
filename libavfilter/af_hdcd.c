/*
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
 * HDCD decoding filter, using libhdcd
 */

#include <hdcd/hdcd_simple.h>

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct HDCDContext {
    const AVClass *class;

    hdcd_simple *shdcd;

    /* AVOption members */
    /** analyze mode replaces the audio with a solid tone and adjusts
     *  the amplitude to signal some specific aspect of the decoding
     *  process. See docs or HDCD_ANA_* defines. */
    int analyze_mode;
    /* end AVOption members */
} HDCDContext;

#define OFFSET(x) offsetof(HDCDContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define HDCD_ANA_MAX 6
static const AVOption hdcd_options[] = {
    { "analyze_mode",  "Replace audio with solid tone and signal some processing aspect in the amplitude.",
      OFFSET(analyze_mode), AV_OPT_TYPE_INT, { .i64=HDCD_ANA_OFF }, 0, HDCD_ANA_MAX, A, "analyze_mode"},
    { "off",  HDCD_ANA_OFF_DESC,  0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_OFF},  0, 0, A, "analyze_mode" },
    { "lle",  HDCD_ANA_LLE_DESC,  0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_LLE},  0, 0, A, "analyze_mode" },
    { "pe",   HDCD_ANA_PE_DESC,   0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_PE},   0, 0, A, "analyze_mode" },
    { "cdt",  HDCD_ANA_CDT_DESC,  0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_CDT},  0, 0, A, "analyze_mode" },
    { "tgm",  HDCD_ANA_TGM_DESC,  0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_TGM},  0, 0, A, "analyze_mode" },
    { "pel",  HDCD_ANA_PEL_DESC,  0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_PEL},  0, 0, A, "analyze_mode" },
    { "ltgm", HDCD_ANA_LTGM_DESC, 0, AV_OPT_TYPE_CONST, { .i64 = HDCD_ANA_LTGM}, 0, 0, A, "analyze_mode" },
    { NULL }
};

static const AVClass hdcd_class = {
    .class_name = "HDCD filter",
    .item_name  = av_default_item_name,
    .option     = hdcd_options,
    .version    = LIBAVFILTER_VERSION_INT,
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    HDCDContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const int16_t *in_data;
    int32_t *out_data;
    int n, result;
    int channel_count = av_get_channel_layout_nb_channels(in->channel_layout);

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    result = av_frame_copy_props(out, in);
    if (result) {
        av_frame_free(&out);
        av_frame_free(&in);
        return result;
    }

    in_data  = (int16_t *)in->data[0];
    out_data = (int32_t *)out->data[0];
    for (n = 0; n < in->nb_samples * channel_count; n++)
        out_data[n] = in_data[n];

    hdcd_process(s->shdcd, out_data, in->nb_samples);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *in_formats, *out_formats, *sample_rates = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    static const enum AVSampleFormat sample_fmts_in[] = {
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_NONE
    };
    static const enum AVSampleFormat sample_fmts_out[] = {
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_NONE
    };

    ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO);

    ff_set_common_channel_layouts(ctx, layouts);

    in_formats  = ff_make_format_list(sample_fmts_in);
    out_formats = ff_make_format_list(sample_fmts_out);
    if (!in_formats || !out_formats)
        return AVERROR(ENOMEM);

    ff_formats_ref(in_formats, &inlink->out_formats);
    ff_formats_ref(out_formats, &outlink->in_formats);

    ff_add_format(&sample_rates, 44100);
    ff_set_common_samplerates(ctx, sample_rates);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HDCDContext *s = ctx->priv;
    char detect_str[256] = "";

    /* log the HDCD decode information */
    hdcd_detect_str(s->shdcd, detect_str, sizeof(detect_str));
    av_log(ctx, AV_LOG_INFO, "%s\n", detect_str);

    hdcd_free(s->shdcd);
}

/** callback for error logging */
static void af_hdcd_log(const void *priv, const char *fmt, va_list args)
{
    av_vlog((AVFilterContext *)priv, AV_LOG_VERBOSE, fmt, args);
}

static av_cold int init(AVFilterContext *ctx)
{
    HDCDContext *s = ctx->priv;

    s->shdcd = hdcd_new();
    hdcd_logger_attach(s->shdcd, af_hdcd_log, ctx);

    if (s->analyze_mode)
        hdcd_analyze_mode(s->shdcd, s->analyze_mode);
    av_log(ctx, AV_LOG_VERBOSE, "Analyze mode: [%d] %s\n",
           s->analyze_mode, hdcd_str_analyze_mode_desc(s->analyze_mode));

    return 0;
}

static const AVFilterPad avfilter_af_hdcd_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_hdcd_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_hdcd = {
    .name          = "hdcd",
    .description   = NULL_IF_CONFIG_SMALL("Apply High Definition Compatible Digital (HDCD) decoding."),
    .priv_size     = sizeof(HDCDContext),
    .priv_class    = &hdcd_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_af_hdcd_inputs,
    .outputs       = avfilter_af_hdcd_outputs,
};
