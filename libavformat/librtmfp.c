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
 * RTMFP protocol based on https://github.com/MonaSolutions/librtmfp librtmfp
 */

#include "libavutil/avstring.h"
//#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include <sys/time.h>

#include <librtmfp/librtmfp.h>

typedef struct LibRTMFPContext {
    const AVClass*      class;
    RTMFPConfig         rtmfp;
    unsigned int        id;
    int                 audioUnbuffered;
    int                 videoUnbuffered;
    int                 p2pPublishing;
    char*               peerId;
    char*               publication;
    unsigned short      streamId;
    const char*         swfUrl;
    const char*         app;
    const char*         pageUrl;
    const char*         flashVer;
    const char*         host;
    const char*         hostIPv6;

    // General options
    int                 socketReceiveSize;
    int                 socketSendSize;

    // NetGroup members
    RTMFPGroupConfig    group;
    char*               netgroup;
    unsigned int        updatePeriod;
    unsigned int        windowDuration;
    unsigned int        pushLimit;
    char*               fallbackUrl;
    unsigned int        fallbackTimeout;
    int                 disableRateCtl;
} LibRTMFPContext;

static void rtmfp_log(unsigned int level, const char* fileName, long line, const char* message)
{
    const char* strLevel = "";
    time_t today2 ;
    struct tm *today = NULL;
    struct timeval tv;

    switch (level) {
    default:
    case 1: level = AV_LOG_FATAL; strLevel = "FATAL"; break;
    case 2:
    case 3: level = AV_LOG_ERROR; strLevel = "ERROR"; break;
    case 4: level = AV_LOG_WARNING; strLevel = "WARN"; break;
    case 5:
    case 6: level = AV_LOG_INFO; strLevel = "INFO"; break;
    case 7: level = AV_LOG_DEBUG; strLevel = "DEBUG"; break;
    case 8: level = AV_LOG_TRACE; strLevel = "TRACE"; break;
    }

    today2 = time(NULL);
    today = localtime(&today2);
    gettimeofday(&tv, NULL);
    av_log(NULL, level, "%.2d:%.2d:%.2d.%d [%s] %s\n", today->tm_hour, today->tm_min, today->tm_sec, (int)((tv.tv_usec / 1000) / 100), strLevel, message);
}

/*static void rtmfp_dump(const char* header, const void* data, unsigned int size) {
    av_log(NULL, AV_LOG_DEBUG, "%s\n%.*s", header, size, (const char*)data);
}*/

static int rtmfp_close(URLContext *s)
{
    LibRTMFPContext *ctx = s->priv_data;

    av_log(NULL, AV_LOG_INFO, "Closing RTMFP connection...\n");
    //RTMFP_Close(ctx->id, 1);
    RTMFP_Terminate();
    return 0;
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
    int level = 0;

    switch (av_log_get_level()) {
        case AV_LOG_FATAL:   level = 1; break;
        case AV_LOG_ERROR:   level = 3; break;
        case AV_LOG_WARNING: level = 4; break;
        default:
        case AV_LOG_INFO:    level = 6; break;
        case AV_LOG_DEBUG:   level = 7; break;
        case AV_LOG_VERBOSE: level = 8; break;
        case AV_LOG_TRACE:   level = 8; break;
    }

    RTMFP_SetIntParameter("socketReceiveSize", ctx->socketReceiveSize);
    RTMFP_SetIntParameter("socketSendSize", ctx->socketSendSize);
    RTMFP_SetIntParameter("timeoutFallback", ctx->fallbackTimeout);
    RTMFP_SetIntParameter("logLevel", level);

    RTMFP_Init(&ctx->rtmfp, &ctx->group, 1);
    ctx->rtmfp.pOnStatusEvent = onStatusEvent;
    ctx->rtmfp.isBlocking = 1;
    ctx->rtmfp.swfUrl = ctx->swfUrl;
    ctx->rtmfp.app = ctx->app;
    ctx->rtmfp.pageUrl = ctx->pageUrl;
    ctx->rtmfp.flashVer = ctx->flashVer;
    ctx->rtmfp.host = ctx->host;
    ctx->rtmfp.hostIPv6 = ctx->hostIPv6;

    RTMFP_LogSetCallback(rtmfp_log);
    /*RTMFP_ActiveDump();
    RTMFP_DumpSetCallback(rtmfp_dump);*/
    RTMFP_InterruptSetCallback(s->interrupt_callback.callback, s->interrupt_callback.opaque);

    RTMFP_GetPublicationAndUrlFromUri(uri, &ctx->publication);

    if ((ctx->id = RTMFP_Connect(uri, &ctx->rtmfp)) == 0)
        return -1;

    av_log(NULL, AV_LOG_INFO, "RTMFP Connect called : %d\n", ctx->id);

    // Wait for connection to happen
    if (RTMFP_WaitForEvent(ctx->id, RTMFP_CONNECTED) == 0)
        return -1;

    if (ctx->netgroup) {
        ctx->group.netGroup = ctx->netgroup;
        ctx->group.availabilityUpdatePeriod = ctx->updatePeriod;
        ctx->group.windowDuration = ctx->windowDuration;
        ctx->group.pushLimit = ctx->pushLimit;
        ctx->group.isPublisher = (flags & AVIO_FLAG_WRITE) > 1;
        ctx->group.isBlocking = 1;
        ctx->group.disableRateControl = ctx->disableRateCtl>0;
        ctx->streamId = RTMFP_Connect2Group(ctx->id, ctx->publication, &ctx->rtmfp, &ctx->group, !ctx->audioUnbuffered, !ctx->videoUnbuffered, ctx->fallbackUrl);
    } else if (ctx->peerId)
        ctx->streamId = RTMFP_Connect2Peer(ctx->id, ctx->peerId, ctx->publication, 1);
    else if (ctx->p2pPublishing)
        ctx->streamId = RTMFP_PublishP2P(ctx->id, ctx->publication, !ctx->audioUnbuffered, !ctx->videoUnbuffered, 1);
    else if (flags & AVIO_FLAG_WRITE)
        ctx->streamId = RTMFP_Publish(ctx->id, ctx->publication, !ctx->audioUnbuffered, !ctx->videoUnbuffered, 1);
    else
        ctx->streamId = RTMFP_Play(ctx->id, ctx->publication);

    if (!ctx->streamId)
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
    return (res < 0)? AVERROR_UNKNOWN : res;
}

static int rtmfp_read(URLContext *s, uint8_t *buf, int size)
{
    LibRTMFPContext *ctx = s->priv_data;
    int res = 0;

    res = RTMFP_Read(ctx->streamId, ctx->id, buf, size);
    //av_log(NULL, AV_LOG_INFO, "RTMFP read called, %d/%d bytes read\n", res, size);

    return (res < 0)? AVERROR_UNKNOWN : res;
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
    {"socketReceiveSize", "Socket receive buffer size", OFFSET(socketReceiveSize), AV_OPT_TYPE_INT, {.i64 = 212992}, 0, 0x0FFFFFFF, DEC|ENC},
    {"socketSendSize", "Socket send buffer size", OFFSET(socketSendSize), AV_OPT_TYPE_INT, {.i64 = 212992}, 0, 0x0FFFFFFF, DEC|ENC},
    {"audioUnbuffered", "Unbuffered audio mode (default to false)", OFFSET(audioUnbuffered), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"videoUnbuffered", "Unbuffered video mode (default to false)", OFFSET(videoUnbuffered), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"peerId", "Connect to a peer for playing", OFFSET(peerId), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"p2pPublishing", "Publish the stream in p2p mode (default to false)", OFFSET(p2pPublishing), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"netgroup", "Publish/Play the stream into a NetGroup (multicast)", OFFSET(netgroup), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"fallbackUrl", "Try to play a unicast stream url until the NetGroup connection is not ready (can produce undefined behavior if the stream codecs are different)",
        OFFSET(fallbackUrl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"fallbackTimeout", "Set the timeout in milliseconds to start fallback to unicast", OFFSET(fallbackTimeout), AV_OPT_TYPE_INT, {.i64 = 8000 }, 0, 120000, DEC|ENC},
    {"disableRateControl", "For Netgroup disable the P2P connection rate control to avoid disconnection", OFFSET(disableRateCtl), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, DEC|ENC},
    {"pushLimit", "Specifies the maximum number (-1) of peers to which we will send push fragments", OFFSET(pushLimit), AV_OPT_TYPE_INT, {.i64 = 4 }, 0, 255, DEC|ENC},
    {"updatePeriod", "Specifies the interval in milliseconds between messages sent to peers informating them that the local node has new p2p multicast media fragments available",
        OFFSET(updatePeriod), AV_OPT_TYPE_INT, {.i64 = 100 }, 100, 10000, DEC|ENC},
    {"windowDuration", "Specifies the duration in milliseconds of the p2p multicast reassembly window", OFFSET(windowDuration), AV_OPT_TYPE_INT, {.i64 = 8000 }, 1000, 60000, DEC|ENC},
    {"rtmfp_swfurl", "URL of the SWF player. By default no value will be sent", OFFSET(swfUrl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmfp_app", "Name of application to connect to on the RTMFP server (by default 'live')", OFFSET(app), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmfp_pageurl", "URL of the web page in which the media was embedded. By default no value will be sent.", OFFSET(pageUrl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmfp_flashver", "Version of the Flash plugin used to run the SWF player. By default 'WIN 20,0,0,286'", OFFSET(flashVer), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmfp_host", "IPv4 host address to bind to (use this if you ave multiple interfaces)", OFFSET(host), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmfp_hostIPv6", "IPv6 host address to bind to (use this if you ave multiple interfaces)", OFFSET(hostIPv6), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    { NULL },
};

static const AVClass librtmfp_class = {
    .class_name = "librtmfp protocol",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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

