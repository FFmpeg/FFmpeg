/*
 * Decryption protocol handler
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
    uint8_t *decrypt_key;
    int decrypt_keylen;
    uint8_t *decrypt_iv;
    int decrypt_ivlen;
    uint8_t *encrypt_key;
    int encrypt_keylen;
    uint8_t *encrypt_iv;
    int encrypt_ivlen;
    struct AVAES *aes_decrypt;
    struct AVAES *aes_encrypt;

    uint8_t pad[BLOCKSIZE];
    int pad_len;

} CryptoContext;

#define OFFSET(x) offsetof(CryptoContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"key", "AES encryption/decryption key",                   OFFSET(key),         AV_OPT_TYPE_BINARY, .flags = D|E },
    {"iv",  "AES encryption/decryption initialization vector", OFFSET(iv),          AV_OPT_TYPE_BINARY, .flags = D|E },
    {"decryption_key", "AES decryption key",                   OFFSET(decrypt_key), AV_OPT_TYPE_BINARY, .flags = D },
    {"decryption_iv",  "AES decryption initialization vector", OFFSET(decrypt_iv),  AV_OPT_TYPE_BINARY, .flags = D },
    {"encryption_key", "AES encryption key",                   OFFSET(encrypt_key), AV_OPT_TYPE_BINARY, .flags = E },
    {"encryption_iv",  "AES encryption initialization vector", OFFSET(encrypt_iv),  AV_OPT_TYPE_BINARY, .flags = E },
    { NULL }
};

static const AVClass crypto_class = {
    .class_name     = "crypto",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int set_aes_arg(CryptoContext *c, uint8_t **buf, int *buf_len,
                       uint8_t *default_buf, int default_buf_len,
                       const char *desc)
{
    if (!*buf_len) {
        if (!default_buf_len) {
            av_log(c, AV_LOG_ERROR, "%s not set\n", desc);
            return AVERROR(EINVAL);
        } else if (default_buf_len != BLOCKSIZE) {
            av_log(c, AV_LOG_ERROR,
                   "invalid %s size (%d bytes, block size is %d)\n",
                   desc, default_buf_len, BLOCKSIZE);
            return AVERROR(EINVAL);
        }
        *buf = av_memdup(default_buf, default_buf_len);
        if (!*buf)
            return AVERROR(ENOMEM);
        *buf_len = default_buf_len;
    } else if (*buf_len != BLOCKSIZE) {
        av_log(c, AV_LOG_ERROR,
               "invalid %s size (%d bytes, block size is %d)\n",
               desc, *buf_len, BLOCKSIZE);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int crypto_open2(URLContext *h, const char *uri, int flags, AVDictionary **options)
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

    if (flags & AVIO_FLAG_READ) {
        if ((ret = set_aes_arg(c, &c->decrypt_key, &c->decrypt_keylen,
                               c->key, c->keylen, "decryption key")) < 0)
            goto err;
        if ((ret = set_aes_arg(c, &c->decrypt_iv, &c->decrypt_ivlen,
                               c->iv, c->ivlen, "decryption IV")) < 0)
            goto err;
    }

    if (flags & AVIO_FLAG_WRITE) {
        if ((ret = set_aes_arg(c, &c->encrypt_key, &c->encrypt_keylen,
                               c->key, c->keylen, "encryption key")) < 0)
        if (ret < 0)
            goto err;
        if ((ret = set_aes_arg(c, &c->encrypt_iv, &c->encrypt_ivlen,
                               c->iv, c->ivlen, "encryption IV")) < 0)
            goto err;
    }

    if ((ret = ffurl_open_whitelist(&c->hd, nested_url, flags,
                                    &h->interrupt_callback, options,
                                    h->protocol_whitelist)) < 0) {
        av_log(h, AV_LOG_ERROR, "Unable to open resource: %s\n", nested_url);
        goto err;
    }

    if (flags & AVIO_FLAG_READ) {
        c->aes_decrypt = av_aes_alloc();
        if (!c->aes_decrypt) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        ret = av_aes_init(c->aes_decrypt, c->decrypt_key, BLOCKSIZE*8, 1);
        if (ret < 0)
            goto err;
    }

    if (flags & AVIO_FLAG_WRITE) {
        c->aes_encrypt = av_aes_alloc();
        if (!c->aes_encrypt) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        ret = av_aes_init(c->aes_encrypt, c->encrypt_key, BLOCKSIZE*8, 0);
        if (ret < 0)
            goto err;
    }

    c->pad_len = 0;

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
    av_aes_crypt(c->aes_decrypt, c->outbuffer, c->inbuffer + c->indata_used,
                 blocks, c->decrypt_iv, 1);
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

static int crypto_write(URLContext *h, const unsigned char *buf, int size)
{
    CryptoContext *c = h->priv_data;
    int total_size, blocks, pad_len, out_size;
    uint8_t *out_buf;
    int ret = 0;

    total_size = size + c->pad_len;
    pad_len = total_size % BLOCKSIZE;
    out_size = total_size - pad_len;
    blocks = out_size / BLOCKSIZE;

    if (out_size) {
        out_buf = av_malloc(out_size);
        if (!out_buf)
            return AVERROR(ENOMEM);

        if (c->pad_len) {
            memcpy(&c->pad[c->pad_len], buf, BLOCKSIZE - c->pad_len);
            av_aes_crypt(c->aes_encrypt, out_buf, c->pad, 1, c->encrypt_iv, 0);
            blocks--;
        }

        av_aes_crypt(c->aes_encrypt, &out_buf[c->pad_len ? BLOCKSIZE : 0],
                             &buf[c->pad_len ? BLOCKSIZE - c->pad_len: 0],
                             blocks, c->encrypt_iv, 0);

        ret = ffurl_write(c->hd, out_buf, out_size);
        av_free(out_buf);
        if (ret < 0)
            return ret;

        memcpy(c->pad, &buf[size - pad_len], pad_len);
    } else
        memcpy(&c->pad[c->pad_len], buf, size);

    c->pad_len = pad_len;

    return size;
}

static int crypto_close(URLContext *h)
{
    CryptoContext *c = h->priv_data;
    uint8_t out_buf[BLOCKSIZE];
    int ret, pad;

    if (c->aes_encrypt) {
        pad = BLOCKSIZE - c->pad_len;
        memset(&c->pad[c->pad_len], pad, pad);
        av_aes_crypt(c->aes_encrypt, out_buf, c->pad, 1, c->encrypt_iv, 0);
        if ((ret =  ffurl_write(c->hd, out_buf, BLOCKSIZE)) < 0)
            return ret;
    }

    if (c->hd)
        ffurl_close(c->hd);
    av_freep(&c->aes_decrypt);
    av_freep(&c->aes_encrypt);
    return 0;
}

URLProtocol ff_crypto_protocol = {
    .name            = "crypto",
    .url_open2       = crypto_open2,
    .url_read        = crypto_read,
    .url_write       = crypto_write,
    .url_close       = crypto_close,
    .priv_data_size  = sizeof(CryptoContext),
    .priv_data_class = &crypto_class,
    .flags           = URL_PROTOCOL_FLAG_NESTED_SCHEME,
};
