/*
 * FIFO pseudo-muxer
 * Copyright (c) 2016 Jan Sebechlebsky
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"
#include "avformat.h"
#include "internal.h"

#define FIFO_DEFAULT_QUEUE_SIZE              60
#define FIFO_DEFAULT_MAX_RECOVERY_ATTEMPTS   0
#define FIFO_DEFAULT_RECOVERY_WAIT_TIME_USEC 5000000 // 5 seconds

typedef struct FifoContext {
    const AVClass *class;
    AVFormatContext *avf;

    char *format;
    char *format_options_str;
    AVDictionary *format_options;

    int queue_size;
    AVThreadMessageQueue *queue;

    pthread_t writer_thread;

    /* Return value of last write_trailer_call */
    int write_trailer_ret;

    /* Time to wait before next recovery attempt
     * This can refer to the time in processed stream,
     * or real time. */
    int64_t recovery_wait_time;

    /* Maximal number of unsuccessful successive recovery attempts */
    int max_recovery_attempts;

    /* Whether to attempt recovery from failure */
    int attempt_recovery;

    /* If >0 stream time will be used when waiting
     * for the recovery attempt instead of real time */
    int recovery_wait_streamtime;

    /* If >0 recovery will be attempted regardless of error code
     * (except AVERROR_EXIT, so exit request is never ignored) */
    int recover_any_error;

    /* Whether to drop packets in case the queue is full. */
    int drop_pkts_on_overflow;

    /* Whether to wait for keyframe when recovering
     * from failure or queue overflow */
    int restart_with_keyframe;

    pthread_mutex_t overflow_flag_lock;
    /* Value > 0 signals queue overflow */
    volatile uint8_t overflow_flag;

} FifoContext;

typedef struct FifoThreadContext {
    AVFormatContext *avf;

    /* Timestamp of last failure.
     * This is either pts in case stream time is used,
     * or microseconds as returned by av_getttime_relative() */
    int64_t last_recovery_ts;

    /* Number of current recovery process
     * Value > 0 means we are in recovery process */
    int recovery_nr;

    /* If > 0 all frames will be dropped until keyframe is received */
    uint8_t drop_until_keyframe;

    /* Value > 0 means that the previous write_header call was successful
     * so finalization by calling write_trailer and ff_io_close must be done
     * before exiting / reinitialization of underlying muxer */
    uint8_t header_written;
} FifoThreadContext;

typedef enum FifoMessageType {
    FIFO_WRITE_HEADER,
    FIFO_WRITE_PACKET,
    FIFO_FLUSH_OUTPUT
} FifoMessageType;

typedef struct FifoMessage {
    FifoMessageType type;
    AVPacket pkt;
} FifoMessage;

static int fifo_thread_write_header(FifoThreadContext *ctx)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    AVFormatContext *avf2 = fifo->avf;
    AVDictionary *format_options = NULL;
    int ret, i;

    ret = av_dict_copy(&format_options, fifo->format_options, 0);
    if (ret < 0)
        return ret;

    ret = ff_format_output_open(avf2, avf->filename, &format_options);
    if (ret < 0) {
        av_log(avf, AV_LOG_ERROR, "Error opening %s: %s\n", avf->filename,
               av_err2str(ret));
        goto end;
    }

    for (i = 0;i < avf2->nb_streams; i++)
        avf2->streams[i]->cur_dts = 0;

    ret = avformat_write_header(avf2, &format_options);
    if (!ret)
        ctx->header_written = 1;

    // Check for options unrecognized by underlying muxer
    if (format_options) {
        AVDictionaryEntry *entry = NULL;
        while ((entry = av_dict_get(format_options, "", entry, AV_DICT_IGNORE_SUFFIX)))
            av_log(avf2, AV_LOG_ERROR, "Unknown option '%s'\n", entry->key);
        ret = AVERROR(EINVAL);
    }

end:
    av_dict_free(&format_options);
    return ret;
}

static int fifo_thread_flush_output(FifoThreadContext *ctx)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    AVFormatContext *avf2 = fifo->avf;

    return av_write_frame(avf2, NULL);
}

static int fifo_thread_write_packet(FifoThreadContext *ctx, AVPacket *pkt)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    AVFormatContext *avf2 = fifo->avf;
    AVRational src_tb, dst_tb;
    int ret, s_idx;

    if (ctx->drop_until_keyframe) {
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            ctx->drop_until_keyframe = 0;
            av_log(avf, AV_LOG_VERBOSE, "Keyframe received, recovering...\n");
        } else {
            av_log(avf, AV_LOG_VERBOSE, "Dropping non-keyframe packet\n");
            av_packet_unref(pkt);
            return 0;
        }
    }

    s_idx = pkt->stream_index;
    src_tb = avf->streams[s_idx]->time_base;
    dst_tb = avf2->streams[s_idx]->time_base;
    av_packet_rescale_ts(pkt, src_tb, dst_tb);

    ret = av_write_frame(avf2, pkt);
    if (ret >= 0)
        av_packet_unref(pkt);
    return ret;
}

static int fifo_thread_write_trailer(FifoThreadContext *ctx)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    AVFormatContext *avf2 = fifo->avf;
    int ret;

    if (!ctx->header_written)
        return 0;

    ret = av_write_trailer(avf2);
    ff_format_io_close(avf2, &avf2->pb);

    return ret;
}

static int fifo_thread_dispatch_message(FifoThreadContext *ctx, FifoMessage *msg)
{
    int ret;

    if (!ctx->header_written) {
        ret = fifo_thread_write_header(ctx);
        if (ret < 0)
            return ret;
    }

    switch(msg->type) {
    case FIFO_WRITE_HEADER:
        return ret;
    case FIFO_WRITE_PACKET:
        return fifo_thread_write_packet(ctx, &msg->pkt);
    case FIFO_FLUSH_OUTPUT:
        return fifo_thread_flush_output(ctx);
    }

    return AVERROR(EINVAL);
}

static int is_recoverable(const FifoContext *fifo, int err_no) {
    if (!fifo->attempt_recovery)
        return 0;

    if (fifo->recover_any_error)
        return err_no != AVERROR_EXIT;

    switch (err_no) {
    case AVERROR(EINVAL):
    case AVERROR(ENOSYS):
    case AVERROR_EOF:
    case AVERROR_EXIT:
    case AVERROR_PATCHWELCOME:
        return 0;
    default:
        return 1;
    }
}

static void free_message(void *msg)
{
    FifoMessage *fifo_msg = msg;

    if (fifo_msg->type == FIFO_WRITE_PACKET)
        av_packet_unref(&fifo_msg->pkt);
}

static int fifo_thread_process_recovery_failure(FifoThreadContext *ctx, AVPacket *pkt,
                                                int err_no)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    int ret;

    av_log(avf, AV_LOG_INFO, "Recovery failed: %s\n",
           av_err2str(err_no));

    if (fifo->recovery_wait_streamtime) {
        if (pkt->pts == AV_NOPTS_VALUE)
            av_log(avf, AV_LOG_WARNING, "Packet does not contain presentation"
                   " timestamp, recovery will be attempted immediately");
        ctx->last_recovery_ts = pkt->pts;
    } else {
        ctx->last_recovery_ts = av_gettime_relative();
    }

    if (fifo->max_recovery_attempts &&
        ctx->recovery_nr >= fifo->max_recovery_attempts) {
        av_log(avf, AV_LOG_ERROR,
               "Maximal number of %d recovery attempts reached.\n",
               fifo->max_recovery_attempts);
        ret = err_no;
    } else {
        ret = AVERROR(EAGAIN);
    }

    return ret;
}

static int fifo_thread_attempt_recovery(FifoThreadContext *ctx, FifoMessage *msg, int err_no)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    AVPacket *pkt = &msg->pkt;
    int64_t time_since_recovery;
    int ret;

    if (!is_recoverable(fifo, err_no)) {
        ret = err_no;
        goto fail;
    }

    if (ctx->header_written) {
        fifo->write_trailer_ret = fifo_thread_write_trailer(ctx);
        ctx->header_written = 0;
    }

    if (!ctx->recovery_nr) {
        ctx->last_recovery_ts = fifo->recovery_wait_streamtime ?
                                AV_NOPTS_VALUE : 0;
    } else {
        if (fifo->recovery_wait_streamtime) {
            if (ctx->last_recovery_ts == AV_NOPTS_VALUE) {
                AVRational tb = avf->streams[pkt->stream_index]->time_base;
                time_since_recovery = av_rescale_q(pkt->pts - ctx->last_recovery_ts,
                                                   tb, AV_TIME_BASE_Q);
            } else {
                /* Enforce recovery immediately */
                time_since_recovery = fifo->recovery_wait_time;
            }
        } else {
            time_since_recovery = av_gettime_relative() - ctx->last_recovery_ts;
        }

        if (time_since_recovery < fifo->recovery_wait_time)
            return AVERROR(EAGAIN);
    }

    ctx->recovery_nr++;

    if (fifo->max_recovery_attempts) {
        av_log(avf, AV_LOG_VERBOSE, "Recovery attempt #%d/%d\n",
               ctx->recovery_nr, fifo->max_recovery_attempts);
    } else {
        av_log(avf, AV_LOG_VERBOSE, "Recovery attempt #%d\n",
               ctx->recovery_nr);
    }

    if (fifo->restart_with_keyframe && fifo->drop_pkts_on_overflow)
        ctx->drop_until_keyframe = 1;

    ret = fifo_thread_dispatch_message(ctx, msg);
    if (ret < 0) {
        if (is_recoverable(fifo, ret)) {
            return fifo_thread_process_recovery_failure(ctx, pkt, ret);
        } else {
            goto fail;
        }
    } else {
        av_log(avf, AV_LOG_INFO, "Recovery successful\n");
        ctx->recovery_nr = 0;
    }

    return 0;

fail:
    free_message(msg);
    return ret;
}

static int fifo_thread_recover(FifoThreadContext *ctx, FifoMessage *msg, int err_no)
{
    AVFormatContext *avf = ctx->avf;
    FifoContext *fifo = avf->priv_data;
    int ret;

    do {
        if (!fifo->recovery_wait_streamtime && ctx->recovery_nr > 0) {
            int64_t time_since_recovery = av_gettime_relative() - ctx->last_recovery_ts;
            int64_t time_to_wait = FFMAX(0, fifo->recovery_wait_time - time_since_recovery);
            if (time_to_wait)
                av_usleep(FFMIN(10000, time_to_wait));
        }

        ret = fifo_thread_attempt_recovery(ctx, msg, err_no);
    } while (ret == AVERROR(EAGAIN) && !fifo->drop_pkts_on_overflow);

    if (ret == AVERROR(EAGAIN) && fifo->drop_pkts_on_overflow) {
        if (msg->type == FIFO_WRITE_PACKET)
            av_packet_unref(&msg->pkt);
        ret = 0;
    }

    return ret;
}

static void *fifo_consumer_thread(void *data)
{
    AVFormatContext *avf = data;
    FifoContext *fifo = avf->priv_data;
    AVThreadMessageQueue *queue = fifo->queue;
    FifoMessage msg = {FIFO_WRITE_HEADER, {0}};
    int ret;

    FifoThreadContext fifo_thread_ctx;
    memset(&fifo_thread_ctx, 0, sizeof(FifoThreadContext));
    fifo_thread_ctx.avf = avf;

    while (1) {
        uint8_t just_flushed = 0;

        if (!fifo_thread_ctx.recovery_nr)
            ret = fifo_thread_dispatch_message(&fifo_thread_ctx, &msg);

        if (ret < 0 || fifo_thread_ctx.recovery_nr > 0) {
            int rec_ret = fifo_thread_recover(&fifo_thread_ctx, &msg, ret);
            if (rec_ret < 0) {
                av_thread_message_queue_set_err_send(queue, rec_ret);
                break;
            }
        }

        /* If the queue is full at the moment when fifo_write_packet
         * attempts to insert new message (packet) to the queue,
         * it sets the fifo->overflow_flag to 1 and drops packet.
         * Here in consumer thread, the flag is checked and if it is
         * set, the queue is flushed and flag cleared. */
        pthread_mutex_lock(&fifo->overflow_flag_lock);
        if (fifo->overflow_flag) {
            av_thread_message_flush(queue);
            if (fifo->restart_with_keyframe)
                fifo_thread_ctx.drop_until_keyframe = 1;
            fifo->overflow_flag = 0;
            just_flushed = 1;
        }
        pthread_mutex_unlock(&fifo->overflow_flag_lock);

        if (just_flushed)
            av_log(avf, AV_LOG_INFO, "FIFO queue flushed\n");

        ret = av_thread_message_queue_recv(queue, &msg, 0);
        if (ret < 0) {
            av_thread_message_queue_set_err_send(queue, ret);
            break;
        }
    }

    fifo->write_trailer_ret = fifo_thread_write_trailer(&fifo_thread_ctx);

    return NULL;
}

static int fifo_mux_init(AVFormatContext *avf, AVOutputFormat *oformat)
{
    FifoContext *fifo = avf->priv_data;
    AVFormatContext *avf2;
    int ret = 0, i;

    ret = avformat_alloc_output_context2(&avf2, oformat, NULL, NULL);
    if (ret < 0)
        return ret;

    fifo->avf = avf2;

    avf2->interrupt_callback = avf->interrupt_callback;
    avf2->max_delay = avf->max_delay;
    ret = av_dict_copy(&avf2->metadata, avf->metadata, 0);
    if (ret < 0)
        return ret;
    avf2->opaque = avf->opaque;
    avf2->io_close = avf->io_close;
    avf2->io_open = avf->io_open;
    avf2->flags = avf->flags;

    for (i = 0; i < avf->nb_streams; ++i) {
        AVStream *st = avformat_new_stream(avf2, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        ret = ff_stream_encode_params_copy(st, avf->streams[i]);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int fifo_init(AVFormatContext *avf)
{
    FifoContext *fifo = avf->priv_data;
    AVOutputFormat *oformat;
    int ret = 0;

    if (fifo->recovery_wait_streamtime && !fifo->drop_pkts_on_overflow) {
        av_log(avf, AV_LOG_ERROR, "recovery_wait_streamtime can be turned on"
               " only when drop_pkts_on_overflow is also turned on\n");
        return AVERROR(EINVAL);
    }

    if (fifo->format_options_str) {
        ret = av_dict_parse_string(&fifo->format_options, fifo->format_options_str,
                                   "=", ":", 0);
        if (ret < 0) {
            av_log(avf, AV_LOG_ERROR, "Could not parse format options list '%s'\n",
                   fifo->format_options_str);
            return ret;
        }
    }

    oformat = av_guess_format(fifo->format, avf->filename, NULL);
    if (!oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        return ret;
    }

    ret = fifo_mux_init(avf, oformat);
    if (ret < 0)
        return ret;

    ret = av_thread_message_queue_alloc(&fifo->queue, (unsigned) fifo->queue_size,
                                        sizeof(FifoMessage));
    if (ret < 0)
        return ret;

    av_thread_message_queue_set_free_func(fifo->queue, free_message);

    ret = pthread_mutex_init(&fifo->overflow_flag_lock, NULL);
    if (ret < 0)
        return AVERROR(ret);

    return 0;
}

static int fifo_write_header(AVFormatContext *avf)
{
    FifoContext * fifo = avf->priv_data;
    int ret;

    ret = pthread_create(&fifo->writer_thread, NULL, fifo_consumer_thread, avf);
    if (ret) {
        av_log(avf, AV_LOG_ERROR, "Failed to start thread: %s\n",
               av_err2str(AVERROR(ret)));
        ret = AVERROR(ret);
    }

    return ret;
}

static int fifo_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    FifoContext *fifo = avf->priv_data;
    FifoMessage msg = {.type = pkt ? FIFO_WRITE_PACKET : FIFO_FLUSH_OUTPUT};
    int ret;

    if (pkt) {
        av_init_packet(&msg.pkt);
        ret = av_packet_ref(&msg.pkt,pkt);
        if (ret < 0)
            return ret;
    }

    ret = av_thread_message_queue_send(fifo->queue, &msg,
                                       fifo->drop_pkts_on_overflow ?
                                       AV_THREAD_MESSAGE_NONBLOCK : 0);
    if (ret == AVERROR(EAGAIN)) {
        uint8_t overflow_set = 0;

        /* Queue is full, set fifo->overflow_flag to 1
         * to let consumer thread know the queue should
         * be flushed. */
        pthread_mutex_lock(&fifo->overflow_flag_lock);
        if (!fifo->overflow_flag)
            fifo->overflow_flag = overflow_set = 1;
        pthread_mutex_unlock(&fifo->overflow_flag_lock);

        if (overflow_set)
            av_log(avf, AV_LOG_WARNING, "FIFO queue full\n");
        ret = 0;
        goto fail;
    } else if (ret < 0) {
        goto fail;
    }

    return ret;
fail:
    if (pkt)
        av_packet_unref(&msg.pkt);
    return ret;
}

static int fifo_write_trailer(AVFormatContext *avf)
{
    FifoContext *fifo= avf->priv_data;
    int ret;

    av_thread_message_queue_set_err_recv(fifo->queue, AVERROR_EOF);

    ret = pthread_join(fifo->writer_thread, NULL);
    if (ret < 0) {
        av_log(avf, AV_LOG_ERROR, "pthread join error: %s\n",
               av_err2str(AVERROR(ret)));
        return AVERROR(ret);
    }

    ret = fifo->write_trailer_ret;
    return ret;
}

static void fifo_deinit(AVFormatContext *avf)
{
    FifoContext *fifo = avf->priv_data;

    av_dict_free(&fifo->format_options);
    avformat_free_context(fifo->avf);
    av_thread_message_queue_free(&fifo->queue);
    pthread_mutex_destroy(&fifo->overflow_flag_lock);
}

#define OFFSET(x) offsetof(FifoContext, x)
static const AVOption options[] = {
        {"fifo_format", "Target muxer", OFFSET(format),
         AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM},

        {"queue_size", "Size of fifo queue", OFFSET(queue_size),
         AV_OPT_TYPE_INT, {.i64 = FIFO_DEFAULT_QUEUE_SIZE}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},

        {"format_opts", "Options to be passed to underlying muxer", OFFSET(format_options_str),
         AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM},

        {"drop_pkts_on_overflow", "Drop packets on fifo queue overflow not to block encoder", OFFSET(drop_pkts_on_overflow),
         AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},

        {"restart_with_keyframe", "Wait for keyframe when restarting output", OFFSET(restart_with_keyframe),
         AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},

        {"attempt_recovery", "Attempt recovery in case of failure", OFFSET(attempt_recovery),
        AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},

        {"max_recovery_attempts", "Maximal number of recovery attempts", OFFSET(max_recovery_attempts),
         AV_OPT_TYPE_INT, {.i64 = FIFO_DEFAULT_MAX_RECOVERY_ATTEMPTS}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},

        {"recovery_wait_time", "Waiting time between recovery attempts", OFFSET(recovery_wait_time),
         AV_OPT_TYPE_DURATION, {.i64 = FIFO_DEFAULT_RECOVERY_WAIT_TIME_USEC}, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM},

        {"recovery_wait_streamtime", "Use stream time instead of real time while waiting for recovery",
         OFFSET(recovery_wait_streamtime), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},

        {"recover_any_error", "Attempt recovery regardless of type of the error", OFFSET(recover_any_error),
         AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},

        {NULL},
};

static const AVClass fifo_muxer_class = {
    .class_name = "Fifo muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_fifo_muxer = {
    .name           = "fifo",
    .long_name      = NULL_IF_CONFIG_SMALL("FIFO queue pseudo-muxer"),
    .priv_data_size = sizeof(FifoContext),
    .init           = fifo_init,
    .write_header   = fifo_write_header,
    .write_packet   = fifo_write_packet,
    .write_trailer  = fifo_write_trailer,
    .deinit         = fifo_deinit,
    .priv_class     = &fifo_muxer_class,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH | AVFMT_TS_NEGATIVE,
};
