/*
 * TLS/SSL Protocol
 * Copyright (c) 2018 Thomas Volkert
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

#include <mbedtls/version.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/debug.h>
#ifdef MBEDTLS_PSA_CRYPTO_C
#include <psa/crypto.h>
#endif

#include "avformat.h"
#include "internal.h"
#include "url.h"
#include "tls.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;
    mbedtls_ssl_context ssl_context;
    mbedtls_ssl_config ssl_config;
    mbedtls_entropy_context entropy_context;
    mbedtls_ctr_drbg_context ctr_drbg_context;
    mbedtls_x509_crt ca_cert;
    mbedtls_x509_crt own_cert;
    mbedtls_pk_context priv_key;
    char *priv_key_pw;
} TLSContext;

#define OFFSET(x) offsetof(TLSContext, x)

static int tls_close(URLContext *h)
{
    TLSContext *tls_ctx = h->priv_data;

    mbedtls_ssl_close_notify(&tls_ctx->ssl_context);
    mbedtls_pk_free(&tls_ctx->priv_key);
    mbedtls_x509_crt_free(&tls_ctx->ca_cert);
    mbedtls_x509_crt_free(&tls_ctx->own_cert);
    mbedtls_ssl_free(&tls_ctx->ssl_context);
    mbedtls_ssl_config_free(&tls_ctx->ssl_config);
    mbedtls_ctr_drbg_free(&tls_ctx->ctr_drbg_context);
    mbedtls_entropy_free(&tls_ctx->entropy_context);

    ffurl_closep(&tls_ctx->tls_shared.tcp);
    return 0;
}

static int handle_transport_error(URLContext *h, const char* func_name, int react_on_eagain, int ret)
{
    switch (ret) {
    case AVERROR(EAGAIN):
        return react_on_eagain;
    case AVERROR_EXIT:
        return 0;
    case AVERROR(EPIPE):
    case AVERROR(ECONNRESET):
        return MBEDTLS_ERR_NET_CONN_RESET;
    default:
        av_log(h, AV_LOG_ERROR, "%s returned 0x%x\n", func_name, ret);
        errno = EIO;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
}

static int mbedtls_send(void *ctx, const unsigned char *buf, size_t len)
{
    URLContext *h = (URLContext*) ctx;
    int ret = ffurl_write(h, buf, len);
    if (ret >= 0)
        return ret;

    if (h->max_packet_size && len > h->max_packet_size)
        return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;

    return handle_transport_error(h, "ffurl_write", MBEDTLS_ERR_SSL_WANT_WRITE, ret);
}

static int mbedtls_recv(void *ctx, unsigned char *buf, size_t len)
{
    URLContext *h = (URLContext*) ctx;
    int ret = ffurl_read(h, buf, len);
    if (ret >= 0)
        return ret;

    if (h->max_packet_size && len > h->max_packet_size)
        return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;

    return handle_transport_error(h, "ffurl_read", MBEDTLS_ERR_SSL_WANT_READ, ret);
}

static void mbedtls_debug(void *ctx, int lvl, const char *file, int line, const char *msg)
{
    URLContext *h = (URLContext*) ctx;
    int av_lvl = lvl >= 4 ? AV_LOG_TRACE : AV_LOG_DEBUG;
    av_log(h, av_lvl, "%s:%d: %s", av_basename(file), line, msg);
}

static void handle_pk_parse_error(URLContext *h, int ret)
{
    switch (ret) {
    case MBEDTLS_ERR_PK_FILE_IO_ERROR:
        av_log(h, AV_LOG_ERROR, "Read of key file failed. Is it actually there, are the access permissions correct?\n");
        break;
    case MBEDTLS_ERR_PK_PASSWORD_REQUIRED:
        av_log(h, AV_LOG_ERROR, "A password for the private key is missing.\n");
        break;
    case MBEDTLS_ERR_PK_PASSWORD_MISMATCH:
        av_log(h, AV_LOG_ERROR, "The given password for the private key is wrong.\n");
        break;
    default:
        av_log(h, AV_LOG_ERROR, "mbedtls_pk_parse_key returned -0x%x\n", -ret);
        break;
    }
}

static void handle_handshake_error(URLContext *h, int ret)
{
    switch (ret) {
#if MBEDTLS_VERSION_MAJOR < 3
    case MBEDTLS_ERR_SSL_NO_USABLE_CIPHERSUITE:
        av_log(h, AV_LOG_ERROR, "None of the common ciphersuites is usable. Was the local certificate correctly set?\n");
        break;
#else
    case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
        av_log(h, AV_LOG_ERROR, "TLS handshake failed.\n");
        break;
    case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
        av_log(h, AV_LOG_ERROR, "TLS protocol version mismatch.\n");
        break;
#endif
    case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
        av_log(h, AV_LOG_ERROR, "A fatal alert message was received from the peer, has the peer a correct certificate?\n");
        break;
    case MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED:
        av_log(h, AV_LOG_ERROR, "No CA chain is set, but required to operate. Was the CA correctly set?\n");
        break;
    case MBEDTLS_ERR_SSL_INTERNAL_ERROR:
        av_log(h, AV_LOG_ERROR, "Internal error encountered.\n");
        break;
    case MBEDTLS_ERR_NET_CONN_RESET:
        av_log(h, AV_LOG_ERROR, "TLS handshake was aborted by peer.\n");
        break;
    case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
        av_log(h, AV_LOG_ERROR, "Certificate verification failed.\n");
        break;
    default:
        av_log(h, AV_LOG_ERROR, "mbedtls_ssl_handshake returned -0x%x\n", -ret);
        break;
    }
}

static void parse_options(TLSContext *tls_ctxc, const char *uri)
{
    char buf[1024];
    const char *p = strchr(uri, '?');
    if (!p)
        return;

    if (!tls_ctxc->priv_key_pw && av_find_info_tag(buf, sizeof(buf), "key_password", p))
        tls_ctxc->priv_key_pw = av_strdup(buf);
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *tls_ctx = h->priv_data;
    TLSShared *shr = &tls_ctx->tls_shared;
    uint32_t verify_res_flags;
    int ret;

    // parse additional options
    parse_options(tls_ctx, uri);

    if ((ret = ff_tls_open_underlying(shr, h, uri, options)) < 0)
        goto fail;

#ifdef MBEDTLS_PSA_CRYPTO_C
    if ((ret = psa_crypto_init()) != PSA_SUCCESS) {
        av_log(h, AV_LOG_ERROR, "psa_crypto_init returned %d\n", ret);
        goto fail;
    }
#endif

    mbedtls_ssl_init(&tls_ctx->ssl_context);
    mbedtls_ssl_config_init(&tls_ctx->ssl_config);
    mbedtls_entropy_init(&tls_ctx->entropy_context);
    mbedtls_ctr_drbg_init(&tls_ctx->ctr_drbg_context);
    mbedtls_x509_crt_init(&tls_ctx->ca_cert);
    mbedtls_pk_init(&tls_ctx->priv_key);

    if (av_log_get_level() >= AV_LOG_DEBUG) {
        mbedtls_ssl_conf_dbg(&tls_ctx->ssl_config, mbedtls_debug, shr->tcp);
        /*
         * Note: we can't call mbedtls_debug_set_threshold() here because
         * it's global state. The user is thus expected to manage this.
         */
    }

    // load trusted CA
    if (shr->ca_file) {
        if ((ret = mbedtls_x509_crt_parse_file(&tls_ctx->ca_cert, shr->ca_file)) != 0) {
            av_log(h, AV_LOG_ERROR, "mbedtls_x509_crt_parse_file for CA cert returned %d\n", ret);
            goto fail;
        }
    }

    // load own certificate
    if (shr->cert_file) {
        if ((ret = mbedtls_x509_crt_parse_file(&tls_ctx->own_cert, shr->cert_file)) != 0) {
            av_log(h, AV_LOG_ERROR, "mbedtls_x509_crt_parse_file for own cert returned %d\n", ret);
            goto fail;
        }
    }

    // seed the random number generator
    if ((ret = mbedtls_ctr_drbg_seed(&tls_ctx->ctr_drbg_context,
                                     mbedtls_entropy_func,
                                     &tls_ctx->entropy_context,
                                     NULL, 0)) != 0) {
        av_log(h, AV_LOG_ERROR, "mbedtls_ctr_drbg_seed returned %d\n", ret);
        goto fail;
    }

    // load key file
    if (shr->key_file) {
        if ((ret = mbedtls_pk_parse_keyfile(&tls_ctx->priv_key,
                                            shr->key_file,
                                            tls_ctx->priv_key_pw
#if MBEDTLS_VERSION_MAJOR >= 3
                                            , mbedtls_ctr_drbg_random,
                                            &tls_ctx->ctr_drbg_context
#endif
                                            )) != 0) {
            handle_pk_parse_error(h, ret);
            goto fail;
        }
    }

    if ((ret = mbedtls_ssl_config_defaults(&tls_ctx->ssl_config,
                                           shr->listen ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        av_log(h, AV_LOG_ERROR, "mbedtls_ssl_config_defaults returned %d\n", ret);
        goto fail;
    }

#ifdef MBEDTLS_SSL_PROTO_TLS1_3
    // this version does not allow disabling certificate verification with TLSv1.3 (yes, really).
    if (mbedtls_version_get_number() == 0x03060000 && !shr->verify) {
        av_log(h, AV_LOG_INFO, "Forcing TLSv1.2 because certificate verification is disabled\n");
        mbedtls_ssl_conf_max_tls_version(&tls_ctx->ssl_config, MBEDTLS_SSL_VERSION_TLS1_2);
    }
#endif

    // not VERIFY_REQUIRED because we manually check after handshake
    mbedtls_ssl_conf_authmode(&tls_ctx->ssl_config,
                              shr->verify ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&tls_ctx->ssl_config, mbedtls_ctr_drbg_random, &tls_ctx->ctr_drbg_context);
    mbedtls_ssl_conf_ca_chain(&tls_ctx->ssl_config, &tls_ctx->ca_cert, NULL);

    // set own certificate and private key
    if ((ret = mbedtls_ssl_conf_own_cert(&tls_ctx->ssl_config, &tls_ctx->own_cert, &tls_ctx->priv_key)) != 0) {
        av_log(h, AV_LOG_ERROR, "mbedtls_ssl_conf_own_cert returned %d\n", ret);
        goto fail;
    }

    if ((ret = mbedtls_ssl_setup(&tls_ctx->ssl_context, &tls_ctx->ssl_config)) != 0) {
        av_log(h, AV_LOG_ERROR, "mbedtls_ssl_setup returned %d\n", ret);
        goto fail;
    }

    if (!shr->listen && !shr->numerichost) {
        if ((ret = mbedtls_ssl_set_hostname(&tls_ctx->ssl_context, shr->host)) != 0) {
            av_log(h, AV_LOG_ERROR, "mbedtls_ssl_set_hostname returned %d\n", ret);
            goto fail;
        }
    }

    // set I/O functions to use FFmpeg internal code for transport layer
    mbedtls_ssl_set_bio(&tls_ctx->ssl_context, shr->tcp, mbedtls_send, mbedtls_recv, NULL);

    // ssl handshake
    while ((ret = mbedtls_ssl_handshake(&tls_ctx->ssl_context)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            handle_handshake_error(h, ret);
            goto fail;
        }
    }

    if (shr->verify) {
        // check the result of the certificate verification
        if ((verify_res_flags = mbedtls_ssl_get_verify_result(&tls_ctx->ssl_context)) != 0) {
            av_log(h, AV_LOG_ERROR, "mbedtls_ssl_get_verify_result reported problems "\
                                    "with the certificate verification, returned flags: %u\n",
                                    verify_res_flags);
            if (verify_res_flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
                av_log(h, AV_LOG_ERROR, "The certificate is not correctly signed by the trusted CA.\n");
            goto fail;
        }
    }

    return 0;

fail:
    tls_close(h);
    return AVERROR(EIO);
}

static int handle_tls_error(URLContext *h, const char* func_name, int ret)
{
    switch (ret) {
    case MBEDTLS_ERR_SSL_WANT_READ:
    case MBEDTLS_ERR_SSL_WANT_WRITE:
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    case MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET:
#endif
        return AVERROR(EAGAIN);
    case MBEDTLS_ERR_NET_SEND_FAILED:
    case MBEDTLS_ERR_NET_RECV_FAILED:
        return AVERROR(EIO);
    case MBEDTLS_ERR_NET_CONN_RESET:
    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
        av_log(h, AV_LOG_WARNING, "%s reported connection reset by peer\n", func_name);
        return AVERROR_EOF;
    default:
        av_log(h, AV_LOG_ERROR, "%s returned -0x%x\n", func_name, -ret);
        return AVERROR(EIO);
    }
}

static int tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *tls_ctx = h->priv_data;
    int ret;

    tls_ctx->tls_shared.tcp->flags &= ~AVIO_FLAG_NONBLOCK;
    tls_ctx->tls_shared.tcp->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    if ((ret = mbedtls_ssl_read(&tls_ctx->ssl_context, buf, size)) > 0) {
        // return read length
        return ret;
    }

    return handle_tls_error(h, "mbedtls_ssl_read", ret);
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *tls_ctx = h->priv_data;
    int ret;

    tls_ctx->tls_shared.tcp->flags &= ~AVIO_FLAG_NONBLOCK;
    tls_ctx->tls_shared.tcp->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    if ((ret = mbedtls_ssl_write(&tls_ctx->ssl_context, buf, size)) > 0) {
        // return written length
        return ret;
    }

    return handle_tls_error(h, "mbedtls_ssl_write", ret);
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
    TLS_COMMON_OPTIONS(TLSContext, tls_shared), \
    {"key_password", "Password for the private key file", OFFSET(priv_key_pw),  AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
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
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
