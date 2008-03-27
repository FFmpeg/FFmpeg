/*
 * AVI muxer
 * Copyright (c) 2000 Fabrice Bellard.
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
#include "avi.h"
#include "riff.h"

/*
 * TODO:
 *  - fill all fields if non streamed (nb_frames for example)
 */

#ifdef CONFIG_AVI_MUXER
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

static char* avi_stream2fourcc(char* tag, int index, enum CodecType type)
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

static void avi_write_info_tag(ByteIOContext *pb, const char *tag, const char *str)
{
    int len = strlen(str);
    if (len > 0) {
        len++;
        put_tag(pb, tag);
        put_le32(pb, len);
        put_strz(pb, str);
        if (len & 1)
            put_byte(pb, 0);
    }
}

static int avi_write_counters(AVFormatContext* s, int riff_id)
{
    ByteIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    int n, au_byterate, au_ssize, au_scale, nb_frames = 0;
    offset_t file_size;
    AVCodecContext* stream;

    file_size = url_ftell(pb);
    for(n = 0; n < s->nb_streams; n++) {
        assert(avi->frames_hdr_strm[n]);
        stream = s->streams[n]->codec;
        url_fseek(pb, avi->frames_hdr_strm[n], SEEK_SET);
        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
        if(au_ssize == 0) {
            put_le32(pb, avi->packet_count[n]);
        } else {
            put_le32(pb, avi->audio_strm_length[n] / au_ssize);
        }
        if(stream->codec_type == CODEC_TYPE_VIDEO)
            nb_frames = FFMAX(nb_frames, avi->packet_count[n]);
    }
    if(riff_id == 1) {
        assert(avi->frames_hdr_all);
        url_fseek(pb, avi->frames_hdr_all, SEEK_SET);
        put_le32(pb, nb_frames);
    }
    url_fseek(pb, file_size, SEEK_SET);

    return 0;
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = s->pb;
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
        put_le32(pb, (uint32_t)(INT64_C(1000000) * video_enc->time_base.num / video_enc->time_base.den));
    } else {
        put_le32(pb, 0);
    }
    put_le32(pb, bitrate / 8); /* XXX: not quite exact */
    put_le32(pb, 0); /* padding */
    if (url_is_streamed(pb))
        put_le32(pb, AVIF_TRUSTCKTYPE | AVIF_ISINTERLEAVED); /* flags */
    else
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
        if (url_is_streamed(pb))
            put_le32(pb, AVI_MAX_RIFF_SIZE); /* FIXME: this may be broken, but who cares */
        else
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

        if(   stream->codec_type == CODEC_TYPE_VIDEO
           && stream->sample_aspect_ratio.num>0
           && stream->sample_aspect_ratio.den>0){
            int vprp= start_tag(pb, "vprp");
            AVRational dar = av_mul_q(stream->sample_aspect_ratio,
                                      (AVRational){stream->width, stream->height});
            int num, den;
            av_reduce(&num, &den, dar.num, dar.den, 0xFFFF);

            put_le32(pb, 0); //video format  = unknown
            put_le32(pb, 0); //video standard= unknown
            put_le32(pb, lrintf(1.0/av_q2d(stream->time_base)));
            put_le32(pb, stream->width );
            put_le32(pb, stream->height);
            put_le16(pb, den);
            put_le16(pb, num);
            put_le32(pb, stream->width );
            put_le32(pb, stream->height);
            put_le32(pb, 1); //progressive FIXME

            put_le32(pb, stream->height);
            put_le32(pb, stream->width );
            put_le32(pb, stream->height);
            put_le32(pb, stream->width );
            put_le32(pb, 0);
            put_le32(pb, 0);

            put_le32(pb, 0);
            put_le32(pb, 0);
            end_tag(pb, vprp);
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

    list2 = start_tag(pb, "LIST");
    put_tag(pb, "INFO");
    avi_write_info_tag(pb, "INAM", s->title);
    avi_write_info_tag(pb, "IART", s->author);
    avi_write_info_tag(pb, "ICOP", s->copyright);
    avi_write_info_tag(pb, "ICMT", s->comment);
    avi_write_info_tag(pb, "IPRD", s->album);
    avi_write_info_tag(pb, "IGNR", s->genre);
    if (s->track) {
        char str_track[4];
        snprintf(str_track, 4, "%d", s->track);
        avi_write_info_tag(pb, "IPRT", str_track);
    }
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
        avi_write_info_tag(pb, "ISFT", LIBAVFORMAT_IDENT);
    end_tag(pb, list2);

    /* some padding for easier tag editing */
    list2 = start_tag(pb, "JUNK");
    for (i = 0; i < 1016; i += 4)
        put_le32(pb, 0);
    end_tag(pb, list2);

    avi->movi_list = start_tag(pb, "LIST");
    put_tag(pb, "movi");

    put_flush_packet(pb);

    return 0;
}

static int avi_write_ix(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    char tag[5];
    char ix_tag[] = "ix00";
    int i, j;

    assert(!url_is_streamed(pb));

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
    ByteIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    offset_t idx_chunk;
    int i;
    char tag[5];

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

        avi_write_counters(s, avi->riff_id);
    }
    return 0;
}

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned char tag[5];
    unsigned int flags=0;
    const int stream_index= pkt->stream_index;
    AVCodecContext *enc= s->streams[stream_index]->codec;
    int size= pkt->size;

//    av_log(s, AV_LOG_DEBUG, "%"PRId64" %d %d\n", pkt->dts, avi->packet_count[stream_index], stream_index);
    while(enc->block_align==0 && pkt->dts != AV_NOPTS_VALUE && pkt->dts > avi->packet_count[stream_index]){
        AVPacket empty_packet;

        av_init_packet(&empty_packet);
        empty_packet.size= 0;
        empty_packet.data= NULL;
        empty_packet.stream_index= stream_index;
        avi_write_packet(s, &empty_packet);
//        av_log(s, AV_LOG_DEBUG, "dup %"PRId64" %d\n", pkt->dts, avi->packet_count[stream_index]);
    }
    avi->packet_count[stream_index]++;

    // Make sure to put an OpenDML chunk when the file size exceeds the limits
    if (!url_is_streamed(pb) &&
        (url_ftell(pb) - avi->riff_start > AVI_MAX_RIFF_SIZE)) {

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

    if (!url_is_streamed(s->pb)) {
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
    ByteIOContext *pb = s->pb;
    int res = 0;
    int i, j, n, nb_frames;
    offset_t file_size;

    if (!url_is_streamed(pb)){
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
                    if (nb_frames < avi->packet_count[n])
                        nb_frames = avi->packet_count[n];
                } else {
                    if (stream->codec_id == CODEC_ID_MP2 || stream->codec_id == CODEC_ID_MP3) {
                        nb_frames += avi->packet_count[n];
                    }
                }
            }
            put_le32(pb, nb_frames);
            url_fseek(pb, file_size, SEEK_SET);

            avi_write_counters(s, avi->riff_id);
        }
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

AVOutputFormat avi_muxer = {
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
    .codec_tag= (const AVCodecTag*[]){codec_bmp_tags, codec_wav_tags, 0},
};
#endif //CONFIG_AVI_MUXER
