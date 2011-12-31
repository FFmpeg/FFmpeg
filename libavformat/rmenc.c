/*
 * "Real" compatible muxer.
 * Copyright (c) 2000, 2001 Fabrice Bellard
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
#include "avio_internal.h"
#include "rm.h"
#include "libavutil/dict.h"

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


static void put_str(AVIOContext *s, const char *tag)
{
    avio_wb16(s,strlen(tag));
    while (*tag) {
        avio_w8(s, *tag++);
    }
}

static void put_str8(AVIOContext *s, const char *tag)
{
    avio_w8(s, strlen(tag));
    while (*tag) {
        avio_w8(s, *tag++);
    }
}

static int rv10_write_header(AVFormatContext *ctx,
                             int data_size, int index_pos)
{
    RMMuxContext *rm = ctx->priv_data;
    AVIOContext *s = ctx->pb;
    StreamInfo *stream;
    unsigned char *data_offset_ptr, *start_ptr;
    const char *desc, *mimetype;
    int nb_packets, packet_total_size, packet_max_size, size, packet_avg_size, i;
    int bit_rate, v, duration, flags, data_pos;
    AVDictionaryEntry *tag;

    start_ptr = s->buf_ptr;

    ffio_wfourcc(s, ".RMF");
    avio_wb32(s,18); /* header size */
    avio_wb16(s,0);
    avio_wb32(s,0);
    avio_wb32(s,4 + ctx->nb_streams); /* num headers */

    ffio_wfourcc(s,"PROP");
    avio_wb32(s, 50);
    avio_wb16(s, 0);
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
    avio_wb32(s, bit_rate); /* max bit rate */
    avio_wb32(s, bit_rate); /* avg bit rate */
    avio_wb32(s, packet_max_size);        /* max packet size */
    if (nb_packets > 0)
        packet_avg_size = packet_total_size / nb_packets;
    else
        packet_avg_size = 0;
    avio_wb32(s, packet_avg_size);        /* avg packet size */
    avio_wb32(s, nb_packets);  /* num packets */
    avio_wb32(s, duration); /* duration */
    avio_wb32(s, BUFFER_DURATION);           /* preroll */
    avio_wb32(s, index_pos);           /* index offset */
    /* computation of data the data offset */
    data_offset_ptr = s->buf_ptr;
    avio_wb32(s, 0);           /* data offset : will be patched after */
    avio_wb16(s, ctx->nb_streams);    /* num streams */
    flags = 1 | 2; /* save allowed & perfect play */
    if (!s->seekable)
        flags |= 4; /* live broadcast */
    avio_wb16(s, flags);

    /* comments */

    ffio_wfourcc(s,"CONT");
    size =  4 * 2 + 10;
    for(i=0; i<FF_ARRAY_ELEMS(ff_rm_metadata); i++) {
        tag = av_dict_get(ctx->metadata, ff_rm_metadata[i], NULL, 0);
        if(tag) size += strlen(tag->value);
    }
    avio_wb32(s,size);
    avio_wb16(s,0);
    for(i=0; i<FF_ARRAY_ELEMS(ff_rm_metadata); i++) {
        tag = av_dict_get(ctx->metadata, ff_rm_metadata[i], NULL, 0);
        put_str(s, tag ? tag->value : "");
    }

    for(i=0;i<ctx->nb_streams;i++) {
        int codec_data_size;

        stream = &rm->streams[i];

        if (stream->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            desc = "The Audio Stream";
            mimetype = "audio/x-pn-realaudio";
            codec_data_size = 73;
        } else {
            desc = "The Video Stream";
            mimetype = "video/x-pn-realvideo";
            codec_data_size = 34;
        }

        ffio_wfourcc(s,"MDPR");
        size = 10 + 9 * 4 + strlen(desc) + strlen(mimetype) + codec_data_size;
        avio_wb32(s, size);
        avio_wb16(s, 0);

        avio_wb16(s, i); /* stream number */
        avio_wb32(s, stream->bit_rate); /* max bit rate */
        avio_wb32(s, stream->bit_rate); /* avg bit rate */
        avio_wb32(s, stream->packet_max_size);        /* max packet size */
        if (stream->nb_packets > 0)
            packet_avg_size = stream->packet_total_size /
                stream->nb_packets;
        else
            packet_avg_size = 0;
        avio_wb32(s, packet_avg_size);        /* avg packet size */
        avio_wb32(s, 0);           /* start time */
        avio_wb32(s, BUFFER_DURATION);           /* preroll */
        /* duration */
        if (!s->seekable || !stream->total_frames)
            avio_wb32(s, (int)(3600 * 1000));
        else
            avio_wb32(s, (int)(stream->total_frames * 1000 / stream->frame_rate));
        put_str8(s, desc);
        put_str8(s, mimetype);
        avio_wb32(s, codec_data_size);

        if (stream->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            int coded_frame_size, fscode, sample_rate;
            sample_rate = stream->enc->sample_rate;
            coded_frame_size = (stream->enc->bit_rate *
                                stream->enc->frame_size) / (8 * sample_rate);
            /* audio codec info */
            avio_write(s, ".ra", 3);
            avio_w8(s, 0xfd);
            avio_wb32(s, 0x00040000); /* version */
            ffio_wfourcc(s, ".ra4");
            avio_wb32(s, 0x01b53530); /* stream length */
            avio_wb16(s, 4); /* unknown */
            avio_wb32(s, 0x39); /* header size */

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
            avio_wb16(s, fscode); /* codec additional info, for AC-3, seems
                                     to be a frequency code */
            /* special hack to compensate rounding errors... */
            if (coded_frame_size == 557)
                coded_frame_size--;
            avio_wb32(s, coded_frame_size); /* frame length */
            avio_wb32(s, 0x51540); /* unknown */
            avio_wb32(s, 0x249f0); /* unknown */
            avio_wb32(s, 0x249f0); /* unknown */
            avio_wb16(s, 0x01);
            /* frame length : seems to be very important */
            avio_wb16(s, coded_frame_size);
            avio_wb32(s, 0); /* unknown */
            avio_wb16(s, stream->enc->sample_rate); /* sample rate */
            avio_wb32(s, 0x10); /* unknown */
            avio_wb16(s, stream->enc->channels);
            put_str8(s, "Int0"); /* codec name */
            if (stream->enc->codec_tag) {
                avio_w8(s, 4); /* tag length */
                avio_wl32(s, stream->enc->codec_tag);
            } else {
                av_log(ctx, AV_LOG_ERROR, "Invalid codec tag\n");
                return -1;
            }
            avio_wb16(s, 0); /* title length */
            avio_wb16(s, 0); /* author length */
            avio_wb16(s, 0); /* copyright length */
            avio_w8(s, 0); /* end of header */
        } else {
            /* video codec info */
            avio_wb32(s,34); /* size */
            ffio_wfourcc(s, "VIDO");
            if(stream->enc->codec_id == CODEC_ID_RV10)
                ffio_wfourcc(s,"RV10");
            else
                ffio_wfourcc(s,"RV20");
            avio_wb16(s, stream->enc->width);
            avio_wb16(s, stream->enc->height);
            avio_wb16(s, (int) stream->frame_rate); /* frames per seconds ? */
            avio_wb32(s,0);     /* unknown meaning */
            avio_wb16(s, (int) stream->frame_rate);  /* unknown meaning */
            avio_wb32(s,0);     /* unknown meaning */
            avio_wb16(s, 8);    /* unknown meaning */
            /* Seems to be the codec version: only use basic H263. The next
               versions seems to add a diffential DC coding as in
               MPEG... nothing new under the sun */
            if(stream->enc->codec_id == CODEC_ID_RV10)
                avio_wb32(s,0x10000000);
            else
                avio_wb32(s,0x20103001);
            //avio_wb32(s,0x10003000);
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
    ffio_wfourcc(s, "DATA");
    avio_wb32(s,data_size + 10 + 8);
    avio_wb16(s,0);

    avio_wb32(s, nb_packets); /* number of packets */
    avio_wb32(s,0); /* next data header */
    return 0;
}

static void write_packet_header(AVFormatContext *ctx, StreamInfo *stream,
                                int length, int key_frame)
{
    int timestamp;
    AVIOContext *s = ctx->pb;

    stream->nb_packets++;
    stream->packet_total_size += length;
    if (length > stream->packet_max_size)
        stream->packet_max_size =  length;

    avio_wb16(s,0); /* version */
    avio_wb16(s,length + 12);
    avio_wb16(s, stream->num); /* stream number */
    timestamp = (1000 * (float)stream->nb_frames) / stream->frame_rate;
    avio_wb32(s, timestamp); /* timestamp */
    avio_w8(s, 0); /* reserved */
    avio_w8(s, key_frame ? 2 : 0); /* flags */
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
        case AVMEDIA_TYPE_AUDIO:
            rm->audio_stream = stream;
            stream->frame_rate = (float)codec->sample_rate / (float)codec->frame_size;
            /* XXX: dummy values */
            stream->packet_max_size = 1024;
            stream->nb_packets = 0;
            stream->total_frames = stream->nb_packets;
            break;
        case AVMEDIA_TYPE_VIDEO:
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

    if (rv10_write_header(s, 0, 0))
        return AVERROR_INVALIDDATA;
    avio_flush(s->pb);
    return 0;
}

static int rm_write_audio(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    uint8_t *buf1;
    RMMuxContext *rm = s->priv_data;
    AVIOContext *pb = s->pb;
    StreamInfo *stream = rm->audio_stream;
    int i;

    /* XXX: suppress this malloc */
    buf1 = av_malloc(size * sizeof(uint8_t));

    write_packet_header(s, stream, size, !!(flags & AV_PKT_FLAG_KEY));

    if (stream->enc->codec_id == CODEC_ID_AC3) {
        /* for AC-3, the words seem to be reversed */
        for(i=0;i<size;i+=2) {
            buf1[i] = buf[i+1];
            buf1[i+1] = buf[i];
        }
        avio_write(pb, buf1, size);
    } else {
        avio_write(pb, buf, size);
    }
    avio_flush(pb);
    stream->nb_frames++;
    av_free(buf1);
    return 0;
}

static int rm_write_video(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    RMMuxContext *rm = s->priv_data;
    AVIOContext *pb = s->pb;
    StreamInfo *stream = rm->video_stream;
    int key_frame = !!(flags & AV_PKT_FLAG_KEY);

    /* XXX: this is incorrect: should be a parameter */

    /* Well, I spent some time finding the meaning of these bits. I am
       not sure I understood everything, but it works !! */
#if 1
    write_packet_header(s, stream, size + 7 + (size >= 0x4000)*4, key_frame);
    /* bit 7: '1' if final packet of a frame converted in several packets */
    avio_w8(pb, 0x81);
    /* bit 7: '1' if I frame. bits 6..0 : sequence number in current
       frame starting from 1 */
    if (key_frame) {
        avio_w8(pb, 0x81);
    } else {
        avio_w8(pb, 0x01);
    }
    if(size >= 0x4000){
        avio_wb32(pb, size); /* total frame size */
        avio_wb32(pb, size); /* offset from the start or the end */
    }else{
        avio_wb16(pb, 0x4000 | size); /* total frame size */
        avio_wb16(pb, 0x4000 | size); /* offset from the start or the end */
    }
#else
    /* full frame */
    write_packet_header(s, size + 6);
    avio_w8(pb, 0xc0);
    avio_wb16(pb, 0x4000 + size); /* total frame size */
    avio_wb16(pb, 0x4000 + packet_number * 126); /* position in stream */
#endif
    avio_w8(pb, stream->nb_frames & 0xff);

    avio_write(pb, buf, size);
    avio_flush(pb);

    stream->nb_frames++;
    return 0;
}

static int rm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (s->streams[pkt->stream_index]->codec->codec_type ==
        AVMEDIA_TYPE_AUDIO)
        return rm_write_audio(s, pkt->data, pkt->size, pkt->flags);
    else
        return rm_write_video(s, pkt->data, pkt->size, pkt->flags);
}

static int rm_write_trailer(AVFormatContext *s)
{
    RMMuxContext *rm = s->priv_data;
    int data_size, index_pos, i;
    AVIOContext *pb = s->pb;

    if (s->pb->seekable) {
        /* end of file: finish to write header */
        index_pos = avio_tell(pb);
        data_size = index_pos - rm->data_pos;

        /* FIXME: write index */

        /* undocumented end header */
        avio_wb32(pb, 0);
        avio_wb32(pb, 0);

        avio_seek(pb, 0, SEEK_SET);
        for(i=0;i<s->nb_streams;i++)
            rm->streams[i].total_frames = rm->streams[i].nb_frames;
        rv10_write_header(s, data_size, 0);
    } else {
        /* undocumented end header */
        avio_wb32(pb, 0);
        avio_wb32(pb, 0);
    }
    avio_flush(pb);
    return 0;
}


AVOutputFormat ff_rm_muxer = {
    .name              = "rm",
    .long_name         = NULL_IF_CONFIG_SMALL("RealMedia format"),
    .mime_type         = "application/vnd.rn-realmedia",
    .extensions        = "rm,ra",
    .priv_data_size    = sizeof(RMMuxContext),
    .audio_codec       = CODEC_ID_AC3,
    .video_codec       = CODEC_ID_RV10,
    .write_header      = rm_write_header,
    .write_packet      = rm_write_packet,
    .write_trailer     = rm_write_trailer,
    .codec_tag= (const AVCodecTag* const []){ff_rm_codec_tags, 0},
};
