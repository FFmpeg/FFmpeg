/*
 * Miscellaneous MJPEG based formats
 * Copyright (c) 2000 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include "avformat.h"

/* Multipart JPEG */

#define BOUNDARY_TAG "ffserver"

static int mpjpeg_write_header(AVFormatContext *s)
{
    UINT8 buf1[256];

    snprintf(buf1, sizeof(buf1), "--%s\n", BOUNDARY_TAG);
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_flush_packet(&s->pb);
    return 0;
}

static int mpjpeg_write_packet(AVFormatContext *s, 
                               int stream_index, UINT8 *buf, int size)
{
    UINT8 buf1[256];

    snprintf(buf1, sizeof(buf1), "Content-type: image/jpeg\n\n");
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_buffer(&s->pb, buf, size);

    snprintf(buf1, sizeof(buf1), "\n--%s\n", BOUNDARY_TAG);
    put_buffer(&s->pb, buf1, strlen(buf1));
    put_flush_packet(&s->pb);
    return 0;
}

static int mpjpeg_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVFormat mpjpeg_format = {
    "mpjpeg",
    "Mime multipart JPEG format",
    "multipart/x-mixed-replace;boundary=" BOUNDARY_TAG,
    "mjpg",
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    mpjpeg_write_header,
    mpjpeg_write_packet,
    mpjpeg_write_trailer,
};


/* single frame JPEG */

static int jpeg_write_header(AVFormatContext *s)
{
    return 0;
}

static int jpeg_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size)
{
    put_buffer(&s->pb, buf, size);
    put_flush_packet(&s->pb);
    return 1; /* no more data can be sent */
}

static int jpeg_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVFormat jpeg_format = {
    "jpeg",
    "JPEG image",
    "image/jpeg",
    "jpg,jpeg",
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    jpeg_write_header,
    jpeg_write_packet,
    jpeg_write_trailer,
};
