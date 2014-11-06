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
#include "url.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "network.h"
#include "os_support.h"
#include "internal.h"
#if CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#define TLS_read(c, buf, size)  gnutls_record_recv(c->session, buf, size)
#define TLS_write(c, buf, size) gnutls_record_send(c->session, buf, size)
#define TLS_shutdown(c)         gnutls_bye(c->session, GNUTLS_SHUT_RDWR)
#define TLS_free(c) do { \
        if (c->session) \
            gnutls_deinit(c->session); \
        if (c->cred) \
            gnutls_certificate_free_credentials(c->cred); \
    } while (0)
#elif CONFIG_OPENSSL
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#define TLS_read(c, buf, size)  SSL_read(c->ssl,  buf, size)
#define TLS_write(c, buf, size) SSL_write(c->ssl, buf, size)
#define TLS_shutdown(c)         SSL_shutdown(c->ssl)
#define TLS_free(c) do { \
        if (c->ssl) \
            SSL_free(c->ssl); \
        if (c->ctx) \
            SSL_CTX_free(c->ctx); \
    } while (0)
#endif
#if HAVE_POLL_H
#include <poll.h>
#endif

typedef struct {
    const AVClass *class;
    URLContext *tcp;
#if CONFIG_GNUTLS
    gnutls_session_t session;
    gnutls_certificate_credentials_t cred;
#elif CONFIG_OPENSSL
    SSL_CTX *ctx;
    SSL *ssl;
#endif
    int fd;
    char *ca_file;
    int verify;
    char *cert_file;
    char *key_file;
    int listen;
} TLSContext;

#define OFFSET(x) offsetof(TLSContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"ca_file",    "Certificate Authority database file", OFFSET(ca_file),   AV_OPT_TYPE_STRING, .flags = D|E },
    {"cafile",     "Certificate Authority database file", OFFSET(ca_file),   AV_OPT_TYPE_STRING, .flags = D|E },
    {"tls_verify", "Verify the peer certificate",         OFFSET(verify),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = D|E },
    {"cert_file",  "Certificate file",                    OFFSET(cert_file), AV_OPT_TYPE_STRING, .flags = D|E },
    {"key_file",   "Private key file",                    OFFSET(key_file),  AV_OPT_TYPE_STRING, .flags = D|E },
    {"listen",     "Listen for incoming connections",     OFFSET(listen),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = D|E },
    { NULL }
};

static const AVClass tls_class = {
    .class_name = "tls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int do_tls_poll(URLContext *h, int ret)
{
    TLSContext *c = h->priv_data;
    struct pollfd p = { c->fd, 0, 0 };
#if CONFIG_GNUTLS
    switch (ret) {
    case GNUTLS_E_AGAIN:
    case GNUTLS_E_INTERRUPTED:
        break;
    case GNUTLS_E_WARNING_ALERT_RECEIVED:
        av_log(h, AV_LOG_WARNING, "%s\n", gnutls_strerror(ret));
        break;
    default:
        av_log(h, AV_LOG_ERROR, "%s\n", gnutls_strerror(ret));
        return AVERROR(EIO);
    }
    if (gnutls_record_get_direction(c->session))
        p.events = POLLOUT;
    else
        p.events = POLLIN;
#elif CONFIG_OPENSSL
    ret = SSL_get_error(c->ssl, ret);
    if (ret == SSL_ERROR_WANT_READ) {
        p.events = POLLIN;
    } else if (ret == SSL_ERROR_WANT_WRITE) {
        p.events = POLLOUT;
    } else {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        return AVERROR(EIO);
    }
#endif
    if (h->flags & AVIO_FLAG_NONBLOCK)
        return AVERROR(EAGAIN);
    while (1) {
        int n = poll(&p, 1, 100);
        if (n > 0)
            break;
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR(EINTR);
    }
    return 0;
}

static void set_options(URLContext *h, const char *uri)
{
    TLSContext *c = h->priv_data;
    char buf[1024];
    const char *p = strchr(uri, '?');
    if (!p)
        return;

    if (!c->ca_file && av_find_info_tag(buf, sizeof(buf), "cafile", p))
        c->ca_file = av_strdup(buf);

    if (!c->verify && av_find_info_tag(buf, sizeof(buf), "verify", p)) {
        char *endptr = NULL;
        c->verify = strtol(buf, &endptr, 10);
        if (buf == endptr)
            c->verify = 1;
    }

    if (!c->cert_file && av_find_info_tag(buf, sizeof(buf), "cert", p))
        c->cert_file = av_strdup(buf);

    if (!c->key_file && av_find_info_tag(buf, sizeof(buf), "key", p))
        c->key_file = av_strdup(buf);
}

static int tls_open(URLContext *h, const char *uri, int flags)
{
    TLSContext *c = h->priv_data;
    int ret;
    int port;
    const char *p;
    char buf[200], host[200], opts[50] = "";
    int numerichost = 0;
    struct addrinfo hints = { 0 }, *ai = NULL;
    const char *proxy_path;
    int use_proxy;

    ff_tls_init();

    if (c->listen)
        snprintf(opts, sizeof(opts), "?listen=1");

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port, NULL, 0, uri);

    p = strchr(uri, '?');

    if (!p) {
        p = opts;
    } else {
        if (av_find_info_tag(opts, sizeof(opts), "listen", p))
            c->listen = 1;
    }

    ff_url_join(buf, sizeof(buf), "tcp", NULL, host, port, "%s", p);

    hints.ai_flags = AI_NUMERICHOST;
    if (!getaddrinfo(host, NULL, &hints, &ai)) {
        numerichost = 1;
        freeaddrinfo(ai);
    }

    proxy_path = getenv("http_proxy");
    use_proxy = !ff_http_match_no_proxy(getenv("no_proxy"), host) &&
                proxy_path && av_strstart(proxy_path, "http://", NULL);

    if (use_proxy) {
        char proxy_host[200], proxy_auth[200], dest[200];
        int proxy_port;
        av_url_split(NULL, 0, proxy_auth, sizeof(proxy_auth),
                     proxy_host, sizeof(proxy_host), &proxy_port, NULL, 0,
                     proxy_path);
        ff_url_join(dest, sizeof(dest), NULL, NULL, host, port, NULL);
        ff_url_join(buf, sizeof(buf), "httpproxy", proxy_auth, proxy_host,
                    proxy_port, "/%s", dest);
    }

    ret = ffurl_open(&c->tcp, buf, AVIO_FLAG_READ_WRITE,
                     &h->interrupt_callback, NULL);
    if (ret)
        goto fail;
    c->fd = ffurl_get_file_handle(c->tcp);

#if CONFIG_GNUTLS
    gnutls_init(&c->session, c->listen ? GNUTLS_SERVER : GNUTLS_CLIENT);
    if (!c->listen && !numerichost)
        gnutls_server_name_set(c->session, GNUTLS_NAME_DNS, host, strlen(host));
    gnutls_certificate_allocate_credentials(&c->cred);
    set_options(h, uri);
    if (c->ca_file) {
        ret = gnutls_certificate_set_x509_trust_file(c->cred, c->ca_file, GNUTLS_X509_FMT_PEM);
        if (ret < 0)
            av_log(h, AV_LOG_ERROR, "%s\n", gnutls_strerror(ret));
    }
#if GNUTLS_VERSION_MAJOR >= 3
    else
        gnutls_certificate_set_x509_system_trust(c->cred);
#endif
    gnutls_certificate_set_verify_flags(c->cred, c->verify ?
                                        GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT : 0);
    if (c->cert_file && c->key_file) {
        ret = gnutls_certificate_set_x509_key_file(c->cred,
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
    gnutls_credentials_set(c->session, GNUTLS_CRD_CERTIFICATE, c->cred);
    gnutls_transport_set_ptr(c->session, (gnutls_transport_ptr_t)
                                         (intptr_t) c->fd);
    gnutls_priority_set_direct(c->session, "NORMAL", NULL);
    while (1) {
        ret = gnutls_handshake(c->session);
        if (ret == 0)
            break;
        if ((ret = do_tls_poll(h, ret)) < 0)
            goto fail;
    }
    if (c->verify) {
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
        ret = gnutls_x509_crt_check_hostname(cert, host);
        gnutls_x509_crt_deinit(cert);
        if (!ret) {
            av_log(h, AV_LOG_ERROR,
                   "The certificate's owner does not match hostname %s\n", host);
            ret = AVERROR(EIO);
            goto fail;
        }
    }
#elif CONFIG_OPENSSL
    c->ctx = SSL_CTX_new(c->listen ? TLSv1_server_method() : TLSv1_client_method());
    if (!c->ctx) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    set_options(h, uri);
    if (c->ca_file) {
        if (!SSL_CTX_load_verify_locations(c->ctx, c->ca_file, NULL))
            av_log(h, AV_LOG_ERROR, "SSL_CTX_load_verify_locations %s\n", ERR_error_string(ERR_get_error(), NULL));
    }
    if (c->cert_file && !SSL_CTX_use_certificate_chain_file(c->ctx, c->cert_file)) {
        av_log(h, AV_LOG_ERROR, "Unable to load cert file %s: %s\n",
               c->cert_file, ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    if (c->key_file && !SSL_CTX_use_PrivateKey_file(c->ctx, c->key_file, SSL_FILETYPE_PEM)) {
        av_log(h, AV_LOG_ERROR, "Unable to load key file %s: %s\n",
               c->key_file, ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    // Note, this doesn't check that the peer certificate actually matches
    // the requested hostname.
    if (c->verify)
        SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    c->ssl = SSL_new(c->ctx);
    if (!c->ssl) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    SSL_set_fd(c->ssl, c->fd);
    if (!c->listen && !numerichost)
        SSL_set_tlsext_host_name(c->ssl, host);
    while (1) {
        ret = c->listen ? SSL_accept(c->ssl) : SSL_connect(c->ssl);
        if (ret > 0)
            break;
        if (ret == 0) {
            av_log(h, AV_LOG_ERROR, "Unable to negotiate TLS/SSL session\n");
            ret = AVERROR(EIO);
            goto fail;
        }
        if ((ret = do_tls_poll(h, ret)) < 0)
            goto fail;
    }
#endif
    return 0;
fail:
    TLS_free(c);
    if (c->tcp)
        ffurl_close(c->tcp);
    ff_tls_deinit();
    return ret;
}

static int tls_read(URLContext *h, uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    while (1) {
        int ret = TLS_read(c, buf, size);
        if (ret > 0)
            return ret;
        if (ret == 0)
            return AVERROR_EOF;
        if ((ret = do_tls_poll(h, ret)) < 0)
            return ret;
    }
    return 0;
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    while (1) {
        int ret = TLS_write(c, buf, size);
        if (ret > 0)
            return ret;
        if (ret == 0)
            return AVERROR_EOF;
        if ((ret = do_tls_poll(h, ret)) < 0)
            return ret;
    }
    return 0;
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLS_shutdown(c);
    TLS_free(c);
    ffurl_close(c->tcp);
    ff_tls_deinit();
    return 0;
}

URLProtocol ff_tls_protocol = {
    .name           = "tls",
    .url_open       = tls_open,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
