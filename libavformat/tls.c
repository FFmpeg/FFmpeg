/*
 * TLS/SSL Protocol
 * Copyright (c) 2011 Martin Storsjo
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
#include "url.h"
#include "libavutil/avstring.h"
#if CONFIG_GNUTLS
#include <gnutls/gnutls.h>
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
#include "network.h"
#include "os_support.h"
#include "internal.h"
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
} TLSContext;

static int do_tls_poll(URLContext *h, int ret)
{
    TLSContext *c = h->priv_data;
    struct pollfd p = { c->fd, 0, 0 };
#if CONFIG_GNUTLS
    if (ret != GNUTLS_E_AGAIN && ret != GNUTLS_E_INTERRUPTED) {
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

static int tls_open(URLContext *h, const char *uri, int flags)
{
    TLSContext *c = h->priv_data;
    int ret;
    int port;
    char buf[200], host[200];
    int numerichost = 0;
    struct addrinfo hints = { 0 }, *ai = NULL;
    const char *proxy_path;
    int use_proxy;

    ff_tls_init();

    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") &&
        av_strstart(proxy_path, "http://", NULL);

    av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port, NULL, 0, uri);
    ff_url_join(buf, sizeof(buf), "tcp", NULL, host, port, NULL);

    hints.ai_flags = AI_NUMERICHOST;
    if (!getaddrinfo(host, NULL, &hints, &ai)) {
        numerichost = 1;
        freeaddrinfo(ai);
    }

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
    gnutls_init(&c->session, GNUTLS_CLIENT);
    if (!numerichost)
        gnutls_server_name_set(c->session, GNUTLS_NAME_DNS, host, strlen(host));
    gnutls_certificate_allocate_credentials(&c->cred);
    gnutls_certificate_set_verify_flags(c->cred, 0);
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
#elif CONFIG_OPENSSL
    c->ctx = SSL_CTX_new(TLSv1_client_method());
    if (!c->ctx) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    c->ssl = SSL_new(c->ctx);
    if (!c->ssl) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(ERR_get_error(), NULL));
        ret = AVERROR(EIO);
        goto fail;
    }
    SSL_set_fd(c->ssl, c->fd);
    if (!numerichost)
        SSL_set_tlsext_host_name(c->ssl, host);
    while (1) {
        ret = SSL_connect(c->ssl);
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
            return AVERROR(EIO);
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
            return AVERROR(EIO);
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
};
