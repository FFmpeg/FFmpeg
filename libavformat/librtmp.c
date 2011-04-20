/*
 * RTMP network protocol
 * Copyright (c) 2010 Howard Chu
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
 * RTMP protocol based on http://rtmpdump.mplayerhq.hu/ librtmp
 */

#include "avformat.h"
#include "url.h"

#include <librtmp/rtmp.h>
#include <librtmp/log.h>

static void rtmp_log(int level, const char *fmt, va_list args)
{
    switch (level) {
    default:
    case RTMP_LOGCRIT:    level = AV_LOG_FATAL;   break;
    case RTMP_LOGERROR:   level = AV_LOG_ERROR;   break;
    case RTMP_LOGWARNING: level = AV_LOG_WARNING; break;
    case RTMP_LOGINFO:    level = AV_LOG_INFO;    break;
    case RTMP_LOGDEBUG:   level = AV_LOG_VERBOSE; break;
    case RTMP_LOGDEBUG2:  level = AV_LOG_DEBUG;   break;
    }

    av_vlog(NULL, level, fmt, args);
    av_log(NULL, level, "\n");
}

static int rtmp_close(URLContext *s)
{
    RTMP *r = s->priv_data;

    RTMP_Close(r);
    av_free(r);
    return 0;
}

/**
 * Open RTMP connection and verify that the stream can be played.
 *
 * URL syntax: rtmp://server[:port][/app][/playpath][ keyword=value]...
 *             where 'app' is first one or two directories in the path
 *             (e.g. /ondemand/, /flash/live/, etc.)
 *             and 'playpath' is a file name (the rest of the path,
 *             may be prefixed with "mp4:")
 *
 *             Additional RTMP library options may be appended as
 *             space-separated key-value pairs.
 */
static int rtmp_open(URLContext *s, const char *uri, int flags)
{
    RTMP *r;
    int rc;

    r = av_mallocz(sizeof(RTMP));
    if (!r)
        return AVERROR(ENOMEM);

    switch (av_log_get_level()) {
    default:
    case AV_LOG_FATAL:   rc = RTMP_LOGCRIT;    break;
    case AV_LOG_ERROR:   rc = RTMP_LOGERROR;   break;
    case AV_LOG_WARNING: rc = RTMP_LOGWARNING; break;
    case AV_LOG_INFO:    rc = RTMP_LOGINFO;    break;
    case AV_LOG_VERBOSE: rc = RTMP_LOGDEBUG;   break;
    case AV_LOG_DEBUG:   rc = RTMP_LOGDEBUG2;  break;
    }
    RTMP_LogSetLevel(rc);
    RTMP_LogSetCallback(rtmp_log);

    RTMP_Init(r);
    if (!RTMP_SetupURL(r, s->filename)) {
        rc = -1;
        goto fail;
    }

    if (flags & AVIO_FLAG_WRITE)
        RTMP_EnableWrite(r);

    if (!RTMP_Connect(r, NULL) || !RTMP_ConnectStream(r, 0)) {
        rc = -1;
        goto fail;
    }

    s->priv_data   = r;
    s->is_streamed = 1;
    return 0;
fail:
    av_free(r);
    return rc;
}

static int rtmp_write(URLContext *s, const uint8_t *buf, int size)
{
    RTMP *r = s->priv_data;

    return RTMP_Write(r, buf, size);
}

static int rtmp_read(URLContext *s, uint8_t *buf, int size)
{
    RTMP *r = s->priv_data;

    return RTMP_Read(r, buf, size);
}

static int rtmp_read_pause(URLContext *s, int pause)
{
    RTMP *r = s->priv_data;

    if (!RTMP_Pause(r, pause))
        return -1;
    return 0;
}

static int64_t rtmp_read_seek(URLContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    RTMP *r = s->priv_data;

    if (flags & AVSEEK_FLAG_BYTE)
        return AVERROR(ENOSYS);

    /* seeks are in milliseconds */
    if (stream_index < 0)
        timestamp = av_rescale_rnd(timestamp, 1000, AV_TIME_BASE,
            flags & AVSEEK_FLAG_BACKWARD ? AV_ROUND_DOWN : AV_ROUND_UP);

    if (!RTMP_SendSeek(r, timestamp))
        return -1;
    return timestamp;
}

static int rtmp_get_file_handle(URLContext *s)
{
    RTMP *r = s->priv_data;

    return RTMP_Socket(r);
}

URLProtocol ff_rtmp_protocol = {
    .name                = "rtmp",
    .url_open            = rtmp_open,
    .url_read            = rtmp_read,
    .url_write           = rtmp_write,
    .url_close           = rtmp_close,
    .url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle
};

URLProtocol ff_rtmpt_protocol = {
    .name                = "rtmpt",
    .url_open            = rtmp_open,
    .url_read            = rtmp_read,
    .url_write           = rtmp_write,
    .url_close           = rtmp_close,
    .url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle
};

URLProtocol ff_rtmpe_protocol = {
    .name                = "rtmpe",
    .url_open            = rtmp_open,
    .url_read            = rtmp_read,
    .url_write           = rtmp_write,
    .url_close           = rtmp_close,
    .url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle
};

URLProtocol ff_rtmpte_protocol = {
    .name                = "rtmpte",
    .url_open            = rtmp_open,
    .url_read            = rtmp_read,
    .url_write           = rtmp_write,
    .url_close           = rtmp_close,
    .url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle
};

URLProtocol ff_rtmps_protocol = {
    .name                = "rtmps",
    .url_open            = rtmp_open,
    .url_read            = rtmp_read,
    .url_write           = rtmp_write,
    .url_close           = rtmp_close,
    .url_read_pause      = rtmp_read_pause,
    .url_read_seek       = rtmp_read_seek,
    .url_get_file_handle = rtmp_get_file_handle
};
