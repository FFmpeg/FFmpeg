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
    { "slice_mode", "set slice mode", OFFSET(slice_mode), AV_OPT_TYPE_INT, { .i64 = SM_AUTO_SLICE }, SM_SINGLE_SLICE, SM_RESERVED, VE, "slice_mode" },
        { "fixed", "a fixed number of slices", 0, AV_OPT_TYPE_CONST, { .i64 = SM_FIXEDSLCNUM_SLICE }, 0, 0, VE, "slice_mode" },
        { "rowmb", "one slice per row of macroblocks", 0, AV_OPT_TYPE_CONST, { .i64 = SM_ROWMB_SLICE }, 0, 0, VE, "slice_mode" },
        { "auto", "automatic number of slices according to number of threads", 0, AV_OPT_TYPE_CONST, { .i64 = SM_AUTO_SLICE }, 0, 0, VE, "slice_mode" },
    { "loopfilter", "enable loop filter", OFFSET(loopfilter), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },
    { "profile", "set profile restrictions", OFFSET(profile), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { NULL }
};

static const AVClass class = {
    "libopenh264enc", av_default_item_name, options, LIBAVUTIL_VERSION_INT
};

// Convert ffmpeg log level to equivalent libopenh264 log level.  Given the
// conversions below, you must set the ffmpeg log level to something greater
// than AV_LOG_DEBUG if you want to see WELS_LOG_DETAIL messages.
static int ffmpeg_to_libopenh264_log_level  (
    int ffmpeg_log_level
    )
{
    int equiv_libopenh264_log_level;
    if      (ffmpeg_log_level > AV_LOG_DEBUG)
        equiv_libopenh264_log_level = WELS_LOG_DETAIL;   // > AV_LOG_DEBUG; this is EXTREMELY detailed
    else if (ffmpeg_log_level >= AV_LOG_DEBUG)
        equiv_libopenh264_log_level = WELS_LOG_DEBUG;    // AV_LOG_DEBUG
    else if (ffmpeg_log_level >= AV_LOG_INFO)
        equiv_libopenh264_log_level = WELS_LOG_INFO;     // AV_LOG_INFO, AV_LOG_VERBOSE
    else if (ffmpeg_log_level >= AV_LOG_WARNING)
        equiv_libopenh264_log_level = WELS_LOG_WARNING;  // AV_LOG_WARNING
    else if (ffmpeg_log_level >= AV_LOG_ERROR)
        equiv_libopenh264_log_level = WELS_LOG_ERROR;    // AV_LOG_ERROR
    else
        equiv_libopenh264_log_level = WELS_LOG_QUIET;    // AV_LOG_QUIET, AV_LOG_PANIC, AV_LOG_FATAL
    return equiv_libopenh264_log_level;
}

// Convert libopenh264 log level to equivalent ffmpeg log level.
static int libopenh264_to_ffmpeg_log_level  (
    int libopenh264_log_level
    )
{
    int equiv_ffmpeg_log_level;
    if      (libopenh264_log_level >= WELS_LOG_DETAIL)
        equiv_ffmpeg_log_level = AV_LOG_DEBUG + 1;           // WELS_LOG_DETAIL
    else if (libopenh264_log_level >= WELS_LOG_DEBUG)
        equiv_ffmpeg_log_level = AV_LOG_DEBUG;               // WELS_LOG_DEBUG
    else if (libopenh264_log_level >= WELS_LOG_INFO)
        equiv_ffmpeg_log_level = AV_LOG_INFO;                // WELS_LOG_INFO
    else if (libopenh264_log_level >= WELS_LOG_WARNING)
        equiv_ffmpeg_log_level = AV_LOG_WARNING;             // WELS_LOG_WARNING
    else if (libopenh264_log_level >= WELS_LOG_ERROR)
        equiv_ffmpeg_log_level = AV_LOG_ERROR;               // WELS_LOG_ERROR
    else
        equiv_ffmpeg_log_level = AV_LOG_QUIET;               // WELS_LOG_QUIET
    return equiv_ffmpeg_log_level;
}

// This function will be provided to the libopenh264 library.  The function will be called
// when libopenh264 wants to log a message (error, warning, info, etc.).  The signature for
// this function (defined in .../codec/api/svc/codec_api.h) is:
//
//        typedef void (*WelsTraceCallback) (void* ctx, int level, const char* string);

static void libopenh264_trace_callback  (
    void *          ctx,
    int             level,
    char const *    msg
    )
{
    // The message will be logged only if the requested EQUIVALENT ffmpeg log level is
    // less than or equal to the current ffmpeg log level.  Note, however, that before
    // this function is called, welsCodecTrace::CodecTrace() will have already discarded
    // the message (and this function will not be called) if the requested libopenh264
    // log level "level" is greater than the current libopenh264 log level.
    int equiv_ffmpeg_log_level = libopenh264_to_ffmpeg_log_level(level);
    if (equiv_ffmpeg_log_level <= av_log_get_level())
        av_log(ctx, equiv_ffmpeg_log_level, "%s\n", msg);
}

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
    int equiv_libopenh264_log_level;
    WelsTraceCallback callback_function;

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

    // Set libopenh264 message logging level for this instance of the encoder using
    // the current ffmpeg log level converted to the equivalent libopenh264 level.
    //
    // The client should have the ffmpeg level set to the desired value before creating
    // the libopenh264 encoder.  Once the encoder has been created, the libopenh264
    // log level is fixed for that encoder.  Changing the ffmpeg log level to a LOWER
    // value, in the expectation that higher level libopenh264 messages will no longer
    // be logged, WILL have the expected effect.  However, changing the ffmpeg log level
    // to a HIGHER value, in the expectation that higher level libopenh264 messages will
    // now be logged, WILL NOT have the expected effect.  This is because the higher
    // level messages will be discarded by the libopenh264 logging system before our
    // message logging callback function can be invoked.
    equiv_libopenh264_log_level = ffmpeg_to_libopenh264_log_level(av_log_get_level());
    (*s->encoder)->SetOption(s->encoder,ENCODER_OPTION_TRACE_LEVEL,&equiv_libopenh264_log_level);

    // Set the logging callback function to one that uses av_log() (see implementation above).
    callback_function = (WelsTraceCallback) libopenh264_trace_callback;
    (*s->encoder)->SetOption(s->encoder,ENCODER_OPTION_TRACE_CALLBACK,(void *)&callback_function);

    // Set the AVCodecContext as the libopenh264 callback context so that it can be passed to av_log().
    (*s->encoder)->SetOption(s->encoder,ENCODER_OPTION_TRACE_CALLBACK_CONTEXT,(void *)&avctx);

    (*s->encoder)->GetDefaultParams(s->encoder, &param);

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

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        SFrameBSInfo fbi = { 0 };
        int i, size = 0;
        (*s->encoder)->EncodeParameterSets(s->encoder, &fbi);
        for (i = 0; i < fbi.sLayerInfo[0].iNalCount; i++)
            size += fbi.sLayerInfo[0].pNalLengthInByte[i];
        avctx->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
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
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
};
