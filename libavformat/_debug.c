#include "avformat.h"
#include "url.h"

#if !CONFIG_AVISYNTH
AVInputFormat ff_avisynth_demuxer;
#endif

#if !CONFIG_BLURAY_PROTOCOL
URLProtocol ff_bluray_protocol;
#endif

#if !CONFIG_FFRTMPCRYPT_PROTOCOL
URLProtocol ff_ffrtmpcrypt_protocol;
#endif

#if !CONFIG_SCTP_PROTOCOL
URLProtocol ff_sctp_protocol;
#endif

#if !CONFIG_TLS_SECURETRANSPORT_PROTOCOL
URLProtocol ff_tls_securetransport_protocol;
#endif

#if !CONFIG_TLS_GNUTLS_PROTOCOL
URLProtocol ff_tls_gnutls_protocol;
#endif

#if !CONFIG_TLS_OPENSSL_PROTOCOL
URLProtocol ff_tls_openssl_protocol;
#endif

#if !CONFIG_UNIX_PROTOCOL
URLProtocol ff_unix_protocol;
#endif

#if !CONFIG_CHROMAPRINT_MUXER
AVOutputFormat ff_chromaprint_muxer;
#endif

#if !CONFIG_LIBGME_DEMUXER
AVInputFormat ff_libgme_demuxer;
#endif

#if !CONFIG_LIBMODPLUG_DEMUXER
AVInputFormat ff_libmodplug_demuxer;
#endif

#if !CONFIG_LIBNUT_DEMUXER
AVInputFormat ff_libnut_demuxer;
#endif

#if !CONFIG_LIBNUT_MUXER
AVOutputFormat ff_libnut_muxer;
#endif

#if !CONFIG_LIBRTMP
URLProtocol ff_librtmp_protocol;
URLProtocol ff_librtmpt_protocol;
URLProtocol ff_librtmpe_protocol;
URLProtocol ff_librtmpte_protocol;
URLProtocol ff_librtmps_protocol;
#endif

#if !CONFIG_LIBSSH_PROTOCOL
URLProtocol ff_libssh_protocol;
#endif

#if !CONFIG_LIBSMBCLIENT_PROTOCOL
URLProtocol ff_libsmbclient_protocol;
#endif

#if !CONFIG_FFRTMPCRYPT_PROTOCOL
int ff_rtmpe_gen_pub_key(struct URLContext *h, uint8_t *buf){return 0;};
int ff_rtmpe_compute_secret_key(struct URLContext *h, const uint8_t *serverdata,
                                const uint8_t *clientdata, int type){return 0;}; 
void ff_rtmpe_encrypt_sig(struct URLContext *h, uint8_t *signature,
                          const uint8_t *digest, int type){};
int ff_rtmpe_update_keystream(struct URLContext *h){return 0;};
#endif						  