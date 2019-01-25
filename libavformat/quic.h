#ifndef BVC_QUIC_CLIENT_API_H_
#define BVC_QUIC_CLIENT_API_H_

#include <stdlib.h>

#if defined(WIN32)
#define EXPORT_FROM_BVC __declspec(dllexport)
#else
#define EXPORT_FROM_BVC __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QUIC_HEADERS_SIZE 4096

typedef void* tBvcQuicHandler;

typedef struct BvcQuicClientOptions {
    char*  url;
    char*  host;
    int    port;
    char*  headers;
    char*  body;
    int    quic_version;
    int    init_mtu;
    int    need_cert_verify;
    int    connect_timeout_ms;
    int    buffer_size;
} BvcQuicClientOptions;

EXPORT_FROM_BVC tBvcQuicHandler bvc_quic_client_create(const BvcQuicClientOptions* opt_ptr);
EXPORT_FROM_BVC void bvc_quic_client_destroy(tBvcQuicHandler handler);
EXPORT_FROM_BVC int bvc_quic_client_start(tBvcQuicHandler handler);
EXPORT_FROM_BVC int bvc_quic_client_read(tBvcQuicHandler handler, char* buf, int size);

EXPORT_FROM_BVC int bvc_quic_client_buffer_size(tBvcQuicHandler handler);

EXPORT_FROM_BVC int  bvc_quic_client_response_code(tBvcQuicHandler handler);
EXPORT_FROM_BVC void bvc_quic_client_response_header(tBvcQuicHandler handler, const char* key, int key_len, const char** val, int* val_len);

#ifdef __cplusplus
}
#endif

#endif
