/*
 * Output a MPEG1 multiplexed video/audio stream
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
#include <linux/videodev.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>

#include "mpegenc.h"
#include "mpegvideo.h"
#include "mpegaudio.h"

#define MAX_PAYLOAD_SIZE 4096
#define NB_STREAMS 2

typedef struct {
    UINT8 buffer[MAX_PAYLOAD_SIZE];
    int buffer_ptr;
    UINT8 id;
    int max_buffer_size;
    int packet_number;
    AVEncodeContext *enc;
    float pts;
} StreamInfo;

typedef struct {
    int packet_size; /* required packet size */
    int packet_number;
    int pack_header_freq;     /* frequency (in packets^-1) at which we send pack headers */
    int system_header_freq;
    int mux_rate; /* bitrate in units of 50 bytes/s */
    /* stream info */
    int nb_streams;
    StreamInfo streams[NB_STREAMS];
    AVFormatContext *ctx;
} MpegMuxContext;

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)
  
#define AUDIO_ID 0xc0
#define VIDEO_ID 0xe0

static int put_pack_header(MpegMuxContext *s, UINT8 *buf, long long timestamp)
{
    PutBitContext pb;
    
    init_put_bits(&pb, buf, 128, NULL, NULL);

    put_bits(&pb, 32, PACK_START_CODE);
    put_bits(&pb, 4, 0x2);
    put_bits(&pb, 3, (timestamp >> 30) & 0x07);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (timestamp >> 15) & 0x7fff);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (timestamp) & 0x7fff);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 22, s->mux_rate);

    flush_put_bits(&pb);
    return pb.buf_ptr - pb.buf;
}

static int put_system_header(MpegMuxContext *s, UINT8 *buf)
{
    int audio_bound, video_bound;
    int size, rate_bound, i;
    PutBitContext pb;

    init_put_bits(&pb, buf, 128, NULL, NULL);

    put_bits(&pb, 32, SYSTEM_HEADER_START_CODE);
    put_bits(&pb, 16, 0);
    put_bits(&pb, 1, 1);
    
    rate_bound = s->mux_rate; /* maximum bit rate of the multiplexed stream */
    put_bits(&pb, 22, rate_bound);
    put_bits(&pb, 1, 1); /* marker */
    audio_bound = 1; /* at most one audio stream */
    put_bits(&pb, 6, audio_bound);

    put_bits(&pb, 1, 0); /* variable bitrate */
    put_bits(&pb, 1, 0); /* non constrainted bit stream */
    
    put_bits(&pb, 1, 1); /* audio locked */
    put_bits(&pb, 1, 1); /* video locked */
    put_bits(&pb, 1, 1); /* marker */

    video_bound = 1; /* at most one video stream */
    put_bits(&pb, 5, video_bound);
    put_bits(&pb, 8, 0xff); /* reserved byte */
    
    /* audio stream info */
    for(i=0;i<s->nb_streams;i++) {
        put_bits(&pb, 8, s->streams[i].id); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 1); /* buffer bound scale = 1024 */
        put_bits(&pb, 13, s->streams[i].max_buffer_size); /* max buffer size */
    }
    /* no more streams */
    put_bits(&pb, 1, 0);
    flush_put_bits(&pb);
    size = pb.buf_ptr - pb.buf;
    /* patch packet size */
    buf[4] = (size - 6) >> 8;
    buf[5] = (size - 6) & 0xff;

    return size;
}

/* Format a packet header for a total size of 'total_size'. Return the
   header size */
static int put_packet_header(MpegMuxContext *s, 
                             int id, long long timestamp,
                             UINT8 *buffer, int total_size)
{
    UINT8 *buf_ptr;
    PutBitContext pb;
    int size, payload_size;

#if 0
    printf("packet ID=%2x PTS=%0.3f size=%d\n", 
           id, timestamp / 90000.0, total_size);
#endif

    buf_ptr = buffer;

    if ((s->packet_number % s->pack_header_freq) == 0) {
        /* output pack and systems header */
        size = put_pack_header(s, buf_ptr, timestamp);
        buf_ptr += size;
        if ((s->packet_number % s->system_header_freq) == 0) {
            size = put_system_header(s, buf_ptr);
            buf_ptr += size;
        }
    }

    payload_size = total_size - ((buf_ptr - buffer) + 6 + 5);
    /* packet header */
    init_put_bits(&pb, buf_ptr, 128, NULL, NULL);

    put_bits(&pb, 32, PACKET_START_CODE_PREFIX + id);
    put_bits(&pb, 16, payload_size + 5);
    /* presentation time stamp */
    put_bits(&pb, 4, 0x02);
    put_bits(&pb, 3, (timestamp >> 30) & 0x07);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (timestamp >> 15) & 0x7fff);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (timestamp) & 0x7fff);
    put_bits(&pb, 1, 1);

    flush_put_bits(&pb);

    s->packet_number++;
    return pb.buf_ptr - buffer;
}

int mpeg_mux_init(AVFormatContext *ctx)
{
    MpegMuxContext *s;
    int bitrate, i;
    
    s = malloc(sizeof(MpegMuxContext));
    if (!s)
        return -1;
    memset(s, 0, sizeof(MpegMuxContext));
    ctx->priv_data = s;
    s->ctx = ctx;
    s->packet_number = 0;

    /* XXX: hardcoded */
    s->packet_size = 2048;
    s->nb_streams = 2;
    s->streams[0].id = AUDIO_ID;
    s->streams[0].max_buffer_size = 10; /* in KBytes */
    s->streams[0].enc = ctx->audio_enc;
    s->streams[1].id = VIDEO_ID;
    s->streams[1].max_buffer_size = 50; /* in KBytes */
    s->streams[1].enc = ctx->video_enc;

    /* we increase slightly the bitrate to take into account the
       headers. XXX: compute it exactly */
    bitrate = 2000;
    for(i=0;i<s->nb_streams;i++) {
        bitrate += s->streams[i].enc->bit_rate;
    }
    s->mux_rate = (bitrate + (8 * 50) - 1) / (8 * 50);
    /* every 2 seconds */
    s->pack_header_freq = 2 * bitrate / s->packet_size / 8;
    /* every 10 seconds */
    s->system_header_freq = s->pack_header_freq * 5;

    for(i=0;i<NB_STREAMS;i++) {
        s->streams[i].buffer_ptr = 0;
        s->streams[i].packet_number = 0;
        s->streams[i].pts = 0;
    }
    return 0;
}

int mpeg_mux_end(AVFormatContext *ctx)
{
    PutBitContext pb;
    UINT8 buffer[128];

    /* write the end header */
    init_put_bits(&pb, buffer, sizeof(buffer), NULL, NULL);
    put_bits(&pb, 32, ISO_11172_END_CODE);

    put_buffer(&ctx->pb, buffer, pb.buf_ptr - buffer);
    put_flush_packet(&ctx->pb);
    return 0;
}

static void write_stream(MpegMuxContext *s, StreamInfo *stream, UINT8 *buf, int size)
{
    int len, len1, header_size;
    long long pts;
    while (size > 0) {
        if (stream->buffer_ptr == 0) {
            pts = stream->pts * 90000.0;
            header_size = put_packet_header(s, stream->id, pts, stream->buffer, s->packet_size);
            stream->buffer_ptr = header_size;
        }
        len = size;
        len1 = s->packet_size - stream->buffer_ptr;
        if (len > len1)
            len = len1;
        memcpy(stream->buffer + stream->buffer_ptr, buf, len);
        stream->buffer_ptr += len;
        if (stream->buffer_ptr == s->packet_size) {
            /* output the packet */
            put_buffer(&s->ctx->pb, stream->buffer, s->packet_size);
            put_flush_packet(&s->ctx->pb);
            stream->buffer_ptr = 0;
            stream->packet_number++;
        }
        buf += len;
        size -= len;
    }
}

static int mpeg_mux_write_audio(AVFormatContext *ctx, UINT8 *buf, int size)
{
    MpegMuxContext *s = ctx->priv_data;
    
    write_stream(s, &s->streams[0], buf, size);
    s->streams[0].pts += (float)s->streams[0].enc->frame_size / s->streams[0].enc->rate;
    return 0;
}

int mpeg_mux_write_video(AVFormatContext *ctx, UINT8 *buf, int size)
{
    MpegMuxContext *s = ctx->priv_data;

    write_stream(s, &s->streams[1], buf, size);
    s->streams[1].pts += 1.0 / (float)s->streams[1].enc->rate;

    return 0;
}

AVFormat mpeg_mux_format = {
    "mpeg1",
    "MPEG1 multiplex format",
    "video/mpeg",
    "mpg,mpeg",
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_audio,
    mpeg_mux_write_video,
    mpeg_mux_end,
};
