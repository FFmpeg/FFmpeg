/*
 * RTP JPEG-compressed Video Depacketizer, RFC 2435
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

#include "avformat.h"
#include "avio_internal.h"
#include "rtpdec.h"
#include "rtpdec_formats.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/jpegtables.h"
#include "libavcodec/mjpeg.h"
#include "libavcodec/bytestream.h"

/**
 * RTP/JPEG specific private data.
 */
struct PayloadContext {
    AVIOContext *frame;         ///< current frame buffer
    uint32_t    timestamp;      ///< current frame timestamp
    int         hdr_size;       ///< size of the current frame header
    uint8_t     qtables[128][128];
    uint8_t     qtables_len[128];
};

static const uint8_t default_quantizers[128] = {
    /* luma table */
    16,  11,  12,  14,  12,  10,  16,  14,
    13,  14,  18,  17,  16,  19,  24,  40,
    26,  24,  22,  22,  24,  49,  35,  37,
    29,  40,  58,  51,  61,  60,  57,  51,
    56,  55,  64,  72,  92,  78,  64,  68,
    87,  69,  55,  56,  80,  109, 81,  87,
    95,  98,  103, 104, 103, 62,  77,  113,
    121, 112, 100, 120, 92,  101, 103, 99,

    /* chroma table */
    17,  18,  18,  24,  21,  24,  47,  26,
    26,  47,  99,  66,  56,  66,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99
};

static void jpeg_close_context(PayloadContext *jpeg)
{
    ffio_free_dyn_buf(&jpeg->frame);
}

static int jpeg_create_huffman_table(PutByteContext *p, int table_class,
                                     int table_id, const uint8_t *bits_table,
                                     const uint8_t *value_table)
{
    int i, n = 0;

    bytestream2_put_byte(p, table_class << 4 | table_id);

    for (i = 1; i <= 16; i++) {
        n += bits_table[i];
        bytestream2_put_byte(p, bits_table[i]);
    }

    for (i = 0; i < n; i++) {
        bytestream2_put_byte(p, value_table[i]);
    }
    return n + 17;
}

static void jpeg_put_marker(PutByteContext *pbc, int code)
{
    bytestream2_put_byte(pbc, 0xff);
    bytestream2_put_byte(pbc, code);
}

static int jpeg_create_header(uint8_t *buf, int size, uint32_t type, uint32_t w,
                              uint32_t h, const uint8_t *qtable, int nb_qtable,
                              int dri)
{
    PutByteContext pbc;
    uint8_t *dht_size_ptr;
    int dht_size, i;

    bytestream2_init_writer(&pbc, buf, size);

    /* Convert from blocks to pixels. */
    w <<= 3;
    h <<= 3;

    /* SOI */
    jpeg_put_marker(&pbc, SOI);

    /* JFIF header */
    jpeg_put_marker(&pbc, APP0);
    bytestream2_put_be16(&pbc, 16);
    bytestream2_put_buffer(&pbc, "JFIF", 5);
    bytestream2_put_be16(&pbc, 0x0201);
    bytestream2_put_byte(&pbc, 0);
    bytestream2_put_be16(&pbc, 1);
    bytestream2_put_be16(&pbc, 1);
    bytestream2_put_byte(&pbc, 0);
    bytestream2_put_byte(&pbc, 0);

    if (dri) {
        jpeg_put_marker(&pbc, DRI);
        bytestream2_put_be16(&pbc, 4);
        bytestream2_put_be16(&pbc, dri);
    }

    /* DQT */
    jpeg_put_marker(&pbc, DQT);
    bytestream2_put_be16(&pbc, 2 + nb_qtable * (1 + 64));

    for (i = 0; i < nb_qtable; i++) {
        bytestream2_put_byte(&pbc, i);

        /* Each table is an array of 64 values given in zig-zag
         * order, identical to the format used in a JFIF DQT
         * marker segment. */
        bytestream2_put_buffer(&pbc, qtable + 64 * i, 64);
    }

    /* DHT */
    jpeg_put_marker(&pbc, DHT);
    dht_size_ptr = pbc.buffer;
    bytestream2_put_be16(&pbc, 0);

    dht_size  = 2;
    dht_size += jpeg_create_huffman_table(&pbc, 0, 0,avpriv_mjpeg_bits_dc_luminance,
                                          avpriv_mjpeg_val_dc);
    dht_size += jpeg_create_huffman_table(&pbc, 0, 1, avpriv_mjpeg_bits_dc_chrominance,
                                          avpriv_mjpeg_val_dc);
    dht_size += jpeg_create_huffman_table(&pbc, 1, 0, avpriv_mjpeg_bits_ac_luminance,
                                          avpriv_mjpeg_val_ac_luminance);
    dht_size += jpeg_create_huffman_table(&pbc, 1, 1, avpriv_mjpeg_bits_ac_chrominance,
                                          avpriv_mjpeg_val_ac_chrominance);
    AV_WB16(dht_size_ptr, dht_size);

    /* SOF0 */
    jpeg_put_marker(&pbc, SOF0);
    bytestream2_put_be16(&pbc, 17); /* size */
    bytestream2_put_byte(&pbc, 8); /* bits per component */
    bytestream2_put_be16(&pbc, h);
    bytestream2_put_be16(&pbc, w);
    bytestream2_put_byte(&pbc, 3); /* number of components */
    bytestream2_put_byte(&pbc, 1); /* component number */
    bytestream2_put_byte(&pbc, (2 << 4) | (type ? 2 : 1)); /* hsample/vsample */
    bytestream2_put_byte(&pbc, 0); /* matrix number */
    bytestream2_put_byte(&pbc, 2); /* component number */
    bytestream2_put_byte(&pbc, 1 << 4 | 1); /* hsample/vsample */
    bytestream2_put_byte(&pbc, nb_qtable == 2 ? 1 : 0); /* matrix number */
    bytestream2_put_byte(&pbc, 3); /* component number */
    bytestream2_put_byte(&pbc, 1 << 4 | 1); /* hsample/vsample */
    bytestream2_put_byte(&pbc, nb_qtable == 2 ? 1 : 0); /* matrix number */

    /* SOS */
    jpeg_put_marker(&pbc, SOS);
    bytestream2_put_be16(&pbc, 12);
    bytestream2_put_byte(&pbc, 3);
    bytestream2_put_byte(&pbc, 1);
    bytestream2_put_byte(&pbc, 0);
    bytestream2_put_byte(&pbc, 2);
    bytestream2_put_byte(&pbc, 17);
    bytestream2_put_byte(&pbc, 3);
    bytestream2_put_byte(&pbc, 17);
    bytestream2_put_byte(&pbc, 0);
    bytestream2_put_byte(&pbc, 63);
    bytestream2_put_byte(&pbc, 0);

    /* Return the length in bytes of the JPEG header. */
    return bytestream2_tell_p(&pbc);
}

static void create_default_qtables(uint8_t *qtables, uint8_t q)
{
    int factor = q;
    int i;
    uint16_t S;

    factor = av_clip(q, 1, 99);

    if (q < 50)
        S = 5000 / factor;
    else
        S = 200 - factor * 2;

    for (i = 0; i < 128; i++) {
        int val = (default_quantizers[i] * S + 50) / 100;

        /* Limit the quantizers to 1 <= q <= 255. */
        val = av_clip(val, 1, 255);
        qtables[i] = val;
    }
}

static int jpeg_parse_packet(AVFormatContext *ctx, PayloadContext *jpeg,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags)
{
    uint8_t type, q, width, height;
    const uint8_t *qtables = NULL;
    uint16_t qtable_len;
    uint32_t off;
    int ret, dri = 0;

    if (len < 8) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/JPEG packet.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Parse the main JPEG header. */
    off    = AV_RB24(buf + 1);  /* fragment byte offset */
    type   = AV_RB8(buf + 4);   /* id of jpeg decoder params */
    q      = AV_RB8(buf + 5);   /* quantization factor (or table id) */
    width  = AV_RB8(buf + 6);   /* frame width in 8 pixel blocks */
    height = AV_RB8(buf + 7);   /* frame height in 8 pixel blocks */
    buf += 8;
    len -= 8;

    if (type & 0x40) {
        if (len < 4) {
            av_log(ctx, AV_LOG_ERROR, "Too short RTP/JPEG packet.\n");
            return AVERROR_INVALIDDATA;
        }
        dri = AV_RB16(buf);
        buf += 4;
        len -= 4;
        type &= ~0x40;
    }
    if (type > 1) {
        av_log(ctx, AV_LOG_ERROR, "Unimplemented RTP/JPEG type %d\n", type);
        return AVERROR_PATCHWELCOME;
    }

    /* Parse the quantization table header. */
    if (off == 0) {
        /* Start of JPEG data packet. */
        uint8_t new_qtables[128];
        uint8_t hdr[1024];

        if (q > 127) {
            uint8_t precision;
            if (len < 4) {
                av_log(ctx, AV_LOG_ERROR, "Too short RTP/JPEG packet.\n");
                return AVERROR_INVALIDDATA;
            }

            /* The first byte is reserved for future use. */
            precision  = AV_RB8(buf + 1);    /* size of coefficients */
            qtable_len = AV_RB16(buf + 2);   /* length in bytes */
            buf += 4;
            len -= 4;

            if (precision)
                av_log(ctx, AV_LOG_WARNING, "Only 8-bit precision is supported.\n");

            if (qtable_len > 0) {
                if (len < qtable_len) {
                    av_log(ctx, AV_LOG_ERROR, "Too short RTP/JPEG packet.\n");
                    return AVERROR_INVALIDDATA;
                }
                qtables = buf;
                buf += qtable_len;
                len -= qtable_len;
                if (q < 255) {
                    if (jpeg->qtables_len[q - 128] &&
                        (jpeg->qtables_len[q - 128] != qtable_len ||
                         memcmp(qtables, &jpeg->qtables[q - 128][0], qtable_len))) {
                        av_log(ctx, AV_LOG_WARNING,
                               "Quantization tables for q=%d changed\n", q);
                    } else if (!jpeg->qtables_len[q - 128] && qtable_len <= 128) {
                        memcpy(&jpeg->qtables[q - 128][0], qtables,
                               qtable_len);
                        jpeg->qtables_len[q - 128] = qtable_len;
                    }
                }
            } else {
                if (q == 255) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Invalid RTP/JPEG packet. Quantization tables not found.\n");
                    return AVERROR_INVALIDDATA;
                }
                if (!jpeg->qtables_len[q - 128]) {
                    av_log(ctx, AV_LOG_ERROR,
                           "No quantization tables known for q=%d yet.\n", q);
                    return AVERROR_INVALIDDATA;
                }
                qtables    = &jpeg->qtables[q - 128][0];
                qtable_len =  jpeg->qtables_len[q - 128];
            }
        } else { /* q <= 127 */
            if (q == 0 || q > 99) {
                av_log(ctx, AV_LOG_ERROR, "Reserved q value %d\n", q);
                return AVERROR_INVALIDDATA;
            }
            create_default_qtables(new_qtables, q);
            qtables    = new_qtables;
            qtable_len = sizeof(new_qtables);
        }

        /* Skip the current frame in case of the end packet
         * has been lost somewhere. */
        ffio_free_dyn_buf(&jpeg->frame);

        if ((ret = avio_open_dyn_buf(&jpeg->frame)) < 0)
            return ret;
        jpeg->timestamp = *timestamp;

        /* Generate a frame and scan headers that can be prepended to the
         * RTP/JPEG data payload to produce a JPEG compressed image in
         * interchange format. */
        jpeg->hdr_size = jpeg_create_header(hdr, sizeof(hdr), type, width,
                                            height, qtables,
                                            qtable_len / 64, dri);

        /* Copy JPEG header to frame buffer. */
        avio_write(jpeg->frame, hdr, jpeg->hdr_size);
    }

    if (!jpeg->frame) {
        av_log(ctx, AV_LOG_ERROR,
               "Received packet without a start chunk; dropping frame.\n");
        return AVERROR(EAGAIN);
    }

    if (jpeg->timestamp != *timestamp) {
        /* Skip the current frame if timestamp is incorrect.
         * A start packet has been lost somewhere. */
        ffio_free_dyn_buf(&jpeg->frame);
        av_log(ctx, AV_LOG_ERROR, "RTP timestamps don't match.\n");
        return AVERROR_INVALIDDATA;
    }

    if (off != avio_tell(jpeg->frame) - jpeg->hdr_size) {
        av_log(ctx, AV_LOG_ERROR,
               "Missing packets; dropping frame.\n");
        return AVERROR(EAGAIN);
    }

    /* Copy data to frame buffer. */
    avio_write(jpeg->frame, buf, len);

    if (flags & RTP_FLAG_MARKER) {
        /* End of JPEG data packet. */
        uint8_t buf[2] = { 0xff, EOI };

        /* Put EOI marker. */
        avio_write(jpeg->frame, buf, sizeof(buf));

        /* Prepare the JPEG packet. */
        if ((ret = ff_rtp_finalize_packet(pkt, &jpeg->frame, st->index)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error occurred when getting frame buffer.\n");
            return ret;
        }

        return 0;
    }

    return AVERROR(EAGAIN);
}

RTPDynamicProtocolHandler ff_jpeg_dynamic_handler = {
    .enc_name          = "JPEG",
    .codec_type        = AVMEDIA_TYPE_VIDEO,
    .codec_id          = AV_CODEC_ID_MJPEG,
    .priv_data_size    = sizeof(PayloadContext),
    .close             = jpeg_close_context,
    .parse_packet      = jpeg_parse_packet,
    .static_payload_id = 26,
};
