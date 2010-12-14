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
#include <unistd.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <sys/time.h>

typedef struct TCPContext {
    int fd;
} TCPContext;

/* return non zero if error */
static int tcp_open(URLContext *h, const char *uri, int flags)
{
    struct addrinfo hints, *ai, *cur_ai;
    int port, fd = -1;
    TCPContext *s = NULL;
    fd_set wfds, efds;
    int fd_max, ret;
    struct timeval tv;
    socklen_t optlen;
    char hostname[1024],proto[1024],path[1024];
    char portstr[10];

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto,"tcp") || port <= 0 || port >= 65536)
        return AVERROR(EINVAL);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    ret = getaddrinfo(hostname, portstr, &hints, &ai);
    if (ret) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed to resolve hostname %s: %s\n",
               hostname, gai_strerror(ret));
        return AVERROR(EIO);
    }

    cur_ai = ai;

 restart:
    fd = socket(cur_ai->ai_family, cur_ai->ai_socktype, cur_ai->ai_protocol);
    if (fd < 0)
        goto fail;
    ff_socket_nonblock(fd, 1);

 redo:
    ret = connect(fd, cur_ai->ai_addr, cur_ai->ai_addrlen);
    if (ret < 0) {
        if (ff_neterrno() == FF_NETERROR(EINTR)) {
            if (url_interrupt_cb())
                goto fail1;
            goto redo;
        }
        if (ff_neterrno() != FF_NETERROR(EINPROGRESS) &&
            ff_neterrno() != FF_NETERROR(EAGAIN))
            goto fail;

        /* wait until we are connected or until abort */
        for(;;) {
            if (url_interrupt_cb()) {
                ret = AVERROR(EINTR);
                goto fail1;
            }
            fd_max = fd;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(fd, &wfds);
            FD_SET(fd, &efds);
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            ret = select(fd_max + 1, NULL, &wfds, &efds, &tv);
            if (ret > 0 && (FD_ISSET(fd, &wfds) || FD_ISSET(fd, &efds)))
                break;
        }

        /* test error */
        optlen = sizeof(ret);
        getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &optlen);
        if (ret != 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "TCP connection to %s:%d failed: %s\n",
                   hostname, port, strerror(ret));
            goto fail;
        }
    }
    s = av_malloc(sizeof(TCPContext));
    if (!s) {
        freeaddrinfo(ai);
        return AVERROR(ENOMEM);
    }
    h->priv_data = s;
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
    ret = AVERROR(EIO);
 fail1:
    if (fd >= 0)
        closesocket(fd);
    freeaddrinfo(ai);
    return ret;
}

static int tcp_read(URLContext *h, uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int len, fd_max, ret;
    fd_set rfds;
    struct timeval tv;

    for (;;) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        fd_max = s->fd;
        FD_ZERO(&rfds);
        FD_SET(s->fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        ret = select(fd_max + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(s->fd, &rfds)) {
            len = recv(s->fd, buf, size, 0);
            if (len < 0) {
                if (ff_neterrno() != FF_NETERROR(EINTR) &&
                    ff_neterrno() != FF_NETERROR(EAGAIN))
                    return ff_neterrno();
            } else return len;
        } else if (ret < 0) {
            if (ff_neterrno() == FF_NETERROR(EINTR))
                continue;
            return -1;
        }
    }
}

static int tcp_write(URLContext *h, const uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret, size1, fd_max, len;
    fd_set wfds;
    struct timeval tv;

    size1 = size;
    while (size > 0) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        fd_max = s->fd;
        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        ret = select(fd_max + 1, NULL, &wfds, NULL, &tv);
        if (ret > 0 && FD_ISSET(s->fd, &wfds)) {
            len = send(s->fd, buf, size, 0);
            if (len < 0) {
                if (ff_neterrno() != FF_NETERROR(EINTR) &&
                    ff_neterrno() != FF_NETERROR(EAGAIN))
                    return ff_neterrno();
                continue;
            }
            size -= len;
            buf += len;
        } else if (ret < 0) {
            if (ff_neterrno() == FF_NETERROR(EINTR))
                continue;
            return -1;
        }
    }
    return size1 - size;
}

static int tcp_close(URLContext *h)
{
    TCPContext *s = h->priv_data;
    closesocket(s->fd);
    av_free(s);
    return 0;
}

static int tcp_get_file_handle(URLContext *h)
{
    TCPContext *s = h->priv_data;
    return s->fd;
}

URLProtocol tcp_protocol = {
    "tcp",
    tcp_open,
    tcp_read,
    tcp_write,
    NULL, /* seek */
    tcp_close,
    .url_get_file_handle = tcp_get_file_handle,
};
