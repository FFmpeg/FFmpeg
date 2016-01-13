/*
 * RTMFP network protocol
 * Copyright (c) 2015 Thomas Jammet
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
 * RTMFP protocol based on http://TODO librtmfp
 */

/*#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"*/
#include "libavutil/opt.h"
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "url.h"

#include <librtmfp/librtmfp.h>

typedef struct LibRTMFPContext {
    const AVClass *class;
    unsigned int id;
    int audioUnbuffered;
    int videoUnbuffered;
    int p2pPublishing;
    char* peerId;
    char* publication;
    /*RTMP rtmp;
    char *app;
    char *conn;
    char *subscribe;
    char *playpath;
    char *tcurl;
    char *flashver;
    char *swfurl;
    char *swfverify;
    char *pageurl;
    char *client_buffer_time;
    int live;
    char *temp_filename;
    int buffer_size;*/
} LibRTMFPContext;

static void rtmfp_log(unsigned int threadID, int level, const char* fileName, long line, const char* message)
{
    switch (level) {
    default:
    case 1: level = AV_LOG_FATAL;   break;
    case 2:
    case 3: level = AV_LOG_ERROR;   break;
    case 4: level = AV_LOG_WARNING; break;
    case 5:
    case 6: level = AV_LOG_INFO;    break;
    case 7: level = AV_LOG_DEBUG;   break;
    case 8: level = AV_LOG_VERBOSE; break;
    }

    av_log(NULL, level, message);
    av_log(NULL, level, "\n");
}

static int rtmfp_close(URLContext *s)
{
    LibRTMFPContext *ctx = s->priv_data;

    av_log(NULL, AV_LOG_INFO, "Closing RTMFP connection...\n");
    RTMFP_Close(ctx->id);
    return 0;
}

static void onSocketError(const char* err) {
  av_log(NULL, AV_LOG_ERROR, "Error on RTMFP socket : %s\n", err);
}

static void onStatusEvent(const char* code, const char* description) {
  av_log(NULL, AV_LOG_INFO, "onStatusEvent : %s - %s\n", code, description);
}

/**
 * Open RTMFP connection and verify that the stream can be played.
 *
 * URL syntax: rtmp://server[:port][/app][/playpath][ keyword=value]...
 *             where 'app' is first one or two directories in the path
 *             (e.g. /ondemand/, /flash/live/, etc.)
 *             and 'playpath' is a file name (the rest of the path,
 *             may be prefixed with "mp4:")
 *
 *             Additional RTMFP library options may be appended as
 *             space-separated key-value pairs.
 */
static int rtmfp_open(URLContext *s, const char *uri, int flags)
{
    LibRTMFPContext *ctx = s->priv_data;
    int level = 0, res = 0;
    char *url = av_malloc(strlen(uri)+1);
    snprintf(url, strlen(uri)+1, uri);

    switch (av_log_get_level()) {
        case AV_LOG_FATAL:   level = 1; break;
        case AV_LOG_ERROR:   level = 3; break;
        case AV_LOG_WARNING: level = 4; break;
        default:
        case AV_LOG_INFO:    level = 6; break;
        case AV_LOG_DEBUG:   level = 7; break;
        case AV_LOG_VERBOSE: level = 8; break;
    }
    RTMFP_LogSetLevel(level);
    RTMFP_LogSetCallback(rtmfp_log);
    RTMFP_InterruptSetCallback(s->interrupt_callback.callback, s->interrupt_callback.opaque);

    RTMFP_GetPublicationAndUrlFromUri(url, &ctx->publication);

    if ((ctx->id = RTMFP_Connect(url, onSocketError, onStatusEvent, NULL, 1)) == 0)
        return -1;

    av_log(NULL, AV_LOG_INFO, "RTMFP Connect called : %d\n", ctx->id);

    if (ctx->peerId)
        res = RTMFP_Connect2Peer(ctx->id, ctx->peerId, ctx->publication);
    else if (ctx->p2pPublishing)
        res = RTMFP_PublishP2P(ctx->id, ctx->publication, !ctx->audioUnbuffered, !ctx->videoUnbuffered, 1);
    else if (flags & AVIO_FLAG_WRITE)
        res = RTMFP_Publish(ctx->id, ctx->publication, !ctx->audioUnbuffered, !ctx->videoUnbuffered);
    else
        res = RTMFP_Play(ctx->id, ctx->publication);

    if (!res)
        return -1;

    s->is_streamed = 1;
    return 0;
}

static int rtmfp_write(URLContext *s, const uint8_t *buf, int size)
{
    LibRTMFPContext *ctx = s->priv_data;
    int res = 0;

    res = RTMFP_Write(ctx->id, buf, size);
    //av_log(NULL, AV_LOG_INFO, "RTMFP write called, %d/%d bytes read\n", res, size);
    if (res < 0)
        return AVERROR_UNKNOWN;
    return res;
}

static int rtmfp_read(URLContext *s, uint8_t *buf, int size)
{
    LibRTMFPContext *ctx = s->priv_data;
    int res = 0;

    res = RTMFP_Read(ctx->id, buf, size);
    //av_log(NULL, AV_LOG_INFO, "RTMFP read called, %d/%d bytes read\n", res, size);
    if (res < 0)
        return AVERROR_UNKNOWN;
    return res;
}

/*static int rtmp_read_pause(URLContext *s, int pause)
{
    LibRTMPContext *ctx = s->priv_data;
    RTMP *r = &ctx->rtmp;

    if (!RTMP_Pause(r, pause))
        return AVERROR_UNKNOWN;
    return 0;
}

static int64_t rtmp_read_seek(URLContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    LibRTMPContext *ctx = s->priv_data;
    RTMP *r = &ctx->rtmp;

    if (flags & AVSEEK_FLAG_BYTE)
        return AVERROR(ENOSYS);

    // seeks are in milliseconds
    if (stream_index < 0)
        timestamp = av_rescale_rnd(timestamp, 1000, AV_TIME_BASE,
            flags & AVSEEK_FLAG_BACKWARD ? AV_ROUND_DOWN : AV_ROUND_UP);

    if (!RTMP_SendSeek(r, timestamp))
        return AVERROR_UNKNOWN;
    return timestamp;
}

static int rtmp_get_file_handle(URLContext *s)
{
    LibRTMPContext *ctx = s->priv_data;
    RTMP *r = &ctx->rtmp;

    return RTMP_Socket(r);
}*/

#define OFFSET(x) offsetof(LibRTMFPContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"rtmfp_audioUnbuffered", "Unbuffered audio mode (default to false)", OFFSET(audioUnbuffered), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"rtmfp_videoUnbuffered", "Unbuffered video mode (default to false)", OFFSET(videoUnbuffered), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"rtmfp_peerId", "Connect to a peer for playing", OFFSET(peerId), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmfp_p2pPublishing", "Publish the stream in p2p mode (default to false)", OFFSET(p2pPublishing), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    { NULL },
};

//define RTMFP_CLASS(flavor)
static const AVClass librtmfp_class = {
    .class_name = "librtmfp protocol",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

//RTMP_CLASS(rtmp)
URLProtocol ff_librtmfp_protocol = {
    .name                = "rtmfp",
    .url_open            = rtmfp_open,
    .url_read            = rtmfp_read,
    .url_write           = rtmfp_write,
    .url_close           = rtmfp_close,
    /*.url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle,*/
    .priv_data_size      = sizeof(LibRTMFPContext),
    .priv_data_class     = &librtmfp_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};

