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

#include <math.h>

#include "avformat.h"
#include "internal.h"
#include "avi.h"
#include "avio_internal.h"
#include "riff.h"
#include "mpegts.h"
#include "libavformat/avlanguage.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/internal.h"
#include "libavutil/dict.h"
#include "libavutil/avassert.h"
#include "libavutil/timestamp.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/raw.h"

/*
 * TODO:
 *  - fill all fields if non streamed (nb_frames for example)
 */

typedef struct AVIIentry {
    char tag[4];
    unsigned int flags;
    unsigned int pos;
    unsigned int len;
} AVIIentry;

#define AVI_INDEX_CLUSTER_SIZE 16384
#define AVI_MASTER_INDEX_PREFIX_SIZE    (8+2+1+1+4+8+4+4)
#define AVI_MASTER_INDEX_ENTRY_SIZE     16  /* bytes per entry */
#define AVI_MASTER_INDEX_SIZE_DEFAULT   256 /* number of entries */

typedef struct AVIIndex {
    int64_t     indx_start;
    int64_t     audio_strm_offset;
    int         entry;
    int         ents_allocated;
    int         master_odml_riff_id_base;
    AVIIentry** cluster;
} AVIIndex;

typedef struct AVIContext {
    const AVClass *class;
    int64_t riff_start, movi_list, odml_list;
    int64_t frames_hdr_all;
    int riff_id;
    int reserve_index_space;
    int master_index_max_size;
    int write_channel_mask;
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

    int64_t strh_flags_offset;

    uint32_t palette[AVPALETTE_COUNT];
    uint32_t old_palette[AVPALETTE_COUNT];
    int64_t pal_offset;
} AVIStream;

static int avi_write_packet_internal(AVFormatContext *s, AVPacket *pkt);

static inline AVIIentry *avi_get_ientry(const AVIIndex *idx, int ent_id)
{
    int cl = ent_id / AVI_INDEX_CLUSTER_SIZE;
    int id = ent_id % AVI_INDEX_CLUSTER_SIZE;
    return &idx->cluster[cl][id];
}

static int avi_add_ientry(AVFormatContext *s, int stream_index, char *tag,
                          unsigned int flags, unsigned int size)
{
    AVIContext *avi  = s->priv_data;
    AVIOContext *pb  = s->pb;
    AVIStream *avist = s->streams[stream_index]->priv_data;
    AVIIndex *idx    = &avist->indexes;
    int cl           = idx->entry / AVI_INDEX_CLUSTER_SIZE;
    int id           = idx->entry % AVI_INDEX_CLUSTER_SIZE;

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

    if (tag)
        memcpy(idx->cluster[cl][id].tag, tag, 4);
    else
        memset(idx->cluster[cl][id].tag, 0, 4);
    idx->cluster[cl][id].flags = flags;
    idx->cluster[cl][id].pos   = avio_tell(pb) - avi->movi_list;
    idx->cluster[cl][id].len   = size;
    avist->max_size = FFMAX(avist->max_size, size);
    idx->entry++;

    return 0;
}

static av_cold int avi_init(struct AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;

    if (avi->reserve_index_space > 0) {
        avi->master_index_max_size = (avi->reserve_index_space - AVI_MASTER_INDEX_PREFIX_SIZE) / AVI_MASTER_INDEX_ENTRY_SIZE;
        avi->master_index_max_size = FFMAX(avi->master_index_max_size, 16);
    } else
        avi->master_index_max_size = AVI_MASTER_INDEX_SIZE_DEFAULT;
    av_log(s, AV_LOG_DEBUG, "reserve_index_space:%d master_index_max_size:%d\n",
           avi->reserve_index_space, avi->master_index_max_size);

    return 1; /* stream initialization continues in avi_write_header */
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
    AVCodecParameters *par;

    file_size = avio_tell(pb);
    for (n = 0; n < s->nb_streams; n++) {
        AVIStream *avist = s->streams[n]->priv_data;

        av_assert0(avist->frames_hdr_strm);
        par = s->streams[n]->codecpar;
        avio_seek(pb, avist->frames_hdr_strm, SEEK_SET);
        ff_parse_specific_params(s->streams[n], &au_byterate, &au_ssize, &au_scale);
        if (au_ssize == 0)
            avio_wl32(pb, avist->packet_count);
        else
            avio_wl32(pb, avist->audio_strm_length / au_ssize);
        if (par->codec_type == AVMEDIA_TYPE_VIDEO)
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
    AVIContext *avi = s->priv_data;
    AVStream *st = s->streams[stream_index];
    AVCodecParameters *par = st->codecpar;
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
    ffio_wfourcc(pb, avi_stream2fourcc(tag, stream_index, par->codec_type));
                        /* dwChunkId */
    avio_wl64(pb, 0);   /* dwReserved[3] */
    avio_wl32(pb, 0);   /* Must be 0.    */
    for (j = 0; j < avi->master_index_max_size * 2; j++)
        avio_wl64(pb, 0);
    ff_end_tag(pb, avist->indexes.indx_start);
}

static int avi_write_header(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    AVIOContext *pb = s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    int64_t max_stream_duration = 0;
    AVCodecParameters *video_par;
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

    video_par = NULL;
    for (n = 0; n < s->nb_streams; n++) {
        AVCodecParameters *par = s->streams[n]->codecpar;
        AVStream *st = s->streams[n];
        bitrate = FFMIN(bitrate + par->bit_rate, INT32_MAX);
        if (st->duration > 0) {
            int64_t stream_duration = av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
            max_stream_duration = FFMAX(stream_duration, max_stream_duration);
        }
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_par = par;
            video_st = st;
        }
    }

    /* guess master index size based on bitrate and duration */
    if (!avi->reserve_index_space) {
        double duration_est, filesize_est;
        if (s->duration > 0)
            duration_est = (double)s->duration / AV_TIME_BASE;
        else if (max_stream_duration > 0)
            duration_est = (double)max_stream_duration / AV_TIME_BASE;
        else
            duration_est = 10 * 60 * 60; /* default to 10 hours */
        filesize_est = duration_est * (bitrate / 8) * 1.10; /* add 10% safety margin for muxer+bitrate */
        avi->master_index_max_size = FFMAX((int)ceil(filesize_est / AVI_MAX_RIFF_SIZE) + 1,
                                           avi->master_index_max_size);
        av_log(s, AV_LOG_DEBUG, "duration_est:%0.3f, filesize_est:%0.1fGiB, master_index_max_size:%d\n",
               duration_est, filesize_est / (1024*1024*1024), avi->master_index_max_size);
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
    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_ISINTERLEAVED);  /* flags */
    else
        avio_wl32(pb, AVIF_TRUSTCKTYPE | AVIF_HASINDEX | AVIF_ISINTERLEAVED);  /* flags */
    avi->frames_hdr_all = avio_tell(pb); /* remember this offset to fill later */
    avio_wl32(pb, nb_frames); /* nb frames, filled later */
    avio_wl32(pb, 0); /* initial frame */
    avio_wl32(pb, s->nb_streams); /* nb streams */
    avio_wl32(pb, 1024 * 1024); /* suggested buffer size */
    if (video_par) {
        avio_wl32(pb, video_par->width);
        avio_wl32(pb, video_par->height);
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
        AVCodecParameters *par = st->codecpar;
        AVIStream *avist = st->priv_data;
        list2 = ff_start_tag(pb, "LIST");
        ffio_wfourcc(pb, "strl");

        /* stream generic header */
        strh = ff_start_tag(pb, "strh");
        switch (par->codec_type) {
        case AVMEDIA_TYPE_SUBTITLE:
            // XSUB subtitles behave like video tracks, other subtitles
            // are not (yet) supported.
            if (par->codec_id != AV_CODEC_ID_XSUB) {
                avpriv_report_missing_feature(s, "Subtitle streams other than DivX XSUB");
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
        if (par->codec_type == AVMEDIA_TYPE_VIDEO ||
            par->codec_id == AV_CODEC_ID_XSUB)
            avio_wl32(pb, par->codec_tag);
        else
            avio_wl32(pb, 1);
        avist->strh_flags_offset = avio_tell(pb);
        avio_wl32(pb, 0); /* flags */
        avio_wl16(pb, 0); /* priority */
        avio_wl16(pb, 0); /* language */
        avio_wl32(pb, 0); /* initial frame */

        ff_parse_specific_params(st, &au_byterate, &au_ssize, &au_scale);

        if (   par->codec_type == AVMEDIA_TYPE_VIDEO
            && par->codec_id != AV_CODEC_ID_XSUB
            && au_byterate > 1000LL*au_scale) {
            au_byterate = 600;
            au_scale    = 1;
        }
        avpriv_set_pts_info(st, 64, au_scale, au_byterate);
        if (par->codec_id == AV_CODEC_ID_XSUB)
            au_scale = au_byterate = 0;

        avio_wl32(pb, au_scale); /* scale */
        avio_wl32(pb, au_byterate); /* rate */

        avio_wl32(pb, 0); /* start */
        /* remember this offset to fill later */
        avist->frames_hdr_strm = avio_tell(pb);
        if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
            /* FIXME: this may be broken, but who cares */
            avio_wl32(pb, AVI_MAX_RIFF_SIZE);
        else
            avio_wl32(pb, 0);  /* length, XXX: filled later */

        /* suggested buffer size, is set to largest chunk size in avi_write_trailer */
        if (par->codec_type == AVMEDIA_TYPE_VIDEO)
            avio_wl32(pb, 1024 * 1024);
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
            avio_wl32(pb, 12 * 1024);
        else
            avio_wl32(pb, 0);
        avio_wl32(pb, -1); /* quality */
        avio_wl32(pb, au_ssize); /* sample size */
        avio_wl32(pb, 0);
        avio_wl16(pb, par->width);
        avio_wl16(pb, par->height);
        ff_end_tag(pb, strh);

        if (par->codec_type != AVMEDIA_TYPE_DATA) {
            int ret, flags;
            enum AVPixelFormat pix_fmt;

            strf = ff_start_tag(pb, "strf");
            switch (par->codec_type) {
            case AVMEDIA_TYPE_SUBTITLE:
                /* XSUB subtitles behave like video tracks, other subtitles
                 * are not (yet) supported. */
                if (par->codec_id != AV_CODEC_ID_XSUB)
                    break;
            case AVMEDIA_TYPE_VIDEO:
                /* WMP expects RGB 5:5:5 rawvideo in avi to have bpp set to 16. */
                if (  !par->codec_tag
                    && par->codec_id == AV_CODEC_ID_RAWVIDEO
                    && par->format == AV_PIX_FMT_RGB555LE
                    && par->bits_per_coded_sample == 15)
                    par->bits_per_coded_sample = 16;
                avist->pal_offset = avio_tell(pb) + 40;
                ff_put_bmp_header(pb, par, 0, 0);
                pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                              par->bits_per_coded_sample);
                if (   !par->codec_tag
                    && par->codec_id == AV_CODEC_ID_RAWVIDEO
                    && par->format != pix_fmt
                    && par->format != AV_PIX_FMT_NONE)
                    av_log(s, AV_LOG_ERROR, "%s rawvideo cannot be written to avi, output file will be unreadable\n",
                          av_get_pix_fmt_name(par->format));
                break;
            case AVMEDIA_TYPE_AUDIO:
                flags = (avi->write_channel_mask == 0) ? FF_PUT_WAV_HEADER_SKIP_CHANNELMASK : 0;
                if ((ret = ff_put_wav_header(s, pb, par, flags)) < 0)
                    return ret;
                break;
            default:
                av_log(s, AV_LOG_ERROR,
                    "Invalid or not supported codec type '%s' found in the input\n",
                    (char *)av_x_if_null(av_get_media_type_string(par->codec_type), "?"));
                return AVERROR(EINVAL);
            }
            ff_end_tag(pb, strf);
            if ((t = av_dict_get(st->metadata, "title", NULL, 0))) {
                ff_riff_write_info_tag(s->pb, "strn", t->value);
                t = NULL;
            }
            if (par->codec_id == AV_CODEC_ID_XSUB
            && (t = av_dict_get(s->streams[i]->metadata, "language", NULL, 0))) {
                const char* langstr = ff_convert_lang_to(t->value, AV_LANG_ISO639_1);
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

        if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
            write_odml_master(s, i);
        }

        if (par->codec_type == AVMEDIA_TYPE_VIDEO   &&
            st->sample_aspect_ratio.num > 0 &&
            st->sample_aspect_ratio.den > 0) {
            int vprp       = ff_start_tag(pb, "vprp");
            AVRational dar = av_mul_q(st->sample_aspect_ratio,
                                      (AVRational) { par->width,
                                                     par->height });
            int num, den, fields, i;
            av_reduce(&num, &den, dar.num, dar.den, 0xFFFF);
            if (par->field_order == AV_FIELD_TT || par->field_order == AV_FIELD_BB ||
                par->field_order == AV_FIELD_TB || par->field_order == AV_FIELD_BT) {
                fields = 2; // interlaced
            } else {
                fields = 1; // progressive
            }

            avio_wl32(pb, 0); // video format   = unknown
            avio_wl32(pb, 0); // video standard = unknown
            // TODO: should be avg_frame_rate
            avio_wl32(pb, (2LL*st->time_base.den + st->time_base.num - 1) / (2LL * st->time_base.num));
            avio_wl32(pb, par->width);
            avio_wl32(pb, par->height);
            avio_wl16(pb, den);
            avio_wl16(pb, num);
            avio_wl32(pb, par->width);
            avio_wl32(pb, par->height);
            avio_wl32(pb, fields); // fields per frame

            for (i = 0; i < fields; i++) {
                int start_line;
                // OpenDML v1.02 is not very specific on what value to use for
                // start_line when frame data is not coming from a capturing device,
                // so just use 0/1 depending on the field order for interlaced frames
                if (par->field_order == AV_FIELD_TT || par->field_order == AV_FIELD_TB) {
                    start_line = (i == 0) ? 0 : 1;
                } else if (par->field_order == AV_FIELD_BB || par->field_order == AV_FIELD_BT) {
                    start_line = (i == 0) ? 1 : 0;
                } else {
                    start_line = 0;
                }

                avio_wl32(pb, par->height / fields); // compressed bitmap height
                avio_wl32(pb, par->width);           // compressed bitmap width
                avio_wl32(pb, par->height / fields); // valid bitmap height
                avio_wl32(pb, par->width);           // valid bitmap width
                avio_wl32(pb, 0);                    // valid bitmap X offset
                avio_wl32(pb, 0);                    // valid bitmap Y offset
                avio_wl32(pb, 0);                    // valid X offset in T
                avio_wl32(pb, start_line);           // valid Y start line
            }
            ff_end_tag(pb, vprp);
        }

        ff_end_tag(pb, list2);
    }

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
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

    return 0;
}

static void update_odml_entry(AVFormatContext *s, int stream_index, int64_t ix, int size)
{
    AVIOContext *pb = s->pb;
    AVIContext *avi = s->priv_data;
    AVIStream *avist = s->streams[stream_index]->priv_data;
    int64_t pos;
    int au_byterate, au_ssize, au_scale;

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
    if (s->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && au_ssize > 0) {
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

    av_assert0(pb->seekable & AVIO_SEEKABLE_NORMAL);

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        if (avi->riff_id - avist->indexes.master_odml_riff_id_base == avi->master_index_max_size) {
            int64_t pos;
            int size = AVI_MASTER_INDEX_PREFIX_SIZE + AVI_MASTER_INDEX_ENTRY_SIZE * avi->master_index_max_size;

            pos = avio_tell(pb);
            update_odml_entry(s, i, pos, size);
            write_odml_master(s, i);
            av_assert1(avio_tell(pb) - pos == size);
            avist->indexes.master_odml_riff_id_base = avi->riff_id - 1;
        }
        av_assert0(avi->riff_id - avist->indexes.master_odml_riff_id_base < avi->master_index_max_size);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        int64_t ix;

        avi_stream2fourcc(tag, i, s->streams[i]->codecpar->codec_type);
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

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
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
                if (*ie->tag)
                    ffio_wfourcc(pb, ie->tag);
                else {
                    avi_stream2fourcc(tag, stream_id,
                                  s->streams[stream_id]->codecpar->codec_type);
                    ffio_wfourcc(pb, tag);
                }
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
    AVCodecParameters *par = s->streams[stream_index]->codecpar;

    ff_dlog(s, "dts:%s packet_count:%d stream_index:%d\n", av_ts2str(dts), avist->packet_count, stream_index);
    while (par->block_align == 0 && dts != AV_NOPTS_VALUE &&
           dts > avist->packet_count && par->codec_id != AV_CODEC_ID_XSUB && avist->packet_count) {
        AVPacket empty_packet;

        if (dts - avist->packet_count > 60000) {
            av_log(s, AV_LOG_ERROR, "Too large number of skipped frames %"PRId64" > 60000\n", dts - avist->packet_count);
            return AVERROR(EINVAL);
        }

        av_init_packet(&empty_packet);
        empty_packet.size         = 0;
        empty_packet.data         = NULL;
        empty_packet.stream_index = stream_index;
        avi_write_packet_internal(s, &empty_packet);
        ff_dlog(s, "dup dts:%s packet_count:%d\n", av_ts2str(dts), avist->packet_count);
    }

    return 0;
}

static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    const int stream_index = pkt->stream_index;
    AVCodecParameters *par = s->streams[stream_index]->codecpar;
    int ret;

    if (par->codec_id == AV_CODEC_ID_H264 && par->codec_tag == MKTAG('H','2','6','4') && pkt->size) {
        ret = ff_check_h264_startcode(s, s->streams[stream_index], pkt);
        if (ret < 0)
            return ret;
    }

    if ((ret = write_skip_frames(s, stream_index, pkt->dts)) < 0)
        return ret;

    if (!pkt->size)
        return avi_write_packet_internal(s, pkt); /* Passthrough */

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        AVIStream *avist = s->streams[stream_index]->priv_data;
        AVIOContext *pb  = s->pb;
        AVPacket *opkt   = pkt;
        int reshuffle_ret;
        if (par->codec_id == AV_CODEC_ID_RAWVIDEO && par->codec_tag == 0) {
            int64_t bpc = par->bits_per_coded_sample != 15 ? par->bits_per_coded_sample : 16;
            int expected_stride = ((par->width * bpc + 31) >> 5)*4;
            reshuffle_ret = ff_reshuffle_raw_rgb(s, &pkt, par, expected_stride);
            if (reshuffle_ret < 0)
                return reshuffle_ret;
        } else
            reshuffle_ret = 0;
        if (par->format == AV_PIX_FMT_PAL8) {
            ret = ff_get_packet_palette(s, opkt, reshuffle_ret, avist->palette);
            if (ret < 0)
                goto fail;
            if (ret) {
                int pal_size = 1 << par->bits_per_coded_sample;
                int pc_tag, i;

                av_assert0(par->bits_per_coded_sample >= 0 && par->bits_per_coded_sample <= 8);

                if ((pb->seekable & AVIO_SEEKABLE_NORMAL) && avist->pal_offset) {
                    int64_t cur_offset = avio_tell(pb);
                    avio_seek(pb, avist->pal_offset, SEEK_SET);
                    for (i = 0; i < pal_size; i++) {
                        uint32_t v = avist->palette[i];
                        avio_wl32(pb, v & 0xffffff);
                    }
                    avio_seek(pb, cur_offset, SEEK_SET);
                    memcpy(avist->old_palette, avist->palette, pal_size * 4);
                    avist->pal_offset = 0;
                }
                if (memcmp(avist->palette, avist->old_palette, pal_size * 4)) {
                    unsigned char tag[5];
                    avi_stream2fourcc(tag, stream_index, par->codec_type);
                    tag[2] = 'p'; tag[3] = 'c';
                    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
                        if (avist->strh_flags_offset) {
                            int64_t cur_offset = avio_tell(pb);
                            avio_seek(pb, avist->strh_flags_offset, SEEK_SET);
                            avio_wl32(pb, AVISF_VIDEO_PALCHANGES);
                            avio_seek(pb, cur_offset, SEEK_SET);
                            avist->strh_flags_offset = 0;
                        }
                        ret = avi_add_ientry(s, stream_index, tag, AVIIF_NO_TIME,
                                       pal_size * 4 + 4);
                        if (ret < 0)
                            goto fail;
                    }
                    pc_tag = ff_start_tag(pb, tag);
                    avio_w8(pb, 0);
                    avio_w8(pb, pal_size & 0xFF);
                    avio_wl16(pb, 0); // reserved
                    for (i = 0; i < pal_size; i++) {
                        uint32_t v = avist->palette[i];
                        avio_wb32(pb, v<<8);
                    }
                    ff_end_tag(pb, pc_tag);
                    memcpy(avist->old_palette, avist->palette, pal_size * 4);
                }
            }
        }
        if (reshuffle_ret) {
            ret = avi_write_packet_internal(s, pkt);

fail:
            if (reshuffle_ret)
                av_packet_free(&pkt);
            return ret;
        }
    }

    return avi_write_packet_internal(s, pkt);
}

static int avi_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    unsigned char tag[5];
    unsigned int flags = 0;
    const int stream_index = pkt->stream_index;
    int size               = pkt->size;
    AVIContext *avi     = s->priv_data;
    AVIOContext *pb     = s->pb;
    AVIStream *avist    = s->streams[stream_index]->priv_data;
    AVCodecParameters *par = s->streams[stream_index]->codecpar;

    if (pkt->dts != AV_NOPTS_VALUE)
        avist->last_dts = pkt->dts + pkt->duration;

    avist->packet_count++;

    // Make sure to put an OpenDML chunk when the file size exceeds the limits
    if ((pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        (avio_tell(pb) - avi->riff_start > AVI_MAX_RIFF_SIZE)) {
        avi_write_ix(s);
        ff_end_tag(pb, avi->movi_list);

        if (avi->riff_id == 1)
            avi_write_idx1(s);

        ff_end_tag(pb, avi->riff_start);
        avi->movi_list = avi_start_new_riff(s, pb, "AVIX", "movi");
    }

    avi_stream2fourcc(tag, stream_index, par->codec_type);
    if (pkt->flags & AV_PKT_FLAG_KEY)
        flags = 0x10;
    if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        avist->audio_strm_length += size;

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        int ret;
        ret = avi_add_ientry(s, stream_index, NULL, flags, size);
        if (ret < 0)
            return ret;
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
    int i, n, nb_frames;
    int64_t file_size;

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        write_skip_frames(s, i, avist->last_dts);
    }

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
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
                AVCodecParameters *par = s->streams[n]->codecpar;
                AVIStream *avist       = s->streams[n]->priv_data;

                if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                    if (nb_frames < avist->packet_count)
                        nb_frames = avist->packet_count;
                } else {
                    if (par->codec_id == AV_CODEC_ID_MP2 ||
                        par->codec_id == AV_CODEC_ID_MP3)
                        nb_frames += avist->packet_count;
                }
            }
            avio_wl32(pb, nb_frames);
            avio_seek(pb, file_size, SEEK_SET);

            avi_write_counters(s, avi->riff_id);
        }
    }

    if (avi->riff_id >= avi->master_index_max_size) {
        int index_space = AVI_MASTER_INDEX_PREFIX_SIZE +
                          AVI_MASTER_INDEX_ENTRY_SIZE * avi->riff_id;
        av_log(s, AV_LOG_WARNING, "Output file not strictly OpenDML compliant, "
               "consider re-muxing with 'reserve_index_space' option value >= %d\n",
               index_space);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
            avio_seek(pb, avist->frames_hdr_strm + 4, SEEK_SET);
            avio_wl32(pb, avist->max_size);
        }
    }

    return res;
}

static void avi_deinit(AVFormatContext *s)
{
    for (int i = 0; i < s->nb_streams; i++) {
        AVIStream *avist = s->streams[i]->priv_data;
        if (!avist)
            continue;
        for (int j = 0; j < avist->indexes.ents_allocated / AVI_INDEX_CLUSTER_SIZE; j++)
            av_freep(&avist->indexes.cluster[j]);
        av_freep(&avist->indexes.cluster);
        avist->indexes.ents_allocated = avist->indexes.entry = 0;
    }
}

#define OFFSET(x) offsetof(AVIContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reserve_index_space", "reserve space (in bytes) at the beginning of the file for each stream index", OFFSET(reserve_index_space), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, ENC },
    { "write_channel_mask", "write channel mask into wave format header", OFFSET(write_channel_mask), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, ENC },
    { NULL },
};

static const AVClass avi_muxer_class = {
    .class_name = "AVI muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_avi_muxer = {
    .name           = "avi",
    .long_name      = NULL_IF_CONFIG_SMALL("AVI (Audio Video Interleaved)"),
    .mime_type      = "video/x-msvideo",
    .extensions     = "avi",
    .priv_data_size = sizeof(AVIContext),
    .audio_codec    = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_AC3,
    .video_codec    = AV_CODEC_ID_MPEG4,
    .init           = avi_init,
    .deinit         = avi_deinit,
    .write_header   = avi_write_header,
    .write_packet   = avi_write_packet,
    .write_trailer  = avi_write_trailer,
    .codec_tag      = (const AVCodecTag * const []) {
        ff_codec_bmp_tags, ff_codec_wav_tags, 0
    },
    .priv_class     = &avi_muxer_class,
};
