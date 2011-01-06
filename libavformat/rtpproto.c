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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "rtpdec.h"

#include <unistd.h>
#include <stdarg.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include <fcntl.h>
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
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

int rtp_set_remote_url(URLContext *h, const char *uri)
{
    RTPContext *s = h->priv_data;
    char hostname[256];
    int port;

    char buf[1024];
    char path[1024];

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
                 path, sizeof(path), uri);

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, port, "%s", path);
    udp_set_remote_url(s->rtp_hd, buf);

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, port + 1, "%s", path);
    udp_set_remote_url(s->rtcp_hd, buf);
    return 0;
}


/**
 * add option to url of the form:
 * "http://host:port/path?option1=val1&option2=val2...
 */

static void url_add_option(char *buf, int buf_size, const char *fmt, ...)
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
        is_output, ttl, connect,
        local_rtp_port, local_rtcp_port, max_packet_size;
    char hostname[256];
    char buf[1024];
    char path[1024];
    const char *p;

    is_output = (flags & URL_WRONLY);

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
        if (find_info_tag(buf, sizeof(buf), "ttl", p)) {
            ttl = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "rtcpport", p)) {
            rtcp_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localport", p)) {
            local_rtp_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localrtpport", p)) {
            local_rtp_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localrtcpport", p)) {
            local_rtcp_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            max_packet_size = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "connect", p)) {
            connect = strtol(buf, NULL, 10);
        }
    }

    build_udp_url(buf, sizeof(buf),
                  hostname, rtp_port, local_rtp_port, ttl, max_packet_size,
                  connect);
    if (url_open(&s->rtp_hd, buf, flags) < 0)
        goto fail;
    if (local_rtp_port>=0 && local_rtcp_port<0)
        local_rtcp_port = udp_get_local_port(s->rtp_hd) + 1;

    build_udp_url(buf, sizeof(buf),
                  hostname, rtcp_port, local_rtcp_port, ttl, max_packet_size,
                  connect);
    if (url_open(&s->rtcp_hd, buf, flags) < 0)
        goto fail;

    /* just to ease handle access. XXX: need to suppress direct handle
       access */
    s->rtp_fd = url_get_file_handle(s->rtp_hd);
    s->rtcp_fd = url_get_file_handle(s->rtcp_hd);

    h->max_packet_size = url_get_max_packet_size(s->rtp_hd);
    h->is_streamed = 1;
    return 0;

 fail:
    if (s->rtp_hd)
        url_close(s->rtp_hd);
    if (s->rtcp_hd)
        url_close(s->rtcp_hd);
    av_free(s);
    return AVERROR(EIO);
}

static int rtp_read(URLContext *h, uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    struct sockaddr_storage from;
    socklen_t from_len;
    int len, fd_max, n;
    fd_set rfds;
    struct timeval tv;
#if 0
    for(;;) {
        from_len = sizeof(from);
        len = recvfrom (s->rtp_fd, buf, size, 0,
                        (struct sockaddr *)&from, &from_len);
        if (len < 0) {
            if (ff_neterrno() == FF_NETERROR(EAGAIN) ||
                ff_neterrno() == FF_NETERROR(EINTR))
                continue;
            return AVERROR(EIO);
        }
        break;
    }
#else
    for(;;) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        /* build fdset to listen to RTP and RTCP packets */
        FD_ZERO(&rfds);
        fd_max = s->rtp_fd;
        FD_SET(s->rtp_fd, &rfds);
        if (s->rtcp_fd > fd_max)
            fd_max = s->rtcp_fd;
        FD_SET(s->rtcp_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        n = select(fd_max + 1, &rfds, NULL, NULL, &tv);
        if (n > 0) {
            /* first try RTCP */
            if (FD_ISSET(s->rtcp_fd, &rfds)) {
                from_len = sizeof(from);
                len = recvfrom (s->rtcp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (ff_neterrno() == FF_NETERROR(EAGAIN) ||
                        ff_neterrno() == FF_NETERROR(EINTR))
                        continue;
                    return AVERROR(EIO);
                }
                break;
            }
            /* then RTP */
            if (FD_ISSET(s->rtp_fd, &rfds)) {
                from_len = sizeof(from);
                len = recvfrom (s->rtp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (ff_neterrno() == FF_NETERROR(EAGAIN) ||
                        ff_neterrno() == FF_NETERROR(EINTR))
                        continue;
                    return AVERROR(EIO);
                }
                break;
            }
        } else if (n < 0) {
            if (ff_neterrno() == FF_NETERROR(EINTR))
                continue;
            return AVERROR(EIO);
        }
    }
#endif
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

    ret = url_write(hd, buf, size);
#if 0
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10 * 1000000;
        nanosleep(&ts, NULL);
    }
#endif
    return ret;
}

static int rtp_close(URLContext *h)
{
    RTPContext *s = h->priv_data;

    url_close(s->rtp_hd);
    url_close(s->rtcp_hd);
    av_free(s);
    return 0;
}

/**
 * Return the local rtp port used by the RTP connection
 * @param h media file context
 * @return the local port number
 */

int rtp_get_local_rtp_port(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return udp_get_local_port(s->rtp_hd);
}

/**
 * Return the local rtcp port used by the RTP connection
 * @param h media file context
 * @return the local port number
 */

int rtp_get_local_rtcp_port(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return udp_get_local_port(s->rtcp_hd);
}

static int rtp_get_file_handle(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return s->rtp_fd;
}

int rtp_get_rtcp_file_handle(URLContext *h) {
    RTPContext *s = h->priv_data;
    return s->rtcp_fd;
}

URLProtocol rtp_protocol = {
    "rtp",
    rtp_open,
    rtp_read,
    rtp_write,
    NULL, /* seek */
    rtp_close,
    .url_get_file_handle = rtp_get_file_handle,
};
