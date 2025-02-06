/*
 * Unix socket protocol
 * Copyright (c) 2013 Luca Barbato
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

/**
 * @file
 *
 * Unix socket url_protocol
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "os_support.h"
#include "network.h"
#include <sys/un.h>
#include "url.h"

typedef struct UnixContext {
    const AVClass *class;
    struct sockaddr_un addr;
    int timeout;
    int listen;
    int type;
    int fd;
} UnixContext;

#define OFFSET(x) offsetof(UnixContext, x)
#define ED AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_ENCODING_PARAM
static const AVOption unix_options[] = {
    { "listen",    "Open socket for listening",             OFFSET(listen),  AV_OPT_TYPE_BOOL,  { .i64 = 0 },                    0,       1, ED },
    { "timeout",   "Timeout in ms",                         OFFSET(timeout), AV_OPT_TYPE_INT,   { .i64 = -1 },                  -1, INT_MAX, ED },
    { "type",      "Socket type",                           OFFSET(type),    AV_OPT_TYPE_INT,   { .i64 = SOCK_STREAM },    INT_MIN, INT_MAX, ED, .unit = "type" },
    { "stream",    "Stream (reliable stream-oriented)",     0,               AV_OPT_TYPE_CONST, { .i64 = SOCK_STREAM },    INT_MIN, INT_MAX, ED, .unit = "type" },
    { "datagram",  "Datagram (unreliable packet-oriented)", 0,               AV_OPT_TYPE_CONST, { .i64 = SOCK_DGRAM },     INT_MIN, INT_MAX, ED, .unit = "type" },
    { "seqpacket", "Seqpacket (reliable packet-oriented",   0,               AV_OPT_TYPE_CONST, { .i64 = SOCK_SEQPACKET }, INT_MIN, INT_MAX, ED, .unit = "type" },
    { NULL }
};

static const AVClass unix_class = {
    .class_name = "unix",
    .item_name  = av_default_item_name,
    .option     = unix_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int unix_open(URLContext *h, const char *filename, int flags)
{
    UnixContext *s = h->priv_data;
    int fd, ret;

    av_strstart(filename, "unix:", &filename);
    s->addr.sun_family = AF_UNIX;
    av_strlcpy(s->addr.sun_path, filename, sizeof(s->addr.sun_path));

    if ((fd = ff_socket(AF_UNIX, s->type, 0, h)) < 0)
        return ff_neterrno();

    if (s->timeout < 0 && h->rw_timeout)
        s->timeout = h->rw_timeout / 1000;

    if (s->listen) {
        ret = ff_listen_bind(fd, (struct sockaddr *)&s->addr,
                             sizeof(s->addr), s->timeout, h);
        if (ret < 0)
            goto fail;
        fd = ret;
    } else {
        ret = ff_listen_connect(fd, (struct sockaddr *)&s->addr,
                                sizeof(s->addr), s->timeout, h, 0);
        if (ret < 0)
            goto fail;
    }

    s->fd = fd;
    h->is_streamed = 1;

    return 0;

fail:
    if (s->listen && AVUNERROR(ret) != EADDRINUSE)
        unlink(s->addr.sun_path);
    if (fd >= 0)
        closesocket(fd);
    return ret;
}

static int unix_read(URLContext *h, uint8_t *buf, int size)
{
    UnixContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd(s->fd, 0);
        if (ret < 0)
            return ret;
    }
    ret = recv(s->fd, buf, size, 0);
    if (!ret && s->type == SOCK_STREAM)
        return AVERROR_EOF;
    return ret < 0 ? ff_neterrno() : ret;
}

static int unix_write(URLContext *h, const uint8_t *buf, int size)
{
    UnixContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = ff_network_wait_fd(s->fd, 1);
        if (ret < 0)
            return ret;
    }
    ret = send(s->fd, buf, size, MSG_NOSIGNAL);
    return ret < 0 ? ff_neterrno() : ret;
}

static int unix_close(URLContext *h)
{
    UnixContext *s = h->priv_data;
    if (s->listen)
        unlink(s->addr.sun_path);
    closesocket(s->fd);
    return 0;
}

static int unix_get_file_handle(URLContext *h)
{
    UnixContext *s = h->priv_data;
    return s->fd;
}

const URLProtocol ff_unix_protocol = {
    .name                = "unix",
    .url_open            = unix_open,
    .url_read            = unix_read,
    .url_write           = unix_write,
    .url_close           = unix_close,
    .url_get_file_handle = unix_get_file_handle,
    .priv_data_size      = sizeof(UnixContext),
    .priv_data_class     = &unix_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
