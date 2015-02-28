/*
 * Decryption protocol handler
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
#include "libavutil/aes.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "url.h"

#define MAX_BUFFER_BLOCKS 150
#define BLOCKSIZE 16

typedef struct CryptoContext {
    const AVClass *class;
    URLContext *hd;
    uint8_t inbuffer [BLOCKSIZE*MAX_BUFFER_BLOCKS],
            outbuffer[BLOCKSIZE*MAX_BUFFER_BLOCKS];
    uint8_t *outptr;
    int indata, indata_used, outdata;
    int eof;
    uint8_t *key;
    int keylen;
    uint8_t *iv;
    int ivlen;
    struct AVAES *aes;
} CryptoContext;

#define OFFSET(x) offsetof(CryptoContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"key", "AES decryption key", OFFSET(key), AV_OPT_TYPE_BINARY, .flags = D },
    {"iv",  "AES decryption initialization vector", OFFSET(iv), AV_OPT_TYPE_BINARY, .flags = D },
    { NULL }
};

static const AVClass crypto_class = {
    .class_name     = "crypto",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int crypto_open(URLContext *h, const char *uri, int flags)
{
    const char *nested_url;
    int ret = 0;
    CryptoContext *c = h->priv_data;

    if (!av_strstart(uri, "crypto+", &nested_url) &&
        !av_strstart(uri, "crypto:", &nested_url)) {
        av_log(h, AV_LOG_ERROR, "Unsupported url %s\n", uri);
        ret = AVERROR(EINVAL);
        goto err;
    }

    if (c->keylen < BLOCKSIZE || c->ivlen < BLOCKSIZE) {
        av_log(h, AV_LOG_ERROR, "Key or IV not set\n");
        ret = AVERROR(EINVAL);
        goto err;
    }
    if (flags & AVIO_FLAG_WRITE) {
        av_log(h, AV_LOG_ERROR, "Only decryption is supported currently\n");
        ret = AVERROR(ENOSYS);
        goto err;
    }
    if ((ret = ffurl_open(&c->hd, nested_url, AVIO_FLAG_READ,
                          &h->interrupt_callback, NULL, h->protocols, h)) < 0) {
        av_log(h, AV_LOG_ERROR, "Unable to open input\n");
        goto err;
    }
    c->aes = av_aes_alloc();
    if (!c->aes) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    av_aes_init(c->aes, c->key, 128, 1);

    h->is_streamed = 1;

err:
    return ret;
}

static int crypto_read(URLContext *h, uint8_t *buf, int size)
{
    CryptoContext *c = h->priv_data;
    int blocks;
retry:
    if (c->outdata > 0) {
        size = FFMIN(size, c->outdata);
        memcpy(buf, c->outptr, size);
        c->outptr  += size;
        c->outdata -= size;
        return size;
    }
    // We avoid using the last block until we've found EOF,
    // since we'll remove PKCS7 padding at the end. So make
    // sure we've got at least 2 blocks, so we can decrypt
    // at least one.
    while (c->indata - c->indata_used < 2*BLOCKSIZE) {
        int n = ffurl_read(c->hd, c->inbuffer + c->indata,
                           sizeof(c->inbuffer) - c->indata);
        if (n <= 0) {
            c->eof = 1;
            break;
        }
        c->indata += n;
    }
    blocks = (c->indata - c->indata_used) / BLOCKSIZE;
    if (!blocks)
        return AVERROR_EOF;
    if (!c->eof)
        blocks--;
    av_aes_crypt(c->aes, c->outbuffer, c->inbuffer + c->indata_used, blocks,
                 c->iv, 1);
    c->outdata      = BLOCKSIZE * blocks;
    c->outptr       = c->outbuffer;
    c->indata_used += BLOCKSIZE * blocks;
    if (c->indata_used >= sizeof(c->inbuffer)/2) {
        memmove(c->inbuffer, c->inbuffer + c->indata_used,
                c->indata - c->indata_used);
        c->indata     -= c->indata_used;
        c->indata_used = 0;
    }
    if (c->eof) {
        // Remove PKCS7 padding at the end
        int padding = c->outbuffer[c->outdata - 1];
        c->outdata -= padding;
    }
    goto retry;
}

static int crypto_close(URLContext *h)
{
    CryptoContext *c = h->priv_data;
    if (c->hd)
        ffurl_close(c->hd);
    av_freep(&c->aes);
    return 0;
}

const URLProtocol ff_crypto_protocol = {
    .name            = "crypto",
    .url_open        = crypto_open,
    .url_read        = crypto_read,
    .url_close       = crypto_close,
    .priv_data_size  = sizeof(CryptoContext),
    .priv_data_class = &crypto_class,
    .flags           = URL_PROTOCOL_FLAG_NESTED_SCHEME,
};
