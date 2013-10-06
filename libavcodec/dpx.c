/*
 * DPX (.dpx) image decoder
 * Copyright (c) 2009 Jimmy Christensen
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

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "bytestream.h"
#include "avcodec.h"
#include "internal.h"

static unsigned int read16(const uint8_t **ptr, int is_big)
{
    unsigned int temp;
    if (is_big) {
        temp = AV_RB16(*ptr);
    } else {
        temp = AV_RL16(*ptr);
    }
    *ptr += 2;
    return temp;
}

static unsigned int read32(const uint8_t **ptr, int is_big)
{
    unsigned int temp;
    if (is_big) {
        temp = AV_RB32(*ptr);
    } else {
        temp = AV_RL32(*ptr);
    }
    *ptr += 4;
    return temp;
}

static uint16_t read10in32(const uint8_t **ptr, uint32_t * lbuf,
                                  int * n_datum, int is_big)
{
    if (*n_datum)
        (*n_datum)--;
    else {
        *lbuf = read32(ptr, is_big);
        *n_datum = 2;
    }

    *lbuf = (*lbuf << 10) | (*lbuf >> 22);

    return *lbuf & 0x3FF;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data,
                        int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AVFrame *const p = data;
    uint8_t *ptr[AV_NUM_DATA_POINTERS];

    unsigned int offset;
    int magic_num, endian;
    int x, y, i, ret;
    int w, h, bits_per_color, descriptor, elements, packing, total_size;
    int encoding;

    unsigned int rgbBuffer = 0;
    int n_datum = 0;

    if (avpkt->size <= 1634) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small for DPX header\n");
        return AVERROR_INVALIDDATA;
    }

    magic_num = AV_RB32(buf);
    buf += 4;

    /* Check if the files "magic number" is "SDPX" which means it uses
     * big-endian or XPDS which is for little-endian files */
    if (magic_num == AV_RL32("SDPX")) {
        endian = 0;
    } else if (magic_num == AV_RB32("SDPX")) {
        endian = 1;
    } else {
        av_log(avctx, AV_LOG_ERROR, "DPX marker not found\n");
        return AVERROR_INVALIDDATA;
    }

    offset = read32(&buf, endian);
    if (avpkt->size <= offset) {
        av_log(avctx, AV_LOG_ERROR, "Invalid data start offset\n");
        return AVERROR_INVALIDDATA;
    }
    // Need to end in 0x304 offset from start of file
    buf = avpkt->data + 0x304;
    w = read32(&buf, endian);
    h = read32(&buf, endian);
    if ((ret = av_image_check_size(w, h, 0, avctx)) < 0)
        return ret;

    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);

    // Need to end in 0x320 to read the descriptor
    buf += 20;
    descriptor = buf[0];

    // Need to end in 0x323 to read the bits per color
    buf += 3;
    avctx->bits_per_raw_sample =
    bits_per_color = buf[0];
    buf++;
    packing = read16(&buf, endian);
    encoding = read16(&buf, endian);

    if (packing > 1) {
        avpriv_report_missing_feature(avctx, "Packing %d", packing);
        return AVERROR_PATCHWELCOME;
    }
    if (encoding) {
        avpriv_report_missing_feature(avctx, "Encoding %d", encoding);
        return AVERROR_PATCHWELCOME;
    }

    buf += 820;
    avctx->sample_aspect_ratio.num = read32(&buf, endian);
    avctx->sample_aspect_ratio.den = read32(&buf, endian);
    if (avctx->sample_aspect_ratio.num > 0 && avctx->sample_aspect_ratio.den > 0)
        av_reduce(&avctx->sample_aspect_ratio.num, &avctx->sample_aspect_ratio.den,
                   avctx->sample_aspect_ratio.num,  avctx->sample_aspect_ratio.den,
                  0x10000);
    else
        avctx->sample_aspect_ratio = (AVRational){ 0, 1 };

    switch (descriptor) {
    case 51: // RGBA
        elements = 4;
        break;
    case 50: // RGB
        elements = 3;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Descriptor %d", descriptor);
        return AVERROR_PATCHWELCOME;
    }

    switch (bits_per_color) {
    case 8:
        if (elements == 4) {
            avctx->pix_fmt = AV_PIX_FMT_RGBA;
        } else {
            avctx->pix_fmt = AV_PIX_FMT_RGB24;
        }
        total_size = avctx->width * avctx->height * elements;
        break;
    case 10:
        if (!packing) {
            av_log(avctx, AV_LOG_ERROR, "Packing to 32bit required\n");
            return -1;
        }
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        total_size = (avctx->width * elements + 2) / 3 * 4 * avctx->height;
        break;
    case 12:
        if (!packing) {
            av_log(avctx, AV_LOG_ERROR, "Packing to 16bit required\n");
            return -1;
        }
        avctx->pix_fmt = AV_PIX_FMT_GBRP12;
        total_size = 2 * avctx->width * avctx->height * elements;
        break;
    case 16:
        if (endian) {
            avctx->pix_fmt = elements == 4 ? AV_PIX_FMT_RGBA64BE : AV_PIX_FMT_RGB48BE;
        } else {
            avctx->pix_fmt = elements == 4 ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGB48LE;
        }
        total_size = 2 * avctx->width * avctx->height * elements;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported color depth : %d\n", bits_per_color);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // Move pointer to offset from start of file
    buf =  avpkt->data + offset;

    for (i=0; i<AV_NUM_DATA_POINTERS; i++)
        ptr[i] = p->data[i];

    if (total_size + (int64_t)offset > avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "Overread buffer. Invalid header?\n");
        return AVERROR_INVALIDDATA;
    }
    switch (bits_per_color) {
    case 10:
        for (x = 0; x < avctx->height; x++) {
            uint16_t *dst[3] = {(uint16_t*)ptr[0],
                                (uint16_t*)ptr[1],
                                (uint16_t*)ptr[2]};
            for (y = 0; y < avctx->width; y++) {
                *dst[2]++ = read10in32(&buf, &rgbBuffer,
                                       &n_datum, endian);
                *dst[0]++ = read10in32(&buf, &rgbBuffer,
                                       &n_datum, endian);
                *dst[1]++ = read10in32(&buf, &rgbBuffer,
                                       &n_datum, endian);
                // For 10 bit, ignore alpha
                if (elements == 4)
                    read10in32(&buf, &rgbBuffer,
                               &n_datum, endian);
            }
            n_datum = 0;
            for (i = 0; i < 3; i++)
                ptr[i] += p->linesize[i];
        }
        break;
    case 12:
        for (x = 0; x < avctx->height; x++) {
            uint16_t *dst[3] = {(uint16_t*)ptr[0],
                                (uint16_t*)ptr[1],
                                (uint16_t*)ptr[2]};
            for (y = 0; y < avctx->width; y++) {
                *dst[2] = read16(&buf, endian) >> 4;
                dst[2]++;
                *dst[0] = read16(&buf, endian) >> 4;
                dst[0]++;
                *dst[1] = read16(&buf, endian) >> 4;
                dst[1]++;
                // For 12 bit, ignore alpha
                if (elements == 4)
                    buf += 2;
            }
            for (i = 0; i < 3; i++)
                ptr[i] += p->linesize[i];
        }
        break;
    case 16:
        elements *= 2;
    case 8:
        av_image_copy_plane(ptr[0], p->linesize[0],
                            buf, elements * avctx->width,
                            elements * avctx->width, avctx->height);
        break;
    }

    *got_frame = 1;

    return buf_size;
}

AVCodec ff_dpx_decoder = {
    .name           = "dpx",
    .long_name      = NULL_IF_CONFIG_SMALL("DPX (Digital Picture Exchange) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DPX,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
