/*
 * Android MediaCodec encoders
 *
 * Copyright (c) 2022 Zhao Zhili <zhilizhao@tencent.com>
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

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/hwcontext_mediacodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "hwconfig.h"
#include "jni.h"
#include "mediacodec.h"
#include "mediacodec_wrapper.h"
#include "mediacodecdec_common.h"

#define INPUT_DEQUEUE_TIMEOUT_US 8000
#define OUTPUT_DEQUEUE_TIMEOUT_US 8000

typedef struct MediaCodecEncContext {
    AVClass *avclass;
    FFAMediaCodec *codec;
    int use_ndk_codec;
    FFANativeWindow *window;

    int fps;
    int width;
    int height;

    uint8_t *extradata;
    int extradata_size;

    // Since MediaCodec doesn't output DTS, use a timestamp queue to save pts
    // of AVFrame and generate DTS for AVPacket.
    //
    // This doesn't work when use Surface as input, in that case frames can be
    // sent to encoder without our notice. One exception is frames come from
    // our MediaCodec decoder wrapper, since we can control it's render by
    // av_mediacodec_release_buffer.
    int64_t timestamps[32];
    int ts_head;
    int ts_tail;

    int eof_sent;

    AVFrame *frame;
} MediaCodecEncContext;

enum {
    COLOR_FormatYUV420Planar                              = 0x13,
    COLOR_FormatYUV420SemiPlanar                          = 0x15,
    COLOR_FormatSurface                                   = 0x7F000789,
};

static const struct {
    int color_format;
    enum AVPixelFormat pix_fmt;
} color_formats[] = {
    { COLOR_FormatYUV420Planar,         AV_PIX_FMT_YUV420P },
    { COLOR_FormatYUV420SemiPlanar,     AV_PIX_FMT_NV12    },
    { COLOR_FormatSurface,              AV_PIX_FMT_MEDIACODEC },
};

static const enum AVPixelFormat avc_pix_fmts[] = {
    AV_PIX_FMT_MEDIACODEC,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NONE
};

static void mediacodec_output_format(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    char *name = ff_AMediaCodec_getName(s->codec);
    FFAMediaFormat *out_format = ff_AMediaCodec_getOutputFormat(s->codec);
    char *str = ff_AMediaFormat_toString(out_format);

    av_log(avctx, AV_LOG_DEBUG, "MediaCodec encoder %s output format %s\n",
           name ? name : "unknown", str);
    av_free(name);
    av_free(str);
    ff_AMediaFormat_delete(out_format);
}

static av_cold int mediacodec_init(AVCodecContext *avctx)
{
    const char *codec_mime = NULL;
    MediaCodecEncContext *s = avctx->priv_data;
    FFAMediaFormat *format = NULL;
    int ret;
    int gop;

    if (s->use_ndk_codec < 0)
        s->use_ndk_codec = !av_jni_get_java_vm(avctx);

    switch (avctx->codec_id) {
    case AV_CODEC_ID_H264:
        codec_mime = "video/avc";
        break;
    case AV_CODEC_ID_HEVC:
        codec_mime = "video/hevc";
        break;
    default:
        av_assert0(0);
    }

    s->codec = ff_AMediaCodec_createEncoderByType(codec_mime, s->use_ndk_codec);
    if (!s->codec) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create encoder for type %s\n",
               codec_mime);
        return AVERROR_EXTERNAL;
    }

    format = ff_AMediaFormat_new(s->use_ndk_codec);
    if (!format) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create media format\n");
        return AVERROR_EXTERNAL;
    }

    ff_AMediaFormat_setString(format, "mime", codec_mime);
    s->width = FFALIGN(avctx->width, 16);
    s->height = avctx->height;
    ff_AMediaFormat_setInt32(format, "width", s->width);
    ff_AMediaFormat_setInt32(format, "height", s->height);

    if (avctx->pix_fmt == AV_PIX_FMT_MEDIACODEC) {
        AVMediaCodecContext *user_ctx = avctx->hwaccel_context;
        if (avctx->hw_device_ctx) {
            AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)(avctx->hw_device_ctx->data);
            AVMediaCodecDeviceContext *dev_ctx;

            if (device_ctx->type != AV_HWDEVICE_TYPE_MEDIACODEC || !device_ctx->hwctx) {
                ret = AVERROR(EINVAL);
                goto bailout;
            }
            dev_ctx = device_ctx->hwctx;
            s->window = ff_mediacodec_surface_ref(dev_ctx->surface, dev_ctx->native_window, avctx);
        }

        if (!s->window && user_ctx && user_ctx->surface)
            s->window = ff_mediacodec_surface_ref(user_ctx->surface, NULL, avctx);

        if (!s->window) {
            ret = AVERROR(EINVAL);
            av_log(avctx, AV_LOG_ERROR, "Missing hw_device_ctx or hwaccel_context for AV_PIX_FMT_MEDIACODEC\n");
            goto bailout;
        }
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(color_formats); i++) {
        if (avctx->pix_fmt == color_formats[i].pix_fmt) {
            ff_AMediaFormat_setInt32(format, "color-format",
                                     color_formats[i].color_format);
            break;
        }
    }

    if (avctx->bit_rate)
        ff_AMediaFormat_setInt32(format, "bitrate", avctx->bit_rate);
    // frame-rate and i-frame-interval are required to configure codec
    if (avctx->framerate.num >= avctx->framerate.den && avctx->framerate.den > 0) {
        s->fps = avctx->framerate.num / avctx->framerate.den;
    } else {
        s->fps = 30;
        av_log(avctx, AV_LOG_INFO, "Use %d as the default MediaFormat frame-rate\n", s->fps);
    }
    gop = round(avctx->gop_size / s->fps);
    if (gop == 0) {
        gop = 1;
        av_log(avctx, AV_LOG_INFO,
                "Use %d as the default MediaFormat i-frame-interval, "
                "please set gop_size properly (>= fps)\n", gop);
    } else {
        av_log(avctx, AV_LOG_DEBUG, "Set i-frame-interval to %d\n", gop);
    }

    ff_AMediaFormat_setInt32(format, "frame-rate", s->fps);
    ff_AMediaFormat_setInt32(format, "i-frame-interval", gop);


    ret = ff_AMediaCodec_getConfigureFlagEncode(s->codec);
    ret = ff_AMediaCodec_configure(s->codec, format, s->window, NULL, ret);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "MediaCodec configure failed, %s\n", av_err2str(ret));
        goto bailout;
    }

    ret = ff_AMediaCodec_start(s->codec);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "MediaCodec failed to start, %s\n", av_err2str(ret));
        goto bailout;
    }

    mediacodec_output_format(avctx);

    s->frame = av_frame_alloc();
    if (!s->frame)
        ret = AVERROR(ENOMEM);

bailout:
    if (format)
        ff_AMediaFormat_delete(format);
    return ret;
}

static int mediacodec_receive(AVCodecContext *avctx,
                               AVPacket *pkt,
                               int *got_packet)
{
    MediaCodecEncContext *s = avctx->priv_data;
    FFAMediaCodec *codec = s->codec;
    FFAMediaCodecBufferInfo out_info = {0};
    uint8_t *out_buf;
    size_t out_size = 0;
    int ret;
    int extradata_size = 0;
    int64_t timeout_us = s->eof_sent ? OUTPUT_DEQUEUE_TIMEOUT_US : 0;
    ssize_t index = ff_AMediaCodec_dequeueOutputBuffer(codec, &out_info, timeout_us);

    if (ff_AMediaCodec_infoTryAgainLater(codec, index))
        return AVERROR(EAGAIN);

    if (ff_AMediaCodec_infoOutputFormatChanged(codec, index)) {
        mediacodec_output_format(avctx);
        return AVERROR(EAGAIN);
    }

    if (ff_AMediaCodec_infoOutputBuffersChanged(codec, index)) {
        ff_AMediaCodec_cleanOutputBuffers(codec);
        return AVERROR(EAGAIN);
    }

    if (index < 0)
        return AVERROR_EXTERNAL;

    if (out_info.flags & ff_AMediaCodec_getBufferFlagEndOfStream(codec))
        return AVERROR_EOF;

    out_buf = ff_AMediaCodec_getOutputBuffer(codec, index, &out_size);
    if (!out_buf) {
        ret = AVERROR_EXTERNAL;
        goto bailout;
    }

    if (out_info.flags & ff_AMediaCodec_getBufferFlagCodecConfig(codec)) {
        ret = av_reallocp(&s->extradata, out_info.size);
        if (ret)
            goto bailout;

        s->extradata_size = out_info.size;
        memcpy(s->extradata, out_buf + out_info.offset, out_info.size);
        ff_AMediaCodec_releaseOutputBuffer(codec, index, false);
        // try immediately
        return mediacodec_receive(avctx, pkt, got_packet);
    }

    ret = ff_get_encode_buffer(avctx, pkt, out_info.size + s->extradata_size, 0);
    if (ret < 0)
      goto bailout;

    if (s->extradata_size) {
        extradata_size = s->extradata_size;
        s->extradata_size = 0;
        memcpy(pkt->data, s->extradata, extradata_size);
    }
    memcpy(pkt->data + extradata_size, out_buf + out_info.offset, out_info.size);
    pkt->pts = av_rescale_q(out_info.presentationTimeUs, AV_TIME_BASE_Q, avctx->time_base);
    if (s->ts_tail != s->ts_head) {
        pkt->dts = s->timestamps[s->ts_tail];
        s->ts_tail = (s->ts_tail + 1) % FF_ARRAY_ELEMS(s->timestamps);
    }

    if (out_info.flags & ff_AMediaCodec_getBufferFlagKeyFrame(codec))
        pkt->flags |= AV_PKT_FLAG_KEY;
    ret = 0;
    *got_packet = 1;

    av_log(avctx, AV_LOG_TRACE, "receive packet pts %" PRId64 " dts %" PRId64
           " flags %d extradata %d\n",
           pkt->pts, pkt->dts, pkt->flags, extradata_size);

bailout:
    ff_AMediaCodec_releaseOutputBuffer(codec, index, false);
    return ret;
}

static void copy_frame_to_buffer(AVCodecContext *avctx, const AVFrame *frame, uint8_t *dst, size_t size)
{
    MediaCodecEncContext *s = avctx->priv_data;
    uint8_t *dst_data[4] = {};
    int dst_linesize[4] = {};
    const uint8_t *src_data[4] = {
            frame->data[0], frame->data[1], frame->data[2], frame->data[3]
    };

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
        dst_data[0] = dst;
        dst_data[1] = dst + s->width * s->height;
        dst_data[2] = dst_data[1] + s->width * s->height / 4;

        dst_linesize[0] = s->width;
        dst_linesize[1] = dst_linesize[2] = s->width / 2;
    } else if (avctx->pix_fmt == AV_PIX_FMT_NV12) {
        dst_data[0] = dst;
        dst_data[1] = dst + s->width * s->height;

        dst_linesize[0] = s->width;
        dst_linesize[1] = s->width;
    } else {
        av_assert0(0);
    }

    av_image_copy(dst_data, dst_linesize, src_data, frame->linesize,
                  avctx->pix_fmt, avctx->width, avctx->height);
}

static int mediacodec_send(AVCodecContext *avctx,
                           const AVFrame *frame) {
    MediaCodecEncContext *s = avctx->priv_data;
    FFAMediaCodec *codec = s->codec;
    ssize_t index;
    uint8_t *input_buf = NULL;
    size_t input_size = 0;
    int64_t pts = 0;
    uint32_t flags = 0;
    int64_t timeout_us;

    if (s->eof_sent)
        return 0;

    if (s->window) {
        if (!frame) {
            s->eof_sent = 1;
            return ff_AMediaCodec_signalEndOfInputStream(codec);
        }


        if (frame->data[3]) {
            pts = av_rescale_q(frame->pts, avctx->time_base, AV_TIME_BASE_Q);
            s->timestamps[s->ts_head] = frame->pts;
            s->ts_head = (s->ts_head + 1) % FF_ARRAY_ELEMS(s->timestamps);

            av_mediacodec_release_buffer((AVMediaCodecBuffer *)frame->data[3], 1);
        }
        return 0;
    }

    timeout_us = INPUT_DEQUEUE_TIMEOUT_US;
    index = ff_AMediaCodec_dequeueInputBuffer(codec, timeout_us);
    if (ff_AMediaCodec_infoTryAgainLater(codec, index))
        return AVERROR(EAGAIN);

    if (index < 0) {
        av_log(avctx, AV_LOG_ERROR, "dequeue input buffer failed, %zd", index);
        return AVERROR_EXTERNAL;
    }

    if (frame) {
        input_buf = ff_AMediaCodec_getInputBuffer(codec, index, &input_size);
        copy_frame_to_buffer(avctx, frame, input_buf, input_size);

        pts = av_rescale_q(frame->pts, avctx->time_base, AV_TIME_BASE_Q);

        s->timestamps[s->ts_head] = frame->pts;
        s->ts_head = (s->ts_head + 1) % FF_ARRAY_ELEMS(s->timestamps);
    } else {
        flags |= ff_AMediaCodec_getBufferFlagEndOfStream(codec);
        s->eof_sent = 1;
    }

    ff_AMediaCodec_queueInputBuffer(codec, index, 0, input_size, pts, flags);
    return 0;
}

static int mediacodec_encode(AVCodecContext *avctx, AVPacket *pkt)
{
    MediaCodecEncContext *s = avctx->priv_data;
    int ret;
    int got_packet = 0;

    // Return on three case:
    // 1. Serious error
    // 2. Got a packet success
    // 3. No AVFrame is available yet (don't return if get_frame return EOF)
    while (1) {
        ret = mediacodec_receive(avctx, pkt, &got_packet);
        if (!ret)
            return 0;
        else if (ret != AVERROR(EAGAIN))
            return ret;

        if (!s->frame->buf[0]) {
            ret = ff_encode_get_frame(avctx, s->frame);
            if (ret && ret != AVERROR_EOF)
                return ret;
        }

        ret = mediacodec_send(avctx, s->frame->buf[0] ? s->frame : NULL);
        if (!ret)
            av_frame_unref(s->frame);
        else if (ret != AVERROR(EAGAIN))
            return ret;
    }

    return 0;
}

static av_cold int mediacodec_close(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    if (s->codec) {
        ff_AMediaCodec_stop(s->codec);
        ff_AMediaCodec_delete(s->codec);
        s->codec = NULL;
    }

    if (s->window) {
        ff_mediacodec_surface_unref(s->window, avctx);
        s->window = NULL;
    }

    av_frame_free(&s->frame);

    return 0;
}

static const AVCodecHWConfigInternal *const mediacodec_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public          = {
            .pix_fmt     = AV_PIX_FMT_MEDIACODEC,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_MEDIACODEC,
        },
        .hwaccel         = NULL,
    },
    NULL
};

#define OFFSET(x) offsetof(MediaCodecEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption common_options[] = {
    { "ndk_codec", "Use MediaCodec from NDK",
                    OFFSET(use_ndk_codec), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, VE },
    { NULL },
};

#define MEDIACODEC_ENCODER_CLASS(name)              \
static const AVClass name ## _mediacodec_class = {  \
    .class_name = #name "_mediacodec",              \
    .item_name  = av_default_item_name,             \
    .option     = common_options,                   \
    .version    = LIBAVUTIL_VERSION_INT,            \
};                                                  \

#define DECLARE_MEDIACODEC_ENCODER(short_name, long_name, codec_id)     \
MEDIACODEC_ENCODER_CLASS(short_name)                                    \
const FFCodec ff_ ## short_name ## _mediacodec_encoder = {              \
    .p.name           = #short_name "_mediacodec",                      \
    CODEC_LONG_NAME(long_name " Android MediaCodec encoder"),           \
    .p.type           = AVMEDIA_TYPE_VIDEO,                             \
    .p.id             = codec_id,                                       \
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY           \
                        | AV_CODEC_CAP_HARDWARE,                        \
    .priv_data_size   = sizeof(MediaCodecEncContext),                   \
    .p.pix_fmts       = avc_pix_fmts,                                   \
    .init             = mediacodec_init,                                \
    FF_CODEC_RECEIVE_PACKET_CB(mediacodec_encode),                      \
    .close            = mediacodec_close,                               \
    .p.priv_class     = &short_name ## _mediacodec_class,               \
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,                      \
    .p.wrapper_name = "mediacodec",                                     \
    .hw_configs     = mediacodec_hw_configs,                            \
};                                                                      \

#if CONFIG_H264_MEDIACODEC_ENCODER
DECLARE_MEDIACODEC_ENCODER(h264, "H.264", AV_CODEC_ID_H264)
#endif

#if CONFIG_HEVC_MEDIACODEC_ENCODER
DECLARE_MEDIACODEC_ENCODER(hevc, "H.265", AV_CODEC_ID_HEVC)
#endif
