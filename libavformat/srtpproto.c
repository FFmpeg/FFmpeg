/*
 * SRTP network protocol
 * Copyright (c) 2012 Martin Storsjo
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

#include "libavutil/opt.h"
#include "avformat.h"
#include "url.h"

#include "rtpdec.h"
#include "srtp.h"

typedef struct SRTPProtoContext {
    const AVClass *class;
    URLContext *rtp_hd;
    const char *out_suite, *out_params;
    const char *in_suite, *in_params;
    struct SRTPContext srtp_out, srtp_in;
    uint8_t encryptbuf[RTP_MAX_PACKET_LENGTH];
} SRTPProtoContext;

#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "srtp_out_suite", "", offsetof(SRTPProtoContext, out_suite), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, E },
    { "srtp_out_params", "", offsetof(SRTPProtoContext, out_params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, E },
    { "srtp_in_suite", "", offsetof(SRTPProtoContext, in_suite), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D },
    { "srtp_in_params", "", offsetof(SRTPProtoContext, in_params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D },
    { NULL }
};

static const AVClass srtp_context_class = {
    .class_name     = "srtp",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int srtp_close(URLContext *h)
{
    SRTPProtoContext *s = h->priv_data;
    ff_srtp_free(&s->srtp_out);
    ff_srtp_free(&s->srtp_in);
    ffurl_closep(&s->rtp_hd);
    return 0;
}

static int srtp_open(URLContext *h, const char *uri, int flags)
{
    SRTPProtoContext *s = h->priv_data;
    char hostname[256], buf[1024], path[1024];
    int rtp_port, ret;

    if (s->out_suite && s->out_params)
        if ((ret = ff_srtp_set_crypto(&s->srtp_out, s->out_suite, s->out_params)) < 0)
            goto fail;
    if (s->in_suite && s->in_params)
        if ((ret = ff_srtp_set_crypto(&s->srtp_in, s->in_suite, s->in_params)) < 0)
            goto fail;

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &rtp_port,
                 path, sizeof(path), uri);
    ff_url_join(buf, sizeof(buf), "rtp", NULL, hostname, rtp_port, "%s", path);
    if ((ret = ffurl_open_whitelist(&s->rtp_hd, buf, flags, &h->interrupt_callback,
                                    NULL, h->protocol_whitelist, h->protocol_blacklist, h)) < 0)
        goto fail;

    h->max_packet_size = FFMIN(s->rtp_hd->max_packet_size,
                               sizeof(s->encryptbuf)) - 14;
    h->is_streamed = 1;
    return 0;

fail:
    srtp_close(h);
    return ret;
}

static int srtp_read(URLContext *h, uint8_t *buf, int size)
{
    SRTPProtoContext *s = h->priv_data;
    int ret;
start:
    ret = ffurl_read(s->rtp_hd, buf, size);
    if (ret > 0 && s->srtp_in.aes) {
        if (ff_srtp_decrypt(&s->srtp_in, buf, &ret) < 0)
            goto start;
    }
    return ret;
}

static int srtp_write(URLContext *h, const uint8_t *buf, int size)
{
    SRTPProtoContext *s = h->priv_data;
    if (!s->srtp_out.aes)
        return ffurl_write(s->rtp_hd, buf, size);
    size = ff_srtp_encrypt(&s->srtp_out, buf, size, s->encryptbuf,
                           sizeof(s->encryptbuf));
    if (size < 0)
        return size;
    return ffurl_write(s->rtp_hd, s->encryptbuf, size);
}

static int srtp_get_file_handle(URLContext *h)
{
    SRTPProtoContext *s = h->priv_data;
    return ffurl_get_file_handle(s->rtp_hd);
}

static int srtp_get_multi_file_handle(URLContext *h, int **handles,
                                      int *numhandles)
{
    SRTPProtoContext *s = h->priv_data;
    return ffurl_get_multi_file_handle(s->rtp_hd, handles, numhandles);
}

const URLProtocol ff_srtp_protocol = {
    .name                      = "srtp",
    .url_open                  = srtp_open,
    .url_read                  = srtp_read,
    .url_write                 = srtp_write,
    .url_close                 = srtp_close,
    .url_get_file_handle       = srtp_get_file_handle,
    .url_get_multi_file_handle = srtp_get_multi_file_handle,
    .priv_data_size            = sizeof(SRTPProtoContext),
    .priv_data_class           = &srtp_context_class,
    .flags                     = URL_PROTOCOL_FLAG_NETWORK,
};
