/*
 * MMAL Video Decoder
 * Copyright (c) 2015 Rodger Combs
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

/**
 * @file
 * MMAL Video Decoder
 */

#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/mmal_parameters_video.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/vc/mmal_vc_api.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

typedef struct FFBufferEntry {
    AVBufferRef *ref;
    void *data;
    size_t length;
    int64_t pts, dts;
    int flags;
    struct FFBufferEntry *next;
} FFBufferEntry;

// MMAL_POOL_T destroys all of its MMAL_BUFFER_HEADER_Ts. If we want correct
// refcounting for AVFrames, we can free the MMAL_POOL_T only after all AVFrames
// have been unreferenced.
typedef struct FFPoolRef {
    volatile int refcount;
    MMAL_POOL_T *pool;
} FFPoolRef;

typedef struct FFBufferRef {
    MMAL_BUFFER_HEADER_T *buffer;
    FFPoolRef *pool;
} FFBufferRef;

typedef struct MMALDecodeContext {
    AVClass *av_class;
    int extra_buffers;

    MMAL_COMPONENT_T *decoder;
    MMAL_QUEUE_T *queue_decoded_frames;
    MMAL_POOL_T *pool_in;
    FFPoolRef *pool_out;

    // Waiting input packets. Because the libavcodec API requires decoding and
    // returning packets in lockstep, it can happen that queue_decoded_frames
    // contains almost all surfaces - then the decoder input queue can quickly
    // fill up and won't accept new input either. Without consuming input, the
    // libavcodec API can't return new frames, and we have a logical deadlock.
    // This is avoided by queuing such buffers here.
    FFBufferEntry *waiting_buffers, *waiting_buffers_tail;

    int64_t packets_sent;
    volatile int packets_buffered;
    int64_t frames_output;
    int eos_received;
    int eos_sent;
    int extradata_sent;
} MMALDecodeContext;

// Assume decoder is guaranteed to produce output after at least this many
// packets (where each packet contains 1 frame).
#define MAX_DELAYED_FRAMES 16

static void ffmmal_poolref_unref(FFPoolRef *ref)
{
    if (ref && avpriv_atomic_int_add_and_fetch(&ref->refcount, -1) == 0) {
        mmal_pool_destroy(ref->pool);
        av_free(ref);
    }
}

static void ffmmal_release_frame(void *opaque, uint8_t *data)
{
    FFBufferRef *ref = (void *)data;

    mmal_buffer_header_release(ref->buffer);
    ffmmal_poolref_unref(ref->pool);

    av_free(ref);
}

// Setup frame with a new reference to buffer. The buffer must have been
// allocated from the given pool.
static int ffmmal_set_ref(AVFrame *frame, FFPoolRef *pool,
                          MMAL_BUFFER_HEADER_T *buffer)
{
    FFBufferRef *ref = av_mallocz(sizeof(*ref));
    if (!ref)
        return AVERROR(ENOMEM);

    ref->pool = pool;
    ref->buffer = buffer;

    frame->buf[0] = av_buffer_create((void *)ref, sizeof(*ref),
                                     ffmmal_release_frame, NULL,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        av_free(ref);
        return AVERROR(ENOMEM);
    }

    avpriv_atomic_int_add_and_fetch(&ref->pool->refcount, 1);
    mmal_buffer_header_acquire(buffer);

    frame->format = AV_PIX_FMT_MMAL;
    frame->data[3] = (uint8_t *)ref->buffer;
    return 0;
}

static void ffmmal_stop_decoder(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_COMPONENT_T *decoder = ctx->decoder;
    MMAL_BUFFER_HEADER_T *buffer;

    mmal_port_disable(decoder->input[0]);
    mmal_port_disable(decoder->output[0]);
    mmal_port_disable(decoder->control);

    mmal_port_flush(decoder->input[0]);
    mmal_port_flush(decoder->output[0]);
    mmal_port_flush(decoder->control);

    while ((buffer = mmal_queue_get(ctx->queue_decoded_frames)))
        mmal_buffer_header_release(buffer);

    while (ctx->waiting_buffers) {
        FFBufferEntry *buffer = ctx->waiting_buffers;

        ctx->waiting_buffers = buffer->next;

        if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END)
            avpriv_atomic_int_add_and_fetch(&ctx->packets_buffered, -1);

        av_buffer_unref(&buffer->ref);
        av_free(buffer);
    }
    ctx->waiting_buffers_tail = NULL;

    av_assert0(avpriv_atomic_int_get(&ctx->packets_buffered) == 0);

    ctx->frames_output = ctx->eos_received = ctx->eos_sent = ctx->packets_sent = ctx->extradata_sent = 0;
}

static av_cold int ffmmal_close_decoder(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;

    if (ctx->decoder)
        ffmmal_stop_decoder(avctx);

    mmal_component_destroy(ctx->decoder);
    ctx->decoder = NULL;
    mmal_queue_destroy(ctx->queue_decoded_frames);
    mmal_pool_destroy(ctx->pool_in);
    ffmmal_poolref_unref(ctx->pool_out);

    mmal_vc_deinit();

    return 0;
}

static void input_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    AVCodecContext *avctx = (AVCodecContext*)port->userdata;
    MMALDecodeContext *ctx = avctx->priv_data;

    if (!buffer->cmd) {
        FFBufferEntry *entry = buffer->user_data;
        av_buffer_unref(&entry->ref);
        if (entry->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END)
            avpriv_atomic_int_add_and_fetch(&ctx->packets_buffered, -1);
        av_free(entry);
    }
    mmal_buffer_header_release(buffer);
}

static void output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    AVCodecContext *avctx = (AVCodecContext*)port->userdata;
    MMALDecodeContext *ctx = avctx->priv_data;

    mmal_queue_put(ctx->queue_decoded_frames, buffer);
}

static void control_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    AVCodecContext *avctx = (AVCodecContext*)port->userdata;
    MMAL_STATUS_T status;

    if (buffer->cmd == MMAL_EVENT_ERROR) {
        status = *(uint32_t *)buffer->data;
        av_log(avctx, AV_LOG_ERROR, "MMAL error %d on control port\n", (int)status);
    } else {
        char s[20];
        av_get_codec_tag_string(s, sizeof(s), buffer->cmd);
        av_log(avctx, AV_LOG_WARNING, "Unknown MMAL event %s on control port\n", s);
    }

    mmal_buffer_header_release(buffer);
}

// Feed free output buffers to the decoder.
static int ffmmal_fill_output_port(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_BUFFER_HEADER_T *buffer;
    MMAL_STATUS_T status;

    if (!ctx->pool_out)
        return AVERROR_UNKNOWN; // format change code failed with OOM previously

    while ((buffer = mmal_queue_get(ctx->pool_out->pool->queue))) {
        if ((status = mmal_port_send_buffer(ctx->decoder->output[0], buffer))) {
            mmal_buffer_header_release(buffer);
            av_log(avctx, AV_LOG_ERROR, "MMAL error %d when sending output buffer.\n", (int)status);
            return AVERROR_UNKNOWN;
        }
    }

    return 0;
}

static enum AVColorSpace ffmmal_csp_to_av_csp(MMAL_FOURCC_T fourcc)
{
    switch (fourcc) {
    case MMAL_COLOR_SPACE_BT470_2_BG:
    case MMAL_COLOR_SPACE_BT470_2_M:
    case MMAL_COLOR_SPACE_ITUR_BT601:   return AVCOL_SPC_BT470BG;
    case MMAL_COLOR_SPACE_ITUR_BT709:   return AVCOL_SPC_BT709;
    case MMAL_COLOR_SPACE_FCC:          return AVCOL_SPC_FCC;
    case MMAL_COLOR_SPACE_SMPTE240M:    return AVCOL_SPC_SMPTE240M;
    default:                            return AVCOL_SPC_UNSPECIFIED;
    }
}

static int ffmal_update_format(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_STATUS_T status;
    int ret = 0;
    MMAL_COMPONENT_T *decoder = ctx->decoder;
    MMAL_ES_FORMAT_T *format_out = decoder->output[0]->format;

    ffmmal_poolref_unref(ctx->pool_out);
    if (!(ctx->pool_out = av_mallocz(sizeof(*ctx->pool_out)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ctx->pool_out->refcount = 1;

    if (!format_out)
        goto fail;

    if ((status = mmal_port_parameter_set_uint32(decoder->output[0], MMAL_PARAMETER_EXTRA_BUFFERS, ctx->extra_buffers)))
        goto fail;

    if ((status = mmal_port_parameter_set_boolean(decoder->output[0], MMAL_PARAMETER_VIDEO_INTERPOLATE_TIMESTAMPS, 0)))
        goto fail;

    if (avctx->pix_fmt == AV_PIX_FMT_MMAL) {
        format_out->encoding = MMAL_ENCODING_OPAQUE;
    } else {
        format_out->encoding_variant = format_out->encoding = MMAL_ENCODING_I420;
    }

    if ((status = mmal_port_format_commit(decoder->output[0])))
        goto fail;

    if ((ret = ff_set_dimensions(avctx, format_out->es->video.crop.x + format_out->es->video.crop.width,
                                        format_out->es->video.crop.y + format_out->es->video.crop.height)) < 0)
        goto fail;

    if (format_out->es->video.par.num && format_out->es->video.par.den) {
        avctx->sample_aspect_ratio.num = format_out->es->video.par.num;
        avctx->sample_aspect_ratio.den = format_out->es->video.par.den;
    }

    avctx->colorspace = ffmmal_csp_to_av_csp(format_out->es->video.color_space);

    decoder->output[0]->buffer_size =
        FFMAX(decoder->output[0]->buffer_size_min, decoder->output[0]->buffer_size_recommended);
    decoder->output[0]->buffer_num =
        FFMAX(decoder->output[0]->buffer_num_min, decoder->output[0]->buffer_num_recommended) + ctx->extra_buffers;
    ctx->pool_out->pool = mmal_pool_create(decoder->output[0]->buffer_num,
                                           decoder->output[0]->buffer_size);
    if (!ctx->pool_out->pool) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;

fail:
    return ret < 0 ? ret : AVERROR_UNKNOWN;
}

static av_cold int ffmmal_init_decoder(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_STATUS_T status;
    MMAL_ES_FORMAT_T *format_in;
    MMAL_COMPONENT_T *decoder;
    int ret = 0;

    bcm_host_init();

    if (mmal_vc_init()) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialize MMAL VC driver!\n");
        return AVERROR(ENOSYS);
    }

    if ((ret = ff_get_format(avctx, avctx->codec->pix_fmts)) < 0)
        return ret;

    avctx->pix_fmt = ret;

    if ((status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_DECODER, &ctx->decoder)))
        goto fail;

    decoder = ctx->decoder;

    format_in = decoder->input[0]->format;
    format_in->type = MMAL_ES_TYPE_VIDEO;
    switch (avctx->codec_id) {
        case AV_CODEC_ID_MPEG2VIDEO:
            format_in->encoding = MMAL_ENCODING_MP2V;
            av_log(avctx, AV_LOG_DEBUG, "Use MMAL MP2V encoding\n");
            break;
        case AV_CODEC_ID_VC1:
            format_in->encoding = MMAL_ENCODING_WVC1;
            av_log(avctx, AV_LOG_DEBUG, "Use MMAL WVC1 encoding\n");
            break;
        case AV_CODEC_ID_H264:
        default:
            format_in->encoding = MMAL_ENCODING_H264;
            av_log(avctx, AV_LOG_DEBUG, "Use MMAL H264 encoding\n");
            break;
    }
    format_in->es->video.width = FFALIGN(avctx->width, 32);
    format_in->es->video.height = FFALIGN(avctx->height, 16);
    format_in->es->video.crop.width = avctx->width;
    format_in->es->video.crop.height = avctx->height;
    format_in->es->video.frame_rate.num = 24000;
    format_in->es->video.frame_rate.den = 1001;
    format_in->es->video.par.num = avctx->sample_aspect_ratio.num;
    format_in->es->video.par.den = avctx->sample_aspect_ratio.den;
    format_in->flags = MMAL_ES_FORMAT_FLAG_FRAMED;

    if ((status = mmal_port_format_commit(decoder->input[0])))
        goto fail;

    decoder->input[0]->buffer_num =
        FFMAX(decoder->input[0]->buffer_num_min, 20);
    decoder->input[0]->buffer_size =
        FFMAX(decoder->input[0]->buffer_size_min, 512 * 1024);
    ctx->pool_in = mmal_pool_create(decoder->input[0]->buffer_num, 0);
    if (!ctx->pool_in) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = ffmal_update_format(avctx)) < 0)
        goto fail;

    ctx->queue_decoded_frames = mmal_queue_create();
    if (!ctx->queue_decoded_frames)
        goto fail;

    decoder->input[0]->userdata = (void*)avctx;
    decoder->output[0]->userdata = (void*)avctx;
    decoder->control->userdata = (void*)avctx;

    if ((status = mmal_port_enable(decoder->control, control_port_cb)))
        goto fail;
    if ((status = mmal_port_enable(decoder->input[0], input_callback)))
        goto fail;
    if ((status = mmal_port_enable(decoder->output[0], output_callback)))
        goto fail;

    if ((status = mmal_component_enable(decoder)))
        goto fail;

    return 0;

fail:
    ffmmal_close_decoder(avctx);
    return ret < 0 ? ret : AVERROR_UNKNOWN;
}

static void ffmmal_flush(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_COMPONENT_T *decoder = ctx->decoder;
    MMAL_STATUS_T status;

    ffmmal_stop_decoder(avctx);

    if ((status = mmal_port_enable(decoder->control, control_port_cb)))
        goto fail;
    if ((status = mmal_port_enable(decoder->input[0], input_callback)))
        goto fail;
    if ((status = mmal_port_enable(decoder->output[0], output_callback)))
        goto fail;

    return;

fail:
    av_log(avctx, AV_LOG_ERROR, "MMAL flush error: %i\n", (int)status);
}

// Split packets and add them to the waiting_buffers list. We don't queue them
// immediately, because it can happen that the decoder is temporarily blocked
// (due to us not reading/returning enough output buffers) and won't accept
// new input. (This wouldn't be an issue if MMAL input buffers always were
// complete frames - then the input buffer just would have to be big enough.)
// If is_extradata is set, send it as MMAL_BUFFER_HEADER_FLAG_CONFIG.
static int ffmmal_add_packet(AVCodecContext *avctx, AVPacket *avpkt,
                             int is_extradata)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    AVBufferRef *buf = NULL;
    int size = 0;
    uint8_t *data = (uint8_t *)"";
    uint8_t *start;
    int ret = 0;

    if (avpkt->size) {
        if (avpkt->buf) {
            buf = av_buffer_ref(avpkt->buf);
            size = avpkt->size;
            data = avpkt->data;
        } else {
            buf = av_buffer_alloc(avpkt->size);
            if (buf) {
                memcpy(buf->data, avpkt->data, avpkt->size);
                size = buf->size;
                data = buf->data;
            }
        }
        if (!buf) {
            ret = AVERROR(ENOMEM);
            goto done;
        }
        if (!is_extradata)
            ctx->packets_sent++;
    } else {
        if (ctx->eos_sent)
            goto done;
        if (!ctx->packets_sent) {
            // Short-cut the flush logic to avoid upsetting MMAL.
            ctx->eos_sent = 1;
            ctx->eos_received = 1;
            goto done;
        }
    }

    start = data;

    do {
        FFBufferEntry *buffer = av_mallocz(sizeof(*buffer));
        if (!buffer) {
            ret = AVERROR(ENOMEM);
            goto done;
        }

        buffer->data = data;
        buffer->length = FFMIN(size, ctx->decoder->input[0]->buffer_size);

        if (is_extradata)
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_CONFIG;

        if (data == start)
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_FRAME_START;

        data += buffer->length;
        size -= buffer->length;

        buffer->pts = avpkt->pts == AV_NOPTS_VALUE ? MMAL_TIME_UNKNOWN : avpkt->pts;
        buffer->dts = avpkt->dts == AV_NOPTS_VALUE ? MMAL_TIME_UNKNOWN : avpkt->dts;

        if (!size) {
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_FRAME_END;
            avpriv_atomic_int_add_and_fetch(&ctx->packets_buffered, 1);
        }

        if (!buffer->length) {
            buffer->flags |= MMAL_BUFFER_HEADER_FLAG_EOS;
            ctx->eos_sent = 1;
        }

        if (buf) {
            buffer->ref = av_buffer_ref(buf);
            if (!buffer->ref) {
                av_free(buffer);
                ret = AVERROR(ENOMEM);
                goto done;
            }
        }

        // Insert at end of the list
        if (!ctx->waiting_buffers)
            ctx->waiting_buffers = buffer;
        if (ctx->waiting_buffers_tail)
            ctx->waiting_buffers_tail->next = buffer;
        ctx->waiting_buffers_tail = buffer;
    } while (size);

done:
    av_buffer_unref(&buf);
    return ret;
}

// Move prepared/split packets from waiting_buffers to the MMAL decoder.
static int ffmmal_fill_input_port(AVCodecContext *avctx)
{
    MMALDecodeContext *ctx = avctx->priv_data;

    while (ctx->waiting_buffers) {
        MMAL_BUFFER_HEADER_T *mbuffer;
        FFBufferEntry *buffer;
        MMAL_STATUS_T status;

        mbuffer = mmal_queue_get(ctx->pool_in->queue);
        if (!mbuffer)
            return 0;

        buffer = ctx->waiting_buffers;

        mmal_buffer_header_reset(mbuffer);
        mbuffer->cmd = 0;
        mbuffer->pts = buffer->pts;
        mbuffer->dts = buffer->dts;
        mbuffer->flags = buffer->flags;
        mbuffer->data = buffer->data;
        mbuffer->length = buffer->length;
        mbuffer->user_data = buffer;
        mbuffer->alloc_size = ctx->decoder->input[0]->buffer_size;

        // Remove from start of the list
        ctx->waiting_buffers = buffer->next;
        if (ctx->waiting_buffers_tail == buffer)
            ctx->waiting_buffers_tail = NULL;

        if ((status = mmal_port_send_buffer(ctx->decoder->input[0], mbuffer))) {
            mmal_buffer_header_release(mbuffer);
            av_buffer_unref(&buffer->ref);
            if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END)
                avpriv_atomic_int_add_and_fetch(&ctx->packets_buffered, -1);
            av_free(buffer);
        }

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "MMAL error %d when sending input\n", (int)status);
            return AVERROR_UNKNOWN;
        }
    }

    return 0;
}

static int ffmal_copy_frame(AVCodecContext *avctx,  AVFrame *frame,
                            MMAL_BUFFER_HEADER_T *buffer)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    int ret = 0;

    if (avctx->pix_fmt == AV_PIX_FMT_MMAL) {
        if (!ctx->pool_out)
            return AVERROR_UNKNOWN; // format change code failed with OOM previously

        if ((ret = ff_decode_frame_props(avctx, frame)) < 0)
            goto done;

        if ((ret = ffmmal_set_ref(frame, ctx->pool_out, buffer)) < 0)
            goto done;
    } else {
        int w = FFALIGN(avctx->width, 32);
        int h = FFALIGN(avctx->height, 16);
        char *ptr;
        int plane;
        int i;

        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            goto done;

        ptr = buffer->data + buffer->type->video.offset[0];
        for (i = 0; i < avctx->height; i++)
            memcpy(frame->data[0] + frame->linesize[0] * i, ptr + w * i, avctx->width);

        ptr += w * h;

        for (plane = 1; plane < 3; plane++) {
            for (i = 0; i < avctx->height / 2; i++)
                memcpy(frame->data[plane] + frame->linesize[plane] * i, ptr + w / 2 * i, (avctx->width + 1) / 2);
            ptr += w / 2 * h / 2;
        }
    }

    frame->pkt_pts = buffer->pts == MMAL_TIME_UNKNOWN ? AV_NOPTS_VALUE : buffer->pts;
    frame->pkt_dts = AV_NOPTS_VALUE;

done:
    return ret;
}

// Fetch a decoded buffer and place it into the frame parameter.
static int ffmmal_read_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    MMAL_BUFFER_HEADER_T *buffer = NULL;
    MMAL_STATUS_T status = 0;
    int ret = 0;

    if (ctx->eos_received)
        goto done;

    while (1) {
        // To ensure decoding in lockstep with a constant delay between fed packets
        // and output frames, we always wait until an output buffer is available.
        // Except during start we don't know after how many input packets the decoder
        // is going to return the first buffer, and we can't distinguish decoder
        // being busy from decoder waiting for input. So just poll at the start and
        // keep feeding new data to the buffer.
        // We are pretty sure the decoder will produce output if we sent more input
        // frames than what a h264 decoder could logically delay. This avoids too
        // excessive buffering.
        // We also wait if we sent eos, but didn't receive it yet (think of decoding
        // stream with a very low number of frames).
        if (avpriv_atomic_int_get(&ctx->packets_buffered) > MAX_DELAYED_FRAMES ||
            (ctx->packets_sent && ctx->eos_sent)) {
            // MMAL will ignore broken input packets, which means the frame we
            // expect here may never arrive. Dealing with this correctly is
            // complicated, so here's a hack to avoid that it freezes forever
            // in this unlikely situation.
            buffer = mmal_queue_timedwait(ctx->queue_decoded_frames, 100);
            if (!buffer) {
                av_log(avctx, AV_LOG_ERROR, "Did not get output frame from MMAL.\n");
                ret = AVERROR_UNKNOWN;
                goto done;
            }
        } else {
            buffer = mmal_queue_get(ctx->queue_decoded_frames);
            if (!buffer)
                goto done;
        }

        ctx->eos_received |= !!(buffer->flags & MMAL_BUFFER_HEADER_FLAG_EOS);
        if (ctx->eos_received)
            goto done;

        if (buffer->cmd == MMAL_EVENT_FORMAT_CHANGED) {
            MMAL_COMPONENT_T *decoder = ctx->decoder;
            MMAL_EVENT_FORMAT_CHANGED_T *ev = mmal_event_format_changed_get(buffer);
            MMAL_BUFFER_HEADER_T *stale_buffer;

            av_log(avctx, AV_LOG_INFO, "Changing output format.\n");

            if ((status = mmal_port_disable(decoder->output[0])))
                goto done;

            while ((stale_buffer = mmal_queue_get(ctx->queue_decoded_frames)))
                mmal_buffer_header_release(stale_buffer);

            mmal_format_copy(decoder->output[0]->format, ev->format);

            if ((ret = ffmal_update_format(avctx)) < 0)
                goto done;

            if ((status = mmal_port_enable(decoder->output[0], output_callback)))
                goto done;

            if ((ret = ffmmal_fill_output_port(avctx)) < 0)
                goto done;

            if ((ret = ffmmal_fill_input_port(avctx)) < 0)
                goto done;

            mmal_buffer_header_release(buffer);
            continue;
        } else if (buffer->cmd) {
            char s[20];
            av_get_codec_tag_string(s, sizeof(s), buffer->cmd);
            av_log(avctx, AV_LOG_WARNING, "Unknown MMAL event %s on output port\n", s);
            goto done;
        } else if (buffer->length == 0) {
            // Unused output buffer that got drained after format change.
            mmal_buffer_header_release(buffer);
            continue;
        }

        ctx->frames_output++;

        if ((ret = ffmal_copy_frame(avctx, frame, buffer)) < 0)
            goto done;

        *got_frame = 1;
        break;
    }

done:
    if (buffer)
        mmal_buffer_header_release(buffer);
    if (status && ret >= 0)
        ret = AVERROR_UNKNOWN;
    return ret;
}

static int ffmmal_decode(AVCodecContext *avctx, void *data, int *got_frame,
                         AVPacket *avpkt)
{
    MMALDecodeContext *ctx = avctx->priv_data;
    AVFrame *frame = data;
    int ret = 0;

    if (avctx->extradata_size && !ctx->extradata_sent) {
        AVPacket pkt = {0};
        av_init_packet(&pkt);
        pkt.data = avctx->extradata;
        pkt.size = avctx->extradata_size;
        ctx->extradata_sent = 1;
        if ((ret = ffmmal_add_packet(avctx, &pkt, 1)) < 0)
            return ret;
    }

    if ((ret = ffmmal_add_packet(avctx, avpkt, 0)) < 0)
        return ret;

    if ((ret = ffmmal_fill_input_port(avctx)) < 0)
        return ret;

    if ((ret = ffmmal_fill_output_port(avctx)) < 0)
        return ret;

    if ((ret = ffmmal_read_frame(avctx, frame, got_frame)) < 0)
        return ret;

    // ffmmal_read_frame() can block for a while. Since the decoder is
    // asynchronous, it's a good idea to fill the ports again.

    if ((ret = ffmmal_fill_output_port(avctx)) < 0)
        return ret;

    if ((ret = ffmmal_fill_input_port(avctx)) < 0)
        return ret;

    return ret;
}

AVHWAccel ff_h264_mmal_hwaccel = {
    .name       = "h264_mmal",
    .type       = AVMEDIA_TYPE_VIDEO,
    .id         = AV_CODEC_ID_H264,
    .pix_fmt    = AV_PIX_FMT_MMAL,
};

AVHWAccel ff_mpeg2_mmal_hwaccel = {
    .name       = "mpeg2_mmal",
    .type       = AVMEDIA_TYPE_VIDEO,
    .id         = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt    = AV_PIX_FMT_MMAL,
};

AVHWAccel ff_vc1_mmal_hwaccel = {
    .name       = "vc1_mmal",
    .type       = AVMEDIA_TYPE_VIDEO,
    .id         = AV_CODEC_ID_VC1,
    .pix_fmt    = AV_PIX_FMT_MMAL,
};

static const AVOption options[]={
    {"extra_buffers", "extra buffers", offsetof(MMALDecodeContext, extra_buffers), AV_OPT_TYPE_INT, {.i64 = 10}, 0, 256, 0},
    {NULL}
};

#define FFMMAL_DEC_CLASS(NAME) \
    static const AVClass ffmmal_##NAME##_dec_class = { \
        .class_name = "mmal_" #NAME "_dec", \
        .option     = options, \
        .version    = LIBAVUTIL_VERSION_INT, \
    };

#define FFMMAL_DEC(NAME, ID) \
    FFMMAL_DEC_CLASS(NAME) \
    AVCodec ff_##NAME##_mmal_decoder = { \
        .name           = #NAME "_mmal", \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME " (mmal)"), \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = ID, \
        .priv_data_size = sizeof(MMALDecodeContext), \
        .init           = ffmmal_init_decoder, \
        .close          = ffmmal_close_decoder, \
        .decode         = ffmmal_decode, \
        .flush          = ffmmal_flush, \
        .priv_class     = &ffmmal_##NAME##_dec_class, \
        .capabilities   = AV_CODEC_CAP_DELAY, \
        .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS, \
        .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_MMAL, \
                                                         AV_PIX_FMT_YUV420P, \
                                                         AV_PIX_FMT_NONE}, \
    };

FFMMAL_DEC(h264, AV_CODEC_ID_H264)
FFMMAL_DEC(mpeg2, AV_CODEC_ID_MPEG2VIDEO)
FFMMAL_DEC(vc1, AV_CODEC_ID_VC1)
