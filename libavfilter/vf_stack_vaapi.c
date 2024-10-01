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
 * Hardware accelerated hstack, vstack and xstack filters based on VA-API
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
#include "libavutil/mem.h"

#include "filters.h"
#include "formats.h"
#include "video.h"
#include "framesync.h"
#include "vaapi_vpp.h"

#define HSTACK_NAME             "hstack_vaapi"
#define VSTACK_NAME             "vstack_vaapi"
#define XSTACK_NAME             "xstack_vaapi"
#define HWContext               VAAPIVPPContext
#define StackHWContext          StackVAAPIContext
#include "stack_internal.h"

typedef struct StackVAAPIContext {
    StackBaseContext base;

    VARectangle *rects;
} StackVAAPIContext;

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *avctx = fs->parent;
    AVFilterLink *outlink = avctx->outputs[0];
    StackVAAPIContext *sctx = fs->opaque;
    VAAPIVPPContext *vppctx = fs->opaque;
    AVFrame *oframe, *iframe;
    VAProcPipelineParameterBuffer *params = NULL;
    VARectangle *irect = NULL;
    int ret = 0;

    if (vppctx->va_context == VA_INVALID_ID)
        return AVERROR(EINVAL);

    oframe = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!oframe)
        return AVERROR(ENOMEM);

    irect = av_calloc(avctx->nb_inputs, sizeof(*irect));
    params = av_calloc(avctx->nb_inputs, sizeof(*params));
    if (!irect || !params) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < avctx->nb_inputs; i++) {
        ret = ff_framesync_get_frame(fs, i, &iframe, 0);
        if (ret)
            goto fail;

        if (i == 0) {
            ret = av_frame_copy_props(oframe, iframe);
            if (ret < 0)
                goto fail;
        }

        ret = ff_vaapi_vpp_init_params(avctx, &params[i], iframe, oframe);
        if (ret)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "stack input %d: %s, %ux%u (%"PRId64").\n",
               i, av_get_pix_fmt_name(iframe->format),
               iframe->width, iframe->height, iframe->pts);
        irect[i].x = 0;
        irect[i].y = 0;
        irect[i].width = iframe->width;
        irect[i].height = iframe->height;
        params[i].surface_region = &irect[i];
        params[i].surface = (VASurfaceID)(uintptr_t)iframe->data[3];
        params[i].output_region = &sctx->rects[i];

        if (sctx->base.fillcolor_enable)
            params[i].output_background_color = (sctx->base.fillcolor[3] << 24 |
                                                 sctx->base.fillcolor[0] << 16 |
                                                 sctx->base.fillcolor[1] << 8 |
                                                 sctx->base.fillcolor[2]);
    }

    oframe->pts = av_rescale_q(sctx->base.fs.pts, sctx->base.fs.time_base, outlink->time_base);
    oframe->sample_aspect_ratio = outlink->sample_aspect_ratio;

    ret = ff_vaapi_vpp_render_pictures(avctx, params, avctx->nb_inputs, oframe);
    if (ret)
        goto fail;

    av_freep(&irect);
    av_freep(&params);
    return ff_filter_frame(outlink, oframe);

fail:
    av_freep(&irect);
    av_freep(&params);
    av_frame_free(&oframe);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    StackVAAPIContext *sctx = avctx->priv;
    VAAPIVPPContext *vppctx = avctx->priv;
    AVFilterLink *inlink0 = avctx->inputs[0];
    FilterLink      *inl0 = ff_filter_link(inlink0);
    AVHWFramesContext *hwfc0 = NULL;
    int ret;

    if (inlink0->format != AV_PIX_FMT_VAAPI || !inl0->hw_frames_ctx || !inl0->hw_frames_ctx->data) {
        av_log(avctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
        return AVERROR(EINVAL);
    }

    hwfc0 = (AVHWFramesContext *)inl0->hw_frames_ctx->data;

    for (int i = 1; i < sctx->base.nb_inputs; i++) {
        AVFilterLink *inlink = avctx->inputs[i];
        FilterLink      *inl = ff_filter_link(inlink);
        AVHWFramesContext *hwfc = NULL;

        if (inlink->format != AV_PIX_FMT_VAAPI || !inl->hw_frames_ctx || !inl->hw_frames_ctx->data) {
            av_log(avctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
            return AVERROR(EINVAL);
        }

        hwfc = (AVHWFramesContext *)inl->hw_frames_ctx->data;

        if (hwfc0->sw_format != hwfc->sw_format) {
            av_log(avctx, AV_LOG_ERROR, "All inputs should have the same underlying software pixel format.\n");
            return AVERROR(EINVAL);
        }

        if (hwfc0->device_ctx != hwfc->device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "All inputs should have the same underlying vaapi devices.\n");
            return AVERROR(EINVAL);
        }
    }

    ff_vaapi_vpp_config_input(inlink0);
    vppctx->output_format = hwfc0->sw_format;

    ret = config_comm_output(outlink);
    if (ret < 0)
        return ret;

    for (int i = 0; i < sctx->base.nb_inputs; i++) {
        sctx->rects[i].x = sctx->base.regions[i].x;
        sctx->rects[i].y = sctx->base.regions[i].y;
        sctx->rects[i].width = sctx->base.regions[i].width;
        sctx->rects[i].height = sctx->base.regions[i].height;
    }

    vppctx->output_width = outlink->w;
    vppctx->output_height = outlink->h;

    return ff_vaapi_vpp_config_output(outlink);
}

static int vaapi_stack_init(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;
    VAAPIVPPContext *vppctx = avctx->priv;
    int ret;

    ret = stack_init(avctx);
    if (ret)
        return ret;

    /* stack region */
    sctx->rects = av_calloc(sctx->base.nb_inputs, sizeof(*sctx->rects));
    if (!sctx->rects)
        return AVERROR(ENOMEM);

    ff_vaapi_vpp_ctx_init(avctx);
    vppctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

static av_cold void vaapi_stack_uninit(AVFilterContext *avctx)
{
    StackVAAPIContext *sctx = avctx->priv;

    stack_uninit(avctx);

    av_freep(&sctx->rects);
}

static const enum AVPixelFormat vaapi_stack_pix_fmts[] = {
    AV_PIX_FMT_VAAPI,
    AV_PIX_FMT_NONE,
};

#include "stack_internal.c"

#if CONFIG_HSTACK_VAAPI_FILTER

DEFINE_HSTACK_OPTIONS(vaapi);
DEFINE_STACK_FILTER(hstack, vaapi, "VA-API", 0);

#endif

#if CONFIG_VSTACK_VAAPI_FILTER

DEFINE_VSTACK_OPTIONS(vaapi);
DEFINE_STACK_FILTER(vstack, vaapi, "VA-API", 0);

#endif

#if CONFIG_XSTACK_VAAPI_FILTER

DEFINE_XSTACK_OPTIONS(vaapi);
DEFINE_STACK_FILTER(xstack, vaapi, "VA-API", 0);

#endif
