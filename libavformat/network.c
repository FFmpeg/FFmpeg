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
#include "libavcodec/internal.h"

#define THREADS (HAVE_PTHREADS || (defined(WIN32) && !defined(__MINGW32CE__)))

#if THREADS
#if HAVE_PTHREADS
#include <pthread.h>
#else
#include "libavcodec/w32pthreads.h"
#endif
#endif

#if CONFIG_OPENSSL
#include <openssl/ssl.h>
static int openssl_init;
#if THREADS
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
#if THREADS && GNUTLS_VERSION_NUMBER <= 0x020b00
#include <gcrypt.h>
#include <errno.h>
#undef malloc
#undef free
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
#if THREADS
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
#if THREADS && GNUTLS_VERSION_NUMBER < 0x020b00
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
#if THREADS
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
    if (!ff_network_inited_globally)
        av_log(NULL, AV_LOG_WARNING, "Using network protocols without global "
                                     "network initialization. Please use "
                                     "avformat_network_init(), this will "
                                     "become mandatory later.\n");
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
    ret = poll(&p, 1, 100);
    return ret < 0 ? ff_neterrno() : p.revents & (ev | POLLERR | POLLHUP) ? 0 : AVERROR(EAGAIN);
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

