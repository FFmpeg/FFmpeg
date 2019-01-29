/*
 * QUIC protocol
 * Copyright (c) 2018 BiLiBiLi
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
#include "libavutil/avassert.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "quic.h"
#include "libavutil/application.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include <ctype.h>

typedef struct QUICContext {
    const AVClass*  class;
    tBvcQuicHandler handler;
    char*           url;
    char*           host_ip;
    int             host_port;
    char*           headers;
    char*           user_agent;
    char*           body;
    int             proto_version;
    int             init_mtu;
    int             need_cert_verify;
    int             connect_timeout_us;
    int             rw_timeout;
    int             recv_buffer_size;
    int             seekable; /**< Control seekability, 0 = disable, 1 = enable, -1 = probe. */
    int64_t         app_ctx_intptr;
    AVApplicationContext *app_ctx;
    int dash_audio_tcp;
    int dash_video_tcp;
    int dns_cache_clear;
    // response values
    int             resp_code;
    uint64_t        body_off, body_len;
} QUICContext;

#define OFFSET(x) offsetof(QUICContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM

#define DEFAULT_USER_AGENT "Lavf/" AV_STRINGIFY(LIBAVFORMAT_VERSION)
static const AVOption options[] = {
    { "seekable", "control seekability of connection", OFFSET(seekable), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, D },
    { "offset", "initial byte offset", OFFSET(body_off), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, { .str = DEFAULT_USER_AGENT }, 0, 0, D },
    { "host", "IP address of the hostname to connect to", OFFSET(host_ip), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D|E },
    { "port", "Host port to connect to", OFFSET(host_port), AV_OPT_TYPE_INT, { .i64 = 443 }, 0, 65535, .flags = D|E },
    { "headers", "HTTP request headers(a semicolon separated list of key:value pairs), can override built in default headers", OFFSET(headers), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D|E },
    { "body",    "HTTP request body content", OFFSET(body), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D|E },
    { "quic_version", "Version of QUIC protocol", OFFSET(proto_version), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, .flags = D|E },
    { "initial_mtu", "Initial MTU of quic connection", OFFSET(init_mtu), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, .flags = D|E },
    { "need_cert_verify", "Need quic verify certificates", OFFSET(need_cert_verify), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, .flags = D|E },
    { "recv_buffer_size", "Quic client receive buffer in bytes", OFFSET(recv_buffer_size), AV_OPT_TYPE_INT, { .i64 = 1048576 }, 1024, 67108864, .flags = D|E },
    { "timeout",     "set timeout (in microseconds) of socket I/O operations", OFFSET(rw_timeout),     AV_OPT_TYPE_INT, { .i64 = 2000000 },         -1, INT_MAX, .flags = D|E },
    { "connect_timeout",  "set connect timeout (in microseconds) of socket", OFFSET(connect_timeout_us),     AV_OPT_TYPE_INT, { .i64 = 10000000 },         -1, INT_MAX, .flags = D|E },
    { "dash_audio_tcp", "dash audio tcp", OFFSET(dash_audio_tcp), AV_OPT_TYPE_INT, { .i64 = 0},       0, 1, .flags = D|E },
    { "dash_video_tcp", "dash video tcp", OFFSET(dash_video_tcp), AV_OPT_TYPE_INT, { .i64 = 0},       0, 1, .flags = D|E },
    { "ijkapplication", "AVApplicationContext", OFFSET(app_ctx_intptr), AV_OPT_TYPE_INT64, { .i64 = 0 }, INT64_MIN, INT64_MAX, .flags = D },
    { "dns_cache_clear", "clear dns cache",   OFFSET(dns_cache_clear), AV_OPT_TYPE_INT, { .i64 = 0},       -1, INT_MAX, .flags = D|E },
    { NULL }
};

static const AVClass quic_class = {
    .class_name = "quic",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int quic_strtoi(const char* str, int len, uint64_t* val_ptr)
{
    uint64_t val = 0;
    int i = 0;
    int convert_stat = 0; // 0: start; 1: converting; 2: tail
    if (!val_ptr) {
        return AVERROR(EINVAL);
    }
    for (i = 0; i < len; i++) {
        // 0. skip head space
        if (convert_stat == 0) {
            if (isspace(str[i])) {
                continue;
            }
            convert_stat = 1;
        }
        // 1. converting
        if (convert_stat == 1) {
            if (isdigit(str[i])) {
                val = (val * 10 + (str[i] - '0'));
            } else if (isspace(str[i])) {
                convert_stat = 2;
            } else {
                return AVERROR(EINVAL);
            }
        }
        // 2. tail check
        if (convert_stat == 2) {
            if (!isspace(str[i])) {
                return AVERROR(EINVAL);
            }
        }
    }

    *val_ptr = val;
    return 0;
}

static inline int has_header(const char *str, const char *header)
{
    /* header + 2 to skip over CRLF prefix. (make sure you have one!) */
    if (!str)
        return 0;
    return av_stristart(str, header + 2, NULL) || av_stristr(str, header);
}

static void quic_url_split(const char *url, char *hostname, int hostname_size, char *ip,int ip_size)
{
    const char *p;
    int url0_len = 0;
    int url1_len = 0;

    if (!url) {
        return;
    }

    if (p = strstr(url, "quic://")) {
        av_strstart(p, "quic://", &p);
        url0_len = strlen(p);
        if (url0_len < hostname_size) {
            for (int i = 0; i < url0_len; i++) {
                if (*p == '/') {
                    break;
                }
                hostname[i] = *p;
                p++;
            }
        }
    }

    if (p = strstr(url, "cdnip=")) {
        av_strstart(p, "cdnip=", &p);
        url1_len = strlen(p);
        if (url1_len < ip_size) {
            for (int i = 0; i < url1_len; i++) {
                if (*p == '&') {
                    break;
                }
                ip[i] = *p;
                p++;
            }
        }
    }
}

static int quic_open_internal(URLContext *h)
{
    int ret = 0, header_val_len = 0, len = 0;
    uint64_t val = 0;
    char header_name[256] = {0}, *slash = NULL;
    const char *header_val = NULL;
    tBvcQuicHandler handler;
    QUICContext* s = h->priv_data;
    BvcQuicClientOptions opts;
    char headers[QUIC_HEADERS_SIZE] = {0};
    int64_t start_time = 0, end_time = 0;
    char hostname[1024] = {0}, quic_ip[1024] = {0};

    s->body_len = UINT64_MAX;

    quic_url_split(s->url, hostname, sizeof(hostname), quic_ip, sizeof(quic_ip));
    av_log(NULL, AV_LOG_INFO, "quic_open_internal quic_ip = %s\n", quic_ip);
    /* set default headers if needed */
    if (!has_header(s->headers, "\r\nuser-agent: "))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "user-agent: %s;", s->user_agent);

    len += av_strlcatf(headers + len, sizeof(headers) - len, "range: bytes=%"PRIu64"-;", s->body_off);

    /* now add in custom headers */
    if (s->headers)
        av_strlcpy(headers + len, s->headers, sizeof(headers) - len);

    av_log(NULL, AV_LOG_INFO, "quic_open_internal headers = %s\n", headers);

    opts.url                = s->url;
    opts.host               = quic_ip;
    opts.port               = s->host_port;
    opts.headers            = headers;
    opts.body               = s->body;
    opts.quic_version       = s->proto_version;
    opts.init_mtu           = s->init_mtu;
    opts.need_cert_verify   = s->need_cert_verify;
    opts.connect_timeout_ms = s->connect_timeout_us / 1000;
    opts.buffer_size        = s->recv_buffer_size;

    s->body_off         = 0;
    s->body_len         = UINT64_MAX;

    if (s->dns_cache_clear || strlen(quic_ip) < 7) {
        opts.host = NULL;
    }

    handler = bvc_quic_client_create(&opts);
    if (handler == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to create quic client handler.\n");
        return AVERROR(EINVAL);
    }

    start_time = av_gettime();
    av_application_will_http_open(s->app_ctx, (void*)h, opts.url, start_time, end_time);

    av_application_on_tcp_will_open(s->app_ctx);
    ret = bvc_quic_client_start(handler);
    av_application_quic_on_tcp_did_open(s->app_ctx, ret);

    if (ret != 0) {
        end_time = av_gettime();
        av_application_did_http_open(s->app_ctx, (void*)h, opts.url, ret, 0, 0, start_time, end_time);
        bvc_quic_client_destroy(handler);
        av_log(NULL, AV_LOG_ERROR, "quic_open_internal ECONNREFUSED ret = %d\n", ret);
        return AVERROR(ECONNREFUSED);
    }

    s->resp_code = bvc_quic_client_response_code(handler);

    av_log(NULL, AV_LOG_INFO, "quic_open_internal resp_code = %d\n", s->resp_code);
    if (s->resp_code != 200 && s->resp_code != 206) {
        end_time = av_gettime();
        av_application_did_http_open(s->app_ctx, (void*)h, opts.url, ret, s->resp_code, 0, start_time, end_time);
        bvc_quic_client_destroy(handler);
        return AVERROR(ECONNREFUSED);//TODO AVHTTP_ERRORCODE
    }

    // check seekable
    if (s->resp_code == 200) {
        strcpy(header_name, "accept-ranges");
        bvc_quic_client_response_header(handler, header_name, strlen(header_name), &header_val, &header_val_len);
        if (header_val != NULL && !av_strncasecmp(header_val, "bytes", 5)) {
            h->is_streamed = 0;
        }
    } else if (s->resp_code == 206) {
        h->is_streamed = 0;
    }

    strcpy(header_name, "content-range");
    bvc_quic_client_response_header(handler, header_name, strlen(header_name), &header_val, &header_val_len);
    if (header_val != NULL && !av_strncasecmp(header_val, "bytes ", 6)) {
        // body offset
        slash = strchr(header_val, '-');
        len = slash - (header_val + 6);
        if (slash && len > 0) {
            ret = quic_strtoi(header_val + 6, len, &val);
            if (!ret) {
                s->body_off = val;
            }
        }
        // body len
        slash = strchr(header_val, '/') + 1;
        len = header_val_len - (slash - header_val);
        if (slash && len > 0) {
            ret = quic_strtoi(slash, len, &val);
            if (!ret) {
                s->body_len = val;
            }
        }
    }

    av_log(NULL, AV_LOG_INFO, "quic_open_internal body_len = %lld\n", s->body_len);

    end_time = av_gettime();
    av_application_did_http_open(s->app_ctx, (void*)h, opts.url, ret, s->resp_code, s->body_len, start_time, end_time);

    s->handler = handler;

    return 0;
}

/* return non zero if error */
static int quic_open(URLContext *h, const char *uri, int flags)
{
    // const char quic_url_proto[] = "https";

    QUICContext* s = h->priv_data;

    s->handler = NULL;

    if( s->seekable == 1 )
        h->is_streamed = 0;
    else
        h->is_streamed = 1;

    s->app_ctx = (AVApplicationContext *)(intptr_t)s->app_ctx_intptr;
    // replace quic url to https url
    s->url = av_mallocz(strlen(uri) + 1);
    if (!s->url) {
        return AVERROR(ENOMEM);
    }
    // strcpy(s->url, quic_url_proto);
    strcpy(s->url, uri);

    return quic_open_internal(h);
}

static int quic_read_wait_timeout(URLContext *h)
{
    int64_t wait_start = 0;
    int buf_size = 0;
    AVIOInterruptCB *int_cb = &h->interrupt_callback;
    QUICContext* s = h->priv_data;
    int timeout = s->rw_timeout;
    if (h->flags & AVIO_FLAG_NONBLOCK) { // non-block no need to wait
        return 0;
    }

    while (1) {
        if (ff_check_interrupt(int_cb)) {
            return AVERROR_EXIT;
        }

        buf_size = bvc_quic_client_buffer_size(s->handler);
        if (buf_size < 0) {
            return AVERROR(EIO);
        } else if (buf_size > 0) {
            return 0;
        }

        if (timeout > 0) { // check timeout
            if (!wait_start) {
                wait_start = av_gettime_relative();
            } else if (av_gettime_relative() - wait_start > timeout) {
                return AVERROR_TCP_READ_TIMEOUT;
            }
        }
    }
    return AVERROR_EXIT; // never goes here
}

static int quic_read(URLContext *h, uint8_t *buf, int size)
{
    int ret = 0;
    QUICContext* s = h->priv_data;

    if ((s->handler == NULL) || (s->body_off >= s->body_len)) {
        return AVERROR_EOF;
    }
    ret = quic_read_wait_timeout(h);
    if (ret != 0) {
        return ret;
    }
    ret = bvc_quic_client_read(s->handler, buf, size);
    if (ret > 0) {
        s->body_off += ret;
        av_application_did_io_tcp_read(s->app_ctx, (void*)h, ret);
    } else if (ret == 0 && s->body_off < s->body_len) {
        av_log(h, AV_LOG_ERROR,
            "Quic stream ends prematurely at %"PRIu64", should be %"PRIu64"\n", s->body_off, s->body_len);
        return AVERROR(EIO);
    }

    return ret < 0 ? AVERROR(EIO) : ret;
}

static int quic_write(URLContext *h, const uint8_t *buf, int size)
{
    // No need quic_write for upper caller using, just fack return size.
    av_log(NULL, AV_LOG_WARNING, "Quic write procedure should not be called.\n");
    return size;
}

static int64_t quic_seek(URLContext *h, int64_t off, int whence)
{
    tBvcQuicHandler old_handler;
    uint64_t old_off = 0;
    char *headers = NULL;
    int64_t ret = 0;
    QUICContext *s = h->priv_data;
    int64_t start_time = 0, end_time = 0;

    old_off = s->body_off;
    old_handler = s->handler;
    if (whence == AVSEEK_SIZE) {
        return s->body_len;
    } else if ((whence == SEEK_CUR && off == 0) ||
               (whence == SEEK_SET && off == s->body_off)) {
        return s->body_off;
    } else if (s->body_len == UINT64_MAX && whence == SEEK_END) {
        return AVERROR(ENOSYS);
    }

    if (whence == SEEK_CUR) {
        off += s->body_off;
    } else if (whence == SEEK_END) {
        off += s->body_len;
    } else if (whence != SEEK_SET) {
        return AVERROR(EINVAL);
    }
    if (off < 0) {
        return AVERROR(EINVAL);
    }
    s->body_off = off;

    if (s->body_off && h->is_streamed) {
        return AVERROR(ENOSYS);
    }
    /* if it fails, continue on old connection */
    start_time = av_gettime();
    av_application_will_http_seek(s->app_ctx, (void*)h, s->url, off, start_time, end_time);
    s->handler = NULL;
    ret = quic_open_internal(h);
    av_freep(&headers);
    if (ret < 0) {
        end_time = av_gettime();
        av_application_did_http_seek(s->app_ctx, (void*)h, s->url, off, ret, s->resp_code, start_time, end_time);
        s->handler  = old_handler;
        s->body_off = old_off;
        return ret;
    }
    end_time = av_gettime();
    av_application_did_http_seek(s->app_ctx, (void*)h, s->url, off, ret, s->resp_code, start_time, end_time);
    bvc_quic_client_destroy(old_handler);

    return off;
}

static int quic_close(URLContext *h)
{
    QUICContext* s = h->priv_data;
    av_freep(&s->url);
    bvc_quic_client_destroy(s->handler);
    return 0;
}

static int quic_get_window_size(URLContext *h)
{
    const int kDefaultWindowSize = 16384; // 16k
    int buf_size = 0;
    QUICContext* s = h->priv_data;
    buf_size = bvc_quic_client_buffer_size(s->handler);

    return (buf_size <= 0 ? kDefaultWindowSize : buf_size);
}

const URLProtocol ff_quic_protocol = {
    .name                = "quic",
    .url_open            = quic_open,
    .url_read            = quic_read,
    .url_write           = quic_write,
    .url_seek            = quic_seek,
    .url_close           = quic_close,
    .url_get_short_seek  = quic_get_window_size,
    .priv_data_size      = sizeof(QUICContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &quic_class,
};
