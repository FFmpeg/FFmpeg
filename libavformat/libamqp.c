/*
 * Advanced Message Queuing Protocol (AMQP) 0-9-1
 * Copyright (c) 2020 Andriy Gelman
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

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <sys/time.h>
#include "avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "network.h"
#include "url.h"
#include "urldecode.h"

typedef struct AMQPContext {
    const AVClass *class;
    amqp_connection_state_t conn;
    amqp_socket_t *socket;
    const char *exchange;
    const char *routing_key;
    int pkt_size;
    int64_t connection_timeout;
    int pkt_size_overflow;
    int delivery_mode;
} AMQPContext;

#define STR_LEN           1024
#define DEFAULT_CHANNEL   1

#define OFFSET(x) offsetof(AMQPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pkt_size", "Maximum send/read packet size", OFFSET(pkt_size), AV_OPT_TYPE_INT, { .i64 = 131072 }, 4096, INT_MAX, .flags = D | E },
    { "exchange", "Exchange to send/read packets", OFFSET(exchange), AV_OPT_TYPE_STRING, { .str = "amq.direct" }, 0, 0, .flags = D | E },
    { "routing_key", "Key to filter streams", OFFSET(routing_key), AV_OPT_TYPE_STRING, { .str = "amqp" }, 0, 0, .flags = D | E },
    { "connection_timeout", "Initial connection timeout", OFFSET(connection_timeout), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, .flags = D | E},
    { "delivery_mode",  "Delivery mode", OFFSET(delivery_mode), AV_OPT_TYPE_INT, { .i64 = AMQP_DELIVERY_PERSISTENT }, 1, 2, .flags = E, "delivery_mode"},
    { "persistent",     "Persistent delivery mode",     0, AV_OPT_TYPE_CONST, { .i64 = AMQP_DELIVERY_PERSISTENT }, 0, 0, E, "delivery_mode" },
    { "non-persistent", "Non-persistent delivery mode", 0, AV_OPT_TYPE_CONST, { .i64 = AMQP_DELIVERY_NONPERSISTENT }, 0, 0, E, "delivery_mode" },
    { NULL }
};

static int amqp_proto_open(URLContext *h, const char *uri, int flags)
{
    int ret, server_msg;
    char hostname[STR_LEN], credentials[STR_LEN];
    int port;
    const char *user, *password = NULL;
    const char *user_decoded, *password_decoded;
    char *p;
    amqp_rpc_reply_t broker_reply;
    struct timeval tval = { 0 };

    AMQPContext *s = h->priv_data;

    h->is_streamed     = 1;
    h->max_packet_size = s->pkt_size;

    av_url_split(NULL, 0, credentials, sizeof(credentials),
                 hostname, sizeof(hostname), &port, NULL, 0, uri);

    if (port < 0)
        port = 5672;

    if (hostname[0] == '\0' || port <= 0 || port > 65535 ) {
        av_log(h, AV_LOG_ERROR, "Invalid hostname/port\n");
        return AVERROR(EINVAL);
    }

    p = strchr(credentials, ':');
    if (p) {
        *p = '\0';
        password = p + 1;
    }

    if (!password || *password == '\0')
        password = "guest";

    password_decoded = ff_urldecode(password, 0);
    if (!password_decoded)
        return AVERROR(ENOMEM);

    user = credentials;
    if (*user == '\0')
        user = "guest";

    user_decoded = ff_urldecode(user, 0);
    if (!user_decoded) {
        av_freep(&password_decoded);
        return AVERROR(ENOMEM);
    }

    s->conn = amqp_new_connection();
    if (!s->conn) {
        av_freep(&user_decoded);
        av_freep(&password_decoded);
        av_log(h, AV_LOG_ERROR, "Error creating connection\n");
        return AVERROR_EXTERNAL;
    }

    s->socket = amqp_tcp_socket_new(s->conn);
    if (!s->socket) {
        av_log(h, AV_LOG_ERROR, "Error creating socket\n");
        goto destroy_connection;
    }

    if (s->connection_timeout < 0)
        s->connection_timeout = (h->rw_timeout > 0 ? h->rw_timeout : 5000000);

    tval.tv_sec  = s->connection_timeout / 1000000;
    tval.tv_usec = s->connection_timeout % 1000000;
    ret = amqp_socket_open_noblock(s->socket, hostname, port, &tval);

    if (ret) {
        av_log(h, AV_LOG_ERROR, "Error connecting to server: %s\n",
                                 amqp_error_string2(ret));
        goto destroy_connection;
    }

    broker_reply = amqp_login(s->conn, "/", 0, s->pkt_size, 0,
                              AMQP_SASL_METHOD_PLAIN, user_decoded, password_decoded);

    if (broker_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        av_log(h, AV_LOG_ERROR, "Error login\n");
        server_msg = AMQP_ACCESS_REFUSED;
        goto close_connection;
    }

    amqp_channel_open(s->conn, DEFAULT_CHANNEL);
    broker_reply = amqp_get_rpc_reply(s->conn);

    if (broker_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        av_log(h, AV_LOG_ERROR, "Error set channel\n");
        server_msg = AMQP_CHANNEL_ERROR;
        goto close_connection;
    }

    if (h->flags & AVIO_FLAG_READ) {
        amqp_bytes_t queuename;
        char queuename_buff[STR_LEN];
        amqp_queue_declare_ok_t *r;

        r = amqp_queue_declare(s->conn, DEFAULT_CHANNEL, amqp_empty_bytes,
                               0, 0, 0, 1, amqp_empty_table);
        broker_reply = amqp_get_rpc_reply(s->conn);
        if (!r || broker_reply.reply_type != AMQP_RESPONSE_NORMAL) {
            av_log(h, AV_LOG_ERROR, "Error declare queue\n");
            server_msg = AMQP_RESOURCE_ERROR;
            goto close_channel;
        }

        /* store queuename */
        queuename.bytes = queuename_buff;
        queuename.len = FFMIN(r->queue.len, STR_LEN);
        memcpy(queuename.bytes, r->queue.bytes, queuename.len);

        amqp_queue_bind(s->conn, DEFAULT_CHANNEL, queuename,
                        amqp_cstring_bytes(s->exchange),
                        amqp_cstring_bytes(s->routing_key), amqp_empty_table);

        broker_reply = amqp_get_rpc_reply(s->conn);
        if (broker_reply.reply_type != AMQP_RESPONSE_NORMAL) {
            av_log(h, AV_LOG_ERROR, "Queue bind error\n");
            server_msg = AMQP_INTERNAL_ERROR;
            goto close_channel;
        }

        amqp_basic_consume(s->conn, DEFAULT_CHANNEL, queuename, amqp_empty_bytes,
                           0, 1, 0, amqp_empty_table);

        broker_reply = amqp_get_rpc_reply(s->conn);
        if (broker_reply.reply_type != AMQP_RESPONSE_NORMAL) {
            av_log(h, AV_LOG_ERROR, "Set consume error\n");
            server_msg = AMQP_INTERNAL_ERROR;
            goto close_channel;
        }
    }

    av_freep(&user_decoded);
    av_freep(&password_decoded);
    return 0;

close_channel:
    amqp_channel_close(s->conn, DEFAULT_CHANNEL, server_msg);
close_connection:
    amqp_connection_close(s->conn, server_msg);
destroy_connection:
    amqp_destroy_connection(s->conn);

    av_freep(&user_decoded);
    av_freep(&password_decoded);
    return AVERROR_EXTERNAL;
}

static int amqp_proto_write(URLContext *h, const unsigned char *buf, int size)
{
    int ret;
    AMQPContext *s = h->priv_data;
    int fd = amqp_socket_get_sockfd(s->socket);

    amqp_bytes_t message = { size, (void *)buf };
    amqp_basic_properties_t props;

    ret = ff_network_wait_fd_timeout(fd, 1, h->rw_timeout, &h->interrupt_callback);
    if (ret)
        return ret;

    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("octet/stream");
    props.delivery_mode = s->delivery_mode;

    ret = amqp_basic_publish(s->conn, DEFAULT_CHANNEL, amqp_cstring_bytes(s->exchange),
                             amqp_cstring_bytes(s->routing_key), 0, 0,
                             &props, message);

    if (ret) {
        av_log(h, AV_LOG_ERROR, "Error publish: %s\n", amqp_error_string2(ret));
        return AVERROR_EXTERNAL;
    }

    return size;
}

static int amqp_proto_read(URLContext *h, unsigned char *buf, int size)
{
    AMQPContext *s = h->priv_data;
    int fd = amqp_socket_get_sockfd(s->socket);
    int ret;

    amqp_rpc_reply_t broker_reply;
    amqp_envelope_t envelope;

    ret = ff_network_wait_fd_timeout(fd, 0, h->rw_timeout, &h->interrupt_callback);
    if (ret)
        return ret;

    amqp_maybe_release_buffers(s->conn);
    broker_reply = amqp_consume_message(s->conn, &envelope, NULL, 0);

    if (broker_reply.reply_type != AMQP_RESPONSE_NORMAL)
        return AVERROR_EXTERNAL;

    if (envelope.message.body.len > size) {
        s->pkt_size_overflow = FFMAX(s->pkt_size_overflow, envelope.message.body.len);
        av_log(h, AV_LOG_WARNING, "Message exceeds space in the buffer. "
                                  "Message will be truncated. Setting -pkt_size %d "
                                  "may resolve this issue.\n", s->pkt_size_overflow);
    }
    size = FFMIN(size, envelope.message.body.len);

    memcpy(buf, envelope.message.body.bytes, size);
    amqp_destroy_envelope(&envelope);

    return size;
}

static int amqp_proto_close(URLContext *h)
{
    AMQPContext *s = h->priv_data;
    amqp_channel_close(s->conn, DEFAULT_CHANNEL, AMQP_REPLY_SUCCESS);
    amqp_connection_close(s->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(s->conn);

    return 0;
}

static const AVClass amqp_context_class = {
    .class_name = "amqp",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libamqp_protocol = {
    .name            = "amqp",
    .url_close       = amqp_proto_close,
    .url_open        = amqp_proto_open,
    .url_read        = amqp_proto_read,
    .url_write       = amqp_proto_write,
    .priv_data_size  = sizeof(AMQPContext),
    .priv_data_class = &amqp_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
};
