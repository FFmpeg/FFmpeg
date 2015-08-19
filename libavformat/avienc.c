/*
 * AVI muxer
 * Copyright (c) 2000 Fabrice Bellard
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

//#define DEBUG

#include "avformat.h"
#include "internal.h"
#include "avi.h"
#include "avio_internal.h"
#include "riff.h"
#include "mpegts.h"
#include "libavformat/avlanguage.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/avassert.h"
#include "libavutil/timestamp.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/raw.h"

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
    int64_t     audio_strm_offset;
    int         entry;
    int         ents_allocated;
    int         master_odml_riff_id_base;
    AVIIentry** cluster;
} AVIIndex;

typedef struct AVIContext {
    int64_t riff_start, movi_list, odml_list;
    int64_t frames_hdr_all;
    int riff_id;
} AVIContext;

typedef struct AVIStream {
    int64_t frames_hdr_strm;
    int64_t audio_strm_length;
    int packet_count;
    int entry;
    int max_size;
    int sample_requested;

    int64_t last_dts;

    AVIIndex indexes;
} AVIStream;

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt);

static inline AVIIentry *avi_get_ientry(const AVIIndex *idx, int ent_id)
{
    int cl = ent_id / AVI_INDEX_CLUSTER_SIZE;
    int id = ent_id % AVI_INDEX_CLUSTER_SIZE;
    return &idx->cluster[cl][id];
}

static int64_t avi_start_new_riff(AVFormatContext *s, AVIOContext *pb,
                                  const char *riff_tag, const char *list_tag)
{
    AVIContext *avi = s->priv_data;
    int64_t loff;
    int i;

    avi->riff_id++;
    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        avist->indexes.audio_strm_offset = avist->audio_strm_length;
        avist->indexes.entry = 0;
    }

    avi->riff_start = ff_start_tag(pb, "RIFF");
    ffio_wfourcc(pb, riff_tag);
    loff = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, list_tag);
    return loff;
}

static char *avi_stream2fourcc(char *tag, int index, enum AVMediaType type)
{
    tag[0] = '0' + index / 10;
    tag[1] = '0' + index % 10;
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

static int avi_write_counters(AVFormatContext *s, int riff_id)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    int n, au_byterate, au_ssize, au_scale, nb_frames = 0;
    int64_t file_size;
    AVCodecContext *stream;

    file_size = avio_tell(pb);
    for (n = 0; n < s->nb_streams; n++) {
        AVIStream *avist = s->streams[n]->priv_data;

        av_assert0(avist->frames_hdr_strm);
        stream = s->streams[n]->codec;
        avio_seek(pb, avist->frames_hdr_strm, SEEK_SET);
        ff_parse_specific_params(s->streams[n], &au_byterate, &au_ssize, &au_scale);
        if (au_ssize == 0)
            avio_wl32(pb, avist->packet_count);
        else
            avio_wl32(pb, avist->audio_strm_length / au_ssize);
        if (stream->codec_type == AVMEDIA_TYPE_VIDEO)
            nb_frames = FFMAX(nb_frames, avist->packet_count);
    }
    if (riff_id == 1) {
        av_assert0(avi->frames_hdr_all);
        avio_seek(pb, avi->frames_hdr_all, SEEK_SET);
        avio_wl32(pb, nb_frames);
    }
    avio_seek(pb, file_size, SEEK_SET);

    return 0;
}

static void write_odml_master(AVFormatContext *s, int stream_index)
{
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[stream_index];
    AVCodecContext *enc = st->codec;
    AVIStream *avist = st->priv_data;
    unsigned char tag[5];
    int j;

    /* Starting to lay out AVI OpenDML master index.
        * We want to make it JUNK entry for now, since we'd
        * like to get away without making AVI an OpenDML one
        * for compatibility reasons. */
    avist->indexes.indx_start = ff_start_tag(pb, "JUNK");
    avio_wl16(pb, 4);   /* wLongsPerEntry */
    avio_w8(pb, 0);     /* bIndexSubType (0 == frame index) */
    avio_w8(pb, 0);     /* bIndexType (0 == AVI_INDEX_OF_INDEXES) */
    avio_wl32(pb, 0);   /* nEntriesInUse (will fill out later on) */
    ffio_wfourcc(pb, avi_stream2fourcc(tag, stream_index, enc->codec_type));
                        /* dwChunkId */
    avio_wl64(pb, 0);   /* dwReserved[3] */
    avio_wl32(pb, 0);   /* Must be 0.    */
    for (j = 0; j < AVI_MASTER_INDEX_SIZE * 2; j++)
        avio_wl64(pb, 0);
    ff_end_tag(pb, avist->indexes.indx_start);
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *video_enc;
    AVStream *video_st = NULL;
    int64_t list1, list2, strh, strf;
    AVDictionaryEntry *t = NULL;
    int padding;

    if (s->nb_streams > AVI_MAX_STREAM_COUNT) {
        av_log(s, AV_LOG_ERROR, "AVI does not support >%d streams\n",
               AVI_MAX_STREAM_COUNT);
        return AVERROR(EINVAL);
    }

    for (n = 0; n < s->nb_streams; n++) {
        s->streams[n]->priv_data = av_mallocz(sizeof(AVIStream));
        if (!s->streams[n]->priv_data)
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
    for (n = 0; n < s->nb_streams; n++) {
        AVCodecContext *codec = s->streams[n]->codec;
        bitrate += codec->bit_rate;
        if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_enc = codec;
            video_st = s->streams[n];
        }
    }

    nb_frames = 0;

    // TODO: should be avg_frame_rate
    if (video_st)
        avio_wl32(pb, (uint32_t) (INT64_C(1000000) * video_st->time_base.num /
                                  video_st->time_base.den));
    else
        avio_wl32(pb, 0);
    avio_wl32(pb, bitrate / 8); /* XXX: not quite exact */
    avio_wl32(pb, 0); /* padding */
    if (!pb->seekable)
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_ISINTERLEAVED);  /* flags */
    else
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED);  /* flags */
    avi->frames_hdr_all = avio_tell(pb); /* remember this offset to fill later */
    avio_wl32(pb, nb_frames); /* nb frames, filled later */
    avio_wl32(pb, 0); /* initial frame */
    avio_wl32(pb, s->nb_streams); /* nb streams */
    avio_wl32(pb, 1024 * 1024); /* suggested buffer size */
    if (video_enc) {
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
    for (i = 0; i < n; i++) {
        AVStream *st = s->streams[i];
        AVCodecContext *enc = st->codec;
        AVIStream *avist = st->priv_data;
        list2 = ff_start_tag(pb, "LIST");
        ffio_wfourcc(pb, "strl");

        /* stream generic header */
        strh = ff_start_tag(pb, "strh");
        switch (enc->codec_type) {
        case AVMEDIA_TYPE_SUBTITLE:
            // XSUB subtitles behave like video tracks, other subtitles
            // are not (yet) supported.
            if (enc->codec_id != AV_CODEC_ID_XSUB) {
                av_log(s, AV_LOG_ERROR,
                       "Subtitle streams other than DivX XSUB are not supported by the AVI muxer.\n");
                return AVERROR_PATCHWELCOME;
            }
        case AVMEDIA_TYPE_VIDEO:
            ffio_wfourcc(pb, "vids");
            break;
        case AVMEDIA_TYPE_AUDIO:
            ffio_wfourcc(pb, "auds");
            break;
//      case AVMEDIA_TYPE_TEXT:
//          ffio_wfourcc(pb, "txts");
//          break;
        case AVMEDIA_TYPE_DATA:
            ffio_wfourcc(pb, "dats");
            break;
        }
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO ||
            enc->codec_id == AV_CODEC_ID_XSUB)
            avio_wl32(pb, enc->codec_tag);
        else
            avio_wl32(pb, 1);
        avio_wl32(pb, 0); /* flags */
        avio_wl16(pb, 0); /* priority */
        avio_wl16(pb, 0); /* language */
        avio_wl32(pb, 0); /* initial frame */

        ff_parse_specific_params(st, &au_byterate, &au_ssize, &au_scale);

        if (   enc->codec_type == AVMEDIA_TYPE_VIDEO
            && enc->codec_id != AV_CODEC_ID_XSUB
            && au_byterate > 1000LL*au_scale) {
            au_byterate = 600;
            au_scale    = 1;
        }
        avpriv_set_pts_info(st, 64, au_scale, au_byterate);
        if (enc->codec_id == AV_CODEC_ID_XSUB)
            au_scale = au_byterate = 0;

        avio_wl32(pb, au_scale); /* scale */
        avio_wl32(pb, au_byterate); /* rate */

        avio_wl32(pb, 0); /* start */
        /* remember this offset to fill later */
        avist->frames_hdr_strm = avio_tell(pb);
        if (!pb->seekable)
            /* FIXME: this may be broken, but who cares */
            avio_wl32(pb, AVI_MAX_RIFF_SIZE);
        else
            avio_wl32(pb, 0);  /* length, XXX: filled later */

        /* suggested buffer size, is set to largest chunk size in avi_write_trailer */
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
            avio_wl32(pb, 1024 * 1024);
        else if (enc->codec_type == AVMEDIA_TYPE_AUDIO)
            avio_wl32(pb, 12 * 1024);
        else
            avio_wl32(pb, 0);
        avio_wl32(pb, -1); /* quality */
        avio_wl32(pb, au_ssize); /* sample size */
        avio_wl32(pb, 0);
        avio_wl16(pb, enc->width);
        avio_wl16(pb, enc->height);
        ff_end_tag(pb, strh);

        if (enc->codec_type != AVMEDIA_TYPE_DATA) {
            int ret;
            enum AVPixelFormat pix_fmt;

            strf = ff_start_tag(pb, "strf");
            switch (enc->codec_type) {
            case AVMEDIA_TYPE_SUBTITLE:
                /* XSUB subtitles behave like video tracks, other subtitles
                 * are not (yet) supported. */
                if (enc->codec_id != AV_CODEC_ID_XSUB)
                    break;
            case AVMEDIA_TYPE_VIDEO:
                /* WMP expects RGB 5:5:5 rawvideo in avi to have bpp set to 16. */
                if (  !enc->codec_tag
                    && enc->codec_id == AV_CODEC_ID_RAWVIDEO
                    && enc->pix_fmt == AV_PIX_FMT_RGB555LE
                    && enc->bits_per_coded_sample == 15)
                    enc->bits_per_coded_sample = 16;
                ff_put_bmp_header(pb, enc, ff_codec_bmp_tags, 0, 0);
                pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                              enc->bits_per_coded_sample);
                if (   !enc->codec_tag
                    && enc->codec_id == AV_CODEC_ID_RAWVIDEO
                    && enc->pix_fmt != pix_fmt
                    && enc->pix_fmt != AV_PIX_FMT_NONE)
                    av_log(s, AV_LOG_ERROR, "%s rawvideo cannot be written to avi, output file will be unreadable\n",
                          av_get_pix_fmt_name(enc->pix_fmt));
                break;
            case AVMEDIA_TYPE_AUDIO:
                if ((ret = ff_put_wav_header(pb, enc, 0)) < 0)
                    return ret;
                break;
            default:
                av_log(s, AV_LOG_ERROR,
                    "Invalid or not supported codec type '%s' found in the input\n",
                    (char *)av_x_if_null(av_get_media_type_string(enc->codec_type), "?"));
                return AVERROR(EINVAL);
            }
            ff_end_tag(pb, strf);
            if ((t = av_dict_get(st->metadata, "title", NULL, 0))) {
                ff_riff_write_info_tag(s->pb, "strn", t->value);
                t = NULL;
            }
            if (enc->codec_id == AV_CODEC_ID_XSUB
            && (t = av_dict_get(s->streams[i]->metadata, "language", NULL, 0))) {
                const char* langstr = av_convert_lang_to(t->value, AV_LANG_ISO639_1);
                t = NULL;
                if (langstr) {
                    char* str = av_asprintf("Subtitle - %s-xx;02", langstr);
                    if (!str)
                        return AVERROR(ENOMEM);
                    ff_riff_write_info_tag(s->pb, "strn", str);
                    av_free(str);
                }
            }
        }

        if (pb->seekable) {
            write_odml_master(s, i);
        }

        if (enc->codec_type == AVMEDIA_TYPE_VIDEO   &&
            st->sample_aspect_ratio.num > 0 &&
            st->sample_aspect_ratio.den > 0) {
            int vprp       = ff_start_tag(pb, "vprp");
            AVRational dar = av_mul_q(st->sample_aspect_ratio,
                                      (AVRational) { enc->width,
                                                     enc->height });
            int num, den;
            av_reduce(&num, &den, dar.num, dar.den, 0xFFFF);

            avio_wl32(pb, 0); // video format   = unknown
            avio_wl32(pb, 0); // video standard = unknown
            // TODO: should be avg_frame_rate
            avio_wl32(pb, (2LL*st->time_base.den + st->time_base.num - 1) / (2LL * st->time_base.num));
            avio_wl32(pb, enc->width);
            avio_wl32(pb, enc->height);
            avio_wl16(pb, den);
            avio_wl16(pb, num);
            avio_wl32(pb, enc->width);
            avio_wl32(pb, enc->height);
            avio_wl32(pb, 1); // progressive FIXME

            avio_wl32(pb, enc->height);
            avio_wl32(pb, enc->width);
            avio_wl32(pb, enc->height);
            avio_wl32(pb, enc->width);
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
        for (i = 0; i < 248; i += 4)
            avio_wl32(pb, 0);
        ff_end_tag(pb, avi->odml_list);
    }

    ff_end_tag(pb, list1);

    ff_riff_write_info(s);


    padding = s->metadata_header_padding;
    if (padding < 0)
        padding = 1016;

    /* some padding for easier tag editing */
    if (padding) {
        list2 = ff_start_tag(pb, "JUNK");
        for (i = padding; i > 0; i -= 4)
            avio_wl32(pb, 0);
        ff_end_tag(pb, list2);
    }

    avi->movi_list = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "movi");

    avio_flush(pb);

    return 0;
}

static void update_odml_entry(AVFormatContext *s, int stream_index, int64_t ix, int size)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    AVIStream *avist = s->streams[stream_index]->priv_data;
    int64_t pos;
    int au_byterate, au_ssize, au_scale;

    avio_flush(pb);
    pos = avio_tell(pb);

    /* Updating one entry in the AVI OpenDML master index */
    avio_seek(pb, avist->indexes.indx_start - 8, SEEK_SET);
    ffio_wfourcc(pb, "indx");             /* enabling this entry */
    avio_skip(pb, 8);
    avio_wl32(pb, avi->riff_id - avist->indexes.master_odml_riff_id_base);          /* nEntriesInUse */
    avio_skip(pb, 16 * (avi->riff_id - avist->indexes.master_odml_riff_id_base));
    avio_wl64(pb, ix);                    /* qwOffset */
    avio_wl32(pb, size);                  /* dwSize */
    ff_parse_specific_params(s->streams[stream_index], &au_byterate, &au_ssize, &au_scale);
    if (s->streams[stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO && au_ssize > 0) {
        uint32_t audio_segm_size = (avist->audio_strm_length - avist->indexes.audio_strm_offset);
        if ((audio_segm_size % au_ssize > 0) && !avist->sample_requested) {
            avpriv_request_sample(s, "OpenDML index duration for audio packets with partial frames");
            avist->sample_requested = 1;
        }
        avio_wl32(pb, audio_segm_size / au_ssize);  /* dwDuration (sample count) */
    } else
        avio_wl32(pb, avist->indexes.entry);  /* dwDuration (packet count) */

    avio_seek(pb, pos, SEEK_SET);
}

static int avi_write_ix(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    char tag[5];
    char ix_tag[] = "ix00";
    int i, j;

    av_assert0(pb->seekable);

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        if (avi->riff_id - avist->indexes.master_odml_riff_id_base == AVI_MASTER_INDEX_SIZE) {
            int64_t pos;
            int size = 8+2+1+1+4+8+4+4+16*AVI_MASTER_INDEX_SIZE;

            pos = avio_tell(pb);
            update_odml_entry(s, i, pos, size);
            write_odml_master(s, i);
            av_assert1(avio_tell(pb) - pos == size);
            avist->indexes.master_odml_riff_id_base = avi->riff_id - 1;
        }
        av_assert0(avi->riff_id - avist->indexes.master_odml_riff_id_base < AVI_MASTER_INDEX_SIZE);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        int64_t ix;

        avi_stream2fourcc(tag, i, s->streams[i]->codec->codec_type);
        ix_tag[3] = '0' + i;

        /* Writing AVI OpenDML leaf index chunk */
        ix = avio_tell(pb);
        ffio_wfourcc(pb, ix_tag);      /* ix?? */
        avio_wl32(pb, avist->indexes.entry * 8 + 24);
        /* chunk size */
        avio_wl16(pb, 2);           /* wLongsPerEntry */
        avio_w8(pb, 0);             /* bIndexSubType (0 == frame index) */
        avio_w8(pb, 1);             /* bIndexType (1 == AVI_INDEX_OF_CHUNKS) */
        avio_wl32(pb, avist->indexes.entry);
        /* nEntriesInUse */
        ffio_wfourcc(pb, tag);         /* dwChunkId */
        avio_wl64(pb, avi->movi_list); /* qwBaseOffset */
        avio_wl32(pb, 0);              /* dwReserved_3 (must be 0) */

        for (j = 0; j < avist->indexes.entry; j++) {
            AVIIentry *ie = avi_get_ientry(&avist->indexes, j);
            avio_wl32(pb, ie->pos + 8);
            avio_wl32(pb, ((uint32_t) ie->len & ~0x80000000) |
                          (ie->flags & 0x10 ? 0 : 0x80000000));
        }

        update_odml_entry(s, i, ix, avio_tell(pb) - ix);
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
        AVIIentry *ie = 0, *tie;
        int empty, stream_id = -1;

        idx_chunk = ff_start_tag(pb, "idx1");
        for (i = 0; i < s->nb_streams; i++) {
            avist        = s->streams[i]->priv_data;
            avist->entry = 0;
        }

        do {
            empty = 1;
            for (i = 0; i < s->nb_streams; i++) {
                avist = s->streams[i]->priv_data;
                if (avist->indexes.entry <= avist->entry)
                    continue;

                tie = avi_get_ientry(&avist->indexes, avist->entry);
                if (empty || tie->pos < ie->pos) {
                    ie        = tie;
                    stream_id = i;
                }
                empty = 0;
            }
            if (!empty) {
                avist = s->streams[stream_id]->priv_data;
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

static int write_skip_frames(AVFormatContext *s, int stream_index, int64_t dts)
{
    AVIStream *avist    = s->streams[stream_index]->priv_data;
    AVCodecContext *enc = s->streams[stream_index]->codec;

    ff_dlog(s, "dts:%s packet_count:%d stream_index:%d\n", av_ts2str(dts), avist->packet_count, stream_index);
    while (enc->block_align == 0 && dts != AV_NOPTS_VALUE &&
           dts > avist->packet_count && enc->codec_id != AV_CODEC_ID_XSUB && avist->packet_count) {
        AVPacket empty_packet;

        if (dts - avist->packet_count > 60000) {
            av_log(s, AV_LOG_ERROR, "Too large number of skipped frames %"PRId64" > 60000\n", dts - avist->packet_count);
            return AVERROR(EINVAL);
        }

        av_init_packet(&empty_packet);
        empty_packet.size         = 0;
        empty_packet.data         = NULL;
        empty_packet.stream_index = stream_index;
        avi_write_packet(s, &empty_packet);
        ff_dlog(s, "dup dts:%s packet_count:%d\n", av_ts2str(dts), avist->packet_count);
    }

    return 0;
}

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    unsigned char tag[5];
    unsigned int flags = 0;
    const int stream_index = pkt->stream_index;
    int size               = pkt->size;
    AVIContext *avi     = s->priv_data;
    AVIOContext *pb     = s->pb;
    AVIStream *avist    = s->streams[stream_index]->priv_data;
    AVCodecContext *enc = s->streams[stream_index]->codec;
    int ret;

    if (enc->codec_id == AV_CODEC_ID_H264 && enc->codec_tag == MKTAG('H','2','6','4') && pkt->size) {
        ret = ff_check_h264_startcode(s, s->streams[stream_index], pkt);
        if (ret < 0)
            return ret;
    }

    if ((ret = write_skip_frames(s, stream_index, pkt->dts)) < 0)
        return ret;

    if (pkt->dts != AV_NOPTS_VALUE)
        avist->last_dts = pkt->dts + pkt->duration;

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
    if (pkt->flags & AV_PKT_FLAG_KEY)
        flags = 0x10;
    if (enc->codec_type == AVMEDIA_TYPE_AUDIO)
        avist->audio_strm_length += size;

    if (s->pb->seekable) {
        AVIIndex *idx = &avist->indexes;
        int cl = idx->entry / AVI_INDEX_CLUSTER_SIZE;
        int id = idx->entry % AVI_INDEX_CLUSTER_SIZE;
        if (idx->ents_allocated <= idx->entry) {
            idx->cluster = av_realloc_f(idx->cluster, sizeof(void*), cl+1);
            if (!idx->cluster) {
                idx->ents_allocated = 0;
                idx->entry          = 0;
                return AVERROR(ENOMEM);
            }
            idx->cluster[cl] =
                av_malloc(AVI_INDEX_CLUSTER_SIZE * sizeof(AVIIentry));
            if (!idx->cluster[cl])
                return AVERROR(ENOMEM);
            idx->ents_allocated += AVI_INDEX_CLUSTER_SIZE;
        }

        idx->cluster[cl][id].flags = flags;
        idx->cluster[cl][id].pos   = avio_tell(pb) - avi->movi_list;
        idx->cluster[cl][id].len   = size;
        avist->max_size = FFMAX(avist->max_size, size);
        idx->entry++;
    }

    avio_write(pb, tag, 4);
    avio_wl32(pb, size);
    avio_write(pb, pkt->data, size);
    if (size & 1)
        avio_w8(pb, 0);

    return 0;
}

static int avi_write_trailer(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    int res = 0;
    int i, j, n, nb_frames;
    int64_t file_size;

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        write_skip_frames(s, i, avist->last_dts);
    }

    if (pb->seekable) {
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

            for (n = nb_frames = 0; n < s->nb_streams; n++) {
                AVCodecContext *stream = s->streams[n]->codec;
                AVIStream *avist       = s->streams[n]->priv_data;

                if (stream->codec_type == AVMEDIA_TYPE_VIDEO) {
                    if (nb_frames < avist->packet_count)
                        nb_frames = avist->packet_count;
                } else {
                    if (stream->codec_id == AV_CODEC_ID_MP2 ||
                        stream->codec_id == AV_CODEC_ID_MP3)
                        nb_frames += avist->packet_count;
                }
            }
            avio_wl32(pb, nb_frames);
            avio_seek(pb, file_size, SEEK_SET);

            avi_write_counters(s, avi->riff_id);
        }
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        for (j = 0; j < avist->indexes.ents_allocated / AVI_INDEX_CLUSTER_SIZE; j++)
            av_freep(&avist->indexes.cluster[j]);
        av_freep(&avist->indexes.cluster);
        avist->indexes.ents_allocated = avist->indexes.entry = 0;
        if (pb->seekable) {
            avio_seek(pb, avist->frames_hdr_strm + 4, SEEK_SET);
            avio_wl32(pb, avist->max_size);
        }
    }

    return res;
}

AVOutputFormat ff_avi_muxer = {
    .name           = "avi",
    .long_name      = NULL_IF_CONFIG_SMALL("AVI (Audio Video Interleaved)"),
    .mime_type      = "video/x-msvideo",
    .extensions     = "avi",
    .priv_data_size = sizeof(AVIContext),
    .audio_codec    = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_AC3,
    .video_codec    = AV_CODEC_ID_MPEG4,
    .write_header   = avi_write_header,
    .write_packet   = avi_write_packet,
    .write_trailer  = avi_write_trailer,
    .codec_tag      = (const AVCodecTag * const []) {
        ff_codec_bmp_tags, ff_codec_wav_tags, 0
    },
};
