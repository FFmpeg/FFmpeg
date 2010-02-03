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
        pal[i] = AV_RB24( avctx->extradata + i*3 );
    }
    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    AVFrame *frame = avctx->priv_data;

    avctx->pix_fmt = PIX_FMT_PAL8;
    frame->reference = 1;

    if (avctx->get_buffer(avctx, frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return AVERROR_UNKNOWN;
    }
    return ff_cmap_read_palette(avctx, (uint32_t*)frame->data[1]);
}

/**
 * Interleaved memcpy
 */
static void imemcpy(uint8_t *dst, const uint8_t const *buf, int x, int bps, int plane, int length)
{
    int i, b;
    for(i = 0; i < length; i++) {
        int value = buf[i];
        for (b = 0; b < bps; b++) {
            if (value & (1<<b))
                dst[ (x+i)*bps + 7 - b] |= 1<<plane;
       }
    }
}

/**
 * Interleaved memset
 */
static void imemset(uint8_t *dst, int value, int x, int bps, int plane, int length)
{
    int i, b;
    for(i = 0; i < length; i++) {
        for (b = 0; b < bps; b++) {
            if (value & (1<<b))
                dst[ (x+i)*bps + 7 - b] |= 1<<plane;
       }
    }
}

static int decode_frame_ilbm(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    AVFrame *frame = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int planewidth = avctx->width / avctx->bits_per_coded_sample;
    int y, plane;

    if (avctx->reget_buffer(avctx, frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if (buf_size < avctx->width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "buffer underflow\n");
        return -1;
    }

    for(y = 0; y < avctx->height; y++ ) {
        uint8_t *row = &frame->data[0][ y*frame->linesize[0] ];
        memset(row, 0, avctx->width);
        for (plane = 0; plane < avctx->bits_per_coded_sample; plane++) {
            imemcpy(row, buf, 0, avctx->bits_per_coded_sample, plane, planewidth);
            buf += planewidth;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *frame;
    return buf_size;
}

static int decode_frame_byterun1(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    AVFrame *frame = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    int planewidth = avctx->width / avctx->bits_per_coded_sample;
    int y, plane, x;

    if (avctx->reget_buffer(avctx, frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    for(y = 0; y < avctx->height ; y++ ) {
        uint8_t *row = &frame->data[0][ y*frame->linesize[0] ];
        if (avctx->codec_tag == MKTAG('I','L','B','M')) { //interleaved
            memset(row, 0, avctx->width);
            for (plane = 0; plane < avctx->bits_per_coded_sample; plane++) {
                for(x = 0; x < planewidth && buf < buf_end; ) {
                    char value = *buf++;
                    int length;
                    if (value >= 0) {
                        length = value + 1;
                        imemcpy(row, buf, x, avctx->bits_per_coded_sample, plane, FFMIN3(length, buf_end - buf, planewidth - x));
                        buf += length;
                    } else if (value > -128) {
                        length = -value + 1;
                        imemset(row, *buf++, x, avctx->bits_per_coded_sample, plane, FFMIN(length, planewidth - x));
                    } else { //noop
                        continue;
                    }
                    x += length;
                }
            }
        } else {
            for(x = 0; x < avctx->width && buf < buf_end; ) {
                char value = *buf++;
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
    *(AVFrame*)data = *frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    AVFrame *frame = avctx->priv_data;
    if (frame->data[0])
        avctx->release_buffer(avctx, frame);
    return 0;
}

AVCodec iff_ilbm_decoder = {
    "iff_ilbm",
    CODEC_TYPE_VIDEO,
    CODEC_ID_IFF_ILBM,
    sizeof(AVFrame),
    decode_init,
    NULL,
    decode_end,
    decode_frame_ilbm,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ILBM"),
};

AVCodec iff_byterun1_decoder = {
    "iff_byterun1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_IFF_BYTERUN1,
    sizeof(AVFrame),
    decode_init,
    NULL,
    decode_end,
    decode_frame_byterun1,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ByteRun1"),
};
