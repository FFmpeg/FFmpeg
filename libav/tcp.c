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
#include <sys/socket.h>
#include "avformat.h"
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifndef __BEOS__
# include <arpa/inet.h>
#else
# include "barpainet.h"
#endif
#include <netdb.h>

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

    if (connect(fd, (struct sockaddr *)&dest_addr, 
                sizeof(dest_addr)) < 0)
        goto fail;

    s->fd = fd;
    return 0;

 fail:
    if (fd >= 0)
        close(fd);
    av_free(s);
    return -EIO;
}

static int tcp_read(URLContext *h, UINT8 *buf, int size)
{
    TCPContext *s = h->priv_data;
    int size1, len;

    size1 = size;
    while (size > 0) {
#ifdef CONFIG_BEOS_NETSERVER
        len = recv (s->fd, buf, size, 0);
#else
        len = read (s->fd, buf, size);
#endif
        if (len < 0) {
            if (errno != EINTR && errno != EAGAIN)
#ifdef __BEOS__
                return errno;
#else
                return -errno;
#endif
            else
                continue;
        } else if (len == 0) {
            break;
        }
        size -= len;
        buf += len;
    }
    return size1 - size;
}

static int tcp_write(URLContext *h, UINT8 *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret, size1;

    size1 = size;
    while (size > 0) {
#ifdef CONFIG_BEOS_NETSERVER
        ret = send (s->fd, buf, size, 0);
#else
        ret = write (s->fd, buf, size);
#endif
        if (ret < 0 && errno != EINTR && errno != EAGAIN)
#ifdef __BEOS__
            return errno;
#else
            return -errno;
#endif
        size -= ret;
        buf += ret;
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
