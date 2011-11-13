/*
 * RTP network protocol
 * Copyright (c) 2002 Fabrice Bellard
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
 * RTP protocol
 */

#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "avformat.h"
#include "avio_internal.h"
#include "rtpdec.h"
#include "url.h"

#include <unistd.h>
#include <stdarg.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include <fcntl.h>
#if HAVE_POLL_H
#include <sys/poll.h>
#endif
#include <sys/time.h>

#define RTP_TX_BUF_SIZE  (64 * 1024)
#define RTP_RX_BUF_SIZE  (128 * 1024)

typedef struct RTPContext {
    URLContext *rtp_hd, *rtcp_hd;
    int rtp_fd, rtcp_fd;
} RTPContext;

/**
 * If no filename is given to av_open_input_file because you want to
 * get the local port first, then you must call this function to set
 * the remote server address.
 *
 * @param h media file context
 * @param uri of the remote server
 * @return zero if no error.
 */

int ff_rtp_set_remote_url(URLContext *h, const char *uri)
{
    RTPContext *s = h->priv_data;
    char hostname[256];
    int port;

    char buf[1024];
    char path[1024];

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
                 path, sizeof(path), uri);

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, port, "%s", path);
    ff_udp_set_remote_url(s->rtp_hd, buf);

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, port + 1, "%s", path);
    ff_udp_set_remote_url(s->rtcp_hd, buf);
    return 0;
}


/**
 * add option to url of the form:
 * "http://host:port/path?option1=val1&option2=val2...
 */

static av_printf_format(3, 4) void url_add_option(char *buf, int buf_size, const char *fmt, ...)
{
    char buf1[1024];
    va_list ap;

    va_start(ap, fmt);
    if (strchr(buf, '?'))
        av_strlcat(buf, "&", buf_size);
    else
        av_strlcat(buf, "?", buf_size);
    vsnprintf(buf1, sizeof(buf1), fmt, ap);
    av_strlcat(buf, buf1, buf_size);
    va_end(ap);
}

static void build_udp_url(char *buf, int buf_size,
                          const char *hostname, int port,
                          int local_port, int ttl,
                          int max_packet_size, int connect)
{
    ff_url_join(buf, buf_size, "udp", NULL, hostname, port, NULL);
    if (local_port >= 0)
        url_add_option(buf, buf_size, "localport=%d", local_port);
    if (ttl >= 0)
        url_add_option(buf, buf_size, "ttl=%d", ttl);
    if (max_packet_size >=0)
        url_add_option(buf, buf_size, "pkt_size=%d", max_packet_size);
    if (connect)
        url_add_option(buf, buf_size, "connect=1");
    url_add_option(buf, buf_size, "fifo_size=0");
}

/**
 * url syntax: rtp://host:port[?option=val...]
 * option: 'ttl=n'            : set the ttl value (for multicast only)
 *         'rtcpport=n'       : set the remote rtcp port to n
 *         'localrtpport=n'   : set the local rtp port to n
 *         'localrtcpport=n'  : set the local rtcp port to n
 *         'pkt_size=n'       : set max packet size
 *         'connect=0/1'      : do a connect() on the UDP socket
 * deprecated option:
 *         'localport=n'      : set the local port to n
 *
 * if rtcpport isn't set the rtcp port will be the rtp port + 1
 * if local rtp port isn't set any available port will be used for the local
 * rtp and rtcp ports
 * if the local rtcp port is not set it will be the local rtp port + 1
 */

static int rtp_open(URLContext *h, const char *uri, int flags)
{
    RTPContext *s;
    int rtp_port, rtcp_port,
        ttl, connect,
        local_rtp_port, local_rtcp_port, max_packet_size;
    char hostname[256];
    char buf[1024];
    char path[1024];
    const char *p;

    s = av_mallocz(sizeof(RTPContext));
    if (!s)
        return AVERROR(ENOMEM);
    h->priv_data = s;

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &rtp_port,
                 path, sizeof(path), uri);
    /* extract parameters */
    ttl = -1;
    rtcp_port = rtp_port+1;
    local_rtp_port = -1;
    local_rtcp_port = -1;
    max_packet_size = -1;
    connect = 0;

    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "ttl", p)) {
            ttl = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "rtcpport", p)) {
            rtcp_port = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "localport", p)) {
            local_rtp_port = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "localrtpport", p)) {
            local_rtp_port = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "localrtcpport", p)) {
            local_rtcp_port = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            max_packet_size = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "connect", p)) {
            connect = strtol(buf, NULL, 10);
        }
    }

    build_udp_url(buf, sizeof(buf),
                  hostname, rtp_port, local_rtp_port, ttl, max_packet_size,
                  connect);
    if (ffurl_open(&s->rtp_hd, buf, flags, &h->interrupt_callback, NULL) < 0)
        goto fail;
    if (local_rtp_port>=0 && local_rtcp_port<0)
        local_rtcp_port = ff_udp_get_local_port(s->rtp_hd) + 1;

    build_udp_url(buf, sizeof(buf),
                  hostname, rtcp_port, local_rtcp_port, ttl, max_packet_size,
                  connect);
    if (ffurl_open(&s->rtcp_hd, buf, flags, &h->interrupt_callback, NULL) < 0)
        goto fail;

    /* just to ease handle access. XXX: need to suppress direct handle
       access */
    s->rtp_fd = ffurl_get_file_handle(s->rtp_hd);
    s->rtcp_fd = ffurl_get_file_handle(s->rtcp_hd);

    h->max_packet_size = s->rtp_hd->max_packet_size;
    h->is_streamed = 1;
    return 0;

 fail:
    if (s->rtp_hd)
        ffurl_close(s->rtp_hd);
    if (s->rtcp_hd)
        ffurl_close(s->rtcp_hd);
    av_free(s);
    return AVERROR(EIO);
}

static int rtp_read(URLContext *h, uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    struct sockaddr_storage from;
    socklen_t from_len;
    int len, n;
    struct pollfd p[2] = {{s->rtp_fd, POLLIN, 0}, {s->rtcp_fd, POLLIN, 0}};

    for(;;) {
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
        /* build fdset to listen to RTP and RTCP packets */
        n = poll(p, 2, 100);
        if (n > 0) {
            /* first try RTCP */
            if (p[1].revents & POLLIN) {
                from_len = sizeof(from);
                len = recvfrom (s->rtcp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (ff_neterrno() == AVERROR(EAGAIN) ||
                        ff_neterrno() == AVERROR(EINTR))
                        continue;
                    return AVERROR(EIO);
                }
                break;
            }
            /* then RTP */
            if (p[0].revents & POLLIN) {
                from_len = sizeof(from);
                len = recvfrom (s->rtp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (ff_neterrno() == AVERROR(EAGAIN) ||
                        ff_neterrno() == AVERROR(EINTR))
                        continue;
                    return AVERROR(EIO);
                }
                break;
            }
        } else if (n < 0) {
            if (ff_neterrno() == AVERROR(EINTR))
                continue;
            return AVERROR(EIO);
        }
    }
    return len;
}

static int rtp_write(URLContext *h, const uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    int ret;
    URLContext *hd;

    if (buf[1] >= RTCP_SR && buf[1] <= RTCP_APP) {
        /* RTCP payload type */
        hd = s->rtcp_hd;
    } else {
        /* RTP payload type */
        hd = s->rtp_hd;
    }

    ret = ffurl_write(hd, buf, size);
    return ret;
}

static int rtp_close(URLContext *h)
{
    RTPContext *s = h->priv_data;

    ffurl_close(s->rtp_hd);
    ffurl_close(s->rtcp_hd);
    av_free(s);
    return 0;
}

/**
 * Return the local rtp port used by the RTP connection
 * @param h media file context
 * @return the local port number
 */

int ff_rtp_get_local_rtp_port(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return ff_udp_get_local_port(s->rtp_hd);
}

/**
 * Return the local rtcp port used by the RTP connection
 * @param h media file context
 * @return the local port number
 */

int ff_rtp_get_local_rtcp_port(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return ff_udp_get_local_port(s->rtcp_hd);
}

static int rtp_get_file_handle(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return s->rtp_fd;
}

int ff_rtp_get_rtcp_file_handle(URLContext *h) {
    RTPContext *s = h->priv_data;
    return s->rtcp_fd;
}

URLProtocol ff_rtp_protocol = {
    .name                = "rtp",
    .url_open            = rtp_open,
    .url_read            = rtp_read,
    .url_write           = rtp_write,
    .url_close           = rtp_close,
    .url_get_file_handle = rtp_get_file_handle,
};
