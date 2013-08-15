/*
 * RTP network protocol
 * Copyright (c) 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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
#include "rtp.h"
#include "rtpproto.h"
#include "url.h"

#include <stdarg.h>
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include <fcntl.h>
#if HAVE_POLL_H
#include <sys/poll.h>
#endif

typedef struct RTPContext {
    URLContext *rtp_hd, *rtcp_hd;
    int rtp_fd, rtcp_fd, nb_ssm_include_addrs, nb_ssm_exclude_addrs;
    struct sockaddr_storage **ssm_include_addrs, **ssm_exclude_addrs;
    int write_to_source;
    struct sockaddr_storage last_rtp_source, last_rtcp_source;
    socklen_t last_rtp_source_len, last_rtcp_source_len;
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
    int port, rtcp_port;
    const char *p;

    char buf[1024];
    char path[1024];

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
                 path, sizeof(path), uri);
    rtcp_port = port + 1;

    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "rtcpport", p)) {
            rtcp_port = strtol(buf, NULL, 10);
        }
    }

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, port, "%s", path);
    ff_udp_set_remote_url(s->rtp_hd, buf);

    ff_url_join(buf, sizeof(buf), "udp", NULL, hostname, rtcp_port, "%s", path);
    ff_udp_set_remote_url(s->rtcp_hd, buf);
    return 0;
}

static struct addrinfo* rtp_resolve_host(const char *hostname, int port,
                                         int type, int family, int flags)
{
    struct addrinfo hints = { 0 }, *res = 0;
    int error;
    char service[16];

    snprintf(service, sizeof(service), "%d", port);
    hints.ai_socktype = type;
    hints.ai_family   = family;
    hints.ai_flags    = flags;
    if ((error = getaddrinfo(hostname, service, &hints, &res))) {
        res = NULL;
        av_log(NULL, AV_LOG_ERROR, "rtp_resolve_host: %s\n", gai_strerror(error));
    }

    return res;
}

static int compare_addr(const struct sockaddr_storage *a,
                        const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)
        return 1;
    if (a->ss_family == AF_INET) {
        return (((const struct sockaddr_in *)a)->sin_addr.s_addr !=
                ((const struct sockaddr_in *)b)->sin_addr.s_addr);
    }

#if HAVE_STRUCT_SOCKADDR_IN6
    if (a->ss_family == AF_INET6) {
        const uint8_t *s6_addr_a = ((const struct sockaddr_in6 *)a)->sin6_addr.s6_addr;
        const uint8_t *s6_addr_b = ((const struct sockaddr_in6 *)b)->sin6_addr.s6_addr;
        return memcmp(s6_addr_a, s6_addr_b, 16);
    }
#endif
    return 1;
}

static int get_port(const struct sockaddr_storage *ss)
{
    if (ss->ss_family == AF_INET)
        return ntohs(((const struct sockaddr_in *)ss)->sin_port);
#if HAVE_STRUCT_SOCKADDR_IN6
    if (ss->ss_family == AF_INET6)
        return ntohs(((const struct sockaddr_in6 *)ss)->sin6_port);
#endif
    return 0;
}

static void set_port(struct sockaddr_storage *ss, int port)
{
    if (ss->ss_family == AF_INET)
        ((struct sockaddr_in *)ss)->sin_port = htons(port);
#if HAVE_STRUCT_SOCKADDR_IN6
    else if (ss->ss_family == AF_INET6)
        ((struct sockaddr_in6 *)ss)->sin6_port = htons(port);
#endif
}

static int rtp_check_source_lists(RTPContext *s, struct sockaddr_storage *source_addr_ptr)
{
    int i;
    if (s->nb_ssm_exclude_addrs) {
        for (i = 0; i < s->nb_ssm_exclude_addrs; i++) {
            if (!compare_addr(source_addr_ptr, s->ssm_exclude_addrs[i]))
                return 1;
        }
    }
    if (s->nb_ssm_include_addrs) {
        for (i = 0; i < s->nb_ssm_include_addrs; i++) {
            if (!compare_addr(source_addr_ptr, s->ssm_include_addrs[i]))
                return 0;
        }
        return 1;
    }
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
                          int max_packet_size, int connect,
                          const char *include_sources,
                          const char *exclude_sources)
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
    if (include_sources && include_sources[0])
        url_add_option(buf, buf_size, "sources=%s", include_sources);
    if (exclude_sources && exclude_sources[0])
        url_add_option(buf, buf_size, "block=%s", exclude_sources);
}

static void rtp_parse_addr_list(URLContext *h, char *buf,
                                struct sockaddr_storage ***address_list_ptr,
                                int *address_list_size_ptr)
{
    struct addrinfo *ai = NULL;
    struct sockaddr_storage *source_addr;
    char tmp = '\0', *p = buf, *next;

    /* Resolve all of the IPs */

    while (p && p[0]) {
        next = strchr(p, ',');

        if (next) {
            tmp = *next;
            *next = '\0';
        }

        ai = rtp_resolve_host(p, 0, SOCK_DGRAM, AF_UNSPEC, 0);
        if (ai) {
            source_addr = av_mallocz(sizeof(struct sockaddr_storage));
            if (!source_addr)
                break;

            memcpy(source_addr, ai->ai_addr, ai->ai_addrlen);
            freeaddrinfo(ai);
            dynarray_add(address_list_ptr, address_list_size_ptr, source_addr);
        } else {
            av_log(h, AV_LOG_WARNING, "Unable to resolve %s\n", p);
        }

        if (next) {
            *next = tmp;
            p = next + 1;
        } else {
            p = NULL;
        }
    }
}

/**
 * url syntax: rtp://host:port[?option=val...]
 * option: 'ttl=n'            : set the ttl value (for multicast only)
 *         'rtcpport=n'       : set the remote rtcp port to n
 *         'localrtpport=n'   : set the local rtp port to n
 *         'localrtcpport=n'  : set the local rtcp port to n
 *         'pkt_size=n'       : set max packet size
 *         'connect=0/1'      : do a connect() on the UDP socket
 *         'sources=ip[,ip]'  : list allowed source IP addresses
 *         'block=ip[,ip]'    : list disallowed source IP addresses
 *         'write_to_source=0/1' : send packets to the source address of the latest received packet
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
    RTPContext *s = h->priv_data;
    int rtp_port, rtcp_port,
        ttl, connect,
        local_rtp_port, local_rtcp_port, max_packet_size;
    char hostname[256], include_sources[1024] = "", exclude_sources[1024] = "";
    char buf[1024];
    char path[1024];
    const char *p;

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
        if (av_find_info_tag(buf, sizeof(buf), "write_to_source", p)) {
            s->write_to_source = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "sources", p)) {
            av_strlcpy(include_sources, buf, sizeof(include_sources));
            rtp_parse_addr_list(h, buf, &s->ssm_include_addrs, &s->nb_ssm_include_addrs);
        }
        if (av_find_info_tag(buf, sizeof(buf), "block", p)) {
            av_strlcpy(exclude_sources, buf, sizeof(exclude_sources));
            rtp_parse_addr_list(h, buf, &s->ssm_exclude_addrs, &s->nb_ssm_exclude_addrs);
        }
    }

    build_udp_url(buf, sizeof(buf),
                  hostname, rtp_port, local_rtp_port, ttl, max_packet_size,
                  connect, include_sources, exclude_sources);
    if (ffurl_open(&s->rtp_hd, buf, flags, &h->interrupt_callback, NULL) < 0)
        goto fail;
    if (local_rtp_port>=0 && local_rtcp_port<0)
        local_rtcp_port = ff_udp_get_local_port(s->rtp_hd) + 1;

    build_udp_url(buf, sizeof(buf),
                  hostname, rtcp_port, local_rtcp_port, ttl, max_packet_size,
                  connect, include_sources, exclude_sources);
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
    return AVERROR(EIO);
}

static int rtp_read(URLContext *h, uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    int len, n, i;
    struct pollfd p[2] = {{s->rtp_fd, POLLIN, 0}, {s->rtcp_fd, POLLIN, 0}};
    int poll_delay = h->flags & AVIO_FLAG_NONBLOCK ? 0 : 100;
    struct sockaddr_storage *addrs[2] = { &s->last_rtp_source, &s->last_rtcp_source };
    socklen_t *addr_lens[2] = { &s->last_rtp_source_len, &s->last_rtcp_source_len };

    for(;;) {
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
        n = poll(p, 2, poll_delay);
        if (n > 0) {
            /* first try RTCP, then RTP */
            for (i = 1; i >= 0; i--) {
                if (!(p[i].revents & POLLIN))
                    continue;
                *addr_lens[i] = sizeof(*addrs[i]);
                len = recvfrom(p[i].fd, buf, size, 0,
                                (struct sockaddr *)addrs[i], addr_lens[i]);
                if (len < 0) {
                    if (ff_neterrno() == AVERROR(EAGAIN) ||
                        ff_neterrno() == AVERROR(EINTR))
                        continue;
                    return AVERROR(EIO);
                }
                if (rtp_check_source_lists(s, addrs[i]))
                    continue;
                return len;
            }
        } else if (n < 0) {
            if (ff_neterrno() == AVERROR(EINTR))
                continue;
            return AVERROR(EIO);
        }
        if (h->flags & AVIO_FLAG_NONBLOCK)
            return AVERROR(EAGAIN);
    }
    return len;
}

static int rtp_write(URLContext *h, const uint8_t *buf, int size)
{
    RTPContext *s = h->priv_data;
    int ret;
    URLContext *hd;

    if (size < 2)
        return AVERROR(EINVAL);

    if (s->write_to_source) {
        int fd;
        struct sockaddr_storage *source, temp_source;
        socklen_t *source_len, temp_len;
        if (!s->last_rtp_source.ss_family && !s->last_rtcp_source.ss_family) {
            av_log(h, AV_LOG_ERROR,
                   "Unable to send packet to source, no packets received yet\n");
            // Intentionally not returning an error here
            return size;
        }

        if (RTP_PT_IS_RTCP(buf[1])) {
            fd = s->rtcp_fd;
            source     = &s->last_rtcp_source;
            source_len = &s->last_rtcp_source_len;
        } else {
            fd = s->rtp_fd;
            source     = &s->last_rtp_source;
            source_len = &s->last_rtp_source_len;
        }
        if (!source->ss_family) {
            source      = &temp_source;
            source_len  = &temp_len;
            if (RTP_PT_IS_RTCP(buf[1])) {
                temp_source = s->last_rtp_source;
                temp_len    = s->last_rtp_source_len;
                set_port(source, get_port(source) + 1);
                av_log(h, AV_LOG_INFO,
                       "Not received any RTCP packets yet, inferring peer port "
                       "from the RTP port\n");
            } else {
                temp_source = s->last_rtcp_source;
                temp_len    = s->last_rtcp_source_len;
                set_port(source, get_port(source) - 1);
                av_log(h, AV_LOG_INFO,
                       "Not received any RTP packets yet, inferring peer port "
                       "from the RTCP port\n");
            }
        }

        if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
            ret = ff_network_wait_fd(fd, 1);
            if (ret < 0)
                return ret;
        }
        ret = sendto(fd, buf, size, 0, (struct sockaddr *) source,
                     *source_len);

        return ret < 0 ? ff_neterrno() : ret;
    }

    if (RTP_PT_IS_RTCP(buf[1])) {
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
    int i;

    for (i = 0; i < s->nb_ssm_include_addrs; i++)
        av_free(s->ssm_include_addrs[i]);
    av_freep(&s->ssm_include_addrs);
    for (i = 0; i < s->nb_ssm_exclude_addrs; i++)
        av_free(s->ssm_exclude_addrs[i]);
    av_freep(&s->ssm_exclude_addrs);

    ffurl_close(s->rtp_hd);
    ffurl_close(s->rtcp_hd);
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

static int rtp_get_multi_file_handle(URLContext *h, int **handles,
                                     int *numhandles)
{
    RTPContext *s = h->priv_data;
    int *hs       = *handles = av_malloc(sizeof(**handles) * 2);
    if (!hs)
        return AVERROR(ENOMEM);
    hs[0] = s->rtp_fd;
    hs[1] = s->rtcp_fd;
    *numhandles = 2;
    return 0;
}

URLProtocol ff_rtp_protocol = {
    .name                      = "rtp",
    .url_open                  = rtp_open,
    .url_read                  = rtp_read,
    .url_write                 = rtp_write,
    .url_close                 = rtp_close,
    .url_get_file_handle       = rtp_get_file_handle,
    .url_get_multi_file_handle = rtp_get_multi_file_handle,
    .priv_data_size            = sizeof(RTPContext),
    .flags                     = URL_PROTOCOL_FLAG_NETWORK,
};
