/*
 * AVI encoder.
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
#include "avformat.h"
#include "avi.h"

/*
 * TODO: 
 *  - fill all fields if non streamed (nb_frames for example)
 */

typedef struct AVIIndex {
    unsigned char tag[4];
    unsigned int flags, pos, len;
    struct AVIIndex *next;
} AVIIndex;

typedef struct {
    offset_t movi_list;
    AVIIndex *first, *last;
} AVIContext;

offset_t start_tag(ByteIOContext *pb, char *tag)
{
    put_tag(pb, tag);
    put_le32(pb, 0);
    return url_ftell(pb);
}

void end_tag(ByteIOContext *pb, offset_t start)
{
    offset_t pos;

    pos = url_ftell(pb);
    url_fseek(pb, start - 4, SEEK_SET);
    put_le32(pb, (UINT32)(pos - start));
    url_fseek(pb, pos, SEEK_SET);
}

/* Note: when encoding, the first matching tag is used, so order is
   important if multiple tags possible for a given codec. */
CodecTag codec_bmp_tags[] = {
    { CODEC_ID_H263, MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') },
    { CODEC_ID_MPEG4, MKTAG('d', 'i', 'v', 'x') },
    { CODEC_ID_MPEG4, MKTAG(0x04, 0, 0, 0) }, /* some broken avi use this */
    { CODEC_ID_MSMPEG4, MKTAG('D', 'I', 'V', '3') }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4, MKTAG('M', 'P', '4', '3') }, 
    { 0, 0 },
};

unsigned int codec_get_tag(CodecTag *tags, int id)
{
    while (tags->id != 0) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

int codec_get_id(CodecTag *tags, unsigned int tag)
{
    while (tags->id != 0) {
        if (tags->tag == tag)
            return tags->id;
        tags++;
    }
    return 0;
}

unsigned int codec_get_bmp_tag(int id)
{
    return codec_get_tag(codec_bmp_tags, id);
}

/* BITMAPINFOHEADER header */
void put_bmp_header(ByteIOContext *pb, AVCodecContext *enc)
{
    put_le32(pb, 40); /* size */
    put_le32(pb, enc->width);
    put_le32(pb, enc->height);
    put_le16(pb, 1); /* planes */
    put_le16(pb, 24); /* depth */
    /* compression type */
    put_le32(pb, codec_get_bmp_tag(enc->codec_id));
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi;
    ByteIOContext *pb = &s->pb;
    int bitrate, n, i, nb_frames;
    AVCodecContext *stream, *video_enc;
    offset_t list1, list2, strh, strf;

    avi = malloc(sizeof(AVIContext));
    if (!avi)
        return -1;
    memset(avi, 0, sizeof(AVIContext));
    s->priv_data = avi;

    put_tag(pb, "RIFF");
    put_le32(pb, 0); /* file length */
    put_tag(pb, "AVI ");

    /* header list */
    list1 = start_tag(pb, "LIST");
    put_tag(pb, "hdrl");

    /* avi header */
    put_tag(pb, "avih");
    put_le32(pb, 14 * 4);
    bitrate = 0;

    video_enc = NULL;
    for(n=0;n<s->nb_streams;n++) {
        stream = &s->streams[n]->codec;
        bitrate += stream->bit_rate;
        if (stream->codec_type == CODEC_TYPE_VIDEO)
            video_enc = stream;
    }
    
    if (!video_enc) {
        free(avi);
        return -1;
    }
    nb_frames = 0;

    put_le32(pb, (UINT32)(INT64_C(1000000) * FRAME_RATE_BASE / video_enc->frame_rate));
    put_le32(pb, bitrate / 8); /* XXX: not quite exact */
    put_le32(pb, 0); /* padding */
    put_le32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED); /* flags */
    put_le32(pb, nb_frames); /* nb frames, filled later */
    put_le32(pb, 0); /* initial frame */
    put_le32(pb, s->nb_streams); /* nb streams */
    put_le32(pb, 1024 * 1024); /* suggested buffer size */
    put_le32(pb, video_enc->width);
    put_le32(pb, video_enc->height);
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    
    /* stream list */
    for(i=0;i<n;i++) {
        list2 = start_tag(pb, "LIST");
        put_tag(pb, "strl");
    
        stream = &s->streams[i]->codec;

        /* stream generic header */
        strh = start_tag(pb, "strh");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO:
            put_tag(pb, "vids");
            put_le32(pb, codec_get_bmp_tag(stream->codec_id));
            put_le32(pb, 0); /* flags */
            put_le16(pb, 0); /* priority */
            put_le16(pb, 0); /* language */
            put_le32(pb, 0); /* initial frame */
            put_le32(pb, 1000); /* scale */
            put_le32(pb, (1000 * stream->frame_rate) / FRAME_RATE_BASE); /* rate */
            put_le32(pb, 0); /* start */
            put_le32(pb, nb_frames); /* length, XXX: fill later */
            put_le32(pb, 1024 * 1024); /* suggested buffer size */
            put_le32(pb, 10000); /* quality */
            put_le32(pb, stream->width * stream->height * 3); /* sample size */
            put_le16(pb, 0);
            put_le16(pb, 0);
            put_le16(pb, stream->width);
            put_le16(pb, stream->height);
            break;
        case CODEC_TYPE_AUDIO:
            put_tag(pb, "auds");
            put_le32(pb, 0);
            put_le32(pb, 0); /* flags */
            put_le16(pb, 0); /* priority */
            put_le16(pb, 0); /* language */
            put_le32(pb, 0); /* initial frame */
            put_le32(pb, 1); /* scale */
            put_le32(pb, stream->bit_rate / 8); /* rate */
            put_le32(pb, 0); /* start */
            put_le32(pb, 0); /* length, XXX: filled later */
            put_le32(pb, 12 * 1024); /* suggested buffer size */
            put_le32(pb, -1); /* quality */
            put_le32(pb, 1); /* sample size */
            put_le32(pb, 0);
            put_le32(pb, 0);
            break;
        }
        end_tag(pb, strh);

        strf = start_tag(pb, "strf");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO:
            put_bmp_header(pb, stream);
            break;
        case CODEC_TYPE_AUDIO:
            if (put_wav_header(pb, stream) < 0) {
                free(avi);
                return -1;
            }
            break;
        }
        end_tag(pb, strf);
        end_tag(pb, list2);
    }

    end_tag(pb, list1);
    
    avi->movi_list = start_tag(pb, "LIST");
    avi->first = NULL;
    avi->last = NULL;
    put_tag(pb, "movi");

    put_flush_packet(pb);

    return 0;
}

static int avi_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVIIndex *idx;
    unsigned char tag[5];
    unsigned int flags;
    AVCodecContext *enc;
    
    enc = &s->streams[stream_index]->codec;

    tag[0] = '0';
    tag[1] = '0' + stream_index;
    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        tag[2] = 'd';
        tag[3] = 'c';
        flags = enc->key_frame ? 0x10 : 0x00;
    } else {
        tag[2] = 'w';
        tag[3] = 'b';
        flags = 0x10;
    }

    if (!url_is_streamed(&s->pb)) {
        idx = malloc(sizeof(AVIIndex));
        memcpy(idx->tag, tag, 4);
        idx->flags = flags;
        idx->pos = url_ftell(pb) - avi->movi_list;
        idx->len = size;
        idx->next = NULL;
        if (!avi->last)
            avi->first = idx;
        else
            avi->last->next = idx;
        avi->last = idx;
    }
    
    put_buffer(pb, tag, 4);
    put_le32(pb, size);
    put_buffer(pb, buf, size);
    if (size & 1)
        put_byte(pb, 0);

    put_flush_packet(pb);
    return 0;
}

static int avi_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AVIContext *avi = s->priv_data;
    offset_t file_size, idx_chunk;
    AVIIndex *idx;

    if (!url_is_streamed(&s->pb)) {
        end_tag(pb, avi->movi_list);

        idx_chunk = start_tag(pb, "idx1");
        idx = avi->first;
        while (idx != NULL) {
            put_buffer(pb, idx->tag, 4);
            put_le32(pb, idx->flags);
            put_le32(pb, idx->pos);
            put_le32(pb, idx->len);
            idx = idx->next;
        }
        end_tag(pb, idx_chunk);
        
        /* update file size */
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, (UINT32)(file_size - 8));
        url_fseek(pb, file_size, SEEK_SET);
    }
    put_flush_packet(pb);

    free(avi);
    return 0;
}

AVFormat avi_format = {
    "avi",
    "avi format",
    "video/x-msvideo",
    "avi",
    CODEC_ID_MP2,
    CODEC_ID_MSMPEG4,
    avi_write_header,
    avi_write_packet,
    avi_write_trailer,

    avi_read_header,
    avi_read_packet,
    avi_read_close,
};
