/*
 * Concat URL protocol
 * Copyright (c) 2006 Steve Lhomme
 * Copyright (c) 2007 Wolfram Gloger
 * Copyright (c) 2010 Michele Orrù
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

#include "avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "url.h"

#define AV_CAT_SEPARATOR "|"

struct concat_nodes {
    URLContext *uc;                ///< node's URLContext
    int64_t     size;              ///< url filesize
};

struct concat_data {
    struct concat_nodes *nodes;    ///< list of nodes to concat
    size_t               length;   ///< number of cat'ed nodes
    size_t               current;  ///< index of currently read node
};

static av_cold int concat_close(URLContext *h)
{
    int err = 0;
    size_t i;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;

    for (i = 0; i != data->length; i++)
        err |= ffurl_close(nodes[i].uc);

    av_freep(&data->nodes);

    return err < 0 ? -1 : 0;
}

static av_cold int concat_open(URLContext *h, const char *uri, int flags)
{
    char *node_uri = NULL, *tmp_uri;
    int err = 0;
    int64_t size;
    size_t  len, i;
    URLContext *uc;
    struct concat_data  *data = h->priv_data;
    struct concat_nodes *nodes;

    av_strstart(uri, "concat:", &uri);

    for (i = 0, len = 1; uri[i]; i++)
        if (uri[i] == *AV_CAT_SEPARATOR)
            /* integer overflow */
            if (++len == UINT_MAX / sizeof(*nodes)) {
                av_freep(&h->priv_data);
                return AVERROR(ENAMETOOLONG);
            }

    if (!(nodes = av_malloc(sizeof(*nodes) * len))) {
        return AVERROR(ENOMEM);
    } else
        data->nodes = nodes;

    /* handle input */
    if (!*uri)
        err = AVERROR(ENOENT);
    for (i = 0; *uri; i++) {
        /* parsing uri */
        len = strcspn(uri, AV_CAT_SEPARATOR);
        if (!(tmp_uri = av_realloc(node_uri, len+1))) {
            err = AVERROR(ENOMEM);
            break;
        } else
            node_uri = tmp_uri;
        av_strlcpy(node_uri, uri, len+1);
        uri += len + strspn(uri+len, AV_CAT_SEPARATOR);

        /* creating URLContext */
        if ((err = ffurl_open(&uc, node_uri, flags,
                              &h->interrupt_callback, NULL)) < 0)
            break;

        /* creating size */
        if ((size = ffurl_size(uc)) < 0) {
            ffurl_close(uc);
            err = AVERROR(ENOSYS);
            break;
        }

        /* assembling */
        nodes[i].uc   = uc;
        nodes[i].size = size;
    }
    av_free(node_uri);
    data->length = i;

    if (err < 0)
        concat_close(h);
    else if (!(nodes = av_realloc(nodes, data->length * sizeof(*nodes)))) {
        concat_close(h);
        err = AVERROR(ENOMEM);
    } else
        data->nodes = nodes;
    return err;
}

static int concat_read(URLContext *h, unsigned char *buf, int size)
{
    int result, total = 0;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i = data->current;

    while (size > 0) {
        result = ffurl_read(nodes[i].uc, buf, size);
        if (result < 0)
            return total ? total : result;
        if (!result)
            if (i + 1 == data->length ||
                ffurl_seek(nodes[++i].uc, 0, SEEK_SET) < 0)
                break;
        total += result;
        buf   += result;
        size  -= result;
    }
    data->current = i;
    return total;
}

static int64_t concat_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t result;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i;

    switch (whence) {
    case SEEK_END:
        for (i = data->length - 1;
             i && pos < -nodes[i].size;
             i--)
            pos += nodes[i].size;
        break;
    case SEEK_CUR:
        /* get the absolute position */
        for (i = 0; i != data->current; i++)
            pos += nodes[i].size;
        pos += ffurl_seek(nodes[i].uc, 0, SEEK_CUR);
        whence = SEEK_SET;
        /* fall through with the absolute position */
    case SEEK_SET:
        for (i = 0; i != data->length - 1 && pos >= nodes[i].size; i++)
            pos -= nodes[i].size;
        break;
    default:
        return AVERROR(EINVAL);
    }

    result = ffurl_seek(nodes[i].uc, pos, whence);
    if (result >= 0) {
        data->current = i;
        while (i)
            result += nodes[--i].size;
    }
    return result;
}

URLProtocol ff_concat_protocol = {
    .name           = "concat",
    .url_open       = concat_open,
    .url_read       = concat_read,
    .url_seek       = concat_seek,
    .url_close      = concat_close,
    .priv_data_size = sizeof(struct concat_data),
};
