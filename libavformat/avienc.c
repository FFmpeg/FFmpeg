/*
 * AVI encoder.
 * Copyright (c) 2000 Fabrice Bellard.
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
    offset_t movi_list, frames_hdr_all, frames_hdr_strm[MAX_STREAMS];
    int audio_strm_length[MAX_STREAMS];
    AVIIndex *first, *last;
} AVIContext;

offset_t start_tag(ByteIOContext *pb, const char *tag)
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
const CodecTag codec_bmp_tags[] = {
    { CODEC_ID_H263, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('d', 'i', 'v', 'x'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('D', 'X', '5', '0'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('x', 'v', 'i', 'd'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 's'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('M', 'P', '4', 'S') },
    { CODEC_ID_MPEG4, MKTAG('M', '4', 'S', '2') },
    { CODEC_ID_MPEG4, MKTAG('m', '4', 's', '2') },
    { CODEC_ID_MPEG4, MKTAG(0x04, 0, 0, 0) }, /* some broken avi use this */
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '3'), .invalid_asf = 1 }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4V3, MKTAG('d', 'i', 'v', '3'), .invalid_asf = 1 },
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') }, 
    { CODEC_ID_MSMPEG4V2, MKTAG('M', 'P', '4', '2') }, 
    { CODEC_ID_MSMPEG4V1, MKTAG('M', 'P', 'G', '4') }, 
    { CODEC_ID_WMV1, MKTAG('W', 'M', 'V', '1') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'l') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'd') }, 
    { CODEC_ID_DVVIDEO, MKTAG('D', 'V', 'S', 'D') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'd') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '1') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '2') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('P', 'I', 'M', '1') }, 
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_HUFFYUV, MKTAG('H', 'F', 'Y', 'U') },
    { CODEC_ID_HUFFYUV, MKTAG('h', 'f', 'y', 'u') },
    { 0, 0 },
};

unsigned int codec_get_tag(const CodecTag *tags, int id)
{
    while (tags->id != 0) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

static unsigned int codec_get_asf_tag(const CodecTag *tags, int id)
{
    while (tags->id != 0) {
        if (!tags->invalid_asf && tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

int codec_get_id(const CodecTag *tags, unsigned int tag)
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
void put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const CodecTag *tags, int for_asf)
{
    put_le32(pb, 40); /* size */
    put_le32(pb, enc->width);
    put_le32(pb, enc->height);
    put_le16(pb, 1); /* planes */
    put_le16(pb, 24); /* depth */
    /* compression type */
    put_le32(pb, for_asf ? codec_get_asf_tag(tags, enc->codec_id) : codec_get_tag(tags, enc->codec_id));
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
}

static void parse_specific_params(AVCodecContext *stream, int *au_byterate, int *au_ssize, int *au_scale)
{
    switch(stream->codec_id) {
    case CODEC_ID_PCM_S16LE:
       *au_scale = *au_ssize = 2*stream->channels;
       *au_byterate = *au_ssize * stream->sample_rate;
        break;
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        *au_scale = *au_ssize = stream->channels;
        *au_byterate = *au_ssize * stream->sample_rate;
        break;
    case CODEC_ID_MP2:
        *au_ssize = 1;
        *au_scale = 1;
        *au_byterate = stream->bit_rate / 8;
    case CODEC_ID_MP3LAME:
        *au_ssize = 1;
        *au_scale = 1;
        *au_byterate = stream->bit_rate / 8;    
    default:
        *au_ssize = 1;
        *au_scale = 1; 
        *au_byterate = stream->bit_rate / 8;
        break;
    }
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream, *video_enc;
    offset_t list1, list2, strh, strf;

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
        av_free(avi);
        return -1;
    }
    nb_frames = 0;

    put_le32(pb, (UINT32)(INT64_C(1000000) * FRAME_RATE_BASE / video_enc->frame_rate));
    put_le32(pb, bitrate / 8); /* XXX: not quite exact */
    put_le32(pb, 0); /* padding */
    put_le32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED); /* flags */
    avi->frames_hdr_all = url_ftell(pb); /* remember this offset to fill later */
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
            avi->frames_hdr_strm[i] = url_ftell(pb); /* remember this offset to fill later */
            put_le32(pb, nb_frames); /* length, XXX: fill later */
            put_le32(pb, 1024 * 1024); /* suggested buffer size */
            put_le32(pb, -1); /* quality */
            put_le32(pb, stream->width * stream->height * 3); /* sample size */
            put_le16(pb, 0);
            put_le16(pb, 0);
            put_le16(pb, stream->width);
            put_le16(pb, stream->height);
            break;
        case CODEC_TYPE_AUDIO:
            put_tag(pb, "auds");
            put_le32(pb, 1); /* tag */
            put_le32(pb, 0); /* flags */
            put_le16(pb, 0); /* priority */
            put_le16(pb, 0); /* language */
            put_le32(pb, 0); /* initial frame */
            parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
            put_le32(pb, au_scale); /* scale */
            put_le32(pb, au_byterate); /* rate */
            put_le32(pb, 0); /* start */
            avi->frames_hdr_strm[i] = url_ftell(pb); /* remember this offset to fill later */
            put_le32(pb, 0); /* length, XXX: filled later */
            put_le32(pb, 12 * 1024); /* suggested buffer size */
            put_le32(pb, -1); /* quality */
            put_le32(pb, au_ssize); /* sample size */
            put_le32(pb, 0);
            put_le32(pb, 0);
            break;
        default:
            av_abort();
        }
        end_tag(pb, strh);

        strf = start_tag(pb, "strf");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO:
	    put_bmp_header(pb, stream, codec_bmp_tags, 0);
            break;
        case CODEC_TYPE_AUDIO:
            if (put_wav_header(pb, stream) < 0) {
                av_free(avi);
                return -1;
            }
            break;
        default:
            av_abort();
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
                            UINT8 *buf, int size, int force_pts)
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
        flags = enc->coded_frame->key_frame ? 0x10 : 0x00;
    } else {
        tag[2] = 'w';
        tag[3] = 'b';
        flags = 0x10;
    }
    if (enc->codec_type == CODEC_TYPE_AUDIO) 
       avi->audio_strm_length[stream_index] += size;

    if (!url_is_streamed(&s->pb)) {
        idx = av_malloc(sizeof(AVIIndex));
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
    int n, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream;
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

        /* Fill in frame/sample counters */
        nb_frames = 0;
        for(n=0;n<s->nb_streams;n++) {
            if (avi->frames_hdr_strm[n] != 0) {
                stream = &s->streams[n]->codec;
                url_fseek(pb, avi->frames_hdr_strm[n], SEEK_SET);
                if (stream->codec_type == CODEC_TYPE_VIDEO) {
                    put_le32(pb, stream->frame_number); 
                    if (nb_frames < stream->frame_number)
                        nb_frames = stream->frame_number;
                } else {
                    if (stream->codec_id == CODEC_ID_MP2 || stream->codec_id == CODEC_ID_MP3LAME) {
                        put_le32(pb, stream->frame_number);
                        nb_frames += stream->frame_number;
                    } else {
                        parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
                        put_le32(pb, avi->audio_strm_length[n] / au_ssize);
                    }
                }
            }
       }
       if (avi->frames_hdr_all != 0) {
           url_fseek(pb, avi->frames_hdr_all, SEEK_SET);
           put_le32(pb, nb_frames); 
       }
        url_fseek(pb, file_size, SEEK_SET);
    }
    put_flush_packet(pb);
    return 0;
}

static AVOutputFormat avi_oformat = {
    "avi",
    "avi format",
    "video/x-msvideo",
    "avi",
    sizeof(AVIContext),
    CODEC_ID_MP2,
    CODEC_ID_MSMPEG4V3,
    avi_write_header,
    avi_write_packet,
    avi_write_trailer,
};

int avienc_init(void)
{
    av_register_output_format(&avi_oformat);
    return 0;
}
