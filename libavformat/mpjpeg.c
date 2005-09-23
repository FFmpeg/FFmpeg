/*
 * Multipart JPEG format
 * Copyright (c) 2000, 2001, 2002, 2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

/* Multipart JPEG */

#define BOUNDARY_TAG "ffserver"

#ifdef CONFIG_MUXERS
static int mpjpeg_write_header(AVFormatContext *s)
{
    uint8_t buf1[256];

    snprintf(buf1, sizeof(buf1), "--%s\n", BOUNDARY_TAG);
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_flush_packet(&s->pb);
    return 0;
}

static int mpjpeg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    uint8_t buf1[256];

    snprintf(buf1, sizeof(buf1), "Content-type: image/jpeg\n\n");
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_buffer(&s->pb, pkt->data, pkt->size);

    snprintf(buf1, sizeof(buf1), "\n--%s\n", BOUNDARY_TAG);
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_flush_packet(&s->pb);
    return 0;
}

static int mpjpeg_write_trailer(AVFormatContext *s)
{
    return 0;
}

static AVOutputFormat mpjpeg_format = {
    "mpjpeg",
    "Mime multipart JPEG format",
    "multipart/x-mixed-replace;boundary=" BOUNDARY_TAG,
    "mjpg",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    mpjpeg_write_header,
    mpjpeg_write_packet,
    mpjpeg_write_trailer,
};

int jpeg_init(void)
{
    av_register_output_format(&mpjpeg_format);
    return 0;
}
#endif //CONFIG_MUXERS
