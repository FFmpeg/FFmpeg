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
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"

#include "avcodec.h"
#include "internal.h"
#include "libopenh264.h"

#if !OPENH264_VER_AT_LEAST(1, 6)
#define SM_SIZELIMITED_SLICE SM_DYN_SLICE
#endif

typedef struct SVCContext {
    const AVClass *av_class;
    ISVCEncoder *encoder;
    int slice_mode;
    int loopfilter;
    char *profile;
    int max_nal_size;
    int skip_frames;
    int skipped;
    int cabac;
} SVCContext;

#define OFFSET(x) offsetof(SVCContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
#if OPENH264_VER_AT_LEAST(1, 6)
    { "slice_mode", "set slice mode", OFFSET(slice_mode), AV_OPT_TYPE_INT, { .i64 = SM_FIXEDSLCNUM_SLICE }, SM_SINGLE_SLICE, SM_RESERVED, VE, "slice_mode" },
#else
    { "slice_mode", "set slice mode", OFFSET(slice_mode), AV_OPT_TYPE_INT, { .i64 = SM_AUTO_SLICE }, SM_SINGLE_SLICE, SM_RESERVED, VE, "slice_mode" },
#endif
        { "fixed", "a fixed number of slices", 0, AV_OPT_TYPE_CONST, { .i64 = SM_FIXEDSLCNUM_SLICE }, 0, 0, VE, "slice_mode" },
#if OPENH264_VER_AT_LEAST(1, 6)
        { "dyn", "Size limited (compatibility name)", 0, AV_OPT_TYPE_CONST, { .i64 = SM_SIZELIMITED_SLICE }, 0, 0, VE, "slice_mode" },
        { "sizelimited", "Size limited", 0, AV_OPT_TYPE_CONST, { .i64 = SM_SIZELIMITED_SLICE }, 0, 0, VE, "slice_mode" },
#else
        { "rowmb", "one slice per row of macroblocks", 0, AV_OPT_TYPE_CONST, { .i64 = SM_ROWMB_SLICE }, 0, 0, VE, "slice_mode" },
        { "auto", "automatic number of slices according to number of threads", 0, AV_OPT_TYPE_CONST, { .i64 = SM_AUTO_SLICE }, 0, 0, VE, "slice_mode" },
        { "dyn", "Dynamic slicing", 0, AV_OPT_TYPE_CONST, { .i64 = SM_DYN_SLICE }, 0, 0, VE, "slice_mode" },
#endif
    { "loopfilter", "enable loop filter", OFFSET(loopfilter), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },
    { "profile", "set profile restrictions", OFFSET(profile), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },
    { "max_nal_size", "set maximum NAL size in bytes", OFFSET(max_nal_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "allow_skip_frames", "allow skipping frames to hit the target bitrate", OFFSET(skip_frames), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "cabac", "Enable cabac", OFFSET(cabac), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { NULL }
};

static const AVClass class = {
    .class_name = "libvo_amrwbenc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int svc_encode_close(AVCodecContext *avctx)
{
    SVCContext *s = avctx->priv_data;

    if (s->encoder)
        WelsDestroySVCEncoder(s->encoder);
    if (s->skipped > 0)
        av_log(avctx, AV_LOG_WARNING, "%d frames skipped\n", s->skipped);
    return 0;
}

static av_cold int svc_encode_init(AVCodecContext *avctx)
{
    SVCContext *s = avctx->priv_data;
    SEncParamExt param = { 0 };
    int err;
    int log_level;
    WelsTraceCallback callback_function;
    AVCPBProperties *props;

    if ((err = ff_libopenh264_check_version(avctx)) < 0)
        return err;

    if (WelsCreateSVCEncoder(&s->encoder)) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create encoder\n");
        return AVERROR_UNKNOWN;
    }

    // Pass all libopenh264 messages to our callback, to allow ourselves to filter them.
    log_level = WELS_LOG_DETAIL;
    (*s->encoder)->SetOption(s->encoder, ENCODER_OPTION_TRACE_LEVEL, &log_level);

    // Set the logging callback function to one that uses av_log() (see implementation above).
    callback_function = (WelsTraceCallback) ff_libopenh264_trace_callback;
    (*s->encoder)->SetOption(s->encoder, ENCODER_OPTION_TRACE_CALLBACK, &callback_function);

    // Set the AVCodecContext as the libopenh264 callback context so that it can be passed to av_log().
    (*s->encoder)->SetOption(s->encoder, ENCODER_OPTION_TRACE_CALLBACK_CONTEXT, &avctx);

    (*s->encoder)->GetDefaultParams(s->encoder, &param);

#if FF_API_CODER_TYPE
FF_DISABLE_DEPRECATION_WARNINGS
    if (!s->cabac)
        s->cabac = avctx->coder_type == FF_CODER_TYPE_AC;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    param.fMaxFrameRate              = 1/av_q2d(avctx->time_base);
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
    param.bEnableFrameSkip           = s->skip_frames;
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
    else if (!s->profile && s->cabac)
        param.iEntropyCodingModeFlag = 1;

    param.sSpatialLayers[0].iVideoWidth         = param.iPicWidth;
    param.sSpatialLayers[0].iVideoHeight        = param.iPicHeight;
    param.sSpatialLayers[0].fFrameRate          = param.fMaxFrameRate;
    param.sSpatialLayers[0].iSpatialBitrate     = param.iTargetBitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate  = param.iMaxBitrate;

#if OPENH264_VER_AT_LEAST(1, 7)
    if (avctx->sample_aspect_ratio.num && avctx->sample_aspect_ratio.den) {
        // Table E-1.
        static const AVRational sar_idc[] = {
            {   0,  0 }, // Unspecified (never written here).
            {   1,  1 }, {  12, 11 }, {  10, 11 }, {  16, 11 },
            {  40, 33 }, {  24, 11 }, {  20, 11 }, {  32, 11 },
            {  80, 33 }, {  18, 11 }, {  15, 11 }, {  64, 33 },
            { 160, 99 }, // Last 3 are unknown to openh264: {   4,  3 }, {   3,  2 }, {   2,  1 },
        };
        static const ESampleAspectRatio asp_idc[] = {
            ASP_UNSPECIFIED,
            ASP_1x1,      ASP_12x11,   ASP_10x11,   ASP_16x11,
            ASP_40x33,    ASP_24x11,   ASP_20x11,   ASP_32x11,
            ASP_80x33,    ASP_18x11,   ASP_15x11,   ASP_64x33,
            ASP_160x99,
        };
        int num, den, i;

        av_reduce(&num, &den, avctx->sample_aspect_ratio.num,
                  avctx->sample_aspect_ratio.den, 65535);

        for (i = 1; i < FF_ARRAY_ELEMS(sar_idc); i++) {
            if (num == sar_idc[i].num &&
                den == sar_idc[i].den)
                break;
        }
        if (i == FF_ARRAY_ELEMS(sar_idc)) {
            param.sSpatialLayers[0].eAspectRatio = ASP_EXT_SAR;
            param.sSpatialLayers[0].sAspectRatioExtWidth = num;
            param.sSpatialLayers[0].sAspectRatioExtHeight = den;
        } else {
            param.sSpatialLayers[0].eAspectRatio = asp_idc[i];
        }
        param.sSpatialLayers[0].bAspectRatioPresent = true;
    }
    else {
        param.sSpatialLayers[0].bAspectRatioPresent = false;
    }
#endif

    if ((avctx->slices > 1) && (s->max_nal_size)) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid combination -slices %d and -max_nal_size %d.\n",
               avctx->slices, s->max_nal_size);
        return AVERROR(EINVAL);
    }

    if (avctx->slices > 1)
        s->slice_mode = SM_FIXEDSLCNUM_SLICE;

    if (s->max_nal_size)
        s->slice_mode = SM_SIZELIMITED_SLICE;

#if OPENH264_VER_AT_LEAST(1, 6)
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = s->slice_mode;
    param.sSpatialLayers[0].sSliceArgument.uiSliceNum  = avctx->slices;
#else
    param.sSpatialLayers[0].sSliceCfg.uiSliceMode               = s->slice_mode;
    param.sSpatialLayers[0].sSliceCfg.sSliceArgument.uiSliceNum = avctx->slices;
#endif

    if (s->slice_mode == SM_SIZELIMITED_SLICE) {
        if (s->max_nal_size){
            param.uiMaxNalSize = s->max_nal_size;
#if OPENH264_VER_AT_LEAST(1, 6)
            param.sSpatialLayers[0].sSliceArgument.uiSliceSizeConstraint = s->max_nal_size;
#else
            param.sSpatialLayers[0].sSliceCfg.sSliceArgument.uiSliceSizeConstraint = s->max_nal_size;
#endif
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid -max_nal_size, "
                   "specify a valid max_nal_size to use -slice_mode dyn\n");
            return AVERROR(EINVAL);
        }
    }

    if ((*s->encoder)->InitializeExt(s->encoder, &param) != cmResultSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Initialize failed\n");
        return AVERROR_UNKNOWN;
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        SFrameBSInfo fbi = { 0 };
        int i, size = 0;
        (*s->encoder)->EncodeParameterSets(s->encoder, &fbi);
        for (i = 0; i < fbi.sLayerInfo[0].iNalCount; i++)
            size += fbi.sLayerInfo[0].pNalLengthInByte[i];
        avctx->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata)
            return AVERROR(ENOMEM);
        avctx->extradata_size = size;
        memcpy(avctx->extradata, fbi.sLayerInfo[0].pBsBuf, size);
    }

    props = ff_add_cpb_side_data(avctx);
    if (!props)
        return AVERROR(ENOMEM);
    props->max_bitrate = param.iMaxBitrate;
    props->avg_bitrate = param.iTargetBitrate;

    return 0;
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

    if (frame->pict_type == AV_PICTURE_TYPE_I) {
        (*s->encoder)->ForceIntraFrame(s->encoder, true);
    }

    encoded = (*s->encoder)->EncodeFrame(s->encoder, &sp, &fbi);
    if (encoded != cmResultSuccess) {
        av_log(avctx, AV_LOG_ERROR, "EncodeFrame failed\n");
        return AVERROR_UNKNOWN;
    }
    if (fbi.eFrameType == videoFrameTypeSkip) {
        s->skipped++;
        av_log(avctx, AV_LOG_DEBUG, "frame skipped\n");
        return 0;
    }
    first_layer = 0;
    // Normal frames are returned with one single layer, while IDR
    // frames have two layers, where the first layer contains the SPS/PPS.
    // If using global headers, don't include the SPS/PPS in the returned
    // packet - thus, only return one layer.
    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
        first_layer = fbi.iLayerNum - 1;

    for (layer = first_layer; layer < fbi.iLayerNum; layer++) {
        for (i = 0; i < fbi.sLayerInfo[layer].iNalCount; i++)
            layer_size[layer] += fbi.sLayerInfo[layer].pNalLengthInByte[i];
        size += layer_size[layer];
    }
    av_log(avctx, AV_LOG_DEBUG, "%d slices\n", fbi.sLayerInfo[fbi.iLayerNum - 1].iNalCount);

    if ((ret = ff_alloc_packet2(avctx, avpkt, size, size))) {
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
    .capabilities   = AV_CODEC_CAP_AUTO_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .wrapper_name   = "libopenh264",
};
