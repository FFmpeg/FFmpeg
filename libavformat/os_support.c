/*
 * various OS-feature replacement utilities
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * copyright (c) 2002 Francois Revol
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

/* needed by inet_aton() */
#define _DEFAULT_SOURCE
#define _SVID_SOURCE

#include "config.h"
#include "avformat.h"
#include "os_support.h"

#if CONFIG_NETWORK
#include <fcntl.h>
#if !HAVE_POLL_H
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#endif /* !HAVE_POLL_H */

#include "network.h"

#if !HAVE_GETADDRINFO
#if !HAVE_INET_ATON
#include <stdlib.h>

static int inet_aton(const char *str, struct in_addr *add)
{
    unsigned int add1 = 0, add2 = 0, add3 = 0, add4 = 0;

    if (sscanf(str, "%d.%d.%d.%d", &add1, &add2, &add3, &add4) != 4)
        return 0;

    if (!add1 || (add1 | add2 | add3 | add4) > 255)
        return 0;

    add->s_addr = htonl((add1 << 24) + (add2 << 16) + (add3 << 8) + add4);

    return 1;
}
#endif /* !HAVE_INET_ATON */

int ff_getaddrinfo(const char *node, const char *service,
                   const struct addrinfo *hints, struct addrinfo **res)
{
    struct hostent *h = NULL;
    struct addrinfo *ai;
    struct sockaddr_in *sin;

    *res = NULL;
    sin  = av_mallocz(sizeof(struct sockaddr_in));
    if (!sin)
        return EAI_FAIL;
    sin->sin_family = AF_INET;

    if (node) {
        if (!inet_aton(node, &sin->sin_addr)) {
            if (hints && (hints->ai_flags & AI_NUMERICHOST)) {
                av_free(sin);
                return EAI_FAIL;
            }
            h = gethostbyname(node);
            if (!h) {
                av_free(sin);
                return EAI_FAIL;
            }
            memcpy(&sin->sin_addr, h->h_addr_list[0], sizeof(struct in_addr));
        }
    } else {
        if (hints && (hints->ai_flags & AI_PASSIVE))
            sin->sin_addr.s_addr = INADDR_ANY;
        else
            sin->sin_addr.s_addr = INADDR_LOOPBACK;
    }

    /* Note: getaddrinfo allows service to be a string, which
     * should be looked up using getservbyname. */
    if (service)
        sin->sin_port = htons(atoi(service));

    ai = av_mallocz(sizeof(struct addrinfo));
    if (!ai) {
        av_free(sin);
        return EAI_FAIL;
    }

    *res            = ai;
    ai->ai_family   = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    switch (ai->ai_socktype) {
    case SOCK_STREAM:
        ai->ai_protocol = IPPROTO_TCP;
        break;
    case SOCK_DGRAM:
        ai->ai_protocol = IPPROTO_UDP;
        break;
    default:
        ai->ai_protocol = 0;
        break;
    }

    ai->ai_addr    = (struct sockaddr *)sin;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    if (hints && (hints->ai_flags & AI_CANONNAME))
        ai->ai_canonname = h ? av_strdup(h->h_name) : NULL;

    ai->ai_next = NULL;
    return 0;
}

void ff_freeaddrinfo(struct addrinfo *res)
{
    av_freep(&res->ai_canonname);
    av_freep(&res->ai_addr);
    av_freep(&res);
}

int ff_getnameinfo(const struct sockaddr *sa, int salen,
                   char *host, int hostlen,
                   char *serv, int servlen, int flags)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;

    if (sa->sa_family != AF_INET)
        return EAI_FAMILY;
    if (!host && !serv)
        return EAI_NONAME;

    if (host && hostlen > 0) {
        struct hostent *ent = NULL;
        uint32_t a;
        if (!(flags & NI_NUMERICHOST))
            ent = gethostbyaddr((const char *)&sin->sin_addr,
                                sizeof(sin->sin_addr), AF_INET);

        if (ent) {
            snprintf(host, hostlen, "%s", ent->h_name);
        } else if (flags & NI_NAMERQD) {
            return EAI_NONAME;
        } else {
            a = ntohl(sin->sin_addr.s_addr);
            snprintf(host, hostlen, "%d.%d.%d.%d",
                     ((a >> 24) & 0xff), ((a >> 16) & 0xff),
                     ((a >>  8) & 0xff),  (a        & 0xff));
        }
    }

    if (serv && servlen > 0) {
        if (!(flags & NI_NUMERICSERV))
            return EAI_FAIL;
        snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
    }

    return 0;
}
#endif /* !HAVE_GETADDRINFO */

#if !HAVE_GETADDRINFO || HAVE_WINSOCK2_H
const char *ff_gai_strerror(int ecode)
{
    switch (ecode) {
    case EAI_AGAIN:
        return "Temporary failure in name resolution";
    case EAI_BADFLAGS:
        return "Invalid flags for ai_flags";
    case EAI_FAIL:
        return "A non-recoverable error occurred";
    case EAI_FAMILY:
        return "The address family was not recognized or the address "
               "length was invalid for the specified family";
    case EAI_MEMORY:
        return "Memory allocation failure";
#if EAI_NODATA != EAI_NONAME
    case EAI_NODATA:
        return "No address associated with hostname";
#endif /* EAI_NODATA != EAI_NONAME */
    case EAI_NONAME:
        return "The name does not resolve for the supplied parameters";
    case EAI_SERVICE:
        return "servname not supported for ai_socktype";
    case EAI_SOCKTYPE:
        return "ai_socktype not supported";
    }

    return "Unknown error";
}
#endif /* !HAVE_GETADDRINFO || HAVE_WINSOCK2_H */

int ff_socket_nonblock(int socket, int enable)
{
#if HAVE_WINSOCK2_H
    u_long param = enable;
    return ioctlsocket(socket, FIONBIO, &param);
#else
    if (enable)
        return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);
    else
        return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) & ~O_NONBLOCK);
#endif /* HAVE_WINSOCK2_H */
}

#if !HAVE_POLL_H
int ff_poll(struct pollfd *fds, nfds_t numfds, int timeout)
{
    fd_set read_set;
    fd_set write_set;
    fd_set exception_set;
    nfds_t i;
    int n;
    int rc;

#if HAVE_WINSOCK2_H
    if (numfds >= FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
#endif /* HAVE_WINSOCK2_H */

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&exception_set);

    n = 0;
    for (i = 0; i < numfds; i++) {
        if (fds[i].fd < 0)
            continue;
#if !HAVE_WINSOCK2_H
        if (fds[i].fd >= FD_SETSIZE) {
            errno = EINVAL;
            return -1;
        }
#endif /* !HAVE_WINSOCK2_H */

        if (fds[i].events & POLLIN)
            FD_SET(fds[i].fd, &read_set);
        if (fds[i].events & POLLOUT)
            FD_SET(fds[i].fd, &write_set);
        if (fds[i].events & POLLERR)
            FD_SET(fds[i].fd, &exception_set);

        if (fds[i].fd >= n)
            n = fds[i].fd + 1;
    }

    if (n == 0)
        /* Hey!? Nothing to poll, in fact!!! */
        return 0;

    if (timeout < 0) {
        rc = select(n, &read_set, &write_set, &exception_set, NULL);
    } else {
        struct timeval tv;
        tv.tv_sec  = timeout / 1000;
        tv.tv_usec = 1000 * (timeout % 1000);
        rc         = select(n, &read_set, &write_set, &exception_set, &tv);
    }

    if (rc < 0)
        return rc;

    for (i = 0; i < numfds; i++) {
        fds[i].revents = 0;

        if (FD_ISSET(fds[i].fd, &read_set))
            fds[i].revents |= POLLIN;
        if (FD_ISSET(fds[i].fd, &write_set))
            fds[i].revents |= POLLOUT;
        if (FD_ISSET(fds[i].fd, &exception_set))
            fds[i].revents |= POLLERR;
    }

    return rc;
}
#endif /* !HAVE_POLL_H */

#endif /* CONFIG_NETWORK */
