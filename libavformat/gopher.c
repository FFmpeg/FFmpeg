/*
 * Gopher protocol
 *
 * Copyright (c) 2009 Toshimitsu Kimura
 *
 * based on libavformat/http.c, Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "network.h"

typedef struct {
    URLContext *hd;
} GopherContext;

static int gopher_write(URLContext *h, uint8_t *buf, int size)
{
    GopherContext *s = h->priv_data;
    return url_write(s->hd, buf, size);
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
            av_log(NULL, AV_LOG_WARNING,
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
        url_close(s->hd);
        s->hd = NULL;
    }
    av_freep(&h->priv_data);
    return 0;
}

static int gopher_open(URLContext *h, const char *uri, int flags)
{
    GopherContext *s;
    char hostname[1024], auth[1024], path[1024], buf[1024];
    int port, err;

    h->is_streamed = 1;

    s = av_malloc(sizeof(GopherContext));
    if (!s) {
        return AVERROR(ENOMEM);
    }
    h->priv_data = s;

    /* needed in any case to build the host string */
    url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
              path, sizeof(path), uri);

    if (port < 0)
        port = 70;

    snprintf(buf, sizeof(buf), "tcp://%s:%d", hostname, port);

    s->hd = NULL;
    err = url_open(&s->hd, buf, URL_RDWR);
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
    int len = url_read(s->hd, buf, size);
    return len;
}


URLProtocol gopher_protocol = {
    "gopher",
    gopher_open,
    gopher_read,
    gopher_write,
    NULL, /*seek*/
    gopher_close,
};
