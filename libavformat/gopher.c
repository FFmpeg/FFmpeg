/*
 * Gopher protocol
 *
 * Copyright (c) 2009 Toshimitsu Kimura
 *
 * based on libavformat/http.c, Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "url.h"

typedef struct {
    URLContext *hd;
} GopherContext;

static int gopher_write(URLContext *h, const uint8_t *buf, int size)
{
    GopherContext *s = h->priv_data;
    return ffurl_write(s->hd, buf, size);
}

static int gopher_connect(URLContext *h, const char *path)
{
    char buffer[1024];

    if (!*path) return AVERROR(EINVAL);
    switch (*++path) {
        case '5':
        case '9':
            path = strchr(path, '/');
            if (!path) return AVERROR(EINVAL);
            break;
        default:
            av_log(h, AV_LOG_WARNING,
                   "Gopher protocol type '%c' not supported yet!\n",
                   *path);
            return AVERROR(EINVAL);
    }

    /* send gopher sector */
    snprintf(buffer, sizeof(buffer), "%s\r\n", path);

    if (gopher_write(h, buffer, strlen(buffer)) < 0)
        return AVERROR(EIO);

    return 0;
}

static int gopher_close(URLContext *h)
{
    GopherContext *s = h->priv_data;
    if (s->hd) {
        ffurl_close(s->hd);
        s->hd = NULL;
    }
    return 0;
}

static int gopher_open(URLContext *h, const char *uri, int flags)
{
    GopherContext *s = h->priv_data;
    char hostname[1024], auth[1024], path[1024], buf[1024];
    int port, err;

    h->is_streamed = 1;

    /* needed in any case to build the host string */
    av_url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                 path, sizeof(path), uri);

    if (port < 0)
        port = 70;

    ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port, NULL);

    s->hd = NULL;
    err = ffurl_open(&s->hd, buf, AVIO_FLAG_READ_WRITE,
                     &h->interrupt_callback, NULL);
    if (err < 0)
        goto fail;

    if ((err = gopher_connect(h, path)) < 0)
        goto fail;
    return 0;
 fail:
    gopher_close(h);
    return err;
}

static int gopher_read(URLContext *h, uint8_t *buf, int size)
{
    GopherContext *s = h->priv_data;
    int len = ffurl_read(s->hd, buf, size);
    return len;
}


URLProtocol ff_gopher_protocol = {
    .name           = "gopher",
    .url_open       = gopher_open,
    .url_read       = gopher_read,
    .url_write      = gopher_write,
    .url_close      = gopher_close,
    .priv_data_size = sizeof(GopherContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
};
