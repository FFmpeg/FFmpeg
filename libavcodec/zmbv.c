/*
 * Zip Motion Blocks Video (ZMBV) decoder
 * Copyright (c) 2006 Konstantin Shishkov
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

/**
 * @file
 * Zip Motion Blocks Video decoder
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

#include <zlib.h>

#define ZMBV_KEYFRAME 1
#define ZMBV_DELTAPAL 2

enum ZmbvFormat {
    ZMBV_FMT_NONE  = 0,
    ZMBV_FMT_1BPP  = 1,
    ZMBV_FMT_2BPP  = 2,
    ZMBV_FMT_4BPP  = 3,
    ZMBV_FMT_8BPP  = 4,
    ZMBV_FMT_15BPP = 5,
    ZMBV_FMT_16BPP = 6,
    ZMBV_FMT_24BPP = 7,
    ZMBV_FMT_32BPP = 8
};

/*
 * Decoder context
 */
typedef struct ZmbvContext {
    AVCodecContext *avctx;
    AVFrame pic;

    int bpp;
    unsigned int decomp_size;
    uint8_t* decomp_buf;
    uint8_t pal[768];
    uint8_t *prev, *cur;
    int width, height;
    int fmt;
    int comp;
    int flags;
    int bw, bh, bx, by;
    int decomp_len;
    z_stream zstream;
    int (*decode_intra)(struct ZmbvContext *c);
    int (*decode_xor)(struct ZmbvContext *c);
} ZmbvContext;

/**
 * Decode XOR'ed frame - 8bpp version
 */

static int zmbv_decode_xor_8(ZmbvContext *c)
{
    uint8_t *src = c->decomp_buf;
    uint8_t *output, *prev;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    output = c->cur;
    prev = c->prev;

    if (c->flags & ZMBV_DELTAPAL) {
        for (i = 0; i < 768; i++)
            c->pal[i] ^= *src++;
    }

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint8_t *out, *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x;
            tprev = prev + x + dx + dy * c->width;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2);
                } else {
                    for (i = 0; i < bw2; i++) {
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += c->width;
                tprev += c->width;
            }

            if (d) { /* apply XOR'ed difference */
                out = output + x;
                for (j = 0; j < bh2; j++) {
                    for (i = 0; i < bw2; i++)
                        out[i] ^= *src++;
                    out += c->width;
                }
            }
        }
        output += c->width * c->bh;
        prev += c->width * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %ti of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

/**
 * Decode XOR'ed frame - 15bpp and 16bpp version
 */

static int zmbv_decode_xor_16(ZmbvContext *c)
{
    uint8_t *src = c->decomp_buf;
    uint16_t *output, *prev;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    output = (uint16_t*)c->cur;
    prev = (uint16_t*)c->prev;

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint16_t *out, *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x;
            tprev = prev + x + dx + dy * c->width;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 2);
                } else {
                    for (i = 0; i < bw2; i++) {
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += c->width;
                tprev += c->width;
            }

            if (d) { /* apply XOR'ed difference */
                out = output + x;
                for (j = 0; j < bh2; j++){
                    for (i = 0; i < bw2; i++) {
                        out[i] ^= *((uint16_t*)src);
                        src += 2;
                    }
                    out += c->width;
                }
            }
        }
        output += c->width * c->bh;
        prev += c->width * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %ti of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

#ifdef ZMBV_ENABLE_24BPP
/**
 * Decode XOR'ed frame - 24bpp version
 */

static int zmbv_decode_xor_24(ZmbvContext *c)
{
    uint8_t *src = c->decomp_buf;
    uint8_t *output, *prev;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;
    int stride;

    output = c->cur;
    prev = c->prev;

    stride = c->width * 3;
    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint8_t *out, *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x * 3;
            tprev = prev + (x + dx) * 3 + dy * stride;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 3);
                } else {
                    for (i = 0; i < bw2; i++){
                        if (mx + i < 0 || mx + i >= c->width) {
                            out[i * 3 + 0] = 0;
                            out[i * 3 + 1] = 0;
                            out[i * 3 + 2] = 0;
                        } else {
                            out[i * 3 + 0] = tprev[i * 3 + 0];
                            out[i * 3 + 1] = tprev[i * 3 + 1];
                            out[i * 3 + 2] = tprev[i * 3 + 2];
                        }
                    }
                }
                out += stride;
                tprev += stride;
            }

            if (d) { /* apply XOR'ed difference */
                out = output + x * 3;
                for (j = 0; j < bh2; j++) {
                    for (i = 0; i < bw2; i++) {
                        out[i * 3 + 0] ^= *src++;
                        out[i * 3 + 1] ^= *src++;
                        out[i * 3 + 2] ^= *src++;
                    }
                    out += stride;
                }
            }
        }
        output += stride * c->bh;
        prev += stride * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %i of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}
#endif //ZMBV_ENABLE_24BPP

/**
 * Decode XOR'ed frame - 32bpp version
 */

static int zmbv_decode_xor_32(ZmbvContext *c)
{
    uint8_t *src = c->decomp_buf;
    uint32_t *output, *prev;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    output = (uint32_t*)c->cur;
    prev = (uint32_t*)c->prev;

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint32_t *out, *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x;
            tprev = prev + x + dx + dy * c->width;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 4);
                } else {
                    for (i = 0; i < bw2; i++){
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += c->width;
                tprev += c->width;
            }

            if (d) { /* apply XOR'ed difference */
                out = output + x;
                for (j = 0; j < bh2; j++){
                    for (i = 0; i < bw2; i++) {
                        out[i] ^= *((uint32_t *) src);
                        src += 4;
                    }
                    out += c->width;
                }
            }
        }
        output += c->width * c->bh;
        prev   += c->width * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %ti of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

/**
 * Decode intraframe
 */
static int zmbv_decode_intra(ZmbvContext *c)
{
    uint8_t *src = c->decomp_buf;

    /* make the palette available on the way out */
    if (c->fmt == ZMBV_FMT_8BPP) {
        memcpy(c->pal, src, 768);
        src += 768;
    }

    memcpy(c->cur, src, c->width * c->height * (c->bpp / 8));
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ZmbvContext * const c = avctx->priv_data;
    int zret = Z_OK; // Zlib return code
    int len = buf_size;
    int hi_ver, lo_ver, ret;
    uint8_t *tmp;

    if (c->pic.data[0])
            avctx->release_buffer(avctx, &c->pic);

    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
    if ((ret = avctx->get_buffer(avctx, &c->pic)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    /* parse header */
    c->flags = buf[0];
    buf++; len--;
    if (c->flags & ZMBV_KEYFRAME) {
        hi_ver = buf[0];
        lo_ver = buf[1];
        c->comp = buf[2];
        c->fmt = buf[3];
        c->bw = buf[4];
        c->bh = buf[5];

        buf += 6;
        len -= 6;
        av_log(avctx, AV_LOG_DEBUG,
               "Flags=%X ver=%i.%i comp=%i fmt=%i blk=%ix%i\n",
               c->flags,hi_ver,lo_ver,c->comp,c->fmt,c->bw,c->bh);
        if (hi_ver != 0 || lo_ver != 1) {
            av_log_ask_for_sample(avctx, "Unsupported version %i.%i\n",
                                  hi_ver, lo_ver);
            return AVERROR_PATCHWELCOME;
        }
        if (c->bw == 0 || c->bh == 0) {
            av_log_ask_for_sample(avctx, "Unsupported block size %ix%i\n",
                                  c->bw, c->bh);
            return AVERROR_PATCHWELCOME;
        }
        if (c->comp != 0 && c->comp != 1) {
            av_log_ask_for_sample(avctx, "Unsupported compression type %i\n",
                                  c->comp);
            return AVERROR_PATCHWELCOME;
        }

        switch (c->fmt) {
        case ZMBV_FMT_8BPP:
            c->bpp = 8;
            c->decode_intra = zmbv_decode_intra;
            c->decode_xor = zmbv_decode_xor_8;
            break;
        case ZMBV_FMT_15BPP:
        case ZMBV_FMT_16BPP:
            c->bpp = 16;
            c->decode_intra = zmbv_decode_intra;
            c->decode_xor = zmbv_decode_xor_16;
            break;
#ifdef ZMBV_ENABLE_24BPP
        case ZMBV_FMT_24BPP:
            c->bpp = 24;
            c->decode_intra = zmbv_decode_intra;
            c->decode_xor = zmbv_decode_xor_24;
            break;
#endif //ZMBV_ENABLE_24BPP
        case ZMBV_FMT_32BPP:
            c->bpp = 32;
            c->decode_intra = zmbv_decode_intra;
            c->decode_xor = zmbv_decode_xor_32;
            break;
        default:
            c->decode_intra = NULL;
            c->decode_xor = NULL;
            av_log_ask_for_sample(avctx, "Unsupported (for now) format %i\n",
                                  c->fmt);
            return AVERROR_PATCHWELCOME;
        }

        zret = inflateReset(&c->zstream);
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
            return -1;
        }

        tmp = av_realloc(c->cur,  avctx->width * avctx->height * (c->bpp / 8));
        if (!tmp)
            return AVERROR(ENOMEM);
        c->cur = tmp;
        tmp = av_realloc(c->prev, avctx->width * avctx->height * (c->bpp / 8));
        if (!tmp)
            return AVERROR(ENOMEM);
        c->prev = tmp;
        c->bx   = (c->width  + c->bw - 1) / c->bw;
        c->by   = (c->height + c->bh - 1) / c->bh;
    }

    if (c->decode_intra == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Error! Got no format or no keyframe!\n");
        return AVERROR_INVALIDDATA;
    }

    if (c->comp == 0) { //Uncompressed data
        memcpy(c->decomp_buf, buf, len);
        c->decomp_size = 1;
    } else { // ZLIB-compressed data
        c->zstream.total_in = c->zstream.total_out = 0;
        c->zstream.next_in = buf;
        c->zstream.avail_in = len;
        c->zstream.next_out = c->decomp_buf;
        c->zstream.avail_out = c->decomp_size;
        zret = inflate(&c->zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR, "inflate error %d\n", zret);
            return AVERROR_INVALIDDATA;
        }
        c->decomp_len = c->zstream.total_out;
    }
    if (c->flags & ZMBV_KEYFRAME) {
        c->pic.key_frame = 1;
        c->pic.pict_type = AV_PICTURE_TYPE_I;
        c->decode_intra(c);
    } else {
        c->pic.key_frame = 0;
        c->pic.pict_type = AV_PICTURE_TYPE_P;
        if (c->decomp_len)
            c->decode_xor(c);
    }

    /* update frames */
    {
        uint8_t *out, *src;
        int i, j;

        out = c->pic.data[0];
        src = c->cur;
        switch (c->fmt) {
        case ZMBV_FMT_8BPP:
            for (j = 0; j < c->height; j++) {
                for (i = 0; i < c->width; i++) {
                    out[i * 3 + 0] = c->pal[(*src) * 3 + 0];
                    out[i * 3 + 1] = c->pal[(*src) * 3 + 1];
                    out[i * 3 + 2] = c->pal[(*src) * 3 + 2];
                    src++;
                }
                out += c->pic.linesize[0];
            }
            break;
        case ZMBV_FMT_15BPP:
            for (j = 0; j < c->height; j++) {
                for (i = 0; i < c->width; i++) {
                    uint16_t tmp = AV_RL16(src);
                    src += 2;
                    out[i * 3 + 0] = (tmp & 0x7C00) >> 7;
                    out[i * 3 + 1] = (tmp & 0x03E0) >> 2;
                    out[i * 3 + 2] = (tmp & 0x001F) << 3;
                }
                out += c->pic.linesize[0];
            }
            break;
        case ZMBV_FMT_16BPP:
            for (j = 0; j < c->height; j++) {
                for (i = 0; i < c->width; i++) {
                    uint16_t tmp = AV_RL16(src);
                    src += 2;
                    out[i * 3 + 0] = (tmp & 0xF800) >> 8;
                    out[i * 3 + 1] = (tmp & 0x07E0) >> 3;
                    out[i * 3 + 2] = (tmp & 0x001F) << 3;
                }
                out += c->pic.linesize[0];
            }
            break;
#ifdef ZMBV_ENABLE_24BPP
        case ZMBV_FMT_24BPP:
            for (j = 0; j < c->height; j++) {
                memcpy(out, src, c->width * 3);
                src += c->width * 3;
                out += c->pic.linesize[0];
            }
            break;
#endif //ZMBV_ENABLE_24BPP
        case ZMBV_FMT_32BPP:
            for (j = 0; j < c->height; j++) {
                for (i = 0; i < c->width; i++) {
                    uint32_t tmp = AV_RL32(src);
                    src += 4;
                    AV_WB24(out+(i*3), tmp);
                }
                out += c->pic.linesize[0];
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Cannot handle format %i\n", c->fmt);
        }
        FFSWAP(uint8_t *, c->cur, c->prev);
    }
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}



/*
 *
 * Init zmbv decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    ZmbvContext * const c = avctx->priv_data;
    int zret; // Zlib return code

    c->avctx = avctx;

    c->width = avctx->width;
    c->height = avctx->height;

    c->bpp = avctx->bits_per_coded_sample;

    // Needed if zlib unused or init aborted before inflateInit
    memset(&c->zstream, 0, sizeof(z_stream));

    avctx->pix_fmt = PIX_FMT_RGB24;
    c->decomp_size = (avctx->width + 255) * 4 * (avctx->height + 64);

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(c->decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR,
                   "Can't allocate decompression buffer.\n");
            return AVERROR(ENOMEM);
        }
    }

    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = inflateInit(&c->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return -1;
    }

    return 0;
}



/*
 *
 * Uninit zmbv decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    ZmbvContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    inflateEnd(&c->zstream);
    av_freep(&c->cur);
    av_freep(&c->prev);

    return 0;
}

AVCodec ff_zmbv_decoder = {
    .name           = "zmbv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_ZMBV,
    .priv_data_size = sizeof(ZmbvContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Zip Motion Blocks Video"),
};
