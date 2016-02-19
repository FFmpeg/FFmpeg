/*
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

#include "config.h"

#include "url.h"

extern const URLProtocol ff_concat_protocol;
extern const URLProtocol ff_crypto_protocol;
extern const URLProtocol ff_ffrtmpcrypt_protocol;
extern const URLProtocol ff_ffrtmphttp_protocol;
extern const URLProtocol ff_file_protocol;
extern const URLProtocol ff_gopher_protocol;
extern const URLProtocol ff_hls_protocol;
extern const URLProtocol ff_http_protocol;
extern const URLProtocol ff_httpproxy_protocol;
extern const URLProtocol ff_https_protocol;
extern const URLProtocol ff_icecast_protocol;
extern const URLProtocol ff_mmsh_protocol;
extern const URLProtocol ff_mmst_protocol;
extern const URLProtocol ff_md5_protocol;
extern const URLProtocol ff_pipe_protocol;
extern const URLProtocol ff_rtmp_protocol;
extern const URLProtocol ff_rtmpe_protocol;
extern const URLProtocol ff_rtmps_protocol;
extern const URLProtocol ff_rtmpt_protocol;
extern const URLProtocol ff_rtmpte_protocol;
extern const URLProtocol ff_rtmpts_protocol;
extern const URLProtocol ff_rtp_protocol;
extern const URLProtocol ff_sctp_protocol;
extern const URLProtocol ff_srtp_protocol;
extern const URLProtocol ff_tcp_protocol;
extern const URLProtocol ff_tls_gnutls_protocol;
extern const URLProtocol ff_tls_openssl_protocol;
extern const URLProtocol ff_udp_protocol;
extern const URLProtocol ff_unix_protocol;
extern const URLProtocol ff_librtmp_protocol;
extern const URLProtocol ff_librtmpe_protocol;
extern const URLProtocol ff_librtmps_protocol;
extern const URLProtocol ff_librtmpt_protocol;
extern const URLProtocol ff_librtmpte_protocol;

const URLProtocol *ff_url_protocols[] = {
#if CONFIG_CONCAT_PROTOCOL
    &ff_concat_protocol,
#endif
#if CONFIG_CRYPTO_PROTOCOL
    &ff_crypto_protocol,
#endif
#if CONFIG_FFRTMPCRYPT_PROTOCOL
    &ff_ffrtmpcrypt_protocol,
#endif
#if CONFIG_FFRTMPHTTP_PROTOCOL
    &ff_ffrtmphttp_protocol,
#endif
#if CONFIG_FILE_PROTOCOL
    &ff_file_protocol,
#endif
#if CONFIG_GOPHER_PROTOCOL
    &ff_gopher_protocol,
#endif
#if CONFIG_HLS_PROTOCOL
    &ff_hls_protocol,
#endif
#if CONFIG_HTTP_PROTOCOL
    &ff_http_protocol,
#endif
#if CONFIG_HTTPPROXY_PROTOCOL
    &ff_httpproxy_protocol,
#endif
#if CONFIG_HTTPS_PROTOCOL
    &ff_https_protocol,
#endif
#if CONFIG_ICECAST_PROTOCOL
    &ff_icecast_protocol,
#endif
#if CONFIG_MMSH_PROTOCOL
    &ff_mmsh_protocol,
#endif
#if CONFIG_MMST_PROTOCOL
    &ff_mmst_protocol,
#endif
#if CONFIG_MD5_PROTOCOL
    &ff_md5_protocol,
#endif
#if CONFIG_PIPE_PROTOCOL
    &ff_pipe_protocol,
#endif
#if CONFIG_RTMP_PROTOCOL
    &ff_rtmp_protocol,
#endif
#if CONFIG_RTMPE_PROTOCOL
    &ff_rtmpe_protocol,
#endif
#if CONFIG_RTMPS_PROTOCOL
    &ff_rtmps_protocol,
#endif
#if CONFIG_RTMPT_PROTOCOL
    &ff_rtmpt_protocol,
#endif
#if CONFIG_RTMPTE_PROTOCOL
    &ff_rtmpte_protocol,
#endif
#if CONFIG_RTMPTS_PROTOCOL
    &ff_rtmpts_protocol,
#endif
#if CONFIG_RTP_PROTOCOL
    &ff_rtp_protocol,
#endif
#if CONFIG_SCTP_PROTOCOL
    &ff_sctp_protocol,
#endif
#if CONFIG_SRTP_PROTOCOL
    &ff_srtp_protocol,
#endif
#if CONFIG_TCP_PROTOCOL
    &ff_tcp_protocol,
#endif
#if CONFIG_TLS_GNUTLS_PROTOCOL
    &ff_tls_gnutls_protocol,
#endif
#if CONFIG_TLS_OPENSSL_PROTOCOL
    &ff_tls_openssl_protocol,
#endif
#if CONFIG_UDP_PROTOCOL
    &ff_udp_protocol,
#endif
#if CONFIG_UNIX_PROTOCOL
    &ff_unix_protocol,
#endif

    /* external libraries */
#if CONFIG_LIBRTMP_PROTOCOL
    &ff_librtmp_protocol,
#endif
#if CONFIG_LIBRTMPE_PROTOCOL
    &ff_librtmpe_protocol,
#endif
#if CONFIG_LIBRTMPS_PROTOCOL
    &ff_librtmps_protocol,
#endif
#if CONFIG_LIBRTMPT_PROTOCOL
    &ff_librtmpt_protocol,
#endif
#if CONFIG_LIBRTMPTE_PROTOCOL
    &ff_librtmpte_protocol,
#endif
    NULL,
};

const char *avio_enum_protocols(void **opaque, int output)
{
    const URLProtocol **p = *opaque;

    p = p ? p + 1 : ff_url_protocols;
    *opaque = p;
    if (!*p) {
        *opaque = NULL;
        return NULL;
    }
    if ((output && (*p)->url_write) || (!output && (*p)->url_read))
        return (*p)->name;
    return avio_enum_protocols(opaque, output);
}
