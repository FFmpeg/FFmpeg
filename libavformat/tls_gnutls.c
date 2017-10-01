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

#include <errno.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include "tls.h"
#include "libavcodec/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#ifndef GNUTLS_VERSION_NUMBER
#define GNUTLS_VERSION_NUMBER LIBGNUTLS_VERSION_NUMBER
#endif

#if HAVE_THREADS && GNUTLS_VERSION_NUMBER <= 0x020b00
#include <gcrypt.h>
#include "libavutil/thread.h"
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;
    gnutls_session_t session;
    gnutls_certificate_credentials_t cred;
    int need_shutdown;
} TLSContext;

void ff_gnutls_init(void)
{
    avpriv_lock_avformat();
#if HAVE_THREADS && GNUTLS_VERSION_NUMBER < 0x020b00
    if (gcry_control(GCRYCTL_ANY_INITIALIZATION_P) == 0)
        gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
    gnutls_global_init();
    avpriv_unlock_avformat();
}

void ff_gnutls_deinit(void)
{
    avpriv_lock_avformat();
    gnutls_global_deinit();
    avpriv_unlock_avformat();
}

static int print_tls_error(URLContext *h, int ret)
{
    switch (ret) {
    case GNUTLS_E_AGAIN:
    case GNUTLS_E_INTERRUPTED:
#ifdef GNUTLS_E_PREMATURE_TERMINATION
    case GNUTLS_E_PREMATURE_TERMINATION:
#endif
        break;
    case GNUTLS_E_WARNING_ALERT_RECEIVED:
        av_log(h, AV_LOG_WARNING, "%s\n", gnutls_strerror(ret));
        break;
    default:
        av_log(h, AV_LOG_ERROR, "%s\n", gnutls_strerror(ret));
        break;
    }
    return AVERROR(EIO);
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;
    if (c->need_shutdown)
        gnutls_bye(c->session, GNUTLS_SHUT_WR);
    if (c->session)
        gnutls_deinit(c->session);
    if (c->cred)
        gnutls_certificate_free_credentials(c->cred);
    if (c->tls_shared.tcp)
        ffurl_close(c->tls_shared.tcp);
    ff_gnutls_deinit();
    return 0;
}

static ssize_t gnutls_url_pull(gnutls_transport_ptr_t transport,
                               void *buf, size_t len)
{
    URLContext *h = (URLContext*) transport;
    int ret = ffurl_read(h, buf, len);
    if (ret >= 0)
        return ret;
    if (ret == AVERROR_EXIT)
        return 0;
    errno = EIO;
    return -1;
}

static ssize_t gnutls_url_push(gnutls_transport_ptr_t transport,
                               const void *buf, size_t len)
{
    URLContext *h = (URLContext*) transport;
    int ret = ffurl_write(h, buf, len);
    if (ret >= 0)
        return ret;
    if (ret == AVERROR_EXIT)
        return 0;
    errno = EIO;
    return -1;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    int ret;

    ff_gnutls_init();

    if ((ret = ff_tls_open_underlying(c, h, uri, options)) < 0)
        goto fail;

    gnutls_init(&p->session, c->listen ? GNUTLS_SERVER : GNUTLS_CLIENT);
    if (!c->listen && !c->numerichost)
        gnutls_server_name_set(p->session, GNUTLS_NAME_DNS, c->host, strlen(c->host));
    gnutls_certificate_allocate_credentials(&p->cred);
    if (c->ca_file) {
        ret = gnutls_certificate_set_x509_trust_file(p->cred, c->ca_file, GNUTLS_X509_FMT_PEM);
        if (ret < 0)
            av_log(h, AV_LOG_ERROR, "%s\n", gnutls_strerror(ret));
    }
#if GNUTLS_VERSION_NUMBER >= 0x030020
    else
        gnutls_certificate_set_x509_system_trust(p->cred);
#endif
    gnutls_certificate_set_verify_flags(p->cred, c->verify ?
                                        GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT : 0);
    if (c->cert_file && c->key_file) {
        ret = gnutls_certificate_set_x509_key_file(p->cred,
                                                   c->cert_file, c->key_file,
                                                   GNUTLS_X509_FMT_PEM);
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR,
                   "Unable to set cert/key files %s and %s: %s\n",
                   c->cert_file, c->key_file, gnutls_strerror(ret));
            ret = AVERROR(EIO);
            goto fail;
        }
    } else if (c->cert_file || c->key_file)
        av_log(h, AV_LOG_ERROR, "cert and key required\n");
    gnutls_credentials_set(p->session, GNUTLS_CRD_CERTIFICATE, p->cred);
    gnutls_transport_set_pull_function(p->session, gnutls_url_pull);
    gnutls_transport_set_push_function(p->session, gnutls_url_push);
    gnutls_transport_set_ptr(p->session, c->tcp);
    gnutls_priority_set_direct(p->session, "NORMAL", NULL);
    ret = gnutls_handshake(p->session);
    if (ret) {
        ret = print_tls_error(h, ret);
        goto fail;
    }
    p->need_shutdown = 1;
    if (c->verify) {
        unsigned int status, cert_list_size;
        gnutls_x509_crt_t cert;
        const gnutls_datum_t *cert_list;
        if ((ret = gnutls_certificate_verify_peers2(p->session, &status)) < 0) {
            av_log(h, AV_LOG_ERROR, "Unable to verify peer certificate: %s\n",
                                    gnutls_strerror(ret));
            ret = AVERROR(EIO);
            goto fail;
        }
        if (status & GNUTLS_CERT_INVALID) {
            av_log(h, AV_LOG_ERROR, "Peer certificate failed verification\n");
            ret = AVERROR(EIO);
            goto fail;
        }
        if (gnutls_certificate_type_get(p->session) != GNUTLS_CRT_X509) {
            av_log(h, AV_LOG_ERROR, "Unsupported certificate type\n");
            ret = AVERROR(EIO);
            goto fail;
        }
        gnutls_x509_crt_init(&cert);
        cert_list = gnutls_certificate_get_peers(p->session, &cert_list_size);
        gnutls_x509_crt_import(cert, cert_list, GNUTLS_X509_FMT_DER);
        ret = gnutls_x509_crt_check_hostname(cert, c->host);
        gnutls_x509_crt_deinit(cert);
        if (!ret) {
            av_log(h, AV_LOG_ERROR,
                   "The certificate's owner does not match hostname %s\n", c->host);
            ret = AVERROR(EIO);
            goto fail;
        }
    }

    return 0;
fail:
    tls_close(h);
    return ret;
}

static int tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    int ret = gnutls_record_recv(c->session, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_tls_error(h, ret);
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    int ret = gnutls_record_send(c->session, buf, size);
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

const URLProtocol ff_tls_gnutls_protocol = {
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
