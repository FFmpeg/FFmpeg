/*
 * Copyright (c) 2007 The FFmpeg Project
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

#include <fcntl.h>
#include "network.h"
#include "tls.h"
#include "url.h"
#include "libavcodec/internal.h"
#include "libavutil/avutil.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"

int ff_tls_init(void)
{
#if CONFIG_TLS_PROTOCOL
#if CONFIG_OPENSSL
    int ret;
    if ((ret = ff_openssl_init()) < 0)
        return ret;
#endif
#if CONFIG_GNUTLS
    ff_gnutls_init();
#endif
#endif
    return 0;
}

void ff_tls_deinit(void)
{
#if CONFIG_TLS_PROTOCOL
#if CONFIG_OPENSSL
    ff_openssl_deinit();
#endif
#if CONFIG_GNUTLS
    ff_gnutls_deinit();
#endif
#endif
}

int ff_network_init(void)
{
#if HAVE_WINSOCK2_H
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(1,1), &wsaData))
        return 0;
#endif
    return 1;
}

int ff_network_wait_fd(int fd, int write)
{
    int ev = write ? POLLOUT : POLLIN;
    struct pollfd p = { .fd = fd, .events = ev, .revents = 0 };
    int ret;
    ret = poll(&p, 1, POLLING_TIME);
    return ret < 0 ? ff_neterrno() : p.revents & (ev | POLLERR | POLLHUP) ? 0 : AVERROR(EAGAIN);
}

int ff_network_wait_fd_timeout(int fd, int write, int64_t timeout, AVIOInterruptCB *int_cb)
{
    int ret;
    int64_t wait_start = 0;

    while (1) {
        if (ff_check_interrupt(int_cb))
            return AVERROR_EXIT;
        ret = ff_network_wait_fd(fd, write);
        if (ret != AVERROR(EAGAIN))
            return ret;
        if (timeout > 0) {
            if (!wait_start)
                wait_start = av_gettime_relative();
            else if (av_gettime_relative() - wait_start > timeout)
                return AVERROR(ETIMEDOUT);
        }
    }
}

int ff_network_sleep_interruptible(int64_t timeout, AVIOInterruptCB *int_cb)
{
    int64_t wait_start = av_gettime_relative();

    while (1) {
        int64_t time_left;

        if (ff_check_interrupt(int_cb))
            return AVERROR_EXIT;

        time_left = timeout - (av_gettime_relative() - wait_start);
        if (time_left <= 0)
            return AVERROR(ETIMEDOUT);

        av_usleep(FFMIN(time_left, POLLING_TIME * 1000));
    }
}

void ff_network_close(void)
{
#if HAVE_WINSOCK2_H
    WSACleanup();
#endif
}

#if HAVE_WINSOCK2_H
int ff_neterrno(void)
{
    int err = WSAGetLastError();
    switch (err) {
    case WSAEWOULDBLOCK:
        return AVERROR(EAGAIN);
    case WSAEINTR:
        return AVERROR(EINTR);
    case WSAEPROTONOSUPPORT:
        return AVERROR(EPROTONOSUPPORT);
    case WSAETIMEDOUT:
        return AVERROR(ETIMEDOUT);
    case WSAECONNREFUSED:
        return AVERROR(ECONNREFUSED);
    case WSAEINPROGRESS:
        return AVERROR(EINPROGRESS);
    }
    return -err;
}
#endif

int ff_is_multicast_address(struct sockaddr *addr)
{
    if (addr->sa_family == AF_INET) {
        return IN_MULTICAST(ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr));
    }
#if HAVE_STRUCT_SOCKADDR_IN6
    if (addr->sa_family == AF_INET6) {
        return IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6 *)addr)->sin6_addr);
    }
#endif

    return 0;
}

static int ff_poll_interrupt(struct pollfd *p, nfds_t nfds, int timeout,
                             AVIOInterruptCB *cb)
{
    int runs = timeout / POLLING_TIME;
    int ret = 0;

    do {
        if (ff_check_interrupt(cb))
            return AVERROR_EXIT;
        ret = poll(p, nfds, POLLING_TIME);
        if (ret != 0) {
            if (ret < 0)
                ret = ff_neterrno();
            if (ret == AVERROR(EINTR))
                continue;
            break;
        }
    } while (timeout <= 0 || runs-- > 0);

    if (!ret)
        return AVERROR(ETIMEDOUT);
    return ret;
}

int ff_socket(int af, int type, int proto)
{
    int fd;

#ifdef SOCK_CLOEXEC
    fd = socket(af, type | SOCK_CLOEXEC, proto);
    if (fd == -1 && errno == EINVAL)
#endif
    {
        fd = socket(af, type, proto);
#if HAVE_FCNTL
        if (fd != -1) {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
                av_log(NULL, AV_LOG_DEBUG, "Failed to set close on exec\n");
        }
#endif
    }
#ifdef SO_NOSIGPIPE
    if (fd != -1) {
        if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &(int){1}, sizeof(int))) {
             av_log(NULL, AV_LOG_WARNING, "setsockopt(SO_NOSIGPIPE) failed\n");
        }
    }
#endif
    return fd;
}

int ff_listen(int fd, const struct sockaddr *addr,
              socklen_t addrlen)
{
    int ret;
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
        av_log(NULL, AV_LOG_WARNING, "setsockopt(SO_REUSEADDR) failed\n");
    }
    ret = bind(fd, addr, addrlen);
    if (ret)
        return ff_neterrno();

    ret = listen(fd, 1);
    if (ret)
        return ff_neterrno();
    return ret;
}

int ff_accept(int fd, int timeout, URLContext *h)
{
    int ret;
    struct pollfd lp = { fd, POLLIN, 0 };

    ret = ff_poll_interrupt(&lp, 1, timeout, &h->interrupt_callback);
    if (ret < 0)
        return ret;

    ret = accept(fd, NULL, NULL);
    if (ret < 0)
        return ff_neterrno();
    if (ff_socket_nonblock(ret, 1) < 0)
        av_log(h, AV_LOG_DEBUG, "ff_socket_nonblock failed\n");

    return ret;
}

int ff_listen_bind(int fd, const struct sockaddr *addr,
                   socklen_t addrlen, int timeout, URLContext *h)
{
    int ret;
    if ((ret = ff_listen(fd, addr, addrlen)) < 0)
        return ret;
    if ((ret = ff_accept(fd, timeout, h)) < 0)
        return ret;
    closesocket(fd);
    return ret;
}

int ff_listen_connect(int fd, const struct sockaddr *addr,
                      socklen_t addrlen, int timeout, URLContext *h,
                      int will_try_next)
{
    struct pollfd p = {fd, POLLOUT, 0};
    int ret;
    socklen_t optlen;

    if (ff_socket_nonblock(fd, 1) < 0)
        av_log(h, AV_LOG_DEBUG, "ff_socket_nonblock failed\n");

    while ((ret = connect(fd, addr, addrlen))) {
        ret = ff_neterrno();
        switch (ret) {
        case AVERROR(EINTR):
            if (ff_check_interrupt(&h->interrupt_callback))
                return AVERROR_EXIT;
            continue;
        case AVERROR(EINPROGRESS):
        case AVERROR(EAGAIN):
            ret = ff_poll_interrupt(&p, 1, timeout, &h->interrupt_callback);
            if (ret < 0)
                return ret;
            optlen = sizeof(ret);
            if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &optlen))
                ret = AVUNERROR(ff_neterrno());
            if (ret != 0) {
                char errbuf[100];
                ret = AVERROR(ret);
                av_strerror(ret, errbuf, sizeof(errbuf));
                if (will_try_next)
                    av_log(h, AV_LOG_WARNING,
                           "Connection to %s failed (%s), trying next address\n",
                           h->filename, errbuf);
                else
                    av_log(h, AV_LOG_ERROR, "Connection to %s failed: %s\n",
                           h->filename, errbuf);
            }
        default:
            return ret;
        }
    }
    return ret;
}

static void interleave_addrinfo(struct addrinfo *base)
{
    struct addrinfo **next = &base->ai_next;
    while (*next) {
        struct addrinfo *cur = *next;
        // Iterate forward until we find an entry of a different family.
        if (cur->ai_family == base->ai_family) {
            next = &cur->ai_next;
            continue;
        }
        if (cur == base->ai_next) {
            // If the first one following base is of a different family, just
            // move base forward one step and continue.
            base = cur;
            next = &base->ai_next;
            continue;
        }
        // Unchain cur from the rest of the list from its current spot.
        *next = cur->ai_next;
        // Hook in cur directly after base.
        cur->ai_next = base->ai_next;
        base->ai_next = cur;
        // Restart with a new base. We know that before moving the cur element,
        // everything between the previous base and cur had the same family,
        // different from cur->ai_family. Therefore, we can keep next pointing
        // where it was, and continue from there with base at the one after
        // cur.
        base = cur->ai_next;
    }
}

static void print_address_list(void *ctx, const struct addrinfo *addr,
                               const char *title)
{
    char hostbuf[100], portbuf[20];
    av_log(ctx, AV_LOG_DEBUG, "%s:\n", title);
    while (addr) {
        getnameinfo(addr->ai_addr, addr->ai_addrlen,
                    hostbuf, sizeof(hostbuf), portbuf, sizeof(portbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        av_log(ctx, AV_LOG_DEBUG, "Address %s port %s\n", hostbuf, portbuf);
        addr = addr->ai_next;
    }
}

struct ConnectionAttempt {
    int fd;
    int64_t deadline_us;
    struct addrinfo *addr;
};

// Returns < 0 on error, 0 on successfully started connection attempt,
// > 0 for a connection that succeeded already.
static int start_connect_attempt(struct ConnectionAttempt *attempt,
                                 struct addrinfo **ptr, int timeout_ms,
                                 URLContext *h,
                                 void (*customize_fd)(void *, int), void *customize_ctx)
{
    struct addrinfo *ai = *ptr;
    int ret;

    *ptr = ai->ai_next;

    attempt->fd = ff_socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (attempt->fd < 0)
        return ff_neterrno();
    attempt->deadline_us = av_gettime_relative() + timeout_ms * 1000;
    attempt->addr = ai;

    ff_socket_nonblock(attempt->fd, 1);

    if (customize_fd)
        customize_fd(customize_ctx, attempt->fd);

    while ((ret = connect(attempt->fd, ai->ai_addr, ai->ai_addrlen))) {
        ret = ff_neterrno();
        switch (ret) {
        case AVERROR(EINTR):
            if (ff_check_interrupt(&h->interrupt_callback)) {
                closesocket(attempt->fd);
                attempt->fd = -1;
                return AVERROR_EXIT;
            }
            continue;
        case AVERROR(EINPROGRESS):
        case AVERROR(EAGAIN):
            return 0;
        default:
            closesocket(attempt->fd);
            attempt->fd = -1;
            return ret;
        }
    }
    return 1;
}

// Try a new connection to another address after 200 ms, as suggested in
// RFC 8305 (or sooner if an earlier attempt fails).
#define NEXT_ATTEMPT_DELAY_MS 200

int ff_connect_parallel(struct addrinfo *addrs, int timeout_ms_per_address,
                        int parallel, URLContext *h, int *fd,
                        void (*customize_fd)(void *, int), void *customize_ctx)
{
    struct ConnectionAttempt attempts[3];
    struct pollfd pfd[3];
    int nb_attempts = 0, i, j;
    int64_t next_attempt_us = av_gettime_relative(), next_deadline_us;
    int last_err = AVERROR(EIO);
    socklen_t optlen;
    char errbuf[100], hostbuf[100], portbuf[20];

    if (parallel > FF_ARRAY_ELEMS(attempts))
        parallel = FF_ARRAY_ELEMS(attempts);

    print_address_list(h, addrs, "Original list of addresses");
    // This mutates the list, but the head of the list is still the same
    // element, so the caller, who owns the list, doesn't need to get
    // an updated pointer.
    interleave_addrinfo(addrs);
    print_address_list(h, addrs, "Interleaved list of addresses");

    while (nb_attempts > 0 || addrs) {
        // Start a new connection attempt, if possible.
        if (nb_attempts < parallel && addrs) {
            getnameinfo(addrs->ai_addr, addrs->ai_addrlen,
                        hostbuf, sizeof(hostbuf), portbuf, sizeof(portbuf),
                        NI_NUMERICHOST | NI_NUMERICSERV);
            av_log(h, AV_LOG_VERBOSE, "Starting connection attempt to %s port %s\n",
                                      hostbuf, portbuf);
            last_err = start_connect_attempt(&attempts[nb_attempts], &addrs,
                                             timeout_ms_per_address, h,
                                             customize_fd, customize_ctx);
            if (last_err < 0) {
                av_strerror(last_err, errbuf, sizeof(errbuf));
                av_log(h, AV_LOG_VERBOSE, "Connected attempt failed: %s\n",
                                          errbuf);
                continue;
            }
            if (last_err > 0) {
                for (i = 0; i < nb_attempts; i++)
                    closesocket(attempts[i].fd);
                *fd = attempts[nb_attempts].fd;
                return 0;
            }
            pfd[nb_attempts].fd = attempts[nb_attempts].fd;
            pfd[nb_attempts].events = POLLOUT;
            next_attempt_us = av_gettime_relative() + NEXT_ATTEMPT_DELAY_MS * 1000;
            nb_attempts++;
        }

        av_assert0(nb_attempts > 0);
        // The connection attempts are sorted from oldest to newest, so the
        // first one will have the earliest deadline.
        next_deadline_us = attempts[0].deadline_us;
        // If we can start another attempt in parallel, wait until that time.
        if (nb_attempts < parallel && addrs)
            next_deadline_us = FFMIN(next_deadline_us, next_attempt_us);
        last_err = ff_poll_interrupt(pfd, nb_attempts,
                                     (next_deadline_us - av_gettime_relative())/1000,
                                     &h->interrupt_callback);
        if (last_err < 0 && last_err != AVERROR(ETIMEDOUT))
            break;

        // Check the status from the poll output.
        for (i = 0; i < nb_attempts; i++) {
            last_err = 0;
            if (pfd[i].revents) {
                // Some sort of action for this socket, check its status (either
                // a successful connection or an error).
                optlen = sizeof(last_err);
                if (getsockopt(attempts[i].fd, SOL_SOCKET, SO_ERROR, &last_err, &optlen))
                    last_err = ff_neterrno();
                else if (last_err != 0)
                    last_err = AVERROR(last_err);
                if (last_err == 0) {
                    // Everything is ok, we seem to have a successful
                    // connection. Close other sockets and return this one.
                    for (j = 0; j < nb_attempts; j++)
                        if (j != i)
                            closesocket(attempts[j].fd);
                    *fd = attempts[i].fd;
                    getnameinfo(attempts[i].addr->ai_addr, attempts[i].addr->ai_addrlen,
                                hostbuf, sizeof(hostbuf), portbuf, sizeof(portbuf),
                                NI_NUMERICHOST | NI_NUMERICSERV);
                    av_log(h, AV_LOG_VERBOSE, "Successfully connected to %s port %s\n",
                                              hostbuf, portbuf);
                    return 0;
                }
            }
            if (attempts[i].deadline_us < av_gettime_relative() && !last_err)
                last_err = AVERROR(ETIMEDOUT);
            if (!last_err)
                continue;
            // Error (or timeout) for this socket; close the socket and remove
            // it from the attempts/pfd arrays, to let a new attempt start
            // directly.
            getnameinfo(attempts[i].addr->ai_addr, attempts[i].addr->ai_addrlen,
                        hostbuf, sizeof(hostbuf), portbuf, sizeof(portbuf),
                        NI_NUMERICHOST | NI_NUMERICSERV);
            av_strerror(last_err, errbuf, sizeof(errbuf));
            av_log(h, AV_LOG_VERBOSE, "Connection attempt to %s port %s "
                                      "failed: %s\n", hostbuf, portbuf, errbuf);
            closesocket(attempts[i].fd);
            memmove(&attempts[i], &attempts[i + 1],
                    (nb_attempts - i - 1) * sizeof(*attempts));
            memmove(&pfd[i], &pfd[i + 1],
                    (nb_attempts - i - 1) * sizeof(*pfd));
            i--;
            nb_attempts--;
        }
    }
    for (i = 0; i < nb_attempts; i++)
        closesocket(attempts[i].fd);
    if (last_err >= 0)
        last_err = AVERROR(ECONNREFUSED);
    if (last_err != AVERROR_EXIT) {
        av_strerror(last_err, errbuf, sizeof(errbuf));
        av_log(h, AV_LOG_ERROR, "Connection to %s failed: %s\n",
               h->filename, errbuf);
    }
    return last_err;
}

static int match_host_pattern(const char *pattern, const char *hostname)
{
    int len_p, len_h;
    if (!strcmp(pattern, "*"))
        return 1;
    // Skip a possible *. at the start of the pattern
    if (pattern[0] == '*')
        pattern++;
    if (pattern[0] == '.')
        pattern++;
    len_p = strlen(pattern);
    len_h = strlen(hostname);
    if (len_p > len_h)
        return 0;
    // Simply check if the end of hostname is equal to 'pattern'
    if (!strcmp(pattern, &hostname[len_h - len_p])) {
        if (len_h == len_p)
            return 1; // Exact match
        if (hostname[len_h - len_p - 1] == '.')
            return 1; // The matched substring is a domain and not just a substring of a domain
    }
    return 0;
}

int ff_http_match_no_proxy(const char *no_proxy, const char *hostname)
{
    char *buf, *start;
    int ret = 0;
    if (!no_proxy)
        return 0;
    if (!hostname)
        return 0;
    buf = av_strdup(no_proxy);
    if (!buf)
        return 0;
    start = buf;
    while (start) {
        char *sep, *next = NULL;
        start += strspn(start, " ,");
        sep = start + strcspn(start, " ,");
        if (*sep) {
            next = sep + 1;
            *sep = '\0';
        }
        if (match_host_pattern(start, hostname)) {
            ret = 1;
            break;
        }
        start = next;
    }
    av_free(buf);
    return ret;
}

void ff_log_net_error(void *ctx, int level, const char* prefix)
{
    char errbuf[100];
    av_strerror(ff_neterrno(), errbuf, sizeof(errbuf));
    av_log(ctx, level, "%s: %s\n", prefix, errbuf);
}
