/* 
 * RAW encoder and decoder
 * Copyright (c) 2001 Gerard Lantau.
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

/* simple formats */
int raw_write_header(struct AVFormatContext *s)
{
    return 0;
}

int raw_write_packet(struct AVFormatContext *s, 
                     int stream_index,
                     unsigned char *buf, int size)
{
    put_buffer(&s->pb, buf, size);
    put_flush_packet(&s->pb);
    return 0;
}

int raw_write_trailer(struct AVFormatContext *s)
{
    return 0;
}

/* raw input */
static int raw_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = malloc(sizeof(AVStream));
    if (!st)
        return -1;
    s->nb_streams = 1;
    s->streams[0] = st;

    st->id = 0;

    if (ap) {
        if (s->format->audio_codec != CODEC_ID_NONE) {
            st->codec.codec_type = CODEC_TYPE_AUDIO;
            st->codec.codec_id = s->format->audio_codec;
        } else if (s->format->video_codec != CODEC_ID_NONE) {
            st->codec.codec_type = CODEC_TYPE_VIDEO;
            st->codec.codec_id = s->format->video_codec;
        } else {
            free(st);
            return -1;
        }
        
        switch(st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec.sample_rate = ap->sample_rate;
            st->codec.channels = ap->channels;
            /* XXX: endianness */
            break;
        case CODEC_TYPE_VIDEO:
            st->codec.frame_rate = ap->frame_rate;
            st->codec.width = ap->width;
            st->codec.height = ap->height;
            break;
        default:
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

#define RAW_PACKET_SIZE 1024

int raw_read_packet(AVFormatContext *s,
                    AVPacket *pkt)
{
    int ret;

    if (av_new_packet(pkt, RAW_PACKET_SIZE) < 0)
        return -EIO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, RAW_PACKET_SIZE);
    if (ret <= 0) {
        av_free_packet(pkt);
        return -EIO;
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

int raw_read_close(AVFormatContext *s)
{
    return 0;
}

/* mp3 read */
static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = malloc(sizeof(AVStream));
    if (!st)
        return -1;
    s->nb_streams = 1;
    s->streams[0] = st;

    st->id = 0;

    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_id = CODEC_ID_MP2;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* mpeg1/h263 input */
static int video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return -1;
    s->nb_streams = 1;
    s->streams[0] = st;

    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = s->format->video_codec;
    /* for mjpeg, specify frame rate */
    if (st->codec.codec_id == CODEC_ID_MJPEG) {
        if (ap) {
            st->codec.frame_rate = ap->frame_rate;
        } else {
            st->codec.frame_rate = 25 * FRAME_RATE_BASE;
        }
    }
    return 0;
}

AVFormat mp2_format = {
    "mp2",
    "MPEG audio",
    "audio/x-mpeg",
    "mp2,mp3",
    CODEC_ID_MP2,
    0,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,

    mp3_read_header,
    raw_read_packet,
    raw_read_close,
};

AVFormat ac3_format = {
    "ac3",
    "raw ac3",
    "audio/x-ac3", 
    "ac3",
    CODEC_ID_AC3,
    0,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};

AVFormat h263_format = {
    "h263",
    "raw h263",
    "video/x-h263",
    "h263",
    0,
    CODEC_ID_H263,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
    video_read_header,
    raw_read_packet,
    raw_read_close,
};

AVFormat mpeg1video_format = {
    "mpegvideo",
    "MPEG video",
    "video/x-mpeg",
    "mpg,mpeg",
    0,
    CODEC_ID_MPEG1VIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
    video_read_header,
    raw_read_packet,
    raw_read_close,
};

AVFormat mjpeg_format = {
    "mjpeg",
    "MJPEG video",
    "video/x-mjpeg",
    "mjpg,mjpeg",
    0,
    CODEC_ID_MJPEG,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
    video_read_header,
    raw_read_packet,
    raw_read_close,
};

AVFormat pcm_format = {
    "pcm",
    "pcm raw format",
    NULL,
    "sw",
    CODEC_ID_PCM,
    0,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,

    raw_read_header,
    raw_read_packet,
    raw_read_close,
};

int rawvideo_read_packet(AVFormatContext *s,
                         AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec.width;
    height = st->codec.height;

    switch(st->codec.pix_fmt) {
    case PIX_FMT_YUV420P:
        packet_size = (width * height * 3) / 2;
        break;
    case PIX_FMT_YUV422:
        packet_size = (width * height * 2);
        break;
    case PIX_FMT_BGR24:
    case PIX_FMT_RGB24:
        packet_size = (width * height * 3);
        break;
    default:
        abort();
        break;
    }

    if (av_new_packet(pkt, packet_size) < 0)
        return -EIO;

    pkt->stream_index = 0;
    /* bypass buffered I/O */
    ret = url_read(url_fileno(&s->pb), pkt->data, pkt->size);
    if (ret != pkt->size) {
        av_free_packet(pkt);
        return -EIO;
    } else {
        return 0;
    }
}

AVFormat rawvideo_format = {
    "rawvideo",
    "raw video format",
    NULL,
    "yuv",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,

    raw_read_header,
    rawvideo_read_packet,
    raw_read_close,
};
