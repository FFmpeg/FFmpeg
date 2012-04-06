/*
 * AVI muxer
 * Copyright (c) 2000 Fabrice Bellard
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
#include "internal.h"
#include "avi.h"
#include "avio_internal.h"
#include "riff.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"

/*
 * TODO:
 *  - fill all fields if non streamed (nb_frames for example)
 */

typedef struct AVIIentry {
    unsigned int flags, pos, len;
} AVIIentry;

#define AVI_INDEX_CLUSTER_SIZE 16384

typedef struct AVIIndex {
    int64_t     indx_start;
    int         entry;
    int         ents_allocated;
    AVIIentry** cluster;
} AVIIndex;

typedef struct {
    int64_t riff_start, movi_list, odml_list;
    int64_t frames_hdr_all;
    int riff_id;
} AVIContext;

typedef struct  {
    int64_t frames_hdr_strm;
    int audio_strm_length;
    int packet_count;
    int entry;

    AVIIndex indexes;
} AVIStream ;

static inline AVIIentry* avi_get_ientry(AVIIndex* idx, int ent_id)
{
    int cl = ent_id / AVI_INDEX_CLUSTER_SIZE;
    int id = ent_id % AVI_INDEX_CLUSTER_SIZE;
    return &idx->cluster[cl][id];
}

static int64_t avi_start_new_riff(AVFormatContext *s, AVIOContext *pb,
                                  const char* riff_tag, const char* list_tag)
{
    AVIContext *avi= s->priv_data;
    int64_t loff;
    int i;

    avi->riff_id++;
    for (i=0; i<s->nb_streams; i++){
        AVIStream *avist= s->streams[i]->priv_data;
        avist->indexes.entry = 0;
    }

    avi->riff_start = ff_start_tag(pb, "RIFF");
    ffio_wfourcc(pb, riff_tag);
    loff = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, list_tag);
    return loff;
}

static char* avi_stream2fourcc(char* tag, int index, enum AVMediaType type)
{
    tag[0] = '0' + index/10;
    tag[1] = '0' + index%10;
    if (type == AVMEDIA_TYPE_VIDEO) {
        tag[2] = 'd';
        tag[3] = 'c';
    } else if (type == AVMEDIA_TYPE_SUBTITLE) {
        // note: this is not an official code
        tag[2] = 's';
        tag[3] = 'b';
    } else {
        tag[2] = 'w';
        tag[3] = 'b';
    }
    tag[4] = '\0';
    return tag;
}

static void avi_write_info_tag(AVIOContext *pb, const char *tag, const char *str)
{
    int len = strlen(str);
    if (len > 0) {
        len++;
        ffio_wfourcc(pb, tag);
        avio_wl32(pb, len);
        avio_put_str(pb, str);
        if (len & 1)
            avio_w8(pb, 0);
    }
}

static int avi_write_counters(AVFormatContext* s, int riff_id)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    int n, au_byterate, au_ssize, au_scale, nb_frames = 0;
    int64_t file_size;
    AVCodecContext* stream;

    file_size = avio_tell(pb);
    for(n = 0; n < s->nb_streams; n++) {
        AVIStream *avist= s->streams[n]->priv_data;

        assert(avist->frames_hdr_strm);
        stream = s->streams[n]->codec;
        avio_seek(pb, avist->frames_hdr_strm, SEEK_SET);
        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
        if(au_ssize == 0) {
            avio_wl32(pb, avist->packet_count);
        } else {
            avio_wl32(pb, avist->audio_strm_length / au_ssize);
        }
        if(stream->codec_type == AVMEDIA_TYPE_VIDEO)
            nb_frames = FFMAX(nb_frames, avist->packet_count);
    }
    if(riff_id == 1) {
        assert(avi->frames_hdr_all);
        avio_seek(pb, avi->frames_hdr_all, SEEK_SET);
        avio_wl32(pb, nb_frames);
    }
    avio_seek(pb, file_size, SEEK_SET);

    return 0;
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream, *video_enc;
    int64_t list1, list2, strh, strf;
    AVDictionaryEntry *t = NULL;

    if (s->nb_streams > AVI_MAX_STREAM_COUNT) {
        av_log(s, AV_LOG_ERROR, "AVI does not support >%d streams\n",
               AVI_MAX_STREAM_COUNT);
        return -1;
    }

    for(n=0;n<s->nb_streams;n++) {
        s->streams[n]->priv_data= av_mallocz(sizeof(AVIStream));
        if(!s->streams[n]->priv_data)
            return AVERROR(ENOMEM);
    }

    /* header list */
    avi->riff_id = 0;
    list1 = avi_start_new_riff(s, pb, "AVI ", "hdrl");

    /* avi header */
    ffio_wfourcc(pb, "avih");
    avio_wl32(pb, 14 * 4);
    bitrate = 0;

    video_enc = NULL;
    for(n=0;n<s->nb_streams;n++) {
        stream = s->streams[n]->codec;
        bitrate += stream->bit_rate;
        if (stream->codec_type == AVMEDIA_TYPE_VIDEO)
            video_enc = stream;
    }

    nb_frames = 0;

    if(video_enc){
        avio_wl32(pb, (uint32_t)(INT64_C(1000000) * video_enc->time_base.num / video_enc->time_base.den));
    } else {
        avio_wl32(pb, 0);
    }
    avio_wl32(pb, bitrate / 8); /* XXX: not quite exact */
    avio_wl32(pb, 0); /* padding */
    if (!pb->seekable)
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_ISINTERLEAVED); /* flags */
    else
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED); /* flags */
    avi->frames_hdr_all = avio_tell(pb); /* remember this offset to fill later */
    avio_wl32(pb, nb_frames); /* nb frames, filled later */
    avio_wl32(pb, 0); /* initial frame */
    avio_wl32(pb, s->nb_streams); /* nb streams */
    avio_wl32(pb, 1024 * 1024); /* suggested buffer size */
    if(video_enc){
        avio_wl32(pb, video_enc->width);
        avio_wl32(pb, video_enc->height);
    } else {
        avio_wl32(pb, 0);
        avio_wl32(pb, 0);
    }
    avio_wl32(pb, 0); /* reserved */
    avio_wl32(pb, 0); /* reserved */
    avio_wl32(pb, 0); /* reserved */
    avio_wl32(pb, 0); /* reserved */

    /* stream list */
    for(i=0;i<n;i++) {
        AVIStream *avist= s->streams[i]->priv_data;
        list2 = ff_start_tag(pb, "LIST");
        ffio_wfourcc(pb, "strl");

        stream = s->streams[i]->codec;

        /* stream generic header */
        strh = ff_start_tag(pb, "strh");
        switch(stream->codec_type) {
        case AVMEDIA_TYPE_SUBTITLE:
            // XSUB subtitles behave like video tracks, other subtitles
            // are not (yet) supported.
            if (stream->codec_id != CODEC_ID_XSUB) {
                av_log(s, AV_LOG_ERROR, "Subtitle streams other than DivX XSUB are not supported by the AVI muxer.\n");
                return AVERROR_PATCHWELCOME;
            }
        case AVMEDIA_TYPE_VIDEO: ffio_wfourcc(pb, "vids"); break;
        case AVMEDIA_TYPE_AUDIO: ffio_wfourcc(pb, "auds"); break;
//      case AVMEDIA_TYPE_TEXT : ffio_wfourcc(pb, "txts"); break;
        case AVMEDIA_TYPE_DATA : ffio_wfourcc(pb, "dats"); break;
        }
        if(stream->codec_type == AVMEDIA_TYPE_VIDEO ||
           stream->codec_id == CODEC_ID_XSUB)
            avio_wl32(pb, stream->codec_tag);
        else
            avio_wl32(pb, 1);
        avio_wl32(pb, 0); /* flags */
        avio_wl16(pb, 0); /* priority */
        avio_wl16(pb, 0); /* language */
        avio_wl32(pb, 0); /* initial frame */

        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);

        avio_wl32(pb, au_scale); /* scale */
        avio_wl32(pb, au_byterate); /* rate */
        avpriv_set_pts_info(s->streams[i], 64, au_scale, au_byterate);

        avio_wl32(pb, 0); /* start */
        avist->frames_hdr_strm = avio_tell(pb); /* remember this offset to fill later */
        if (!pb->seekable)
            avio_wl32(pb, AVI_MAX_RIFF_SIZE); /* FIXME: this may be broken, but who cares */
        else
            avio_wl32(pb, 0); /* length, XXX: filled later */

        /* suggested buffer size */ //FIXME set at the end to largest chunk
        if(stream->codec_type == AVMEDIA_TYPE_VIDEO)
            avio_wl32(pb, 1024 * 1024);
        else if(stream->codec_type == AVMEDIA_TYPE_AUDIO)
            avio_wl32(pb, 12 * 1024);
        else
            avio_wl32(pb, 0);
        avio_wl32(pb, -1); /* quality */
        avio_wl32(pb, au_ssize); /* sample size */
        avio_wl32(pb, 0);
        avio_wl16(pb, stream->width);
        avio_wl16(pb, stream->height);
        ff_end_tag(pb, strh);

      if(stream->codec_type != AVMEDIA_TYPE_DATA){
        strf = ff_start_tag(pb, "strf");
        switch(stream->codec_type) {
        case AVMEDIA_TYPE_SUBTITLE:
            // XSUB subtitles behave like video tracks, other subtitles
            // are not (yet) supported.
            if (stream->codec_id != CODEC_ID_XSUB) break;
        case AVMEDIA_TYPE_VIDEO:
            ff_put_bmp_header(pb, stream, ff_codec_bmp_tags, 0);
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (ff_put_wav_header(pb, stream) < 0) {
                return -1;
            }
            break;
        default:
            return -1;
        }
        ff_end_tag(pb, strf);
        if ((t = av_dict_get(s->streams[i]->metadata, "title", NULL, 0))) {
            avi_write_info_tag(s->pb, "strn", t->value);
            t = NULL;
        }
      }

        if (pb->seekable) {
            unsigned char tag[5];
            int j;

            /* Starting to lay out AVI OpenDML master index.
             * We want to make it JUNK entry for now, since we'd
             * like to get away without making AVI an OpenDML one
             * for compatibility reasons.
             */
            avist->indexes.entry = avist->indexes.ents_allocated = 0;
            avist->indexes.indx_start = ff_start_tag(pb, "JUNK");
            avio_wl16(pb, 4);        /* wLongsPerEntry */
            avio_w8(pb, 0);          /* bIndexSubType (0 == frame index) */
            avio_w8(pb, 0);          /* bIndexType (0 == AVI_INDEX_OF_INDEXES) */
            avio_wl32(pb, 0);        /* nEntriesInUse (will fill out later on) */
            ffio_wfourcc(pb, avi_stream2fourcc(tag, i, stream->codec_type));
                                    /* dwChunkId */
            avio_wl64(pb, 0);        /* dwReserved[3]
            avio_wl32(pb, 0);           Must be 0.    */
            for (j=0; j < AVI_MASTER_INDEX_SIZE * 2; j++)
                 avio_wl64(pb, 0);
            ff_end_tag(pb, avist->indexes.indx_start);
        }

        if(   stream->codec_type == AVMEDIA_TYPE_VIDEO
           && s->streams[i]->sample_aspect_ratio.num>0
           && s->streams[i]->sample_aspect_ratio.den>0){
            int vprp= ff_start_tag(pb, "vprp");
            AVRational dar = av_mul_q(s->streams[i]->sample_aspect_ratio,
                                      (AVRational){stream->width, stream->height});
            int num, den;
            av_reduce(&num, &den, dar.num, dar.den, 0xFFFF);

            avio_wl32(pb, 0); //video format  = unknown
            avio_wl32(pb, 0); //video standard= unknown
            avio_wl32(pb, lrintf(1.0/av_q2d(stream->time_base)));
            avio_wl32(pb, stream->width );
            avio_wl32(pb, stream->height);
            avio_wl16(pb, den);
            avio_wl16(pb, num);
            avio_wl32(pb, stream->width );
            avio_wl32(pb, stream->height);
            avio_wl32(pb, 1); //progressive FIXME

            avio_wl32(pb, stream->height);
            avio_wl32(pb, stream->width );
            avio_wl32(pb, stream->height);
            avio_wl32(pb, stream->width );
            avio_wl32(pb, 0);
            avio_wl32(pb, 0);

            avio_wl32(pb, 0);
            avio_wl32(pb, 0);
            ff_end_tag(pb, vprp);
        }

        ff_end_tag(pb, list2);
    }

    if (pb->seekable) {
        /* AVI could become an OpenDML one, if it grows beyond 2Gb range */
        avi->odml_list = ff_start_tag(pb, "JUNK");
        ffio_wfourcc(pb, "odml");
        ffio_wfourcc(pb, "dmlh");
        avio_wl32(pb, 248);
        for (i = 0; i < 248; i+= 4)
             avio_wl32(pb, 0);
        ff_end_tag(pb, avi->odml_list);
    }

    ff_end_tag(pb, list1);

    list2 = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "INFO");
    ff_metadata_conv(&s->metadata, ff_riff_info_conv, NULL);
    for (i = 0; *ff_riff_tags[i]; i++) {
        if ((t = av_dict_get(s->metadata, ff_riff_tags[i], NULL, AV_DICT_MATCH_CASE)))
            avi_write_info_tag(s->pb, t->key, t->value);
    }
    ff_end_tag(pb, list2);

    /* some padding for easier tag editing */
    list2 = ff_start_tag(pb, "JUNK");
    for (i = 0; i < 1016; i += 4)
        avio_wl32(pb, 0);
    ff_end_tag(pb, list2);

    avi->movi_list = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "movi");

    avio_flush(pb);

    return 0;
}

static int avi_write_ix(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    char tag[5];
    char ix_tag[] = "ix00";
    int i, j;

    assert(pb->seekable);

    if (avi->riff_id > AVI_MASTER_INDEX_SIZE)
        return -1;

    for (i=0;i<s->nb_streams;i++) {
        AVIStream *avist= s->streams[i]->priv_data;
         int64_t ix, pos;

         avi_stream2fourcc(tag, i, s->streams[i]->codec->codec_type);
         ix_tag[3] = '0' + i;

         /* Writing AVI OpenDML leaf index chunk */
         ix = avio_tell(pb);
         ffio_wfourcc(pb, ix_tag);     /* ix?? */
         avio_wl32(pb, avist->indexes.entry * 8 + 24);
                                      /* chunk size */
         avio_wl16(pb, 2);             /* wLongsPerEntry */
         avio_w8(pb, 0);             /* bIndexSubType (0 == frame index) */
         avio_w8(pb, 1);             /* bIndexType (1 == AVI_INDEX_OF_CHUNKS) */
         avio_wl32(pb, avist->indexes.entry);
                                      /* nEntriesInUse */
         ffio_wfourcc(pb, tag);        /* dwChunkId */
         avio_wl64(pb, avi->movi_list);/* qwBaseOffset */
         avio_wl32(pb, 0);             /* dwReserved_3 (must be 0) */

         for (j=0; j<avist->indexes.entry; j++) {
             AVIIentry* ie = avi_get_ientry(&avist->indexes, j);
             avio_wl32(pb, ie->pos + 8);
             avio_wl32(pb, ((uint32_t)ie->len & ~0x80000000) |
                          (ie->flags & 0x10 ? 0 : 0x80000000));
         }
         avio_flush(pb);
         pos = avio_tell(pb);

         /* Updating one entry in the AVI OpenDML master index */
         avio_seek(pb, avist->indexes.indx_start - 8, SEEK_SET);
         ffio_wfourcc(pb, "indx");            /* enabling this entry */
         avio_skip(pb, 8);
         avio_wl32(pb, avi->riff_id);         /* nEntriesInUse */
         avio_skip(pb, 16*avi->riff_id);
         avio_wl64(pb, ix);                   /* qwOffset */
         avio_wl32(pb, pos - ix);             /* dwSize */
         avio_wl32(pb, avist->indexes.entry); /* dwDuration */

         avio_seek(pb, pos, SEEK_SET);
    }
    return 0;
}

static int avi_write_idx1(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    int64_t idx_chunk;
    int i;
    char tag[5];

    if (pb->seekable) {
        AVIStream *avist;
        AVIIentry* ie = 0, *tie;
        int empty, stream_id = -1;

        idx_chunk = ff_start_tag(pb, "idx1");
        for(i=0; i<s->nb_streams; i++){
            avist= s->streams[i]->priv_data;
            avist->entry=0;
        }

        do {
            empty = 1;
            for (i=0; i<s->nb_streams; i++) {
                avist= s->streams[i]->priv_data;
                 if (avist->indexes.entry <= avist->entry)
                     continue;

                 tie = avi_get_ientry(&avist->indexes, avist->entry);
                 if (empty || tie->pos < ie->pos) {
                     ie = tie;
                     stream_id = i;
                 }
                 empty = 0;
            }
            if (!empty) {
                avist= s->streams[stream_id]->priv_data;
                avi_stream2fourcc(tag, stream_id,
                                  s->streams[stream_id]->codec->codec_type);
                ffio_wfourcc(pb, tag);
                avio_wl32(pb, ie->flags);
                avio_wl32(pb, ie->pos);
                avio_wl32(pb, ie->len);
                avist->entry++;
            }
        } while (!empty);
        ff_end_tag(pb, idx_chunk);

        avi_write_counters(s, avi->riff_id);
    }
    return 0;
}

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned char tag[5];
    unsigned int flags=0;
    const int stream_index= pkt->stream_index;
    AVIStream *avist= s->streams[stream_index]->priv_data;
    AVCodecContext *enc= s->streams[stream_index]->codec;
    int size= pkt->size;

//    av_log(s, AV_LOG_DEBUG, "%"PRId64" %d %d\n", pkt->dts, avi->packet_count[stream_index], stream_index);
    while(enc->block_align==0 && pkt->dts != AV_NOPTS_VALUE && pkt->dts > avist->packet_count){
        AVPacket empty_packet;

        av_init_packet(&empty_packet);
        empty_packet.size= 0;
        empty_packet.data= NULL;
        empty_packet.stream_index= stream_index;
        avi_write_packet(s, &empty_packet);
//        av_log(s, AV_LOG_DEBUG, "dup %"PRId64" %d\n", pkt->dts, avi->packet_count[stream_index]);
    }
    avist->packet_count++;

    // Make sure to put an OpenDML chunk when the file size exceeds the limits
    if (pb->seekable &&
        (avio_tell(pb) - avi->riff_start > AVI_MAX_RIFF_SIZE)) {

        avi_write_ix(s);
        ff_end_tag(pb, avi->movi_list);

        if (avi->riff_id == 1)
            avi_write_idx1(s);

        ff_end_tag(pb, avi->riff_start);
        avi->movi_list = avi_start_new_riff(s, pb, "AVIX", "movi");
    }

    avi_stream2fourcc(tag, stream_index, enc->codec_type);
    if(pkt->flags&AV_PKT_FLAG_KEY)
        flags = 0x10;
    if (enc->codec_type == AVMEDIA_TYPE_AUDIO) {
       avist->audio_strm_length += size;
    }

    if (s->pb->seekable) {
        AVIIndex* idx = &avist->indexes;
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
        idx->cluster[cl][id].pos = avio_tell(pb) - avi->movi_list;
        idx->cluster[cl][id].len = size;
        idx->entry++;
    }

    avio_write(pb, tag, 4);
    avio_wl32(pb, size);
    avio_write(pb, pkt->data, size);
    if (size & 1)
        avio_w8(pb, 0);

    avio_flush(pb);
    return 0;
}

static int avi_write_trailer(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    int res = 0;
    int i, j, n, nb_frames;
    int64_t file_size;

    if (pb->seekable){
        if (avi->riff_id == 1) {
            ff_end_tag(pb, avi->movi_list);
            res = avi_write_idx1(s);
            ff_end_tag(pb, avi->riff_start);
        } else {
            avi_write_ix(s);
            ff_end_tag(pb, avi->movi_list);
            ff_end_tag(pb, avi->riff_start);

            file_size = avio_tell(pb);
            avio_seek(pb, avi->odml_list - 8, SEEK_SET);
            ffio_wfourcc(pb, "LIST"); /* Making this AVI OpenDML one */
            avio_skip(pb, 16);

            for (n=nb_frames=0;n<s->nb_streams;n++) {
                AVCodecContext *stream = s->streams[n]->codec;
                AVIStream *avist= s->streams[n]->priv_data;

                if (stream->codec_type == AVMEDIA_TYPE_VIDEO) {
                    if (nb_frames < avist->packet_count)
                        nb_frames = avist->packet_count;
                } else {
                    if (stream->codec_id == CODEC_ID_MP2 || stream->codec_id == CODEC_ID_MP3) {
                        nb_frames += avist->packet_count;
                    }
                }
            }
            avio_wl32(pb, nb_frames);
            avio_seek(pb, file_size, SEEK_SET);

            avi_write_counters(s, avi->riff_id);
        }
    }
    avio_flush(pb);

    for (i=0; i<s->nb_streams; i++) {
         AVIStream *avist= s->streams[i]->priv_data;
         for (j=0; j<avist->indexes.ents_allocated/AVI_INDEX_CLUSTER_SIZE; j++)
              av_free(avist->indexes.cluster[j]);
         av_freep(&avist->indexes.cluster);
         avist->indexes.ents_allocated = avist->indexes.entry = 0;
    }

    return res;
}

AVOutputFormat ff_avi_muxer = {
    .name              = "avi",
    .long_name         = NULL_IF_CONFIG_SMALL("AVI format"),
    .mime_type         = "video/x-msvideo",
    .extensions        = "avi",
    .priv_data_size    = sizeof(AVIContext),
#if CONFIG_LIBMP3LAME_ENCODER
    .audio_codec       = CODEC_ID_MP3,
#else
    .audio_codec       = CODEC_ID_AC3,
#endif
    .video_codec       = CODEC_ID_MPEG4,
    .write_header      = avi_write_header,
    .write_packet      = avi_write_packet,
    .write_trailer     = avi_write_trailer,
    .codec_tag         = (const AVCodecTag* const []){
        ff_codec_bmp_tags, ff_codec_wav_tags, 0
    },
    .flags             = AVFMT_VARIABLE_FPS,
};
