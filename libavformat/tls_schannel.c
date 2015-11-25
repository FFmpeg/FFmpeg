/*
 * Copyright (c) 2015 Hendrik Leppkes
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

/** Based on the CURL SChannel module */

#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include "tls.h"

#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schnlsp.h>

#define SCHANNEL_INITIAL_BUFFER_SIZE   4096
#define SCHANNEL_FREE_BUFFER_SIZE      1024

/* mingw does not define this symbol */
#ifndef SECBUFFER_ALERT
#define SECBUFFER_ALERT                17
#endif

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;

    CredHandle cred_handle;
    TimeStamp cred_timestamp;

    CtxtHandle ctxt_handle;
    TimeStamp ctxt_timestamp;

    ULONG request_flags;
    ULONG context_flags;

    uint8_t *enc_buf;
    int enc_buf_size;
    int enc_buf_offset;

    uint8_t *dec_buf;
    int dec_buf_size;
    int dec_buf_offset;

    SecPkgContext_StreamSizes sizes;

    int connected;
    int connection_closed;
    int sspi_close_notify;
} TLSContext;

static void init_sec_buffer(SecBuffer *buffer, unsigned long type,
                            void *data, unsigned long size)
{
    buffer->cbBuffer   = size;
    buffer->BufferType = type;
    buffer->pvBuffer   = data;
}

static void init_sec_buffer_desc(SecBufferDesc *desc, SecBuffer *buffers,
                                 unsigned long buffer_count)
{
    desc->ulVersion = SECBUFFER_VERSION;
    desc->pBuffers = buffers;
    desc->cBuffers = buffer_count;
}

static int tls_shutdown_client(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    int ret;

    if (c->connected) {
        SecBufferDesc BuffDesc;
        SecBuffer Buffer;
        SECURITY_STATUS sspi_ret;
        SecBuffer outbuf;
        SecBufferDesc outbuf_desc;

        DWORD dwshut = SCHANNEL_SHUTDOWN;
        init_sec_buffer(&Buffer, SECBUFFER_TOKEN, &dwshut, sizeof(dwshut));
        init_sec_buffer_desc(&BuffDesc, &Buffer, 1);

        sspi_ret = ApplyControlToken(&c->ctxt_handle, &BuffDesc);
        if (sspi_ret != SEC_E_OK)
            av_log(h, AV_LOG_ERROR, "ApplyControlToken failed\n");

        init_sec_buffer(&outbuf, SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&outbuf_desc, &outbuf, 1);

        sspi_ret = InitializeSecurityContext(&c->cred_handle, &c->ctxt_handle, s->host,
                                             c->request_flags, 0, 0, NULL, 0, &c->ctxt_handle,
                                             &outbuf_desc, &c->context_flags, &c->ctxt_timestamp);
        if (sspi_ret == SEC_E_OK || sspi_ret == SEC_I_CONTEXT_EXPIRED) {
            ret = ffurl_write(s->tcp, outbuf.pvBuffer, outbuf.cbBuffer);
            FreeContextBuffer(outbuf.pvBuffer);
            if (ret < 0 || ret != outbuf.cbBuffer)
                av_log(h, AV_LOG_ERROR, "Failed to send close message\n");
        }

        c->connected = 0;
    }
    return 0;
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;

    tls_shutdown_client(h);

    DeleteSecurityContext(&c->ctxt_handle);
    FreeCredentialsHandle(&c->cred_handle);

    av_freep(&c->enc_buf);
    c->enc_buf_size = c->enc_buf_offset = 0;

    av_freep(&c->dec_buf);
    c->dec_buf_size = c->dec_buf_offset = 0;

    if (c->tls_shared.tcp)
        ffurl_close(c->tls_shared.tcp);
    return 0;
}

static int tls_client_handshake_loop(URLContext *h, int initial)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret;
    SecBuffer outbuf[3];
    SecBufferDesc outbuf_desc;
    SecBuffer inbuf[2];
    SecBufferDesc inbuf_desc;
    int i, ret = 0, read_data = initial;

    if (c->enc_buf == NULL) {
        c->enc_buf_offset = 0;
        ret = av_reallocp(&c->enc_buf, SCHANNEL_INITIAL_BUFFER_SIZE);
        if (ret < 0)
            goto fail;
        c->enc_buf_size = SCHANNEL_INITIAL_BUFFER_SIZE;
    }

    if (c->dec_buf == NULL) {
        c->dec_buf_offset = 0;
        ret = av_reallocp(&c->dec_buf, SCHANNEL_INITIAL_BUFFER_SIZE);
        if (ret < 0)
            goto fail;
        c->dec_buf_size = SCHANNEL_INITIAL_BUFFER_SIZE;
    }

    while (1) {
        if (c->enc_buf_size - c->enc_buf_offset < SCHANNEL_FREE_BUFFER_SIZE) {
            c->enc_buf_size = c->enc_buf_offset + SCHANNEL_FREE_BUFFER_SIZE;
            ret = av_reallocp(&c->enc_buf, c->enc_buf_size);
            if (ret < 0) {
                c->enc_buf_size = c->enc_buf_offset = 0;
                goto fail;
            }
        }

        if (read_data) {
            ret = ffurl_read(c->tls_shared.tcp, c->enc_buf + c->enc_buf_offset,
                             c->enc_buf_size - c->enc_buf_offset);
            if (ret < 0) {
                av_log(h, AV_LOG_ERROR, "Failed to read handshake response\n");
                goto fail;
            }
            c->enc_buf_offset += ret;
        }

        /* input buffers */
        init_sec_buffer(&inbuf[0], SECBUFFER_TOKEN, av_malloc(c->enc_buf_offset), c->enc_buf_offset);
        init_sec_buffer(&inbuf[1], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&inbuf_desc, inbuf, 2);

        if (inbuf[0].pvBuffer == NULL) {
            av_log(h, AV_LOG_ERROR, "Failed to allocate input buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        memcpy(inbuf[0].pvBuffer, c->enc_buf, c->enc_buf_offset);

        /* output buffers */
        init_sec_buffer(&outbuf[0], SECBUFFER_TOKEN, NULL, 0);
        init_sec_buffer(&outbuf[1], SECBUFFER_ALERT, NULL, 0);
        init_sec_buffer(&outbuf[2], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&outbuf_desc, outbuf, 3);

        sspi_ret = InitializeSecurityContext(&c->cred_handle, &c->ctxt_handle, s->host, c->request_flags,
                                             0, 0, &inbuf_desc, 0, NULL, &outbuf_desc, &c->context_flags,
                                             &c->ctxt_timestamp);
        av_freep(&inbuf[0].pvBuffer);

        if (sspi_ret == SEC_E_INCOMPLETE_MESSAGE) {
            av_log(h, AV_LOG_DEBUG, "Received incomplete handshake, need more data\n");
            read_data = 1;
            continue;
        }

        /* remote requests a client certificate - attempt to continue without one anyway */
        if (sspi_ret == SEC_I_INCOMPLETE_CREDENTIALS &&
            !(c->request_flags & ISC_REQ_USE_SUPPLIED_CREDS)) {
            av_log(h, AV_LOG_VERBOSE, "Client certificate has been requested, ignoring\n");
            c->request_flags |= ISC_REQ_USE_SUPPLIED_CREDS;
            read_data = 0;
            continue;
        }

        /* continue handshake */
        if (sspi_ret == SEC_I_CONTINUE_NEEDED || sspi_ret == SEC_E_OK) {
            for (i = 0; i < 3; i++) {
                if (outbuf[i].BufferType == SECBUFFER_TOKEN && outbuf[i].cbBuffer > 0) {
                    ret = ffurl_write(c->tls_shared.tcp, outbuf[i].pvBuffer, outbuf[i].cbBuffer);
                    if (ret < 0 || ret != outbuf[i].cbBuffer) {
                        av_log(h, AV_LOG_VERBOSE, "Failed to send handshake data\n");
                        ret = AVERROR(EIO);
                        goto fail;
                    }
                }

                if (outbuf[i].pvBuffer != NULL) {
                    FreeContextBuffer(outbuf[i].pvBuffer);
                    outbuf[i].pvBuffer = NULL;
                }
            }
        } else {
            if (sspi_ret == SEC_E_WRONG_PRINCIPAL)
                av_log(h, AV_LOG_ERROR, "SNI or certificate check failed\n");
            else
                av_log(h, AV_LOG_ERROR, "Creating security context failed (0x%lx)\n", sspi_ret);
            ret = AVERROR_UNKNOWN;
            goto fail;
        }

        if (inbuf[1].BufferType == SECBUFFER_EXTRA && inbuf[1].cbBuffer > 0) {
            if (c->enc_buf_offset > inbuf[1].cbBuffer) {
                memmove(c->enc_buf, (c->enc_buf + c->enc_buf_offset) - inbuf[1].cbBuffer,
                        inbuf[1].cbBuffer);
                c->enc_buf_offset = inbuf[1].cbBuffer;
                if (sspi_ret == SEC_I_CONTINUE_NEEDED) {
                    read_data = 0;
                    continue;
                }
            }
        } else {
            c->enc_buf_offset  = 0;
        }

        if (sspi_ret == SEC_I_CONTINUE_NEEDED) {
            read_data = 1;
            continue;
        }

        break;
    }

    return 0;

fail:
    /* free any remaining output data */
    for (i = 0; i < 3; i++) {
        if (outbuf[i].pvBuffer != NULL) {
            FreeContextBuffer(outbuf[i].pvBuffer);
            outbuf[i].pvBuffer = NULL;
        }
    }

    return ret;
}

static int tls_client_handshake(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SecBuffer outbuf;
    SecBufferDesc outbuf_desc;
    SECURITY_STATUS sspi_ret;
    int ret;

    init_sec_buffer(&outbuf, SECBUFFER_EMPTY, NULL, 0);
    init_sec_buffer_desc(&outbuf_desc, &outbuf, 1);

    c->request_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                       ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
                       ISC_REQ_STREAM;

    sspi_ret = InitializeSecurityContext(&c->cred_handle, NULL, s->host, c->request_flags, 0, 0,
                                         NULL, 0, &c->ctxt_handle, &outbuf_desc, &c->context_flags,
                                         &c->ctxt_timestamp);
    if (sspi_ret != SEC_I_CONTINUE_NEEDED) {
        av_log(h, AV_LOG_ERROR, "Unable to create initial security context (0x%lx)\n", sspi_ret);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    ret = ffurl_write(s->tcp, outbuf.pvBuffer, outbuf.cbBuffer);
    FreeContextBuffer(outbuf.pvBuffer);
    if (ret < 0 || ret != outbuf.cbBuffer) {
        av_log(h, AV_LOG_ERROR, "Failed to send initial handshake data\n");
        ret = AVERROR(EIO);
        goto fail;
    }

    return tls_client_handshake_loop(h, 1);

fail:
    DeleteSecurityContext(&c->ctxt_handle);
    return ret;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret;
    SCHANNEL_CRED schannel_cred = { 0 };
    int ret;

    if ((ret = ff_tls_open_underlying(s, h, uri, options)) < 0)
        goto fail;

    if (s->listen) {
        av_log(h, AV_LOG_ERROR, "TLS Listen Sockets with SChannel is not implemented.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* SChannel Options */
    schannel_cred.dwVersion = SCHANNEL_CRED_VERSION;

    if (s->verify)
        schannel_cred.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION |
                                SCH_CRED_REVOCATION_CHECK_CHAIN;
    else
        schannel_cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
                                SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                                SCH_CRED_IGNORE_REVOCATION_OFFLINE;

    /* Get credential handle */
    sspi_ret = AcquireCredentialsHandle(NULL, (TCHAR *)UNISP_NAME, SECPKG_CRED_OUTBOUND,
                                        NULL,  &schannel_cred, NULL, NULL, &c->cred_handle,
                                        &c->cred_timestamp);
    if (sspi_ret != SEC_E_OK) {
        av_log(h, AV_LOG_ERROR, "Unable to acquire security credentials (0x%lx)\n", sspi_ret);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    ret = tls_client_handshake(h);
    if (ret < 0)
        goto fail;

    c->connected = 1;

    return 0;

fail:
    tls_close(h);
    return ret;
}

static int tls_read(URLContext *h, uint8_t *buf, int len)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret = SEC_E_OK;
    SecBuffer inbuf[4];
    SecBufferDesc inbuf_desc;
    int size, ret;
    int min_enc_buf_size = len + SCHANNEL_FREE_BUFFER_SIZE;

    if (len <= c->dec_buf_offset)
        goto cleanup;

    if (c->sspi_close_notify)
        goto cleanup;

    if (!c->connection_closed) {
        size = c->enc_buf_size - c->enc_buf_offset;
        if (size < SCHANNEL_FREE_BUFFER_SIZE || c->enc_buf_size < min_enc_buf_size) {
            c->enc_buf_size = c->enc_buf_offset + SCHANNEL_FREE_BUFFER_SIZE;
            if (c->enc_buf_size < min_enc_buf_size)
                c->enc_buf_size = min_enc_buf_size;
            ret = av_reallocp(&c->enc_buf, c->enc_buf_size);
            if (ret < 0) {
                c->enc_buf_size = c->enc_buf_offset = 0;
                return ret;
            }
        }

        ret = ffurl_read(s->tcp, c->enc_buf + c->enc_buf_offset,
                         c->enc_buf_size - c->enc_buf_offset);
        if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Unable to read from socket\n");
            return ret;
        } else if (ret == 0)
            c->connection_closed = 1;

        c->enc_buf_offset += ret;
    }

    while (c->enc_buf_offset > 0 && sspi_ret == SEC_E_OK && c->dec_buf_offset < len) {
        /*  input buffer */
        init_sec_buffer(&inbuf[0], SECBUFFER_DATA, c->enc_buf, c->enc_buf_offset);

        /* additional buffers for possible output */
        init_sec_buffer(&inbuf[1], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer(&inbuf[2], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer(&inbuf[3], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&inbuf_desc, inbuf, 4);

        sspi_ret = DecryptMessage(&c->ctxt_handle, &inbuf_desc, 0, NULL);
        if (sspi_ret == SEC_E_OK || sspi_ret == SEC_I_RENEGOTIATE ||
            sspi_ret == SEC_I_CONTEXT_EXPIRED) {
            /* handle decrypted data */
            if (inbuf[1].BufferType == SECBUFFER_DATA) {
                /* grow buffer if needed */
                size = inbuf[1].cbBuffer > SCHANNEL_FREE_BUFFER_SIZE ?
                       inbuf[1].cbBuffer : SCHANNEL_FREE_BUFFER_SIZE;
                if (c->dec_buf_size - c->dec_buf_offset < size || c->dec_buf_size < len)  {
                    c->dec_buf_size = c->dec_buf_offset + size;
                    if (c->dec_buf_size < len)
                        c->dec_buf_size = len;
                    ret = av_reallocp(&c->dec_buf, c->dec_buf_size);
                    if (ret < 0) {
                        c->dec_buf_size = c->dec_buf_offset = 0;
                        return ret;
                    }
                }

                /* copy decrypted data to buffer */
                size = inbuf[1].cbBuffer;
                if (size) {
                    memcpy(c->dec_buf + c->dec_buf_offset, inbuf[1].pvBuffer, size);
                    c->dec_buf_offset += size;
                }
            }
            if (inbuf[3].BufferType == SECBUFFER_EXTRA && inbuf[3].cbBuffer > 0) {
                if (c->enc_buf_offset > inbuf[3].cbBuffer) {
                    memmove(c->enc_buf, (c->enc_buf + c->enc_buf_offset) - inbuf[3].cbBuffer,
                    inbuf[3].cbBuffer);
                    c->enc_buf_offset = inbuf[3].cbBuffer;
                }
            } else
                c->enc_buf_offset = 0;

            if (sspi_ret == SEC_I_RENEGOTIATE) {
                if (c->enc_buf_offset) {
                    av_log(h, AV_LOG_ERROR, "Cannot renegotiate, encrypted data buffer not empty\n");
                    ret = AVERROR_UNKNOWN;
                    goto cleanup;
                }

                av_log(h, AV_LOG_VERBOSE, "Re-negotiating security context\n");
                ret = tls_client_handshake_loop(h, 0);
                if (ret < 0) {
                    goto cleanup;
                }
                sspi_ret = SEC_E_OK;
                continue;
            } else if (sspi_ret == SEC_I_CONTEXT_EXPIRED) {
                c->sspi_close_notify = 1;
                if (!c->connection_closed) {
                    c->connection_closed = 1;
                    av_log(h, AV_LOG_VERBOSE, "Server closed the connection\n");
                }
                ret = 0;
                goto cleanup;
            }
        } else if (sspi_ret == SEC_E_INCOMPLETE_MESSAGE) {
            ret = AVERROR(EAGAIN);
            goto cleanup;
        } else {
            av_log(h, AV_LOG_ERROR, "Unable to decrypt message\n");
            ret = AVERROR(EIO);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    size = FFMIN(len, c->dec_buf_offset);
    if (size) {
        memcpy(buf, c->dec_buf, size);
        memmove(c->dec_buf, c->dec_buf + size, c->dec_buf_offset - size);
        c->dec_buf_offset -= size;

        return size;
    }

    if (ret == 0 && !c->connection_closed)
        ret = AVERROR(EAGAIN);

    return ret < 0 ? ret : 0;
}

static int tls_write(URLContext *h, const uint8_t *buf, int len)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret;
    int ret = 0, data_size;
    uint8_t *data = NULL;
    SecBuffer outbuf[4];
    SecBufferDesc outbuf_desc;

    if (c->sizes.cbMaximumMessage == 0) {
        sspi_ret = QueryContextAttributes(&c->ctxt_handle, SECPKG_ATTR_STREAM_SIZES, &c->sizes);
        if (sspi_ret != SEC_E_OK)
            return AVERROR_UNKNOWN;
    }

    /* limit how much data we can consume */
    len = FFMIN(len, c->sizes.cbMaximumMessage);

    data_size = c->sizes.cbHeader + len + c->sizes.cbTrailer;
    data = av_malloc(data_size);
    if (data == NULL)
        return AVERROR(ENOMEM);

    init_sec_buffer(&outbuf[0], SECBUFFER_STREAM_HEADER,
                  data, c->sizes.cbHeader);
    init_sec_buffer(&outbuf[1], SECBUFFER_DATA,
                  data + c->sizes.cbHeader, len);
    init_sec_buffer(&outbuf[2], SECBUFFER_STREAM_TRAILER,
                  data + c->sizes.cbHeader + len,
                  c->sizes.cbTrailer);
    init_sec_buffer(&outbuf[3], SECBUFFER_EMPTY, NULL, 0);
    init_sec_buffer_desc(&outbuf_desc, outbuf, 4);

    memcpy(outbuf[1].pvBuffer, buf, len);

    sspi_ret = EncryptMessage(&c->ctxt_handle, 0, &outbuf_desc, 0);
    if (sspi_ret == SEC_E_OK)  {
        len = outbuf[0].cbBuffer + outbuf[1].cbBuffer + outbuf[2].cbBuffer;
        ret = ffurl_write(s->tcp, data, len);
        if (ret < 0 || ret != len) {
            ret = AVERROR(EIO);
            av_log(h, AV_LOG_ERROR, "Writing encrypted data to socket failed\n");
            goto done;
        }
    } else {
        av_log(h, AV_LOG_ERROR, "Encrypting data failed\n");
        if (sspi_ret == SEC_E_INSUFFICIENT_MEMORY)
            ret = AVERROR(ENOMEM);
        else
            ret = AVERROR(EIO);
        goto done;
    }

done:
    av_freep(&data);
    return ret < 0 ? ret : outbuf[1].cbBuffer;
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

URLProtocol ff_tls_schannel_protocol = {
    .name           = "tls",
    .url_open2      = tls_open,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
