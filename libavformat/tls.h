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

#ifndef AVFORMAT_TLS_H
#define AVFORMAT_TLS_H

#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "url.h"

/**
 * Maximum size limit of a certificate and private key size.
 */
#define MAX_CERTIFICATE_SIZE 8192

enum DTLSState {
    DTLS_STATE_NONE,

    /* Whether DTLS handshake is finished. */
    DTLS_STATE_FINISHED,
    /* Whether DTLS session is closed. */
    DTLS_STATE_CLOSED,
    /* Whether DTLS handshake is failed. */
    DTLS_STATE_FAILED,
};

typedef struct TLSShared {
    char *ca_file;
    int verify;
    char *cert_file;
    char *key_file;
    int listen;

    char *host;
    char *http_proxy;

    char underlying_host[200];
    int numerichost;

    URLContext *tcp;

    int is_dtls;

    enum DTLSState state;

    int use_external_udp;
    URLContext *udp;

    /* The fingerprint of certificate, used in SDP offer. */
    char *fingerprint;

    /* The certificate and private key content used for DTLS handshake */
    char* cert_buf;
    char* key_buf;
    /**
     * The size of RTP packet, should generally be set to MTU.
     * Note that pion requires a smaller value, for example, 1200.
     */
    int mtu;
} TLSShared;

#define TLS_OPTFL (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
#define TLS_COMMON_OPTIONS(pstruct, options_field) \
    {"ca_file",    "Certificate Authority database file", offsetof(pstruct, options_field . ca_file),   AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"cafile",     "Certificate Authority database file", offsetof(pstruct, options_field . ca_file),   AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"tls_verify", "Verify the peer certificate",         offsetof(pstruct, options_field . verify),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = TLS_OPTFL }, \
    {"cert_file",  "Certificate file",                    offsetof(pstruct, options_field . cert_file), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"key_file",   "Private key file",                    offsetof(pstruct, options_field . key_file),  AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"listen",     "Listen for incoming connections",     offsetof(pstruct, options_field . listen),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = TLS_OPTFL }, \
    {"verifyhost", "Verify against a specific hostname",  offsetof(pstruct, options_field . host),      AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"http_proxy", "Set proxy to tunnel through",         offsetof(pstruct, options_field . http_proxy), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL }, \
    {"use_external_udp", "Use external UDP from muxer or demuxer", offsetof(pstruct, options_field . use_external_udp), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 1, .flags = TLS_OPTFL }, \
    {"mtu", "Maximum Transmission Unit", offsetof(pstruct, options_field . mtu), AV_OPT_TYPE_INT,  { .i64 = 0 }, 0, INT_MAX, .flags = TLS_OPTFL}, \
    {"fingerprint", "The optional fingerprint for DTLS", offsetof(pstruct, options_field . fingerprint), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL}, \
    {"cert_buf", "The optional certificate buffer for DTLS", offsetof(pstruct, options_field . cert_buf), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL}, \
    {"key_buf", "The optional private key buffer for DTLS", offsetof(pstruct, options_field . key_buf), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL}

int ff_tls_open_underlying(TLSShared *c, URLContext *parent, const char *uri, AVDictionary **options);

int ff_url_read_all(const char *url, AVBPrint *bp);

int ff_dtls_set_udp(URLContext *h, URLContext *udp);

int ff_dtls_export_materials(URLContext *h, char *dtls_srtp_materials, size_t materials_sz);

int ff_dtls_state(URLContext *h);

int ff_ssl_read_key_cert(char *key_url, char *cert_url, char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint);

int ff_ssl_gen_key_cert(char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint);

void ff_gnutls_init(void);
void ff_gnutls_deinit(void);

int ff_openssl_init(void);
void ff_openssl_deinit(void);

#endif /* AVFORMAT_TLS_H */
