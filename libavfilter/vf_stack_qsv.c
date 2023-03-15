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
 * Hardware accelerated hstack, vstack and xstack filters based on Intel Quick Sync Video VPP
 */

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"

#include "internal.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

#include "framesync.h"
#include "qsvvpp.h"

#define HSTACK_NAME             "hstack_qsv"
#define VSTACK_NAME             "vstack_qsv"
#define XSTACK_NAME             "xstack_qsv"
#define HWContext               QSVVPPContext
#define StackHWContext          StackQSVContext
#include "stack_internal.h"

typedef struct StackQSVContext {
    StackBaseContext base;

    QSVVPPParam qsv_param;
    mfxExtVPPComposite comp_conf;
} StackQSVContext;

static void rgb2yuv(float r, float g, float b, int *y, int *u, int *v, int depth)
{
    *y = ((0.21260*219.0/255.0) * r + (0.71520*219.0/255.0) * g +
         (0.07220*219.0/255.0) * b) * ((1 << depth) - 1);
    *u = (-(0.11457*224.0/255.0) * r - (0.38543*224.0/255.0) * g +
         (0.50000*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
    *v = ((0.50000*224.0/255.0) * r - (0.45415*224.0/255.0) * g -
         (0.04585*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    QSVVPPContext *qsv = fs->opaque;
    AVFrame *frame = NULL;
    int ret = 0;

    for (int i = 0; i < ctx->nb_inputs; i++) {
        ret = ff_framesync_get_frame(fs, i, &frame, 0);
        if (ret == 0)
            ret = ff_qsvvpp_filter_frame(qsv, ctx->inputs[i], frame);
        if (ret < 0 && ret != AVERROR(EAGAIN))
            break;
    }

    if (ret == 0 && qsv->got_frame == 0) {
        for (int i = 0; i < ctx->nb_inputs; i++)
            FF_FILTER_FORWARD_WANTED(ctx->outputs[0], ctx->inputs[i]);

        ret = FFERROR_NOT_READY;
    }

    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    StackQSVContext *sctx = ctx->priv;
    AVFilterLink *inlink0 = ctx->inputs[0];
    enum AVPixelFormat in_format;
    int depth = 8, ret;
    mfxVPPCompInputStream *is = sctx->comp_conf.InputStream;

    if (inlink0->format == AV_PIX_FMT_QSV) {
         if (!inlink0->hw_frames_ctx || !inlink0->hw_frames_ctx->data)
             return AVERROR(EINVAL);

         in_format = ((AVHWFramesContext*)inlink0->hw_frames_ctx->data)->sw_format;
    } else
        in_format = inlink0->format;

    sctx->qsv_param.out_sw_format = in_format;

    for (int i = 1; i < sctx->base.nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        if (inlink0->format == AV_PIX_FMT_QSV) {
            AVHWFramesContext *hwfc0 = (AVHWFramesContext *)inlink0->hw_frames_ctx->data;
            AVHWFramesContext *hwfc = (AVHWFramesContext *)inlink->hw_frames_ctx->data;

            if (inlink0->format != inlink->format) {
                av_log(ctx, AV_LOG_ERROR, "Mixing hardware and software pixel formats is not supported.\n");

                return AVERROR(EINVAL);
            } else if (hwfc0->device_ctx != hwfc->device_ctx) {
                av_log(ctx, AV_LOG_ERROR, "Inputs with different underlying QSV devices are forbidden.\n");

                return AVERROR(EINVAL);
            }
        }
    }

    if (in_format == AV_PIX_FMT_P010)
        depth = 10;

    if (sctx->base.fillcolor_enable) {
        int Y, U, V;

        rgb2yuv(sctx->base.fillcolor[0] / 255.0, sctx->base.fillcolor[1] / 255.0,
                sctx->base.fillcolor[2] / 255.0, &Y, &U, &V, depth);
        sctx->comp_conf.Y = Y;
        sctx->comp_conf.U = U;
        sctx->comp_conf.V = V;
    }

    ret = config_comm_output(outlink);
    if (ret < 0)
        return ret;

    for (int i = 0; i < sctx->base.nb_inputs; i++) {
        is[i].DstX = sctx->base.regions[i].x;
        is[i].DstY = sctx->base.regions[i].y;
        is[i].DstW = sctx->base.regions[i].width;
        is[i].DstH = sctx->base.regions[i].height;
        is[i].GlobalAlpha = 255;
        is[i].GlobalAlphaEnable = 0;
        is[i].PixelAlphaEnable = 0;
    }

    return ff_qsvvpp_init(ctx, &sctx->qsv_param);
}

/*
 * Callback for qsvvpp
 * @Note: qsvvpp composition does not generate PTS for result frame.
 *        so we assign the PTS from framesync to the output frame.
 */

static int filter_callback(AVFilterLink *outlink, AVFrame *frame)
{
    StackQSVContext *sctx = outlink->src->priv;

    frame->pts = av_rescale_q(sctx->base.fs.pts,
                              sctx->base.fs.time_base, outlink->time_base);
    return ff_filter_frame(outlink, frame);
}


static int qsv_stack_init(AVFilterContext *ctx)
{
    StackQSVContext *sctx = ctx->priv;
    int ret;

    ret = stack_init(ctx);
    if (ret)
        return ret;

    /* fill composite config */
    sctx->comp_conf.Header.BufferId = MFX_EXTBUFF_VPP_COMPOSITE;
    sctx->comp_conf.Header.BufferSz = sizeof(sctx->comp_conf);
    sctx->comp_conf.NumInputStream = sctx->base.nb_inputs;
    sctx->comp_conf.InputStream = av_calloc(sctx->base.nb_inputs,
                                            sizeof(*sctx->comp_conf.InputStream));
    if (!sctx->comp_conf.InputStream)
        return AVERROR(ENOMEM);

    /* initialize QSVVPP params */
    sctx->qsv_param.filter_frame = filter_callback;
    sctx->qsv_param.ext_buf = av_mallocz(sizeof(*sctx->qsv_param.ext_buf));

    if (!sctx->qsv_param.ext_buf)
        return AVERROR(ENOMEM);

    sctx->qsv_param.ext_buf[0] = (mfxExtBuffer *)&sctx->comp_conf;
    sctx->qsv_param.num_ext_buf = 1;
    sctx->qsv_param.num_crop = 0;

    return 0;
}

static av_cold void qsv_stack_uninit(AVFilterContext *ctx)
{
    StackQSVContext *sctx = ctx->priv;

    stack_uninit(ctx);

    ff_qsvvpp_close(ctx);
    av_freep(&sctx->comp_conf.InputStream);
    av_freep(&sctx->qsv_param.ext_buf);
}

static int qsv_stack_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_QSV,
        AV_PIX_FMT_NONE,
    };

    return ff_set_common_formats_from_list(ctx, pixel_formats);
}

#include "stack_internal.c"

#if CONFIG_HSTACK_QSV_FILTER

DEFINE_HSTACK_OPTIONS(qsv);
DEFINE_STACK_FILTER(hstack, qsv, "Quick Sync Video", AVFILTER_FLAG_HWDEVICE);

#endif

#if CONFIG_VSTACK_QSV_FILTER

DEFINE_VSTACK_OPTIONS(qsv);
DEFINE_STACK_FILTER(vstack, qsv, "Quick Sync Video", AVFILTER_FLAG_HWDEVICE);

#endif

#if CONFIG_XSTACK_QSV_FILTER

DEFINE_XSTACK_OPTIONS(qsv);
DEFINE_STACK_FILTER(xstack, qsv, "Quick Sync Video", AVFILTER_FLAG_HWDEVICE);

#endif
