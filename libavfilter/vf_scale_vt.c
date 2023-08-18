/*
 * Copyright (c) 2023 Zhao Zhili <zhilizhao@tencent.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <VideoToolbox/VideoToolbox.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include "scale_eval.h"
#include "video.h"

typedef struct ScaleVtContext {
    AVClass *class;

    VTPixelTransferSessionRef transfer;
    int output_width;
    int output_height;
    char *w_expr;
    char *h_expr;

    enum AVColorPrimaries colour_primaries;
    enum AVColorTransferCharacteristic colour_transfer;
    enum AVColorSpace colour_matrix;
    char *colour_primaries_string;
    char *colour_transfer_string;
    char *colour_matrix_string;
} ScaleVtContext;

static av_cold int scale_vt_init(AVFilterContext *avctx)
{
    ScaleVtContext *s = avctx->priv;
    int ret;
    CFStringRef value;

    ret = VTPixelTransferSessionCreate(kCFAllocatorDefault, &s->transfer);
    if (ret != noErr) {
        av_log(avctx, AV_LOG_ERROR, "transfer session create failed, %d\n", ret);
        return AVERROR_EXTERNAL;
    }

#define STRING_OPTION(var_name, func_name, default_value)                \
    do {                                                                 \
        if (s->var_name##_string) {                                      \
            int var = av_##func_name##_from_name(s->var_name##_string);  \
            if (var < 0) {                                               \
                av_log(avctx, AV_LOG_ERROR, "Invalid %s.\n", #var_name); \
                return AVERROR(EINVAL);                                  \
            }                                                            \
            s->var_name = var;                                           \
        } else {                                                         \
            s->var_name = default_value;                                 \
        }                                                                \
    } while (0)

    STRING_OPTION(colour_primaries, color_primaries, AVCOL_PRI_UNSPECIFIED);
    STRING_OPTION(colour_transfer,  color_transfer,  AVCOL_TRC_UNSPECIFIED);
    STRING_OPTION(colour_matrix,    color_space,     AVCOL_SPC_UNSPECIFIED);

    if (s->colour_primaries != AVCOL_PRI_UNSPECIFIED) {
        value = av_map_videotoolbox_color_primaries_from_av(s->colour_primaries);
        if (!value) {
            av_log(avctx, AV_LOG_ERROR,
                   "Doesn't support converting to colour primaries %s\n",
                   s->colour_primaries_string);
            return AVERROR(ENOTSUP);
        }
        VTSessionSetProperty(s->transfer, kVTPixelTransferPropertyKey_DestinationColorPrimaries, value);
    }

    if (s->colour_transfer != AVCOL_TRC_UNSPECIFIED) {
        value = av_map_videotoolbox_color_trc_from_av(s->colour_transfer);
        if (!value) {
            av_log(avctx, AV_LOG_ERROR,
                   "Doesn't support converting to trc %s\n",
                   s->colour_transfer_string);
            return AVERROR(ENOTSUP);
        }
        VTSessionSetProperty(s->transfer, kVTPixelTransferPropertyKey_DestinationTransferFunction, value);
    }

    if (s->colour_matrix != AVCOL_SPC_UNSPECIFIED) {
        value = av_map_videotoolbox_color_matrix_from_av(s->colour_matrix);
        if (!value) {
            av_log(avctx, AV_LOG_ERROR,
                   "Doesn't support converting to colorspace %s\n",
                   s->colour_matrix_string);
            return AVERROR(ENOTSUP);
        }
        VTSessionSetProperty(s->transfer, kVTPixelTransferPropertyKey_DestinationYCbCrMatrix, value);
    }

    return 0;
}

static av_cold void scale_vt_uninit(AVFilterContext *avctx)
{
    ScaleVtContext *s = avctx->priv;

    if (s->transfer) {
        VTPixelTransferSessionInvalidate(s->transfer);
        CFRelease(s->transfer);
        s->transfer = NULL;
    }
}

static int scale_vt_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int ret;
    AVFilterContext *ctx = link->dst;
    ScaleVtContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    CVPixelBufferRef src;
    CVPixelBufferRef dst;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);
    if (s->colour_primaries != AVCOL_PRI_UNSPECIFIED)
        out->color_primaries = s->colour_primaries;
    if (s->colour_transfer != AVCOL_TRC_UNSPECIFIED)
        out->color_trc = s->colour_transfer;
    if (s->colour_matrix != AVCOL_SPC_UNSPECIFIED)
        out->colorspace = s->colour_matrix;

    src = (CVPixelBufferRef)in->data[3];
    dst = (CVPixelBufferRef)out->data[3];
    ret = VTPixelTransferSessionTransferImage(s->transfer, src, dst);
    if (ret != noErr) {
        av_log(ctx, AV_LOG_ERROR, "transfer image failed, %d\n", ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int scale_vt_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    ScaleVtContext *s  = avctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    AVHWFramesContext *hw_frame_ctx_in;
    AVHWFramesContext *hw_frame_ctx_out;

    err = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink,
                                   &s->output_width,
                                   &s->output_height);
    if (err < 0)
        return err;

    outlink->w = s->output_width;
    outlink->h = s->output_height;

    if (inlink->sample_aspect_ratio.num) {
        AVRational r = {outlink->h * inlink->w, outlink->w * inlink->h};
        outlink->sample_aspect_ratio = av_mul_q(r, inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

    hw_frame_ctx_in = (AVHWFramesContext *)inlink->hw_frames_ctx->data;

    av_buffer_unref(&outlink->hw_frames_ctx);
    outlink->hw_frames_ctx = av_hwframe_ctx_alloc(hw_frame_ctx_in->device_ref);
    hw_frame_ctx_out = (AVHWFramesContext *)outlink->hw_frames_ctx->data;
    hw_frame_ctx_out->format = AV_PIX_FMT_VIDEOTOOLBOX;
    hw_frame_ctx_out->sw_format = hw_frame_ctx_in->sw_format;
    hw_frame_ctx_out->width = outlink->w;
    hw_frame_ctx_out->height = outlink->h;

    err = ff_filter_init_hw_frames(avctx, outlink, 1);
    if (err < 0)
        return err;

    err = av_hwframe_ctx_init(outlink->hw_frames_ctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to init videotoolbox frame context, %s\n",
               av_err2str(err));
        return err;
    }

    return 0;
}

#define OFFSET(x) offsetof(ScaleVtContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_vt_options[] = {
    { "w", "Output video width",
            OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height",
            OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "color_matrix", "Output colour matrix coefficient set",
            OFFSET(colour_matrix_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "color_primaries", "Output colour primaries",
            OFFSET(colour_primaries_string), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "color_transfer", "Output colour transfer characteristics",
            OFFSET(colour_transfer_string),  AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(scale_vt);

static const AVFilterPad scale_vt_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &scale_vt_filter_frame,
    },
};

static const AVFilterPad scale_vt_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &scale_vt_config_output,
    },
};

const AVFilter ff_vf_scale_vt = {
    .name           = "scale_vt",
    .description    = NULL_IF_CONFIG_SMALL("Scale Videotoolbox frames"),
    .priv_size      = sizeof(ScaleVtContext),
    .init           = scale_vt_init,
    .uninit         = scale_vt_uninit,
    FILTER_INPUTS(scale_vt_inputs),
    FILTER_OUTPUTS(scale_vt_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VIDEOTOOLBOX),
    .priv_class     = &scale_vt_class,
    .flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
