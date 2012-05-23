/*
 * TCP protocol
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
#include "avformat.h"
#include "libavutil/parseutils.h"
#include <unistd.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#if HAVE_POLL_H
#include <poll.h>
#endif
#include <sys/time.h>

typedef struct TCPContext {
    int fd;
} TCPContext;

/* return non zero if error */
static int tcp_open(URLContext *h, const char *uri, int flags)
{
    struct addrinfo hints = { 0 }, *ai, *cur_ai;
    int port, fd = -1;
    TCPContext *s = h->priv_data;
    int listen_socket = 0;
    const char *p;
    char buf[256];
    int ret;
    socklen_t optlen;
    int timeout = 50;
    char hostname[1024],proto[1024],path[1024];
    char portstr[10];

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto,"tcp") || port <= 0 || port >= 65536)
        return AVERROR(EINVAL);

    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "listen", p))
            listen_socket = 1;
        if (av_find_info_tag(buf, sizeof(buf), "timeout", p)) {
            timeout = strtol(buf, NULL, 10);
        }
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    ret = getaddrinfo(hostname, portstr, &hints, &ai);
    if (ret) {
        av_log(h, AV_LOG_ERROR,
               "Failed to resolve hostname %s: %s\n",
               hostname, gai_strerror(ret));
        return AVERROR(EIO);
    }

    cur_ai = ai;

 restart:
    ret = AVERROR(EIO);
    fd = socket(cur_ai->ai_family, cur_ai->ai_socktype, cur_ai->ai_protocol);
    if (fd < 0)
        goto fail;

    if (listen_socket) {
        int fd1;
        ret = bind(fd, cur_ai->ai_addr, cur_ai->ai_addrlen);
        listen(fd, 1);
        fd1 = accept(fd, NULL, NULL);
        closesocket(fd);
        fd = fd1;
        ff_socket_nonblock(fd, 1);
    } else {
 redo:
        ff_socket_nonblock(fd, 1);
        ret = connect(fd, cur_ai->ai_addr, cur_ai->ai_addrlen);
    }

    if (ret < 0) {
        struct pollfd p = {fd, POLLOUT, 0};
        ret = ff_neterrno();
        if (ret == AVERROR(EINTR)) {
            if (ff_check_interrupt(&h->interrupt_callback)) {
                ret = AVERROR_EXIT;
                goto fail1;
            }
            goto redo;
        }
        if (ret != AVERROR(EINPROGRESS) &&
            ret != AVERROR(EAGAIN))
            goto fail;

        /* wait until we are connected or until abort */
        while(timeout--) {
            if (ff_check_interrupt(&h->interrupt_callback)) {
                ret = AVERROR_EXIT;
                goto fail1;
            }
            ret = poll(&p, 1, 100);
            if (ret > 0)
                break;
        }
        if (ret <= 0) {
            ret = AVERROR(ETIMEDOUT);
            goto fail;
        }
        /* test error */
        optlen = sizeof(ret);
        getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &optlen);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR,
                   "TCP connection to %s:%d failed: %s\n",
                   hostname, port, strerror(ret));
            ret = AVERROR(ret);
            goto fail;
        }
    }
    h->is_streamed = 1;
    s->fd = fd;
    freeaddrinfo(ai);
    return 0;

 fail:
    if (cur_ai->ai_next) {
        /* Retry with the next sockaddr */
        cur_ai = cur_ai->ai_next;
        if (fd >= 0)
            closesocket(fd);
        goto restart;
    }
 fail1:
    if (fd >= 0)
        closesocket(fd);
    freeaddrinfo(ai);
    return ret;
}

static int tcp_read(URLContext *h, uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd(s->fd, 0);
        if (ret < 0)
            return ret;
    }
    ret = recv(s->fd, buf, size, 0);
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_write(URLContext *h, const uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd(s->fd, 1);
        if (ret < 0)
            return ret;
    }
    ret = send(s->fd, buf, size, 0);
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_shutdown(URLContext *h, int flags)
{
    TCPContext *s = h->priv_data;
    int how;

    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ) {
        how = SHUT_RDWR;
    } else if (flags & AVIO_FLAG_WRITE) {
        how = SHUT_WR;
    } else {
        how = SHUT_RD;
    }

    return shutdown(s->fd, how);
}

static int tcp_close(URLContext *h)
{
    TCPContext *s = h->priv_data;
    closesocket(s->fd);
    return 0;
}

static int tcp_get_file_handle(URLContext *h)
{
    TCPContext *s = h->priv_data;
    return s->fd;
}

URLProtocol ff_tcp_protocol = {
    .name                = "tcp",
    .url_open            = tcp_open,
    .url_read            = tcp_read,
    .url_write           = tcp_write,
    .url_close           = tcp_close,
    .url_get_file_handle = tcp_get_file_handle,
    .url_shutdown        = tcp_shutdown,
    .priv_data_size      = sizeof(TCPContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
