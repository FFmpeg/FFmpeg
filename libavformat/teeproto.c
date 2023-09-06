/*
 * Tee output protocol
 * Copyright (c) 2016 Michael Niedermayer
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

#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "tee_common.h"
#include "url.h"

typedef struct ChildContext {
    URLContext *url_context;
} ChildContext;

typedef struct TeeContext {
    int child_count;
    ChildContext *child;
} TeeContext;

static const char *const child_delim = "|";

static int tee_write(URLContext *h, const unsigned char *buf, int size)
{
    TeeContext *c = h->priv_data;
    int i;
    int main_ret = size;

    for (i=0; i<c->child_count; i++) {
        int ret = ffurl_write(c->child[i].url_context, buf, size);
        if (ret < 0)
            main_ret = ret;
    }
    return main_ret;
}

static int tee_close(URLContext *h)
{
    TeeContext *c = h->priv_data;
    int i;
    int main_ret = 0;

    for (i=0; i<c->child_count; i++) {
        int ret = ffurl_closep(&c->child[i].url_context);
        if (ret < 0)
            main_ret = ret;
    }

    av_freep(&c->child);
    c->child_count = 0;
    return main_ret;
}

static int tee_open(URLContext *h, const char *filename, int flags)
{
    TeeContext *c = h->priv_data;
    int ret, i;

    av_strstart(filename, "tee:", &filename);

    while (*filename) {
        char *child_string = av_get_token(&filename, child_delim);
        char *child_name = NULL;
        void *tmp;
        AVDictionary *options = NULL;
        if (!child_string) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        tmp = av_realloc_array(c->child, c->child_count + 1, sizeof(*c->child));
        if (!tmp) {
            ret = AVERROR(ENOMEM);
            goto loop_fail;
        }
        c->child = tmp;
        memset(&c->child[c->child_count], 0, sizeof(c->child[c->child_count]));

        ret = ff_tee_parse_slave_options(h, child_string, &options, &child_name);
        if (ret < 0)
            goto loop_fail;

        ret = ffurl_open_whitelist(&c->child[c->child_count].url_context, child_name, flags,
                                   &h->interrupt_callback, &options,
                                   h->protocol_whitelist, h->protocol_blacklist,
                                   h);
loop_fail:
        av_freep(&child_string);
        av_dict_free(&options);
        if (ret < 0)
            goto fail;
        c->child_count++;

        if (strspn(filename, child_delim))
            filename++;
    }

    h->is_streamed = 0;
    for (i=0; i<c->child_count; i++) {
        h->is_streamed |= c->child[i].url_context->is_streamed;
    }

    h->max_packet_size = 0;
    for (i = 0; i < c->child_count; i++) {
        int max = c->child[i].url_context->max_packet_size;
        if (!max)
            continue;

        if (!h->max_packet_size)
            h->max_packet_size = max;
        else if (h->max_packet_size > max)
            h->max_packet_size = max;
    }

    return 0;
fail:
    tee_close(h);
    return ret;
}
const URLProtocol ff_tee_protocol = {
    .name                = "tee",
    .url_open            = tee_open,
    .url_write           = tee_write,
    .url_close           = tee_close,
    .priv_data_size      = sizeof(TeeContext),
    .default_whitelist   = "crypto,file,http,https,httpproxy,rtmp,tcp,tls"
};
