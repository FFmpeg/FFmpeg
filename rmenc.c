/*
 * RV 1.0 compatible encoder.
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
#include "mpegenc.h"
#include "mpegvideo.h"

/* in ms */
#define BUFFER_DURATION 0 

typedef struct {
    int nb_packets;
    int packet_total_size;
    int packet_max_size;
    /* codec related output */
    int bit_rate;
    float frame_rate;
    int nb_frames;    /* current frame number */
    int total_frames; /* total number of frames */
    int num;
    AVEncodeContext *enc;
} StreamInfo;

typedef struct {
    StreamInfo streams[2];
    StreamInfo *audio_stream, *video_stream;
    int nb_streams;
    int data_pos; /* position of the data after the header */
} RMContext;

static void put_long(PutByteContext *s, unsigned int val)
{
    put_byte(s, val >> 24);
    put_byte(s, val >> 16);
    put_byte(s, val >> 8);
    put_byte(s, val);
}

static void put_short(PutByteContext *s, unsigned int val)
{
    put_byte(s, val >> 8);
    put_byte(s, val);
}

static void put_str(PutByteContext *s, const char *tag)
{
    put_short(s,strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

static void put_str8(PutByteContext *s, const char *tag)
{
    put_byte(s, strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

int find_tag(const char *tag, char *buf, int buf_size, const char *str)
{
    int len = strlen(tag);
    char *q;

    buf[0] = '\0';
    while (*str) {
        if (*str == '+' && !strncmp(str + 1, tag, len) && str[len+1] == '=') {
            str += len + 2;
            q = buf;
            while (*str && *str != '+' && (q - buf) < (buf_size - 1)) {
                *q++ = *str++;
            }
            /* remove trailing spaces */
            while (q > buf && q[-1] == ' ') q--;
            *q = '\0';
            return 1;
        }
        str++;
    }
    return 0;
}

static void rv10_write_header(AVFormatContext *ctx, 
                              int data_size, int index_pos)
{
    RMContext *rm = ctx->priv_data;
    PutByteContext *s = &ctx->pb;
    StreamInfo *stream;
    unsigned char *data_offset_ptr, *start_ptr;
    char title[1024], author[1024], copyright[1024], comment[1024];
    const char *desc, *mimetype;
    int nb_packets, packet_total_size, packet_max_size, size, packet_avg_size, i;
    int bit_rate, v, duration, flags, data_pos;

    start_ptr = s->buf_ptr;

    put_tag(s, ".RMF");
    put_long(s,18); /* header size */
    put_short(s,0);
    put_long(s,0);
    put_long(s,4 + rm->nb_streams); /* num headers */

    put_tag(s,"PROP");
    put_long(s, 50);
    put_short(s, 0);
    packet_max_size = 0;
    packet_total_size = 0;
    nb_packets = 0;
    bit_rate = 0;
    duration = 0;
    for(i=0;i<rm->nb_streams;i++) {
        StreamInfo *stream = &rm->streams[i];
        bit_rate += stream->bit_rate;
        if (stream->packet_max_size > packet_max_size)
            packet_max_size = stream->packet_max_size;
        nb_packets += stream->nb_packets;
        packet_total_size += stream->packet_total_size;
        /* select maximum duration */
        v = (int) (1000.0 * (float)stream->total_frames / stream->frame_rate);
        if (v > duration)
            duration = v;
    }
    put_long(s, bit_rate); /* max bit rate */
    put_long(s, bit_rate); /* avg bit rate */
    put_long(s, packet_max_size);        /* max packet size */
    if (nb_packets > 0)
        packet_avg_size = packet_total_size / nb_packets;
    else
        packet_avg_size = 0;
    put_long(s, packet_avg_size);        /* avg packet size */
    put_long(s, nb_packets);  /* num packets */
    put_long(s, duration); /* duration */
    put_long(s, BUFFER_DURATION);           /* preroll */
    put_long(s, index_pos);           /* index offset */
    /* computation of data the data offset */
    data_offset_ptr = s->buf_ptr;
    put_long(s, 0);           /* data offset : will be patched after */
    put_short(s, rm->nb_streams);    /* num streams */
    flags = 1 | 2; /* save allowed & perfect play */
    if (ctx->is_streamed)
        flags |= 4; /* live broadcast */
    put_short(s, flags);
    
    /* comments */
    find_tag("title", title, sizeof(title), comment_string);
    find_tag("author", author, sizeof(author), comment_string);
    find_tag("copyright", copyright, sizeof(copyright), comment_string);
    find_tag("comment", comment, sizeof(comment), comment_string);

    put_tag(s,"CONT");
    size = strlen(title) + strlen(author) + strlen(copyright) + strlen(comment) + 
        4 * 2 + 10;
    put_long(s,size);
    put_short(s,0);
    put_str(s, title);
    put_str(s, author);
    put_str(s, copyright);
    put_str(s, comment);
    
    for(i=0;i<rm->nb_streams;i++) {
        int codec_data_size;

        stream = &rm->streams[i];
        
        if (stream->enc->codec->type == CODEC_TYPE_AUDIO) {
            desc = "The Audio Stream";
            mimetype = "audio/x-pn-realaudio";
            codec_data_size = 73;
        } else {
            desc = "The Video Stream";
            mimetype = "video/x-pn-realvideo";
            codec_data_size = 34;
        }

        put_tag(s,"MDPR");
        size = 10 + 9 * 4 + strlen(desc) + strlen(mimetype) + codec_data_size;
        put_long(s, size);
        put_short(s, 0);

        put_short(s, i); /* stream number */
        put_long(s, stream->bit_rate); /* max bit rate */
        put_long(s, stream->bit_rate); /* avg bit rate */
        put_long(s, stream->packet_max_size);        /* max packet size */
        if (stream->nb_packets > 0)
            packet_avg_size = stream->packet_total_size / 
                stream->nb_packets;
        else
            packet_avg_size = 0;
        put_long(s, packet_avg_size);        /* avg packet size */
        put_long(s, 0);           /* start time */
        put_long(s, BUFFER_DURATION);           /* preroll */
        /* duration */
        put_long(s, (int)(stream->total_frames * 1000 / stream->frame_rate));
        put_str8(s, desc);
        put_str8(s, mimetype);
        put_long(s, codec_data_size);
        
        if (stream->enc->codec->type == CODEC_TYPE_AUDIO) {
            int coded_frame_size, fscode, sample_rate;
            sample_rate = stream->enc->rate;
            coded_frame_size = (stream->enc->bit_rate * 
                                stream->enc->frame_size) / (8 * sample_rate);
            /* audio codec info */
            put_tag(s, ".ra");
            put_byte(s, 0xfd);
            put_long(s, 0x00040000); /* version */
            put_tag(s, ".ra4");
            put_long(s, 0x01b53530); /* stream length */
            put_short(s, 4); /* unknown */
            put_long(s, 0x39); /* header size */

            switch(sample_rate) {
            case 48000:
            case 24000:
            case 12000:
                fscode = 1;
                break;
            default:
            case 44100:
            case 22050:
            case 11025:
                fscode = 2;
                break;
            case 32000:
            case 16000:
            case 8000:
                fscode = 3;
            }
            put_short(s, fscode); /* codec additional info, for AC3, seems
                                     to be a frequency code */
            /* special hack to compensate rounding errors... */
            if (coded_frame_size == 557)
                coded_frame_size--;
            put_long(s, coded_frame_size); /* frame length */
            put_long(s, 0x51540); /* unknown */
            put_long(s, 0x249f0); /* unknown */
            put_long(s, 0x249f0); /* unknown */
            put_short(s, 0x01);
            /* frame length : seems to be very important */
            put_short(s, coded_frame_size); 
            put_long(s, 0); /* unknown */
            put_short(s, stream->enc->rate); /* sample rate */
            put_long(s, 0x10); /* unknown */
            put_short(s, stream->enc->channels);
            put_str8(s, "Int0"); /* codec name */
            put_str8(s, "dnet"); /* codec name */
            put_short(s, 0); /* title length */
            put_short(s, 0); /* author length */
            put_short(s, 0); /* copyright length */
            put_byte(s, 0); /* end of header */
        } else {
            /* video codec info */
            put_long(s,34); /* size */
            put_tag(s,"VIDORV10");
            put_short(s, stream->enc->width);
            put_short(s, stream->enc->height);
            put_short(s, 24); /* frames per seconds ? */
            put_long(s,0);     /* unknown meaning */
            put_short(s, 12);  /* unknown meaning */
            put_long(s,0);     /* unknown meaning */
            put_short(s, 8);    /* unknown meaning */
            /* Seems to be the codec version: only use basic H263. The next
               versions seems to add a diffential DC coding as in
               MPEG... nothing new under the sun */
            put_long(s,0x10000000); 
            //put_long(s,0x10003000); 
        }
    }

    /* patch data offset field */
    data_pos = s->buf_ptr - start_ptr;
    rm->data_pos = data_pos;
    data_offset_ptr[0] = data_pos >> 24;
    data_offset_ptr[1] = data_pos >> 16;
    data_offset_ptr[2] = data_pos >> 8;
    data_offset_ptr[3] = data_pos;
    
    /* data stream */
    put_tag(s,"DATA");
    put_long(s,data_size + 10 + 8);
    put_short(s,0);

    put_long(s, nb_packets); /* number of packets */
    put_long(s,0); /* next data header */
}

static void write_packet_header(AVFormatContext *ctx, StreamInfo *stream, 
                                int length, int key_frame)
{
    int timestamp;
    PutByteContext *s = &ctx->pb;

    stream->nb_packets++;
    stream->packet_total_size += length;
    if (length > stream->packet_max_size)
        stream->packet_max_size =  length;

    put_short(s,0); /* version */
    put_short(s,length + 12);
    put_short(s, stream->num); /* stream number */
    timestamp = (1000 * (float)stream->nb_frames) / stream->frame_rate;
    put_long(s, timestamp); /* timestamp */
    put_byte(s, 0); /* reserved */
    put_byte(s, key_frame ? 2 : 0); /* flags */
}

static int rm_write_header(AVFormatContext *s)
{
    StreamInfo *stream;
    RMContext *rm;
    int n;

    rm = malloc(sizeof(RMContext));
    if (!rm)
        return -1;
    memset(rm, 0, sizeof(RMContext));
    s->priv_data = rm;
    n = 0;
    if (s->audio_enc) {
        stream = &rm->streams[n];
        memset(stream, 0, sizeof(StreamInfo));
        stream->num = n;
        rm->audio_stream = stream;
        stream->bit_rate = s->audio_enc->bit_rate;
        stream->frame_rate = (float)s->audio_enc->rate / (float)s->audio_enc->frame_size;
        stream->enc = s->audio_enc;
        /* XXX: dummy values */
        stream->packet_max_size = 1024;
        stream->nb_packets = 1000;
        stream->total_frames = stream->nb_packets;
        n++;
    }
    if (s->video_enc) {
        stream = &rm->streams[n];
        memset(stream, 0, sizeof(StreamInfo));
        stream->num = n;
        rm->video_stream = stream;
        stream->bit_rate = s->video_enc->bit_rate;
        stream->frame_rate = s->video_enc->rate;
        stream->enc = s->video_enc;
        /* XXX: dummy values */
        stream->packet_max_size = 4096;
        stream->nb_packets = 1000;
        stream->total_frames = stream->nb_packets;
        n++;
    }
    rm->nb_streams = n;

    rv10_write_header(s, 0, 0);
    put_flush_packet(&s->pb);
    return 0;
}

static int rm_write_audio(AVFormatContext *s, UINT8 *buf, int size)
{
    UINT8 buf1[size];
    RMContext *rm = s->priv_data;
    PutByteContext *pb = &s->pb;
    StreamInfo *stream = rm->audio_stream;
    int i;

    write_packet_header(s, stream, size, stream->enc->key_frame);
    
    /* for AC3, the words seems to be reversed */
    for(i=0;i<size;i+=2) {
        buf1[i] = buf[i+1];
        buf1[i+1] = buf[i];
    }
    put_buffer(pb, buf1, size);
    put_flush_packet(pb);
    stream->nb_frames++;
    return 0;
}

static int rm_write_video(AVFormatContext *s, UINT8 *buf, int size)
{
    RMContext *rm = s->priv_data;
    PutByteContext *pb = &s->pb;
    StreamInfo *stream = rm->video_stream;
    int key_frame = stream->enc->key_frame;
    
    /* XXX: this is incorrect: should be a parameter */

    /* Well, I spent some time finding the meaning of these bits. I am
       not sure I understood everything, but it works !! */
#if 1
    write_packet_header(s, stream, size + 7, key_frame);
    /* bit 7: '1' if final packet of a frame converted in several packets */
    put_byte(pb, 0x81); 
    /* bit 7: '1' if I frame. bits 6..0 : sequence number in current
       frame starting from 1 */
    if (key_frame) {
        put_byte(pb, 0x81); 
    } else {
        put_byte(pb, 0x01); 
    }
    put_short(pb, 0x4000 | (size)); /* total frame size */
    put_short(pb, 0x4000 | (size));              /* offset from the start or the end */
#else
    /* seems to be used for prefetch/error correction. Help me ! */
    write_packet_header(s, size + 6);
    put_byte(pb, 0xc0); 
    put_short(pb, 0x4000 | size); /* total frame size */
    put_short(pb, 0x4000 + packet_number * 126);
#endif
    put_byte(pb, stream->nb_frames & 0xff); 
    
    put_buffer(pb, buf, size);
    put_flush_packet(pb);

    stream->nb_frames++;
    return 0;
}

static int rm_write_trailer(AVFormatContext *s)
{
    RMContext *rm = s->priv_data;
    int data_size, index_pos, i;
    PutByteContext *pb = &s->pb;

    if (!s->is_streamed) {
        /* end of file: finish to write header */
        index_pos = put_seek(pb, 0, SEEK_CUR);
        data_size = index_pos - rm->data_pos;

        /* index */
        put_tag(pb, "INDX");
        put_long(pb, 10 + 10 * rm->nb_streams);
        put_short(pb, 0);
        
        for(i=0;i<rm->nb_streams;i++) {
            put_long(pb, 0); /* zero indices */
            put_short(pb, i); /* stream number */
            put_long(pb, 0); /* next index */
        }
        /* undocumented end header */
        put_long(pb, 0);
        put_long(pb, 0);
        
        put_seek(pb, 0, SEEK_SET);
        for(i=0;i<rm->nb_streams;i++)
            rm->streams[i].total_frames = rm->streams[i].nb_frames;
        rv10_write_header(s, data_size, index_pos);
    } else {
        /* undocumented end header */
        put_long(pb, 0);
        put_long(pb, 0);
    }
    put_flush_packet(pb);

    free(rm);
    return 0;
}

AVFormat rm_format = {
    "rm",
    "rm format",
    "audio/x-pn-realaudio",
    "rm,ra",
    CODEC_ID_AC3,
    CODEC_ID_RV10,
    rm_write_header,
    rm_write_audio,
    rm_write_video,
    rm_write_trailer,
};

AVFormat ra_format = {
    "ra",
    "ra format",
    "audio/x-pn-realaudio",
    "ra",
    CODEC_ID_AC3,
    CODEC_ID_NONE,
    rm_write_header,
    rm_write_audio,
    NULL,
    rm_write_trailer,
};
