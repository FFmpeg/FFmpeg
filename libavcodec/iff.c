/*
 * IFF PBM/ILBM bitmap decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * @file libavcodec/iff.c
 * IFF PBM/ILBM bitmap decoder
 */

#include "bytestream.h"
#include "avcodec.h"
#include "get_bits.h"
#include "iff.h"

typedef struct {
    AVFrame frame;
    int planesize;
    uint8_t * planebuf;
} IffContext;

/**
 * Convert CMAP buffer (stored in extradata) to lavc palette format
 */
int ff_cmap_read_palette(AVCodecContext *avctx, uint32_t *pal)
{
    int count, i;

    if (avctx->bits_per_coded_sample > 8) {
        av_log(avctx, AV_LOG_ERROR, "bit_per_coded_sample > 8 not supported\n");
        return AVERROR_INVALIDDATA;
    }

    count = 1 << avctx->bits_per_coded_sample;
    if (avctx->extradata_size < count * 3) {
        av_log(avctx, AV_LOG_ERROR, "palette data underflow\n");
        return AVERROR_INVALIDDATA;
    }
    for (i=0; i < count; i++) {
        pal[i] = 0xFF000000 | AV_RB24( avctx->extradata + i*3 );
    }
    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    int err;

    if (avctx->bits_per_coded_sample <= 8) {
        avctx->pix_fmt = PIX_FMT_PAL8;
    } else if (avctx->bits_per_coded_sample <= 32) {
        avctx->pix_fmt = PIX_FMT_BGR32;
    } else {
        return AVERROR_INVALIDDATA;
    }

    s->planesize = avctx->width / 8;
    s->planebuf = av_malloc(s->planesize + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!s->planebuf)
        return AVERROR(ENOMEM);

    s->frame.reference = 1;
    if ((err = avctx->get_buffer(avctx, &s->frame) < 0)) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return err;
    }

    return avctx->bits_per_coded_sample <= 8 ?
       ff_cmap_read_palette(avctx, (uint32_t*)s->frame.data[1]) : 0;
}

/**
 * Decode interleaved plane buffer
 * @param dst Destination buffer
 * @param buf Source buffer
 * @param buf_size
 * @param bps bits_per_coded_sample
 * @param plane plane number to decode as
 */
#define DECLARE_DECODEPLANE(suffix, type) \
static void decodeplane##suffix(void *dst, const uint8_t *const buf, int buf_size, int bps, int plane) \
{ \
    GetBitContext gb; \
    int i, b; \
    init_get_bits(&gb, buf, buf_size * 8); \
    for(i = 0; i < (buf_size * 8 + bps - 1) / bps; i++) { \
        for (b = 0; b < bps; b++) { \
            ((type *)dst)[ i*bps + b ] |= get_bits1(&gb) << plane; \
        } \
    } \
}
DECLARE_DECODEPLANE(8, uint8_t)
DECLARE_DECODEPLANE(32, uint32_t)

static int decode_frame_ilbm(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    int y, plane;

    if (avctx->reget_buffer(avctx, &s->frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    for(y = 0; y < avctx->height; y++ ) {
        uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
        memset(row, 0, avctx->pix_fmt == PIX_FMT_PAL8 ? avctx->width : (avctx->width * 4));
        for (plane = 0; plane < avctx->bits_per_coded_sample && buf < buf_end; plane++) {
            if (avctx->pix_fmt == PIX_FMT_PAL8) {
                decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), avctx->bits_per_coded_sample, plane);
            } else { // PIX_FMT_BGR32
                decodeplane32(row, buf, FFMIN(s->planesize, buf_end - buf), avctx->bits_per_coded_sample, plane);
            }
            buf += s->planesize;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static int decode_frame_byterun1(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    int y, plane, x;

    if (avctx->reget_buffer(avctx, &s->frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    for(y = 0; y < avctx->height ; y++ ) {
        uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
        if (avctx->codec_tag == MKTAG('I','L','B','M')) { //interleaved
            memset(row, 0, avctx->pix_fmt == PIX_FMT_PAL8 ? avctx->width : (avctx->width * 4));
            for (plane = 0; plane < avctx->bits_per_coded_sample; plane++) {
                for(x = 0; x < s->planesize && buf < buf_end; ) {
                    int8_t value = *buf++;
                    int length;
                    if (value >= 0) {
                        length = value + 1;
                        memcpy(s->planebuf + x, buf, FFMIN3(length, s->planesize - x, buf_end - buf));
                        buf += length;
                    } else if (value > -128) {
                        length = -value + 1;
                        memset(s->planebuf + x, *buf++, FFMIN(length, s->planesize - x));
                    } else { //noop
                        continue;
                    }
                    x += length;
                }
                if (avctx->pix_fmt == PIX_FMT_PAL8) {
                    decodeplane8(row, s->planebuf, s->planesize, avctx->bits_per_coded_sample, plane);
                } else { //PIX_FMT_BGR32
                    decodeplane32(row, s->planebuf, s->planesize, avctx->bits_per_coded_sample, plane);
                }
            }
        } else {
            for(x = 0; x < avctx->width && buf < buf_end; ) {
                int8_t value = *buf++;
                int length;
                if (value >= 0) {
                    length = value + 1;
                    memcpy(row + x, buf, FFMIN3(length, buf_end - buf, avctx->width - x));
                    buf += length;
                } else if (value > -128) {
                    length = -value + 1;
                    memset(row + x, *buf++, FFMIN(length, avctx->width - x));
                } else { //noop
                    continue;
                }
                x += length;
            }
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);
    av_freep(&s->planebuf);
    return 0;
}

AVCodec iff_ilbm_decoder = {
    "iff_ilbm",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_IFF_ILBM,
    sizeof(IffContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame_ilbm,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ILBM"),
};

AVCodec iff_byterun1_decoder = {
    "iff_byterun1",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_IFF_BYTERUN1,
    sizeof(IffContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame_byterun1,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ByteRun1"),
};
