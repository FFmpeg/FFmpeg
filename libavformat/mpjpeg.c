/*
 * Multipart JPEG format
 * Copyright (c) 2000, 2001, 2002, 2003 Fabrice Bellard
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

/* Multipart JPEG */

#define BOUNDARY_TAG "ffserver"

static int mpjpeg_write_header(AVFormatContext *s)
{
    uint8_t buf1[256];

    snprintf(buf1, sizeof(buf1), "--%s\r\n", BOUNDARY_TAG);
    avio_write(s->pb, buf1, strlen(buf1));
    avio_flush(s->pb);
    return 0;
}

static int mpjpeg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    uint8_t buf1[256];

    snprintf(buf1, sizeof(buf1), "Content-type: image/jpeg\r\n");
    avio_write(s->pb, buf1, strlen(buf1));

    snprintf(buf1, sizeof(buf1), "Content-length: %d\r\n\r\n", pkt->size);
    avio_write(s->pb, buf1, strlen(buf1));
    avio_write(s->pb, pkt->data, pkt->size);

    snprintf(buf1, sizeof(buf1), "\r\n--%s\r\n", BOUNDARY_TAG);
    avio_write(s->pb, buf1, strlen(buf1));
    return 0;
}

static int mpjpeg_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVOutputFormat ff_mpjpeg_muxer = {
    .name              = "mpjpeg",
    .long_name         = NULL_IF_CONFIG_SMALL("MIME multipart JPEG"),
    .mime_type         = "multipart/x-mixed-replace;boundary=" BOUNDARY_TAG,
    .extensions        = "mjpg",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_MJPEG,
    .write_header      = mpjpeg_write_header,
    .write_packet      = mpjpeg_write_packet,
    .write_trailer     = mpjpeg_write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
};
