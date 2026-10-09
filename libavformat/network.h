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

#ifndef AVFORMAT_NETWORK_H
#define AVFORMAT_NETWORK_H

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/error.h"
#include "os_support.h"
#include "avio.h"
#include "url.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_WINSOCK2_H
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT       WSAETIMEDOUT
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED    WSAECONNREFUSED
#endif
#ifndef EINPROGRESS
#define EINPROGRESS     WSAEINPROGRESS
#endif
#ifndef ENOTCONN
#define ENOTCONN        WSAENOTCONN
#endif

#define getsockopt(a, b, c, d, e) getsockopt(a, b, c, (char*) d, e)
#define setsockopt(a, b, c, d, e) setsockopt(a, b, c, (const char*) d, e)

int ff_neterrno(void);
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#define ff_neterrno() AVERROR(errno)
#endif /* HAVE_WINSOCK2_H */

#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#if HAVE_POLL_H
#include <poll.h>
#endif

int ff_socket_nonblock(int socket, int enable);

int ff_network_init(void);
void ff_network_close(void);

int ff_tls_init(void);
void ff_tls_deinit(void);

int ff_network_wait_fd(int fd, int write);

/**
 * This works similarly to ff_network_wait_fd, but waits up to 'timeout' microseconds
 * Uses ff_network_wait_fd in a loop
 *
 * @param fd Socket descriptor
 * @param write Set 1 to wait for socket able to be read, 0 to be written
 * @param timeout Timeout interval, in microseconds. Actual precision is 100000 mcs, due to ff_network_wait_fd usage
 * @param int_cb Interrupt callback, is checked before each ff_network_wait_fd call
 * @return 0 if data can be read/written, AVERROR(ETIMEDOUT) if timeout expired, or negative error code
 */
int ff_network_wait_fd_timeout(int fd, int write, int64_t timeout, AVIOInterruptCB *int_cb);

/**
 * Waits for up to 'timeout' microseconds. If the usert's int_cb is set and
 * triggered, return before that.
 * @param timeout Timeout in microseconds. Maybe have lower actual precision.
 * @param int_cb Interrupt callback, is checked regularly.
 * @return AVERROR(ETIMEDOUT) if timeout expirted, AVERROR_EXIT if interrupted by int_cb
 */
int ff_network_sleep_interruptible(int64_t timeout, AVIOInterruptCB *int_cb);

/**
 * A channel for waking a thread that is blocked in
 * ff_network_wait_fd_wakeup(), from another thread.
 *
 * Usage: the waiting thread calls ff_network_wait_fd_wakeup(); another
 * thread calls ff_network_wakeup_signal() to end that wait; once the
 * waiting thread has been joined, ff_network_wakeup_close() is called.
 *
 * rfd is pollable together with sockets on every platform. It is an
 * eventfd, the read end of a pipe, or a loopback datagram socket on
 * Windows, where only sockets can be waited on together with sockets.
 * wfd is the end ff_network_wakeup_signal() writes to; it equals rfd
 * unless a pipe is in use. On systems other than Windows both fds are
 * close-on-exec.
 */
typedef struct FFNetWakeup {
    int rfd;
    int wfd;
    atomic_int signaled;
} FFNetWakeup;

/**
 * Create a wakeup channel. On Windows ff_network_init() must have been
 * called first.
 *
 * @return 0 on success, a negative AVERROR code on failure
 */
int ff_network_wakeup_open(FFNetWakeup *w);

/**
 * Wake the thread blocked in ff_network_wait_fd_wakeup() on this channel,
 * or the next one that waits on it: the signal is not consumed, so a
 * signal sent before the wait starts is not lost. Only the first call has
 * an effect; the function may be called any number of times, from any
 * thread.
 *
 * @return 0 on success, a negative AVERROR code if the first call failed
 *         to write to the channel
 */
int ff_network_wakeup_signal(FFNetWakeup *w);

/**
 * Close the channel. Call it after the waiting thread has been joined and
 * no thread can signal any more.
 */
void ff_network_wakeup_close(FFNetWakeup *w);

/**
 * Like ff_network_wait_fd(), but also returns as soon as the wakeup
 * channel is signaled, and with a caller chosen timeout.
 *
 * @param fd Socket descriptor
 * @param write Set 1 to wait for the socket to be writable, 0 for readable
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @param w Wakeup channel to watch, must have been opened
 * @return 0 if fd is ready, AVERROR(EAGAIN) if the timeout expired,
 *         AVERROR_EXIT if w was signaled (also when fd is ready),
 *         or a negative error code
 */
int ff_network_wait_fd_wakeup(int fd, int write, int timeout_ms,
                              const FFNetWakeup *w);

#if !HAVE_STRUCT_SOCKADDR_STORAGE
struct sockaddr_storage {
#if HAVE_STRUCT_SOCKADDR_SA_LEN
    uint8_t ss_len;
    uint8_t ss_family;
#else
    uint16_t ss_family;
#endif /* HAVE_STRUCT_SOCKADDR_SA_LEN */
    char ss_pad1[6];
    int64_t ss_align;
    char ss_pad2[112];
};
#endif /* !HAVE_STRUCT_SOCKADDR_STORAGE */

typedef union sockaddr_union {
    struct sockaddr_storage storage;
    struct sockaddr_in in;
#if HAVE_STRUCT_SOCKADDR_IN6
    struct sockaddr_in6 in6;
#endif
} sockaddr_union;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#if !HAVE_STRUCT_ADDRINFO
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    int ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
#endif /* !HAVE_STRUCT_ADDRINFO */

/* getaddrinfo constants */
#ifndef EAI_AGAIN
#define EAI_AGAIN 2
#endif
#ifndef EAI_BADFLAGS
#define EAI_BADFLAGS 3
#endif
#ifndef EAI_FAIL
#define EAI_FAIL 4
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY 5
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY 6
#endif
#ifndef EAI_NODATA
#define EAI_NODATA 7
#endif
#ifndef EAI_NONAME
#define EAI_NONAME 8
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE 9
#endif
#ifndef EAI_SOCKTYPE
#define EAI_SOCKTYPE 10
#endif

#ifndef AI_PASSIVE
#define AI_PASSIVE 1
#endif

#ifndef AI_CANONNAME
#define AI_CANONNAME 2
#endif

#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 4
#endif

#ifndef NI_NOFQDN
#define NI_NOFQDN 1
#endif

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 2
#endif

#ifndef NI_NAMERQD
#define NI_NAMERQD 4
#endif

#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 8
#endif

#ifndef NI_DGRAM
#define NI_DGRAM 16
#endif

#if !HAVE_GETADDRINFO
int ff_getaddrinfo(const char *node, const char *service,
                   const struct addrinfo *hints, struct addrinfo **res);
void ff_freeaddrinfo(struct addrinfo *res);
int ff_getnameinfo(const struct sockaddr *sa, int salen,
                   char *host, int hostlen,
                   char *serv, int servlen, int flags);
#define getaddrinfo ff_getaddrinfo
#define freeaddrinfo ff_freeaddrinfo
#define getnameinfo ff_getnameinfo
#endif /* !HAVE_GETADDRINFO */

#if !HAVE_GETADDRINFO || HAVE_WINSOCK2_H
const char *ff_gai_strerror(int ecode);
#undef gai_strerror
#define gai_strerror ff_gai_strerror
#endif /* !HAVE_GETADDRINFO || HAVE_WINSOCK2_H */

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN INET_ADDRSTRLEN
#endif

#ifndef IN_MULTICAST
#define IN_MULTICAST(a) ((((uint32_t)(a)) & 0xf0000000) == 0xe0000000)
#endif
#ifndef IN6_IS_ADDR_MULTICAST
#define IN6_IS_ADDR_MULTICAST(a) (((uint8_t *) (a))[0] == 0xff)
#endif

int ff_is_multicast_address(struct sockaddr *addr);

#define POLLING_TIME 100 /// Time in milliseconds between interrupt check

/**
 * Bind to a file descriptor and poll for a connection.
 *
 * @param fd      First argument of bind().
 * @param addr    Second argument of bind().
 * @param addrlen Third argument of bind().
 * @param timeout Polling timeout in milliseconds.
 * @param h       URLContext providing interrupt check
 *                callback and logging context.
 * @return        A non-blocking file descriptor on success
 *                or an AVERROR on failure.
 */
int ff_listen_bind(int fd, const struct sockaddr *addr,
                   socklen_t addrlen, int timeout,
                   URLContext *h);

/**
 * Bind to a file descriptor to an address without accepting connections.
 * @param fd      First argument of bind().
 * @param addr    Second argument of bind().
 * @param addrlen Third argument of bind().
 * @return        0 on success or an AVERROR on failure.
 */
int ff_listen(int fd, const struct sockaddr *addr, socklen_t addrlen,
              void *logctx);

/**
 * Poll for a single connection on the passed file descriptor.
 * @param fd      The listening socket file descriptor.
 * @param timeout Polling timeout in milliseconds.
 * @param h       URLContext providing interrupt check
 *                callback and logging context.
 * @return        A non-blocking file descriptor on success
 *                or an AVERROR on failure.
 */
int ff_accept(int fd, int timeout, URLContext *h);

/**
 * Connect to a file descriptor and poll for result.
 *
 * @param fd       First argument of connect(),
 *                 will be set as non-blocking.
 * @param addr     Second argument of connect().
 * @param addrlen  Third argument of connect().
 * @param timeout  Polling timeout in milliseconds.
 * @param h        URLContext providing interrupt check
 *                 callback and logging context.
 * @param will_try_next Whether the caller will try to connect to another
 *                 address for the same host name, affecting the form of
 *                 logged errors.
 * @return         0 on success, AVERROR on failure.
 */
int ff_listen_connect(int fd, const struct sockaddr *addr,
                      socklen_t addrlen, int timeout,
                      URLContext *h, int will_try_next);

int ff_http_match_no_proxy(const char *no_proxy, const char *hostname);

int ff_socket(int domain, int type, int protocol, void *logctx);

void ff_log_net_error(void *ctx, int level, const char* prefix);

/**
 * Connect to any of the given addrinfo addresses, with multiple attempts
 * running in parallel.
 *
 * @param addrs    The list of addresses to try to connect to.
 *                 This list will be mutated internally, but the list head
 *                 will remain as such, so this doesn't affect the caller
 *                 freeing the list afterwards.
 * @param timeout_ms_per_address The number of milliseconds to wait for each
 *                 connection attempt. Since multiple addresses are tried,
 *                 some of them in parallel, the total run time will at most
 *                 be timeout_ms_per_address*ceil(nb_addrs/parallel) +
 *                 (parallel - 1) * NEXT_ATTEMPT_DELAY_MS.
 * @param parallel The maximum number of connections to attempt in parallel.
 *                 This is limited to an internal maximum capacity.
 * @param h        URLContext providing interrupt check
 *                 callback and logging context.
 * @param fd       If successful, the connected socket is returned here.
 * @param customize_fd Function that will be called for each socket created,
 *                 to allow the caller to set socket options before calling
 *                 connect() on it, may be NULL.
 * @param customize_ctx Context parameter passed to customize_fd.
 * @return         0 on success, AVERROR on failure.
 */
int ff_connect_parallel(struct addrinfo *addrs, int timeout_ms_per_address,
                        int parallel, URLContext *h, int *fd,
                        int (*customize_fd)(void *, int, int), void *customize_ctx);

void ff_udp_get_last_recv_addr(URLContext *h, struct sockaddr_storage *addr, socklen_t *addr_len);
int ff_udp_set_remote_addr(URLContext *h, const struct sockaddr *dest_addr, socklen_t dest_addr_len, int do_connect);

#endif /* AVFORMAT_NETWORK_H */
