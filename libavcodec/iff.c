/*
 * IFF PBM/ILBM bitmap decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
 * Copyright (c) 2010 Sebastian Vater <cdgs.basty@googlemail.com>
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
 * IFF PBM/ILBM bitmap decoder
 */

#include <stdint.h>

#include "libavutil/imgutils.h"

#include "bytestream.h"
#include "avcodec.h"
#include "internal.h"

typedef struct IffContext {
    AVFrame *frame;
    int planesize;
    uint8_t * planebuf;
    int init; // 1 if buffer and palette data already initialized, 0 otherwise
} IffContext;

#define LUT8_PART(plane, v)                             \
    AV_LE2NE64C(UINT64_C(0x0000000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010101)<<32 | v) << plane

#define LUT8(plane) {                           \
    LUT8_PART(plane, 0x0000000),                \
    LUT8_PART(plane, 0x1000000),                \
    LUT8_PART(plane, 0x0010000),                \
    LUT8_PART(plane, 0x1010000),                \
    LUT8_PART(plane, 0x0000100),                \
    LUT8_PART(plane, 0x1000100),                \
    LUT8_PART(plane, 0x0010100),                \
    LUT8_PART(plane, 0x1010100),                \
    LUT8_PART(plane, 0x0000001),                \
    LUT8_PART(plane, 0x1000001),                \
    LUT8_PART(plane, 0x0010001),                \
    LUT8_PART(plane, 0x1010001),                \
    LUT8_PART(plane, 0x0000101),                \
    LUT8_PART(plane, 0x1000101),                \
    LUT8_PART(plane, 0x0010101),                \
    LUT8_PART(plane, 0x1010101),                \
}

// 8 planes * 8-bit mask
static const uint64_t plane8_lut[8][256] = {
    LUT8(0), LUT8(1), LUT8(2), LUT8(3),
    LUT8(4), LUT8(5), LUT8(6), LUT8(7),
};

#define LUT32(plane) {                                \
             0,          0,          0,          0,   \
             0,          0,          0, 1 << plane,   \
             0,          0, 1 << plane,          0,   \
             0,          0, 1 << plane, 1 << plane,   \
             0, 1 << plane,          0,          0,   \
             0, 1 << plane,          0, 1 << plane,   \
             0, 1 << plane, 1 << plane,          0,   \
             0, 1 << plane, 1 << plane, 1 << plane,   \
    1 << plane,          0,          0,          0,   \
    1 << plane,          0,          0, 1 << plane,   \
    1 << plane,          0, 1 << plane,          0,   \
    1 << plane,          0, 1 << plane, 1 << plane,   \
    1 << plane, 1 << plane,          0,          0,   \
    1 << plane, 1 << plane,          0, 1 << plane,   \
    1 << plane, 1 << plane, 1 << plane,          0,   \
    1 << plane, 1 << plane, 1 << plane, 1 << plane,   \
}

// 32 planes * 4-bit mask * 4 lookup tables each
static const uint32_t plane32_lut[32][16*4] = {
    LUT32( 0), LUT32( 1), LUT32( 2), LUT32( 3),
    LUT32( 4), LUT32( 5), LUT32( 6), LUT32( 7),
    LUT32( 8), LUT32( 9), LUT32(10), LUT32(11),
    LUT32(12), LUT32(13), LUT32(14), LUT32(15),
    LUT32(16), LUT32(17), LUT32(18), LUT32(19),
    LUT32(20), LUT32(21), LUT32(22), LUT32(23),
    LUT32(24), LUT32(25), LUT32(26), LUT32(27),
    LUT32(28), LUT32(29), LUT32(30), LUT32(31),
};

// Gray to RGB, required for palette table of grayscale images with bpp < 8
static av_always_inline uint32_t gray2rgb(const uint32_t x) {
    return x << 16 | x << 8 | x;
}

/**
 * Convert CMAP buffer (stored in extradata) to lavc palette format
 */
static int cmap_read_palette(AVCodecContext *avctx, uint32_t *pal)
{
    int count, i;

    if (avctx->bits_per_coded_sample > 8) {
        av_log(avctx, AV_LOG_ERROR, "bit_per_coded_sample > 8 not supported\n");
        return AVERROR_INVALIDDATA;
    }

    count = 1 << avctx->bits_per_coded_sample;
    // If extradata is smaller than actually needed, fill the remaining with black.
    count = FFMIN(avctx->extradata_size / 3, count);
    if (count) {
        for (i = 0; i < count; i++)
            pal[i] = 0xFF000000 | AV_RB24(avctx->extradata + i * 3);
    } else { // Create gray-scale color palette for bps < 8
        count = 1 << avctx->bits_per_coded_sample;

        for (i = 0; i < count; i++)
            pal[i] = 0xFF000000 | gray2rgb((i * 255) >> avctx->bits_per_coded_sample);
    }
    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    av_frame_free(&s->frame);
    av_freep(&s->planebuf);
    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    int err;

    if (avctx->bits_per_coded_sample <= 8) {
        avctx->pix_fmt = (avctx->bits_per_coded_sample < 8 ||
                          avctx->extradata_size) ? AV_PIX_FMT_PAL8
                                                 : AV_PIX_FMT_GRAY8;
    } else if (avctx->bits_per_coded_sample <= 32) {
        avctx->pix_fmt = AV_PIX_FMT_BGR32;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if ((err = av_image_check_size(avctx->width, avctx->height, 0, avctx)))
        return err;
    s->planesize = FFALIGN(avctx->width, 16) >> 3; // Align plane size in bits to word-boundary
    s->planebuf  = av_malloc(s->planesize + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!s->planebuf)
        return AVERROR(ENOMEM);

    s->frame = av_frame_alloc();
    if (!s->frame) {
        decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

/**
 * Decode interleaved plane buffer up to 8bpp
 * @param dst Destination buffer
 * @param buf Source buffer
 * @param buf_size
 * @param plane plane number to decode as
 */
static void decodeplane8(uint8_t *dst, const uint8_t *buf, int buf_size, int plane)
{
    const uint64_t *lut = plane8_lut[plane];
    do {
        uint64_t v = AV_RN64A(dst) | lut[*buf++];
        AV_WN64A(dst, v);
        dst += 8;
    } while (--buf_size);
}

/**
 * Decode interleaved plane buffer up to 24bpp
 * @param dst Destination buffer
 * @param buf Source buffer
 * @param buf_size
 * @param plane plane number to decode as
 */
static void decodeplane32(uint32_t *dst, const uint8_t *buf, int buf_size, int plane)
{
    const uint32_t *lut = plane32_lut[plane];
    do {
        unsigned mask = (*buf >> 2) & ~3;
        dst[0] |= lut[mask++];
        dst[1] |= lut[mask++];
        dst[2] |= lut[mask++];
        dst[3] |= lut[mask];
        mask    = (*buf++ << 2) & 0x3F;
        dst[4] |= lut[mask++];
        dst[5] |= lut[mask++];
        dst[6] |= lut[mask++];
        dst[7] |= lut[mask];
        dst    += 8;
    } while (--buf_size);
}

/**
 * Decode one complete byterun1 encoded line.
 *
 * @param dst the destination buffer where to store decompressed bitstream
 * @param dst_size the destination plane size in bytes
 * @param buf the source byterun1 compressed bitstream
 * @param buf_end the EOF of source byterun1 compressed bitstream
 * @return number of consumed bytes in byterun1 compressed bitstream
 */
static int decode_byterun(uint8_t *dst, int dst_size,
                          const uint8_t *buf, const uint8_t *const buf_end)
{
    const uint8_t *const buf_start = buf;
    unsigned x;
    for (x = 0; x < dst_size && buf < buf_end;) {
        unsigned length;
        const int8_t value = *buf++;
        if (value >= 0) {
            length = value + 1;
            memcpy(dst + x, buf, FFMIN3(length, dst_size - x, buf_end - buf));
            buf += length;
        } else if (value > -128) {
            length = -value + 1;
            memset(dst + x, *buf++, FFMIN(length, dst_size - x));
        } else { // noop
            continue;
        }
        x += length;
    }
    return buf - buf_start;
}

static int decode_frame_ilbm(AVCodecContext *avctx,
                             void *data, int *got_frame,
                             AVPacket *avpkt)
{
    IffContext *s          = avctx->priv_data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    const uint8_t *buf_end = buf + buf_size;
    int y, plane, res;

    if ((res = ff_reget_buffer(avctx, s->frame)) < 0)
        return res;

    if (!s->init && avctx->bits_per_coded_sample <= 8 &&
        avctx->pix_fmt != AV_PIX_FMT_GRAY8) {
        if ((res = cmap_read_palette(avctx, (uint32_t *)s->frame->data[1])) < 0)
            return res;
    }
    s->init = 1;

    if (avctx->codec_tag == MKTAG('I', 'L', 'B', 'M')) { // interleaved
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                memset(row, 0, avctx->width);
                for (plane = 0; plane < avctx->bits_per_coded_sample && buf < buf_end;
                     plane++) {
                    decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                    buf += s->planesize;
                }
            }
        } else { // AV_PIX_FMT_BGR32
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                memset(row, 0, avctx->width << 2);
                for (plane = 0; plane < avctx->bits_per_coded_sample && buf < buf_end;
                     plane++) {
                    decodeplane32((uint32_t *)row, buf,
                                  FFMIN(s->planesize, buf_end - buf), plane);
                    buf += s->planesize;
                }
            }
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) { // IFF-PBM
        for (y = 0; y < avctx->height && buf < buf_end; y++) {
            uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
            memcpy(row, buf, FFMIN(avctx->width, buf_end - buf));
            buf += avctx->width + (avctx->width % 2); // padding if odd
        }
    }

    if ((res = av_frame_ref(data, s->frame)) < 0)
        return res;

    *got_frame = 1;

    return buf_size;
}

static int decode_frame_byterun1(AVCodecContext *avctx,
                                 void *data, int *got_frame,
                                 AVPacket *avpkt)
{
    IffContext *s          = avctx->priv_data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    const uint8_t *buf_end = buf + buf_size;
    int y, plane, res;

    if ((res = ff_reget_buffer(avctx, s->frame)) < 0)
        return res;

    if (!s->init && avctx->bits_per_coded_sample <= 8 &&
        avctx->pix_fmt != AV_PIX_FMT_GRAY8) {
        if ((res = cmap_read_palette(avctx, (uint32_t *)s->frame->data[1])) < 0)
            return res;
    }
    s->init = 1;

    if (avctx->codec_tag == MKTAG('I', 'L', 'B', 'M')) { // interleaved
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                memset(row, 0, avctx->width);
                for (plane = 0; plane < avctx->bits_per_coded_sample; plane++) {
                    buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                    decodeplane8(row, s->planebuf, s->planesize, plane);
                }
            }
        } else { // AV_PIX_FMT_BGR32
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                memset(row, 0, avctx->width << 2);
                for (plane = 0; plane < avctx->bits_per_coded_sample; plane++) {
                    buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                    decodeplane32((uint32_t *)row, s->planebuf, s->planesize, plane);
                }
            }
        }
    } else {
        for (y = 0; y < avctx->height; y++) {
            uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
            buf += decode_byterun(row, avctx->width, buf, buf_end);
        }
    }

    if ((res = av_frame_ref(data, s->frame)) < 0)
        return res;

    *got_frame = 1;

    return buf_size;
}

AVCodec ff_iff_ilbm_decoder = {
    .name           = "iff_ilbm",
    .long_name      = NULL_IF_CONFIG_SMALL("IFF ILBM"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IFF_ILBM,
    .priv_data_size = sizeof(IffContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame_ilbm,
    .capabilities   = AV_CODEC_CAP_DR1,
};

AVCodec ff_iff_byterun1_decoder = {
    .name           = "iff_byterun1",
    .long_name      = NULL_IF_CONFIG_SMALL("IFF ByteRun1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IFF_BYTERUN1,
    .priv_data_size = sizeof(IffContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame_byterun1,
    .capabilities   = AV_CODEC_CAP_DR1,
};
