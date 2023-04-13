/*
 * Concat URL protocol
 * Copyright (c) 2006 Steve Lhomme
 * Copyright (c) 2007 Wolfram Gloger
 * Copyright (c) 2010 Michele Orrù
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

#include "config_components.h"

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"

#include "avformat.h"
#include "avio_internal.h"
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
    uint64_t total_size;
};

static av_cold int concat_close(URLContext *h)
{
    int err = 0;
    size_t i;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;

    for (i = 0; i != data->length; i++)
        err |= ffurl_closep(&nodes[i].uc);

    av_freep(&data->nodes);

    return err < 0 ? -1 : 0;
}

#if CONFIG_CONCAT_PROTOCOL
static av_cold int concat_open(URLContext *h, const char *uri, int flags)
{
    char *node_uri = NULL;
    int err = 0;
    int64_t size, total_size = 0;
    size_t len, i;
    URLContext *uc;
    struct concat_data  *data = h->priv_data;
    struct concat_nodes *nodes;

    if (!av_strstart(uri, "concat:", &uri)) {
        av_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
        return AVERROR(EINVAL);
    }

    for (i = 0, len = 1; uri[i]; i++) {
        if (uri[i] == *AV_CAT_SEPARATOR) {
            len++;
        }
    }

    if (!(nodes = av_realloc_array(NULL, len, sizeof(*nodes))))
        return AVERROR(ENOMEM);
    else
        data->nodes = nodes;

    /* handle input */
    if (!*uri)
        err = AVERROR(ENOENT);
    for (i = 0; *uri; i++) {
        /* parsing uri */
        len = strcspn(uri, AV_CAT_SEPARATOR);
        if ((err = av_reallocp(&node_uri, len + 1)) < 0)
            break;
        av_strlcpy(node_uri, uri, len + 1);
        uri += len + strspn(uri + len, AV_CAT_SEPARATOR);

        /* creating URLContext */
        err = ffurl_open_whitelist(&uc, node_uri, flags,
                                   &h->interrupt_callback, NULL, h->protocol_whitelist, h->protocol_blacklist, h);
        if (err < 0)
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
        total_size += size;
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
    data->total_size = total_size;
    return err;
}
#endif

static int concat_read(URLContext *h, unsigned char *buf, int size)
{
    int result, total = 0;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i                   = data->current;

    while (size > 0) {
        result = ffurl_read(nodes[i].uc, buf, size);
        if (result == AVERROR_EOF) {
            if (i + 1 == data->length ||
                ffurl_seek(nodes[++i].uc, 0, SEEK_SET) < 0)
                break;
            result = 0;
        }
        if (result < 0)
            return total ? total : result;
        total += result;
        buf   += result;
        size  -= result;
    }
    data->current = i;
    return total ? total : result;
}

static int64_t concat_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t result;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i;

    if ((whence & AVSEEK_SIZE))
        return data->total_size;
    switch (whence) {
    case SEEK_END:
        for (i = data->length - 1; i && pos < -nodes[i].size; i--)
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

#if CONFIG_CONCAT_PROTOCOL
const URLProtocol ff_concat_protocol = {
    .name           = "concat",
    .url_open       = concat_open,
    .url_read       = concat_read,
    .url_seek       = concat_seek,
    .url_close      = concat_close,
    .priv_data_size = sizeof(struct concat_data),
    .default_whitelist = "concat,file,subfile",
};
#endif

#if CONFIG_CONCATF_PROTOCOL
static av_cold int concatf_open(URLContext *h, const char *uri, int flags)
{
    AVBPrint bp;
    struct concat_data *data = h->priv_data;
    AVIOContext *in = NULL;
    const char *cursor;
    int64_t total_size = 0;
    unsigned int nodes_size = 0;
    size_t i = 0;
    int err;

    if (!av_strstart(uri, "concatf:", &uri)) {
        av_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
        return AVERROR(EINVAL);
    }

    /* handle input */
    if (!*uri)
        return AVERROR(ENOENT);

    err = ffio_open_whitelist(&in, uri, AVIO_FLAG_READ, &h->interrupt_callback,
                              NULL, h->protocol_whitelist, h->protocol_blacklist);
    if (err < 0)
        return err;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    err = avio_read_to_bprint(in, &bp, SIZE_MAX);
    avio_closep(&in);
    if (err < 0) {
        av_bprint_finalize(&bp, NULL);
        return err;
    }

    cursor = bp.str;
    while (*cursor) {
        struct concat_nodes *nodes;
        URLContext *uc;
        char *node_uri;
        int64_t size;
        size_t len = i;
        int leading_spaces = strspn(cursor, " \n\t\r");

        if (!cursor[leading_spaces])
            break;

        node_uri = av_get_token(&cursor, "\r\n");
        if (!node_uri) {
            err = AVERROR(ENOMEM);
            break;
        }
        if (*cursor)
            cursor++;

        if (++len == SIZE_MAX / sizeof(*nodes)) {
            av_free(node_uri);
            err = AVERROR(ENAMETOOLONG);
            break;
        }

        /* creating URLContext */
        err = ffurl_open_whitelist(&uc, node_uri, flags,
                                   &h->interrupt_callback, NULL, h->protocol_whitelist, h->protocol_blacklist, h);
        av_free(node_uri);
        if (err < 0)
            break;

        /* creating size */
        if ((size = ffurl_size(uc)) < 0) {
            ffurl_close(uc);
            err = AVERROR(ENOSYS);
            break;
        }

        nodes = av_fast_realloc(data->nodes, &nodes_size, sizeof(*nodes) * len);
        if (!nodes) {
            ffurl_close(uc);
            err = AVERROR(ENOMEM);
            break;
        }
        data->nodes = nodes;

        /* assembling */
        data->nodes[i].uc   = uc;
        data->nodes[i++].size = size;
        total_size += size;
    }
    av_bprint_finalize(&bp, NULL);
    data->length = i;

    if (!data->length)
        err = AVERROR_INVALIDDATA;
    if (err < 0)
        concat_close(h);

    data->total_size = total_size;
    return err;
}

const URLProtocol ff_concatf_protocol = {
    .name           = "concatf",
    .url_open       = concatf_open,
    .url_read       = concat_read,
    .url_seek       = concat_seek,
    .url_close      = concat_close,
    .priv_data_size = sizeof(struct concat_data),
    .default_whitelist = "concatf,concat,file,subfile",
};
#endif
