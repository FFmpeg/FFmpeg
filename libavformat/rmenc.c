/*
 * "Real" compatible muxer.
 * Copyright (c) 2000, 2001 Fabrice Bellard
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
#include "rm.h"

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
    AVCodecContext *enc;
} StreamInfo;

typedef struct {
    StreamInfo streams[2];
    StreamInfo *audio_stream, *video_stream;
    int data_pos; /* position of the data after the header */
} RMMuxContext;

/* in ms */
#define BUFFER_DURATION 0


static void put_str(ByteIOContext *s, const char *tag)
{
    put_be16(s,strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

static void put_str8(ByteIOContext *s, const char *tag)
{
    put_byte(s, strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

static void rv10_write_header(AVFormatContext *ctx,
                              int data_size, int index_pos)
{
    RMMuxContext *rm = ctx->priv_data;
    ByteIOContext *s = ctx->pb;
    StreamInfo *stream;
    unsigned char *data_offset_ptr, *start_ptr;
    const char *desc, *mimetype;
    int nb_packets, packet_total_size, packet_max_size, size, packet_avg_size, i;
    int bit_rate, v, duration, flags, data_pos;

    start_ptr = s->buf_ptr;

    put_tag(s, ".RMF");
    put_be32(s,18); /* header size */
    put_be16(s,0);
    put_be32(s,0);
    put_be32(s,4 + ctx->nb_streams); /* num headers */

    put_tag(s,"PROP");
    put_be32(s, 50);
    put_be16(s, 0);
    packet_max_size = 0;
    packet_total_size = 0;
    nb_packets = 0;
    bit_rate = 0;
    duration = 0;
    for(i=0;i<ctx->nb_streams;i++) {
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
    put_be32(s, bit_rate); /* max bit rate */
    put_be32(s, bit_rate); /* avg bit rate */
    put_be32(s, packet_max_size);        /* max packet size */
    if (nb_packets > 0)
        packet_avg_size = packet_total_size / nb_packets;
    else
        packet_avg_size = 0;
    put_be32(s, packet_avg_size);        /* avg packet size */
    put_be32(s, nb_packets);  /* num packets */
    put_be32(s, duration); /* duration */
    put_be32(s, BUFFER_DURATION);           /* preroll */
    put_be32(s, index_pos);           /* index offset */
    /* computation of data the data offset */
    data_offset_ptr = s->buf_ptr;
    put_be32(s, 0);           /* data offset : will be patched after */
    put_be16(s, ctx->nb_streams);    /* num streams */
    flags = 1 | 2; /* save allowed & perfect play */
    if (url_is_streamed(s))
        flags |= 4; /* live broadcast */
    put_be16(s, flags);

    /* comments */

    put_tag(s,"CONT");
    size = strlen(ctx->title) + strlen(ctx->author) + strlen(ctx->copyright) +
        strlen(ctx->comment) + 4 * 2 + 10;
    put_be32(s,size);
    put_be16(s,0);
    put_str(s, ctx->title);
    put_str(s, ctx->author);
    put_str(s, ctx->copyright);
    put_str(s, ctx->comment);

    for(i=0;i<ctx->nb_streams;i++) {
        int codec_data_size;

        stream = &rm->streams[i];

        if (stream->enc->codec_type == CODEC_TYPE_AUDIO) {
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
        put_be32(s, size);
        put_be16(s, 0);

        put_be16(s, i); /* stream number */
        put_be32(s, stream->bit_rate); /* max bit rate */
        put_be32(s, stream->bit_rate); /* avg bit rate */
        put_be32(s, stream->packet_max_size);        /* max packet size */
        if (stream->nb_packets > 0)
            packet_avg_size = stream->packet_total_size /
                stream->nb_packets;
        else
            packet_avg_size = 0;
        put_be32(s, packet_avg_size);        /* avg packet size */
        put_be32(s, 0);           /* start time */
        put_be32(s, BUFFER_DURATION);           /* preroll */
        /* duration */
        if (url_is_streamed(s) || !stream->total_frames)
            put_be32(s, (int)(3600 * 1000));
        else
            put_be32(s, (int)(stream->total_frames * 1000 / stream->frame_rate));
        put_str8(s, desc);
        put_str8(s, mimetype);
        put_be32(s, codec_data_size);

        if (stream->enc->codec_type == CODEC_TYPE_AUDIO) {
            int coded_frame_size, fscode, sample_rate;
            sample_rate = stream->enc->sample_rate;
            coded_frame_size = (stream->enc->bit_rate *
                                stream->enc->frame_size) / (8 * sample_rate);
            /* audio codec info */
            put_tag(s, ".ra");
            put_byte(s, 0xfd);
            put_be32(s, 0x00040000); /* version */
            put_tag(s, ".ra4");
            put_be32(s, 0x01b53530); /* stream length */
            put_be16(s, 4); /* unknown */
            put_be32(s, 0x39); /* header size */

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
            put_be16(s, fscode); /* codec additional info, for AC-3, seems
                                     to be a frequency code */
            /* special hack to compensate rounding errors... */
            if (coded_frame_size == 557)
                coded_frame_size--;
            put_be32(s, coded_frame_size); /* frame length */
            put_be32(s, 0x51540); /* unknown */
            put_be32(s, 0x249f0); /* unknown */
            put_be32(s, 0x249f0); /* unknown */
            put_be16(s, 0x01);
            /* frame length : seems to be very important */
            put_be16(s, coded_frame_size);
            put_be32(s, 0); /* unknown */
            put_be16(s, stream->enc->sample_rate); /* sample rate */
            put_be32(s, 0x10); /* unknown */
            put_be16(s, stream->enc->channels);
            put_str8(s, "Int0"); /* codec name */
            put_str8(s, "dnet"); /* codec name */
            put_be16(s, 0); /* title length */
            put_be16(s, 0); /* author length */
            put_be16(s, 0); /* copyright length */
            put_byte(s, 0); /* end of header */
        } else {
            /* video codec info */
            put_be32(s,34); /* size */
            if(stream->enc->codec_id == CODEC_ID_RV10)
                put_tag(s,"VIDORV10");
            else
                put_tag(s,"VIDORV20");
            put_be16(s, stream->enc->width);
            put_be16(s, stream->enc->height);
            put_be16(s, (int) stream->frame_rate); /* frames per seconds ? */
            put_be32(s,0);     /* unknown meaning */
            put_be16(s, (int) stream->frame_rate);  /* unknown meaning */
            put_be32(s,0);     /* unknown meaning */
            put_be16(s, 8);    /* unknown meaning */
            /* Seems to be the codec version: only use basic H263. The next
               versions seems to add a diffential DC coding as in
               MPEG... nothing new under the sun */
            if(stream->enc->codec_id == CODEC_ID_RV10)
                put_be32(s,0x10000000);
            else
                put_be32(s,0x20103001);
            //put_be32(s,0x10003000);
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
    put_be32(s,data_size + 10 + 8);
    put_be16(s,0);

    put_be32(s, nb_packets); /* number of packets */
    put_be32(s,0); /* next data header */
}

static void write_packet_header(AVFormatContext *ctx, StreamInfo *stream,
                                int length, int key_frame)
{
    int timestamp;
    ByteIOContext *s = ctx->pb;

    stream->nb_packets++;
    stream->packet_total_size += length;
    if (length > stream->packet_max_size)
        stream->packet_max_size =  length;

    put_be16(s,0); /* version */
    put_be16(s,length + 12);
    put_be16(s, stream->num); /* stream number */
    timestamp = (1000 * (float)stream->nb_frames) / stream->frame_rate;
    put_be32(s, timestamp); /* timestamp */
    put_byte(s, 0); /* reserved */
    put_byte(s, key_frame ? 2 : 0); /* flags */
}

static int rm_write_header(AVFormatContext *s)
{
    RMMuxContext *rm = s->priv_data;
    StreamInfo *stream;
    int n;
    AVCodecContext *codec;

    for(n=0;n<s->nb_streams;n++) {
        s->streams[n]->id = n;
        codec = s->streams[n]->codec;
        stream = &rm->streams[n];
        memset(stream, 0, sizeof(StreamInfo));
        stream->num = n;
        stream->bit_rate = codec->bit_rate;
        stream->enc = codec;

        switch(codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            rm->audio_stream = stream;
            stream->frame_rate = (float)codec->sample_rate / (float)codec->frame_size;
            /* XXX: dummy values */
            stream->packet_max_size = 1024;
            stream->nb_packets = 0;
            stream->total_frames = stream->nb_packets;
            break;
        case CODEC_TYPE_VIDEO:
            rm->video_stream = stream;
            stream->frame_rate = (float)codec->time_base.den / (float)codec->time_base.num;
            /* XXX: dummy values */
            stream->packet_max_size = 4096;
            stream->nb_packets = 0;
            stream->total_frames = stream->nb_packets;
            break;
        default:
            return -1;
        }
    }

    rv10_write_header(s, 0, 0);
    put_flush_packet(s->pb);
    return 0;
}

static int rm_write_audio(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    uint8_t *buf1;
    RMMuxContext *rm = s->priv_data;
    ByteIOContext *pb = s->pb;
    StreamInfo *stream = rm->audio_stream;
    int i;

    /* XXX: suppress this malloc */
    buf1= (uint8_t*) av_malloc( size * sizeof(uint8_t) );

    write_packet_header(s, stream, size, !!(flags & PKT_FLAG_KEY));

    /* for AC-3, the words seem to be reversed */
    for(i=0;i<size;i+=2) {
        buf1[i] = buf[i+1];
        buf1[i+1] = buf[i];
    }
    put_buffer(pb, buf1, size);
    put_flush_packet(pb);
    stream->nb_frames++;
    av_free(buf1);
    return 0;
}

static int rm_write_video(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    RMMuxContext *rm = s->priv_data;
    ByteIOContext *pb = s->pb;
    StreamInfo *stream = rm->video_stream;
    int key_frame = !!(flags & PKT_FLAG_KEY);

    /* XXX: this is incorrect: should be a parameter */

    /* Well, I spent some time finding the meaning of these bits. I am
       not sure I understood everything, but it works !! */
#if 1
    write_packet_header(s, stream, size + 7 + (size >= 0x4000)*4, key_frame);
    /* bit 7: '1' if final packet of a frame converted in several packets */
    put_byte(pb, 0x81);
    /* bit 7: '1' if I frame. bits 6..0 : sequence number in current
       frame starting from 1 */
    if (key_frame) {
        put_byte(pb, 0x81);
    } else {
        put_byte(pb, 0x01);
    }
    if(size >= 0x4000){
        put_be32(pb, size); /* total frame size */
        put_be32(pb, size); /* offset from the start or the end */
    }else{
        put_be16(pb, 0x4000 | size); /* total frame size */
        put_be16(pb, 0x4000 | size); /* offset from the start or the end */
    }
#else
    /* full frame */
    write_packet_header(s, size + 6);
    put_byte(pb, 0xc0);
    put_be16(pb, 0x4000 + size); /* total frame size */
    put_be16(pb, 0x4000 + packet_number * 126); /* position in stream */
#endif
    put_byte(pb, stream->nb_frames & 0xff);

    put_buffer(pb, buf, size);
    put_flush_packet(pb);

    stream->nb_frames++;
    return 0;
}

static int rm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (s->streams[pkt->stream_index]->codec->codec_type ==
        CODEC_TYPE_AUDIO)
        return rm_write_audio(s, pkt->data, pkt->size, pkt->flags);
    else
        return rm_write_video(s, pkt->data, pkt->size, pkt->flags);
}

static int rm_write_trailer(AVFormatContext *s)
{
    RMMuxContext *rm = s->priv_data;
    int data_size, index_pos, i;
    ByteIOContext *pb = s->pb;

    if (!url_is_streamed(s->pb)) {
        /* end of file: finish to write header */
        index_pos = url_fseek(pb, 0, SEEK_CUR);
        data_size = index_pos - rm->data_pos;

        /* index */
        put_tag(pb, "INDX");
        put_be32(pb, 10 + 10 * s->nb_streams);
        put_be16(pb, 0);

        for(i=0;i<s->nb_streams;i++) {
            put_be32(pb, 0); /* zero indexes */
            put_be16(pb, i); /* stream number */
            put_be32(pb, 0); /* next index */
        }
        /* undocumented end header */
        put_be32(pb, 0);
        put_be32(pb, 0);

        url_fseek(pb, 0, SEEK_SET);
        for(i=0;i<s->nb_streams;i++)
            rm->streams[i].total_frames = rm->streams[i].nb_frames;
        rv10_write_header(s, data_size, index_pos);
    } else {
        /* undocumented end header */
        put_be32(pb, 0);
        put_be32(pb, 0);
    }
    put_flush_packet(pb);
    return 0;
}


AVOutputFormat rm_muxer = {
    "rm",
    NULL_IF_CONFIG_SMALL("RM format"),
    "application/vnd.rn-realmedia",
    "rm,ra",
    sizeof(RMMuxContext),
    CODEC_ID_AC3,
    CODEC_ID_RV10,
    rm_write_header,
    rm_write_packet,
    rm_write_trailer,
};
