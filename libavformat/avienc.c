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

#ifdef CONFIG_MUXERS
typedef struct AVIIentry {
    unsigned int flags, pos, len;
} AVIIentry;

#define AVI_INDEX_CLUSTER_SIZE 16384

typedef struct AVIIndex {
    offset_t    indx_start;
    int         entry;
    int         ents_allocated;
    AVIIentry** cluster;
} AVIIndex;

typedef struct {
    offset_t riff_start, movi_list, odml_list;
    offset_t frames_hdr_all, frames_hdr_strm[MAX_STREAMS];
    int audio_strm_length[MAX_STREAMS];
    int riff_id;
    int packet_count[MAX_STREAMS];

    AVIIndex indexes[MAX_STREAMS];
} AVIContext;

static inline AVIIentry* avi_get_ientry(AVIIndex* idx, int ent_id) 
{
    int cl = ent_id / AVI_INDEX_CLUSTER_SIZE;
    int id = ent_id % AVI_INDEX_CLUSTER_SIZE;
    return &idx->cluster[cl][id];
}

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
#endif //CONFIG_MUXERS

/* Note: when encoding, the first matching tag is used, so order is
   important if multiple tags possible for a given codec. */
const CodecTag codec_bmp_tags[] = {
    { CODEC_ID_H264, MKTAG('H', '2', '6', '4') },
    { CODEC_ID_H264, MKTAG('V', 'S', 'S', 'H') },

    { CODEC_ID_H263, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_H261, MKTAG('H', '2', '6', '1') },

    /* added based on MPlayer */
    { CODEC_ID_H263P, MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('v', 'i', 'v', '1') },

    { CODEC_ID_MPEG4, MKTAG('F', 'M', 'P', '4')},
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('D', 'X', '5', '0'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D'), .invalid_asf = 1 },
    { CODEC_ID_MPEG4, MKTAG('M', 'P', '4', 'S') },
    { CODEC_ID_MPEG4, MKTAG('M', '4', 'S', '2') },
    { CODEC_ID_MPEG4, MKTAG(0x04, 0, 0, 0) }, /* some broken avi use this */

    /* added based on MPlayer */
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', '1') },
    { CODEC_ID_MPEG4, MKTAG('B', 'L', 'Z', '0') },
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('U', 'M', 'P', '4') },
    { CODEC_ID_MPEG4, MKTAG('W', 'V', '1', 'F') },

    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '3'), .invalid_asf = 1 }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') }, 

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', 'G', '3') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '5') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '6') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '4') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('A', 'P', '4', '1') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '1') }, 
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '0') }, 

    { CODEC_ID_MSMPEG4V2, MKTAG('M', 'P', '4', '2') }, 

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V2, MKTAG('D', 'I', 'V', '2') },
 
    { CODEC_ID_MSMPEG4V1, MKTAG('M', 'P', 'G', '4') }, 

    { CODEC_ID_WMV1, MKTAG('W', 'M', 'V', '1') }, 

    /* added based on MPlayer */
    { CODEC_ID_WMV2, MKTAG('W', 'M', 'V', '2') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'd') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'd') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'l') }, 
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '2', '5') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '1') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '2') }, 
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'p', 'g', '2') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('P', 'I', 'M', '1') }, 
    { CODEC_ID_MPEG1VIDEO, MKTAG('V', 'C', 'R', '2') }, 
    { CODEC_ID_MPEG1VIDEO, 0x10000001 }, 
    { CODEC_ID_MPEG2VIDEO, 0x10000002 }, 
    { CODEC_ID_MPEG2VIDEO, MKTAG('D', 'V', 'R', ' ') },
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG, MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_LJPEG, MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG, MKTAG('J', 'P', 'G', 'L') }, /* Pegasus lossless JPEG */
    { CODEC_ID_HUFFYUV, MKTAG('H', 'F', 'Y', 'U') },
    { CODEC_ID_FFVHUFF, MKTAG('F', 'F', 'V', 'H') },
    { CODEC_ID_CYUV, MKTAG('C', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO, MKTAG('I', '4', '2', '0') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', 'U', 'Y', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('U', 'Y', 'V', 'Y') },
    { CODEC_ID_RAWVIDEO, MKTAG('I', 'Y', 'U', 'V') },
    { CODEC_ID_INDEO3, MKTAG('I', 'V', '3', '1') },
    { CODEC_ID_INDEO3, MKTAG('I', 'V', '3', '2') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '1') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '0') },
    { CODEC_ID_ASV1, MKTAG('A', 'S', 'V', '1') },
    { CODEC_ID_ASV2, MKTAG('A', 'S', 'V', '2') },
    { CODEC_ID_VCR1, MKTAG('V', 'C', 'R', '1') },
    { CODEC_ID_FFV1, MKTAG('F', 'F', 'V', '1') },
    { CODEC_ID_XAN_WC4, MKTAG('X', 'x', 'a', 'n') },
    { CODEC_ID_MSRLE, MKTAG('m', 'r', 'l', 'e') },
    { CODEC_ID_MSRLE, MKTAG(0x1, 0x0, 0x0, 0x0) },
    { CODEC_ID_MSVIDEO1, MKTAG('M', 'S', 'V', 'C') },
    { CODEC_ID_MSVIDEO1, MKTAG('m', 's', 'v', 'c') },
    { CODEC_ID_MSVIDEO1, MKTAG('C', 'R', 'A', 'M') },
    { CODEC_ID_MSVIDEO1, MKTAG('c', 'r', 'a', 'm') },
    { CODEC_ID_MSVIDEO1, MKTAG('W', 'H', 'A', 'M') },
    { CODEC_ID_MSVIDEO1, MKTAG('w', 'h', 'a', 'm') },
    { CODEC_ID_CINEPAK, MKTAG('c', 'v', 'i', 'd') },
    { CODEC_ID_TRUEMOTION1, MKTAG('D', 'U', 'C', 'K') },
    { CODEC_ID_MSZH, MKTAG('M', 'S', 'Z', 'H') },
    { CODEC_ID_ZLIB, MKTAG('Z', 'L', 'I', 'B') },
    { CODEC_ID_SNOW, MKTAG('S', 'N', 'O', 'W') },
    { CODEC_ID_4XM, MKTAG('4', 'X', 'M', 'V') },
    { CODEC_ID_FLV1, MKTAG('F', 'L', 'V', '1') },
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') },
    { CODEC_ID_TSCC, MKTAG('t', 's', 'c', 'c') },
    { CODEC_ID_ULTI, MKTAG('U', 'L', 'T', 'I') },
    { CODEC_ID_VIXL, MKTAG('V', 'I', 'X', 'L') },
    { CODEC_ID_QPEG, MKTAG('Q', 'P', 'E', 'G') },
    { CODEC_ID_QPEG, MKTAG('Q', '1', '.', '0') },
    { CODEC_ID_QPEG, MKTAG('Q', '1', '.', '1') },
    { CODEC_ID_WMV3, MKTAG('W', 'M', 'V', '3') },
    { CODEC_ID_LOCO, MKTAG('L', 'O', 'C', 'O') },
    { CODEC_ID_WNV1, MKTAG('W', 'N', 'V', '1') },
    { CODEC_ID_AASC, MKTAG('A', 'A', 'S', 'C') },
    { CODEC_ID_INDEO2, MKTAG('R', 'T', '2', '1') },
    { CODEC_ID_FRAPS, MKTAG('F', 'P', 'S', '1') },
    { CODEC_ID_THEORA, MKTAG('t', 'h', 'e', 'o') },
    { CODEC_ID_RAWVIDEO, 0 },
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

static unsigned int codec_get_asf_tag(const CodecTag *tags, unsigned int id)
{
    while (tags->id != 0) {
        if (!tags->invalid_asf && tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum CodecID codec_get_id(const CodecTag *tags, unsigned int tag)
{
    while (tags->id != 0) {
        if(   toupper((tag >> 0)&0xFF) == toupper((tags->tag >> 0)&0xFF)
           && toupper((tag >> 8)&0xFF) == toupper((tags->tag >> 8)&0xFF)
           && toupper((tag >>16)&0xFF) == toupper((tags->tag >>16)&0xFF)
           && toupper((tag >>24)&0xFF) == toupper((tags->tag >>24)&0xFF))
            return tags->id;
        tags++;
    }
    return CODEC_ID_NONE;
}

unsigned int codec_get_bmp_tag(int id)
{
    return codec_get_tag(codec_bmp_tags, id);
}

unsigned int codec_get_wav_tag(int id)
{
    return codec_get_tag(codec_wav_tags, id);
}

enum CodecID codec_get_bmp_id(unsigned int tag)
{
    return codec_get_id(codec_bmp_tags, tag);
}

enum CodecID codec_get_wav_id(unsigned int tag)
{
    return codec_get_id(codec_wav_tags, tag);
}

#ifdef CONFIG_MUXERS
/* BITMAPINFOHEADER header */
void put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const CodecTag *tags, int for_asf)
{
    put_le32(pb, 40 + enc->extradata_size); /* size */
    put_le32(pb, enc->width);
    put_le32(pb, enc->height);
    put_le16(pb, 1); /* planes */
    
    put_le16(pb, enc->bits_per_sample ? enc->bits_per_sample : 24); /* depth */
    /* compression type */
    put_le32(pb, for_asf ? (enc->codec_tag ? enc->codec_tag : codec_get_asf_tag(tags, enc->codec_id)) : enc->codec_tag); //
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    
    put_buffer(pb, enc->extradata, enc->extradata_size);

    if (enc->extradata_size & 1)
        put_byte(pb, 0);
}

void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale)
{
    int gcd;

    *au_ssize= stream->block_align;
    if(stream->frame_size && stream->sample_rate){
        *au_scale=stream->frame_size;
        *au_rate= stream->sample_rate;
    }else if(stream->codec_type == CODEC_TYPE_VIDEO){
        *au_scale= stream->time_base.num;
        *au_rate = stream->time_base.den;
    }else{
        *au_scale= stream->block_align ? stream->block_align*8 : 8;
        *au_rate = stream->bit_rate;
    }
    gcd= ff_gcd(*au_scale, *au_rate);
    *au_scale /= gcd;
    *au_rate /= gcd;
}

static offset_t avi_start_new_riff(AVIContext *avi, ByteIOContext *pb, 
                                   const char* riff_tag, const char* list_tag)
{
    offset_t loff;
    int i;
    
    avi->riff_id++;
    for (i=0; i<MAX_STREAMS; i++)
         avi->indexes[i].entry = 0;
    
    avi->riff_start = start_tag(pb, "RIFF");
    put_tag(pb, riff_tag);
    loff = start_tag(pb, "LIST");
    put_tag(pb, list_tag);
    return loff;
}

static unsigned char* avi_stream2fourcc(unsigned char* tag, int index, 
                                        enum CodecType type)
{
    tag[0] = '0';
    tag[1] = '0' + index;
    if (type == CODEC_TYPE_VIDEO) {
        tag[2] = 'd';
        tag[3] = 'c';
    } else {
        tag[2] = 'w';
        tag[3] = 'b';
    }
    tag[4] = '\0';
    return tag;
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream, *video_enc;
    offset_t list1, list2, strh, strf;

    /* header list */
    avi->riff_id = 0;
    list1 = avi_start_new_riff(avi, pb, "AVI ", "hdrl");

    /* avi header */
    put_tag(pb, "avih");
    put_le32(pb, 14 * 4);
    bitrate = 0;

    video_enc = NULL;
    for(n=0;n<s->nb_streams;n++) {
        stream = s->streams[n]->codec;
        bitrate += stream->bit_rate;
        if (stream->codec_type == CODEC_TYPE_VIDEO)
            video_enc = stream;
    }
    
    nb_frames = 0;

    if(video_enc){
        put_le32(pb, (uint32_t)(int64_t_C(1000000) * video_enc->time_base.num / video_enc->time_base.den));
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
    
        stream = s->streams[i]->codec;

        /* FourCC should really be set by the codec itself */
        if (! stream->codec_tag) {
            stream->codec_tag = codec_get_bmp_tag(stream->codec_id);
        }

        /* stream generic header */
        strh = start_tag(pb, "strh");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO: put_tag(pb, "vids"); break;
        case CODEC_TYPE_AUDIO: put_tag(pb, "auds"); break;
//        case CODEC_TYPE_TEXT : put_tag(pb, "txts"); break;
        case CODEC_TYPE_DATA : put_tag(pb, "dats"); break;
        }
        if(stream->codec_type == CODEC_TYPE_VIDEO)
            put_le32(pb, stream->codec_tag);
        else
            put_le32(pb, 1);
        put_le32(pb, 0); /* flags */
        put_le16(pb, 0); /* priority */
        put_le16(pb, 0); /* language */
        put_le32(pb, 0); /* initial frame */

        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);

        put_le32(pb, au_scale); /* scale */
        put_le32(pb, au_byterate); /* rate */
        av_set_pts_info(s->streams[i], 64, au_scale, au_byterate);

        put_le32(pb, 0); /* start */
        avi->frames_hdr_strm[i] = url_ftell(pb); /* remember this offset to fill later */
        put_le32(pb, 0); /* length, XXX: filled later */
        
        /* suggested buffer size */ //FIXME set at the end to largest chunk
        if(stream->codec_type == CODEC_TYPE_VIDEO)
            put_le32(pb, 1024 * 1024); 
        else if(stream->codec_type == CODEC_TYPE_AUDIO)
            put_le32(pb, 12 * 1024); 
        else
            put_le32(pb, 0); 
        put_le32(pb, -1); /* quality */
        put_le32(pb, au_ssize); /* sample size */
        put_le32(pb, 0);
        put_le16(pb, stream->width);
        put_le16(pb, stream->height);
        end_tag(pb, strh);

      if(stream->codec_type != CODEC_TYPE_DATA){
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
            return -1;
        }
        end_tag(pb, strf);
      }
	
	if (!url_is_streamed(pb)) {
	    unsigned char tag[5];
	    int j;
    
            /* Starting to lay out AVI OpenDML master index. 
	     * We want to make it JUNK entry for now, since we'd
	     * like to get away without making AVI an OpenDML one 
	     * for compatibility reasons.
	     */
	    avi->indexes[i].entry = avi->indexes[i].ents_allocated = 0;
	    avi->indexes[i].indx_start = start_tag(pb, "JUNK"); 
	    put_le16(pb, 4);        /* wLongsPerEntry */
	    put_byte(pb, 0);        /* bIndexSubType (0 == frame index) */
	    put_byte(pb, 0);        /* bIndexType (0 == AVI_INDEX_OF_INDEXES) */
	    put_le32(pb, 0);        /* nEntriesInUse (will fill out later on) */
	    put_tag(pb, avi_stream2fourcc(&tag[0], i, stream->codec_type));
	                            /* dwChunkId */
	    put_le64(pb, 0);        /* dwReserved[3]
	    put_le32(pb, 0);           Must be 0.    */
	    for (j=0; j < AVI_MASTER_INDEX_SIZE * 2; j++)
	         put_le64(pb, 0);
	    end_tag(pb, avi->indexes[i].indx_start);
	}
	
        end_tag(pb, list2);
    }
    
    if (!url_is_streamed(pb)) {
        /* AVI could become an OpenDML one, if it grows beyond 2Gb range */
        avi->odml_list = start_tag(pb, "JUNK");
        put_tag(pb, "odml");
        put_tag(pb, "dmlh");
        put_le32(pb, 248);
        for (i = 0; i < 248; i+= 4)
             put_le32(pb, 0);
        end_tag(pb, avi->odml_list);
    }

    end_tag(pb, list1);
    
    avi->movi_list = start_tag(pb, "LIST");
    put_tag(pb, "movi");

    put_flush_packet(pb);

    return 0;
}

static int avi_write_ix(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AVIContext *avi = s->priv_data;
    unsigned char tag[5];
    unsigned char ix_tag[] = "ix00";
    int i, j;
    
    if (avi->riff_id > AVI_MASTER_INDEX_SIZE)
        return -1;
    
    for (i=0;i<s->nb_streams;i++) {
	 offset_t ix, pos;
	 
	 avi_stream2fourcc(&tag[0], i, s->streams[i]->codec->codec_type);
	 ix_tag[3] = '0' + i;
	 
	 /* Writing AVI OpenDML leaf index chunk */
	 ix = url_ftell(pb); 
	 put_tag(pb, &ix_tag[0]);     /* ix?? */
	 put_le32(pb, avi->indexes[i].entry * 8 + 24); 
	                              /* chunk size */
         put_le16(pb, 2);             /* wLongsPerEntry */
	 put_byte(pb, 0);             /* bIndexSubType (0 == frame index) */ 
	 put_byte(pb, 1);             /* bIndexType (1 == AVI_INDEX_OF_CHUNKS) */
	 put_le32(pb, avi->indexes[i].entry);          
	                              /* nEntriesInUse */
	 put_tag(pb, &tag[0]);        /* dwChunkId */
	 put_le64(pb, avi->movi_list);/* qwBaseOffset */
	 put_le32(pb, 0);             /* dwReserved_3 (must be 0) */

         for (j=0; j<avi->indexes[i].entry; j++) {
             AVIIentry* ie = avi_get_ientry(&avi->indexes[i], j);
	     put_le32(pb, ie->pos + 8);
	     put_le32(pb, ((uint32_t)ie->len & ~0x80000000) |
	                  (ie->flags & 0x10 ? 0 : 0x80000000));
         }
	 put_flush_packet(pb);
         pos = url_ftell(pb);
	
	 /* Updating one entry in the AVI OpenDML master index */
	 url_fseek(pb, avi->indexes[i].indx_start - 8, SEEK_SET);
	 put_tag(pb, "indx");                 /* enabling this entry */
	 url_fskip(pb, 8);
	 put_le32(pb, avi->riff_id);          /* nEntriesInUse */
	 url_fskip(pb, 16*avi->riff_id);
	 put_le64(pb, ix);                    /* qwOffset */
	 put_le32(pb, pos - ix);              /* dwSize */
	 put_le32(pb, avi->indexes[i].entry); /* dwDuration */

	 url_fseek(pb, pos, SEEK_SET);
    }
    return 0;
}

static int avi_write_idx1(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AVIContext *avi = s->priv_data;
    offset_t file_size, idx_chunk;
    int i, n, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream;
    unsigned char tag[5];

    if (!url_is_streamed(pb)) {
	AVIIentry* ie = 0, *tie;
	int entry[MAX_STREAMS];
	int empty, stream_id = -1;

	idx_chunk = start_tag(pb, "idx1");
	memset(&entry[0], 0, sizeof(entry));
	do {
	    empty = 1;
	    for (i=0; i<s->nb_streams; i++) {
	         if (avi->indexes[i].entry <= entry[i])
		     continue;
		 
		 tie = avi_get_ientry(&avi->indexes[i], entry[i]);
		 if (empty || tie->pos < ie->pos) {
		     ie = tie; 
		     stream_id = i;
		 }
		 empty = 0;
	    }
	    if (!empty) {
	        avi_stream2fourcc(&tag[0], stream_id, 
		                  s->streams[stream_id]->codec->codec_type); 
	        put_tag(pb, &tag[0]);
		put_le32(pb, ie->flags);
                put_le32(pb, ie->pos);
                put_le32(pb, ie->len);
		entry[stream_id]++;
	    }
	} while (!empty);
	end_tag(pb, idx_chunk);

        /* Fill in frame/sample counters */
	file_size = url_ftell(pb);
        nb_frames = 0;
        for(n=0;n<s->nb_streams;n++) {
            if (avi->frames_hdr_strm[n] != 0) {
                stream = s->streams[n]->codec;
                url_fseek(pb, avi->frames_hdr_strm[n], SEEK_SET);
                ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
                if (au_ssize == 0) {
                    put_le32(pb, stream->frame_number);
                    nb_frames += stream->frame_number;
                } else {
                    put_le32(pb, avi->audio_strm_length[n] / au_ssize);
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

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    unsigned char tag[5];
    unsigned int flags=0;
    const int stream_index= pkt->stream_index;
    AVCodecContext *enc= s->streams[stream_index]->codec;
    int size= pkt->size;

//    av_log(s, AV_LOG_DEBUG, "%lld %d %d\n", pkt->dts, avi->packet_count[stream_index], stream_index);
    while(enc->block_align==0 && pkt->dts != AV_NOPTS_VALUE && pkt->dts > avi->packet_count[stream_index]){
        AVPacket empty_packet;

        av_init_packet(&empty_packet);
        empty_packet.size= 0;
        empty_packet.data= NULL;
        empty_packet.stream_index= stream_index;
        avi_write_packet(s, &empty_packet);
//        av_log(s, AV_LOG_DEBUG, "dup %lld %d\n", pkt->dts, avi->packet_count[stream_index]);
    }
    avi->packet_count[stream_index]++;

    if (url_ftell(pb) - avi->riff_start > AVI_MAX_RIFF_SIZE) { 
        avi_write_ix(s);
        end_tag(pb, avi->movi_list);
        
	if (avi->riff_id == 1)
	    avi_write_idx1(s);

	end_tag(pb, avi->riff_start);
	avi->movi_list = avi_start_new_riff(avi, pb, "AVIX", "movi");
    }
    
    avi_stream2fourcc(&tag[0], stream_index, enc->codec_type);
    if(pkt->flags&PKT_FLAG_KEY)
        flags = 0x10;
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
       avi->audio_strm_length[stream_index] += size;
    }

    if (!url_is_streamed(&s->pb)) {
        AVIIndex* idx = &avi->indexes[stream_index];
	int cl = idx->entry / AVI_INDEX_CLUSTER_SIZE;
	int id = idx->entry % AVI_INDEX_CLUSTER_SIZE;
        if (idx->ents_allocated <= idx->entry) {
	    idx->cluster = av_realloc(idx->cluster, (cl+1)*sizeof(void*)); 
	    if (!idx->cluster)
	        return -1;
            idx->cluster[cl] = av_malloc(AVI_INDEX_CLUSTER_SIZE*sizeof(AVIIentry));
	    if (!idx->cluster[cl])
		return -1;
	    idx->ents_allocated += AVI_INDEX_CLUSTER_SIZE;
	}
 	
	idx->cluster[cl][id].flags = flags; 
        idx->cluster[cl][id].pos = url_ftell(pb) - avi->movi_list;
        idx->cluster[cl][id].len = size;
	idx->entry++;
    }
    
    put_buffer(pb, tag, 4);
    put_le32(pb, size);
    put_buffer(pb, pkt->data, size);
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
    int i, j, n, nb_frames;
    offset_t file_size;

    if (avi->riff_id == 1) {
        end_tag(pb, avi->movi_list);
        res = avi_write_idx1(s);
	end_tag(pb, avi->riff_start);
    } else {
        avi_write_ix(s);
        end_tag(pb, avi->movi_list);
	end_tag(pb, avi->riff_start);

        file_size = url_ftell(pb);
	url_fseek(pb, avi->odml_list - 8, SEEK_SET);
	put_tag(pb, "LIST"); /* Making this AVI OpenDML one */
	url_fskip(pb, 16);

        for (n=nb_frames=0;n<s->nb_streams;n++) {
             AVCodecContext *stream = s->streams[n]->codec;
             if (stream->codec_type == CODEC_TYPE_VIDEO) {
                 if (nb_frames < stream->frame_number)
                     nb_frames = stream->frame_number;
             } else {
                 if (stream->codec_id == CODEC_ID_MP2 || stream->codec_id == CODEC_ID_MP3) {
                     nb_frames += stream->frame_number;
                }
            }
        }
	put_le32(pb, nb_frames);
	url_fseek(pb, file_size, SEEK_SET);
    }
    put_flush_packet(pb);

    for (i=0; i<MAX_STREAMS; i++) {
	 for (j=0; j<avi->indexes[i].ents_allocated/AVI_INDEX_CLUSTER_SIZE; j++)
              av_free(avi->indexes[i].cluster[j]);
	 av_free(avi->indexes[i].cluster);
	 avi->indexes[i].cluster = NULL;
	 avi->indexes[i].ents_allocated = avi->indexes[i].entry = 0;
    }
    
    return res;
}

static AVOutputFormat avi_oformat = {
    "avi",
    "avi format",
    "video/x-msvideo",
    "avi",
    sizeof(AVIContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG4,
    avi_write_header,
    avi_write_packet,
    avi_write_trailer,
};

int avienc_init(void)
{
    av_register_output_format(&avi_oformat);
    return 0;
}
#endif //CONFIG_MUXERS
