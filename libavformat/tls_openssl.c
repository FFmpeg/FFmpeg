/*
 * TLS/DTLS/SSL Protocol
 * Copyright (c) 2011 Martin Storsjo
 * Copyright (c) 2025 Jack Lau
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

#include "libavutil/mem.h"
#include "network.h"
#include "os_support.h"
#include "libavutil/random_seed.h"
#include "url.h"
#include "tls.h"
#include "libavutil/opt.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/**
 * Returns a heap‐allocated null‐terminated string containing
 * the PEM‐encoded public key.  Caller must free.
 */
static char *pkey_to_pem_string(EVP_PKEY *pkey) {
    BIO        *mem = NULL;
    BUF_MEM    *bptr = NULL;
    char       *pem_str = NULL;

    // Create a memory BIO
    if (!(mem = BIO_new(BIO_s_mem())))
        goto err;

    // Write public key in PEM form
    if (!PEM_write_bio_PrivateKey(mem, pkey, NULL, NULL, 0, NULL, NULL))
        goto err;

    // Extract pointer/length
    BIO_get_mem_ptr(mem, &bptr);
    if (!bptr || !bptr->length)
        goto err;

    // Allocate string (+1 for NUL)
    pem_str = av_malloc(bptr->length + 1);
    if (!pem_str)
        goto err;

    // Copy data & NUL‐terminate
    memcpy(pem_str, bptr->data, bptr->length);
    pem_str[bptr->length] = '\0';

cleanup:
    BIO_free(mem);
    return pem_str;

err:
    // error path: free and return NULL
    free(pem_str);
    pem_str = NULL;
    goto cleanup;
}

/**
 * Serialize an X509 certificate to a av_malloc’d PEM string.
 * Caller must free the returned pointer.
 */
static char *cert_to_pem_string(X509 *cert)
{
    BIO     *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr = NULL;
    char    *out = NULL;

    if (!mem) goto err;

    /* Write the PEM certificate */
    if (!PEM_write_bio_X509(mem, cert))
        goto err;

    BIO_get_mem_ptr(mem, &bptr);
    if (!bptr || !bptr->length) goto err;

    out = av_malloc(bptr->length + 1);
    if (!out) goto err;

    memcpy(out, bptr->data, bptr->length);
    out[bptr->length] = '\0';

cleanup:
    BIO_free(mem);
    return out;

err:
    free(out);
    out = NULL;
    goto cleanup;
}


/**
 * Generate a SHA-256 fingerprint of an X.509 certificate.
 *
 * @param ctx       AVFormatContext for logging (can be NULL)
 * @param cert      X509 certificate to fingerprint
 * @return          Newly allocated fingerprint string in "AA:BB:CC:…" format,
 *                  or NULL on error (logs via av_log if ctx is not NULL).
 *                  Caller must free() the returned string.
 */
static char *generate_fingerprint(X509 *cert)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    int n = 0;
    AVBPrint fingerprint;
    char *result = NULL;
    int i;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&fingerprint, 0, AV_BPRINT_SIZE_UNLIMITED);

    if (X509_digest(cert, EVP_sha256(), md, &n) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate fingerprint, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto end;
    }

    for (i = 0; i < n; i++) {
        av_bprintf(&fingerprint, "%02X", md[i]);
        if (i + 1 < n)
            av_bprintf(&fingerprint, ":");
    }

    if (!fingerprint.str || !strlen(fingerprint.str)) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Fingerprint is empty\n");
        goto end;
    }

    result = av_strdup(fingerprint.str);
    if (!result) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Out of memory generating fingerprint\n");
    }

end:
    av_bprint_finalize(&fingerprint, NULL);
    return result;
}

int ff_ssl_read_key_cert(char *key_url, char *cert_url, char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    int ret = 0;
    BIO *key_b = NULL, *cert_b = NULL;
    AVBPrint key_bp, cert_bp;
    EVP_PKEY *pkey;
    X509 *cert;
    char *key_tem = NULL, *cert_tem = NULL;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&key_bp, 1, MAX_CERTIFICATE_SIZE);
    av_bprint_init(&cert_bp, 1, MAX_CERTIFICATE_SIZE);

    /* Read key file. */
    ret = ff_url_read_all(key_url, &key_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to open key file %s\n", key_url);
        goto end;
    }

    if (!(key_b = BIO_new(BIO_s_mem()))) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    BIO_write(key_b, key_bp.str, key_bp.len);
    pkey = PEM_read_bio_PrivateKey(key_b, NULL, NULL, NULL);
    if (!pkey) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to read private key from %s\n", key_url);
        ret = AVERROR(EIO);
        goto end;
    }

    /* Read certificate. */
    ret = ff_url_read_all(cert_url, &cert_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to open cert file %s\n", cert_url);
        goto end;
    }

    if (!(cert_b = BIO_new(BIO_s_mem()))) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    BIO_write(cert_b, cert_bp.str, cert_bp.len);
    cert = PEM_read_bio_X509(cert_b, NULL, NULL, NULL);
    if (!cert) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to read certificate from %s\n", cert_url);
        ret = AVERROR(EIO);
        goto end;
    }

    key_tem = pkey_to_pem_string(pkey);
    cert_tem = cert_to_pem_string(cert);

    snprintf(key_buf,  key_sz,  "%s", key_tem);
    snprintf(cert_buf, cert_sz, "%s", cert_tem);

    /* Generate fingerprint. */
    *fingerprint = generate_fingerprint(cert);
    if (!*fingerprint) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to generate fingerprint from %s\n", cert_url);
        ret = AVERROR(EIO);
        goto end;
    }

end:
    BIO_free(key_b);
    av_bprint_finalize(&key_bp, NULL);
    BIO_free(cert_b);
    av_bprint_finalize(&cert_bp, NULL);
    if (key_tem) av_free(key_tem);
    if (cert_tem) av_free(cert_tem);
    return ret;
}

static int openssl_gen_private_key(EVP_PKEY **pkey, EC_KEY **eckey)
{
    int ret = 0;

    /**
     * Note that secp256r1 in openssl is called NID_X9_62_prime256v1 or prime256v1 in string,
     * not NID_secp256k1 or secp256k1 in string.
     *
     * TODO: Should choose the curves in ClientHello.supported_groups, for example:
     *      Supported Group: x25519 (0x001d)
     *      Supported Group: secp256r1 (0x0017)
     *      Supported Group: secp384r1 (0x0018)
     */
#if OPENSSL_VERSION_NUMBER < 0x30000000L /* OpenSSL 3.0 */
    EC_GROUP *ecgroup = NULL;
    int curve = NID_X9_62_prime256v1;
#else
    const char *curve = SN_X9_62_prime256v1;
#endif

#if OPENSSL_VERSION_NUMBER < 0x30000000L /* OpenSSL 3.0 */
    *pkey = EVP_PKEY_new();
    *eckey = EC_KEY_new();
    ecgroup = EC_GROUP_new_by_curve_name(curve);
    if (!ecgroup) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Create EC group by curve=%d failed, %s", curve, ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
    /* For openssl 1.0, we must set the group parameters, so that cert is ok. */
    EC_GROUP_set_asn1_flag(ecgroup, OPENSSL_EC_NAMED_CURVE);
#endif

    if (EC_KEY_set_group(*eckey, ecgroup) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Generate private key, EC_KEY_set_group failed, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (EC_KEY_generate_key(*eckey) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Generate private key, EC_KEY_generate_key failed, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (EVP_PKEY_set1_EC_KEY(*pkey, *eckey) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Generate private key, EVP_PKEY_set1_EC_KEY failed, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }
#else
    *pkey = EVP_EC_gen(curve);
    if (!*pkey) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Generate private key, EVP_EC_gen curve=%s failed, %s\n", curve, ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }
#endif
    goto end;

einval_end:
    ret = AVERROR(EINVAL);
end:
#if OPENSSL_VERSION_NUMBER < 0x30000000L /* OpenSSL 3.0 */
    EC_GROUP_free(ecgroup);
#endif
    return ret;
}

static int openssl_gen_certificate(EVP_PKEY *pkey, X509 **cert, char **fingerprint)
{
    int ret = 0, serial, expire_day;
    const char *aor = "lavf";
    X509_NAME* subject = NULL;

    *cert= X509_new();
    if (!*cert) {
        goto enomem_end;
    }

    // TODO: Support non-self-signed certificate, for example, load from a file.
    subject = X509_NAME_new();
    if (!subject) {
        goto enomem_end;
    }

    serial = (int)av_get_random_seed();
    if (ASN1_INTEGER_set(X509_get_serialNumber(*cert), serial) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set serial, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, aor, strlen(aor), -1, 0) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set CN, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (X509_set_issuer_name(*cert, subject) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set issuer, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }
    if (X509_set_subject_name(*cert, subject) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set subject name, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    expire_day = 365;
    if (!X509_gmtime_adj(X509_get_notBefore(*cert), 0)) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set notBefore, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }
    if (!X509_gmtime_adj(X509_get_notAfter(*cert), 60*60*24*expire_day)) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set notAfter, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (X509_set_version(*cert, 2) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set version, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (X509_set_pubkey(*cert, pkey) != 1) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to set public key, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    if (!X509_sign(*cert, pkey, EVP_sha1())) {
        av_log(NULL, AV_LOG_ERROR, "TLS: Failed to sign certificate, %s\n", ERR_error_string(ERR_get_error(), NULL));
        goto einval_end;
    }

    *fingerprint = generate_fingerprint(*cert);
    if (!*fingerprint) {
        goto enomem_end;
    }

    goto end;
enomem_end:
    ret = AVERROR(ENOMEM);
    goto end;
einval_end:
    ret = AVERROR(EINVAL);
end:
    X509_NAME_free(subject);
    //av_bprint_finalize(&fingerprint, NULL);
    return ret;
}

int ff_ssl_gen_key_cert(char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    int ret = 0;
    EVP_PKEY *pkey = NULL;
    EC_KEY *ec_key = NULL;
    X509 *cert = NULL;
    char *key_tem = NULL, *cert_tem = NULL;

    ret = openssl_gen_private_key(&pkey, &ec_key);
    if (ret < 0) goto error;

    ret = openssl_gen_certificate(pkey, &cert, fingerprint);
    if (ret < 0) goto error;

    key_tem = pkey_to_pem_string(pkey);
    cert_tem = cert_to_pem_string(cert);

    snprintf(key_buf,  key_sz,  "%s", key_tem);
    snprintf(cert_buf, cert_sz, "%s", cert_tem);

    if (key_tem) av_free(key_tem);
    if (cert_tem) av_free(cert_tem);
error:
    return ret;
}


/**
 * Deserialize a PEM‐encoded private or public key from a NUL-terminated C string.
 *
 * @param pem_str   The PEM text, e.g.
 *                  "-----BEGIN PRIVATE KEY-----\n…\n-----END PRIVATE KEY-----\n"
 * @param is_priv   If non-zero, parse as a PRIVATE key; otherwise, parse as a PUBLIC key.
 * @return          EVP_PKEY* on success (must EVP_PKEY_free()), or NULL on error.
 */
static EVP_PKEY *pkey_from_pem_string(const char *pem_str, int is_priv)
{
#if OPENSSL_VERSION_NUMBER < 0x10002000L /* OpenSSL 1.0.2 */
    BIO *mem = BIO_new_mem_buf((void *)pem_str, -1);
#else
    BIO *mem = BIO_new_mem_buf(pem_str, -1);
#endif
    if (!mem) {
        av_log(NULL, AV_LOG_ERROR, "BIO_new_mem_buf failed\n");
        return NULL;
    }

    EVP_PKEY *pkey = NULL;
    if (is_priv) {
        pkey = PEM_read_bio_PrivateKey(mem, NULL, NULL, NULL);
    } else {
        pkey = PEM_read_bio_PUBKEY(mem, NULL, NULL, NULL);
    }

    if (!pkey)
        av_log(NULL, AV_LOG_ERROR, "Failed to parse %s key from string\n",
              is_priv ? "private" : "public");

    BIO_free(mem);
    return pkey;
}

/**
 * Deserialize a PEM‐encoded certificate from a NUL-terminated C string.
 *
 * @param pem_str   The PEM text, e.g.
 *                  "-----BEGIN CERTIFICATE-----\n…\n-----END CERTIFICATE-----\n"
 * @return          X509* on success (must X509_free()), or NULL on error.
 */
static X509 *cert_from_pem_string(const char *pem_str)
{
#if OPENSSL_VERSION_NUMBER < 0x10002000L /* OpenSSL 1.0.2 */
    BIO *mem = BIO_new_mem_buf((void *)pem_str, -1);
#else
    BIO *mem = BIO_new_mem_buf(pem_str, -1);
#endif
    if (!mem) {
        av_log(NULL, AV_LOG_ERROR, "BIO_new_mem_buf failed\n");
        return NULL;
    }

    X509 *cert = PEM_read_bio_X509(mem, NULL, NULL, NULL);
    if (!cert) {
        av_log(NULL, AV_LOG_ERROR, "Failed to parse certificate from string\n");
        return NULL;
    }

    BIO_free(mem);
    return cert;
}


typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;
    SSL_CTX *ctx;
    SSL *ssl;
    EVP_PKEY *pkey;
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    BIO_METHOD* url_bio_method;
#endif
    int io_err;
    char error_message[256];
} TLSContext;

/**
 * Retrieves the error message for the latest OpenSSL error.
 *
 * This function retrieves the error code from the thread's error queue, converts it
 * to a human-readable string, and stores it in the TLSContext's error_message field.
 * The error queue is then cleared using ERR_clear_error().
 */
static const char* openssl_get_error(TLSContext *ctx)
{
    int r2 = ERR_get_error();
    if (r2) {
        ERR_error_string_n(r2, ctx->error_message, sizeof(ctx->error_message));
    } else
        ctx->error_message[0] = '\0';

    ERR_clear_error();
    return ctx->error_message;
}

int ff_dtls_set_udp(URLContext *h, URLContext *udp)
{
    TLSContext *c = h->priv_data;
    c->tls_shared.udp = udp;
    return 0;
}

int ff_dtls_export_materials(URLContext *h, char *dtls_srtp_materials, size_t materials_sz)
{
    int ret = 0;
    const char* dst = "EXTRACTOR-dtls_srtp";
    TLSContext *c = h->priv_data;

    ret = SSL_export_keying_material(c->ssl, dtls_srtp_materials, materials_sz,
        dst, strlen(dst), NULL, 0, 0);
    if (!ret) {
        av_log(c, AV_LOG_ERROR, "TLS: Failed to export SRTP material, %s\n", openssl_get_error(c));
        return -1;
    }
    return 0;
}

int ff_dtls_state(URLContext *h)
{
    TLSContext *c = h->priv_data;
    return c->tls_shared.state;
}

/* OpenSSL 1.0.2 or below, then you would use SSL_library_init. If you are
 * using OpenSSL 1.1.0 or above, then the library will initialize
 * itself automatically.
 * https://wiki.openssl.org/index.php/Library_Initialization
 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#include "libavutil/thread.h"

static AVMutex openssl_mutex = AV_MUTEX_INITIALIZER;

static int openssl_init;

#if HAVE_THREADS
#include <openssl/crypto.h>
#include "libavutil/mem.h"

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
    ff_mutex_lock(&openssl_mutex);
    if (!openssl_init) {
        SSL_library_init();
        SSL_load_error_strings();
#if HAVE_THREADS
        if (!CRYPTO_get_locking_callback()) {
            int i;
            openssl_mutexes = av_malloc_array(sizeof(pthread_mutex_t), CRYPTO_num_locks());
            if (!openssl_mutexes) {
                ff_mutex_unlock(&openssl_mutex);
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
    ff_mutex_unlock(&openssl_mutex);

    return 0;
}

void ff_openssl_deinit(void)
{
    ff_mutex_lock(&openssl_mutex);
    openssl_init--;
    if (!openssl_init) {
#if HAVE_THREADS
        if (CRYPTO_get_locking_callback() == openssl_lock) {
            int i;
            CRYPTO_set_locking_callback(NULL);
            for (i = 0; i < CRYPTO_num_locks(); i++)
                pthread_mutex_destroy(&openssl_mutexes[i]);
            av_free(openssl_mutexes);
        }
#endif
    }
    ff_mutex_unlock(&openssl_mutex);
}
#endif

static int print_ssl_error(URLContext *h, int ret)
{
    TLSContext *c = h->priv_data;
    int printed = 0, e, averr = AVERROR(EIO);
    if (h->flags & AVIO_FLAG_NONBLOCK) {
        int err = SSL_get_error(c->ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            return AVERROR(EAGAIN);
    }
    while ((e = ERR_get_error()) != 0) {
        av_log(h, AV_LOG_ERROR, "%s\n", ERR_error_string(e, NULL));
        printed = 1;
    }
    if (c->io_err) {
        av_log(h, AV_LOG_ERROR, "IO error: %s\n", av_err2str(c->io_err));
        printed = 1;
        averr = c->io_err;
        c->io_err = 0;
    }
    if (!printed)
        av_log(h, AV_LOG_ERROR, "Unknown error\n");
    return averr;
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
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ff_openssl_deinit();
#endif
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
    TLSContext *c = GET_BIO_DATA(b);
    int ret = ffurl_read(c->tls_shared.is_dtls ? c->tls_shared.udp : c->tls_shared.tcp, buf, len);
    if (ret >= 0)
        return ret;
    BIO_clear_retry_flags(b);
    if (ret == AVERROR_EXIT)
        return 0;
    if (ret == AVERROR(EAGAIN))
        BIO_set_retry_read(b);
    else
        c->io_err = ret;
    return -1;
}

static int url_bio_bwrite(BIO *b, const char *buf, int len)
{
    TLSContext *c = GET_BIO_DATA(b);
    int ret = ffurl_write(c->tls_shared.is_dtls ? c->tls_shared.udp : c->tls_shared.tcp, buf, len);
    if (ret >= 0)
        return ret;
    BIO_clear_retry_flags(b);
    if (ret == AVERROR_EXIT)
        return 0;
    if (ret == AVERROR(EAGAIN))
        BIO_set_retry_write(b);
    else
        c->io_err = ret;
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

static av_cold void init_bio_method(URLContext *h)
{
    TLSContext *p = h->priv_data;
    BIO *bio;
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
    p->url_bio_method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "urlprotocol bio");
    BIO_meth_set_write(p->url_bio_method, url_bio_bwrite);
    BIO_meth_set_read(p->url_bio_method, url_bio_bread);
    BIO_meth_set_puts(p->url_bio_method, url_bio_bputs);
    BIO_meth_set_ctrl(p->url_bio_method, url_bio_ctrl);
    BIO_meth_set_create(p->url_bio_method, url_bio_create);
    BIO_meth_set_destroy(p->url_bio_method, url_bio_destroy);
    bio = BIO_new(p->url_bio_method);
    BIO_set_data(bio, p);
#else
    bio = BIO_new(&url_bio_method);
    bio->ptr = p;
#endif
    SSL_set_bio(p->ssl, bio, bio);
}

static void openssl_info_callback(const SSL *ssl, int where, int ret) {
    const char *method = "undefined";
    TLSContext *ctx = (TLSContext*)SSL_get_ex_data(ssl, 0);

    if (where & SSL_ST_CONNECT) {
        method = "SSL_connect";
    } else if (where & SSL_ST_ACCEPT)
        method = "SSL_accept";

    if (where & SSL_CB_LOOP) {
        av_log(ctx, AV_LOG_DEBUG, "Info method=%s state=%s(%s), where=%d, ret=%d\n",
               method, SSL_state_string(ssl), SSL_state_string_long(ssl), where, ret);
    } else if (where & SSL_CB_ALERT) {
        method = (where & SSL_CB_READ) ? "read":"write";
        av_log(ctx, AV_LOG_DEBUG, "Alert method=%s state=%s(%s), where=%d, ret=%d\n",
               method, SSL_state_string(ssl), SSL_state_string_long(ssl), where, ret);
    }
}

/**
 * Always return 1 to accept any certificate. This is because we allow the peer to
 * use a temporary self-signed certificate for DTLS.
 */
static int openssl_dtls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    return 1;
}

static int dtls_handshake(URLContext *h)
{
    int ret = 0, r0, r1;
    TLSContext *p = h->priv_data;

    r0 = SSL_do_handshake(p->ssl);
    r1 = SSL_get_error(p->ssl, r0);
    if (r0 <= 0) {
        if (r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE && r1 != SSL_ERROR_ZERO_RETURN) {
            av_log(p, AV_LOG_ERROR, "TLS: Read failed, r0=%d, r1=%d %s\n", r0, r1, openssl_get_error(p));
            ret = AVERROR(EIO);
            goto end;
        }
    } else {
        av_log(p, AV_LOG_TRACE, "TLS: Read %d bytes, r0=%d, r1=%d\n", r0, r0, r1);
    }

    /* Check whether the DTLS is completed. */
    if (SSL_is_init_finished(p->ssl) != 1)
        goto end;

    p->tls_shared.state = DTLS_STATE_FINISHED;
end:
    return ret;
}

static av_cold int openssl_init_ca_key_cert(URLContext *h)
{
    int ret;
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    EVP_PKEY *pkey = NULL;
    X509 *cert = NULL;
    /* setup ca, private key, certificate */
    if (c->ca_file) {
        if (!SSL_CTX_load_verify_locations(p->ctx, c->ca_file, NULL))
            av_log(h, AV_LOG_ERROR, "SSL_CTX_load_verify_locations %s\n", openssl_get_error(p));
    }

    if (c->cert_file) {
        ret = SSL_CTX_use_certificate_chain_file(p->ctx, c->cert_file);
        if (ret <= 0) {
            av_log(h, AV_LOG_ERROR, "Unable to load cert file %s: %s\n",
               c->cert_file, openssl_get_error(p));
            ret = AVERROR(EIO);
            goto fail;
        }
    } else if (p->tls_shared.cert_buf) {
        cert = cert_from_pem_string(p->tls_shared.cert_buf);
        if (SSL_CTX_use_certificate(p->ctx, cert) != 1) {
            av_log(p, AV_LOG_ERROR, "SSL: Init SSL_CTX_use_certificate failed, %s\n", openssl_get_error(p));
            ret = AVERROR(EINVAL);
            return ret;
        }
    } else if (p->tls_shared.is_dtls){
        av_log(p, AV_LOG_ERROR, "TLS: Init cert failed, %s\n", openssl_get_error(p));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (c->key_file) {
        ret = SSL_CTX_use_PrivateKey_file(p->ctx, c->key_file, SSL_FILETYPE_PEM);
        if (ret <= 0) {
            av_log(h, AV_LOG_ERROR, "Unable to load key file %s: %s\n",
                c->key_file, openssl_get_error(p));
            ret = AVERROR(EIO);
            goto fail;
        }
    } else if (p->tls_shared.key_buf) {
        p->pkey = pkey = pkey_from_pem_string(p->tls_shared.key_buf, 1);
        if (SSL_CTX_use_PrivateKey(p->ctx, pkey) != 1) {
            av_log(p, AV_LOG_ERROR, "TLS: Init SSL_CTX_use_PrivateKey failed, %s\n", openssl_get_error(p));
            ret = AVERROR(EINVAL);
            return ret;
        }
    } else if (p->tls_shared.is_dtls){
        av_log(p, AV_LOG_ERROR, "TLS: Init pkey failed, %s\n", openssl_get_error(p));
        ret = AVERROR(EINVAL);
        goto fail;
    }
    ret = 0;
fail:
    return ret;
}

/**
 * Once the DTLS role has been negotiated - active for the DTLS client or passive for the
 * DTLS server - we proceed to set up the DTLS state and initiate the handshake.
 */
static int dtls_start(URLContext *h, const char *url, int flags, AVDictionary **options)
{
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    int ret = 0;
    c->is_dtls = 1;
    const char* ciphers = "ALL";
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    EC_KEY *ec_key = NULL;
#endif
    /**
     * The profile for OpenSSL's SRTP is SRTP_AES128_CM_SHA1_80, see ssl/d1_srtp.c.
     * The profile for FFmpeg's SRTP is SRTP_AES128_CM_HMAC_SHA1_80, see libavformat/srtp.c.
     */
    const char* profiles = "SRTP_AES128_CM_SHA1_80";
    /* Refer to the test cases regarding these curves in the WebRTC code. */
#if OPENSSL_VERSION_NUMBER >= 0x10100000L /* OpenSSL 1.1.0 */
    const char* curves = "X25519:P-256:P-384:P-521";
#elif OPENSSL_VERSION_NUMBER >= 0x10002000L /* OpenSSL 1.0.2 */
    const char* curves = "P-256:P-384:P-521";
#endif

#if OPENSSL_VERSION_NUMBER < 0x10002000L /* OpenSSL v1.0.2 */
    p->ctx = SSL_CTX_new(DTLSv1_method());
#else
    p->ctx = SSL_CTX_new(DTLS_method());
#endif
    if (!p->ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER >= 0x10002000L /* OpenSSL 1.0.2 */
    /* For ECDSA, we could set the curves list. */
    if (SSL_CTX_set1_curves_list(p->ctx, curves) != 1) {
        av_log(p, AV_LOG_ERROR, "TLS: Init SSL_CTX_set1_curves_list failed, curves=%s, %s\n",
            curves, openssl_get_error(p));
        ret = AVERROR(EINVAL);
        return ret;
    }
#endif

    /**
     * We activate "ALL" cipher suites to align with the peer's capabilities,
     * ensuring maximum compatibility.
     */
    if (SSL_CTX_set_cipher_list(p->ctx, ciphers) != 1) {
        av_log(p, AV_LOG_ERROR, "TLS: Init SSL_CTX_set_cipher_list failed, ciphers=%s, %s\n",
            ciphers, openssl_get_error(p));
        ret = AVERROR(EINVAL);
        return ret;
    }
    ret = openssl_init_ca_key_cert(h);
    if (ret < 0) goto fail;

#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    if (p->pkey)
        ec_key = EVP_PKEY_get1_EC_KEY(p->pkey);
    if (ec_key)
        SSL_CTX_set_tmp_ecdh(p->ctx, ec_key);
#else
    SSL_CTX_set_ecdh_auto(p->ctx, 1);
#endif
#endif

    /* Server will send Certificate Request. */
    SSL_CTX_set_verify(p->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, openssl_dtls_verify_callback);
    /* The depth count is "level 0:peer certificate", "level 1: CA certificate",
     * "level 2: higher level CA certificate", and so on. */
    SSL_CTX_set_verify_depth(p->ctx, 4);
    /* Whether we should read as many input bytes as possible (for non-blocking reads) or not. */
    SSL_CTX_set_read_ahead(p->ctx, 1);
    /* Setup the SRTP context */
    if (SSL_CTX_set_tlsext_use_srtp(p->ctx, profiles)) {
        av_log(p, AV_LOG_ERROR, "TLS: Init SSL_CTX_set_tlsext_use_srtp failed, profiles=%s, %s\n",
            profiles, openssl_get_error(p));
        ret = AVERROR(EINVAL);
        return ret;
    }

    /* The ssl should not be created unless the ctx has been initialized. */
    p->ssl = SSL_new(p->ctx);
    if (!p->ssl) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* Setup the callback for logging. */
    SSL_set_ex_data(p->ssl, 0, p);
    SSL_set_info_callback(p->ssl, openssl_info_callback);
    /**
     * We have set the MTU to fragment the DTLS packet. It is important to note that the
     * packet is split to ensure that each handshake packet is smaller than the MTU.
     */
    SSL_set_options(p->ssl, SSL_OP_NO_QUERY_MTU);
    SSL_set_mtu(p->ssl, p->tls_shared.mtu);
#if OPENSSL_VERSION_NUMBER >= 0x100010b0L /* OpenSSL 1.0.1k */
    DTLS_set_link_mtu(p->ssl, p->tls_shared.mtu);
#endif
    init_bio_method(h);

    if (p->tls_shared.use_external_udp != 1) {
        if ((ret = ff_tls_open_underlying(&p->tls_shared, h, url, options)) < 0) {
            av_log(p, AV_LOG_ERROR, "Failed to connect %s\n", url);
            return ret;
        }
    }

    /* Setup DTLS as passive, which is server role. */
    c->listen ? SSL_set_accept_state(p->ssl) : SSL_set_connect_state(p->ssl);

    /**
     * During initialization, we only need to call SSL_do_handshake once because SSL_read consumes
     * the handshake message if the handshake is incomplete.
     * To simplify maintenance, we initiate the handshake for both the DTLS server and client after
     * sending out the ICE response in the start_active_handshake function. It's worth noting that
     * although the DTLS server may receive the ClientHello immediately after sending out the ICE
     * response, this shouldn't be an issue as the handshake function is called before any DTLS
     * packets are received.
     *
     * The SSL_do_handshake can't be called if DTLS hasn't prepare for udp.
     */
    if (p->tls_shared.use_external_udp != 1) {
        ret = dtls_handshake(h);
        // Fatal SSL error, for example, no available suite when peer is DTLS 1.0 while we are DTLS 1.2.
        if (ret < 0) {
            av_log(p, AV_LOG_ERROR, "TLS: Failed to drive SSL context, ret=%d\n", ret);
            return AVERROR(EIO);
        }
    }

    av_log(p, AV_LOG_VERBOSE, "TLS: Setup ok, MTU=%d, fingerprint %s\n",
        p->tls_shared.mtu, p->tls_shared.fingerprint);

    ret = 0;
fail:
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    EC_KEY_free(ec_key);
#endif
    return ret;
}

/**
 * Cleanup the DTLS context.
 */
static av_cold int dtls_close(URLContext *h)
{
    TLSContext *ctx = h->priv_data;
    SSL_free(ctx->ssl);
    SSL_CTX_free(ctx->ctx);
    av_freep(&ctx->tls_shared.fingerprint);
    av_freep(&ctx->tls_shared.cert_buf);
    av_freep(&ctx->tls_shared.key_buf);
    EVP_PKEY_free(ctx->pkey);
    return 0;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *p = h->priv_data;
    TLSShared *c = &p->tls_shared;
    int ret;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    if ((ret = ff_openssl_init()) < 0)
        return ret;
#endif

    if ((ret = ff_tls_open_underlying(c, h, uri, options)) < 0)
        goto fail;

    // We want to support all versions of TLS >= 1.0, but not the deprecated
    // and insecure SSLv2 and SSLv3.  Despite the name, SSLv23_*_method()
    // enables support for all versions of SSL and TLS, and we then disable
    // support for the old protocols immediately after creating the context.
    p->ctx = SSL_CTX_new(c->listen ? SSLv23_server_method() : SSLv23_client_method());
    if (!p->ctx) {
        av_log(h, AV_LOG_ERROR, "%s\n", openssl_get_error(p));
        ret = AVERROR(EIO);
        goto fail;
    }
    SSL_CTX_set_options(p->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    ret = openssl_init_ca_key_cert(h);
    if (ret < 0) goto fail;
    // Note, this doesn't check that the peer certificate actually matches
    // the requested hostname.
    if (c->verify)
        SSL_CTX_set_verify(p->ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    p->ssl = SSL_new(p->ctx);
    if (!p->ssl) {
        av_log(h, AV_LOG_ERROR, "%s\n", openssl_get_error(p));
        ret = AVERROR(EIO);
        goto fail;
    }
    SSL_set_ex_data(p->ssl, 0, p);
    SSL_CTX_set_info_callback(p->ctx, openssl_info_callback);
    init_bio_method(h);
    if (!c->listen && !c->numerichost)
        SSL_set_tlsext_host_name(p->ssl, c->host);
    ret = c->listen ? SSL_accept(p->ssl) : SSL_connect(p->ssl);
    if (ret == 0) {
        av_log(h, AV_LOG_ERROR, "Unable to negotiate TLS/SSL session\n");
        ret = AVERROR(EIO);
        goto fail;
    } else if (ret < 0) {
        ret = print_ssl_error(h, ret);
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
    URLContext *uc = c->tls_shared.is_dtls ? c->tls_shared.udp
                                           : c->tls_shared.tcp;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = SSL_read(c->ssl, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_ssl_error(h, ret);
}

static int tls_write(URLContext *h, const uint8_t *buf, int size)
{
    TLSContext *c = h->priv_data;
    URLContext *uc = c->tls_shared.is_dtls ? c->tls_shared.udp
                                           : c->tls_shared.tcp;
    int ret;
    // Set or clear the AVIO_FLAG_NONBLOCK on c->tls_shared.tcp
    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;
    ret = SSL_write(c->ssl, buf, size);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return AVERROR_EOF;
    return print_ssl_error(h, ret);
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
    .url_open2      = dtls_start,
    .url_handshake  = dtls_handshake,
    .url_close      = dtls_close,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &dtls_class,
};
