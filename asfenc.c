/*
 * ASF compatible encoder
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

#define PACKET_SIZE 3200
#define PACKET_HEADER_SIZE 12
#define FRAME_HEADER_SIZE 17

typedef struct {
    AVEncodeContext *enc;
    int num;
    int seq;
} ASFStream;

typedef struct {
    int is_streamed; /* true if streamed */
    int seqno;
    int packet_size;

    ASFStream streams[2];
    ASFStream *audio_stream, *video_stream;
    int nb_streams;
    /* non streamed additonnal info */
    int file_size_offset;
    int data_offset;
    /* packet filling */
    int packet_size_left;
    int packet_timestamp_start;
    int packet_timestamp_end;
    int packet_nb_frames;
    UINT8 packet_buf[PACKET_SIZE];
    PutByteContext pb;
} ASFContext;

typedef struct {
    UINT32 v1;
    UINT16 v2;
    UINT16 v3;
    UINT8 v4[8];
} GUID;

static const GUID asf_header = {
    0x75B22630, 0x668E, 0x11CF, { 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C },
};

static const GUID file_header = {
    0x8CABDCA1, 0xA947, 0x11CF, { 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID stream_header = {
    0xB7DC0791, 0xA9B7, 0x11CF, { 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID audio_stream = {
    0xF8699E40, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID audio_conceal_none = {
    0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID video_stream = {
    0xBC19EFC0, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID video_conceal_none = {
    0x20FB5700, 0x5B55, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID comment_header = {
    0x86D15240, 0x311D, 0x11D0, { 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 },
};

static const GUID data_header = {
    0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID packet_guid = {
    0xF656CCE1, 0x03B3, 0x11D4, { 0xBE, 0xA2, 0x00, 0xA0, 0xCC, 0x3D, 0x72, 0x74 },
};
    
/* I am not a number !!! This GUID is the one found on the PC used to
   generate the stream */
static const GUID my_guid = {
    0x12345678, 0xA947, 0x11CF, { 0x31, 0x41, 0x59, 0x26, 0x20, 0x20, 0x20, 0x20 },
};

static void put_guid(PutByteContext *s, const GUID *g)
{
    int i;

    put_le32(s, g->v1);
    put_le16(s, g->v2);
    put_le16(s, g->v3);
    for(i=0;i<8;i++)
        put_byte(s, g->v4[i]);
}

#if 0
static void put_str16(PutByteContext *s, const char *tag)
{
    put_le16(s,strlen(tag));
    while (*tag) {
        put_le16(s, *tag++);
    }
}
#endif

/* write an asf chunk (only used in streaming case) */
static void put_chunk(AVFormatContext *s, int type, int payload_length)
{
    ASFContext *asf = s->priv_data;
    PutByteContext *pb = &s->pb;
    int length;

    length = payload_length + 8;
    put_le16(pb, type); 
    put_le16(pb, length); 
    put_le32(pb, asf->seqno);
    put_le16(pb, 0); /* unknown bytes */
    put_le16(pb, length);
    asf->seqno++;
}


static int asf_write_header(AVFormatContext *s)
{
    PutByteContext *pb = &s->pb;
    ASFContext *asf;
    int header_size, n, extra_size, extra_size2, i, wav_extra_size;
    AVEncodeContext *enc;
    long long header_offset, cur_pos;

    asf = malloc(sizeof(ASFContext));
    if (!asf)
        return -1;
    memset(asf, 0, sizeof(ASFContext));
    s->priv_data = asf;

    asf->packet_size = PACKET_SIZE;

    if (!s->is_streamed) {
        put_guid(pb, &asf_header);
        put_le64(pb, 0); /* header length, will be patched after */
        put_le32(pb, 6); /* ??? */
        put_byte(pb, 1); /* ??? */
        put_byte(pb, 2); /* ??? */
    } else {
        put_chunk(s, 0x4824, 0); /* start of stream (length will be patched later) */
    }
    
    /* file header */
    header_offset = put_pos(pb);
    put_guid(pb, &file_header);
    put_le64(pb, 24 + 80);
    put_guid(pb, &my_guid);
    asf->file_size_offset = put_pos(pb);
    put_le64(pb, 0); /* file size (patched after if not streamed) */
    put_le64(pb, 0); /* file time : 1601 :-) */
    put_le64(pb, 0x283); /* ??? */
    put_le64(pb, 0); /* stream 0 length in us */
    put_le64(pb, 0); /* stream 1 length in us */
    put_le32(pb, 0x0c1c); /* ??? */
    put_le32(pb, 0); /* ??? */
    put_le32(pb, 2); /* ??? */
    put_le32(pb, asf->packet_size); /* packet size */
    put_le32(pb, asf->packet_size); /* ??? */
    put_le32(pb, 0x03e800); /* ??? */
    
    /* stream headers */
    n = 0;
    for(i=0;i<2;i++) {
        if (i == 0)
            enc = s->audio_enc;
        else
            enc = s->video_enc;
        
        if (!enc)
            continue;
        asf->streams[n].num = i;
        asf->streams[n].seq = 0;
        asf->streams[n].enc = enc;
        
        switch(enc->codec->type) {
        case CODEC_TYPE_AUDIO:
            asf->audio_stream = &asf->streams[n];
            wav_extra_size = 0;
            extra_size = 18 + wav_extra_size;
            extra_size2 = 0;
            break;
        default:
        case CODEC_TYPE_VIDEO:
            asf->video_stream = &asf->streams[n];
            wav_extra_size = 0;
            extra_size = 0x33;
            extra_size2 = 0;
            break;
        }

        put_guid(pb, &stream_header);
        put_le64(pb, 24 + 16 * 2 + 22 + extra_size + extra_size2);
        if (enc->codec->type == CODEC_TYPE_AUDIO) {
            put_guid(pb, &audio_stream);
            put_guid(pb, &audio_conceal_none);
        } else {
            put_guid(pb, &video_stream);
            put_guid(pb, &video_conceal_none);
        }
        put_le64(pb, 0); /* ??? */
        put_le32(pb, extra_size); /* wav header len */
        put_le32(pb, extra_size2); /* additional data len */
        put_le16(pb, n + 1); /* stream number */
        put_le32(pb, 0); /* ??? */
        
        if (enc->codec->type == CODEC_TYPE_AUDIO) {
            /* WAVEFORMATEX header */

            put_le16(pb, 0x55); /* MP3 format */
            put_le16(pb, s->audio_enc->channels);
            put_le32(pb, s->audio_enc->rate);
            put_le32(pb, s->audio_enc->bit_rate / 8);
            put_le16(pb, 1); /* block align */
            put_le16(pb, 16); /* bits per sample */
            put_le16(pb, wav_extra_size);
        
            /* no addtionnal adata */
        } else {
            put_le32(pb, enc->width);
            put_le32(pb, enc->height);
            put_byte(pb, 2); /* ??? */
            put_le16(pb, 40); /* size */

            /* BITMAPINFOHEADER header */
            put_le32(pb, 40); /* size */
            put_le32(pb, enc->width);
            put_le32(pb, enc->height);
            put_le16(pb, 1); /* planes */
            put_le16(pb, 24); /* depth */
            /* compression type */
            switch(enc->codec->id) {
            case CODEC_ID_H263:
                put_tag(pb, "I263"); 
                break;
            case CODEC_ID_MJPEG:
                put_tag(pb, "MJPG");
                break;
            default:
                put_tag(pb, "XXXX");
                break;
            }
            put_le32(pb, enc->width * enc->height * 3);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
        }
        n++;
    }
    asf->nb_streams = n;

    /* XXX: should media comments */
#if 0
    put_guid(pb, &comment_header);
    put_le64(pb, 0);
#endif
    /* patch the header size fields */
    cur_pos = put_pos(pb);
    header_size = cur_pos - header_offset;
    if (!s->is_streamed) {
        header_size += 24 + 6;
        put_seek(pb, header_offset - 14, SEEK_SET);
        put_le64(pb, header_size);
    } else {
        header_size += 8 + 50;
        put_seek(pb, header_offset - 10, SEEK_SET);
        put_le16(pb, header_size);
        put_seek(pb, header_offset - 2, SEEK_SET);
        put_le16(pb, header_size);
    }
    put_seek(pb, cur_pos, SEEK_SET);
    
    /* movie chunk, followed by packets of packet_size */
    asf->data_offset = cur_pos;
    put_guid(pb, &data_header);
    put_le64(pb, 24); /* will be patched after */
    put_guid(pb, &packet_guid);
    put_le64(pb, 0x283); /* ??? */
    put_byte(pb, 1); /* ??? */
    put_byte(pb, 1); /* ??? */

    put_flush_packet(&s->pb);

    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = asf->packet_size - PACKET_HEADER_SIZE;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size,
                  NULL, NULL, NULL);

    return 0;
}

/* write a fixed size packet */
static void put_packet(AVFormatContext *s, 
                       unsigned int timestamp, unsigned int duration, 
                       int nb_frames, int padsize)
{
    ASFContext *asf = s->priv_data;
    PutByteContext *pb = &s->pb;
    int flags;

    if (s->is_streamed) {
        put_chunk(s, 0x4424, asf->packet_size);
    }

    put_byte(pb, 0x82);
    put_le16(pb, 0);
    
    flags = 0x01; /* nb segments present */
    if (padsize > 0) {
        if (padsize < 256)
            flags |= 0x08;
        else
            flags |= 0x10;
    }
    put_byte(pb, flags); /* flags */
    put_byte(pb, 0x5d);
    if (flags & 0x10)
        put_le16(pb, padsize);
    if (flags & 0x08)
        put_byte(pb, padsize);
    put_le32(pb, timestamp);
    put_le16(pb, duration);
    put_byte(pb, nb_frames | 0x80);
}

static void flush_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int hdr_size, ptr;
    
    put_packet(s, asf->packet_timestamp_start, 
               asf->packet_timestamp_end - asf->packet_timestamp_start, 
               asf->packet_nb_frames, asf->packet_size_left);
    
    /* compute padding */
    hdr_size = PACKET_HEADER_SIZE;
    if (asf->packet_size_left > 0) {
        /* if padding needed, don't forget to count the
           padding byte in the header size */
        hdr_size++;
        asf->packet_size_left--;
        /* XXX: I do not test again exact limit to avoid boundary problems */
        if (asf->packet_size_left > 200) {
            hdr_size++;
            asf->packet_size_left--;
        }
    }
    ptr = asf->packet_size - PACKET_HEADER_SIZE - asf->packet_size_left;
    memset(asf->packet_buf + ptr, 0, asf->packet_size_left);
    
    put_buffer(&s->pb, asf->packet_buf, asf->packet_size - hdr_size);
    
    put_flush_packet(&s->pb);
    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = asf->packet_size - PACKET_HEADER_SIZE;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size,
                  NULL, NULL, NULL);
}

static void put_frame_header(AVFormatContext *s, ASFStream *stream, int timestamp,
                             int payload_size, int frag_offset, int frag_len)
{
    ASFContext *asf = s->priv_data;
    PutByteContext *pb = &asf->pb;
    int val;

    val = stream->num;
    if (stream->enc->key_frame)
        val |= 0x80;
    put_byte(pb, val);
    put_byte(pb, stream->seq);
    put_le32(pb, frag_offset); /* fragment offset */
    put_byte(pb, 0x08); /* flags */
    put_le32(pb, payload_size);
    put_le32(pb, timestamp);
    put_le16(pb, frag_len);
}


/* Output a frame. We suppose that payload_size <= PACKET_SIZE.

   It is there that you understand that the ASF format is really
   crap. They have misread the MPEG Systems spec ! 
 */
static void put_frame(AVFormatContext *s, ASFStream *stream, int timestamp,
                      UINT8 *buf, int payload_size)
{
    ASFContext *asf = s->priv_data;
    int frag_pos, frag_len, frag_len1;
    
    frag_pos = 0;
    while (frag_pos < payload_size) {
        frag_len = payload_size - frag_pos;
        frag_len1 = asf->packet_size_left - FRAME_HEADER_SIZE;
        if (frag_len1 > 0) {
            if (frag_len > frag_len1)
                frag_len = frag_len1;
            put_frame_header(s, stream, timestamp, payload_size, frag_pos, frag_len);
            put_buffer(&asf->pb, buf, frag_len);
            asf->packet_size_left -= (frag_len + FRAME_HEADER_SIZE);
            asf->packet_timestamp_end = timestamp;
            if (asf->packet_timestamp_start == -1)
                asf->packet_timestamp_start = timestamp;
            asf->packet_nb_frames++;
        } else {
            frag_len = 0;
        }
        frag_pos += frag_len;
        buf += frag_len;
        /* output the frame if filled */
        if (asf->packet_size_left <= FRAME_HEADER_SIZE)
            flush_packet(s);
    }
    stream->seq++;
}


static int asf_write_audio(AVFormatContext *s, UINT8 *buf, int size)
{
    ASFContext *asf = s->priv_data;
    int timestamp;

    timestamp = (int)((float)s->audio_enc->frame_number * s->audio_enc->frame_size * 1000.0 / 
                      s->audio_enc->rate);
    put_frame(s, asf->audio_stream, timestamp, buf, size);
    return 0;
}

static int asf_write_video(AVFormatContext *s, UINT8 *buf, int size)
{
    ASFContext *asf = s->priv_data;
    int timestamp;

    timestamp = (int)((float)s->video_enc->frame_number * 1000.0 / 
                      s->video_enc->rate);
    put_frame(s, asf->audio_stream, timestamp, buf, size);
    return 0;
}

static int asf_write_trailer(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    long long file_size;

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    if (s->is_streamed) {
        put_chunk(s, 0x4524, 0); /* end of stream */
    } else {
        /* patch the various place which depends on the file size */
        file_size = put_pos(&s->pb);
        put_seek(&s->pb, asf->file_size_offset, SEEK_SET);
        put_le64(&s->pb, file_size);
        put_seek(&s->pb, asf->data_offset + 16, SEEK_SET);
        put_le64(&s->pb, file_size - asf->data_offset);
    }

    put_flush_packet(&s->pb);

    free(asf);
    return 0;
}

AVFormat asf_format = {
    "asf",
    "asf format",
    "application/octet-stream",
    "asf",
    CODEC_ID_MP2,
    CODEC_ID_MJPEG,
    asf_write_header,
    asf_write_audio,
    asf_write_video,
    asf_write_trailer,
};
