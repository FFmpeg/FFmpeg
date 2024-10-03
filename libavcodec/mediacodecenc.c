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
#include "libavutil/avstring.h"
#include "libavutil/hwcontext_mediacodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bsf.h"
#include "codec_internal.h"
#include "encode.h"
#include "hwconfig.h"
#include "jni.h"
#include "mediacodec.h"
#include "mediacodec_wrapper.h"
#include "mediacodecdec_common.h"
#include "profiles.h"

#define INPUT_DEQUEUE_TIMEOUT_US 8000
#define OUTPUT_DEQUEUE_TIMEOUT_US 8000

enum BitrateMode {
    /* Constant quality mode */
    BITRATE_MODE_CQ = 0,
    /* Variable bitrate mode */
    BITRATE_MODE_VBR = 1,
    /* Constant bitrate mode */
    BITRATE_MODE_CBR = 2,
    /* Constant bitrate mode with frame drops */
    BITRATE_MODE_CBR_FD = 3,
};

typedef struct MediaCodecEncContext {
    AVClass *avclass;
    FFAMediaCodec *codec;
    int use_ndk_codec;
    const char *name;
    FFANativeWindow *window;

    int fps;
    int width;
    int height;

    uint8_t *extradata;
    int extradata_size;
    int eof_sent;

    AVFrame *frame;
    AVBSFContext *bsf;

    int bitrate_mode;
    int level;
    int pts_as_dts;
    int extract_extradata;
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

static int extract_extradata_support(AVCodecContext *avctx)
{
    const AVBitStreamFilter *bsf = av_bsf_get_by_name("extract_extradata");

    if (!bsf) {
        av_log(avctx, AV_LOG_WARNING, "extract_extradata bsf not found\n");
        return 0;
    }

    for (int i = 0; bsf->codec_ids[i] != AV_CODEC_ID_NONE; i++) {
        if (bsf->codec_ids[i] == avctx->codec_id)
            return 1;
    }

    return 0;
}

static int mediacodec_init_bsf(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    char str[128] = {0};
    int ret;
    int crop_right = s->width - avctx->width;
    int crop_bottom = s->height - avctx->height;

    /* Nothing can be done for this format now */
    if (avctx->pix_fmt == AV_PIX_FMT_MEDIACODEC)
        return 0;

    s->extract_extradata = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) &&
                           extract_extradata_support(avctx);
    if (!crop_right && !crop_bottom && !s->extract_extradata)
        return 0;

    ret = 0;
    if (crop_right || crop_bottom) {
        if (avctx->codec_id == AV_CODEC_ID_H264)
            ret = snprintf(str, sizeof(str), "h264_metadata=crop_right=%d:crop_bottom=%d",
                           crop_right, crop_bottom);
        else if (avctx->codec_id == AV_CODEC_ID_HEVC)
            /* Encoder can use CTU size larger than 16x16, so the real crop
             * margin can be larger than crop_right/crop_bottom. Let bsf figure
             * out the real crop margin.
             */
            ret = snprintf(str, sizeof(str), "hevc_metadata=width=%d:height=%d",
                           avctx->width, avctx->height);
        if (ret >= sizeof(str))
            return AVERROR_BUFFER_TOO_SMALL;
    }

    if (s->extract_extradata) {
        ret = av_strlcatf(str, sizeof(str), "%sextract_extradata", ret ? "," : "");
        if (ret >= sizeof(str))
            return AVERROR_BUFFER_TOO_SMALL;
    }

    ret = av_bsf_list_parse_str(str, &s->bsf);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_from_context(s->bsf->par_in, avctx);
    if (ret < 0)
        return ret;
    s->bsf->time_base_in = avctx->time_base;
    ret = av_bsf_init(s->bsf);

    return ret;
}

static int mediacodec_generate_extradata(AVCodecContext *avctx);

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
    case AV_CODEC_ID_VP8:
        codec_mime = "video/x-vnd.on2.vp8";
        break;
    case AV_CODEC_ID_VP9:
        codec_mime = "video/x-vnd.on2.vp9";
        break;
    case AV_CODEC_ID_MPEG4:
        codec_mime = "video/mp4v-es";
        break;
    case AV_CODEC_ID_AV1:
        codec_mime = "video/av01";
        break;
    default:
        av_assert0(0);
    }

    if (s->name)
        s->codec = ff_AMediaCodec_createCodecByName(s->name, s->use_ndk_codec);
    else
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
    // Workaround the alignment requirement of mediacodec. We can't do it
    // silently for AV_PIX_FMT_MEDIACODEC.
    if (avctx->pix_fmt != AV_PIX_FMT_MEDIACODEC &&
        (avctx->codec_id == AV_CODEC_ID_H264 ||
         avctx->codec_id == AV_CODEC_ID_HEVC)) {
        s->width = FFALIGN(avctx->width, 16);
        s->height = FFALIGN(avctx->height, 16);
    } else {
        s->width = avctx->width;
        s->height = avctx->height;
        if (s->width % 16 || s->height % 16)
            av_log(avctx, AV_LOG_WARNING,
                    "Video size %dx%d isn't align to 16, it may have device compatibility issue\n",
                    s->width, s->height);
    }
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
        /* Although there is a method ANativeWindow_toSurface() introduced in
         * API level 26, it's easier and safe to always require a Surface for
         * Java MediaCodec.
         */
        if (!s->use_ndk_codec && !s->window->surface) {
            ret = AVERROR(EINVAL);
            av_log(avctx, AV_LOG_ERROR, "Missing jobject Surface for AV_PIX_FMT_MEDIACODEC. "
                    "Please note that Java MediaCodec doesn't work with ANativeWindow.\n");
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

    ret = ff_AMediaFormatColorRange_from_AVColorRange(avctx->color_range);
    if (ret != COLOR_RANGE_UNSPECIFIED)
        ff_AMediaFormat_setInt32(format, "color-range", ret);
    ret = ff_AMediaFormatColorStandard_from_AVColorSpace(avctx->colorspace);
    if (ret != COLOR_STANDARD_UNSPECIFIED)
        ff_AMediaFormat_setInt32(format, "color-standard", ret);
    ret = ff_AMediaFormatColorTransfer_from_AVColorTransfer(avctx->color_trc);
    if (ret != COLOR_TRANSFER_UNSPECIFIED)
        ff_AMediaFormat_setInt32(format, "color-transfer", ret);

    if (avctx->bit_rate)
        ff_AMediaFormat_setInt32(format, "bitrate", avctx->bit_rate);
    if (s->bitrate_mode >= 0) {
        ff_AMediaFormat_setInt32(format, "bitrate-mode", s->bitrate_mode);
        if (s->bitrate_mode == BITRATE_MODE_CQ && avctx->global_quality > 0)
            ff_AMediaFormat_setInt32(format, "quality", avctx->global_quality);
    }
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

    ret = ff_AMediaCodecProfile_getProfileFromAVCodecContext(avctx);
    if (ret > 0) {
        av_log(avctx, AV_LOG_DEBUG, "set profile to 0x%x\n", ret);
        ff_AMediaFormat_setInt32(format, "profile", ret);
    }
    if (s->level > 0) {
        av_log(avctx, AV_LOG_DEBUG, "set level to 0x%x\n", s->level);
        ff_AMediaFormat_setInt32(format, "level", s->level);
    }
    if (avctx->max_b_frames > 0) {
        if (avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
            av_log(avctx, AV_LOG_ERROR,
                    "Enabling B frames will produce packets with no DTS. "
                    "Use -strict experimental to use it anyway.\n");
            ret = AVERROR(EINVAL);
            goto bailout;
        }
        ff_AMediaFormat_setInt32(format, "max-bframes", avctx->max_b_frames);
    }
    if (s->pts_as_dts == -1)
        s->pts_as_dts = avctx->max_b_frames <= 0;

    ret = ff_AMediaCodec_getConfigureFlagEncode(s->codec);
    ret = ff_AMediaCodec_configure(s->codec, format, s->window, NULL, ret);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "MediaCodec configure failed, %s\n", av_err2str(ret));
        if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
            av_log(avctx, AV_LOG_ERROR, "Please try -pix_fmt nv12, some devices don't "
                                        "support yuv420p as encoder input format.\n");
        goto bailout;
    }

    ret = ff_AMediaCodec_start(s->codec);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "MediaCodec failed to start, %s\n", av_err2str(ret));
        goto bailout;
    }

    ret = mediacodec_init_bsf(avctx);
    if (ret)
        goto bailout;

    mediacodec_output_format(avctx);

    s->frame = av_frame_alloc();
    if (!s->frame) {
        ret = AVERROR(ENOMEM);
        goto bailout;
    }

    ret = mediacodec_generate_extradata(avctx);

bailout:
    if (format)
        ff_AMediaFormat_delete(format);
    return ret;
}

static int mediacodec_receive(AVCodecContext *avctx, AVPacket *pkt)
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
        return mediacodec_receive(avctx, pkt);
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
    if (s->pts_as_dts)
        pkt->dts = pkt->pts;
    if (out_info.flags & ff_AMediaCodec_getBufferFlagKeyFrame(codec))
        pkt->flags |= AV_PKT_FLAG_KEY;
    ret = 0;

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

    av_image_copy2(dst_data, dst_linesize, frame->data, frame->linesize,
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

        if (frame->data[3])
            av_mediacodec_release_buffer((AVMediaCodecBuffer *)frame->data[3], 1);
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

    // Return on three case:
    // 1. Serious error
    // 2. Got a packet success
    // 3. No AVFrame is available yet (don't return if get_frame return EOF)
    while (1) {
        if (s->bsf) {
            ret = av_bsf_receive_packet(s->bsf, pkt);
            if (!ret)
                return 0;
            if (ret != AVERROR(EAGAIN))
                return ret;
        }

        ret = mediacodec_receive(avctx, pkt);
        if (s->bsf) {
            if (!ret || ret == AVERROR_EOF)
                ret = av_bsf_send_packet(s->bsf, pkt);
        } else {
            if (!ret)
                return 0;
        }

        if (ret < 0 && ret != AVERROR(EAGAIN))
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

static int mediacodec_send_dummy_frame(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    int ret;

    s->frame->width = avctx->width;
    s->frame->height = avctx->height;
    s->frame->format = avctx->pix_fmt;
    s->frame->pts = 0;

    ret = av_frame_get_buffer(s->frame, 0);
    if (ret < 0)
        return ret;

    do {
        ret = mediacodec_send(avctx, s->frame);
    } while (ret == AVERROR(EAGAIN));
    av_frame_unref(s->frame);

    if (ret < 0)
        return ret;

    ret = mediacodec_send(avctx, NULL);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Flush failed: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

static int mediacodec_receive_dummy_pkt(AVCodecContext *avctx, AVPacket *pkt)
{
    MediaCodecEncContext *s = avctx->priv_data;
    int ret;

    do {
        ret = mediacodec_receive(avctx, pkt);
    } while (ret == AVERROR(EAGAIN));

    if (ret < 0)
        return ret;

    do {
        ret = av_bsf_send_packet(s->bsf, pkt);
        if (ret < 0)
            return ret;
        ret = av_bsf_receive_packet(s->bsf, pkt);
    } while (ret == AVERROR(EAGAIN));

    return ret;
}

static int mediacodec_generate_extradata(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    AVPacket *pkt = NULL;
    int ret;
    size_t side_size;
    uint8_t *side;

    if (!(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER))
        return 0;

    if (!s->extract_extradata) {
        av_log(avctx, AV_LOG_WARNING,
               "Mediacodec encoder doesn't support AV_CODEC_FLAG_GLOBAL_HEADER. "
               "Use extract_extradata bsf when necessary.\n");
        return 0;
    }

    pkt = av_packet_alloc();
    if (!pkt)
        return AVERROR(ENOMEM);

    ret = mediacodec_send_dummy_frame(avctx);
    if (ret < 0)
        goto bailout;
    ret = mediacodec_receive_dummy_pkt(avctx, pkt);
    if (ret < 0)
        goto bailout;

    side = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &side_size);
    if (side && side_size > 0) {
        avctx->extradata = av_mallocz(side_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            ret = AVERROR(ENOMEM);
            goto bailout;
        }

        memcpy(avctx->extradata, side, side_size);
        avctx->extradata_size = side_size;
    }

bailout:
    if (s->eof_sent) {
        s->eof_sent = 0;
        ff_AMediaCodec_flush(s->codec);
    }
    av_bsf_flush(s->bsf);
    av_packet_free(&pkt);
    return ret;
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

    av_bsf_free(&s->bsf);
    av_frame_free(&s->frame);

    return 0;
}

static av_cold void mediacodec_flush(AVCodecContext *avctx)
{
    MediaCodecEncContext *s = avctx->priv_data;
    if (s->bsf)
        av_bsf_flush(s->bsf);
    av_frame_unref(s->frame);
    ff_AMediaCodec_flush(s->codec);
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
#define COMMON_OPTION                                                                                       \
    { "ndk_codec", "Use MediaCodec from NDK",                                                               \
                    OFFSET(use_ndk_codec), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, VE },                      \
    { "codec_name", "Select codec by name",                                                                 \
                    OFFSET(name), AV_OPT_TYPE_STRING, {0}, 0, 0, VE },                                      \
    { "bitrate_mode", "Bitrate control method",                                                             \
                    OFFSET(bitrate_mode), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, VE, .unit = "bitrate_mode" },  \
    { "cq", "Constant quality mode",                                                                                \
                    0, AV_OPT_TYPE_CONST, {.i64 = BITRATE_MODE_CQ}, 0, 0, VE, .unit = "bitrate_mode" },             \
    { "vbr", "Variable bitrate mode",                                                                               \
                    0, AV_OPT_TYPE_CONST, {.i64 = BITRATE_MODE_VBR}, 0, 0, VE, .unit = "bitrate_mode" },            \
    { "cbr", "Constant bitrate mode",                                                                               \
                    0, AV_OPT_TYPE_CONST, {.i64 = BITRATE_MODE_CBR}, 0, 0, VE, .unit = "bitrate_mode" },            \
    { "cbr_fd", "Constant bitrate mode with frame drops",                                                           \
                    0, AV_OPT_TYPE_CONST, {.i64 = BITRATE_MODE_CBR_FD}, 0, 0, VE, .unit = "bitrate_mode" },         \
    { "pts_as_dts", "Use PTS as DTS. It is enabled automatically if avctx max_b_frames <= 0, "              \
                    "since most of Android devices don't output B frames by default.",                      \
                    OFFSET(pts_as_dts), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, VE },                         \


#define MEDIACODEC_ENCODER_CLASS(name)              \
static const AVClass name ## _mediacodec_class = {  \
    .class_name = #name "_mediacodec",              \
    .item_name  = av_default_item_name,             \
    .option     = name ## _options,                 \
    .version    = LIBAVUTIL_VERSION_INT,            \
};                                                  \

#define DECLARE_MEDIACODEC_ENCODER(short_name, long_name, codec_id)     \
MEDIACODEC_ENCODER_CLASS(short_name)                                    \
const FFCodec ff_ ## short_name ## _mediacodec_encoder = {              \
    .p.name           = #short_name "_mediacodec",                      \
    CODEC_LONG_NAME(long_name " Android MediaCodec encoder"),           \
    .p.type           = AVMEDIA_TYPE_VIDEO,                             \
    .p.id             = codec_id,                                       \
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |         \
                        AV_CODEC_CAP_HARDWARE |                         \
                        AV_CODEC_CAP_ENCODER_FLUSH,                     \
    .priv_data_size   = sizeof(MediaCodecEncContext),                   \
    .p.pix_fmts       = avc_pix_fmts,                                   \
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,              \
    .init             = mediacodec_init,                                \
    FF_CODEC_RECEIVE_PACKET_CB(mediacodec_encode),                      \
    .close            = mediacodec_close,                               \
    .flush            = mediacodec_flush,                               \
    .p.priv_class     = &short_name ## _mediacodec_class,               \
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,                      \
    .p.wrapper_name = "mediacodec",                                     \
    .hw_configs     = mediacodec_hw_configs,                            \
};                                                                      \

#if CONFIG_H264_MEDIACODEC_ENCODER

enum MediaCodecAvcLevel {
    AVCLevel1       = 0x01,
    AVCLevel1b      = 0x02,
    AVCLevel11      = 0x04,
    AVCLevel12      = 0x08,
    AVCLevel13      = 0x10,
    AVCLevel2       = 0x20,
    AVCLevel21      = 0x40,
    AVCLevel22      = 0x80,
    AVCLevel3       = 0x100,
    AVCLevel31      = 0x200,
    AVCLevel32      = 0x400,
    AVCLevel4       = 0x800,
    AVCLevel41      = 0x1000,
    AVCLevel42      = 0x2000,
    AVCLevel5       = 0x4000,
    AVCLevel51      = 0x8000,
    AVCLevel52      = 0x10000,
    AVCLevel6       = 0x20000,
    AVCLevel61      = 0x40000,
    AVCLevel62      = 0x80000,
};

static const AVOption h264_options[] = {
    COMMON_OPTION

    FF_AVCTX_PROFILE_OPTION("baseline",             NULL, VIDEO, AV_PROFILE_H264_BASELINE)
    FF_AVCTX_PROFILE_OPTION("constrained_baseline", NULL, VIDEO, AV_PROFILE_H264_CONSTRAINED_BASELINE)
    FF_AVCTX_PROFILE_OPTION("main",                 NULL, VIDEO, AV_PROFILE_H264_MAIN)
    FF_AVCTX_PROFILE_OPTION("extended",             NULL, VIDEO, AV_PROFILE_H264_EXTENDED)
    FF_AVCTX_PROFILE_OPTION("high",                 NULL, VIDEO, AV_PROFILE_H264_HIGH)
    FF_AVCTX_PROFILE_OPTION("high10",               NULL, VIDEO, AV_PROFILE_H264_HIGH_10)
    FF_AVCTX_PROFILE_OPTION("high422",              NULL, VIDEO, AV_PROFILE_H264_HIGH_422)
    FF_AVCTX_PROFILE_OPTION("high444",              NULL, VIDEO, AV_PROFILE_H264_HIGH_444)

    { "level", "Specify level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "1",      "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel1  }, 0, 0, VE, .unit = "level" },
    { "1b",     "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel1b }, 0, 0, VE, .unit = "level" },
    { "1.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel11 }, 0, 0, VE, .unit = "level" },
    { "1.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel12 }, 0, 0, VE, .unit = "level" },
    { "1.3",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel13 }, 0, 0, VE, .unit = "level" },
    { "2",      "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel2  }, 0, 0, VE, .unit = "level" },
    { "2.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel21 }, 0, 0, VE, .unit = "level" },
    { "2.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel22 }, 0, 0, VE, .unit = "level" },
    { "3",      "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel3  }, 0, 0, VE, .unit = "level" },
    { "3.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel31 }, 0, 0, VE, .unit = "level" },
    { "3.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel32 }, 0, 0, VE, .unit = "level" },
    { "4",      "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel4  }, 0, 0, VE, .unit = "level" },
    { "4.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel41 }, 0, 0, VE, .unit = "level" },
    { "4.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel42 }, 0, 0, VE, .unit = "level" },
    { "5",      "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel5  }, 0, 0, VE, .unit = "level" },
    { "5.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel51 }, 0, 0, VE, .unit = "level" },
    { "5.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel52 }, 0, 0, VE, .unit = "level" },
    { "6.0",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel6  }, 0, 0, VE, .unit = "level" },
    { "6.1",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel61 }, 0, 0, VE, .unit = "level" },
    { "6.2",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AVCLevel62 }, 0, 0, VE, .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(h264, "H.264", AV_CODEC_ID_H264)

#endif  // CONFIG_H264_MEDIACODEC_ENCODER

#if CONFIG_HEVC_MEDIACODEC_ENCODER

enum MediaCodecHevcLevel {
    HEVCMainTierLevel1  = 0x1,
    HEVCHighTierLevel1  = 0x2,
    HEVCMainTierLevel2  = 0x4,
    HEVCHighTierLevel2  = 0x8,
    HEVCMainTierLevel21 = 0x10,
    HEVCHighTierLevel21 = 0x20,
    HEVCMainTierLevel3  = 0x40,
    HEVCHighTierLevel3  = 0x80,
    HEVCMainTierLevel31 = 0x100,
    HEVCHighTierLevel31 = 0x200,
    HEVCMainTierLevel4  = 0x400,
    HEVCHighTierLevel4  = 0x800,
    HEVCMainTierLevel41 = 0x1000,
    HEVCHighTierLevel41 = 0x2000,
    HEVCMainTierLevel5  = 0x4000,
    HEVCHighTierLevel5  = 0x8000,
    HEVCMainTierLevel51 = 0x10000,
    HEVCHighTierLevel51 = 0x20000,
    HEVCMainTierLevel52 = 0x40000,
    HEVCHighTierLevel52 = 0x80000,
    HEVCMainTierLevel6  = 0x100000,
    HEVCHighTierLevel6  = 0x200000,
    HEVCMainTierLevel61 = 0x400000,
    HEVCHighTierLevel61 = 0x800000,
    HEVCMainTierLevel62 = 0x1000000,
    HEVCHighTierLevel62 = 0x2000000,
};

static const AVOption hevc_options[] = {
    COMMON_OPTION

    FF_AVCTX_PROFILE_OPTION("main",   NULL, VIDEO, AV_PROFILE_HEVC_MAIN)
    FF_AVCTX_PROFILE_OPTION("main10", NULL, VIDEO, AV_PROFILE_HEVC_MAIN_10)

    { "level", "Specify tier and level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "m1",    "Main tier level 1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel1  },  0, 0, VE,  .unit = "level" },
    { "h1",    "High tier level 1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel1  },  0, 0, VE,  .unit = "level" },
    { "m2",    "Main tier level 2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel2  },  0, 0, VE,  .unit = "level" },
    { "h2",    "High tier level 2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel2  },  0, 0, VE,  .unit = "level" },
    { "m2.1",  "Main tier level 2.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel21 },  0, 0, VE,  .unit = "level" },
    { "h2.1",  "High tier level 2.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel21 },  0, 0, VE,  .unit = "level" },
    { "m3",    "Main tier level 3",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel3  },  0, 0, VE,  .unit = "level" },
    { "h3",    "High tier level 3",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel3  },  0, 0, VE,  .unit = "level" },
    { "m3.1",  "Main tier level 3.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel31 },  0, 0, VE,  .unit = "level" },
    { "h3.1",  "High tier level 3.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel31 },  0, 0, VE,  .unit = "level" },
    { "m4",    "Main tier level 4",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel4  },  0, 0, VE,  .unit = "level" },
    { "h4",    "High tier level 4",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel4  },  0, 0, VE,  .unit = "level" },
    { "m4.1",  "Main tier level 4.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel41 },  0, 0, VE,  .unit = "level" },
    { "h4.1",  "High tier level 4.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel41 },  0, 0, VE,  .unit = "level" },
    { "m5",    "Main tier level 5",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel5  },  0, 0, VE,  .unit = "level" },
    { "h5",    "High tier level 5",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel5  },  0, 0, VE,  .unit = "level" },
    { "m5.1",  "Main tier level 5.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel51 },  0, 0, VE,  .unit = "level" },
    { "h5.1",  "High tier level 5.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel51 },  0, 0, VE,  .unit = "level" },
    { "m5.2",  "Main tier level 5.2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel52 },  0, 0, VE,  .unit = "level" },
    { "h5.2",  "High tier level 5.2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel52 },  0, 0, VE,  .unit = "level" },
    { "m6",    "Main tier level 6",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel6  },  0, 0, VE,  .unit = "level" },
    { "h6",    "High tier level 6",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel6  },  0, 0, VE,  .unit = "level" },
    { "m6.1",  "Main tier level 6.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel61 },  0, 0, VE,  .unit = "level" },
    { "h6.1",  "High tier level 6.1",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel61 },  0, 0, VE,  .unit = "level" },
    { "m6.2",  "Main tier level 6.2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCMainTierLevel62 },  0, 0, VE,  .unit = "level" },
    { "h6.2",  "High tier level 6.2",
                0, AV_OPT_TYPE_CONST, { .i64 = HEVCHighTierLevel62 },  0, 0, VE,  .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(hevc, "H.265", AV_CODEC_ID_HEVC)

#endif  // CONFIG_HEVC_MEDIACODEC_ENCODER

#if CONFIG_VP8_MEDIACODEC_ENCODER

enum MediaCodecVP8Level {
    VP8Level_Version0 = 0x01,
    VP8Level_Version1 = 0x02,
    VP8Level_Version2 = 0x04,
    VP8Level_Version3 = 0x08,
};

static const AVOption vp8_options[] = {
    COMMON_OPTION
    { "level", "Specify tier and level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "V0",    "Level Version 0",
                0, AV_OPT_TYPE_CONST, { .i64 = VP8Level_Version0 },  0, 0, VE,  .unit = "level" },
    { "V1",    "Level Version 1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP8Level_Version1 },  0, 0, VE,  .unit = "level" },
    { "V2",    "Level Version 2",
                0, AV_OPT_TYPE_CONST, { .i64 = VP8Level_Version2 },  0, 0, VE,  .unit = "level" },
    { "V3",    "Level Version 3",
                0, AV_OPT_TYPE_CONST, { .i64 = VP8Level_Version3 },  0, 0, VE,  .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(vp8, "VP8", AV_CODEC_ID_VP8)

#endif  // CONFIG_VP8_MEDIACODEC_ENCODER

#if CONFIG_VP9_MEDIACODEC_ENCODER

enum MediaCodecVP9Level {
    VP9Level1  = 0x1,
    VP9Level11  = 0x2,
    VP9Level2  = 0x4,
    VP9Level21  = 0x8,
    VP9Level3 = 0x10,
    VP9Level31 = 0x20,
    VP9Level4  = 0x40,
    VP9Level41  = 0x80,
    VP9Level5 = 0x100,
    VP9Level51 = 0x200,
    VP9Level52  = 0x400,
    VP9Level6  = 0x800,
    VP9Level61 = 0x1000,
    VP9Level62 = 0x2000,
};

static const AVOption vp9_options[] = {
    COMMON_OPTION

    FF_AVCTX_PROFILE_OPTION("profile0",   NULL, VIDEO, AV_PROFILE_VP9_0)
    FF_AVCTX_PROFILE_OPTION("profile1",   NULL, VIDEO, AV_PROFILE_VP9_1)
    FF_AVCTX_PROFILE_OPTION("profile2",   NULL, VIDEO, AV_PROFILE_VP9_2)
    FF_AVCTX_PROFILE_OPTION("profile3",   NULL, VIDEO, AV_PROFILE_VP9_3)

    { "level", "Specify tier and level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "1",     "Level 1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level1  },  0, 0, VE,  .unit = "level" },
    { "1.1",   "Level 1.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level11 },  0, 0, VE,  .unit = "level" },
    { "2",     "Level 2",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level2  },  0, 0, VE,  .unit = "level" },
    { "2.1",   "Level 2.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level21 },  0, 0, VE,  .unit = "level" },
    { "3",     "Level 3",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level3  },  0, 0, VE,  .unit = "level" },
    { "3.1",   "Level 3.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level31 },  0, 0, VE,  .unit = "level" },
    { "4",     "Level 4",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level4  },  0, 0, VE,  .unit = "level" },
    { "4.1",   "Level 4.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level41 },  0, 0, VE,  .unit = "level" },
    { "5",     "Level 5",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level5  },  0, 0, VE,  .unit = "level" },
    { "5.1",   "Level 5.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level51 },  0, 0, VE,  .unit = "level" },
    { "5.2",   "Level 5.2",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level52 },  0, 0, VE,  .unit = "level" },
    { "6",     "Level 6",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level6  },  0, 0, VE,  .unit = "level" },
    { "6.1",   "Level 4.1",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level61 },  0, 0, VE,  .unit = "level" },
    { "6.2",   "Level 6.2",
                0, AV_OPT_TYPE_CONST, { .i64 = VP9Level62 },  0, 0, VE,  .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(vp9, "VP9", AV_CODEC_ID_VP9)

#endif  // CONFIG_VP9_MEDIACODEC_ENCODER

#if CONFIG_MPEG4_MEDIACODEC_ENCODER

enum MediaCodecMpeg4Level {
    MPEG4Level0  = 0x01,
    MPEG4Level0b  = 0x02,
    MPEG4Level1 = 0x04,
    MPEG4Level2  = 0x08,
    MPEG4Level3 = 0x10,
    MPEG4Level3b = 0x18,
    MPEG4Level4 = 0x20,
    MPEG4Level4a  = 0x40,
    MPEG4Level5  = 0x80,
    MPEG4Level6 = 0x100,
};

static const AVOption mpeg4_options[] = {
    COMMON_OPTION

    FF_MPEG4_PROFILE_OPTS

    { "level", "Specify tier and level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "0",     "Level 0",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level0  },  0, 0, VE,  .unit = "level" },
    { "0b",    "Level 0b",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level0b },  0, 0, VE,  .unit = "level" },
    { "1",     "Level 1",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level1  },  0, 0, VE,  .unit = "level" },
    { "2",     "Level 2",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level2 },  0, 0, VE,  .unit = "level" },
    { "3",     "Level 3",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level3  },  0, 0, VE,  .unit = "level" },
    { "3b",    "Level 3b",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level3b },  0, 0, VE,  .unit = "level" },
    { "4",     "Level 4",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level4  },  0, 0, VE,  .unit = "level" },
    { "4a",    "Level 4a",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level4a },  0, 0, VE,  .unit = "level" },
    { "5",     "Level 5",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level5  },  0, 0, VE,  .unit = "level" },
    { "6",     "Level 6",
                0, AV_OPT_TYPE_CONST, { .i64 = MPEG4Level6  },  0, 0, VE,  .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(mpeg4, "MPEG-4", AV_CODEC_ID_MPEG4)

#endif  // CONFIG_MPEG4_MEDIACODEC_ENCODER

#if CONFIG_AV1_MEDIACODEC_ENCODER

enum MediaCodecAV1Level {
    AV1Level2  = 0x1,
    AV1Level21 = 0x2,
    AV1Level22 = 0x4,
    AV1Level23 = 0x8,
    AV1Level3  = 0x10,
    AV1Level31 = 0x20,
    AV1Level32 = 0x40,
    AV1Level33 = 0x80,
    AV1Level4  = 0x100,
    AV1Level41 = 0x200,
    AV1Level42 = 0x400,
    AV1Level43 = 0x800,
    AV1Level5  = 0x1000,
    AV1Level51 = 0x2000,
    AV1Level52 = 0x4000,
    AV1Level53 = 0x8000,
    AV1Level6  = 0x10000,
    AV1Level61 = 0x20000,
    AV1Level62 = 0x40000,
    AV1Level63 = 0x80000,
    AV1Level7  = 0x100000,
    AV1Level71 = 0x200000,
    AV1Level72 = 0x400000,
    AV1Level73 = 0x800000,
};

static const AVOption av1_options[] = {
    COMMON_OPTION

    FF_AV1_PROFILE_OPTS

    { "level", "Specify tier and level",
                OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE, .unit = "level" },
    { "2",     "Level 2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level2  },  0, 0, VE,  .unit = "level" },
    { "2.1",    "Level 2.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level21 },  0, 0, VE,  .unit = "level" },
    { "2.2",    "Level 2.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level22 },  0, 0, VE,  .unit = "level" },
    { "2.3",    "Level 2.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level23 },  0, 0, VE,  .unit = "level" },
    { "3",      "Level 3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level3  },  0, 0, VE,  .unit = "level" },
    { "3.1",    "Level 3.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level31 },  0, 0, VE,  .unit = "level" },
    { "3.2",    "Level 3.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level32 },  0, 0, VE,  .unit = "level" },
    { "3.3",    "Level 3.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level33 },  0, 0, VE,  .unit = "level" },
    { "4",      "Level 4",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level4  },  0, 0, VE,  .unit = "level" },
    { "4.1",    "Level 4.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level41 },  0, 0, VE,  .unit = "level" },
    { "4.2",    "Level 4.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level42 },  0, 0, VE,  .unit = "level" },
    { "4.3",    "Level 4.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level43 },  0, 0, VE,  .unit = "level" },
    { "5",      "Level 5",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level5  },  0, 0, VE,  .unit = "level" },
    { "5.1",    "Level 5.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level51 },  0, 0, VE,  .unit = "level" },
    { "5.2",    "Level 5.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level52 },  0, 0, VE,  .unit = "level" },
    { "5.3",    "Level 5.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level53 },  0, 0, VE,  .unit = "level" },
    { "6",      "Level 6",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level6  },  0, 0, VE,  .unit = "level" },
    { "6.1",    "Level 6.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level61 },  0, 0, VE,  .unit = "level" },
    { "6.2",    "Level 6.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level62 },  0, 0, VE,  .unit = "level" },
    { "6.3",    "Level 6.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level63 },  0, 0, VE,  .unit = "level" },
    { "7",      "Level 7",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level7  },  0, 0, VE,  .unit = "level" },
    { "7.1",    "Level 7.1",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level71 },  0, 0, VE,  .unit = "level" },
    { "7.2",    "Level 7.2",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level72 },  0, 0, VE,  .unit = "level" },
    { "7.3",    "Level 7.3",
                0, AV_OPT_TYPE_CONST, { .i64 = AV1Level73 },  0, 0, VE,  .unit = "level" },
    { NULL, }
};

DECLARE_MEDIACODEC_ENCODER(av1, "AV1", AV_CODEC_ID_AV1)

#endif  // CONFIG_AV1_MEDIACODEC_ENCODER
