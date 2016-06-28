/*
 * TCP protocol
 * Copyright (c) 2002 Fabrice Bellard
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
#include "avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/application.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#if HAVE_POLL_H
#include <poll.h>
#endif
#if HAVE_PTHREADS
#include <pthread.h>
#endif

typedef struct TCPContext {
    const AVClass *class;
    int fd;
    int listen;
    int open_timeout;
    int rw_timeout;
    int listen_timeout;
    int recv_buffer_size;
    int send_buffer_size;
    int tcp_nodelay;
    int64_t app_ctx_intptr;

    int addrinfo_timeout;

    AVApplicationContext *app_ctx;
} TCPContext;

#define OFFSET(x) offsetof(TCPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "listen",          "Listen for incoming connections",  OFFSET(listen),         AV_OPT_TYPE_INT, { .i64 = 0 },     0,       2,       .flags = D|E },
    { "timeout",     "set timeout (in microseconds) of socket I/O operations", OFFSET(rw_timeout),     AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "listen_timeout",  "Connection awaiting timeout (in milliseconds)",      OFFSET(listen_timeout), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "send_buffer_size", "Socket send buffer size (in bytes)",                OFFSET(send_buffer_size), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "recv_buffer_size", "Socket receive buffer size (in bytes)",             OFFSET(recv_buffer_size), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "tcp_nodelay", "Use TCP_NODELAY to disable nagle's algorithm",           OFFSET(tcp_nodelay), AV_OPT_TYPE_BOOL, { .i64 = 0 },             0, 1, .flags = D|E },
    { "ijkapplication",   "AVApplicationContext",                              OFFSET(app_ctx_intptr),   AV_OPT_TYPE_INT64, { .i64 = 0 }, INT64_MIN, INT64_MAX, .flags = D },

    { "addrinfo_timeout", "set timeout (in microseconds) for getaddrinfo()",   OFFSET(addrinfo_timeout), AV_OPT_TYPE_INT, { .i64 = -1 },       -1, INT_MAX, .flags = D|E },
    { NULL }
};

static const AVClass tcp_class = {
    .class_name = "tcp",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#ifdef HAVE_PTHREADS

typedef struct TCPAddrinfoRequest
{
    AVBufferRef *buffer;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    AVIOInterruptCB interrupt_callback;

    char            *hostname;
    char            *servname;
    struct addrinfo  hints;
    struct addrinfo *res;

    volatile int     finished;
    int              last_error;
} TCPAddrinfoRequest;

static void tcp_getaddrinfo_request_free(TCPAddrinfoRequest *req)
{
    av_assert0(req);
    if (req->res) {
        freeaddrinfo(req->res);
        req->res = NULL;
    }

    av_freep(&req->servname);
    av_freep(&req->hostname);
    pthread_cond_destroy(&req->cond);
    pthread_mutex_destroy(&req->mutex);
    av_freep(&req);
}

static void tcp_getaddrinfo_request_free_buffer(void *opaque, uint8_t *data)
{
    av_assert0(opaque);
    TCPAddrinfoRequest *req = (TCPAddrinfoRequest *)opaque;
    tcp_getaddrinfo_request_free(req);
}

static int tcp_getaddrinfo_request_create(TCPAddrinfoRequest **request,
                                          const char *hostname,
                                          const char *servname,
                                          const struct addrinfo *hints,
                                          const AVIOInterruptCB *int_cb)
{
    TCPAddrinfoRequest *req = (TCPAddrinfoRequest *) av_mallocz(sizeof(TCPAddrinfoRequest));
    if (!req)
        return AVERROR(ENOMEM);

    if (pthread_mutex_init(&req->mutex, NULL)) {
        av_freep(&req);
        return AVERROR(ENOMEM);
    }

    if (pthread_cond_init(&req->cond, NULL)) {
        pthread_mutex_destroy(&req->mutex);
        av_freep(&req);
        return AVERROR(ENOMEM);
    }

    if (int_cb)
        req->interrupt_callback = *int_cb;

    if (hostname) {
        req->hostname = av_strdup(hostname);
        if (!req->hostname)
            goto fail;
    }

    if (servname) {
        req->servname = av_strdup(servname);
        if (!req->hostname)
            goto fail;
    }

    if (hints) {
        req->hints.ai_family   = hints->ai_family;
        req->hints.ai_socktype = hints->ai_socktype;
        req->hints.ai_protocol = hints->ai_protocol;
        req->hints.ai_flags    = hints->ai_flags;
    }

    req->buffer = av_buffer_create(NULL, 0, tcp_getaddrinfo_request_free_buffer, req, 0);
    if (!req->buffer)
        goto fail;

    *request = req;
    return 0;
fail:
    tcp_getaddrinfo_request_free(req);
    return AVERROR(ENOMEM);
}

static void *tcp_getaddrinfo_worker(void *arg)
{
    TCPAddrinfoRequest *req = arg;

    getaddrinfo(req->hostname, req->servname, &req->hints, &req->res);
    pthread_mutex_lock(&req->mutex);
    req->finished = 1;
    pthread_cond_signal(&req->cond);
    pthread_mutex_unlock(&req->mutex);
    av_buffer_unref(&req->buffer);
    return NULL;
}

static void *tcp_getaddrinfo_one_by_one_worker(void *arg)
{
    struct addrinfo *temp_addrinfo = NULL;
    struct addrinfo *cur = NULL;
    int ret = EAI_FAIL;
    int i = 0;
    int option_length = 0;

    pthread_mutex_lock(&req->mutex);
    req->finished = 1;
    pthread_cond_signal(&req->cond);
    pthread_mutex_unlock(&req->mutex);
    av_buffer_unref(&req->buffer);
    return NULL;
}

int ijk_tcp_getaddrinfo_nonblock(const char *hostname, const char *servname,
                                 const struct addrinfo *hints, struct addrinfo **res,
                                 int64_t timeout,
                                 const AVIOInterruptCB *int_cb)
{
    int     ret;
    int64_t start;
    int64_t now;
    AVBufferRef        *req_ref = NULL;
    TCPAddrinfoRequest *req     = NULL;
    pthread_t work_thread;

    if (hostname && !hostname[0])
        hostname = NULL;

    if (timeout <= 0)
        return getaddrinfo(hostname, servname, hints, res);

    ret = tcp_getaddrinfo_request_create(&req, hostname, servname, hints, int_cb);
    if (ret)
        goto fail;

    req_ref = av_buffer_ref(req->buffer);
    if (req_ref == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* FIXME: using a thread pool would be better. */
    ret = pthread_create(&work_thread, NULL, tcp_getaddrinfo_worker, req);
    if (ret) {
        ret = AVERROR(ret);
        goto fail;
    }

    pthread_detach(work_thread);

    start = av_gettime();
    now   = start;

    pthread_mutex_lock(&req->mutex);
    while (1) {
        int64_t wait_time = now + 100000;
        struct timespec tv = { .tv_sec  =  wait_time / 1000000,
                               .tv_nsec = (wait_time % 1000000) * 1000 };

        if (req->finished || (start + timeout < now)) {
            if (req->res) {
                ret = 0;
                *res = req->res;
                req->res = NULL;
            } else {
                ret = req->last_error ? req->last_error : AVERROR_EXIT;
            }
            break;
        }
#if defined(__ANDROID__) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
        ret = pthread_cond_timedwait_monotonic_np(&req->cond, &req->mutex, &tv);
#else
        ret = pthread_cond_timedwait(&req->cond, &req->mutex, &tv);
#endif
        if (ret != 0 && ret != ETIMEDOUT) {
            av_log(NULL, AV_LOG_ERROR, "pthread_cond_timedwait failed: %d\n", ret);
            ret = AVERROR_EXIT;
            break;
        }

        if (ff_check_interrupt(&req->interrupt_callback)) {
            ret = AVERROR_EXIT;
            break;
        }

        now = av_gettime();
    }
    pthread_mutex_unlock(&req->mutex);
fail:
    av_buffer_unref(&req_ref);
    return ret;
}

#else
int ijk_tcp_getaddrinfo_nonblock(const char *hostname, const char *servname,
                                 const struct addrinfo *hints, struct addrinfo **res,
                                 int64_t timeout,
                                 const AVIOInterruptCB *int_cb)
{
    return getaddrinfo(hostname, servname, hints, res);
}
#endif

/* return non zero if error */
static int tcp_open(URLContext *h, const char *uri, int flags)
{
    struct addrinfo hints = { 0 }, *ai, *cur_ai;
    int port, fd = -1;
    TCPContext *s = h->priv_data;
    const char *p;
    char buf[256];
    int ret;
    char hostname[1024],proto[1024],path[1024];
    char portstr[10];
    s->open_timeout = 5000000;
    s->app_ctx = (AVApplicationContext *)(intptr_t)s->app_ctx_intptr;

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto, "tcp"))
        return AVERROR(EINVAL);
    if (port <= 0 || port >= 65536) {
        av_log(h, AV_LOG_ERROR, "Port missing in uri\n");
        return AVERROR(EINVAL);
    }
    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "listen", p)) {
            char *endptr = NULL;
            s->listen = strtol(buf, &endptr, 10);
            /* assume if no digits were found it is a request to enable it */
            if (buf == endptr)
                s->listen = 1;
        }
        if (av_find_info_tag(buf, sizeof(buf), "timeout", p)) {
            s->rw_timeout = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "listen_timeout", p)) {
            s->listen_timeout = strtol(buf, NULL, 10);
        }
    }
    if (s->rw_timeout >= 0) {
        s->open_timeout =
        h->rw_timeout   = s->rw_timeout;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (s->listen)
        hints.ai_flags |= AI_PASSIVE;
#ifdef HAVE_PTHREADS
    ret = ijk_tcp_getaddrinfo_nonblock(hostname, portstr, &hints, &ai, s->addrinfo_timeout, &h->interrupt_callback);
#else
    if (s->addrinfo_timeout > 0)
        av_log(h, AV_LOG_WARNING, "Ignore addrinfo_timeout without pthreads support.\n")
    if (!hostname[0])
        ret = getaddrinfo(NULL, portstr, &hints, &ai);
    else
        ret = getaddrinfo(hostname, portstr, &hints, &ai);
#endif

    if (ret) {
        av_log(h, AV_LOG_ERROR,
               "Failed to resolve hostname %s: %s\n",
               hostname, gai_strerror(ret));
        return AVERROR(EIO);
    }

    cur_ai = ai;

 restart:
#if HAVE_STRUCT_SOCKADDR_IN6
    // workaround for IOS9 getaddrinfo in IPv6 only network use hardcode IPv4 address can not resolve port number.
    if (cur_ai->ai_family == AF_INET6){
        struct sockaddr_in6 * sockaddr_v6 = (struct sockaddr_in6 *)cur_ai->ai_addr;
        if (!sockaddr_v6->sin6_port){
            sockaddr_v6->sin6_port = htons(port);
        }
    }
#endif

    fd = ff_socket(cur_ai->ai_family,
                   cur_ai->ai_socktype,
                   cur_ai->ai_protocol);
    if (fd < 0) {
        ret = ff_neterrno();
        goto fail;
    }

    /* Set the socket's send or receive buffer sizes, if specified.
       If unspecified or setting fails, system default is used. */
    if (s->recv_buffer_size > 0) {
        setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &s->recv_buffer_size, sizeof (s->recv_buffer_size));
    }
    if (s->send_buffer_size > 0) {
        setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &s->send_buffer_size, sizeof (s->send_buffer_size));
    }
    if (s->tcp_nodelay > 0) {
        setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &s->tcp_nodelay, sizeof (s->tcp_nodelay));
    }

    if (s->listen == 2) {
        // multi-client
        if ((ret = ff_listen(fd, cur_ai->ai_addr, cur_ai->ai_addrlen)) < 0)
            goto fail1;
    } else if (s->listen == 1) {
        // single client
        if ((ret = ff_listen_bind(fd, cur_ai->ai_addr, cur_ai->ai_addrlen,
                                  s->listen_timeout, h)) < 0)
            goto fail1;
        // Socket descriptor already closed here. Safe to overwrite to client one.
        fd = ret;
    } else {
        ret = av_application_on_tcp_will_open(s->app_ctx);
        if (ret) {
            av_log(NULL, AV_LOG_WARNING, "terminated by application in AVAPP_CTRL_WILL_TCP_OPEN");
            goto fail1;
        }

        if ((ret = ff_listen_connect(fd, cur_ai->ai_addr, cur_ai->ai_addrlen,
                                     s->open_timeout / 1000, h, !!cur_ai->ai_next)) < 0) {
            if (av_application_on_tcp_did_open(s->app_ctx, ret, fd))
                goto fail1;
            if (ret == AVERROR_EXIT)
                goto fail1;
            else
                goto fail;
        } else {
            ret = av_application_on_tcp_did_open(s->app_ctx, 0, fd);
            if (ret) {
                av_log(NULL, AV_LOG_WARNING, "terminated by application in AVAPP_CTRL_DID_TCP_OPEN");
                goto fail1;
            }
        }
    }

    h->is_streamed = 1;
    s->fd = fd;

    freeaddrinfo(ai);
    return 0;

 fail:
    if (cur_ai->ai_next) {
        /* Retry with the next sockaddr */
        cur_ai = cur_ai->ai_next;
        if (fd >= 0)
            closesocket(fd);
        ret = 0;
        goto restart;
    }
 fail1:
    if (fd >= 0)
        closesocket(fd);
    freeaddrinfo(ai);
    return ret;
}

static int tcp_accept(URLContext *s, URLContext **c)
{
    TCPContext *sc = s->priv_data;
    TCPContext *cc;
    int ret;
    av_assert0(sc->listen);
    if ((ret = ffurl_alloc(c, s->filename, s->flags, &s->interrupt_callback)) < 0)
        return ret;
    cc = (*c)->priv_data;
    ret = ff_accept(sc->fd, sc->listen_timeout, s);
    if (ret < 0)
        return ret;
    cc->fd = ret;
    return 0;
}

static int tcp_read(URLContext *h, uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd_timeout(s->fd, 0, h->rw_timeout, &h->interrupt_callback);
        if (ret)
            return ret;
    }
    ret = recv(s->fd, buf, size, 0);
    if (ret == 0)
        return AVERROR_EOF;
    if (ret > 0)
        av_application_did_io_tcp_read(s->app_ctx, (void*)h, ret);
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_write(URLContext *h, const uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd_timeout(s->fd, 1, h->rw_timeout, &h->interrupt_callback);
        if (ret)
            return ret;
    }
    ret = send(s->fd, buf, size, MSG_NOSIGNAL);
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_shutdown(URLContext *h, int flags)
{
    TCPContext *s = h->priv_data;
    int how;

    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ) {
        how = SHUT_RDWR;
    } else if (flags & AVIO_FLAG_WRITE) {
        how = SHUT_WR;
    } else {
        how = SHUT_RD;
    }

    return shutdown(s->fd, how);
}

static int tcp_close(URLContext *h)
{
    TCPContext *s = h->priv_data;
    closesocket(s->fd);
    return 0;
}

static int tcp_get_file_handle(URLContext *h)
{
    TCPContext *s = h->priv_data;
    return s->fd;
}

static int tcp_get_window_size(URLContext *h)
{
    TCPContext *s = h->priv_data;
    int avail;
    socklen_t avail_len = sizeof(avail);

#if HAVE_WINSOCK2_H
    /* SO_RCVBUF with winsock only reports the actual TCP window size when
    auto-tuning has been disabled via setting SO_RCVBUF */
    if (s->recv_buffer_size < 0) {
        return AVERROR(ENOSYS);
    }
#endif

    if (getsockopt(s->fd, SOL_SOCKET, SO_RCVBUF, &avail, &avail_len)) {
        return ff_neterrno();
    }
    return avail;
}

const URLProtocol ff_tcp_protocol = {
    .name                = "tcp",
    .url_open            = tcp_open,
    .url_accept          = tcp_accept,
    .url_read            = tcp_read,
    .url_write           = tcp_write,
    .url_close           = tcp_close,
    .url_get_file_handle = tcp_get_file_handle,
    .url_get_short_seek  = tcp_get_window_size,
    .url_shutdown        = tcp_shutdown,
    .priv_data_size      = sizeof(TCPContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &tcp_class,
};
