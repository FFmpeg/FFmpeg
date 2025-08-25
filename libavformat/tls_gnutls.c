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
#include <gnutls/dtls.h>
#include <gnutls/x509.h>

#include "avformat.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include "tls.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"

#ifndef GNUTLS_VERSION_NUMBER
#define GNUTLS_VERSION_NUMBER LIBGNUTLS_VERSION_NUMBER
#endif

#if HAVE_THREADS && GNUTLS_VERSION_NUMBER <= 0x020b00
#include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

typedef struct TLSContext {
    TLSShared tls_shared;
    gnutls_session_t session;
    gnutls_certificate_credentials_t cred;
    int need_shutdown;
    int io_err;
} TLSContext;

static AVMutex gnutls_mutex = AV_MUTEX_INITIALIZER;

void ff_gnutls_init(void)
{
    ff_mutex_lock(&gnutls_mutex);
#if HAVE_THREADS && GNUTLS_VERSION_NUMBER < 0x020b00
    if (gcry_control(GCRYCTL_ANY_INITIALIZATION_P) == 0)
        gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
    gnutls_global_init();
    ff_mutex_unlock(&gnutls_mutex);
}

void ff_gnutls_deinit(void)
{
    ff_mutex_lock(&gnutls_mutex);
    gnutls_global_deinit();
    ff_mutex_unlock(&gnutls_mutex);
}

static int print_tls_error(URLContext *h, int ret)
{
    TLSContext *c = h->priv_data;
    switch (ret) {
    case GNUTLS_E_AGAIN:
        return AVERROR(EAGAIN);
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
    if (c->io_err) {
        av_log(h, AV_LOG_ERROR, "IO error: %s\n", av_err2str(c->io_err));
        ret = c->io_err;
        c->io_err = 0;
        return ret;
    }
    return AVERROR(EIO);
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    if (c->need_shutdown)
        gnutls_bye(c->session, GNUTLS_SHUT_WR);
    if (c->session)
        gnutls_deinit(c->session);
    if (c->cred)
        gnutls_certificate_free_credentials(c->cred);
    if (!s->external_sock)
        ffurl_closep(s->is_dtls ? &s->udp : &s->tcp);
    ff_gnutls_deinit();
    return 0;
}

static ssize_t gnutls_url_pull(gnutls_transport_ptr_t transport,
                               void *buf, size_t len)
{
    TLSContext *c = (TLSContext*) transport;
    int ret = ffurl_read(c->tls_shared.tcp, buf, len);
    if (ret >= 0)
        return ret;
    if (ret == AVERROR_EXIT)
        return 0;
    if (ret == AVERROR(EAGAIN)) {
        errno = EAGAIN;
    } else {
        errno = EIO;
        c->io_err = ret;
    }
    return -1;
}

static ssize_t gnutls_url_push(gnutls_transport_ptr_t transport,
                               const void *buf, size_t len)
{
    TLSContext *c = (TLSContext*) transport;
    int ret = ffurl_write(c->tls_shared.tcp, buf, len);
    if (ret >= 0)
        return ret;
    if (ret == AVERROR_EXIT)
        return 0;
    if (ret == AVERROR(EAGAIN)) {
        errno = EAGAIN;
    } else {
        errno = EIO;
        c->io_err = ret;
    }
    return -1;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    uint16_t gnutls_flags = 0;
    int ret;
    av_assert0(s);

    ff_gnutls_init();

    if ((ret = ff_tls_open_underlying(s, h, uri, options)) < 0)
        goto fail;

    if (s->is_dtls)
        gnutls_flags |= GNUTLS_DATAGRAM;

    if (s->listen)
        gnutls_flags |= GNUTLS_SERVER;
    else
        gnutls_flags |= GNUTLS_CLIENT;
    gnutls_init(&c->session, gnutls_flags);
    if (!s->listen && !s->numerichost)
        gnutls_server_name_set(c->session, GNUTLS_NAME_DNS, s->host, strlen(s->host));
    gnutls_certificate_allocate_credentials(&c->cred);
    if (s->ca_file) {
        ret = gnutls_certificate_set_x509_trust_file(c->cred, s->ca_file, GNUTLS_X509_FMT_PEM);
        if (ret < 0)
            av_log(h, AV_LOG_ERROR, "%s\n", gnutls_strerror(ret));
    }
#if GNUTLS_VERSION_NUMBER >= 0x030020
    else
        gnutls_certificate_set_x509_system_trust(c->cred);
#endif
    gnutls_certificate_set_verify_flags(c->cred, s->verify ?
                                        GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT : 0);
    if (s->cert_file && s->key_file) {
        ret = gnutls_certificate_set_x509_key_file(c->cred,
                                                   s->cert_file, s->key_file,
                                                   GNUTLS_X509_FMT_PEM);
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR,
                   "Unable to set cert/key files %s and %s: %s\n",
                   s->cert_file, s->key_file, gnutls_strerror(ret));
            ret = AVERROR(EIO);
            goto fail;
        }
    } else if (s->cert_file || s->key_file)
        av_log(h, AV_LOG_ERROR, "cert and key required\n");
    gnutls_credentials_set(c->session, GNUTLS_CRD_CERTIFICATE, c->cred);
    gnutls_transport_set_pull_function(c->session, gnutls_url_pull);
    gnutls_transport_set_push_function(c->session, gnutls_url_push);
    gnutls_transport_set_ptr(c->session, c);
    if (s->is_dtls)
        if (s->mtu)
            gnutls_dtls_set_mtu(c->session, s->mtu);
    gnutls_set_default_priority(c->session);
    do {
        if (ff_check_interrupt(&h->interrupt_callback)) {
            ret = AVERROR_EXIT;
            goto fail;
        }

        ret = gnutls_handshake(c->session);
        if (gnutls_error_is_fatal(ret)) {
            ret = print_tls_error(h, ret);
            goto fail;
        }
    } while (ret);
    c->need_shutdown = 1;
    if (s->verify) {
        unsigned int status, cert_list_size;
        gnutls_x509_crt_t cert;
        const gnutls_datum_t *cert_list;
        if ((ret = gnutls_certificate_verify_peers2(c->session, &status)) < 0) {
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
        if (gnutls_certificate_type_get(c->session) != GNUTLS_CRT_X509) {
            av_log(h, AV_LOG_ERROR, "Unsupported certificate type\n");
            ret = AVERROR(EIO);
            goto fail;
        }
        gnutls_x509_crt_init(&cert);
        cert_list = gnutls_certificate_get_peers(c->session, &cert_list_size);
        gnutls_x509_crt_import(cert, cert_list, GNUTLS_X509_FMT_DER);
        ret = gnutls_x509_crt_check_hostname(cert, s->host);
        gnutls_x509_crt_deinit(cert);
        if (!ret) {
            av_log(h, AV_LOG_ERROR,
                   "The certificate's owner does not match hostname %s\n", s->host);
            ret = AVERROR(EIO);
            goto fail;
        }
    }

    return 0;
fail:
    tls_close(h);
    return ret;
}

static int dtls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    av_assert0(s);
    s->is_dtls = 1;
    return tls_open(h, uri, flags, options);
}

static int tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = gnutls_record_recv(c->session, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_tls_error(h, ret);
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = gnutls_record_send(c->session, buf, size);
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

static const AVClass dtls_class = {
    .class_name = "dtls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_dtls_protocol = {
    .name           = "dtls",
    .url_open2      = dtls_open,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .url_get_file_handle = tls_get_file_handle,
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &dtls_class,
};
