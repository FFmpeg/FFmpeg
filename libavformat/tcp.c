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
    struct sockaddr_in dest_addr;
    int port, fd = -1;
    TCPContext *s = NULL;
    fd_set wfds;
    int fd_max, ret;
    struct timeval tv;
    socklen_t optlen;
    char hostname[1024],proto[1024],path[1024];

    if(!ff_network_init())
        return AVERROR(EIO);

    url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto,"tcp") || port <= 0 || port >= 65536)
        return AVERROR(EINVAL);

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (resolve_host(&dest_addr.sin_addr, hostname) < 0)
        return AVERROR(EIO);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return AVERROR(EIO);
    ff_socket_nonblock(fd, 1);

 redo:
    ret = connect(fd, (struct sockaddr *)&dest_addr,
                  sizeof(dest_addr));
    if (ret < 0) {
        if (ff_neterrno() == FF_NETERROR(EINTR))
            goto redo;
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
            FD_SET(fd, &wfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            ret = select(fd_max + 1, NULL, &wfds, NULL, &tv);
            if (ret > 0 && FD_ISSET(fd, &wfds))
                break;
        }

        /* test error */
        optlen = sizeof(ret);
        getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &optlen);
        if (ret != 0)
            goto fail;
    }
    s = av_malloc(sizeof(TCPContext));
    if (!s)
        return AVERROR(ENOMEM);
    h->priv_data = s;
    h->is_streamed = 1;
    s->fd = fd;
    return 0;

 fail:
    ret = AVERROR(EIO);
 fail1:
    if (fd >= 0)
        closesocket(fd);
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
                    return AVERROR(errno);
            } else return len;
        } else if (ret < 0) {
            return -1;
        }
    }
}

static int tcp_write(URLContext *h, uint8_t *buf, int size)
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
                    return AVERROR(errno);
                continue;
            }
            size -= len;
            buf += len;
        } else if (ret < 0) {
            return -1;
        }
    }
    return size1 - size;
}

static int tcp_close(URLContext *h)
{
    TCPContext *s = h->priv_data;
    closesocket(s->fd);
    ff_network_close();
    av_free(s);
    return 0;
}

URLProtocol tcp_protocol = {
    "tcp",
    tcp_open,
    tcp_read,
    tcp_write,
    NULL, /* seek */
    tcp_close,
};
