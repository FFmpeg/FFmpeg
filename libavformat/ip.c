/*
 * IP common code
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>
#include "ip.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"

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

int ff_ip_check_source_lists(struct sockaddr_storage *source_addr_ptr, IPSourceFilters *s)
{
    int i;
    if (s->nb_exclude_addrs) {
        for (i = 0; i < s->nb_exclude_addrs; i++) {
            if (!compare_addr(source_addr_ptr, &s->exclude_addrs[i]))
                return 1;
        }
    }
    if (s->nb_include_addrs) {
        for (i = 0; i < s->nb_include_addrs; i++) {
            if (!compare_addr(source_addr_ptr, &s->include_addrs[i]))
                return 0;
        }
        return 1;
    }
    return 0;
}

struct addrinfo *ff_ip_resolve_host(void *log_ctx,
                                    const char *hostname, int port,
                                    int type, int family, int flags)
{
    struct addrinfo hints = { 0 }, *res = 0;
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
    hints.ai_socktype = type;
    hints.ai_family   = family;
    hints.ai_flags    = flags;
    if ((error = getaddrinfo(node, service, &hints, &res))) {
        res = NULL;
        av_log(log_ctx, AV_LOG_ERROR, "getaddrinfo(%s, %s): %s\n",
               node ? node : "unknown",
               service,
               gai_strerror(error));
    }

    return res;
}


static int ip_parse_addr_list(void *log_ctx, const char *buf,
                              struct sockaddr_storage **address_list_ptr,
                              int *address_list_size_ptr)
{
    struct addrinfo *ai = NULL;

    /* Resolve all of the IPs */

    while (buf && buf[0]) {
        char* host = av_get_token(&buf, ",");
        if (!host)
            return AVERROR(ENOMEM);

        ai = ff_ip_resolve_host(log_ctx, host, 0, SOCK_DGRAM, AF_UNSPEC, 0);
        av_freep(&host);

        if (ai) {
            struct sockaddr_storage source_addr = {0};
            memcpy(&source_addr, ai->ai_addr, ai->ai_addrlen);
            freeaddrinfo(ai);
            av_dynarray2_add((void **)address_list_ptr, address_list_size_ptr, sizeof(source_addr), (uint8_t *)&source_addr);
            if (!*address_list_ptr)
                return AVERROR(ENOMEM);
        } else {
            return AVERROR(EINVAL);
        }

        if (*buf)
            buf++;
    }

    return 0;
}

static int ip_parse_sources_and_blocks(void *log_ctx, const char *buf, IPSourceFilters *filters, int parse_include_list)
{
    int ret;
    if (parse_include_list)
        ret = ip_parse_addr_list(log_ctx, buf, &filters->include_addrs, &filters->nb_include_addrs);
    else
        ret = ip_parse_addr_list(log_ctx, buf, &filters->exclude_addrs, &filters->nb_exclude_addrs);

    if (ret >= 0 && filters->nb_include_addrs && filters->nb_exclude_addrs) {
        av_log(log_ctx, AV_LOG_ERROR, "Simultaneously including and excluding sources is not supported.\n");
        return AVERROR(EINVAL);
    }
    return ret;
}

int ff_ip_parse_sources(void *log_ctx, const char *buf, IPSourceFilters *filters)
{
    return ip_parse_sources_and_blocks(log_ctx, buf, filters, 1);
}

int ff_ip_parse_blocks(void *log_ctx, const char *buf, IPSourceFilters *filters)
{
    return ip_parse_sources_and_blocks(log_ctx, buf, filters, 0);
}

void ff_ip_reset_filters(IPSourceFilters *filters)
{
    av_freep(&filters->exclude_addrs);
    av_freep(&filters->include_addrs);
    filters->nb_include_addrs = 0;
    filters->nb_exclude_addrs = 0;
}
