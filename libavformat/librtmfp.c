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
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"*/
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "url.h"

#include <librtmfp/librtmfp.h>

typedef struct LibRTMFPContext {
    const AVClass *class;
    unsigned int id;
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

static void rtmfp_log(int level, const char *message)
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
    //RTMP *r = &ctx->rtmp;

    RTMFP_Close(ctx->id);
    //av_freep(&ctx->temp_filename);
    return 0;
}

static void onSocketError(const char* err) {
  printf("Error on rtmfp socket : %s", err);
}

static void onStatusEvent(const char* code, const char* description) {
  printf("onStatusEvent : %s - %s", code, description);
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
    //RTMP *r = &ctx->rtmp;
    int rc = 0, level;
    char *filename = s->filename;
    int len = strlen(s->filename) + 1;

    /*switch (av_log_get_level()) {
    default:
    case AV_LOG_FATAL:   level = RTMP_LOGCRIT;    break;
    case AV_LOG_ERROR:   level = RTMP_LOGERROR;   break;
    case AV_LOG_WARNING: level = RTMP_LOGWARNING; break;
    case AV_LOG_INFO:    level = RTMP_LOGINFO;    break;
    case AV_LOG_VERBOSE: level = RTMP_LOGDEBUG;   break;
    case AV_LOG_DEBUG:   level = RTMP_LOGDEBUG2;  break;
    }
    RTMP_LogSetLevel(level);*/

    RTMFP_LogSetCallback(rtmfp_log);

    /*if (ctx->app)      len += strlen(ctx->app)      + sizeof(" app=");
    if (ctx->tcurl)    len += strlen(ctx->tcurl)    + sizeof(" tcUrl=");
    if (ctx->pageurl)  len += strlen(ctx->pageurl)  + sizeof(" pageUrl=");
    if (ctx->flashver) len += strlen(ctx->flashver) + sizeof(" flashver=");

    if (ctx->conn) {
        char *sep, *p = ctx->conn;
        int options = 0;

        while (p) {
            options++;
            p += strspn(p, " ");
            if (!*p)
                break;
            sep = strchr(p, ' ');
            if (sep)
                p = sep + 1;
            else
                break;
        }
        len += options * sizeof(" conn=");
        len += strlen(ctx->conn);
    }

    if (ctx->playpath)
        len += strlen(ctx->playpath) + sizeof(" playpath=");
    if (ctx->live)
        len += sizeof(" live=1");
    if (ctx->subscribe)
        len += strlen(ctx->subscribe) + sizeof(" subscribe=");

    if (ctx->client_buffer_time)
        len += strlen(ctx->client_buffer_time) + sizeof(" buffer=");

    if (ctx->swfurl || ctx->swfverify) {
        len += sizeof(" swfUrl=");

        if (ctx->swfverify)
            len += strlen(ctx->swfverify) + sizeof(" swfVfy=1");
        else
            len += strlen(ctx->swfurl);
    }

    if (!(ctx->temp_filename = filename = av_malloc(len)))
        return AVERROR(ENOMEM);

    av_strlcpy(filename, s->filename, len);
    if (ctx->app) {
        av_strlcat(filename, " app=", len);
        av_strlcat(filename, ctx->app, len);
    }
    if (ctx->tcurl) {
        av_strlcat(filename, " tcUrl=", len);
        av_strlcat(filename, ctx->tcurl, len);
    }
    if (ctx->pageurl) {
        av_strlcat(filename, " pageUrl=", len);
        av_strlcat(filename, ctx->pageurl, len);
    }
    if (ctx->swfurl) {
        av_strlcat(filename, " swfUrl=", len);
        av_strlcat(filename, ctx->swfurl, len);
    }
    if (ctx->flashver) {
        av_strlcat(filename, " flashVer=", len);
        av_strlcat(filename, ctx->flashver, len);
    }
    if (ctx->conn) {
        char *sep, *p = ctx->conn;
        while (p) {
            av_strlcat(filename, " conn=", len);
            p += strspn(p, " ");
            if (!*p)
                break;
            sep = strchr(p, ' ');
            if (sep)
                *sep = '\0';
            av_strlcat(filename, p, len);

            if (sep)
                p = sep + 1;
        }
    }
    if (ctx->playpath) {
        av_strlcat(filename, " playpath=", len);
        av_strlcat(filename, ctx->playpath, len);
    }
    if (ctx->live)
        av_strlcat(filename, " live=1", len);
    if (ctx->subscribe) {
        av_strlcat(filename, " subscribe=", len);
        av_strlcat(filename, ctx->subscribe, len);
    }
    if (ctx->client_buffer_time) {
        av_strlcat(filename, " buffer=", len);
        av_strlcat(filename, ctx->client_buffer_time, len);
    }
    if (ctx->swfurl || ctx->swfverify) {
        av_strlcat(filename, " swfUrl=", len);

        if (ctx->swfverify) {
            av_strlcat(filename, ctx->swfverify, len);
            av_strlcat(filename, " swfVfy=1", len);
        } else {
            av_strlcat(filename, ctx->swfurl, len);
        }
    }

    RTMP_Init(r);
    if (!RTMP_SetupURL(r, filename)) {
        rc = AVERROR_UNKNOWN;
        goto fail;
    }

    if (flags & AVIO_FLAG_WRITE)
        RTMP_EnableWrite(r);

    if (!RTMP_Connect(r, NULL) || !RTMP_ConnectStream(r, 0)) {
        rc = AVERROR_UNKNOWN;
        goto fail;
    }

#if CONFIG_NETWORK
    if (ctx->buffer_size >= 0 && (flags & AVIO_FLAG_WRITE)) {
        int tmp = ctx->buffer_size;
        setsockopt(r->m_sb.sb_socket, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp));
    }
#endif

    s->is_streamed = 1;
    return 0;
fail:
    av_freep(&ctx->temp_filename);
    if (rc)
        RTMP_Close(r);*/

    av_log(NULL, AV_LOG_INFO, "uri : %s\n", uri);

    ctx->id = RTMFP_Connect("127.0.0.1", 1935, uri, onSocketError, onStatusEvent, NULL);

    av_log(NULL, AV_LOG_INFO, "RTMFP Connect called : %d\n", ctx->id);

    return rc;
}

static int rtmfp_write(URLContext *s, const uint8_t *buf, int size)
{
    /*LibRTMPContext *ctx = s->priv_data;
    RTMP *r = &ctx->rtmp;

    return RTMP_Write(r, buf, size);*/
    return 0;
}

static int rtmfp_read(URLContext *s, uint8_t *buf, int size)
{
    LibRTMFPContext *ctx = s->priv_data;
    //RTMP *r = &ctx->rtmp;

    av_log(NULL, AV_LOG_INFO, "RTMFP read called, size : %d\n", size);
    return RTMFP_Read(ctx->id, buf, size);
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

/*#define OFFSET(x) offsetof(LibRTMPContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"rtmp_app", "Name of application to connect to on the RTMP server", OFFSET(app), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_buffer", "Set buffer time in milliseconds. The default is 3000.", OFFSET(client_buffer_time), AV_OPT_TYPE_STRING, {.str = "3000"}, 0, 0, DEC|ENC},
    {"rtmp_conn", "Append arbitrary AMF data to the Connect message", OFFSET(conn), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_flashver", "Version of the Flash plugin used to run the SWF player.", OFFSET(flashver), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_live", "Specify that the media is a live stream.", OFFSET(live), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtmp_live"},
    {"any", "both", 0, AV_OPT_TYPE_CONST, {.i64 = -2}, 0, 0, DEC, "rtmp_live"},
    {"live", "live stream", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, DEC, "rtmp_live"},
    {"recorded", "recorded stream", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, 0, 0, DEC, "rtmp_live"},
    {"rtmp_pageurl", "URL of the web page in which the media was embedded. By default no value will be sent.", OFFSET(pageurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_playpath", "Stream identifier to play or to publish", OFFSET(playpath), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_subscribe", "Name of live stream to subscribe to. Defaults to rtmp_playpath.", OFFSET(subscribe), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_swfurl", "URL of the SWF player. By default no value will be sent", OFFSET(swfurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_swfverify", "URL to player swf file, compute hash/size automatically. (unimplemented)", OFFSET(swfverify), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_tcurl", "URL of the target stream. Defaults to proto://host[:port]/app.", OFFSET(tcurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
#if CONFIG_NETWORK
    {"rtmp_buffer_size", "set buffer size in bytes", OFFSET(buffer_size), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, DEC|ENC },
#endif
    { NULL },
};*/

//define RTMFP_CLASS(flavor)
static const AVClass librtmfp_class = {
    .class_name = "librtmfp protocol",
    .item_name  = av_default_item_name,
    //.option     = options,
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

