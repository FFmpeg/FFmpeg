/*
 * TCP protocol
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#if defined(__APPLE__) || defined(__BEOS__)
typedef int socklen_t;
#endif
#ifndef __BEOS__
# include <arpa/inet.h>
#else
# include "barpainet.h"
#endif
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>

typedef struct TCPContext {
    int fd;
} TCPContext;

/* resolve host with also IP address parsing */
int resolve_host(struct in_addr *sin_addr, const char *hostname)
{
    struct hostent *hp;

    if ((inet_aton(hostname, sin_addr)) == 0) {
        hp = gethostbyname(hostname);
        if (!hp)
            return -1;
        memcpy (sin_addr, hp->h_addr, sizeof(struct in_addr));
    }
    return 0;
}

/* return non zero if error */
static int tcp_open(URLContext *h, const char *uri, int flags)
{
    struct sockaddr_in dest_addr;
    char hostname[1024], *q;
    int port, fd = -1;
    TCPContext *s;
    const char *p;
    fd_set wfds;
    int fd_max, ret;
    struct timeval tv;
    socklen_t optlen;
    
    s = av_malloc(sizeof(TCPContext));
    if (!s)
        return -ENOMEM;
    h->priv_data = s;
    p = uri;
    if (!strstart(p, "tcp://", &p))
        goto fail;
    q = hostname;
    while (*p != ':' && *p != '/' && *p != '\0') {
        if ((q - hostname) < sizeof(hostname) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (*p != ':')
        goto fail;
    p++;
    port = strtoul(p, (char **)&p, 10);
    if (port <= 0 || port >= 65536)
        goto fail;
    
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (resolve_host(&dest_addr.sin_addr, hostname) < 0)
        goto fail;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto fail;
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
 redo:
    ret = connect(fd, (struct sockaddr *)&dest_addr, 
                  sizeof(dest_addr));
    if (ret < 0) {
        if (errno == EINTR)
            goto redo;
        if (errno != EINPROGRESS)
            goto fail;

        /* wait until we are connected or until abort */
        for(;;) {
            if (url_interrupt_cb()) {
                ret = -EINTR;
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
    s->fd = fd;
    return 0;

 fail:
    ret = AVERROR_IO;
 fail1:
    if (fd >= 0)
        close(fd);
    av_free(s);
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
            return -EINTR;
        fd_max = s->fd;
        FD_ZERO(&rfds);
        FD_SET(s->fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        ret = select(fd_max + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(s->fd, &rfds)) {
#ifdef __BEOS__
            len = recv(s->fd, buf, size, 0);
#else
            len = read(s->fd, buf, size);
#endif
            if (len < 0) {
                if (errno != EINTR && errno != EAGAIN)
#ifdef __BEOS__
                    return errno;
#else
                    return -errno;
#endif
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
            return -EINTR;
        fd_max = s->fd;
        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        ret = select(fd_max + 1, NULL, &wfds, NULL, &tv);
        if (ret > 0 && FD_ISSET(s->fd, &wfds)) {
#ifdef __BEOS__
            len = send(s->fd, buf, size, 0);
#else
            len = write(s->fd, buf, size);
#endif
            if (len < 0) {
                if (errno != EINTR && errno != EAGAIN) {
#ifdef __BEOS__
                    return errno;
#else
                    return -errno;
#endif
                }
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
#ifdef CONFIG_BEOS_NETSERVER
    closesocket(s->fd);
#else
    close(s->fd);
#endif
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
