/*
 * TLS/SSL Protocol
 * Copyright (c) 2011 Martin Storsjo
 * Copyright (c) 2017 sfan5 <sfan5@live.de>
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

#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "url.h"
#include "tls.h"
#include "libavcodec/internal.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"

#include <tls.h>

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;
    struct tls *ctx;
} TLSContext;

static int ff_tls_close(URLContext *h)
{
    TLSContext *p = h->priv_data;
    if (p->ctx) {
        tls_close(p->ctx);
        tls_free(p->ctx);
    }
    ffurl_closep(&p->tls_shared.tcp);
    return 0;
}

static ssize_t tls_read_callback(struct tls *ctx, void *buf, size_t buflen, void *cb_arg)
{
    URLContext *h = (URLContext*) cb_arg;
    int ret = ffurl_read(h, buf, buflen);
    if (ret == AVERROR(EAGAIN))
        return TLS_WANT_POLLIN;
    else if (ret == AVERROR_EXIT)
        return 0;
    return ret >= 0 ? ret : -1;
}

static ssize_t tls_write_callback(struct tls *ctx, const void *buf, size_t buflen, void *cb_arg)
{
    URLContext *h = (URLContext*) cb_arg;
    int ret = ffurl_write(h, buf, buflen);
    if (ret == AVERROR(EAGAIN))
        return TLS_WANT_POLLOUT;
    else if (ret == AVERROR_EXIT)
        return 0;
    return ret >= 0 ? ret : -1;
}

static int ff_tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    struct tls_config *cfg = NULL;
    int ret;

    if (tls_init() == -1) {
        ret = AVERROR(EIO);
        goto fail;
    }

    if ((ret = ff_tls_open_underlying(c, h, uri, options)) < 0)
        goto fail;

    p->ctx = !c->listen ? tls_client() : tls_server();
    if (!p->ctx) {
        ret = AVERROR(EIO);
        goto fail;
    }

    cfg = tls_config_new();
    if (!p->ctx) {
        ret = AVERROR(EIO);
        goto fail;
    }
    if (tls_config_set_protocols(cfg, TLS_PROTOCOLS_ALL) == -1)
        goto err_config;
    // While TLSv1.0 and TLSv1.1 are already enabled by the above,
    // we need to be less strict with ciphers so it works in practice.
    if (tls_config_set_ciphers(cfg, "compat") == -1)
        goto err_config;
    if (c->ca_file && tls_config_set_ca_file(cfg, c->ca_file) == -1)
        goto err_config;
    if (c->cert_file && tls_config_set_cert_file(cfg, c->cert_file) == -1)
        goto err_config;
    if (c->key_file && tls_config_set_key_file(cfg, c->key_file) == -1)
        goto err_config;
    if (!c->verify) {
        tls_config_insecure_noverifycert(cfg);
        tls_config_insecure_noverifyname(cfg);
        tls_config_insecure_noverifytime(cfg);
    }
    if (tls_configure(p->ctx, cfg) == -1)
        goto err_ctx;

    if (!c->listen) {
        ret = tls_connect_cbs(p->ctx, tls_read_callback, tls_write_callback,
            c->tcp, c->host);
    } else {
        struct tls *ctx_new;
        ret = tls_accept_cbs(p->ctx, &ctx_new, tls_read_callback,
            tls_write_callback, c->tcp);
        if (ret == 0) {
            // free "server" context and replace by "connection" context
            tls_free(p->ctx);
            p->ctx = ctx_new;
        }
    }
    if (ret == -1)
        goto err_ctx;

    tls_config_free(cfg);
    return 0;
err_config:
    av_log(h, AV_LOG_ERROR, "%s\n", tls_config_error(cfg));
    ret = AVERROR(EIO);
    goto fail;
err_ctx:
    av_log(h, AV_LOG_ERROR, "%s\n", tls_error(p->ctx));
    ret = AVERROR(EIO);
    /* fallthrough */
fail:
    if (cfg)
        tls_config_free(cfg);
    ff_tls_close(h);
    return ret;
}

static int ff_tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *p = h->priv_data;
    ssize_t ret;
    ret = tls_read(p->ctx, buf, size);
    if (ret > 0)
        return ret;
    else if (ret == 0)
        return AVERROR_EOF;
    av_log(h, AV_LOG_ERROR, "%s\n", tls_error(p->ctx));
    return AVERROR(EIO);
}

static int ff_tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *p = h->priv_data;
    ssize_t ret;
    ret = tls_write(p->ctx, buf, size);
    if (ret > 0)
        return ret;
    else if (ret == 0)
        return AVERROR_EOF;
    av_log(h, AV_LOG_ERROR, "%s\n", tls_error(p->ctx));
    return AVERROR(EIO);
}

static int tls_get_file_handle(URLContext *h)
{
    TLSContext *c = h->priv_data;
    return ffurl_get_file_handle(c->tls_shared.tcp);
}

static int tls_get_short_seek(URLContext *h)
{
    TLSContext *s = h->priv_data;
    return ffurl_get_short_seek(s->tls_shared.tcp);
}

static const AVOption options[] = {
    TLS_COMMON_OPTIONS(TLSContext, tls_shared),
    { NULL }
};

static const AVClass tls_class = {
    .class_name = "tls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_tls_protocol = {
    .name           = "tls",
    .url_open2      = ff_tls_open,
    .url_read       = ff_tls_read,
    .url_write      = ff_tls_write,
    .url_close      = ff_tls_close,
    .url_get_file_handle = tls_get_file_handle,
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
