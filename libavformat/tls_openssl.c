/*
 * TLS/SSL Protocol
 * Copyright (c) 2011 Martin Storsjo
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
#include "os_support.h"
#include "url.h"
#include "tls.h"
#include "libavcodec/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/thread.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

static int openssl_init;

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;
    SSL_CTX *ctx;
    SSL *ssl;
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    BIO_METHOD* url_bio_method;
#endif
} TLSContext;

#if HAVE_THREADS && OPENSSL_VERSION_NUMBER < 0x10100000L
#include <openssl/crypto.h>
pthread_mutex_t *openssl_mutexes;
static void openssl_lock(int mode, int type, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        pthread_mutex_lock(&openssl_mutexes[type]);
    else
        pthread_mutex_unlock(&openssl_mutexes[type]);
}
#if !defined(WIN32) && OPENSSL_VERSION_NUMBER < 0x10000000
static unsigned long openssl_thread_id(void)
{
    return (intptr_t) pthread_self();
}
#endif
#endif

int ff_openssl_init(void)
{
    ff_lock_avformat();
    if (!openssl_init) {
        /* OpenSSL 1.0.2 or below, then you would use SSL_library_init. If you are
         * using OpenSSL 1.1.0 or above, then the library will initialize
         * itself automatically.
         * https://wiki.openssl.org/index.php/Library_Initialization
         */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_library_init();
        SSL_load_error_strings();
#endif
#if HAVE_THREADS && OPENSSL_VERSION_NUMBER < 0x10100000L
        if (!CRYPTO_get_locking_callback()) {
            int i;
            openssl_mutexes = av_malloc_array(sizeof(pthread_mutex_t), CRYPTO_num_locks());
            if (!openssl_mutexes) {
                ff_unlock_avformat();
                return AVERROR(ENOMEM);
            }

            for (i = 0; i < CRYPTO_num_locks(); i++)
                pthread_mutex_init(&openssl_mutexes[i], NULL);
            CRYPTO_set_locking_callback(openssl_lock);
#if !defined(WIN32) && OPENSSL_VERSION_NUMBER < 0x10000000
            CRYPTO_set_id_callback(openssl_thread_id);
#endif
        }
#endif
    }
    openssl_init++;
    ff_unlock_avformat();

    return 0;
}

void ff_openssl_deinit(void)
{
    ff_lock_avformat();
    openssl_init--;
    if (!openssl_init) {
#if HAVE_THREADS && OPENSSL_VERSION_NUMBER < 0x10100000L
        if (CRYPTO_get_locking_callback() == openssl_lock) {
            int i;
            CRYPTO_set_locking_callback(NULL);
            for (i = 0; i < CRYPTO_num_locks(); i++)
                pthread_mutex_destroy(&openssl_mutexes[i]);
            av_free(openssl_mutexes);
        }
#endif
    }
    ff_unlock_avformat();
}

static int print_tls_error(URLContext *h, int ret)
{
    TLSContext *c = h->priv_data;
    if (h->flags & AVIO_FLAG_NONBLOCK) {
        int err = SSL_get_error(c->ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            return AVERROR(EAGAIN);
    }
    av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
    return AVERROR(EIO);
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->ctx)
        SSL_CTX_free(c->ctx);
    ffurl_closep(&c->tls_shared.tcp);
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    if (c->url_bio_method)
        BIO_meth_free(c->url_bio_method);
#endif
    ff_openssl_deinit();
    return 0;
}

static int url_bio_create(BIO *b)
{
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    BIO_set_init(b, 1);
    BIO_set_data(b, NULL);
    BIO_set_flags(b, 0);
#else
    b->init = 1;
    b->ptr = NULL;
    b->flags = 0;
#endif
    return 1;
}

static int url_bio_destroy(BIO *b)
{
    return 1;
}

#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
#define GET_BIO_DATA(x) BIO_get_data(x)
#else
#define GET_BIO_DATA(x) (x)->ptr
#endif

static int url_bio_bread(BIO *b, char *buf, int len)
{
    URLContext *h = GET_BIO_DATA(b);
    int ret = ffurl_read(h, buf, len);
    if (ret >= 0)
        return ret;
    BIO_clear_retry_flags(b);
    if (ret == AVERROR(EAGAIN))
        BIO_set_retry_read(b);
    if (ret == AVERROR_EXIT)
        return 0;
    return -1;
}

static int url_bio_bwrite(BIO *b, const char *buf, int len)
{
    URLContext *h = GET_BIO_DATA(b);
    int ret = ffurl_write(h, buf, len);
    if (ret >= 0)
        return ret;
    BIO_clear_retry_flags(b);
    if (ret == AVERROR(EAGAIN))
        BIO_set_retry_write(b);
    if (ret == AVERROR_EXIT)
        return 0;
    return -1;
}

static long url_bio_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    if (cmd == BIO_CTRL_FLUSH) {
        BIO_clear_retry_flags(b);
        return 1;
    }
    return 0;
}

static int url_bio_bputs(BIO *b, const char *str)
{
    return url_bio_bwrite(b, str, strlen(str));
}

#if OPENSSL_VERSION_NUMBER < 0x1010000fL
static BIO_METHOD url_bio_method = {
    .type = BIO_TYPE_SOURCE_SINK,
    .name = "urlprotocol bio",
    .bwrite = url_bio_bwrite,
    .bread = url_bio_bread,
    .bputs = url_bio_bputs,
    .bgets = NULL,
    .ctrl = url_bio_ctrl,
    .create = url_bio_create,
    .destroy = url_bio_destroy,
};
#endif

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    BIO *bio;
    int ret;

    if ((ret = ff_openssl_init()) < 0)
        return ret;

    if ((ret = ff_tls_open_underlying(c, h, uri, options)) < 0)
        goto fail;

    // We want to support all versions of TLS >= 1.0, but not the deprecated
    // and insecure SSLv2 and SSLv3.  Despite the name, SSLv23_*_method()
    // enables support for all versions of SSL and TLS, and we then disable
    // support for the old protocols immediately after creating the context.
    p->ctx = SSL_CTX_new(c->listen ? SSLv23_server_method() : SSLv23_client_method());
    if (!p->ctx) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    SSL_CTX_set_options(p->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    if (c->ca_file) {
        if (!SSL_CTX_load_verify_locations(p->ctx, c->ca_file, NULL))
            av_log(h, AV_LOG_ERROR, "SSL_CTX_load_verify_locations %s\n", ERR_error_string(ERR_get_error(), NULL));
    }
    if (c->cert_file && !SSL_CTX_use_certificate_chain_file(p->ctx, c->cert_file)) {
        av_log(h, AV_LOG_ERROR, "Unable to load cert file %s: %s\n",
               c->cert_file, ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    if (c->key_file && !SSL_CTX_use_PrivateKey_file(p->ctx, c->key_file, SSL_FILETYPE_PEM)) {
        av_log(h, AV_LOG_ERROR, "Unable to load key file %s: %s\n",
               c->key_file, ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    // Note, this doesn't check that the peer certificate actually matches
    // the requested hostname.
    if (c->verify)
        SSL_CTX_set_verify(p->ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    p->ssl = SSL_new(p->ctx);
    if (!p->ssl) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    p->url_bio_method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "urlprotocol bio");
    BIO_meth_set_write(p->url_bio_method, url_bio_bwrite);
    BIO_meth_set_read(p->url_bio_method, url_bio_bread);
    BIO_meth_set_puts(p->url_bio_method, url_bio_bputs);
    BIO_meth_set_ctrl(p->url_bio_method, url_bio_ctrl);
    BIO_meth_set_create(p->url_bio_method, url_bio_create);
    BIO_meth_set_destroy(p->url_bio_method, url_bio_destroy);
    bio = BIO_new(p->url_bio_method);
    BIO_set_data(bio, c->tcp);
#else
    bio = BIO_new(&url_bio_method);
    bio->ptr = c->tcp;
#endif
    SSL_set_bio(p->ssl, bio, bio);
    if (!c->listen && !c->numerichost)
        SSL_set_tlsext_host_name(p->ssl, c->host);
    ret = c->listen ? SSL_accept(p->ssl) : SSL_connect(p->ssl);
    if (ret == 0) {
        av_log(h, AV_LOG_ERROR, "Unable to negotiate TLS/SSL session\n");
        ret = AVERROR(EIO);
        goto fail;
    } else if (ret < 0) {
        ret = print_tls_error(h, ret);
        goto fail;
    }

    return 0;
fail:
    tls_close(h);
    return ret;
}

static int tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    c->tls_shared.tcp->flags &= ~AVIO_FLAG_NONBLOCK;
    c->tls_shared.tcp->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = SSL_read(c->ssl, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_tls_error(h, ret);
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    c->tls_shared.tcp->flags &= ~AVIO_FLAG_NONBLOCK;
    c->tls_shared.tcp->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = SSL_write(c->ssl, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_tls_error(h, ret);
}

static int tls_get_file_handle(URLContext *h)
{
    TLSContext *c = h->priv_data;
    return ffurl_get_file_handle(c->tls_shared.tcp);
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
    .url_open2      = tls_open,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .url_get_file_handle = tls_get_file_handle,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
