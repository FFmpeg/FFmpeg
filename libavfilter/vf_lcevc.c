/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2024 James Almer <jamrial@gmail.com>
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

#include <stdint.h>

#include <LCEVC/lcevc_dec.h>

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "filters.h"
#include "video.h"

typedef struct LCEVCContext {
    LCEVC_DecoderHandle decoder;
    int w, h;
} LCEVCContext;

static LCEVC_ColorFormat map_format(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        return LCEVC_I420_8;
    case AV_PIX_FMT_YUV420P10:
        return LCEVC_I420_10_LE;
    case AV_PIX_FMT_NV12:
        return LCEVC_NV12_8;
    case AV_PIX_FMT_NV21:
        return LCEVC_NV21_8;
    case AV_PIX_FMT_GRAY8:
        return LCEVC_GRAY_8;
    case AV_PIX_FMT_GRAY10LE:
        return LCEVC_GRAY_10_LE;
    }

    return LCEVC_ColorFormat_Unknown;
}

static inline LCEVC_ColorRange map_range(int range)
{
    switch (range) {
    case AVCOL_RANGE_MPEG:
        return LCEVC_ColorRange_Limited;
    case AVCOL_RANGE_JPEG:
        return LCEVC_ColorRange_Full;
    }

    return LCEVC_ColorRange_Unknown;
}

static inline enum AVColorRange map_av_range(int range)
{
    switch (range) {
    case LCEVC_ColorRange_Limited:
        return AVCOL_RANGE_MPEG;
    case LCEVC_ColorRange_Full:
        return AVCOL_RANGE_JPEG;
    }

    return AVCOL_RANGE_UNSPECIFIED;
}

static int alloc_base_frame(AVFilterLink *inlink, const AVFrame *in,
                            LCEVC_PictureHandle *picture)
{
    AVFilterContext *ctx = inlink->dst;
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureDesc desc;
    LCEVC_PicturePlaneDesc planes[AV_VIDEO_MAX_PLANES] = { 0 };
    LCEVC_ColorFormat fmt = map_format(in->format);
    int width = in->width - in->crop_left - in->crop_right;
    int height = in->height - in->crop_top - in->crop_bottom;
    LCEVC_ReturnCode res;

    res = LCEVC_DefaultPictureDesc(&desc, fmt, width, height);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_DefaultPictureDesc failed\n");
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < AV_VIDEO_MAX_PLANES; i++) {
        planes[i].firstSample = in->data[i];
        planes[i].rowByteStride = in->linesize[i];
    }

    desc.cropTop    = in->crop_top;
    desc.cropBottom = in->crop_bottom;
    desc.cropLeft   = in->crop_left;
    desc.cropRight  = in->crop_right;
    desc.sampleAspectRatioNum = in->sample_aspect_ratio.num;
    desc.sampleAspectRatioDen = in->sample_aspect_ratio.den;
    desc.colorRange = map_range(in->color_range);
    desc.colorPrimaries = (LCEVC_ColorPrimaries)in->color_primaries;
    desc.matrixCoefficients = (LCEVC_MatrixCoefficients)in->colorspace;
    desc.transferCharacteristics = (LCEVC_TransferCharacteristics)in->color_trc;
    av_log(ctx, AV_LOG_DEBUG, "in  PTS %"PRId64", %dx%d, "
                              "%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER", "
                              "SAR %d:%d\n",
           in->pts, in->width, in->height,
           in->crop_top, in->crop_bottom, in->crop_left, in->crop_right,
           in->sample_aspect_ratio.num, in->sample_aspect_ratio.den);

    res = LCEVC_AllocPictureExternal(lcevc->decoder, &desc, NULL, planes, picture);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_AllocPictureExternal to allocate a buffer for a base frame\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int send_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureHandle picture;
    const AVFrameSideData *sd = av_frame_get_side_data(in, AV_FRAME_DATA_LCEVC);
    LCEVC_ReturnCode res;
    int ret;

    ret = alloc_base_frame(inlink, in, &picture);
    if (ret < 0)
        return ret;

    if (sd) {
        res = LCEVC_SendDecoderEnhancementData(lcevc->decoder, in->pts, 0, sd->data, sd->size);
        if (res == LCEVC_Again)
            return AVERROR(EAGAIN);
        else if (res != LCEVC_Success) {
            av_log(ctx, AV_LOG_ERROR, "LCEVC_SendDecoderEnhancementData failed\n");
            return AVERROR_EXTERNAL;
        }
    }

    res = LCEVC_SendDecoderBase(lcevc->decoder, in->pts, 0, picture, -1, in);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_SendDecoderBase failed\n");
        LCEVC_FreePicture(lcevc->decoder, picture);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int alloc_enhanced_frame(AVFilterLink *inlink, const AVFrame *out,
                                LCEVC_PictureHandle *picture)
{
    AVFilterContext *ctx = inlink->dst;
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureDesc desc;
    LCEVC_PicturePlaneDesc planes[AV_VIDEO_MAX_PLANES] = { 0 };
    LCEVC_ColorFormat fmt = map_format(out->format);
    LCEVC_ReturnCode res;

    res = LCEVC_DefaultPictureDesc(&desc, fmt, out->width, out->height);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    for (int i = 0; i < AV_VIDEO_MAX_PLANES; i++) {
        planes[i].firstSample = out->data[i];
        planes[i].rowByteStride = out->linesize[i];
    }

    res = LCEVC_AllocPictureExternal(lcevc->decoder, &desc, NULL, planes, picture);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_AllocPictureExternal to allocate a buffer for an enhanced frame\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int generate_output(AVFilterLink *inlink, AVFrame *out)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureDesc desc;
    LCEVC_DecodeInformation info;
    LCEVC_PictureHandle picture;
    LCEVC_ReturnCode res;

    res = LCEVC_ReceiveDecoderPicture(lcevc->decoder, &picture, &info);
    if (res == LCEVC_Again) {
        int64_t pts;
        int status;
        if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
            av_frame_free(&out);
            ff_outlink_set_status(outlink, status, pts);
            return 0;
        }
        // this shouldn't be reachable, but instead of asserting, just error out
        return AVERROR_BUG;
    } else if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_ReceiveDecoderPicture failed\n");
        return AVERROR_EXTERNAL;
    }

    av_frame_copy_props(out, (AVFrame *)info.baseUserData);
    av_frame_remove_side_data(out, AV_FRAME_DATA_LCEVC);

    av_frame_free((AVFrame **)&info.baseUserData);

    res = LCEVC_GetPictureDesc(lcevc->decoder, picture, &desc);
    LCEVC_FreePicture(lcevc->decoder, picture);

    out->crop_top = desc.cropTop;
    out->crop_bottom = desc.cropBottom;
    out->crop_left = desc.cropLeft;
    out->crop_right = desc.cropRight;
    out->sample_aspect_ratio.num = outlink->sample_aspect_ratio.num = desc.sampleAspectRatioNum;
    out->sample_aspect_ratio.den = outlink->sample_aspect_ratio.den = desc.sampleAspectRatioDen;
    out->color_range = map_range(desc.colorRange);
    out->color_primaries = (enum AVColorPrimaries)desc.colorPrimaries;
    out->colorspace = (enum AVColorSpace)desc.matrixCoefficients;
    out->color_trc = (enum AVColorTransferCharacteristic)desc.transferCharacteristics;
    out->width = outlink->w = desc.width + out->crop_left + out->crop_right;
    out->height = outlink->h = desc.height + out->crop_top + out->crop_bottom;

    av_log(ctx, AV_LOG_DEBUG, "out PTS %"PRId64", %dx%d, "
                              "%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER", "
                              "SAR %d:%d, "
                              "hasEnhancement %d, enhanced %d\n",
           out->pts, out->width, out->height,
           out->crop_top, out->crop_bottom, out->crop_left, out->crop_right,
           out->sample_aspect_ratio.num, out->sample_aspect_ratio.den,
           info.hasEnhancement, info.enhanced);

    return ff_filter_frame(outlink, out);
}

static int receive_frame(AVFilterLink *inlink, AVFrame *out)
{
    AVFilterContext *ctx = inlink->dst;
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureHandle picture;
    LCEVC_ReturnCode res;
    int ret;

    ret = alloc_enhanced_frame(inlink, out, &picture);
    if (ret < 0)
        return ret;

    res = LCEVC_SendDecoderPicture(lcevc->decoder, picture);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_SendDecoderPicture failed\n");
        return AVERROR_EXTERNAL;
    }

    return generate_output(inlink, out);
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    LCEVCContext *lcevc = ctx->priv;

    outlink->w = lcevc->w = inlink->w * 2 / FFMAX(inlink->sample_aspect_ratio.den, 1);
    outlink->h = lcevc->h = inlink->h * 2 / FFMAX(inlink->sample_aspect_ratio.den, 1);
    outlink->sample_aspect_ratio = (AVRational) { 0, 1 };

    return 0;
}

static void flush_bases(AVFilterContext *ctx)
{
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_PictureHandle picture;

    while (LCEVC_ReceiveDecoderBase(lcevc->decoder, &picture) == LCEVC_Success)
        LCEVC_FreePicture(lcevc->decoder, picture);
}

static int activate(AVFilterContext *ctx)
{
    LCEVCContext *lcevc   = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in, *out;
    int status, ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (!ret) {
        int64_t pts;
        if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
            if (!status)
                ff_outlink_set_status(outlink, status, pts);
        }
        if (!status)
            FF_FILTER_FORWARD_WANTED(outlink, inlink);
    }

    if (in) {
       if (in->width  != inlink->w ||
            in->height != inlink->h ||
            in->sample_aspect_ratio.den != inlink->sample_aspect_ratio.den ||
            in->sample_aspect_ratio.num != inlink->sample_aspect_ratio.num) {
            inlink->dst->inputs[0]->w                       = in->width;
            inlink->dst->inputs[0]->h                       = in->height;
            inlink->dst->inputs[0]->sample_aspect_ratio.den = in->sample_aspect_ratio.den;
            inlink->dst->inputs[0]->sample_aspect_ratio.num = in->sample_aspect_ratio.num;

            config_props(outlink);
        }

        ret = send_frame(inlink, in);
        if (ret < 0)
            return ret;
    }

    out = ff_get_video_buffer(outlink, lcevc->w, lcevc->h);
    if (!out)
        return AVERROR(ENOMEM);

    ret = receive_frame(inlink, out);
    if (ret < 0) {
        av_frame_free(&out);
        return ret;
    }

    flush_bases(ctx);

    return ret;
}

static void log_callback(LCEVC_DecoderHandle dec, LCEVC_Event event,
                         LCEVC_PictureHandle pic, const LCEVC_DecodeInformation *info,
                         const uint8_t *data, uint32_t size, void *logctx)
{
    if (event != LCEVC_Log) // shouldn't happen
        return;

    if (strlen(data) != size) // sanitize input
        return;

    av_log(logctx, AV_LOG_INFO, "LCEVC Log: %s\n", data);
}

static av_cold int init(AVFilterContext *ctx)
{
    LCEVCContext *lcevc = ctx->priv;
    LCEVC_AccelContextHandle dummy = { 0 };
    const int32_t event = LCEVC_Log;
    LCEVC_ReturnCode res;

    res = LCEVC_CreateDecoder(&lcevc->decoder, dummy);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_CreateDecoder failed\n");
        return AVERROR_EXTERNAL;
    }

    res = LCEVC_ConfigureDecoderInt(lcevc->decoder, "log_level", 4);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_ConfigureDecoderInt failed to set \"log_level\"\n");
        return AVERROR_EXTERNAL;
    }
    res = LCEVC_ConfigureDecoderIntArray(lcevc->decoder, "events", 1, &event);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_ConfigureDecoderIntArray failed to set \"events\"\n");
        return AVERROR_EXTERNAL;
    }
    res = LCEVC_SetDecoderEventCallback(lcevc->decoder, log_callback, ctx);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_SetDecoderEventCallback failed\n");
        return AVERROR_EXTERNAL;
    }

    res = LCEVC_InitializeDecoder(lcevc->decoder);
    if (res != LCEVC_Success) {
        av_log(ctx, AV_LOG_ERROR, "LCEVC_InitializeDecoder failed\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LCEVCContext *lcevc = ctx->priv;

    LCEVC_DestroyDecoder(lcevc->decoder);
}

static const AVFilterPad lcevc_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE,
    AV_PIX_FMT_NV12, AV_PIX_FMT_NV21,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10LE,
    AV_PIX_FMT_NONE
};

const AVFilter ff_vf_lcevc = {
    .name          = "lcevc",
    .description   = NULL_IF_CONFIG_SMALL("LCEVC"),
    .activate      = activate,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(lcevc_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_size     = sizeof(LCEVCContext),
    .init          = init,
    .uninit        = uninit,
};
