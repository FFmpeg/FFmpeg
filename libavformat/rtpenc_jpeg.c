/*
 * RTP JPEG-compressed video Packetizer, RFC 2435
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

#include "libavcodec/bytestream.h"
#include "libavcodec/mjpeg.h"
#include "libavutil/intreadwrite.h"
#include "rtpenc.h"

void ff_rtp_send_jpeg(AVFormatContext *s1, const uint8_t *buf, int size)
{
    RTPMuxContext *s = s1->priv_data;
    const uint8_t *qtables = NULL;
    int nb_qtables = 0;
    uint8_t type;
    uint8_t w, h;
    uint8_t *p;
    int off = 0; /* fragment offset of the current JPEG frame */
    int len;
    int i;

    s->buf_ptr   = s->buf;
    s->timestamp = s->cur_timestamp;

    /* convert video pixel dimensions from pixels to blocks */
    w = s1->streams[0]->codec->width  >> 3;
    h = s1->streams[0]->codec->height >> 3;

    /* get the pixel format type or fail */
    if (s1->streams[0]->codec->pix_fmt == AV_PIX_FMT_YUVJ422P ||
        (s1->streams[0]->codec->color_range == AVCOL_RANGE_JPEG &&
         s1->streams[0]->codec->pix_fmt == AV_PIX_FMT_YUV422P)) {
        type = 0;
    } else if (s1->streams[0]->codec->pix_fmt == AV_PIX_FMT_YUVJ420P ||
               (s1->streams[0]->codec->color_range == AVCOL_RANGE_JPEG &&
                s1->streams[0]->codec->pix_fmt == AV_PIX_FMT_YUV420P)) {
        type = 1;
    } else {
        av_log(s1, AV_LOG_ERROR, "Unsupported pixel format\n");
        return;
    }

    /* preparse the header for getting some infos */
    for (i = 0; i < size; i++) {
        if (buf[i] != 0xff)
            continue;

        if (buf[i + 1] == DQT) {
            if (buf[i + 4])
                av_log(s1, AV_LOG_WARNING,
                       "Only 8-bit precision is supported.\n");

            /* a quantization table is 64 bytes long */
            nb_qtables = AV_RB16(&buf[i + 2]) / 65;
            if (i + 4 + nb_qtables * 65 > size) {
                av_log(s1, AV_LOG_ERROR, "Too short JPEG header. Aborted!\n");
                return;
            }

            qtables = &buf[i + 4];
        } else if (buf[i + 1] == SOF0) {
            if (buf[i + 14] != 17 || buf[i + 17] != 17) {
                av_log(s1, AV_LOG_ERROR,
                       "Only 1x1 chroma blocks are supported. Aborted!\n");
                return;
            }
        } else if (buf[i + 1] == SOS) {
            /* SOS is last marker in the header */
            i += AV_RB16(&buf[i + 2]) + 2;
            break;
        }
    }

    /* skip JPEG header */
    buf  += i;
    size -= i;

    for (i = size - 2; i >= 0; i--) {
        if (buf[i] == 0xff && buf[i + 1] == EOI) {
            /* Remove the EOI marker */
            size = i;
            break;
        }
    }

    p = s->buf_ptr;
    while (size > 0) {
        int hdr_size = 8;

        if (off == 0 && nb_qtables)
            hdr_size += 4 + 64 * nb_qtables;

        /* payload max in one packet */
        len = FFMIN(size, s->max_payload_size - hdr_size);

        /* set main header */
        bytestream_put_byte(&p, 0);
        bytestream_put_be24(&p, off);
        bytestream_put_byte(&p, type);
        bytestream_put_byte(&p, 255);
        bytestream_put_byte(&p, w);
        bytestream_put_byte(&p, h);

        if (off == 0 && nb_qtables) {
            /* set quantization tables header */
            bytestream_put_byte(&p, 0);
            bytestream_put_byte(&p, 0);
            bytestream_put_be16(&p, 64 * nb_qtables);

            for (i = 0; i < nb_qtables; i++)
                bytestream_put_buffer(&p, &qtables[65 * i + 1], 64);
        }

        /* copy payload data */
        memcpy(p, buf, len);

        /* marker bit is last packet in frame */
        ff_rtp_send_data(s1, s->buf, len + hdr_size, size == len);

        buf  += len;
        size -= len;
        off  += len;
        p     = s->buf;
    }
}
