/*
 * CRI image decoder
 *
 * Copyright (c) 2020 Paul B Mahol
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
 * Cintel RAW image decoder
 */

#define BITSTREAM_READER_LE

#include "libavutil/intfloat.h"
#include "libavutil/display.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "thread.h"

typedef struct CRIContext {
    AVCodecContext *jpeg_avctx;   // wrapper context for MJPEG
    AVPacket *jpkt;               // encoded JPEG tile
    AVFrame *jpgframe;            // decoded JPEG tile

    GetByteContext gb;
    int color_model;
    const uint8_t *data;
    unsigned data_size;
    uint64_t tile_size[4];
} CRIContext;

static av_cold int cri_decode_init(AVCodecContext *avctx)
{
    CRIContext *s = avctx->priv_data;
    const AVCodec *codec;
    int ret;

    s->jpgframe = av_frame_alloc();
    if (!s->jpgframe)
        return AVERROR(ENOMEM);

    s->jpkt = av_packet_alloc();
    if (!s->jpkt)
        return AVERROR(ENOMEM);

    codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!codec)
        return AVERROR_BUG;
    s->jpeg_avctx = avcodec_alloc_context3(codec);
    if (!s->jpeg_avctx)
        return AVERROR(ENOMEM);
    s->jpeg_avctx->flags = avctx->flags;
    s->jpeg_avctx->flags2 = avctx->flags2;
    s->jpeg_avctx->idct_algo = avctx->idct_algo;
    ret = avcodec_open2(s->jpeg_avctx, codec, NULL);
    if (ret < 0)
        return ret;

    return 0;
}

static void unpack_10bit(GetByteContext *gb, uint16_t *dst, int shift,
                         int w, int h, ptrdiff_t stride)
{
    int count = w * h;
    int pos = 0;

    while (count > 0) {
        uint32_t a0, a1, a2, a3;
        if (bytestream2_get_bytes_left(gb) < 4)
            break;
        a0 = bytestream2_get_le32(gb);
        a1 = bytestream2_get_le32(gb);
        a2 = bytestream2_get_le32(gb);
        a3 = bytestream2_get_le32(gb);
        dst[pos] = (((a0 >> 1) & 0xE00) | (a0 & 0x1FF)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 1)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a0 >> 13) & 0x3F) | ((a0 >> 14) & 0xFC0)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 2)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a0 >> 26) & 7) | ((a1 & 0x1FF) << 3)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 3)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a1 >> 10) & 0x1FF) | ((a1 >> 11) & 0xE00)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 4)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a1 >> 23) & 0x3F) | ((a2 & 0x3F) << 6)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 5)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a2 >> 7) & 0xFF8) | ((a2 >> 6) & 7)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 6)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a3 & 7) << 9) | ((a2 >> 20) & 0x1FF)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 7)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a3 >> 4) & 0xFC0) | ((a3 >> 3) & 0x3F)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 8)
                break;
            dst += stride;
            pos = 0;
        }
        dst[pos] = (((a3 >> 16) & 7) | ((a3 >> 17) & 0xFF8)) << shift;
        pos++;
        if (pos >= w) {
            if (count == 9)
                break;
            dst += stride;
            pos = 0;
        }

        count -= 9;
    }
}

static int cri_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    CRIContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int ret, bps, hflip = 0, vflip = 0;
    AVFrameSideData *rotation;
    int compressed = 0;

    s->data = NULL;
    s->data_size = 0;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    while (bytestream2_get_bytes_left(gb) > 8) {
        char codec_name[1024];
        uint32_t key, length;
        float framerate;
        int width, height;

        key    = bytestream2_get_le32(gb);
        length = bytestream2_get_le32(gb);

        switch (key) {
        case 1:
            if (length != 4)
                return AVERROR_INVALIDDATA;

            if (bytestream2_get_le32(gb) != MKTAG('D', 'V', 'C', 'C'))
                return AVERROR_INVALIDDATA;
            break;
        case 100:
            if (length < 16)
                return AVERROR_INVALIDDATA;
            width   = bytestream2_get_le32(gb);
            height  = bytestream2_get_le32(gb);
            s->color_model = bytestream2_get_le32(gb);
            if (bytestream2_get_le32(gb) != 1)
                return AVERROR_INVALIDDATA;
            ret = ff_set_dimensions(avctx, width, height);
            if (ret < 0)
                return ret;
            length -= 16;
            goto skip;
        case 101:
            if (length != 4)
                return AVERROR_INVALIDDATA;

            if (bytestream2_get_le32(gb) != 0)
                return AVERROR_INVALIDDATA;
            break;
        case 102:
            bytestream2_get_buffer(gb, codec_name, FFMIN(length, sizeof(codec_name) - 1));
            length -= FFMIN(length, sizeof(codec_name) - 1);
            if (strncmp(codec_name, "cintel_craw", FFMIN(length, sizeof(codec_name) - 1)))
                return AVERROR_INVALIDDATA;
            compressed = 1;
            goto skip;
        case 103:
            if (bytestream2_get_bytes_left(gb) < length)
                return AVERROR_INVALIDDATA;
            s->data = gb->buffer;
            s->data_size = length;
            goto skip;
        case 105:
            if (length <= 0)
                return AVERROR_INVALIDDATA;
            hflip = bytestream2_get_byte(gb) != 0;
            length--;
            goto skip;
        case 106:
            if (length <= 0)
                return AVERROR_INVALIDDATA;
            vflip = bytestream2_get_byte(gb) != 0;
            length--;
            goto skip;
        case 107:
            if (length != 4)
                return AVERROR_INVALIDDATA;
            framerate = av_int2float(bytestream2_get_le32(gb));
            avctx->framerate.num = framerate * 1000;
            avctx->framerate.den = 1000;
            break;
        case 119:
            if (length != 32)
                return AVERROR_INVALIDDATA;

            for (int i = 0; i < 4; i++)
                s->tile_size[i] = bytestream2_get_le64(gb);
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG, "skipping unknown key %u of length %u\n", key, length);
skip:
            bytestream2_skip(gb, length);
        }
    }

    switch (s->color_model) {
    case 76:
    case 88:
        avctx->pix_fmt = AV_PIX_FMT_BAYER_BGGR16;
        break;
    case 77:
    case 89:
        avctx->pix_fmt = AV_PIX_FMT_BAYER_GBRG16;
        break;
    case 78:
    case 90:
        avctx->pix_fmt = AV_PIX_FMT_BAYER_RGGB16;
        break;
    case 45:
    case 79:
    case 91:
        avctx->pix_fmt = AV_PIX_FMT_BAYER_GRBG16;
        break;
    }

    switch (s->color_model) {
    case 45:
        bps = 10;
        break;
    case 76:
    case 77:
    case 78:
    case 79:
        bps = 12;
        break;
    case 88:
    case 89:
    case 90:
    case 91:
        bps = 16;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    if (compressed) {
        for (int i = 0; i < 4; i++) {
            if (s->tile_size[i] >= s->data_size)
                return AVERROR_INVALIDDATA;
        }

        if (s->tile_size[0] + s->tile_size[1] + s->tile_size[2] + s->tile_size[3] !=
            s->data_size)
            return AVERROR_INVALIDDATA;
    }

    if (!s->data || !s->data_size)
        return AVERROR_INVALIDDATA;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    avctx->bits_per_raw_sample = bps;

    if (!compressed && s->color_model == 45) {
        uint16_t *dst = (uint16_t *)p->data[0];
        GetByteContext gb;

        bytestream2_init(&gb, s->data, s->data_size);
        unpack_10bit(&gb, dst, 4, avctx->width, avctx->height, p->linesize[0] / 2);
    } else if (!compressed) {
        GetBitContext gbit;
        const int shift = 16 - bps;

        ret = init_get_bits8(&gbit, s->data, s->data_size);
        if (ret < 0)
            return ret;

        for (int y = 0; y < avctx->height; y++) {
            uint16_t *dst = (uint16_t *)(p->data[0] + y * p->linesize[0]);

            if (get_bits_left(&gbit) < avctx->width * bps)
                break;

            for (int x = 0; x < avctx->width; x++)
                dst[x] = get_bits(&gbit, bps) << shift;
        }
    } else {
        unsigned offset = 0;

        for (int tile = 0; tile < 4; tile++) {
            av_packet_unref(s->jpkt);
            s->jpkt->data = (uint8_t *)s->data + offset;
            s->jpkt->size = s->tile_size[tile];

            ret = avcodec_send_packet(s->jpeg_avctx, s->jpkt);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
                return ret;
            }

            ret = avcodec_receive_frame(s->jpeg_avctx, s->jpgframe);
            if (ret < 0 || s->jpgframe->format != AV_PIX_FMT_GRAY16 ||
                s->jpeg_avctx->width  * 2 != avctx->width ||
                s->jpeg_avctx->height * 2 != avctx->height) {
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "JPEG decoding error (%d).\n", ret);
                } else {
                    av_log(avctx, AV_LOG_ERROR,
                           "JPEG invalid format.\n");
                    ret = AVERROR_INVALIDDATA;
                }

                /* Normally skip, if error explode */
                if (avctx->err_recognition & AV_EF_EXPLODE)
                    return ret;
                else
                    return 0;
            }

            for (int y = 0; y < s->jpeg_avctx->height; y++) {
                const int hw =  s->jpgframe->width / 2;
                uint16_t *dst = (uint16_t *)(p->data[0] + (y * 2) * p->linesize[0] + tile * hw * 2);
                const uint16_t *src = (const uint16_t *)(s->jpgframe->data[0] + y * s->jpgframe->linesize[0]);

                memcpy(dst, src, hw * 2);
                src += hw;
                dst += p->linesize[0] / 2;
                memcpy(dst, src, hw * 2);
            }

            av_frame_unref(s->jpgframe);
            offset += s->tile_size[tile];
        }
    }

    if (hflip || vflip) {
        ff_frame_new_side_data(avctx, p, AV_FRAME_DATA_DISPLAYMATRIX,
                               sizeof(int32_t) * 9, &rotation);
        if (rotation) {
            av_display_rotation_set((int32_t *)rotation->data, 0.f);
            av_display_matrix_flip((int32_t *)rotation->data, hflip, vflip);
        }
    }

    *got_frame = 1;

    return 0;
}

static av_cold int cri_decode_close(AVCodecContext *avctx)
{
    CRIContext *s = avctx->priv_data;

    av_frame_free(&s->jpgframe);
    av_packet_free(&s->jpkt);
    avcodec_free_context(&s->jpeg_avctx);

    return 0;
}

const FFCodec ff_cri_decoder = {
    .p.name         = "cri",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_CRI,
    .priv_data_size = sizeof(CRIContext),
    .init           = cri_decode_init,
    FF_CODEC_DECODE_CB(cri_decode_frame),
    .close          = cri_decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    CODEC_LONG_NAME("Cintel RAW"),
};
