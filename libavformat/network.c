/*
 * Copyright (c) 2007 The Libav Project
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

#include "libavutil/avutil.h"
#include "network.h"
#include "url.h"
#include "libavcodec/internal.h"
#include "libavutil/mem.h"
#include "url.h"
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
#include "libavutil/avutil.h"
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
        ret = ff_network_wait_fd(fd, write);
        if (ret != AVERROR(EAGAIN))
            return ret;
        if (ff_check_interrupt(int_cb))
            return AVERROR_EXIT;
        if (timeout) {
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

int ff_listen_bind(int fd, const struct sockaddr *addr,
                   socklen_t addrlen, int timeout)
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

    ret = poll(&lp, 1, timeout >= 0 ? timeout : -1);
    if (ret <= 0)
        return AVERROR(ETIMEDOUT);

    ret = accept(fd, NULL, NULL);
    if (ret < 0)
        return ff_neterrno();

    closesocket(fd);

    ff_socket_nonblock(ret, 1);
    return ret;
}

int ff_listen_connect(int fd, const struct sockaddr *addr,
                      socklen_t addrlen, int rw_timeout, URLContext *h)
{
    struct pollfd p = {fd, POLLOUT, 0};
    int64_t wait_started;
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
            wait_started = av_gettime();
            do {
                if (ff_check_interrupt(&h->interrupt_callback))
                    return AVERROR_EXIT;
                ret = poll(&p, 1, 100);
                if (ret > 0)
                    break;
            } while (!rw_timeout || (av_gettime() - wait_started < rw_timeout));
            if (ret <= 0)
                return AVERROR(ETIMEDOUT);
            optlen = sizeof(ret);
            if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &ret, &optlen))
                ret = AVUNERROR(ff_neterrno());
            if (ret != 0) {
                char errbuf[100];
                ret = AVERROR(ret);
                av_strerror(ret, errbuf, sizeof(errbuf));
                av_log(h, AV_LOG_ERROR,
                       "Connection to %s failed: %s\n",
                       h->filename, errbuf);
            }
        default:
            return ret;
        }
    }
    return ret;
}
