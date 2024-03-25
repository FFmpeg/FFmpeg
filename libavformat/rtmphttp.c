/*
 * RTMP HTTP network protocol
 * Copyright (c) 2012 Samuel Pitoiset
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

/**
 * @file
 * RTMP HTTP protocol
 */

#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "http.h"
#include "rtmp.h"

#define RTMPT_DEFAULT_PORT 80
#define RTMPTS_DEFAULT_PORT RTMPS_DEFAULT_PORT

/* protocol handler context */
typedef struct RTMP_HTTPContext {
    const AVClass *class;
    URLContext   *stream;           ///< HTTP stream
    char         host[256];         ///< hostname of the server
    int          port;              ///< port to connect (default is 80)
    char         client_id[64];     ///< client ID used for all requests except the first one
    int          seq;               ///< sequence ID used for all requests
    uint8_t      *out_data;         ///< output buffer
    int          out_size;          ///< current output buffer size
    int          out_capacity;      ///< current output buffer capacity
    int          initialized;       ///< flag indicating when the http context is initialized
    int          finishing;         ///< flag indicating when the client closes the connection
    int          nb_bytes_read;     ///< number of bytes read since the last request
    int          tls;               ///< use Transport Security Layer (RTMPTS)
} RTMP_HTTPContext;

static int rtmp_http_send_cmd(URLContext *h, const char *cmd)
{
    RTMP_HTTPContext *rt = h->priv_data;
    char uri[2048];
    uint8_t c;
    int ret;

    ff_url_join(uri, sizeof(uri), "http", NULL, rt->host, rt->port,
                "/%s/%s/%d", cmd, rt->client_id, rt->seq++);

    av_opt_set_bin(rt->stream->priv_data, "post_data", rt->out_data,
                   rt->out_size, 0);

    /* send a new request to the server */
    if ((ret = ff_http_do_new_request(rt->stream, uri)) < 0)
        return ret;

    /* re-init output buffer */
    rt->out_size = 0;

    /* read the first byte which contains the polling interval */
    if ((ret = ffurl_read(rt->stream, &c, 1)) < 0)
        return ret;

    /* re-init the number of bytes read */
    rt->nb_bytes_read = 0;

    return ret;
}

static int rtmp_http_write(URLContext *h, const uint8_t *buf, int size)
{
    RTMP_HTTPContext *rt = h->priv_data;

    if (rt->out_size + size > rt->out_capacity) {
        int err;
        rt->out_capacity = (rt->out_size + size) * 2;
        if ((err = av_reallocp(&rt->out_data, rt->out_capacity)) < 0) {
            rt->out_size = 0;
            rt->out_capacity = 0;
            return err;
        }
    }

    memcpy(rt->out_data + rt->out_size, buf, size);
    rt->out_size += size;

    return size;
}

static int rtmp_http_read(URLContext *h, uint8_t *buf, int size)
{
    RTMP_HTTPContext *rt = h->priv_data;
    int ret, off = 0;

    /* try to read at least 1 byte of data */
    do {
        ret = ffurl_read(rt->stream, buf + off, size);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;

        if (!ret || ret == AVERROR_EOF) {
            if (rt->finishing) {
                /* Do not send new requests when the client wants to
                 * close the connection. */
                return AVERROR(EAGAIN);
            }

            /* When the client has reached end of file for the last request,
             * we have to send a new request if we have buffered data.
             * Otherwise, we have to send an idle POST. */
            if (rt->out_size > 0) {
                if ((ret = rtmp_http_send_cmd(h, "send")) < 0)
                    return ret;
            } else {
                if (rt->nb_bytes_read == 0) {
                    /* Wait 50ms before retrying to read a server reply in
                     * order to reduce the number of idle requests. */
                    av_usleep(50000);
                }

                if ((ret = rtmp_http_write(h, "", 1)) < 0)
                    return ret;

                if ((ret = rtmp_http_send_cmd(h, "idle")) < 0)
                    return ret;
            }

            if (h->flags & AVIO_FLAG_NONBLOCK) {
                /* no incoming data to handle in nonblocking mode */
                return AVERROR(EAGAIN);
            }
        } else {
            off  += ret;
            size -= ret;
            rt->nb_bytes_read += ret;
        }
    } while (off <= 0);

    return off;
}

static int rtmp_http_close(URLContext *h)
{
    RTMP_HTTPContext *rt = h->priv_data;
    uint8_t tmp_buf[2048];
    int ret = 0;

    if (rt->initialized) {
        /* client wants to close the connection */
        rt->finishing = 1;

        do {
            ret = rtmp_http_read(h, tmp_buf, sizeof(tmp_buf));
        } while (ret > 0);

        /* re-init output buffer before sending the close command */
        rt->out_size = 0;

        if ((ret = rtmp_http_write(h, "", 1)) == 1)
            ret = rtmp_http_send_cmd(h, "close");
    }

    av_freep(&rt->out_data);
    ffurl_closep(&rt->stream);

    return ret;
}

static int rtmp_http_open(URLContext *h, const char *uri, int flags)
{
    RTMP_HTTPContext *rt = h->priv_data;
    char headers[1024], url[1024];
    int ret, off = 0;

    av_url_split(NULL, 0, NULL, 0, rt->host, sizeof(rt->host), &rt->port,
                 NULL, 0, uri);

    /* This is the first request that is sent to the server in order to
     * register a client on the server and start a new session. The server
     * replies with a unique id (usually a number) that is used by the client
     * for all future requests.
     * Note: the reply doesn't contain a value for the polling interval.
     * A successful connect resets the consecutive index that is used
     * in the URLs. */
    if (rt->tls) {
        if (rt->port < 0)
            rt->port = RTMPTS_DEFAULT_PORT;
        ff_url_join(url, sizeof(url), "https", NULL, rt->host, rt->port, "/open/1");
    } else {
        if (rt->port < 0)
            rt->port = RTMPT_DEFAULT_PORT;
        ff_url_join(url, sizeof(url), "http", NULL, rt->host, rt->port, "/open/1");
    }

    /* alloc the http context */
    if ((ret = ffurl_alloc(&rt->stream, url, AVIO_FLAG_READ_WRITE, &h->interrupt_callback)) < 0)
        goto fail;

    /* set options */
    snprintf(headers, sizeof(headers),
             "Cache-Control: no-cache\r\n"
             "Content-type: application/x-fcs\r\n"
             "User-Agent: Shockwave Flash\r\n");
    av_opt_set(rt->stream->priv_data, "headers", headers, 0);
    av_opt_set(rt->stream->priv_data, "multiple_requests", "1", 0);
    av_opt_set_bin(rt->stream->priv_data, "post_data", "", 1, 0);

    if (!rt->stream->protocol_whitelist && h->protocol_whitelist) {
        rt->stream->protocol_whitelist = av_strdup(h->protocol_whitelist);
        if (!rt->stream->protocol_whitelist) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* open the http context */
    if ((ret = ffurl_connect(rt->stream, NULL)) < 0)
        goto fail;

    /* read the server reply which contains a unique ID */
    for (;;) {
        ret = ffurl_read(rt->stream, rt->client_id + off, sizeof(rt->client_id) - off);
        if (!ret || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            goto fail;
        off += ret;
        if (off == sizeof(rt->client_id)) {
            ret = AVERROR(EIO);
            goto fail;
        }
    }
    while (off > 0 && av_isspace(rt->client_id[off - 1]))
        off--;
    rt->client_id[off] = '\0';

    /* http context is now initialized */
    rt->initialized = 1;
    return 0;

fail:
    rtmp_http_close(h);
    return ret;
}

#define OFFSET(x) offsetof(RTMP_HTTPContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption ffrtmphttp_options[] = {
    {"ffrtmphttp_tls", "Use a HTTPS tunneling connection (RTMPTS).", OFFSET(tls), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC},
    { NULL },
};

static const AVClass ffrtmphttp_class = {
    .class_name = "ffrtmphttp",
    .item_name  = av_default_item_name,
    .option     = ffrtmphttp_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_ffrtmphttp_protocol = {
    .name           = "ffrtmphttp",
    .url_open       = rtmp_http_open,
    .url_read       = rtmp_http_read,
    .url_write      = rtmp_http_write,
    .url_close      = rtmp_http_close,
    .priv_data_size = sizeof(RTMP_HTTPContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class= &ffrtmphttp_class,
    .default_whitelist = "https,http,tcp,tls",
};
