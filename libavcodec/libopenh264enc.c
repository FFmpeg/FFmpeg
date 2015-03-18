/*
 * OpenH264 video encoder
 * Copyright (C) 2014 Martin Storsjo
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

#include <wels/codec_api.h>
#include <wels/codec_ver.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"

#include "avcodec.h"
#include "internal.h"

typedef struct SVCContext {
    const AVClass *av_class;
    ISVCEncoder *encoder;
    int slice_mode;
    int loopfilter;
    char *profile;
} SVCContext;

#define OPENH264_VER_AT_LEAST(maj, min) \
    ((OPENH264_MAJOR  > (maj)) || \
     (OPENH264_MAJOR == (maj) && OPENH264_MINOR >= (min)))

#define OFFSET(x) offsetof(SVCContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "slice_mode", "Slice mode", OFFSET(slice_mode), AV_OPT_TYPE_INT, { .i64 = SM_AUTO_SLICE }, SM_SINGLE_SLICE, SM_RESERVED, VE, "slice_mode" },
    { "fixed", "A fixed number of slices", 0, AV_OPT_TYPE_CONST, { .i64 = SM_FIXEDSLCNUM_SLICE }, 0, 0, VE, "slice_mode" },
    { "rowmb", "One slice per row of macroblocks", 0, AV_OPT_TYPE_CONST, { .i64 = SM_ROWMB_SLICE }, 0, 0, VE, "slice_mode" },
    { "auto", "Automatic number of slices according to number of threads", 0, AV_OPT_TYPE_CONST, { .i64 = SM_AUTO_SLICE }, 0, 0, VE, "slice_mode" },
    { "loopfilter", "Enable loop filter", OFFSET(loopfilter), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },
    { "profile", "Set profile restrictions", OFFSET(profile), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { NULL }
};

static const AVClass class = {
    "libopenh264enc", av_default_item_name, options, LIBAVUTIL_VERSION_INT
};

static av_cold int svc_encode_close(AVCodecContext *avctx)
{
    SVCContext *s = avctx->priv_data;

    if (s->encoder)
        WelsDestroySVCEncoder(s->encoder);
    return 0;
}

static av_cold int svc_encode_init(AVCodecContext *avctx)
{
    SVCContext *s = avctx->priv_data;
    SEncParamExt param = { 0 };
    int err = AVERROR_UNKNOWN;

    // Mingw GCC < 4.7 on x86_32 uses an incorrect/buggy ABI for the WelsGetCodecVersion
    // function (for functions returning larger structs), thus skip the check in those
    // configurations.
#if !defined(_WIN32) || !defined(__GNUC__) || !ARCH_X86_32 || AV_GCC_VERSION_AT_LEAST(4, 7)
    OpenH264Version libver = WelsGetCodecVersion();
    if (memcmp(&libver, &g_stCodecVersion, sizeof(libver))) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect library version loaded\n");
        return AVERROR(EINVAL);
    }
#endif

    if (WelsCreateSVCEncoder(&s->encoder)) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create encoder\n");
        return AVERROR_UNKNOWN;
    }

    (*s->encoder)->GetDefaultParams(s->encoder, &param);

    param.fMaxFrameRate              = avctx->time_base.den / avctx->time_base.num;
    param.iPicWidth                  = avctx->width;
    param.iPicHeight                 = avctx->height;
    param.iTargetBitrate             = avctx->bit_rate;
    param.iMaxBitrate                = FFMAX(avctx->rc_max_rate, avctx->bit_rate);
    param.iRCMode                    = RC_QUALITY_MODE;
    param.iTemporalLayerNum          = 1;
    param.iSpatialLayerNum           = 1;
    param.bEnableDenoise             = 0;
    param.bEnableBackgroundDetection = 1;
    param.bEnableAdaptiveQuant       = 1;
    param.bEnableFrameSkip           = 0;
    param.bEnableLongTermReference   = 0;
    param.iLtrMarkPeriod             = 30;
    param.uiIntraPeriod              = avctx->gop_size;
#if OPENH264_VER_AT_LEAST(1, 4)
    param.eSpsPpsIdStrategy          = CONSTANT_ID;
#else
    param.bEnableSpsPpsIdAddition    = 0;
#endif
    param.bPrefixNalAddingCtrl       = 0;
    param.iLoopFilterDisableIdc      = !s->loopfilter;
    param.iEntropyCodingModeFlag     = 0;
    param.iMultipleThreadIdc         = avctx->thread_count;
    if (s->profile && !strcmp(s->profile, "main"))
        param.iEntropyCodingModeFlag = 1;
    else if (!s->profile && avctx->coder_type == FF_CODER_TYPE_AC)
        param.iEntropyCodingModeFlag = 1;

    param.sSpatialLayers[0].iVideoWidth         = param.iPicWidth;
    param.sSpatialLayers[0].iVideoHeight        = param.iPicHeight;
    param.sSpatialLayers[0].fFrameRate          = param.fMaxFrameRate;
    param.sSpatialLayers[0].iSpatialBitrate     = param.iTargetBitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate  = param.iMaxBitrate;

    if (avctx->slices > 1)
        s->slice_mode = SM_FIXEDSLCNUM_SLICE;
    param.sSpatialLayers[0].sSliceCfg.uiSliceMode               = s->slice_mode;
    param.sSpatialLayers[0].sSliceCfg.sSliceArgument.uiSliceNum = avctx->slices;

    if ((*s->encoder)->InitializeExt(s->encoder, &param) != cmResultSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Initialize failed\n");
        goto fail;
    }

    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {
        SFrameBSInfo fbi = { 0 };
        int i, size = 0;
        (*s->encoder)->EncodeParameterSets(s->encoder, &fbi);
        for (i = 0; i < fbi.sLayerInfo[0].iNalCount; i++)
            size += fbi.sLayerInfo[0].pNalLengthInByte[i];
        avctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        avctx->extradata_size = size;
        memcpy(avctx->extradata, fbi.sLayerInfo[0].pBsBuf, size);
    }

    return 0;

fail:
    svc_encode_close(avctx);
    return err;
}

static int svc_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet)
{
    SVCContext *s = avctx->priv_data;
    SFrameBSInfo fbi = { 0 };
    int i, ret;
    int encoded;
    SSourcePicture sp = { 0 };
    int size = 0, layer, first_layer = 0;
    int layer_size[MAX_LAYER_NUM_OF_FRAME] = { 0 };

    sp.iColorFormat = videoFormatI420;
    for (i = 0; i < 3; i++) {
        sp.iStride[i] = frame->linesize[i];
        sp.pData[i]   = frame->data[i];
    }
    sp.iPicWidth  = avctx->width;
    sp.iPicHeight = avctx->height;

    encoded = (*s->encoder)->EncodeFrame(s->encoder, &sp, &fbi);
    if (encoded != cmResultSuccess) {
        av_log(avctx, AV_LOG_ERROR, "EncodeFrame failed\n");
        return AVERROR_UNKNOWN;
    }
    if (fbi.eFrameType == videoFrameTypeSkip) {
        av_log(avctx, AV_LOG_DEBUG, "frame skipped\n");
        return 0;
    }
    first_layer = 0;
    // Normal frames are returned with one single layer, while IDR
    // frames have two layers, where the first layer contains the SPS/PPS.
    // If using global headers, don't include the SPS/PPS in the returned
    // packet - thus, only return one layer.
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER)
        first_layer = fbi.iLayerNum - 1;

    for (layer = first_layer; layer < fbi.iLayerNum; layer++) {
        for (i = 0; i < fbi.sLayerInfo[layer].iNalCount; i++)
            layer_size[layer] += fbi.sLayerInfo[layer].pNalLengthInByte[i];
        size += layer_size[layer];
    }
    av_log(avctx, AV_LOG_DEBUG, "%d slices\n", fbi.sLayerInfo[fbi.iLayerNum - 1].iNalCount);

    if ((ret = ff_alloc_packet(avpkt, size))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }
    size = 0;
    for (layer = first_layer; layer < fbi.iLayerNum; layer++) {
        memcpy(avpkt->data + size, fbi.sLayerInfo[layer].pBsBuf, layer_size[layer]);
        size += layer_size[layer];
    }
    avpkt->pts = frame->pts;
    if (fbi.eFrameType == videoFrameTypeIDR)
        avpkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

AVCodec ff_libopenh264_encoder = {
    .name           = "libopenh264",
    .long_name      = NULL_IF_CONFIG_SMALL("OpenH264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(SVCContext),
    .init           = svc_encode_init,
    .encode2        = svc_encode_frame,
    .close          = svc_encode_close,
    .capabilities   = CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
};
