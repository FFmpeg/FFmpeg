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
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "libavutil/random_seed.h"

#ifndef GNUTLS_VERSION_NUMBER
#define GNUTLS_VERSION_NUMBER LIBGNUTLS_VERSION_NUMBER
#endif

#if HAVE_THREADS && GNUTLS_VERSION_NUMBER <= 0x020b00
#include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

#define MAX_MD_SIZE 64

static int pkey_to_pem_string(gnutls_x509_privkey_t key, char *out, size_t out_sz)
{
    size_t required_sz = out_sz - 1;
    int ret = 0;

    if (!out || !out_sz)
        return AVERROR(EINVAL);

    ret = gnutls_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, out, &required_sz);
    if (ret < 0) {
        if (ret == GNUTLS_E_SHORT_MEMORY_BUFFER)
            av_log(NULL, AV_LOG_ERROR,
                   "TLS: Buffer size %zu is not enough to store private key PEM (need %zu)\n",
                   out_sz, required_sz + 1);
        return AVERROR(EINVAL);
    }
    out[required_sz] = '\0';
    return required_sz;
}

static int crt_to_pem_string(gnutls_x509_crt_t crt, char *out, size_t out_sz)
{
    size_t required_sz = out_sz - 1;
    int ret = 0;

    if (!out || !out_sz)
        return AVERROR(EINVAL);

    ret = gnutls_x509_crt_export(crt, GNUTLS_X509_FMT_PEM, out, &required_sz);
    if (ret < 0) {
        if (ret == GNUTLS_E_SHORT_MEMORY_BUFFER)
            av_log(NULL, AV_LOG_ERROR,
                   "TLS: Buffer size %zu is not enough to store certificate PEM (need %zu)\n",
                   out_sz, required_sz + 1);
        return AVERROR(EINVAL);
    }
    out[required_sz] = '\0';
    return required_sz;
}

static int gnutls_x509_fingerprint(gnutls_x509_crt_t cert, char **fingerprint)
{
    unsigned char md[MAX_MD_SIZE];
    size_t n = sizeof(md);
    AVBPrint buf;
    int ret;

    ret = gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA256, md, &n);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate fingerprint, %s\n",
               gnutls_strerror(ret));
        return AVERROR(EINVAL);
    }

    av_bprint_init(&buf, n*3, n*3);

    for (int i = 0; i < n - 1; i++)
        av_bprintf(&buf, "%02X:", md[i]);
    av_bprintf(&buf, "%02X", md[n - 1]);

    return av_bprint_finalize(&buf, fingerprint);
}

int ff_ssl_read_key_cert(char *key_url, char *crt_url, char *key_buf, size_t key_sz, char *crt_buf, size_t crt_sz, char **fingerprint)
{
    int ret = 0;
    AVBPrint key_bp, crt_bp;
    gnutls_x509_crt_t crt = NULL;
    gnutls_x509_privkey_t key = NULL;
    gnutls_datum_t tmp;

    av_bprint_init(&key_bp, 1, MAX_CERTIFICATE_SIZE);
    av_bprint_init(&crt_bp, 1, MAX_CERTIFICATE_SIZE);

    ret = ff_url_read_all(key_url, &key_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to open key file %s\n", key_url);
        goto end;
    }

    ret = ff_url_read_all(crt_url, &crt_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to open certificate file %s\n", crt_url);
        goto end;
    }

    ret = gnutls_x509_privkey_init(&key);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to init private key: %s\n", gnutls_strerror(ret));
        goto end;
    }

    ret = gnutls_x509_crt_init(&crt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to init certificate: %s\n", gnutls_strerror(ret));
        goto end;
    }

    tmp.data = key_bp.str;
    tmp.size = key_bp.len;
    ret = gnutls_x509_privkey_import(key, &tmp, GNUTLS_X509_FMT_PEM);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to import private key: %s\n", gnutls_strerror(ret));
        goto end;
    }

    tmp.data = crt_bp.str;
    tmp.size = crt_bp.len;
    ret = gnutls_x509_crt_import(crt, &tmp, GNUTLS_X509_FMT_PEM);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to import certificate: %s\n", gnutls_strerror(ret));
        goto end;
    }

    ret = pkey_to_pem_string(key, key_buf, key_sz);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to converter private key to PEM string\n");
        goto end;
    }

    ret = crt_to_pem_string(crt, crt_buf, crt_sz);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to converter certificate to PEM string\n");
        goto end;
    }

    ret = gnutls_x509_fingerprint(crt, fingerprint);
    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate fingerprint\n");

end:
    av_bprint_finalize(&key_bp, NULL);
    av_bprint_finalize(&crt_bp, NULL);
    if (crt)
        gnutls_x509_crt_deinit(crt);
    if (key)
        gnutls_x509_privkey_deinit(key);
    return ret;
}

static int gnutls_gen_private_key(gnutls_x509_privkey_t *key)
{
    int ret = 0;

    ret = gnutls_x509_privkey_init(key);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to init private key: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_privkey_generate(*key, GNUTLS_PK_ECDSA,
                                       gnutls_sec_param_to_pk_bits(GNUTLS_PK_ECDSA, GNUTLS_SEC_PARAM_MEDIUM), 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate private key: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    goto end;
einval_end:
    ret = AVERROR(EINVAL);
    gnutls_x509_privkey_deinit(*key);
    *key = NULL;
end:
    return ret;
}

static int gnutls_gen_certificate(gnutls_x509_privkey_t key, gnutls_x509_crt_t *crt, char **fingerprint)
{
    int ret = 0;
    uint64_t serial;
    unsigned char buf[8];
    const char *dn = "CN=lavf";

    ret = gnutls_x509_crt_init(crt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to init certificate: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_version(*crt, 3);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate version: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    /**
     * See https://gnutls.org/manual/gnutls.html#gnutls_005fx509_005fcrt_005fset_005fserial-1
     * The provided serial should be a big-endian positive number (i.e. its leftmost bit should be zero).
     */
    serial = av_get_random_seed();
    AV_WB64(buf, serial);
    buf[0] &= 0x7F;
    ret = gnutls_x509_crt_set_serial(*crt, buf, sizeof(buf));
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate serial: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_activation_time(*crt, time(NULL));
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate activation time: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_expiration_time(*crt, time(NULL) + 365 * 24 * 60 * 60);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate expiration time: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_dn(*crt, dn, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate dn: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_issuer_dn(*crt, dn, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set certificate issuer dn: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_set_key(*crt, key);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set key: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_crt_sign2(*crt, *crt, key, GNUTLS_DIG_SHA256, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to sign certificate: %s\n", gnutls_strerror(ret));
        goto einval_end;
    }

    ret = gnutls_x509_fingerprint(*crt, fingerprint);
    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate fingerprint\n");

    goto end;
einval_end:
    ret = AVERROR(EINVAL);
    gnutls_x509_crt_deinit(*crt);
    *crt = NULL;
end:
    return ret;
}

int ff_ssl_gen_key_cert(char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    int ret;
    gnutls_x509_crt_t crt = NULL;
    gnutls_x509_privkey_t key = NULL;

    ret = gnutls_gen_private_key(&key);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate private key\n");
        goto end;
    }

    ret = gnutls_gen_certificate(key, &crt, fingerprint);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate certificate\n");
        goto end;
    }

    ret = pkey_to_pem_string(key, key_buf, key_sz);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to convert private key to PEM string\n");
        goto end;
    }

    ret = crt_to_pem_string(crt, cert_buf, cert_sz);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to convert certificate to PEM string\n");
        goto end;
    }
end:
    if (crt)
        gnutls_x509_crt_deinit(crt);
    if (key)
        gnutls_x509_privkey_deinit(key);
    return ret;
}

typedef struct TLSContext {
    TLSShared tls_shared;
    gnutls_session_t session;
    gnutls_certificate_credentials_t cred;
    int need_shutdown;
    int io_err;
    struct sockaddr_storage dest_addr;
    socklen_t dest_addr_len;
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

int ff_tls_set_external_socket(URLContext *h, URLContext *sock)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;

    if (s->is_dtls)
        s->udp = sock;
    else
        s->tcp = sock;

    return 0;
}

int ff_dtls_export_materials(URLContext *h, char *dtls_srtp_materials, size_t materials_sz)
{
    int ret = 0;
    TLSContext *c = h->priv_data;

    ret = gnutls_srtp_get_keys(c->session, dtls_srtp_materials, materials_sz, NULL, NULL, NULL, NULL);
    if (ret < 0) {
        av_log(c, AV_LOG_ERROR, "Failed to export SRTP material: %s\n", gnutls_strerror(ret));
        return -1;
    }
    return 0;
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
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret = ffurl_read(uc, buf, len);
    if (ret >= 0) {
        if (s->is_dtls && s->listen && !c->dest_addr_len) {
            int err_ret;

            ff_udp_get_last_recv_addr(s->udp, &c->dest_addr, &c->dest_addr_len);
            err_ret = ff_udp_set_remote_addr(s->udp, (struct sockaddr *)&c->dest_addr, c->dest_addr_len, 1);
            if (err_ret < 0) {
                av_log(c, AV_LOG_ERROR, "Failed connecting udp context\n");
                return err_ret;
            }
            av_log(c, AV_LOG_TRACE, "Set UDP remote addr on UDP socket, now 'connected'\n");
        }
        return ret;
    }
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
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret = ffurl_write(uc, buf, len);
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

static int gnutls_pull_timeout(gnutls_transport_ptr_t ptr, unsigned int ms)
{
    TLSContext *c = (TLSContext*) ptr;
    TLSShared *s = &c->tls_shared;
    int ret;
    int sockfd = ffurl_get_file_handle(s->udp);
    struct pollfd pfd = { .fd = sockfd, .events = POLLIN, .revents = 0 };

    if (sockfd < 0)
        return 0;

    ret = poll(&pfd, 1, ms);
    if (ret <= 0)
        return ret;
    return 1;
}

static int tls_handshake(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret;

    uc->flags &= ~AVIO_FLAG_NONBLOCK;

    do {
        if (ff_check_interrupt(&h->interrupt_callback)) {
            ret = AVERROR_EXIT;
            goto end;
        }

        ret = gnutls_handshake(c->session);
        if (gnutls_error_is_fatal(ret)) {
            ret = print_tls_error(h, ret);
            goto end;
        }
    } while (ret);

end:
    return ret;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    uint16_t gnutls_flags = 0;
    gnutls_x509_crt_t cert = NULL;
    gnutls_x509_privkey_t pkey = NULL;
    int ret;

    ff_gnutls_init();

    if (!s->external_sock) {
        if ((ret = ff_tls_open_underlying(s, h, uri, options)) < 0)
            goto fail;
    }

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

    if (s->listen && !s->cert_file && !s->cert_buf && !s->key_file && !s->key_buf) {
        av_log(h, AV_LOG_VERBOSE, "No server certificate provided, using self-signed\n");

        ret = gnutls_gen_private_key(&pkey);
        if (ret < 0)
            goto fail;

        ret = gnutls_gen_certificate(pkey, &cert, NULL);
        if (ret < 0)
            goto fail;

        ret = gnutls_certificate_set_x509_key(c->cred, &cert, 1, pkey);
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Unable to set self-signed certificate: %s\n", gnutls_strerror(ret));
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }
    gnutls_credentials_set(c->session, GNUTLS_CRD_CERTIFICATE, c->cred);
    gnutls_transport_set_pull_function(c->session, gnutls_url_pull);
    gnutls_transport_set_push_function(c->session, gnutls_url_push);
    gnutls_transport_set_ptr(c->session, c);
    if (s->is_dtls) {
        gnutls_transport_set_pull_timeout_function(c->session, gnutls_pull_timeout);
        if (s->mtu)
            gnutls_dtls_set_mtu(c->session, s->mtu);
    }
    gnutls_set_default_priority(c->session);
    if (!s->external_sock) {
        ret = tls_handshake(h);
        if (ret < 0)
            goto fail;
    }
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
    if (cert)
        gnutls_x509_crt_deinit(cert);
    if (pkey)
        gnutls_x509_privkey_deinit(pkey);
    tls_close(h);
    return ret;
}

static int dtls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
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
    .url_handshake  = tls_handshake,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .url_get_file_handle = tls_get_file_handle,
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &dtls_class,
};
