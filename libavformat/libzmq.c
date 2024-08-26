/*
 * ZeroMQ Protocol
 * Copyright (c) 2019 Andriy Gelman
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

#include <zmq.h>
#include "url.h"
#include "network.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#define ZMQ_STRERROR zmq_strerror(zmq_errno())

typedef struct ZMQContext {
    const AVClass *class;
    void *context;
    void *socket;
    int   pkt_size;
    int   pkt_size_overflow; /*keep track of the largest packet during overflow*/
} ZMQContext;

#define OFFSET(x) offsetof(ZMQContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pkt_size", "Maximum send/read packet size", OFFSET(pkt_size), AV_OPT_TYPE_INT, { .i64 = 131072 }, -1, INT_MAX, .flags = D | E },
    { NULL }
};

static int zmq_proto_wait(URLContext *h, void *socket, int write)
{
    int ret;
    int ev = write ? ZMQ_POLLOUT : ZMQ_POLLIN;
    zmq_pollitem_t items = { .socket = socket, .fd = 0, .events = ev, .revents = 0 };
    ret = zmq_poll(&items, 1, POLLING_TIME);
    if (ret == -1) {
        av_log(h, AV_LOG_ERROR, "Error occurred during zmq_poll(): %s\n", ZMQ_STRERROR);
        return AVERROR_EXTERNAL;
    }
    return items.revents & ev ? 0 : AVERROR(EAGAIN);
}

static int zmq_proto_wait_timeout(URLContext *h, void *socket, int write, int64_t timeout, AVIOInterruptCB *int_cb)
{
    int ret;
    int64_t wait_start = 0;

    while (1) {
        if (ff_check_interrupt(int_cb))
            return AVERROR_EXIT;
        ret = zmq_proto_wait(h, socket, write);
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

static int zmq_proto_open(URLContext *h, const char *uri, int flags)
{
    int ret;
    ZMQContext *s        = h->priv_data;
    s->pkt_size_overflow = 0;
    h->is_streamed       = 1;

    if (s->pkt_size > 0)
        h->max_packet_size = s->pkt_size;

    s->context = zmq_ctx_new();
    if (!s->context) {
        /*errno not set on failure during zmq_ctx_new()*/
        av_log(h, AV_LOG_ERROR, "Error occurred during zmq_ctx_new()\n");
        return AVERROR_EXTERNAL;
    }

    if (!av_strstart(uri, "zmq:", &uri)) {
        av_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
        return AVERROR(EINVAL);
    }

    /*publish during write*/
    if (h->flags & AVIO_FLAG_WRITE) {
        s->socket = zmq_socket(s->context, ZMQ_PUB);
        if (!s->socket) {
            av_log(h, AV_LOG_ERROR, "Error occurred during zmq_socket(): %s\n", ZMQ_STRERROR);
            goto fail_term;
        }

        ret = zmq_bind(s->socket, uri);
        if (ret == -1) {
            av_log(h, AV_LOG_ERROR, "Error occurred during zmq_bind(): %s\n", ZMQ_STRERROR);
            goto fail_close;
        }
    }

    /*subscribe for read*/
    if (h->flags & AVIO_FLAG_READ) {
        s->socket = zmq_socket(s->context, ZMQ_SUB);
        if (!s->socket) {
            av_log(h, AV_LOG_ERROR, "Error occurred during zmq_socket(): %s\n", ZMQ_STRERROR);
            goto fail_term;
        }

        ret = zmq_setsockopt(s->socket, ZMQ_SUBSCRIBE, "", 0);
        if (ret == -1) {
            av_log(h, AV_LOG_ERROR, "Error occurred during zmq_setsockopt(): %s\n", ZMQ_STRERROR);
            goto fail_close;
        }

        ret = zmq_connect(s->socket, uri);
        if (ret == -1) {
            av_log(h, AV_LOG_ERROR, "Error occurred during zmq_connect(): %s\n", ZMQ_STRERROR);
            goto fail_close;
        }
    }
    return 0;

fail_close:
    zmq_close(s->socket);
fail_term:
    zmq_ctx_term(s->context);
    return AVERROR_EXTERNAL;
}

static int zmq_proto_write(URLContext *h, const unsigned char *buf, int size)
{
    int ret;
    ZMQContext *s = h->priv_data;

    ret = zmq_proto_wait_timeout(h, s->socket, 1, h->rw_timeout, &h->interrupt_callback);
    if (ret)
        return ret;
    ret = zmq_send(s->socket, buf, size, 0);
    if (ret == -1) {
        av_log(h, AV_LOG_ERROR, "Error occurred during zmq_send(): %s\n", ZMQ_STRERROR);
        return AVERROR_EXTERNAL;
    }
    return ret; /*number of bytes sent*/
}

static int zmq_proto_read(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    ZMQContext *s = h->priv_data;

    ret = zmq_proto_wait_timeout(h, s->socket, 0, h->rw_timeout, &h->interrupt_callback);
    if (ret)
        return ret;
    ret = zmq_recv(s->socket, buf, size, 0);
    if (ret == -1) {
        av_log(h, AV_LOG_ERROR, "Error occurred during zmq_recv(): %s\n", ZMQ_STRERROR);
        return AVERROR_EXTERNAL;
    }
    if (ret > size) {
        s->pkt_size_overflow = FFMAX(s->pkt_size_overflow, ret);
        av_log(h, AV_LOG_WARNING, "Message exceeds available space in the buffer. Message will be truncated. Setting -pkt_size %d may resolve the issue.\n", s->pkt_size_overflow);
        ret = size;
    }
    return ret; /*number of bytes read*/
}

static int zmq_proto_close(URLContext *h)
{
    ZMQContext *s = h->priv_data;
    zmq_close(s->socket);
    zmq_ctx_term(s->context);
    return 0;
}

static const AVClass zmq_context_class = {
    .class_name = "zmq",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libzmq_protocol = {
    .name            = "zmq",
    .url_close       = zmq_proto_close,
    .url_open        = zmq_proto_open,
    .url_read        = zmq_proto_read,
    .url_write       = zmq_proto_write,
    .priv_data_size  = sizeof(ZMQContext),
    .priv_data_class = &zmq_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
};
