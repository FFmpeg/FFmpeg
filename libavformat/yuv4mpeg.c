/*
 * YUV4MPEG format
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
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

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_LINE_MAX 256

static int yuv4_write_header(AVFormatContext *s)
{
    AVStream *st;
    int width, height;
    int raten, rated, aspectn, aspectd, fps, fps1, n;
    char buf[Y4M_LINE_MAX+1];

    if (s->nb_streams != 1)
        return -EIO;
    
    st = s->streams[0];
    width = st->codec.width;
    height = st->codec.height;
    
    fps = st->codec.frame_rate;
    fps1 = (((float)fps / FRAME_RATE_BASE) * 1000);
   
   /* Sorry about this messy code, but mpeg2enc is very picky about
    * the framerates it accepts. */
    switch(fps1) {
    case 23976:
        raten = 24000; /* turn the framerate into a ratio */
        rated = 1001;
        break;
    case 29970:
        raten = 30000;
        rated = 1001;
        break;
    case 25000:
        raten = 25;
        rated = 1;
        break;
    case 30000:
        raten = 30;
        rated = 1;
        break;
    case 24000:
        raten = 24;
        rated = 1;
        break;
    case 50000:
        raten = 50;
        rated = 1;
        break;
    case 59940:
        raten = 60000;
        rated = 1001;
        break;
    case 60000:
        raten = 60;
        rated = 1;
        break;
    default:
        raten = fps1; /* this setting should work, but often doesn't */
        rated = 1000;
        break;
    }
    
    aspectn = 1;
    aspectd = 1;	/* ffmpeg always uses a 1:1 aspect ratio */

    /* construct stream header, if this is the first frame */
    n = snprintf(buf, sizeof(buf), "%s W%d H%d F%d:%d I%s A%d:%d\n",
                 Y4M_MAGIC,
                 width,
                 height,
                 raten, rated,
                 "p",			/* ffmpeg seems to only output progressive video */
                 aspectn, aspectd);
    if (n < 0) {
        fprintf(stderr, "Error. YUV4MPEG stream header write failed.\n");
        return -EIO;
    } else {
        put_buffer(&s->pb, buf, strlen(buf));
    }
    return 0;
}

static int yuv4_write_packet(AVFormatContext *s, int stream_index,
                             uint8_t *buf, int size, int force_pts)
{
    AVStream *st = s->streams[stream_index];
    ByteIOContext *pb = &s->pb;
    AVPicture *picture;
    int width, height;
    int i, m;
    char buf1[20];
    uint8_t *ptr, *ptr1, *ptr2;

    picture = (AVPicture *)buf;

    /* construct frame header */
    m = snprintf(buf1, sizeof(buf1), "%s \n", Y4M_FRAME_MAGIC);
    put_buffer(pb, buf1, strlen(buf1));

    width = st->codec.width;
    height = st->codec.height;
    
    ptr = picture->data[0];
    for(i=0;i<height;i++) {
        put_buffer(pb, ptr, width);
        ptr += picture->linesize[0];
    }

    height >>= 1;
    width >>= 1;
    ptr1 = picture->data[1];
    ptr2 = picture->data[2];
    for(i=0;i<height;i++) {		/* Cb */
        put_buffer(pb, ptr1, width);
        ptr1 += picture->linesize[1];
    }
    for(i=0;i<height;i++) {	/* Cr */
        put_buffer(pb, ptr2, width);
            ptr2 += picture->linesize[2];
    }
    put_flush_packet(pb);
    return 0;
}

static int yuv4_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVOutputFormat yuv4mpegpipe_oformat = {
    "yuv4mpegpipe",
    "YUV4MPEG pipe format",
    "",
    "yuv4mpeg",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    yuv4_write_header,
    yuv4_write_packet,
    yuv4_write_trailer,
    .flags = AVFMT_RAWPICTURE,
};


