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
#include "transpose.h"
#include "video.h"

typedef struct TransposeVtContext {
    AVClass *class;

    VTPixelRotationSessionRef session;
    int dir;
    int passthrough;
} TransposeVtContext;

static av_cold int transpose_vt_init(AVFilterContext *avctx)
{
    TransposeVtContext *s = avctx->priv;
    int ret;

    ret = VTPixelRotationSessionCreate(kCFAllocatorDefault, &s->session);
    if (ret != noErr) {
        av_log(avctx, AV_LOG_ERROR, "Rotation session create failed, %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold void transpose_vt_uninit(AVFilterContext *avctx)
{
    TransposeVtContext *s = avctx->priv;

    if (s->session) {
        VTPixelRotationSessionInvalidate(s->session);
        CFRelease(s->session);
        s->session = NULL;
    }
}

static int transpose_vt_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int ret;
    AVFilterContext *ctx = link->dst;
    TransposeVtContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    CVPixelBufferRef src;
    CVPixelBufferRef dst;
    AVFrame *out;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    src = (CVPixelBufferRef)in->data[3];
    dst = (CVPixelBufferRef)out->data[3];
    ret = VTPixelRotationSessionRotateImage(s->session, src, dst);
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

static int transpose_vt_recreate_hw_ctx(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    AVHWFramesContext *hw_frame_ctx_in;
    AVHWFramesContext *hw_frame_ctx_out;
    int err;

    av_buffer_unref(&outlink->hw_frames_ctx);

    hw_frame_ctx_in = (AVHWFramesContext *)inlink->hw_frames_ctx->data;
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

static int transpose_vt_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    TransposeVtContext *s  = avctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    CFStringRef rotation = kVTRotation_0;
    CFBooleanRef vflip = kCFBooleanFalse;
    CFBooleanRef hflip = kCFBooleanFalse;
    int swap_w_h = 0;

    av_buffer_unref(&outlink->hw_frames_ctx);
    outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);

    if ((inlink->w >= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    }

    s->passthrough = TRANSPOSE_PT_TYPE_NONE;

    switch (s->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
        rotation = kVTRotation_CCW90;
        vflip = kCFBooleanTrue;
        swap_w_h = 1;
        break;
    case TRANSPOSE_CCLOCK:
        rotation = kVTRotation_CCW90;
        swap_w_h = 1;
        break;
    case TRANSPOSE_CLOCK:
        rotation = kVTRotation_CW90;
        swap_w_h = 1;
        break;
    case TRANSPOSE_CLOCK_FLIP:
        rotation = kVTRotation_CW90;
        vflip = kCFBooleanTrue;
        swap_w_h = 1;
        break;
    case TRANSPOSE_REVERSAL:
        rotation = kVTRotation_180;
        break;
    case TRANSPOSE_HFLIP:
        hflip = kCFBooleanTrue;
        break;
    case TRANSPOSE_VFLIP:
        vflip = kCFBooleanTrue;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Failed to set direction to %d\n", s->dir);
        return AVERROR(EINVAL);
    }

    err = VTSessionSetProperty(s->session, kVTPixelRotationPropertyKey_Rotation,
                               rotation);
    if (err != noErr) {
        av_log(avctx, AV_LOG_ERROR, "Set rotation property failed, %d\n", err);
        return AVERROR_EXTERNAL;
    }
    err = VTSessionSetProperty(s->session, kVTPixelRotationPropertyKey_FlipVerticalOrientation,
                               vflip);
    if (err != noErr) {
        av_log(avctx, AV_LOG_ERROR, "Set vertical flip property failed, %d\n", err);
        return AVERROR_EXTERNAL;
    }
    err = VTSessionSetProperty(s->session, kVTPixelRotationPropertyKey_FlipHorizontalOrientation,
                               hflip);
    if (err != noErr) {
        av_log(avctx, AV_LOG_ERROR, "Set horizontal flip property failed, %d\n", err);
        return AVERROR_EXTERNAL;
    }

    if (!swap_w_h)
        return 0;

    outlink->w = inlink->h;
    outlink->h = inlink->w;
    return transpose_vt_recreate_hw_ctx(outlink);
}

#define OFFSET(x) offsetof(TransposeVtContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption transpose_vt_options[] = {
    { "dir", "set transpose direction",
            OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 6, FLAGS, .unit = "dir" },
    { "cclock_flip", "rotate counter-clockwise with vertical flip",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
    { "clock", "rotate clockwise",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK }, .flags=FLAGS, .unit = "dir" },
    { "cclock", "rotate counter-clockwise",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK }, .flags=FLAGS, .unit = "dir" },
    { "clock_flip", "rotate clockwise with vertical flip",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
    { "reversal", "rotate by half-turn",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_REVERSAL }, .flags=FLAGS, .unit = "dir" },
    { "hflip", "flip horizontally",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_HFLIP }, .flags=FLAGS, .unit = "dir" },
    { "vflip", "flip vertically",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_VFLIP }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
            OFFSET(passthrough), AV_OPT_TYPE_INT, { .i64=TRANSPOSE_PT_TYPE_NONE },  0, INT_MAX, FLAGS, .unit = "passthrough" },
    { "none", "always apply transposition",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_NONE }, INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
    { "portrait", "preserve portrait geometry",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_PORTRAIT },  INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
    { "landscape", "preserve landscape geometry",
            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_PT_TYPE_LANDSCAPE }, INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(transpose_vt);

static const AVFilterPad transpose_vt_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &transpose_vt_filter_frame,
    },
};

static const AVFilterPad transpose_vt_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &transpose_vt_config_output,
    },
};

const AVFilter ff_vf_transpose_vt = {
    .name           = "transpose_vt",
    .description    = NULL_IF_CONFIG_SMALL("Transpose Videotoolbox frames"),
    .priv_size      = sizeof(TransposeVtContext),
    .init           = transpose_vt_init,
    .uninit         = transpose_vt_uninit,
    FILTER_INPUTS(transpose_vt_inputs),
    FILTER_OUTPUTS(transpose_vt_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VIDEOTOOLBOX),
    .priv_class     = &transpose_vt_class,
    .flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
