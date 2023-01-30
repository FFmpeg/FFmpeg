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
 * deinterlace video filter - QSV
 */

#include <mfxvideo.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavfilter/qsvvpp.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define MFX_IMPL_VIA_MASK(impl) (0x0f00 & (impl))

typedef struct QSVDeintContext {
    QSVVPPContext qsv;

    mfxExtVPPDeinterlacing deint_conf;

    /* option for Deinterlacing algorithm to be used */
    int mode;
} QSVDeintContext;

static av_cold void qsvdeint_uninit(AVFilterContext *ctx)
{
    ff_qsvvpp_close(ctx);
}

static int qsvdeint_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    QSVDeintContext   *s = ctx->priv;
    QSVVPPParam    param = { NULL };
    mfxExtBuffer *ext_buf[1];
    enum AVPixelFormat in_format;

    qsvdeint_uninit(ctx);

    outlink->w          = inlink->w;
    outlink->h          = inlink->h;
    outlink->frame_rate = av_mul_q(inlink->frame_rate,
                                   (AVRational){ 2, 1 });
    outlink->time_base  = av_mul_q(inlink->time_base,
                                   (AVRational){ 1, 2 });

    if (inlink->format == AV_PIX_FMT_QSV) {
        if (!inlink->hw_frames_ctx || !inlink->hw_frames_ctx->data)
            return AVERROR(EINVAL);
        else
            in_format = ((AVHWFramesContext*)inlink->hw_frames_ctx->data)->sw_format;
    } else
        in_format = inlink->format;

    param.out_sw_format = in_format;
    param.ext_buf       = ext_buf;

    memset(&s->deint_conf, 0, sizeof(mfxExtVPPDeinterlacing));
    s->deint_conf.Header.BufferId      = MFX_EXTBUFF_VPP_DEINTERLACING;
    s->deint_conf.Header.BufferSz      = sizeof(s->deint_conf);
    s->deint_conf.Mode                 = s->mode;
    param.ext_buf[param.num_ext_buf++] = (mfxExtBuffer*)&s->deint_conf;

    return ff_qsvvpp_init(ctx, &param);
}

static int qsvdeint_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext  *ctx = link->dst;
    QSVVPPContext    *qsv = ctx->priv;
    int               ret = 0;

    ret = ff_qsvvpp_filter_frame(qsv, link, in);
    av_frame_free(&in);

    return ret;
}

static int qsvdeint_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;

    return ff_request_frame(ctx->inputs[0]);
}

#define OFFSET(x) offsetof(QSVDeintContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "mode", "set deinterlace mode", OFFSET(mode),   AV_OPT_TYPE_INT, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { "bob",   "bob algorithm",                  0, AV_OPT_TYPE_CONST,      {.i64 = MFX_DEINTERLACING_BOB}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { "advanced", "Motion adaptive algorithm",   0, AV_OPT_TYPE_CONST, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { NULL },
};

static const AVClass qsvdeint_class = {
    .class_name = "deinterlace_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad qsvdeint_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = qsvdeint_filter_frame,
        .get_buffer.video = ff_qsvvpp_get_video_buffer,
    },
};

static const AVFilterPad qsvdeint_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = qsvdeint_config_props,
        .request_frame = qsvdeint_request_frame,
    },
};

const AVFilter ff_vf_deinterlace_qsv = {
    .name      = "deinterlace_qsv",
    .description = NULL_IF_CONFIG_SMALL("QuickSync video deinterlacing"),

    .uninit        = qsvdeint_uninit,

    .priv_size = sizeof(QSVDeintContext),
    .priv_class = &qsvdeint_class,

    FILTER_INPUTS(qsvdeint_inputs),
    FILTER_OUTPUTS(qsvdeint_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_QSV),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
