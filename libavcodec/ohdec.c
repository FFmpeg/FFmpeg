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
#include <multimedia/player_framework/native_avcodec_videodecoder.h>

#include "libavutil/fifo.h"
#include "libavutil/hwcontext_oh.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwconfig.h"
#include "ohcodec.h"

typedef struct OHCodecDecContext {
    AVClass *avclass;
    OH_AVCodec *dec;
    /* A reference count to dec. Each hardware frame has a reference count to
     * dec. dec will be destroyed only after oh_decode_close and all hardware
     * frames have been released.
     */
    AVBufferRef *dec_ref;

    AVMutex input_mutex;
    AVCond input_cond;
    AVFifo *input_queue;

    AVMutex output_mutex;
    AVCond output_cond;
    AVFifo *output_queue;

    AVPacket pkt;

    int decode_status;
    bool eof_sent;

    bool output_to_window;
    bool got_stream_info;
    int width;
    int height;
    int stride;
    int slice_height;
    OH_AVPixelFormat pix_fmt;

    char *name;
    int allow_sw;
} OHCodecDecContext;

typedef struct OHCodecBuffer {
    uint32_t index;
    OH_AVBuffer *buffer;
    AVBufferRef *dec_ref;
} OHCodecBuffer;

static void oh_decode_release(void *opaque, uint8_t *data)
{
    OH_AVCodec *dec = (OH_AVCodec *)data;
    OH_AVErrCode err = OH_VideoDecoder_Destroy(dec);
    if (err == AV_ERR_OK)
        av_log(NULL, AV_LOG_DEBUG, "Destroy decoder success\n");
    else
        av_log(NULL, AV_LOG_ERROR, "Destroy decoder failed, %d, %s\n",
               err, av_err2str(ff_oh_err_to_ff_err(err)));
}

static int oh_decode_create(OHCodecDecContext *s, AVCodecContext *avctx)
{
    const char *name = s->name;

    if (!name) {
        const char *mime = ff_oh_mime(avctx->codec_id, avctx);
        if (!mime)
            return AVERROR_BUG;
        OH_AVCapability *cap = OH_AVCodec_GetCapabilityByCategory(mime, false, HARDWARE);
        if (!cap) {
            if (!s->allow_sw) {
                av_log(avctx, AV_LOG_ERROR, "Failed to get hardware codec %s\n", mime);
                return AVERROR_EXTERNAL;
            }
            av_log(avctx, AV_LOG_WARNING,
                   "Failed to get hardware codec %s, try software backend\n", mime);
            cap = OH_AVCodec_GetCapabilityByCategory(mime, false, SOFTWARE);
            if (!cap) {
                av_log(avctx, AV_LOG_ERROR, "Failed to get software codec %s\n", mime);
                return AVERROR_EXTERNAL;
            }
        }
        name = OH_AVCapability_GetName(cap);
        if (!name)
            return AVERROR_EXTERNAL;
    }

    s->dec = OH_VideoDecoder_CreateByName(name);
    if (!s->dec) {
        av_log(avctx, AV_LOG_ERROR, "Create decoder with name %s failed\n", name);
        return AVERROR_EXTERNAL;
    }
    av_log(avctx, AV_LOG_DEBUG, "Create decoder %s success\n", name);

    s->dec_ref = av_buffer_create((uint8_t *)s->dec, 0, oh_decode_release,
                                  NULL, 0);
    if (!s->dec_ref)
        return AVERROR(ENOMEM);

    return 0;
}

static int oh_decode_set_format(OHCodecDecContext *s, AVCodecContext *avctx)
{
    int ret;
    OHNativeWindow *window = NULL;

    if (avctx->hw_device_ctx) {
        AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)(avctx->hw_device_ctx->data);
        if (device_ctx->type == AV_HWDEVICE_TYPE_OHCODEC) {
            AVOHCodecDeviceContext *dev = device_ctx->hwctx;
            window = dev->native_window;
            s->output_to_window = true;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Ignore invalid hw device type %s\n",
                   av_hwdevice_get_type_name(device_ctx->type));
        }
    }

    if (avctx->width <= 0 || avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid width/height (%dx%d), width and height are mandatory for ohcodec\n",
               avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }

    OH_AVFormat *format = OH_AVFormat_Create();
    if (!format)
        return AVERROR(ENOMEM);

    OH_AVFormat_SetIntValue(format, OH_MD_KEY_WIDTH, avctx->width);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_HEIGHT, avctx->height);
    if (!s->output_to_window)
        OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT,
                                AV_PIXEL_FORMAT_NV12);
    else
        OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT,
                                AV_PIXEL_FORMAT_SURFACE_FORMAT);
    OH_AVErrCode err = OH_VideoDecoder_Configure(s->dec, format);
    OH_AVFormat_Destroy(format);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Decoder configure failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }

    if (s->output_to_window) {
        err = OH_VideoDecoder_SetSurface(s->dec, window);
        if (err != AV_ERR_OK) {
            ret = ff_oh_err_to_ff_err(err);
            av_log(avctx, AV_LOG_ERROR, "Set surface failed, %d, %s\n",
                   err, av_err2str(ret));
            return ret;
        }
    }

    return 0;
}

static void oh_decode_on_err(OH_AVCodec *codec, int32_t err, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecDecContext *s = avctx->priv_data;

    // Careful on the lock order.
    // Always lock input first.
    ff_mutex_lock(&s->input_mutex);
    ff_mutex_lock(&s->output_mutex);
    s->decode_status = ff_oh_err_to_ff_err(err);
    ff_mutex_unlock(&s->output_mutex);
    ff_mutex_unlock(&s->input_mutex);

    ff_cond_signal(&s->output_cond);
    ff_cond_signal(&s->input_cond);
}

static void oh_decode_on_stream_changed(OH_AVCodec *codec, OH_AVFormat *format,
                                        void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecDecContext *s = avctx->priv_data;
    int32_t n;
    double d;

    if (!OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_PIC_WIDTH, &s->width) ||
        !OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_PIC_HEIGHT, &s->height) ||
        !OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, &s->stride) ||
        !OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT,
                                 &s->slice_height)) {
        av_log(avctx, AV_LOG_ERROR, "Get dimension info from format failed\n");
        goto out;
    }

    if (ff_set_dimensions(avctx, s->width, s->height) < 0)
        goto out;

    if (s->stride <= 0 || s->slice_height <= 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Buffer stride (%d) or slice height (%d) is invalid\n",
               s->stride, s->slice_height);
        goto out;
    }

    if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, &n)) {
        s->pix_fmt = n;
        /* When use output_to_window, the returned format is the memory
         * layout of hardware frame, not AV_PIXEL_FORMAT_SURFACE_FORMAT as
         * expected.
         */
        if (s->output_to_window)
            avctx->pix_fmt = AV_PIX_FMT_OHCODEC;
        else
            avctx->pix_fmt = ff_oh_pix_to_ff_pix(s->pix_fmt);
        // Check whether this pixel format is supported
        if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported OH_AVPixelFormat %d\n",
                   n);
            goto out;
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Failed to get pixel format\n");
        goto out;
    }

    if (OH_AVFormat_GetIntValue(format,
                                OH_MD_KEY_MATRIX_COEFFICIENTS,
                                &n))
        avctx->colorspace = n;
    if (OH_AVFormat_GetIntValue(format,
                                OH_MD_KEY_COLOR_PRIMARIES,
                                &n))
        avctx->color_primaries = n;
    if (OH_AVFormat_GetIntValue(format,
                                OH_MD_KEY_TRANSFER_CHARACTERISTICS,
                                &n))
        avctx->color_trc = n;
    if (OH_AVFormat_GetIntValue(format,
                                OH_MD_KEY_RANGE_FLAG,
                                &n))
        avctx->color_range = n ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (OH_AVFormat_GetDoubleValue(format, OH_MD_KEY_VIDEO_SAR, &d)) {
        AVRational sar = av_d2q(d, 4096 * 4);
        ff_set_sar(avctx, sar);
    }

    s->got_stream_info = true;

    return;
out:
    av_log(avctx, AV_LOG_ERROR, "Invalid format from decoder: %s\n",
           OH_AVFormat_DumpInfo(format));
    oh_decode_on_err(codec, AV_ERR_UNKNOWN, userdata);
}

static void oh_decode_on_need_input(OH_AVCodec *codec, uint32_t index,
                                    OH_AVBuffer *buffer, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecDecContext *s = avctx->priv_data;
    OHBufferQueueItem item = {
        index, buffer,
    };

    ff_mutex_lock(&s->input_mutex);
    int ret = av_fifo_write(s->input_queue, &item, 1);
    if (ret >= 0)
        ff_cond_signal(&s->input_cond);
    ff_mutex_unlock(&s->input_mutex);

    if (ret < 0)
        oh_decode_on_err(codec, AV_ERR_NO_MEMORY, userdata);
}

static void oh_decode_on_output(OH_AVCodec *codec, uint32_t index,
                                OH_AVBuffer *buffer, void *userdata)
{
    AVCodecContext *avctx = userdata;
    OHCodecDecContext *s = avctx->priv_data;
    OHBufferQueueItem item = {
        index, buffer,
    };

    ff_mutex_lock(&s->output_mutex);
    int ret = av_fifo_write(s->output_queue, &item, 1);
    if (ret >= 0)
        ff_cond_signal(&s->output_cond);
    ff_mutex_unlock(&s->output_mutex);

    if (ret < 0)
        oh_decode_on_err(codec, AV_ERR_NO_MEMORY, userdata);
}

static int oh_decode_start(OHCodecDecContext *s, AVCodecContext *avctx)
{
    int ret;
    OH_AVErrCode err;
    OH_AVCodecCallback cb = {
        .onError = oh_decode_on_err,
        .onStreamChanged = oh_decode_on_stream_changed,
        .onNeedInputBuffer = oh_decode_on_need_input,
        .onNewOutputBuffer = oh_decode_on_output,
    };

    err = OH_VideoDecoder_RegisterCallback(s->dec, cb, avctx);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Register callback failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }
    err = OH_VideoDecoder_Prepare(s->dec);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Prepare failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }
    err = OH_VideoDecoder_Start(s->dec);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Start failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }

    return 0;
}

static av_cold int oh_decode_init(AVCodecContext *avctx)
{
    OHCodecDecContext *s = avctx->priv_data;

    // Initialize these fields first, so oh_decode_close can destroy them safely
    ff_mutex_init(&s->input_mutex, NULL);
    ff_cond_init(&s->input_cond, NULL);
    ff_mutex_init(&s->output_mutex, NULL);
    ff_cond_init(&s->output_cond, NULL);

    int ret = oh_decode_create(s, avctx);
    if (ret < 0)
        return ret;
    ret = oh_decode_set_format(s, avctx);
    if (ret < 0)
        return ret;

    size_t fifo_size = 16;
    s->input_queue = av_fifo_alloc2(fifo_size, sizeof(OHBufferQueueItem),
                                    AV_FIFO_FLAG_AUTO_GROW);
    s->output_queue = av_fifo_alloc2(fifo_size, sizeof(OHBufferQueueItem),
                                     AV_FIFO_FLAG_AUTO_GROW);
    if (!s->input_queue || !s->output_queue)
        return AVERROR(ENOMEM);

    ret = oh_decode_start(s, avctx);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold int oh_decode_close(AVCodecContext *avctx)
{
    OHCodecDecContext *s = avctx->priv_data;

    if (s->dec) {
        /* Stop but don't destroy dec directly, to keep hardware frames on
         * the fly valid.
         */
        OH_AVErrCode err = OH_VideoDecoder_Stop(s->dec);
        if (err == AV_ERR_OK)
            av_log(avctx, AV_LOG_DEBUG, "Stop decoder success\n");
        else
            av_log(avctx, AV_LOG_ERROR, "Stop decoder failed, %d, %s\n",
                   err, av_err2str(ff_oh_err_to_ff_err(err)));
        s->dec = NULL;
        av_buffer_unref(&s->dec_ref);
    }

    av_packet_unref(&s->pkt);

    ff_mutex_destroy(&s->input_mutex);
    ff_cond_destroy(&s->input_cond);
    av_fifo_freep2(&s->input_queue);

    ff_mutex_destroy(&s->output_mutex);
    ff_cond_destroy(&s->output_cond);
    av_fifo_freep2(&s->output_queue);

    return 0;
}

static void oh_buffer_release(void *opaque, uint8_t *data)
{
    if (!opaque)
        return;

    OHCodecBuffer *buffer = opaque;

    if (!buffer->dec_ref) {
        av_free(buffer);
        return;
    }

    if (buffer->buffer) {
        OH_AVCodec *dec = (OH_AVCodec *)buffer->dec_ref->data;
        OH_AVCodecBufferAttr attr;
        OH_AVErrCode err = OH_AVBuffer_GetBufferAttr(buffer->buffer, &attr);
        if (err == AV_ERR_OK && !(attr.flags & AVCODEC_BUFFER_FLAGS_DISCARD))
            OH_VideoDecoder_RenderOutputBuffer(dec, buffer->index);
        else
            OH_VideoDecoder_FreeOutputBuffer(dec, buffer->index);
    }

    av_buffer_unref(&buffer->dec_ref);
    av_free(buffer);
}

static int oh_decode_wrap_hw_buffer(AVCodecContext *avctx, AVFrame *frame,
                                    OHBufferQueueItem *output,
                                    const OH_AVCodecBufferAttr *attr)
{
    OHCodecDecContext *s = avctx->priv_data;

    frame->width = s->width;
    frame->height = s->height;
    int ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0)
        return ret;

    frame->format = AV_PIX_FMT_OHCODEC;
    OHCodecBuffer *buffer = av_mallocz(sizeof(*buffer));
    if (!buffer)
        return AVERROR(ENOMEM);

    buffer->dec_ref = av_buffer_ref(s->dec_ref);
    if (!buffer->dec_ref) {
        oh_buffer_release(buffer, NULL);
        return AVERROR(ENOMEM);
    }

    buffer->index = output->index;
    buffer->buffer = output->buffer;
    frame->buf[0] = av_buffer_create((uint8_t *)buffer->buffer, 1,
                                     oh_buffer_release,
                                     buffer, AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        oh_buffer_release(buffer, NULL);
        return AVERROR(ENOMEM);
    }
    // Point to OH_AVBuffer
    frame->data[3] = frame->buf[0]->data;
    frame->pts = av_rescale_q(attr->pts, AV_TIME_BASE_Q, avctx->pkt_timebase);
    frame->pkt_dts = AV_NOPTS_VALUE;

    return 0;
}

static int oh_decode_wrap_sw_buffer(AVCodecContext *avctx, AVFrame *frame,
                                    OHBufferQueueItem *output,
                                    const OH_AVCodecBufferAttr *attr)
{
    OHCodecDecContext *s = avctx->priv_data;

    frame->format = avctx->pix_fmt;
    frame->width = s->width;
    frame->height = s->height;
    int ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    frame->pts = av_rescale_q(attr->pts, AV_TIME_BASE_Q, avctx->pkt_timebase);
    frame->pkt_dts = AV_NOPTS_VALUE;

    uint8_t *p = OH_AVBuffer_GetAddr(output->buffer);
    if (!p) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get output buffer addr\n");
        return AVERROR_EXTERNAL;
    }

    uint8_t *src[4] = {0};
    int src_linesizes[4] = {0};

    ret = av_image_fill_linesizes(src_linesizes, frame->format, s->stride);
    if (ret < 0)
        return ret;
    ret = av_image_fill_pointers(src, frame->format, s->slice_height, p,
                                 src_linesizes);
    if (ret < 0)
        return ret;
    av_image_copy2(frame->data, frame->linesize, src, src_linesizes,
                   frame->format, frame->width, frame->height);

    OH_AVErrCode err = OH_VideoDecoder_FreeOutputBuffer(s->dec, output->index);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "FreeOutputBuffer failed, %d, %s\n", err,
               av_err2str(ret));
        return ret;
    }

    return 0;
}

static int oh_decode_output_frame(AVCodecContext *avctx, AVFrame *frame,
                                  OHBufferQueueItem *output)
{
    OHCodecDecContext *s = avctx->priv_data;
    OH_AVCodecBufferAttr attr;

    OH_AVErrCode err = OH_AVBuffer_GetBufferAttr(output->buffer, &attr);
    if (err != AV_ERR_OK)
        return ff_oh_err_to_ff_err(err);

    if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) {
        av_log(avctx, AV_LOG_DEBUG, "Buffer flag eos\n");
        OH_VideoDecoder_FreeOutputBuffer(s->dec, output->index);
        return AVERROR_EOF;
    }

    if (!s->got_stream_info) {
        // This shouldn't happen, add a warning message.
        av_log(avctx, AV_LOG_WARNING,
               "decoder didn't notify stream info, try get format explicitly\n");

        OH_AVFormat *format = OH_VideoDecoder_GetOutputDescription(s->dec);
        if (!format) {
            av_log(avctx, AV_LOG_ERROR, "GetOutputDescription failed\n");
            return AVERROR_EXTERNAL;
        }

        oh_decode_on_stream_changed(s->dec, format, avctx);
        OH_AVFormat_Destroy(format);
        if (!s->got_stream_info)
            return AVERROR_EXTERNAL;
    }

    if (s->output_to_window)
        return oh_decode_wrap_hw_buffer(avctx, frame, output, &attr);
    return oh_decode_wrap_sw_buffer(avctx, frame, output, &attr);
}

static int oh_decode_send_pkt(AVCodecContext *avctx, OHBufferQueueItem *input)
{
    OHCodecDecContext *s = avctx->priv_data;
    OH_AVErrCode err;
    int ret;

    if (!s->pkt.size && !s->eof_sent) {
        OH_AVCodecBufferAttr attr = {
            .flags = AVCODEC_BUFFER_FLAGS_EOS,
        };
        err = OH_AVBuffer_SetBufferAttr(input->buffer, &attr);
        if (err != AV_ERR_OK)
            return ff_oh_err_to_ff_err(err);
        err = OH_VideoDecoder_PushInputBuffer(s->dec, input->index);
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
    n = FFMIN(s->pkt.size, n);
    memcpy(p, s->pkt.data, n);

    OH_AVCodecBufferAttr attr = {
            .size = n,
            .offset = 0,
            .pts = av_rescale_q(s->pkt.pts, avctx->pkt_timebase,
                                AV_TIME_BASE_Q),
            .flags = (s->pkt.flags & AV_PKT_FLAG_KEY)
                     ? AVCODEC_BUFFER_FLAGS_SYNC_FRAME : 0,
    };

    err = OH_AVBuffer_SetBufferAttr(input->buffer, &attr);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        return ret;
    }
    err = OH_VideoDecoder_PushInputBuffer(s->dec, input->index);
    if (err != AV_ERR_OK) {
        ret = ff_oh_err_to_ff_err(err);
        av_log(avctx, AV_LOG_ERROR, "Push input buffer failed, %d, %s\n",
               err, av_err2str(ret));
        return ret;
    }

    if (n < s->pkt.size) {
        s->pkt.size -= n;
        s->pkt.data += n;
    } else {
        av_packet_unref(&s->pkt);
    }

    return 0;
}

static int oh_decode_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    OHCodecDecContext *s = avctx->priv_data;

    while (1) {
        OHBufferQueueItem buffer = {0};
        int ret;

        // Try get output
        ff_mutex_lock(&s->output_mutex);
        while (!s->decode_status) {
            if (av_fifo_read(s->output_queue, &buffer, 1) >= 0)
                break;
            // Only wait after send EOF
            if (s->eof_sent && !s->decode_status)
                ff_cond_wait(&s->output_cond, &s->output_mutex);
            else
                break;
        }

        ret = s->decode_status;
        ff_mutex_unlock(&s->output_mutex);

        // Got a frame
        if (buffer.buffer)
            return oh_decode_output_frame(avctx, frame, &buffer);
        if (ret < 0)
            return ret;

        if (!s->pkt.size) {
            /* fetch new packet or eof */
            ret = ff_decode_get_packet(avctx, &s->pkt);
            if (ret < 0 && ret != AVERROR_EOF)
                return ret;
        }

        // Wait input buffer
        ff_mutex_lock(&s->input_mutex);
        while (!s->decode_status) {
            if (av_fifo_read(s->input_queue, &buffer, 1) >= 0)
                break;
            ff_cond_wait(&s->input_cond, &s->input_mutex);
        }

        ret = s->decode_status;
        ff_mutex_unlock(&s->input_mutex);

        if (ret < 0)
            return ret;

        ret = oh_decode_send_pkt(avctx, &buffer);
        if (ret < 0)
            return ret;
    }

    return AVERROR(EAGAIN);
}

static void oh_decode_flush(AVCodecContext *avctx)
{
    OHCodecDecContext *s = avctx->priv_data;

    OH_VideoDecoder_Flush(s->dec);

    ff_mutex_lock(&s->input_mutex);
    ff_mutex_lock(&s->output_mutex);
    av_fifo_reset2(s->input_queue);
    av_fifo_reset2(s->output_queue);
    s->decode_status = 0;
    s->eof_sent = false;
    ff_mutex_unlock(&s->output_mutex);
    ff_mutex_unlock(&s->input_mutex);

    OH_VideoDecoder_Start(s->dec);
}

static const AVCodecHWConfigInternal *const oh_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt = AV_PIX_FMT_OHCODEC,
            .methods = AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                       AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_OHCODEC,
        },
        .hwaccel = NULL,
    },
    NULL
};

#define OFFSET(x) offsetof(OHCodecDecContext, x)
#define VD (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM)
static const AVOption ohcodec_vdec_options[] = {
    {"codec_name", "Select codec by name",
         OFFSET(name), AV_OPT_TYPE_STRING, .flags = VD},
    {"allow_sw", "Allow software decoding",
         OFFSET(allow_sw), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VD},
    {NULL}
};

#define DECLARE_OHCODEC_VCLASS(short_name)                                     \
  static const AVClass short_name##_oh_dec_class = {                           \
      .class_name = #short_name "_ohcodec",                                    \
      .item_name = av_default_item_name,                                       \
      .option = ohcodec_vdec_options,                                          \
      .version = LIBAVUTIL_VERSION_INT,                                        \
  };

#define DECLARE_OHCODEC_VDEC(short_name, full_name, codec_id, bsf)             \
  DECLARE_OHCODEC_VCLASS(short_name)                                           \
  const FFCodec ff_##short_name##_oh_decoder = {                               \
      .p.name = #short_name "_ohcodec",                                        \
      CODEC_LONG_NAME(full_name " OpenHarmony Codec"),                         \
      .p.type = AVMEDIA_TYPE_VIDEO,                                            \
      .p.id = codec_id,                                                        \
      .p.priv_class = &short_name##_oh_dec_class,                              \
      .priv_data_size = sizeof(OHCodecDecContext),                             \
      .init = oh_decode_init,                                                  \
      FF_CODEC_RECEIVE_FRAME_CB(oh_decode_receive_frame),                      \
      .flush = oh_decode_flush,                                                \
      .close = oh_decode_close,                                                \
      .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING |      \
                        AV_CODEC_CAP_HARDWARE,                                 \
      .caps_internal = FF_CODEC_CAP_INIT_CLEANUP,                              \
      .bsfs = bsf,                                                             \
      .hw_configs = oh_hw_configs,                                             \
      .p.wrapper_name = "ohcodec",                                             \
  };

#if CONFIG_H264_OH_DECODER
DECLARE_OHCODEC_VDEC(h264, "H.264", AV_CODEC_ID_H264, "h264_mp4toannexb")
#endif

#if CONFIG_HEVC_OH_DECODER
DECLARE_OHCODEC_VDEC(hevc, "H.265", AV_CODEC_ID_HEVC, "hevc_mp4toannexb")
#endif
