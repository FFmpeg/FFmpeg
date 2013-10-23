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
#include "url.h"
#include "libavcodec/internal.h"
#include "libavutil/avutil.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"

#if HAVE_THREADS
#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_OS2THREADS
#include "compat/os2threads.h"
#else
#include "compat/w32pthreads.h"
#endif
#endif

#if CONFIG_OPENSSL
#include <openssl/ssl.h>
static int openssl_init;
#if HAVE_THREADS
#include <openssl/crypto.h>
pthread_mutex_t *openssl_mutexes;
static void openssl_lock(int mode, int type, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        pthread_mutex_lock(&openssl_mutexes[type]);
    else
        pthread_mutex_unlock(&openssl_mutexes[type]);
}
#if !defined(WIN32) && OPENSSL_VERSION_NUMBER < 0x10000000
static unsigned long openssl_thread_id(void)
{
    return (intptr_t) pthread_self();
}
#endif
#endif
#endif
#if CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#if HAVE_THREADS && GNUTLS_VERSION_NUMBER <= 0x020b00
#include <gcrypt.h>
#include <errno.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif
#endif

void ff_tls_init(void)
{
    avpriv_lock_avformat();
#if CONFIG_OPENSSL
    if (!openssl_init) {
        SSL_library_init();
        SSL_load_error_strings();
#if HAVE_THREADS
        if (!CRYPTO_get_locking_callback()) {
            int i;
            openssl_mutexes = av_malloc(sizeof(pthread_mutex_t) * CRYPTO_num_locks());
            for (i = 0; i < CRYPTO_num_locks(); i++)
                pthread_mutex_init(&openssl_mutexes[i], NULL);
            CRYPTO_set_locking_callback(openssl_lock);
#if !defined(WIN32) && OPENSSL_VERSION_NUMBER < 0x10000000
            CRYPTO_set_id_callback(openssl_thread_id);
#endif
        }
#endif
    }
    openssl_init++;
#endif
#if CONFIG_GNUTLS
#if HAVE_THREADS && GNUTLS_VERSION_NUMBER < 0x020b00
    if (gcry_control(GCRYCTL_ANY_INITIALIZATION_P) == 0)
        gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
    gnutls_global_init();
#endif
    avpriv_unlock_avformat();
}

void ff_tls_deinit(void)
{
    avpriv_lock_avformat();
#if CONFIG_OPENSSL
    openssl_init--;
    if (!openssl_init) {
#if HAVE_THREADS
        if (CRYPTO_get_locking_callback() == openssl_lock) {
            int i;
            CRYPTO_set_locking_callback(NULL);
            for (i = 0; i < CRYPTO_num_locks(); i++)
                pthread_mutex_destroy(&openssl_mutexes[i]);
            av_free(openssl_mutexes);
        }
#endif
    }
#endif
#if CONFIG_GNUTLS
    gnutls_global_deinit();
#endif
    avpriv_unlock_avformat();
}

int ff_network_inited_globally;

int ff_network_init(void)
{
#if HAVE_WINSOCK2_H
    WSADATA wsaData;
#endif

    if (!ff_network_inited_globally)
        av_log(NULL, AV_LOG_WARNING, "Using network protocols without global "
                                     "network initialization. Please use "
                                     "avformat_network_init(), this will "
                                     "become mandatory later.\n");
#if HAVE_WINSOCK2_H
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
    ret = poll(&p, 1, 100);
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
                wait_start = av_gettime();
            else if (av_gettime() - wait_start > timeout)
                return AVERROR(ETIMEDOUT);
        }
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
        if (ret != 0)
            break;
    } while (timeout <= 0 || runs-- > 0);

    if (!ret)
        return AVERROR(ETIMEDOUT);
    if (ret < 0)
        return AVERROR(errno);
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
    return fd;
}

int ff_listen_bind(int fd, const struct sockaddr *addr,
                   socklen_t addrlen, int timeout, URLContext *h)
{
    int ret;
    int reuse = 1;
    struct pollfd lp = { fd, POLLIN, 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
        av_log(NULL, AV_LOG_WARNING, "setsockopt(SO_REUSEADDR) failed\n");
    }
    ret = bind(fd, addr, addrlen);
    if (ret)
        return ff_neterrno();

    ret = listen(fd, 1);
    if (ret)
        return ff_neterrno();

    ret = ff_poll_interrupt(&lp, 1, timeout, &h->interrupt_callback);
    if (ret < 0)
        return ret;

    ret = accept(fd, NULL, NULL);
    if (ret < 0)
        return ff_neterrno();

    closesocket(fd);

    ff_socket_nonblock(ret, 1);
    return ret;
}

int ff_listen_connect(int fd, const struct sockaddr *addr,
                      socklen_t addrlen, int timeout, URLContext *h,
                      int will_try_next)
{
    struct pollfd p = {fd, POLLOUT, 0};
    int ret;
    socklen_t optlen;

    ff_socket_nonblock(fd, 1);

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
