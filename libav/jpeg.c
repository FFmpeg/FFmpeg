/*
 * JPEG based formats
 * Copyright (c) 2000, 2001 Gerard Lantau.
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

static int mpjpeg_write_packet(AVFormatContext *s, int stream_index, 
                               UINT8 *buf, int size, int force_pts)
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


/*************************************/
/* single frame JPEG */

static int single_jpeg_write_header(AVFormatContext *s)
{
    return 0;
}

static int single_jpeg_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size, int force_pts)
{
    put_buffer(&s->pb, buf, size);
    put_flush_packet(&s->pb);
    return 1; /* no more data can be sent */
}

static int single_jpeg_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVFormat single_jpeg_format = {
    "singlejpeg",
    "single JPEG image",
    "image/jpeg",
    "jpg,jpeg",
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    single_jpeg_write_header,
    single_jpeg_write_packet,
    single_jpeg_write_trailer,
};

/*************************************/
/* multiple jpeg images */

typedef struct JpegContext {
    char path[1024];
    int img_number;
} JpegContext;

static int jpeg_write_header(AVFormatContext *s1)
{
    JpegContext *s;

    s = av_mallocz(sizeof(JpegContext));
    if (!s)
        return -1;
    s1->priv_data = s;
    nstrcpy(s->path, sizeof(s->path), s1->filename);
    s->img_number = 1;
    return 0;
}

static int jpeg_write_packet(AVFormatContext *s1, int stream_index,
                            UINT8 *buf, int size, int force_pts)
{
    JpegContext *s = s1->priv_data;
    char filename[1024];
    ByteIOContext f1, *pb = &f1;

    if (get_frame_filename(filename, sizeof(filename), 
                           s->path, s->img_number) < 0)
        return -EIO;
    if (url_fopen(pb, filename, URL_WRONLY) < 0)
        return -EIO;

    put_buffer(pb, buf, size);
    put_flush_packet(pb);

    url_fclose(pb);
    s->img_number++;

    return 0;
}

static int jpeg_write_trailer(AVFormatContext *s1)
{
    JpegContext *s = s1->priv_data;
    free(s);
    return 0;
}

/***/

static int jpeg_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    JpegContext *s;
    int i;
    char buf[1024];
    ByteIOContext pb1, *f = &pb1;
    AVStream *st;

    s = av_mallocz(sizeof(JpegContext));
    if (!s)
        return -1;
    s1->priv_data = s;
    nstrcpy(s->path, sizeof(s->path), s1->filename);

    s1->nb_streams = 1;
    st = av_mallocz(sizeof(AVStream));
    if (!st) {
        free(s);
        return -ENOMEM;
    }
    s1->streams[0] = st;
    s->img_number = 0;

    /* try to find the first image */
    for(i=0;i<5;i++) {
        if (get_frame_filename(buf, sizeof(buf), s->path, s->img_number) < 0)
            goto fail;
        if (url_fopen(f, buf, URL_RDONLY) >= 0)
            break;
        s->img_number++;
    }
    if (i == 5)
        goto fail;
    url_fclose(f);
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_MJPEG;
    
    if (!ap || !ap->frame_rate)
        st->codec.frame_rate = 25 * FRAME_RATE_BASE;
    else
        st->codec.frame_rate = ap->frame_rate;
    return 0;
 fail:
    free(s);
    return -EIO;
}

static int jpeg_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    JpegContext *s = s1->priv_data;
    char filename[1024];
    int size;
    ByteIOContext f1, *f = &f1;

    if (get_frame_filename(filename, sizeof(filename), 
                           s->path, s->img_number) < 0)
        return -EIO;
    
    f = &f1;
    if (url_fopen(f, filename, URL_RDONLY) < 0)
        return -EIO;
    
    size = url_seek(url_fileno(f), 0, SEEK_END);
    url_seek(url_fileno(f), 0, SEEK_SET);

    av_new_packet(pkt, size);
    pkt->stream_index = 0;
    get_buffer(f, pkt->data, size);

    url_fclose(f);
    s->img_number++;
    return 0;
}

static int jpeg_read_close(AVFormatContext *s1)
{
    JpegContext *s = s1->priv_data;
    free(s);
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

    jpeg_read_header,
    jpeg_read_packet,
    jpeg_read_close,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};
