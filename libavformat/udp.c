/*
 * UDP prototype streaming system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
#ifndef __BEOS__
# include <arpa/inet.h>
#else
# include "barpainet.h"
#endif
#include <netdb.h>

typedef struct {
    int udp_fd;
    int ttl;
    int is_multicast;
    int local_port;
    struct ip_mreq mreq;
    struct sockaddr_in dest_addr;
} UDPContext;

#define UDP_TX_BUF_SIZE 32768

/**
 * If no filename is given to av_open_input_file because you want to
 * get the local port first, then you must call this function to set
 * the remote server address.
 *
 * url syntax: udp://host:port[?option=val...]
 * option: 'multicast=1' : enable multicast 
 *         'ttl=n'       : set the ttl value (for multicast only)
 *         'localport=n' : set the local port
 *         'pkt_size=n'  : set max packet size
 *
 * @param s1 media file context
 * @param uri of the remote server
 * @return zero if no error.
 */
int udp_set_remote_url(URLContext *h, const char *uri)
{
    UDPContext *s = h->priv_data;
    char hostname[256];
    int port;
    
    url_split(NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* set the destination address */
    if (resolve_host(&s->dest_addr.sin_addr, hostname) < 0)
        return AVERROR_IO;
    s->dest_addr.sin_family = AF_INET;
    s->dest_addr.sin_port = htons(port);
    return 0;
}

/**
 * Return the local port used by the UDP connexion
 * @param s1 media file context
 * @return the local port number
 */
int udp_get_local_port(URLContext *h)
{
    UDPContext *s = h->priv_data;
    return s->local_port;
}

/**
 * Return the udp file handle for select() usage to wait for several RTP
 * streams at the same time.
 * @param h media file context
 */
int udp_get_file_handle(URLContext *h)
{
    UDPContext *s = h->priv_data;
    return s->udp_fd;
}

/* put it in UDP context */
/* return non zero if error */
static int udp_open(URLContext *h, const char *uri, int flags)
{
    struct sockaddr_in my_addr, my_addr1;
    char hostname[1024];
    int port, udp_fd = -1, tmp;
    UDPContext *s = NULL;
    int is_output, len;
    const char *p;
    char buf[256];

    h->is_streamed = 1;
    h->max_packet_size = 1472;

    is_output = (flags & URL_WRONLY);
    
    s = av_malloc(sizeof(UDPContext));
    if (!s)
        return -ENOMEM;

    h->priv_data = s;
    s->ttl = 16;
    s->is_multicast = 0;
    s->local_port = 0;
    p = strchr(uri, '?');
    if (p) {
        s->is_multicast = find_info_tag(buf, sizeof(buf), "multicast", p);
        if (find_info_tag(buf, sizeof(buf), "ttl", p)) {
            s->ttl = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localport", p)) {
            s->local_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            h->max_packet_size = strtol(buf, NULL, 10);
        }
    }

    /* fill the dest addr */
    url_split(NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);
    
    /* XXX: fix url_split */
    if (hostname[0] == '\0' || hostname[0] == '?') {
        /* only accepts null hostname if input */
        if (s->is_multicast || (flags & URL_WRONLY))
            goto fail;
    } else {
        udp_set_remote_url(h, uri);
    }

    udp_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
        goto fail;

    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    if (s->is_multicast && !(h->flags & URL_WRONLY)) {
        /* special case: the bind must be done on the multicast address port */
        my_addr.sin_port = s->dest_addr.sin_port;
    } else {
        my_addr.sin_port = htons(s->local_port);
    }

    /* the bind is needed to give a port to the socket now */
    if (bind(udp_fd,(struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) 
        goto fail;

    len = sizeof(my_addr1);
    getsockname(udp_fd, (struct sockaddr *)&my_addr1, &len);
    s->local_port = ntohs(my_addr1.sin_port);

#ifndef CONFIG_BEOS_NETSERVER
    if (s->is_multicast) {
        if (h->flags & URL_WRONLY) {
            /* output */
            if (setsockopt(udp_fd, IPPROTO_IP, IP_MULTICAST_TTL, 
                           &s->ttl, sizeof(s->ttl)) < 0) {
                perror("IP_MULTICAST_TTL");
                goto fail;
            }
        } else {
            /* input */
            memset(&s->mreq, 0, sizeof(s->mreq));
            s->mreq.imr_multiaddr = s->dest_addr.sin_addr;
            s->mreq.imr_interface.s_addr = htonl (INADDR_ANY);
            if (setsockopt(udp_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                           &s->mreq, sizeof(s->mreq)) < 0) {
                perror("rtp: IP_ADD_MEMBERSHIP");
                goto fail;
            }
        }
    }
#endif

    if (is_output) {
        /* limit the tx buf size to limit latency */
        tmp = UDP_TX_BUF_SIZE;
        if (setsockopt(udp_fd, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) < 0) {
            perror("setsockopt sndbuf");
            goto fail;
        }
    }

    s->udp_fd = udp_fd;
    return 0;
 fail:
    if (udp_fd >= 0)
#ifdef CONFIG_BEOS_NETSERVER
        closesocket(udp_fd);
#else
        close(udp_fd);
#endif
    av_free(s);
    return AVERROR_IO;
}

static int udp_read(URLContext *h, uint8_t *buf, int size)
{
    UDPContext *s = h->priv_data;
    struct sockaddr_in from;
    int from_len, len;

    for(;;) {
        from_len = sizeof(from);
        len = recvfrom (s->udp_fd, buf, size, 0,
                        (struct sockaddr *)&from, &from_len);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR)
                return AVERROR_IO;
        } else {
            break;
        }
    }
    return len;
}

static int udp_write(URLContext *h, uint8_t *buf, int size)
{
    UDPContext *s = h->priv_data;
    int ret;

    for(;;) {
        ret = sendto (s->udp_fd, buf, size, 0, 
                      (struct sockaddr *) &s->dest_addr,
                      sizeof (s->dest_addr));
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return AVERROR_IO;
        } else {
            break;
        }
    }
    return size;
}

static int udp_close(URLContext *h)
{
    UDPContext *s = h->priv_data;

#ifndef CONFIG_BEOS_NETSERVER
    if (s->is_multicast && !(h->flags & URL_WRONLY)) {
        if (setsockopt(s->udp_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
                       &s->mreq, sizeof(s->mreq)) < 0) {
            perror("IP_DROP_MEMBERSHIP");
        }
    }
    close(s->udp_fd);
#else
    closesocket(s->udp_fd);
#endif
    av_free(s);
    return 0;
}

URLProtocol udp_protocol = {
    "udp",
    udp_open,
    udp_read,
    udp_write,
    NULL, /* seek */
    udp_close,
};
