/*
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>
#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "url.h"

typedef struct {
    const uint8_t *data;
    void *tofree;
    size_t size;
    size_t pos;
} DataContext;

static av_cold int data_open(URLContext *h, const char *uri, int flags)
{
    DataContext *dc = h->priv_data;
    const char *data, *opt, *next;
    char *ddata;
    int ret, base64 = 0;
    size_t in_size;

    /* data:content/type[;base64],payload */

    av_strstart(uri, "data:", &uri);
    data = strchr(uri, ',');
    if (!data) {
        av_log(h, AV_LOG_ERROR, "No ',' delimiter in URI\n");
        return AVERROR(EINVAL);
    }
    opt = uri;
    while (opt < data) {
        next = av_x_if_null(memchr(opt, ';', data - opt), data);
        if (opt == uri) {
            if (!memchr(opt, '/', next - opt)) { /* basic validity check */
                av_log(h, AV_LOG_ERROR, "Invalid content-type '%.*s'\n",
                       (int)(next - opt), opt);
                return AVERROR(EINVAL);
            }
            av_log(h, AV_LOG_VERBOSE, "Content-type: %.*s\n",
                   (int)(next - opt), opt);
        } else {
            if (!av_strncasecmp(opt, "base64", next - opt)) {
                base64 = 1;
            } else {
                av_log(h, AV_LOG_VERBOSE, "Ignoring option '%.*s'\n",
                       (int)(next - opt), opt);
            }
        }
        opt = next + 1;
    }

    data++;
    in_size = strlen(data);
    if (base64) {
        size_t out_size = 3 * (in_size / 4) + 1;

        if (out_size > INT_MAX || !(ddata = av_malloc(out_size)))
            return AVERROR(ENOMEM);
        if ((ret = av_base64_decode(ddata, data, out_size)) < 0) {
            av_free(ddata);
            av_log(h, AV_LOG_ERROR, "Invalid base64 in URI\n");
            return ret;
        }
        dc->data = dc->tofree = ddata;
        dc->size = ret;
    } else {
        dc->data = data;
        dc->size = in_size;
    }
    return 0;
}

static av_cold int data_close(URLContext *h)
{
    DataContext *dc = h->priv_data;

    av_freep(&dc->tofree);
    return 0;
}

static int data_read(URLContext *h, unsigned char *buf, int size)
{
    DataContext *dc = h->priv_data;

    if (dc->pos >= dc->size)
        return AVERROR_EOF;
    size = FFMIN(size, dc->size - dc->pos);
    memcpy(buf, dc->data + dc->pos, size);
    dc->pos += size;
    return size;
}

URLProtocol ff_data_protocol = {
    .name           = "data",
    .url_open       = data_open,
    .url_close      = data_close,
    .url_read       = data_read,
    .priv_data_size = sizeof(DataContext),
};
