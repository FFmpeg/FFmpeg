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
    offset_t riff_start, movi_list, odml_list;
    offset_t frames_hdr_all, frames_hdr_strm[MAX_STREAMS];
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
    put_le32(pb, (uint32_t)(pos - start));
    url_fseek(pb, pos, SEEK_SET);
}

/* Note: when encoding, the first matching tag is used, so order is
   important if multiple tags possible for a given codec. */
const CodecTag codec_bmp_tags[] = {
    { CODEC_ID_H263, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */

    /* added based on MPlayer */
    { CODEC_ID_H263I, MKTAG('i', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('h', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('v', 'i', 'v', '1') },

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

    /* added based on MPlayer */
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', '1') },
    { CODEC_ID_MPEG4, MKTAG('d', 'i', 'v', '1') },
    { CODEC_ID_MPEG4, MKTAG('X', 'v', 'i', 'D') },
    { CODEC_ID_MPEG4, MKTAG('B', 'L', 'Z', '0') },
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('U', 'M', 'P', '4') },

    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '3'), .invalid_asf = 1 }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4V3, MKTAG('d', 'i', 'v', '3'), .invalid_asf = 1 },
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') }, 

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', 'G', '3') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('m', 'p', 'g', '3') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('m', 'p', '4', '3') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '5') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('d', 'i', 'v', '5') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '6') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('d', 'i', 'v', '6') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '4') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('d', 'i', 'v', '4') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('A', 'P', '4', '1') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '1') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('c', 'o', 'l', '1') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '0') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('c', 'o', 'l', '0') }, 

    { CODEC_ID_MSMPEG4V2, MKTAG('M', 'P', '4', '2') }, 

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V2, MKTAG('D', 'I', 'V', '2') },
    { CODEC_ID_MSMPEG4V2, MKTAG('d', 'i', 'v', '2') },
    { CODEC_ID_MSMPEG4V2, MKTAG('m', 'p', '4', '2') },
 
    { CODEC_ID_MSMPEG4V1, MKTAG('M', 'P', 'G', '4') }, 

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V1, MKTAG('D', 'I', 'V', '4') }, 
    { CODEC_ID_MSMPEG4V1, MKTAG('d', 'i', 'v', '4') }, 
    { CODEC_ID_MSMPEG4V1, MKTAG('m', 'p', 'g', '4') }, 

    { CODEC_ID_WMV1, MKTAG('W', 'M', 'V', '1') }, 

    /* added based on MPlayer */
    { CODEC_ID_WMV1, MKTAG('w', 'm', 'v', '1') }, 

    { CODEC_ID_WMV2, MKTAG('W', 'M', 'V', '2') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'd') }, 
    { CODEC_ID_DVVIDEO, MKTAG('D', 'V', 'S', 'D') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'd') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'l') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '2', '5') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '1') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '2') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('P', 'I', 'M', '1') }, 
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_HUFFYUV, MKTAG('H', 'F', 'Y', 'U') },
    { CODEC_ID_HUFFYUV, MKTAG('h', 'f', 'y', 'u') },
    { CODEC_ID_CYUV, MKTAG('C', 'Y', 'U', 'V') },
    { CODEC_ID_CYUV, MKTAG('c', 'y', 'u', 'v') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('I', '4', '2', '0') },
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
    put_le32(pb, 40 + enc->extradata_size); /* size */
    put_le32(pb, enc->width);
    put_le32(pb, enc->height);
    put_le16(pb, 1); /* planes */
    
    put_le16(pb, enc->bits_per_sample ? enc->bits_per_sample : 24); /* depth */
    /* compression type */
    put_le32(pb, for_asf ? codec_get_asf_tag(tags, enc->codec_id) : codec_get_tag(tags, enc->codec_id));
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    
    put_buffer(pb, enc->extradata, enc->extradata_size);

    if (enc->extradata_size & 1)
        put_byte(pb, 0);
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

static offset_t avi_start_new_riff(AVIContext *avi, ByteIOContext *pb, 
                                   const char* riff_tag, const char* list_tag)
{
    offset_t loff;
    avi->riff_start = start_tag(pb, "RIFF");
    put_tag(pb, riff_tag);
    loff = start_tag(pb, "LIST");
    put_tag(pb, list_tag);
    return loff;
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream, *video_enc;
    offset_t list1, list2, strh, strf;

    /* header list */
    list1 = avi_start_new_riff(avi, pb, "AVI ", "hdrl");

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
    
    nb_frames = 0;

    if(video_enc){
        put_le32(pb, (uint32_t)(int64_t_C(1000000) * video_enc->frame_rate_base / video_enc->frame_rate));
    } else {
	put_le32(pb, 0);
    }
    put_le32(pb, bitrate / 8); /* XXX: not quite exact */
    put_le32(pb, 0); /* padding */
    put_le32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED); /* flags */
    avi->frames_hdr_all = url_ftell(pb); /* remember this offset to fill later */
    put_le32(pb, nb_frames); /* nb frames, filled later */
    put_le32(pb, 0); /* initial frame */
    put_le32(pb, s->nb_streams); /* nb streams */
    put_le32(pb, 1024 * 1024); /* suggested buffer size */
    if(video_enc){    
    put_le32(pb, video_enc->width);
    put_le32(pb, video_enc->height);
    } else {
	put_le32(pb, 0);
	put_le32(pb, 0);
    }	
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    
    /* stream list */
    for(i=0;i<n;i++) {
        list2 = start_tag(pb, "LIST");
        put_tag(pb, "strl");
    
        stream = &s->streams[i]->codec;

        /* FourCC should really be set by the codec itself */
        if (! stream->codec_tag) {
            stream->codec_tag = codec_get_bmp_tag(stream->codec_id);
        }

        /* stream generic header */
        strh = start_tag(pb, "strh");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO:
            put_tag(pb, "vids");
            put_le32(pb, stream->codec_tag);
            put_le32(pb, 0); /* flags */
            put_le16(pb, 0); /* priority */
            put_le16(pb, 0); /* language */
            put_le32(pb, 0); /* initial frame */
            
            put_le32(pb, stream->frame_rate_base); /* scale */
            put_le32(pb, stream->frame_rate); /* rate */

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
    
    /* AVI could become an OpenDML one, if it grows beyond 2Gb range */
    avi->odml_list = start_tag(pb, "JUNK");
    put_tag(pb, "odml");
    put_tag(pb, "dmlh");
    put_le32(pb, 248);
    for (i = 0; i < 248; i+= 4)
        put_le32(pb, 0);
    end_tag(pb, avi->odml_list);

    end_tag(pb, list1);
    
    avi->movi_list = start_tag(pb, "LIST");
    avi->first = NULL;
    avi->last = NULL;
    put_tag(pb, "movi");

    put_flush_packet(pb);

    return 0;
}

static int avi_finish_riff1(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AVIContext *avi = s->priv_data;
    offset_t file_size, idx_chunk;
    int n, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream;
    AVIIndex *idx;

    if (!url_is_streamed(pb)) {
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
	end_tag(pb, avi->riff_start);

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
    return 0;
}

static int avi_write_packet(AVFormatContext *s, int stream_index,
                            uint8_t *buf, int size, int force_pts)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVIIndex *idx;
    unsigned char tag[5];
    unsigned int flags;
    AVCodecContext *enc;

    if (url_ftell(pb) - avi->riff_start > AVI_MAX_RIFF_SIZE) { 
        if (avi->riff_start != 8) {
	    end_tag(pb, avi->movi_list);
	    end_tag(pb, avi->riff_start);
	} else
	    avi_finish_riff1(s);
	
	avi->movi_list = avi_start_new_riff(avi, pb, "AVIX", "movi");
    }
    
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
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int res = 0;

    if (avi->riff_start != 8) {
        int n, nb_frames;
	offset_t file_size;
	
        file_size = url_ftell(pb);
	url_fseek(pb, avi->odml_list - 8, SEEK_SET);
	put_tag(pb, "LIST"); /* Making this AVI OpenDML one */
	url_fskip(pb, 16);

        for (n=nb_frames=0;n<s->nb_streams;n++) {
             AVCodecContext *stream = &s->streams[n]->codec;
             if (stream->codec_type == CODEC_TYPE_VIDEO) {
                 if (nb_frames < stream->frame_number)
                     nb_frames = stream->frame_number;
             } else {
                 if (stream->codec_id == CODEC_ID_MP2 || stream->codec_id == CODEC_ID_MP3LAME) {
                     nb_frames += stream->frame_number;
                }
            }
        }
	put_le32(pb, nb_frames);

	end_tag(pb, avi->movi_list);
	end_tag(pb, avi->riff_start);
	url_fseek(pb, file_size, SEEK_SET);
    } else
        res = avi_finish_riff1(s);

    put_flush_packet(pb);
    return res;
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
