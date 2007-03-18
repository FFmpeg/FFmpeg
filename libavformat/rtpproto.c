/*
 * RTP network protocol
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "avformat.h"

#include <unistd.h>
#include <stdarg.h>
#include "network.h"
#include <fcntl.h>

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
 * @param s1 media file context
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

    url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
              path, sizeof(path), uri);

    snprintf(buf, sizeof(buf), "udp://%s:%d%s", hostname, port, path);
    udp_set_remote_url(s->rtp_hd, buf);

    snprintf(buf, sizeof(buf), "udp://%s:%d%s", hostname, port + 1, path);
    udp_set_remote_url(s->rtcp_hd, buf);
    return 0;
}


/* add option to url of the form:
   "http://host:port/path?option1=val1&option2=val2... */
static void url_add_option(char *buf, int buf_size, const char *fmt, ...)
{
    char buf1[1024];
    va_list ap;

    va_start(ap, fmt);
    if (strchr(buf, '?'))
        pstrcat(buf, buf_size, "&");
    else
        pstrcat(buf, buf_size, "?");
    vsnprintf(buf1, sizeof(buf1), fmt, ap);
    pstrcat(buf, buf_size, buf1);
    va_end(ap);
}

static void build_udp_url(char *buf, int buf_size,
                          const char *hostname, int port,
                          int local_port, int multicast, int ttl)
{
    snprintf(buf, buf_size, "udp://%s:%d", hostname, port);
    if (local_port >= 0)
        url_add_option(buf, buf_size, "localport=%d", local_port);
    if (multicast)
        url_add_option(buf, buf_size, "multicast=1");
    if (ttl >= 0)
        url_add_option(buf, buf_size, "ttl=%d", ttl);
}

/*
 * url syntax: rtp://host:port[?option=val...]
 * option: 'multicast=1' : enable multicast
 *         'ttl=n'       : set the ttl value (for multicast only)
 *         'localport=n' : set the local port to n
 *
 */
static int rtp_open(URLContext *h, const char *uri, int flags)
{
    RTPContext *s;
    int port, is_output, is_multicast, ttl, local_port;
    char hostname[256];
    char buf[1024];
    char path[1024];
    const char *p;

    is_output = (flags & URL_WRONLY);

    s = av_mallocz(sizeof(RTPContext));
    if (!s)
        return AVERROR(ENOMEM);
    h->priv_data = s;

    url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
              path, sizeof(path), uri);
    /* extract parameters */
    is_multicast = 0;
    ttl = -1;
    local_port = -1;
    p = strchr(uri, '?');
    if (p) {
        is_multicast = find_info_tag(buf, sizeof(buf), "multicast", p);
        if (find_info_tag(buf, sizeof(buf), "ttl", p)) {
            ttl = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localport", p)) {
            local_port = strtol(buf, NULL, 10);
        }
    }

    build_udp_url(buf, sizeof(buf),
                  hostname, port, local_port, is_multicast, ttl);
    if (url_open(&s->rtp_hd, buf, flags) < 0)
        goto fail;
    local_port = udp_get_local_port(s->rtp_hd);
    /* XXX: need to open another connexion if the port is not even */

    /* well, should suppress localport in path */

    build_udp_url(buf, sizeof(buf),
                  hostname, port + 1, local_port + 1, is_multicast, ttl);
    if (url_open(&s->rtcp_hd, buf, flags) < 0)
        goto fail;

    /* just to ease handle access. XXX: need to suppress direct handle
       access */
    s->rtp_fd = udp_get_file_handle(s->rtp_hd);
    s->rtcp_fd = udp_get_file_handle(s->rtcp_hd);

    h->max_packet_size = url_get_max_packet_size(s->rtp_hd);
    h->is_streamed = 1;
    return 0;

 fail:
    if (s->rtp_hd)
        url_close(s->rtp_hd);
    if (s->rtcp_hd)
        url_close(s->rtcp_hd);
    av_free(s);
    return AVERROR_IO;
}

static int rtp_read(URLContext *h, uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    struct sockaddr_in from;
    socklen_t from_len;
    int len, fd_max, n;
    fd_set rfds;
#if 0
    for(;;) {
        from_len = sizeof(from);
        len = recvfrom (s->rtp_fd, buf, size, 0,
                        (struct sockaddr *)&from, &from_len);
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return AVERROR_IO;
        }
        break;
    }
#else
    for(;;) {
        /* build fdset to listen to RTP and RTCP packets */
        FD_ZERO(&rfds);
        fd_max = s->rtp_fd;
        FD_SET(s->rtp_fd, &rfds);
        if (s->rtcp_fd > fd_max)
            fd_max = s->rtcp_fd;
        FD_SET(s->rtcp_fd, &rfds);
        n = select(fd_max + 1, &rfds, NULL, NULL, NULL);
        if (n > 0) {
            /* first try RTCP */
            if (FD_ISSET(s->rtcp_fd, &rfds)) {
                from_len = sizeof(from);
                len = recvfrom (s->rtcp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (errno == EAGAIN || errno == EINTR)
                        continue;
                    return AVERROR_IO;
                }
                break;
            }
            /* then RTP */
            if (FD_ISSET(s->rtp_fd, &rfds)) {
                from_len = sizeof(from);
                len = recvfrom (s->rtp_fd, buf, size, 0,
                                (struct sockaddr *)&from, &from_len);
                if (len < 0) {
                    if (errno == EAGAIN || errno == EINTR)
                        continue;
                    return AVERROR_IO;
                }
                break;
            }
        }
    }
#endif
    return len;
}

static int rtp_write(URLContext *h, uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    int ret;
    URLContext *hd;

    if (buf[1] >= 200 && buf[1] <= 204) {
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
 * Return the local port used by the RTP connexion
 * @param s1 media file context
 * @return the local port number
 */
int rtp_get_local_port(URLContext *h)
{
    RTPContext *s = h->priv_data;
    return udp_get_local_port(s->rtp_hd);
}

/**
 * Return the rtp and rtcp file handles for select() usage to wait for several RTP
 * streams at the same time.
 * @param h media file context
 */
void rtp_get_file_handles(URLContext *h, int *prtp_fd, int *prtcp_fd)
{
    RTPContext *s = h->priv_data;

    *prtp_fd = s->rtp_fd;
    *prtcp_fd = s->rtcp_fd;
}

URLProtocol rtp_protocol = {
    "rtp",
    rtp_open,
    rtp_read,
    rtp_write,
    NULL, /* seek */
    rtp_close,
};
