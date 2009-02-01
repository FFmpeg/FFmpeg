/*
 * UDP prototype streaming system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
 * @file libavformat/udp.c
 * UDP protocol
 */

#define _BSD_SOURCE     /* Needed for using struct ip_mreq with recent glibc */
#include "avformat.h"
#include <unistd.h>
#include "network.h"
#include "os_support.h"
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <sys/time.h>

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif
#ifndef IN_MULTICAST
#define IN_MULTICAST(a) ((((uint32_t)(a)) & 0xf0000000) == 0xe0000000)
#endif
#ifndef IN6_IS_ADDR_MULTICAST
#define IN6_IS_ADDR_MULTICAST(a) (((uint8_t *) (a))[0] == 0xff)
#endif

typedef struct {
    int udp_fd;
    int ttl;
    int buffer_size;
    int is_multicast;
    int local_port;
    int reuse_socket;
#if !CONFIG_IPV6
    struct sockaddr_in dest_addr;
#else
    struct sockaddr_storage dest_addr;
#endif
    int dest_addr_len;
} UDPContext;

#define UDP_TX_BUF_SIZE 32768
#define UDP_MAX_PKT_SIZE 65536

static int udp_set_multicast_ttl(int sockfd, int mcastTTL, struct sockaddr *addr) {
#ifdef IP_MULTICAST_TTL
    if (addr->sa_family == AF_INET) {
        if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &mcastTTL, sizeof(mcastTTL)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IP_MULTICAST_TTL): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
#if CONFIG_IPV6
    if (addr->sa_family == AF_INET6) {
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &mcastTTL, sizeof(mcastTTL)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IPV6_MULTICAST_HOPS): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
    return 0;
}

static int udp_join_multicast_group(int sockfd, struct sockaddr *addr) {
#ifdef IP_ADD_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IP_ADD_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
#if CONFIG_IPV6
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        mreq6.ipv6mr_interface= 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IPV6_ADD_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
    return 0;
}

static int udp_leave_multicast_group(int sockfd, struct sockaddr *addr) {
#ifdef IP_DROP_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IP_DROP_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
#if CONFIG_IPV6
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        mreq6.ipv6mr_interface= 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(IPV6_DROP_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
#endif
    return 0;
}

#if CONFIG_IPV6
static struct addrinfo* udp_ipv6_resolve_host(const char *hostname, int port, int type, int family, int flags) {
    struct addrinfo hints, *res = 0;
    int error;
    char sport[16];
    const char *node = 0, *service = "0";

    if (port > 0) {
        snprintf(sport, sizeof(sport), "%d", port);
        service = sport;
    }
    if ((hostname) && (hostname[0] != '\0') && (hostname[0] != '?')) {
        node = hostname;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = type;
    hints.ai_family   = family;
    hints.ai_flags = flags;
    if ((error = getaddrinfo(node, service, &hints, &res))) {
        av_log(NULL, AV_LOG_ERROR, "udp_ipv6_resolve_host: %s\n", gai_strerror(error));
    }

    return res;
}

static int udp_set_url(struct sockaddr_storage *addr, const char *hostname, int port) {
    struct addrinfo *res0;
    int addr_len;

    res0 = udp_ipv6_resolve_host(hostname, port, SOCK_DGRAM, AF_UNSPEC, 0);
    if (res0 == 0) return AVERROR(EIO);
    memcpy(addr, res0->ai_addr, res0->ai_addrlen);
    addr_len = res0->ai_addrlen;
    freeaddrinfo(res0);

    return addr_len;
}

static int is_multicast_address(struct sockaddr_storage *addr)
{
    if (addr->ss_family == AF_INET) {
        return IN_MULTICAST(ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr));
    }
    if (addr->ss_family == AF_INET6) {
        return IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6 *)addr)->sin6_addr);
    }

    return 0;
}

static int udp_socket_create(UDPContext *s, struct sockaddr_storage *addr, int *addr_len)
{
    int udp_fd = -1;
    struct addrinfo *res0 = NULL, *res = NULL;
    int family = AF_UNSPEC;

    if (((struct sockaddr *) &s->dest_addr)->sa_family)
        family = ((struct sockaddr *) &s->dest_addr)->sa_family;
    res0 = udp_ipv6_resolve_host(0, s->local_port, SOCK_DGRAM, family, AI_PASSIVE);
    if (res0 == 0)
        goto fail;
    for (res = res0; res; res=res->ai_next) {
        udp_fd = socket(res->ai_family, SOCK_DGRAM, 0);
        if (udp_fd > 0) break;
        av_log(NULL, AV_LOG_ERROR, "socket: %s\n", strerror(errno));
    }

    if (udp_fd < 0)
        goto fail;

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res0);

    return udp_fd;

 fail:
    if (udp_fd >= 0)
        closesocket(udp_fd);
    if(res0)
        freeaddrinfo(res0);
    return -1;
}

static int udp_port(struct sockaddr_storage *addr, int addr_len)
{
    char sbuf[sizeof(int)*3+1];

    if (getnameinfo((struct sockaddr *)addr, addr_len, NULL, 0,  sbuf, sizeof(sbuf), NI_NUMERICSERV) != 0) {
        av_log(NULL, AV_LOG_ERROR, "getnameinfo: %s\n", strerror(errno));
        return -1;
    }

    return strtol(sbuf, NULL, 10);
}

#else

static int udp_set_url(struct sockaddr_in *addr, const char *hostname, int port)
{
    /* set the destination address */
    if (resolve_host(&addr->sin_addr, hostname) < 0)
        return AVERROR(EIO);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    return sizeof(struct sockaddr_in);
}

static int is_multicast_address(struct sockaddr_in *addr)
{
    return IN_MULTICAST(ntohl(addr->sin_addr.s_addr));
}

static int udp_socket_create(UDPContext *s, struct sockaddr_in *addr, int *addr_len)
{
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = htonl (INADDR_ANY);
    addr->sin_port = htons(s->local_port);
    *addr_len = sizeof(struct sockaddr_in);

    return fd;
}

static int udp_port(struct sockaddr_in *addr, int len)
{
    return ntohs(addr->sin_port);
}
#endif /* CONFIG_IPV6 */


/**
 * If no filename is given to av_open_input_file because you want to
 * get the local port first, then you must call this function to set
 * the remote server address.
 *
 * url syntax: udp://host:port[?option=val...]
 * option: 'ttl=n'       : set the ttl value (for multicast only)
 *         'localport=n' : set the local port
 *         'pkt_size=n'  : set max packet size
 *         'reuse=1'     : enable reusing the socket
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

    url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* set the destination address */
    s->dest_addr_len = udp_set_url(&s->dest_addr, hostname, port);
    if (s->dest_addr_len < 0) {
        return AVERROR(EIO);
    }
    s->is_multicast = is_multicast_address(&s->dest_addr);

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
    char hostname[1024];
    int port, udp_fd = -1, tmp, bind_ret = -1;
    UDPContext *s = NULL;
    int is_output;
    const char *p;
    char buf[256];
#if !CONFIG_IPV6
    struct sockaddr_in my_addr;
#else
    struct sockaddr_storage my_addr;
#endif
    int len;

    h->is_streamed = 1;
    h->max_packet_size = 1472;

    is_output = (flags & URL_WRONLY);

    if(!ff_network_init())
        return AVERROR(EIO);

    s = av_mallocz(sizeof(UDPContext));
    if (!s)
        return AVERROR(ENOMEM);

    h->priv_data = s;
    s->ttl = 16;
    s->buffer_size = is_output ? UDP_TX_BUF_SIZE : UDP_MAX_PKT_SIZE;

    p = strchr(uri, '?');
    if (p) {
        s->reuse_socket = find_info_tag(buf, sizeof(buf), "reuse", p);
        if (find_info_tag(buf, sizeof(buf), "ttl", p)) {
            s->ttl = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "localport", p)) {
            s->local_port = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            h->max_packet_size = strtol(buf, NULL, 10);
        }
        if (find_info_tag(buf, sizeof(buf), "buffer_size", p)) {
            s->buffer_size = strtol(buf, NULL, 10);
        }
    }

    /* fill the dest addr */
    url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* XXX: fix url_split */
    if (hostname[0] == '\0' || hostname[0] == '?') {
        /* only accepts null hostname if input */
        if (flags & URL_WRONLY)
            goto fail;
    } else {
        udp_set_remote_url(h, uri);
    }

    if (s->is_multicast && !(h->flags & URL_WRONLY))
        s->local_port = port;
    udp_fd = udp_socket_create(s, &my_addr, &len);
    if (udp_fd < 0)
        goto fail;

    if (s->reuse_socket)
        if (setsockopt (udp_fd, SOL_SOCKET, SO_REUSEADDR, &(s->reuse_socket), sizeof(s->reuse_socket)) != 0)
            goto fail;

    /* the bind is needed to give a port to the socket now */
    /* if multicast, try the multicast address bind first */
    if (s->is_multicast && !(h->flags & URL_WRONLY)) {
        bind_ret = bind(udp_fd,(struct sockaddr *)&s->dest_addr, len);
    }
    /* bind to the local address if not multicast or if the multicast
     * bind failed */
    if (bind_ret < 0 && bind(udp_fd,(struct sockaddr *)&my_addr, len) < 0)
        goto fail;

    len = sizeof(my_addr);
    getsockname(udp_fd, (struct sockaddr *)&my_addr, &len);
    s->local_port = udp_port(&my_addr, len);

    if (s->is_multicast) {
        if (h->flags & URL_WRONLY) {
            /* output */
            if (udp_set_multicast_ttl(udp_fd, s->ttl, (struct sockaddr *)&s->dest_addr) < 0)
                goto fail;
        } else {
            /* input */
            if (udp_join_multicast_group(udp_fd, (struct sockaddr *)&s->dest_addr) < 0)
                goto fail;
        }
    }

    if (is_output) {
        /* limit the tx buf size to limit latency */
        tmp = s->buffer_size;
        if (setsockopt(udp_fd, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "setsockopt(SO_SNDBUF): %s\n", strerror(errno));
            goto fail;
        }
    } else {
        /* set udp recv buffer size to the largest possible udp packet size to
         * avoid losing data on OSes that set this too low by default. */
        tmp = s->buffer_size;
        if (setsockopt(udp_fd, SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) < 0) {
            av_log(NULL, AV_LOG_WARNING, "setsockopt(SO_RECVBUF): %s\n", strerror(errno));
        }
        /* make the socket non-blocking */
        ff_socket_nonblock(udp_fd, 1);
    }

    s->udp_fd = udp_fd;
    return 0;
 fail:
    if (udp_fd >= 0)
        closesocket(udp_fd);
    av_free(s);
    return AVERROR(EIO);
}

static int udp_read(URLContext *h, uint8_t *buf, int size)
{
    UDPContext *s = h->priv_data;
    int len;
    fd_set rfds;
    int ret;
    struct timeval tv;

    for(;;) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        FD_ZERO(&rfds);
        FD_SET(s->udp_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        ret = select(s->udp_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0)
            return AVERROR(EIO);
        if (!(ret > 0 && FD_ISSET(s->udp_fd, &rfds)))
            continue;
        len = recv(s->udp_fd, buf, size, 0);
        if (len < 0) {
            if (ff_neterrno() != FF_NETERROR(EAGAIN) &&
                ff_neterrno() != FF_NETERROR(EINTR))
                return AVERROR(EIO);
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
                      s->dest_addr_len);
        if (ret < 0) {
            if (ff_neterrno() != FF_NETERROR(EINTR) &&
                ff_neterrno() != FF_NETERROR(EAGAIN))
                return AVERROR(EIO);
        } else {
            break;
        }
    }
    return size;
}

static int udp_close(URLContext *h)
{
    UDPContext *s = h->priv_data;

    if (s->is_multicast && !(h->flags & URL_WRONLY))
        udp_leave_multicast_group(s->udp_fd, (struct sockaddr *)&s->dest_addr);
    closesocket(s->udp_fd);
    ff_network_close();
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
