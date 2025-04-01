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

#include "libavutil/aes.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "url.h"

// encourage reads of 4096 bytes - 1 block is always retained.
#define MAX_BUFFER_BLOCKS 257
#define BLOCKSIZE 16

typedef struct CryptoContext {
    const AVClass *class;
    URLContext *hd;
    uint8_t inbuffer [BLOCKSIZE*MAX_BUFFER_BLOCKS],
            outbuffer[BLOCKSIZE*MAX_BUFFER_BLOCKS];
    uint8_t *outptr;
    int indata, indata_used, outdata;
    int64_t position;  // position in file - used in seek
    int flags;
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
    uint8_t *write_buf;
    unsigned int write_buf_size;
    uint8_t pad[BLOCKSIZE];
    int pad_len;
} CryptoContext;

#define OFFSET(x) offsetof(CryptoContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption crypto_options[] = {
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
    .option         = crypto_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int set_aes_arg(URLContext *h, uint8_t **buf, int *buf_len,
                       uint8_t *default_buf, int default_buf_len,
                       const char *desc)
{
    if (!*buf_len) {
        if (!default_buf_len) {
            av_log(h, AV_LOG_ERROR, "%s not set\n", desc);
            return AVERROR(EINVAL);
        } else if (default_buf_len != BLOCKSIZE) {
            av_log(h, AV_LOG_ERROR,
                   "invalid %s size (%d bytes, block size is %d)\n",
                   desc, default_buf_len, BLOCKSIZE);
            return AVERROR(EINVAL);
        }
        *buf = av_memdup(default_buf, default_buf_len);
        if (!*buf)
            return AVERROR(ENOMEM);
        *buf_len = default_buf_len;
    } else if (*buf_len != BLOCKSIZE) {
        av_log(h, AV_LOG_ERROR,
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
    c->flags = flags;

    if (!av_strstart(uri, "crypto+", &nested_url) &&
        !av_strstart(uri, "crypto:", &nested_url)) {
        av_log(h, AV_LOG_ERROR, "Unsupported url %s\n", uri);
        ret = AVERROR(EINVAL);
        goto err;
    }

    if (flags & AVIO_FLAG_READ) {
        if ((ret = set_aes_arg(h, &c->decrypt_key, &c->decrypt_keylen,
                               c->key, c->keylen, "decryption key")) < 0)
            goto err;
        if ((ret = set_aes_arg(h, &c->decrypt_iv, &c->decrypt_ivlen,
                               c->iv, c->ivlen, "decryption IV")) < 0)
            goto err;
    }

    if (flags & AVIO_FLAG_WRITE) {
        if ((ret = set_aes_arg(h, &c->encrypt_key, &c->encrypt_keylen,
                               c->key, c->keylen, "encryption key")) < 0)
        if (ret < 0)
            goto err;
        if ((ret = set_aes_arg(h, &c->encrypt_iv, &c->encrypt_ivlen,
                               c->iv, c->ivlen, "encryption IV")) < 0)
            goto err;
    }

    if ((ret = ffurl_open_whitelist(&c->hd, nested_url, flags,
                                    &h->interrupt_callback, options,
                                    h->protocol_whitelist, h->protocol_blacklist, h)) < 0) {
        av_log(h, AV_LOG_ERROR, "Unable to open resource: %s\n", nested_url);
        goto err;
    }

    if (flags & AVIO_FLAG_READ) {
        c->aes_decrypt = av_aes_alloc();
        if (!c->aes_decrypt) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        ret = av_aes_init(c->aes_decrypt, c->decrypt_key, BLOCKSIZE * 8, 1);
        if (ret < 0)
            goto err;

        // pass back information about the context we openned
        if (c->hd->is_streamed)
            h->is_streamed = c->hd->is_streamed;
    }

    if (flags & AVIO_FLAG_WRITE) {
        c->aes_encrypt = av_aes_alloc();
        if (!c->aes_encrypt) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        ret = av_aes_init(c->aes_encrypt, c->encrypt_key, BLOCKSIZE * 8, 0);
        if (ret < 0)
            goto err;
        // for write, we must be streamed
        // - linear write only for crytpo aes-128-cbc
        h->is_streamed = 1;
    }

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
        c->position = c->position + size;
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

static int64_t crypto_seek(URLContext *h, int64_t pos, int whence)
{
    CryptoContext *c = h->priv_data;
    int64_t block;
    int64_t newpos;

    if (c->flags & AVIO_FLAG_WRITE) {
        av_log(h, AV_LOG_ERROR,
            "Crypto: seek not supported for write\r\n");
        /* seems the most appropriate error to return */
        return AVERROR(ESPIPE);
    }

    // reset eof, else we won't read it correctly if we already hit eof.
    c->eof = 0;

    switch (whence) {
    case SEEK_SET:
        break;
    case SEEK_CUR:
        pos = pos + c->position;
        break;
    case SEEK_END:
        newpos = ffurl_seek( c->hd, pos, AVSEEK_SIZE );
        if (newpos < 0) {
            av_log(h, AV_LOG_ERROR,
                "Crypto: seek_end - can't get file size (pos=%"PRId64")\r\n", pos);
            return newpos;
        }
        pos = newpos - pos;
        break;
    case AVSEEK_SIZE:
        return ffurl_seek( c->hd, pos, AVSEEK_SIZE );
    default:
        av_log(h, AV_LOG_ERROR,
            "Crypto: no support for seek where 'whence' is %d\r\n", whence);
        return AVERROR(EINVAL);
    }

    c->outdata = 0;
    c->indata = 0;
    c->indata_used = 0;
    c->outptr = c->outbuffer;

    // identify the block containing the IV for the
    // next block we will decrypt
    block = pos/BLOCKSIZE;
    if (block == 0) {
        // restore the iv to the seed one - this is the iv for the FIRST block
        memcpy( c->decrypt_iv, c->iv, c->ivlen );
        c->position = 0;
    } else {
        // else, go back one block - we will get av_cyrpt to read this block
        // which it will then store use as the iv.
        // note that the DECRYPTED result will not be correct,
        // but will be discarded
        block--;
        c->position = (block * BLOCKSIZE);
    }

    newpos = ffurl_seek( c->hd, c->position, SEEK_SET );
    if (newpos < 0) {
        av_log(h, AV_LOG_ERROR,
            "Crypto: nested protocol no support for seek or seek failed\n");
        return newpos;
    }

    // read and discard from here up to required position
    // (which will set the iv correctly to it).
    if (pos - c->position) {
        uint8_t buff[BLOCKSIZE*2]; // maximum size of pos-c->position
        int len = pos - c->position;
        int res;

        while (len > 0) {
            // note: this may not return all the bytes first time
            res = crypto_read(h, buff, len);
            if (res < 0)
                break;
            len -= res;
        }

        // if we did not get all the bytes
        if (len != 0) {
            av_log(h, AV_LOG_ERROR,
                "Crypto: discard read did not get all the bytes (%d remain) - read returned (%d)-%s\n",
                len, res, av_err2str(res));
            return AVERROR(EINVAL);
        }
    }

    return c->position;
}

static int crypto_write(URLContext *h, const unsigned char *buf, int size)
{
    CryptoContext *c = h->priv_data;
    int total_size, blocks, pad_len, out_size;
    int ret = 0;

    total_size = size + c->pad_len;
    pad_len = total_size % BLOCKSIZE;
    out_size = total_size - pad_len;
    blocks = out_size / BLOCKSIZE;

    if (out_size) {
        av_fast_malloc(&c->write_buf, &c->write_buf_size, out_size);

        if (!c->write_buf)
            return AVERROR(ENOMEM);

        if (c->pad_len) {
            memcpy(&c->pad[c->pad_len], buf, BLOCKSIZE - c->pad_len);
            av_aes_crypt(c->aes_encrypt, c->write_buf, c->pad, 1, c->encrypt_iv, 0);
            blocks--;
        }

        av_aes_crypt(c->aes_encrypt,
                     &c->write_buf[c->pad_len ? BLOCKSIZE : 0],
                     &buf[c->pad_len ? BLOCKSIZE - c->pad_len : 0],
                     blocks, c->encrypt_iv, 0);

        ret = ffurl_write(c->hd, c->write_buf, out_size);
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
    int ret = 0;

    if (c->aes_encrypt) {
        uint8_t out_buf[BLOCKSIZE];
        int pad = BLOCKSIZE - c->pad_len;

        memset(&c->pad[c->pad_len], pad, pad);
        av_aes_crypt(c->aes_encrypt, out_buf, c->pad, 1, c->encrypt_iv, 0);
        ret = ffurl_write(c->hd, out_buf, BLOCKSIZE);
    }

    ffurl_closep(&c->hd);
    av_freep(&c->aes_decrypt);
    av_freep(&c->aes_encrypt);
    av_freep(&c->write_buf);
    return ret;
}

const URLProtocol ff_crypto_protocol = {
    .name            = "crypto",
    .url_open2       = crypto_open2,
    .url_seek        = crypto_seek,
    .url_read        = crypto_read,
    .url_write       = crypto_write,
    .url_close       = crypto_close,
    .priv_data_size  = sizeof(CryptoContext),
    .priv_data_class = &crypto_class,
    .flags           = URL_PROTOCOL_FLAG_NESTED_SCHEME,
};
