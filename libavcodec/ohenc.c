/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#include <stdbool.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <native_window/external_window.h>

#include "libavutil/fifo.h"
#include "libavutil/hwcontext_oh.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "hwconfig.h"
#include "ohcodec.h"

typedef struct OHCodecEncContext {
    AVClass *avclass;
    OH_AVCodec *enc;

    AVMutex input_mutex;
    AVCond input_cond;
    AVFifo *input_queue;

    AVMutex output_mutex;
    AVCond output_cond;
    AVFifo *output_queue;

    AVFrame *frame;
    uint8_t *extradata;
    int extradata_size;

    int encode_status;
    bool eof_sent;

    bool got_stream_info;
    int stride;
    int slice_height;

    OHNativeWindow *native_window;
    char *name;
    int allow_sw;
    int bitrate_mode;
} OHCodecEncContext;

static const enum AVPixelFormat ohcodec_pix_fmts[] = {
    AV_PIX_FMT_OHCODEC,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NONE
};

static int oh_encode_create(OHCodecEncContext *s, AVCodecContext *avctx)
{
    const char *name = s->name;

    if (!name) {
        const char *mime = ff_oh_mime(avctx->codec_id, avctx);
        if (!mime)
            return AVERROR_BUG;
        OH_AVCapability *cap = OH_AVCodec_GetCapabilityByCategory(mime, true, HARDWARE);
        if (!cap) {
            if (!s->allow_sw) {
                av_log(avctx, AV_LOG_ERROR, "Failed to get hardware codec %s\n", mime);
                return AVERROR_EXTERNAL;
            }
            av_log(avctx, AV_LOG_WARNING,
                   "Failed to get hardware codec %s, try software backend\n", mime);
            cap = OH_AVCodec_GetCapabilityByCategory(mime, true, SOFTWARE);
            if (!cap) {
                av_log(avctx, AV_LOG_ERROR, "Failed to get software codec %s\n", mime);
                return AVERROR_EXTERNAL;
            }
        }
        name = OH_AVCapability_GetName(cap);
        if (!name)
            return AVERROR_EXTERNAL;
    }

    s->enc = OH_VideoEncoder_CreateByName(name);
    if (!s->enc) {
        av_log(avctx, AV_LOG_ERROR, "Create encoder with name %s failed\n", name);
        return AVERROR_EXTERNAL;
    }
    av_log(avctx, AV_LOG_DEBUG, "Create encoder %s success\n", name);

    return 0;
}

static int oh_encode_set_format(OHCodecEncContext *s, AVCodecContext *avctx)
{
    int ret;

    OH_AVFormat *format = OH_AVFormat_Create();
    if (!format)
        return AVERROR(ENOMEM);

    bool b = OH_AVFormat_SetIntValue(format, OH_MD_KEY_WIDTH, avctx->width);
    b = b && OH_AVFormat_SetIntValue(format, OH_MD_KEY_HEIGHT, avctx->height);
    if (!b) {
        av_log(avctx, AV_LOG_ERROR, "Set width/height (%dx%d) failed\n",
               avctx->width, avctx->height);
        ret = AVERROR_EXTERNAL;
        goto out;
    }
    if (avctx->framerate.num && avctx->framerate.den)
        OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE,
                                   av_q2d(avctx->framerate));
    int pix = ff_oh_pix_from_ff_pix(avctx->pix_fmt);
    if (!pix) {
        ret = AVERROR_BUG;
        goto out;
    }
    b = OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pix);
    if (!b) {
        av_log(avctx, AV_LOG_ERROR, "Set pixel format to %d failed\n", pix);
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    if (s->bitrate_mode != -1) {
        b = OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, s->bitrate_mode);
        if (!b) {
            av_log(avctx, AV_LOG_ERROR, "Set bitrate mode to %d failed\n",
                   s->bitrate_mode);
            ret = AVERROR_EXTERNAL;
            goto out;
        }
    }
    OH_AVFormat_SetLongValue(format, OH_MD_KEY_BITRATE, avctx->bit_rate);

    if (avctx->gop_size > 0) {
        if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
            // In milliseconds
            int gop = av_rescale_q(avctx->gop_size,
                                   av_make_q(avctx->framerate.den,
                                             avctx->framerate.num),
                                   av_make_q(1, 1000));
            OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, gop);
        } else {
            av_log(avctx, AV_LOG_WARNING, "Skip setting gop without framerate\n");
        }
    } else if (!avctx->gop_size) {
        // All frames are key frame
        OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, 0);
    } else if (avctx->gop_size == -1) {
        // Infinite gop
        OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, -1);
    }

    OH_AVErrCode err = OH_VideoEncoder_Configure(s->enc, format);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Decoder configure failed, %d, %s\n",
               err, av_err2str(ret));
        goto out;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_OHCODEC) {
        if (avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR,
                   "ohcodec can only export native window via hw device, "
                   "doesn't support import hw device\n");
            ret = AVERROR(EINVAL);
            goto out;
        }

        err = OH_VideoEncoder_GetSurface(s->enc, &s->native_window);
        if (err != AV_ERR_OK) {
            ret = ff_oh_err_to_ff_err(err);
            av_log(avctx, AV_LOG_ERROR, "Get surface failed, %d, %s\n",
                   err, av_err2str(ret));
            goto out;
        }
        av_log(avctx, AV_LOG_INFO, "Native window %p\n", s->native_window);

        ret = av_hwdevice_ctx_create(&avctx->hw_device_ctx,
                                     AV_HWDEVICE_TYPE_OHCODEC, NULL, NULL, 0);
        if (ret < 0)
            goto out;

        AVOHCodecDeviceContext *dev = ((AVHWDeviceContext *)avctx->hw_device_ctx->data)->hwctx;
        dev->native_window = s->native_window;
    }

    return 0;
out:
    OH_AVFormat_Destroy(format);
    return ret;
}

static void oh_encode_on_err(OH_AVCodec *codec, int32_t err, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecEncContext *s = avctx->priv_data;

    // Careful on the lock order.
    // Always lock input first.
    ff_mutex_lock(&s->input_mutex);
    ff_mutex_lock(&s->output_mutex);
    s->encode_status = ff_oh_err_to_ff_err(err);
    ff_mutex_unlock(&s->output_mutex);
    ff_mutex_unlock(&s->input_mutex);

    ff_cond_signal(&s->output_cond);
    ff_cond_signal(&s->input_cond);
}

static void oh_encode_on_stream_changed(OH_AVCodec *codec, OH_AVFormat *format,
                                        void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecEncContext *s = avctx->priv_data;

    if (!OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, &s->stride))
        s->stride = avctx->width;
    if (!OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT, &s->slice_height))
        s->slice_height = avctx->height;

    s->got_stream_info = true;
}

static void oh_encode_on_need_input(OH_AVCodec *codec, uint32_t index,
                                    OH_AVBuffer *buffer, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecEncContext *s = avctx->priv_data;
    OHBufferQueueItem item = {
        index, buffer,
    };

    ff_mutex_lock(&s->input_mutex);
    int ret = av_fifo_write(s->input_queue, &item, 1);
    if (ret >= 0)
        ff_cond_signal(&s->input_cond);
    ff_mutex_unlock(&s->input_mutex);

    if (ret < 0)
        oh_encode_on_err(codec, AV_ERR_NO_MEMORY, userdata);
}

static void oh_encode_on_output(OH_AVCodec *codec, uint32_t index,
                                OH_AVBuffer *buffer, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecEncContext *s = avctx->priv_data;
    OHBufferQueueItem item = {
        index, buffer,
    };

    ff_mutex_lock(&s->output_mutex);
    int ret = av_fifo_write(s->output_queue, &item, 1);
    if (ret >= 0)
        ff_cond_signal(&s->output_cond);
    ff_mutex_unlock(&s->output_mutex);

    if (ret < 0)
        oh_encode_on_err(codec, AV_ERR_NO_MEMORY, userdata);
}

static int oh_encode_start(OHCodecEncContext *s, AVCodecContext *avctx)
{
    int ret;
    OH_AVErrCode err;
    OH_AVCodecCallback cb = {
        .onError = oh_encode_on_err,
        .onStreamChanged = oh_encode_on_stream_changed,
        .onNeedInputBuffer = oh_encode_on_need_input,
        .onNewOutputBuffer = oh_encode_on_output,
    };

    err = OH_VideoEncoder_RegisterCallback(s->enc, cb, avctx);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Register callback failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }
    err = OH_VideoEncoder_Prepare(s->enc);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Prepare failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }
    err = OH_VideoEncoder_Start(s->enc);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Start failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }

    return 0;
}

static av_cold int oh_encode_init(AVCodecContext *avctx)
{
    OHCodecEncContext *s = avctx->priv_data;

    // Initialize these fields first, so oh_decode_close can destroy them safely
    ff_mutex_init(&s->input_mutex, NULL);
    ff_cond_init(&s->input_cond, NULL);
    ff_mutex_init(&s->output_mutex, NULL);
    ff_cond_init(&s->output_cond, NULL);

    int ret = oh_encode_create(s, avctx);
    if (ret < 0)
        return ret;
    ret = oh_encode_set_format(s, avctx);
    if (ret < 0)
        return ret;

    size_t fifo_size = 16;
    s->input_queue = av_fifo_alloc2(fifo_size, sizeof(OHBufferQueueItem),
                                    AV_FIFO_FLAG_AUTO_GROW);
    s->output_queue = av_fifo_alloc2(fifo_size, sizeof(OHBufferQueueItem),
                                     AV_FIFO_FLAG_AUTO_GROW);
    s->frame = av_frame_alloc();
    if (!s->input_queue || !s->output_queue || !s->frame)
        return AVERROR(ENOMEM);

    ret = oh_encode_start(s, avctx);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold int oh_encode_close(AVCodecContext *avctx)
{
    OHCodecEncContext *s = avctx->priv_data;

    if (s->enc) {
        if (s->native_window) {
            OH_NativeWindow_DestroyNativeWindow(s->native_window);
            s->native_window = NULL;
        }
        OH_VideoEncoder_Stop(s->enc);
        OH_AVErrCode err = OH_VideoEncoder_Destroy(s->enc);
        if (err == AV_ERR_OK)
            av_log(avctx, AV_LOG_DEBUG, "Destroy encoder success\n");
        else
            av_log(avctx, AV_LOG_ERROR, "Destroy decoder failed, %d, %s\n",
                   err, av_err2str(ff_oh_err_to_ff_err(err)));
        s->enc = NULL;
    }

    av_freep(&s->extradata);
    av_frame_free(&s->frame);

    ff_mutex_destroy(&s->input_mutex);
    ff_cond_destroy(&s->input_cond);
    av_fifo_freep2(&s->input_queue);

    ff_mutex_destroy(&s->output_mutex);
    ff_cond_destroy(&s->output_cond);
    av_fifo_freep2(&s->output_queue);

    return 0;
}

static int oh_encode_output_packet(AVCodecContext *avctx, AVPacket *pkt,
                                  OHBufferQueueItem *output)
{
    OHCodecEncContext *s = avctx->priv_data;
    uint8_t *p;
    OH_AVCodecBufferAttr attr;
    int ret;

    OH_AVErrCode err = OH_AVBuffer_GetBufferAttr(output->buffer, &attr);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        goto out;
    }
    if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) {
        av_log(avctx, AV_LOG_DEBUG, "Buffer flag eos\n");
        ret = AVERROR_EOF;
        goto out;
    }

    p = OH_AVBuffer_GetAddr(output->buffer);
    if (!p) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get output buffer addr\n");
        ret = AVERROR_EXTERNAL;
        goto out;
    }
    if (attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) {
        av_freep(&s->extradata);
        s->extradata = av_malloc(attr.size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!s->extradata) {
            ret = AVERROR(ENOMEM);
            goto out;
        }
        memset(s->extradata + attr.size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(s->extradata, p + attr.offset, attr.size);
        s->extradata_size = attr.size;
        ret = 0;
        goto out;
    }

    int64_t extradata_size = s->extradata_size;
    s->extradata_size = 0;

    if (extradata_size && (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
        ret = av_packet_add_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                      s->extradata, extradata_size);
        if (ret < 0)
            goto out;
        s->extradata = NULL;
        extradata_size = 0;
    }

    ret = ff_get_encode_buffer(avctx, pkt, attr.size + extradata_size, 0);
    if (ret < 0)
        goto out;

    if (extradata_size)
        memcpy(pkt->data, s->extradata, extradata_size);

    memcpy(pkt->data + extradata_size, p + attr.offset, attr.size);
    pkt->pts = av_rescale_q(attr.pts, AV_TIME_BASE_Q, avctx->time_base);
    if (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME)
        pkt->flags |= AV_PKT_FLAG_KEY;
    ret = 0;
out:
    OH_VideoEncoder_FreeOutputBuffer(s->enc, output->index);
    return ret;
}

static int oh_encode_send_hw_frame(AVCodecContext *avctx)
{
    OHCodecEncContext *s = avctx->priv_data;

    if (s->eof_sent)
        return 0;

    if (s->frame->buf[0]) {
        av_frame_unref(s->frame);
        return 0;
    }

    OH_AVErrCode err = OH_VideoEncoder_NotifyEndOfStream(s->enc);
    s->eof_sent = true;
    return ff_oh_err_to_ff_err(err);
}

static int oh_encode_send_sw_frame(AVCodecContext *avctx, OHBufferQueueItem *input)
{
    OHCodecEncContext *s = avctx->priv_data;
    AVFrame *frame = s->frame;
    OH_AVErrCode err;
    int ret;

    if (!s->got_stream_info) {
        // This shouldn't happen, add a warning message.
        av_log(avctx, AV_LOG_WARNING,
               "decoder didn't notify stream info, try get format explicitly\n");

        OH_AVFormat *format = OH_VideoEncoder_GetOutputDescription(s->enc);
        if (!format) {
            av_log(avctx, AV_LOG_ERROR, "GetOutputDescription failed\n");
            return AVERROR_EXTERNAL;
        }

        oh_encode_on_stream_changed(s->enc, format, avctx);
        OH_AVFormat_Destroy(format);
        if (!s->got_stream_info)
            return AVERROR_EXTERNAL;
    }

    if (!frame->buf[0] && !s->eof_sent) {
        OH_AVCodecBufferAttr attr = {
            .flags = AVCODEC_BUFFER_FLAGS_EOS,
        };
        err = OH_AVBuffer_SetBufferAttr(input->buffer, &attr);
        if (err != AV_ERR_OK)
            return ff_oh_err_to_ff_err(err);
        err = OH_VideoEncoder_PushInputBuffer(s->enc, input->index);
        if (err != AV_ERR_OK)
            return ff_oh_err_to_ff_err(err);
        s->eof_sent = true;
        return 0;
    }

    uint8_t *p = OH_AVBuffer_GetAddr(input->buffer);
    int32_t n = OH_AVBuffer_GetCapacity(input->buffer);
    if (!p || n <= 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get buffer addr (%p) or capacity (%d)\n",
               p, n);
        return AVERROR_EXTERNAL;
    }

    uint8_t *dst[4] = {0};
    int dst_linesizes[4] = {0};
    ret = av_image_fill_linesizes(dst_linesizes, frame->format, s->stride);
    if (ret < 0)
        return ret;
    ret = av_image_fill_pointers(dst, frame->format, s->slice_height, p,
                                 dst_linesizes);
    if (ret < 0)
        return ret;

    av_image_copy2(dst, dst_linesizes, frame->data, frame->linesize,
                   frame->format, frame->width, frame->height);
    OH_AVCodecBufferAttr attr = {
        .size = n,
        .offset = 0,
        .pts = av_rescale_q(s->frame->pts, avctx->pkt_timebase,
                            AV_TIME_BASE_Q),
        .flags = (s->frame->flags & AV_FRAME_FLAG_KEY)
                 ? AVCODEC_BUFFER_FLAGS_SYNC_FRAME : 0,
    };

    err = OH_AVBuffer_SetBufferAttr(input->buffer, &attr);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        return ret;
    }
    err = OH_VideoEncoder_PushInputBuffer(s->enc, input->index);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Push input buffer failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }
    av_frame_unref(s->frame);

    return 0;
}

static int oh_encode_receive(AVCodecContext *avctx, AVPacket *pkt)
{
    OHCodecEncContext *s = avctx->priv_data;

    while (1) {
        OHBufferQueueItem buffer = {0};
        int ret;

        // Try get output
        ff_mutex_lock(&s->output_mutex);
        while (!s->encode_status) {
            if (av_fifo_read(s->output_queue, &buffer, 1) >= 0)
                break;
            // Only wait after send EOF
            if (s->eof_sent && !s->encode_status)
                ff_cond_wait(&s->output_cond, &s->output_mutex);
            else
                break;
        }

        ret = s->encode_status;
        ff_mutex_unlock(&s->output_mutex);

        // Got a packet
        if (buffer.buffer)
            return oh_encode_output_packet(avctx, pkt, &buffer);
        if (ret < 0)
            return ret;

        if (!s->frame->buf[0]) {
            /* fetch new frame or eof */
            ret = ff_encode_get_frame(avctx, s->frame);
            if (ret < 0 && ret != AVERROR_EOF)
                return ret;
        }

        if (s->native_window) {
            ret = oh_encode_send_hw_frame(avctx);
            if (ret < 0)
                return ret;
            continue;
        }

        // Wait input buffer
        ff_mutex_lock(&s->input_mutex);
        while (!s->encode_status) {
            if (av_fifo_read(s->input_queue, &buffer, 1) >= 0)
                break;
            ff_cond_wait(&s->input_cond, &s->input_mutex);
        }

        ret = s->encode_status;
        ff_mutex_unlock(&s->input_mutex);

        if (ret < 0)
            return ret;

        ret = oh_encode_send_sw_frame(avctx, &buffer);
        if (ret < 0)
            return ret;
    }

    return AVERROR(EAGAIN);
}

static void oh_encode_flush(AVCodecContext *avctx)
{
    OHCodecEncContext *s = avctx->priv_data;

    OH_VideoEncoder_Flush(s->enc);

    ff_mutex_lock(&s->input_mutex);
    ff_mutex_lock(&s->output_mutex);
    av_fifo_reset2(s->input_queue);
    av_fifo_reset2(s->output_queue);
    s->encode_status = 0;
    s->eof_sent = false;
    ff_mutex_unlock(&s->output_mutex);
    ff_mutex_unlock(&s->input_mutex);

    OH_VideoEncoder_Start(s->enc);
}

static const AVCodecHWConfigInternal *const oh_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt = AV_PIX_FMT_OHCODEC,
            .methods = AV_CODEC_HW_CONFIG_METHOD_AD_HOC,
        },
        .hwaccel = NULL,
    },
    NULL
};

static const FFCodecDefault ohcodec_defaults[] = {
    {"g", "-2"},
    {NULL},
};

#define OFFSET(x) offsetof(OHCodecEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption ohcodec_venc_options[] = {
    {"codec_name", "Select codec by name",
        OFFSET(name), AV_OPT_TYPE_STRING, .flags = VE},
    {"allow_sw", "Allow software encoding",
        OFFSET(allow_sw), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VE},
    {"bitrate_mode", "Bitrate control method",
        OFFSET(bitrate_mode), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, VE, .unit = "bitrate_mode"},
        {"cbr", "Constant bitrate mode",
            0, AV_OPT_TYPE_CONST, {.i64 = CBR}, 0, 0, VE, .unit = "bitrate_mode"},
        {"vbr", "Variable bitrate mode",
            0, AV_OPT_TYPE_CONST, {.i64 = VBR}, 0, 0, VE, .unit = "bitrate_mode"},
        {"cq", "Constant quality mode",
            0, AV_OPT_TYPE_CONST, {.i64 = CQ}, 0, 0, VE, .unit = "bitrate_mode"},
    {NULL},
};

#define DECLARE_OHCODEC_CLASS(name)                 \
static const AVClass name ## _oh_enc_class = {      \
    .class_name = #name "_ohcodec",                 \
    .item_name  = av_default_item_name,             \
    .option     = ohcodec_venc_options,             \
    .version    = LIBAVUTIL_VERSION_INT,            \
};                                                  \

#define DECLARE_OHCODEC_ENCODER(short_name, long_name, codec_id)        \
DECLARE_OHCODEC_CLASS(short_name)                                       \
const FFCodec ff_ ## short_name ## _oh_encoder = {                      \
    .p.name           = #short_name "_ohcodec",                         \
    CODEC_LONG_NAME(long_name " OpenHarmony Codec"),                    \
    .p.type           = AVMEDIA_TYPE_VIDEO,                             \
    .p.id             = codec_id,                                       \
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |         \
                        AV_CODEC_CAP_HARDWARE |                         \
                        AV_CODEC_CAP_ENCODER_FLUSH,                     \
    .priv_data_size   = sizeof(OHCodecEncContext),                      \
    CODEC_PIXFMTS_ARRAY(ohcodec_pix_fmts),                              \
    .color_ranges     = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,            \
    .defaults         = ohcodec_defaults,                               \
    .init             = oh_encode_init,                                 \
    FF_CODEC_RECEIVE_PACKET_CB(oh_encode_receive),                      \
    .close            = oh_encode_close,                                \
    .flush            = oh_encode_flush,                                \
    .p.priv_class     = &short_name ## _oh_enc_class,                   \
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,                      \
    .p.wrapper_name   = "ohcodec",                                      \
    .hw_configs       = oh_hw_configs,                                  \
};                                                                      \

#if CONFIG_H264_OH_ENCODER
DECLARE_OHCODEC_ENCODER(h264, "H.264", AV_CODEC_ID_H264)
#endif  // CONFIG_H264_OH_ENCODER

#if CONFIG_HEVC_OH_ENCODER
DECLARE_OHCODEC_ENCODER(hevc, "H.265", AV_CODEC_ID_HEVC)
#endif  // CONFIG_HEVC_OH_ENCODER
