/*
 * Multipart JPEG format
 * Copyright (c) 2000, 2001, 2002, 2003 Fabrice Bellard
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
#include "avformat.h"

/* Multipart JPEG */

#define BOUNDARY_TAG "avserver"

static int mpjpeg_write_header(AVFormatContext *s)
{
    avio_printf(s->pb, "--%s\n", BOUNDARY_TAG);
    avio_flush(s->pb);
    return 0;
}

static int mpjpeg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_printf(s->pb,
                "Content-length: %i\n"
                "Content-type: image/jpeg\n\n",
                pkt->size);
    avio_write(s->pb, pkt->data, pkt->size);

    avio_printf(s->pb, "\n--%s\n", BOUNDARY_TAG);
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
