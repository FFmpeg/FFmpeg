/*
 * Matroska muxer
 * Copyright (c) 2007 David Conrad
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

#include "avc.h"
#include "avformat.h"
#include "avlanguage.h"
#include "flacenc.h"
#include "internal.h"
#include "isom.h"
#include "matroska.h"
#include "riff.h"
#include "wv.h"

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/samplefmt.h"
#include "libavutil/sha.h"

#include "libavcodec/xiph.h"
#include "libavcodec/mpeg4audio.h"

typedef struct ebml_master {
    int64_t         pos;                ///< absolute offset in the file where the master's elements start
    int             sizebytes;          ///< how many bytes were reserved for the size
} ebml_master;

typedef struct mkv_seekhead_entry {
    unsigned int    elementid;
    uint64_t        segmentpos;
} mkv_seekhead_entry;

typedef struct mkv_seekhead {
    int64_t                 filepos;
    int64_t                 segment_offset;     ///< the file offset to the beginning of the segment
    int                     reserved_size;      ///< -1 if appending to file
    int                     max_entries;
    mkv_seekhead_entry      *entries;
    int                     num_entries;
} mkv_seekhead;

typedef struct {
    uint64_t        pts;
    int             tracknum;
    int64_t         cluster_pos;        ///< file offset of the cluster containing the block
} mkv_cuepoint;

typedef struct {
    int64_t         segment_offset;
    mkv_cuepoint    *entries;
    int             num_entries;
} mkv_cues;

typedef struct {
    int             write_dts;
    int             has_cue;
} mkv_track;

#define MODE_MATROSKAv2 0x01
#define MODE_WEBM       0x02

typedef struct MatroskaMuxContext {
    const AVClass  *class;
    int             mode;
    AVIOContext   *dyn_bc;
    ebml_master     segment;
    int64_t         segment_offset;
    ebml_master     cluster;
    int64_t         cluster_pos;        ///< file offset of the current cluster
    int64_t         cluster_pts;
    int64_t         duration_offset;
    int64_t         duration;
    mkv_seekhead    *main_seekhead;
    mkv_cues        *cues;
    mkv_track       *tracks;

    AVPacket        cur_audio_pkt;

    int have_attachments;

    int reserve_cues_space;
    int64_t cues_pos;
} MatroskaMuxContext;


/** 2 bytes * 3 for EBML IDs, 3 1-byte EBML lengths, 8 bytes for 64 bit
 * offset, 4 bytes for target EBML ID */
#define MAX_SEEKENTRY_SIZE 21

/** per-cuepoint-track - 3 1-byte EBML IDs, 3 1-byte EBML sizes, 2
 * 8-byte uint max */
#define MAX_CUETRACKPOS_SIZE 22

/** per-cuepoint - 2 1-byte EBML IDs, 2 1-byte EBML sizes, 8-byte uint max */
#define MAX_CUEPOINT_SIZE(num_tracks) 12 + MAX_CUETRACKPOS_SIZE*num_tracks


static int ebml_id_size(unsigned int id)
{
    return (av_log2(id+1)-1)/7+1;
}

static void put_ebml_id(AVIOContext *pb, unsigned int id)
{
    int i = ebml_id_size(id);
    while (i--)
        avio_w8(pb, (uint8_t)(id >> (i*8)));
}

/**
 * Write an EBML size meaning "unknown size".
 *
 * @param bytes The number of bytes the size should occupy (maximum: 8).
 */
static void put_ebml_size_unknown(AVIOContext *pb, int bytes)
{
    av_assert0(bytes <= 8);
    avio_w8(pb, 0x1ff >> bytes);
    while (--bytes)
        avio_w8(pb, 0xff);
}

/**
 * Calculate how many bytes are needed to represent a given number in EBML.
 */
static int ebml_num_size(uint64_t num)
{
    int bytes = 1;
    while ((num+1) >> bytes*7) bytes++;
    return bytes;
}

/**
 * Write a number in EBML variable length format.
 *
 * @param bytes The number of bytes that need to be used to write the number.
 *              If zero, any number of bytes can be used.
 */
static void put_ebml_num(AVIOContext *pb, uint64_t num, int bytes)
{
    int i, needed_bytes = ebml_num_size(num);

    // sizes larger than this are currently undefined in EBML
    av_assert0(num < (1ULL<<56)-1);

    if (bytes == 0)
        // don't care how many bytes are used, so use the min
        bytes = needed_bytes;
    // the bytes needed to write the given size would exceed the bytes
    // that we need to use, so write unknown size. This shouldn't happen.
    av_assert0(bytes >= needed_bytes);

    num |= 1ULL << bytes*7;
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(num >> i*8));
}

static void put_ebml_uint(AVIOContext *pb, unsigned int elementid, uint64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = val;
    while (tmp>>=8) bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_num(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(val >> i*8));
}

static void put_ebml_float(AVIOContext *pb, unsigned int elementid, double val)
{
    put_ebml_id(pb, elementid);
    put_ebml_num(pb, 8, 0);
    avio_wb64(pb, av_double2int(val));
}

static void put_ebml_binary(AVIOContext *pb, unsigned int elementid,
                            const void *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_num(pb, size, 0);
    avio_write(pb, buf, size);
}

static void put_ebml_string(AVIOContext *pb, unsigned int elementid, const char *str)
{
    put_ebml_binary(pb, elementid, str, strlen(str));
}

/**
 * Write a void element of a given size. Useful for reserving space in
 * the file to be written to later.
 *
 * @param size The number of bytes to reserve, which must be at least 2.
 */
static void put_ebml_void(AVIOContext *pb, uint64_t size)
{
    int64_t currentpos = avio_tell(pb);

    av_assert0(size >= 2);

    put_ebml_id(pb, EBML_ID_VOID);
    // we need to subtract the length needed to store the size from the
    // size we need to reserve so 2 cases, we use 8 bytes to store the
    // size if possible, 1 byte otherwise
    if (size < 10)
        put_ebml_num(pb, size-1, 0);
    else
        put_ebml_num(pb, size-9, 8);
    while(avio_tell(pb) < currentpos + size)
        avio_w8(pb, 0);
}

static ebml_master start_ebml_master(AVIOContext *pb, unsigned int elementid, uint64_t expectedsize)
{
    int bytes = expectedsize ? ebml_num_size(expectedsize) : 8;
    put_ebml_id(pb, elementid);
    put_ebml_size_unknown(pb, bytes);
    return (ebml_master){ avio_tell(pb), bytes };
}

static void end_ebml_master(AVIOContext *pb, ebml_master master)
{
    int64_t pos = avio_tell(pb);

    if (avio_seek(pb, master.pos - master.sizebytes, SEEK_SET) < 0)
        return;
    put_ebml_num(pb, pos - master.pos, master.sizebytes);
    avio_seek(pb, pos, SEEK_SET);
}

static void put_xiph_size(AVIOContext *pb, int size)
{
    int i;
    for (i = 0; i < size / 255; i++)
        avio_w8(pb, 255);
    avio_w8(pb, size % 255);
}

/**
 * Initialize a mkv_seekhead element to be ready to index level 1 Matroska
 * elements. If a maximum number of elements is specified, enough space
 * will be reserved at the current file location to write a seek head of
 * that size.
 *
 * @param segment_offset The absolute offset to the position in the file
 *                       where the segment begins.
 * @param numelements The maximum number of elements that will be indexed
 *                    by this seek head, 0 if unlimited.
 */
static mkv_seekhead * mkv_start_seekhead(AVIOContext *pb, int64_t segment_offset, int numelements)
{
    mkv_seekhead *new_seekhead = av_mallocz(sizeof(mkv_seekhead));
    if (new_seekhead == NULL)
        return NULL;

    new_seekhead->segment_offset = segment_offset;

    if (numelements > 0) {
        new_seekhead->filepos = avio_tell(pb);
        // 21 bytes max for a seek entry, 10 bytes max for the SeekHead ID
        // and size, and 3 bytes to guarantee that an EBML void element
        // will fit afterwards
        new_seekhead->reserved_size = numelements * MAX_SEEKENTRY_SIZE + 13;
        new_seekhead->max_entries = numelements;
        put_ebml_void(pb, new_seekhead->reserved_size);
    }
    return new_seekhead;
}

static int mkv_add_seekhead_entry(mkv_seekhead *seekhead, unsigned int elementid, uint64_t filepos)
{
    mkv_seekhead_entry *entries = seekhead->entries;

    // don't store more elements than we reserved space for
    if (seekhead->max_entries > 0 && seekhead->max_entries <= seekhead->num_entries)
        return -1;

    entries = av_realloc(entries, (seekhead->num_entries + 1) * sizeof(mkv_seekhead_entry));
    if (entries == NULL)
        return AVERROR(ENOMEM);

    entries[seekhead->num_entries  ].elementid = elementid;
    entries[seekhead->num_entries++].segmentpos = filepos - seekhead->segment_offset;

    seekhead->entries = entries;
    return 0;
}

/**
 * Write the seek head to the file and free it. If a maximum number of
 * elements was specified to mkv_start_seekhead(), the seek head will
 * be written at the location reserved for it. Otherwise, it is written
 * at the current location in the file.
 *
 * @return The file offset where the seekhead was written,
 * -1 if an error occurred.
 */
static int64_t mkv_write_seekhead(AVIOContext *pb, mkv_seekhead *seekhead)
{
    ebml_master metaseek, seekentry;
    int64_t currentpos;
    int i;

    currentpos = avio_tell(pb);

    if (seekhead->reserved_size > 0) {
        if (avio_seek(pb, seekhead->filepos, SEEK_SET) < 0) {
            currentpos = -1;
            goto fail;
        }
    }

    metaseek = start_ebml_master(pb, MATROSKA_ID_SEEKHEAD, seekhead->reserved_size);
    for (i = 0; i < seekhead->num_entries; i++) {
        mkv_seekhead_entry *entry = &seekhead->entries[i];

        seekentry = start_ebml_master(pb, MATROSKA_ID_SEEKENTRY, MAX_SEEKENTRY_SIZE);

        put_ebml_id(pb, MATROSKA_ID_SEEKID);
        put_ebml_num(pb, ebml_id_size(entry->elementid), 0);
        put_ebml_id(pb, entry->elementid);

        put_ebml_uint(pb, MATROSKA_ID_SEEKPOSITION, entry->segmentpos);
        end_ebml_master(pb, seekentry);
    }
    end_ebml_master(pb, metaseek);

    if (seekhead->reserved_size > 0) {
        uint64_t remaining = seekhead->filepos + seekhead->reserved_size - avio_tell(pb);
        put_ebml_void(pb, remaining);
        avio_seek(pb, currentpos, SEEK_SET);

        currentpos = seekhead->filepos;
    }
fail:
    av_free(seekhead->entries);
    av_free(seekhead);

    return currentpos;
}

static mkv_cues * mkv_start_cues(int64_t segment_offset)
{
    mkv_cues *cues = av_mallocz(sizeof(mkv_cues));
    if (cues == NULL)
        return NULL;

    cues->segment_offset = segment_offset;
    return cues;
}

static int mkv_add_cuepoint(mkv_cues *cues, int stream, int64_t ts, int64_t cluster_pos)
{
    mkv_cuepoint *entries = cues->entries;

    if (ts < 0)
        return 0;

    entries = av_realloc(entries, (cues->num_entries + 1) * sizeof(mkv_cuepoint));
    if (entries == NULL)
        return AVERROR(ENOMEM);

    entries[cues->num_entries  ].pts = ts;
    entries[cues->num_entries  ].tracknum = stream + 1;
    entries[cues->num_entries++].cluster_pos = cluster_pos - cues->segment_offset;

    cues->entries = entries;
    return 0;
}

static int64_t mkv_write_cues(AVIOContext *pb, mkv_cues *cues, mkv_track *tracks, int num_tracks)
{
    ebml_master cues_element;
    int64_t currentpos;
    int i, j;

    currentpos = avio_tell(pb);
    cues_element = start_ebml_master(pb, MATROSKA_ID_CUES, 0);

    for (i = 0; i < cues->num_entries; i++) {
        ebml_master cuepoint, track_positions;
        mkv_cuepoint *entry = &cues->entries[i];
        uint64_t pts = entry->pts;

        cuepoint = start_ebml_master(pb, MATROSKA_ID_POINTENTRY, MAX_CUEPOINT_SIZE(num_tracks));
        put_ebml_uint(pb, MATROSKA_ID_CUETIME, pts);

        // put all the entries from different tracks that have the exact same
        // timestamp into the same CuePoint
        for (j = 0; j < num_tracks; j++)
            tracks[j].has_cue = 0;
        for (j = 0; j < cues->num_entries - i && entry[j].pts == pts; j++) {
            int tracknum = entry[j].tracknum - 1;
            av_assert0(tracknum>=0 && tracknum<num_tracks);
            if (tracks[tracknum].has_cue)
                continue;
            tracks[tracknum].has_cue = 1;
            track_positions = start_ebml_master(pb, MATROSKA_ID_CUETRACKPOSITION, MAX_CUETRACKPOS_SIZE);
            put_ebml_uint(pb, MATROSKA_ID_CUETRACK          , entry[j].tracknum   );
            put_ebml_uint(pb, MATROSKA_ID_CUECLUSTERPOSITION, entry[j].cluster_pos);
            end_ebml_master(pb, track_positions);
        }
        i += j - 1;
        end_ebml_master(pb, cuepoint);
    }
    end_ebml_master(pb, cues_element);

    return currentpos;
}

static int put_xiph_codecpriv(AVFormatContext *s, AVIOContext *pb, AVCodecContext *codec)
{
    uint8_t *header_start[3];
    int header_len[3];
    int first_header_size;
    int j;

    if (codec->codec_id == AV_CODEC_ID_VORBIS)
        first_header_size = 30;
    else
        first_header_size = 42;

    if (avpriv_split_xiph_headers(codec->extradata, codec->extradata_size,
                              first_header_size, header_start, header_len) < 0) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    avio_w8(pb, 2);                    // number packets - 1
    for (j = 0; j < 2; j++) {
        put_xiph_size(pb, header_len[j]);
    }
    for (j = 0; j < 3; j++)
        avio_write(pb, header_start[j], header_len[j]);

    return 0;
}

static int put_wv_codecpriv(AVIOContext *pb, AVCodecContext *codec)
{
    if (codec->extradata && codec->extradata_size == 2)
        avio_write(pb, codec->extradata, 2);
    else
        avio_wl16(pb, 0x403); // fallback to the version mentioned in matroska specs
    return 0;
}

static void get_aac_sample_rates(AVFormatContext *s, AVCodecContext *codec, int *sample_rate, int *output_sample_rate)
{
    MPEG4AudioConfig mp4ac;

    if (avpriv_mpeg4audio_get_config(&mp4ac, codec->extradata,
                                     codec->extradata_size * 8, 1) < 0) {
        av_log(s, AV_LOG_WARNING, "Error parsing AAC extradata, unable to determine samplerate.\n");
        return;
    }

    *sample_rate        = mp4ac.sample_rate;
    *output_sample_rate = mp4ac.ext_sample_rate;
}

static int mkv_write_codecprivate(AVFormatContext *s, AVIOContext *pb, AVCodecContext *codec, int native_id, int qt_id)
{
    AVIOContext *dyn_cp;
    uint8_t *codecpriv;
    int ret, codecpriv_size;

    ret = avio_open_dyn_buf(&dyn_cp);
    if(ret < 0)
        return ret;

    if (native_id) {
        if (codec->codec_id == AV_CODEC_ID_VORBIS || codec->codec_id == AV_CODEC_ID_THEORA)
            ret = put_xiph_codecpriv(s, dyn_cp, codec);
        else if (codec->codec_id == AV_CODEC_ID_FLAC)
            ret = ff_flac_write_header(dyn_cp, codec, 1);
        else if (codec->codec_id == AV_CODEC_ID_WAVPACK)
            ret = put_wv_codecpriv(dyn_cp, codec);
        else if (codec->codec_id == AV_CODEC_ID_H264)
            ret = ff_isom_write_avcc(dyn_cp, codec->extradata, codec->extradata_size);
        else if (codec->codec_id == AV_CODEC_ID_ALAC) {
            if (codec->extradata_size < 36) {
                av_log(s, AV_LOG_ERROR,
                       "Invalid extradata found, ALAC expects a 36-byte "
                       "QuickTime atom.");
                ret = AVERROR_INVALIDDATA;
            } else
                avio_write(dyn_cp, codec->extradata + 12,
                                   codec->extradata_size - 12);
        }
        else if (codec->extradata_size && codec->codec_id != AV_CODEC_ID_TTA)
            avio_write(dyn_cp, codec->extradata, codec->extradata_size);
    } else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (qt_id) {
            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(ff_codec_movvideo_tags, codec->codec_id);
            if (codec->extradata_size)
                avio_write(dyn_cp, codec->extradata, codec->extradata_size);
        } else {
            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(ff_codec_bmp_tags, codec->codec_id);
            if (!codec->codec_tag) {
                av_log(s, AV_LOG_ERROR, "No bmp codec tag found for codec %s\n",
                       avcodec_get_name(codec->codec_id));
                ret = AVERROR(EINVAL);
            }

            ff_put_bmp_header(dyn_cp, codec, ff_codec_bmp_tags, 0);
        }

    } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        unsigned int tag;
        tag = ff_codec_get_tag(ff_codec_wav_tags, codec->codec_id);
        if (!tag) {
            av_log(s, AV_LOG_ERROR, "No wav codec tag found for codec %s\n",
                   avcodec_get_name(codec->codec_id));
            ret = AVERROR(EINVAL);
        }
        if (!codec->codec_tag)
            codec->codec_tag = tag;

        ff_put_wav_header(dyn_cp, codec);
    }

    codecpriv_size = avio_close_dyn_buf(dyn_cp, &codecpriv);
    if (codecpriv_size)
        put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codecpriv, codecpriv_size);
    av_free(codecpriv);
    return ret;
}

static int mkv_write_tracks(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master tracks;
    int i, j, ret, default_stream_exists = 0;

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TRACKS, avio_tell(pb));
    if (ret < 0) return ret;

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS, 0);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        default_stream_exists |= st->disposition & AV_DISPOSITION_DEFAULT;
    }
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecContext *codec = st->codec;
        ebml_master subinfo, track;
        int native_id = 0;
        int qt_id = 0;
        int bit_depth = av_get_bits_per_sample(codec->codec_id);
        int sample_rate = codec->sample_rate;
        int output_sample_rate = 0;
        AVDictionaryEntry *tag;

        if (codec->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
            mkv->have_attachments = 1;
            continue;
        }

        if (!bit_depth)
            bit_depth = av_get_bytes_per_sample(codec->sample_fmt) << 3;
        if (!bit_depth)
            bit_depth = codec->bits_per_coded_sample;

        if (codec->codec_id == AV_CODEC_ID_AAC)
            get_aac_sample_rates(s, codec, &sample_rate, &output_sample_rate);

        track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY, 0);
        put_ebml_uint (pb, MATROSKA_ID_TRACKNUMBER     , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKUID        , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKFLAGLACING , 0);    // no lacing (yet)

        if ((tag = av_dict_get(st->metadata, "title", NULL, 0)))
            put_ebml_string(pb, MATROSKA_ID_TRACKNAME, tag->value);
        tag = av_dict_get(st->metadata, "language", NULL, 0);
        if (mkv->mode != MODE_WEBM || codec->codec_id != AV_CODEC_ID_WEBVTT) {
            put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, tag ? tag->value:"und");
        } else if (tag && tag->value) {
            put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, tag->value);
        }

        if (default_stream_exists) {
            put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGDEFAULT, !!(st->disposition & AV_DISPOSITION_DEFAULT));
        }
        if (st->disposition & AV_DISPOSITION_FORCED)
            put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGFORCED, 1);

        if (mkv->mode == MODE_WEBM && codec->codec_id == AV_CODEC_ID_WEBVTT) {
            const char *codec_id;
            if (st->disposition & AV_DISPOSITION_CAPTIONS) {
                codec_id = "D_WEBVTT/CAPTIONS";
                native_id = MATROSKA_TRACK_TYPE_SUBTITLE;
            } else if (st->disposition & AV_DISPOSITION_DESCRIPTIONS) {
                codec_id = "D_WEBVTT/DESCRIPTIONS";
                native_id = MATROSKA_TRACK_TYPE_METADATA;
            } else if (st->disposition & AV_DISPOSITION_METADATA) {
                codec_id = "D_WEBVTT/METADATA";
                native_id = MATROSKA_TRACK_TYPE_METADATA;
            } else {
                codec_id = "D_WEBVTT/SUBTITLES";
                native_id = MATROSKA_TRACK_TYPE_SUBTITLE;
            }
            put_ebml_string(pb, MATROSKA_ID_CODECID, codec_id);
        } else {
            // look for a codec ID string specific to mkv to use,
            // if none are found, use AVI codes
            for (j = 0; ff_mkv_codec_tags[j].id != AV_CODEC_ID_NONE; j++) {
                if (ff_mkv_codec_tags[j].id == codec->codec_id) {
                    put_ebml_string(pb, MATROSKA_ID_CODECID, ff_mkv_codec_tags[j].str);
                    native_id = 1;
                    break;
                }
            }
        }

        if (mkv->mode == MODE_WEBM && !(codec->codec_id == AV_CODEC_ID_VP8 ||
                                        codec->codec_id == AV_CODEC_ID_VP9 ||
                                      ((codec->codec_id == AV_CODEC_ID_OPUS)&&(codec->strict_std_compliance <= FF_COMPLIANCE_EXPERIMENTAL)) ||
                                        codec->codec_id == AV_CODEC_ID_VORBIS ||
                                        codec->codec_id == AV_CODEC_ID_WEBVTT)) {
            av_log(s, AV_LOG_ERROR,
                   "Only VP8,VP9 video and Vorbis,Opus(experimental, use -strict -2) audio and WebVTT subtitles are supported for WebM.\n");
            return AVERROR(EINVAL);
        }

        switch (codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);
                if(st->avg_frame_rate.num && st->avg_frame_rate.den && 1.0/av_q2d(st->avg_frame_rate) > av_q2d(codec->time_base))
                    put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, 1E9/av_q2d(st->avg_frame_rate));
                else
                    put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, av_q2d(codec->time_base)*1E9);

                if (!native_id &&
                      ff_codec_get_tag(ff_codec_movvideo_tags, codec->codec_id) &&
                    (!ff_codec_get_tag(ff_codec_bmp_tags,   codec->codec_id)
                     || codec->codec_id == AV_CODEC_ID_SVQ1
                     || codec->codec_id == AV_CODEC_ID_SVQ3
                     || codec->codec_id == AV_CODEC_ID_CINEPAK))
                    qt_id = 1;

                if (qt_id)
                    put_ebml_string(pb, MATROSKA_ID_CODECID, "V_QUICKTIME");
                else if (!native_id) {
                    // if there is no mkv-specific codec ID, use VFW mode
                    put_ebml_string(pb, MATROSKA_ID_CODECID, "V_MS/VFW/FOURCC");
                    mkv->tracks[i].write_dts = 1;
                }

                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKVIDEO, 0);
                // XXX: interlace flag?
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELWIDTH , codec->width);
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELHEIGHT, codec->height);

                if ((tag = av_dict_get(st->metadata, "stereo_mode", NULL, 0)) ||
                    (tag = av_dict_get( s->metadata, "stereo_mode", NULL, 0))) {
                    // save stereo mode flag
                    uint64_t st_mode = MATROSKA_VIDEO_STEREO_MODE_COUNT;

                    for (j=0; j<MATROSKA_VIDEO_STEREO_MODE_COUNT; j++)
                        if (!strcmp(tag->value, ff_matroska_video_stereo_mode[j])){
                            st_mode = j;
                            break;
                        }

                    if ((mkv->mode == MODE_WEBM && st_mode > 3 && st_mode != 11)
                        || st_mode >= MATROSKA_VIDEO_STEREO_MODE_COUNT) {
                        av_log(s, AV_LOG_ERROR,
                               "The specified stereo mode is not valid.\n");
                        return AVERROR(EINVAL);
                    } else
                        put_ebml_uint(pb, MATROSKA_ID_VIDEOSTEREOMODE, st_mode);
                }

                if ((tag = av_dict_get(st->metadata, "alpha_mode", NULL, 0)) ||
                    (tag = av_dict_get( s->metadata, "alpha_mode", NULL, 0)) ||
                    (codec->pix_fmt == AV_PIX_FMT_YUVA420P)) {
                    put_ebml_uint(pb, MATROSKA_ID_VIDEOALPHAMODE, 1);
                }

                if (st->sample_aspect_ratio.num) {
                    int64_t d_width = av_rescale(codec->width, st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
                    if (d_width > INT_MAX) {
                        av_log(s, AV_LOG_ERROR, "Overflow in display width\n");
                        return AVERROR(EINVAL);
                    }
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , d_width);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, codec->height);
                }

                if (codec->codec_id == AV_CODEC_ID_RAWVIDEO) {
                    uint32_t color_space = av_le2ne32(codec->codec_tag);
                    put_ebml_binary(pb, MATROSKA_ID_VIDEOCOLORSPACE, &color_space, sizeof(color_space));
                }
                end_ebml_master(pb, subinfo);
                break;

            case AVMEDIA_TYPE_AUDIO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_AUDIO);

                if (!native_id)
                    // no mkv-specific ID, use ACM mode
                    put_ebml_string(pb, MATROSKA_ID_CODECID, "A_MS/ACM");

                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKAUDIO, 0);
                put_ebml_uint  (pb, MATROSKA_ID_AUDIOCHANNELS    , codec->channels);
                put_ebml_float (pb, MATROSKA_ID_AUDIOSAMPLINGFREQ, sample_rate);
                if (output_sample_rate)
                    put_ebml_float(pb, MATROSKA_ID_AUDIOOUTSAMPLINGFREQ, output_sample_rate);
                if (bit_depth)
                    put_ebml_uint(pb, MATROSKA_ID_AUDIOBITDEPTH, bit_depth);
                end_ebml_master(pb, subinfo);
                break;

            case AVMEDIA_TYPE_SUBTITLE:
                if (!native_id) {
                    av_log(s, AV_LOG_ERROR, "Subtitle codec %d is not supported.\n", codec->codec_id);
                    return AVERROR(ENOSYS);
                }

                if (mkv->mode != MODE_WEBM || codec->codec_id != AV_CODEC_ID_WEBVTT)
                    native_id = MATROSKA_TRACK_TYPE_SUBTITLE;

                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, native_id);
                break;
            default:
                av_log(s, AV_LOG_ERROR, "Only audio, video, and subtitles are supported for Matroska.\n");
                return AVERROR(EINVAL);
        }

        if (mkv->mode != MODE_WEBM || codec->codec_id != AV_CODEC_ID_WEBVTT) {
            ret = mkv_write_codecprivate(s, pb, codec, native_id, qt_id);
            if (ret < 0) return ret;
        }

        end_ebml_master(pb, track);

        // ms precision is the de-facto standard timescale for mkv files
        avpriv_set_pts_info(st, 64, 1, 1000);
    }
    end_ebml_master(pb, tracks);
    return 0;
}

static int mkv_write_chapters(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master chapters, editionentry;
    AVRational scale = {1, 1E9};
    int i, ret;

    if (!s->nb_chapters)
        return 0;

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CHAPTERS, avio_tell(pb));
    if (ret < 0) return ret;

    chapters     = start_ebml_master(pb, MATROSKA_ID_CHAPTERS    , 0);
    editionentry = start_ebml_master(pb, MATROSKA_ID_EDITIONENTRY, 0);
    put_ebml_uint(pb, MATROSKA_ID_EDITIONFLAGDEFAULT, 1);
    put_ebml_uint(pb, MATROSKA_ID_EDITIONFLAGHIDDEN , 0);
    for (i = 0; i < s->nb_chapters; i++) {
        ebml_master chapteratom, chapterdisplay;
        AVChapter *c     = s->chapters[i];
        AVDictionaryEntry *t = NULL;

        chapteratom = start_ebml_master(pb, MATROSKA_ID_CHAPTERATOM, 0);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERUID, c->id);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMESTART,
                      av_rescale_q(c->start, c->time_base, scale));
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMEEND,
                      av_rescale_q(c->end,   c->time_base, scale));
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERFLAGHIDDEN , 0);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERFLAGENABLED, 1);
        if ((t = av_dict_get(c->metadata, "title", NULL, 0))) {
            chapterdisplay = start_ebml_master(pb, MATROSKA_ID_CHAPTERDISPLAY, 0);
            put_ebml_string(pb, MATROSKA_ID_CHAPSTRING, t->value);
            put_ebml_string(pb, MATROSKA_ID_CHAPLANG  , "und");
            end_ebml_master(pb, chapterdisplay);
        }
        end_ebml_master(pb, chapteratom);
    }
    end_ebml_master(pb, editionentry);
    end_ebml_master(pb, chapters);
    return 0;
}

static void mkv_write_simpletag(AVIOContext *pb, AVDictionaryEntry *t)
{
    uint8_t *key = av_strdup(t->key);
    uint8_t *p   = key;
    const uint8_t *lang = NULL;
    ebml_master tag;

    if ((p = strrchr(p, '-')) &&
        (lang = av_convert_lang_to(p + 1, AV_LANG_ISO639_2_BIBL)))
        *p = 0;

    p = key;
    while (*p) {
        if (*p == ' ')
            *p = '_';
        else if (*p >= 'a' && *p <= 'z')
            *p -= 'a' - 'A';
        p++;
    }

    tag = start_ebml_master(pb, MATROSKA_ID_SIMPLETAG, 0);
    put_ebml_string(pb, MATROSKA_ID_TAGNAME, key);
    if (lang)
        put_ebml_string(pb, MATROSKA_ID_TAGLANG, lang);
    put_ebml_string(pb, MATROSKA_ID_TAGSTRING, t->value);
    end_ebml_master(pb, tag);

    av_freep(&key);
}

static int mkv_write_tag(AVFormatContext *s, AVDictionary *m, unsigned int elementid,
                         unsigned int uid, ebml_master *tags)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ebml_master tag, targets;
    AVDictionaryEntry *t = NULL;
    int ret;

    if (!tags->pos) {
        ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TAGS, avio_tell(s->pb));
        if (ret < 0) return ret;

        *tags = start_ebml_master(s->pb, MATROSKA_ID_TAGS, 0);
    }

    tag     = start_ebml_master(s->pb, MATROSKA_ID_TAG,        0);
    targets = start_ebml_master(s->pb, MATROSKA_ID_TAGTARGETS, 0);
    if (elementid)
        put_ebml_uint(s->pb, elementid, uid);
    end_ebml_master(s->pb, targets);

    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX)))
        if (av_strcasecmp(t->key, "title") && av_strcasecmp(t->key, "stereo_mode"))
            mkv_write_simpletag(s->pb, t);

    end_ebml_master(s->pb, tag);
    return 0;
}

static int mkv_write_tags(AVFormatContext *s)
{
    ebml_master tags = {0};
    int i, ret;

    ff_metadata_conv_ctx(s, ff_mkv_metadata_conv, NULL);

    if (av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX)) {
        ret = mkv_write_tag(s, s->metadata, 0, 0, &tags);
        if (ret < 0) return ret;
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        if (!av_dict_get(st->metadata, "", 0, AV_DICT_IGNORE_SUFFIX))
            continue;

        ret = mkv_write_tag(s, st->metadata, MATROSKA_ID_TAGTARGETS_TRACKUID, i + 1, &tags);
        if (ret < 0) return ret;
    }

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *ch = s->chapters[i];

        if (!av_dict_get(ch->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
            continue;

        ret = mkv_write_tag(s, ch->metadata, MATROSKA_ID_TAGTARGETS_CHAPTERUID, ch->id, &tags);
        if (ret < 0) return ret;
    }

    if (tags.pos)
        end_ebml_master(s->pb, tags);
    return 0;
}

static int mkv_write_attachments(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master attachments;
    AVLFG c;
    int i, ret;

    if (!mkv->have_attachments)
        return 0;

    av_lfg_init(&c, av_get_random_seed());

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_ATTACHMENTS, avio_tell(pb));
    if (ret < 0) return ret;

    attachments = start_ebml_master(pb, MATROSKA_ID_ATTACHMENTS, 0);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        ebml_master attached_file;
        AVDictionaryEntry *t;
        const char *mimetype = NULL;
        uint64_t fileuid;

        if (st->codec->codec_type != AVMEDIA_TYPE_ATTACHMENT)
            continue;

        attached_file = start_ebml_master(pb, MATROSKA_ID_ATTACHEDFILE, 0);

        if (t = av_dict_get(st->metadata, "title", NULL, 0))
            put_ebml_string(pb, MATROSKA_ID_FILEDESC, t->value);
        if (!(t = av_dict_get(st->metadata, "filename", NULL, 0))) {
            av_log(s, AV_LOG_ERROR, "Attachment stream %d has no filename tag.\n", i);
            return AVERROR(EINVAL);
        }
        put_ebml_string(pb, MATROSKA_ID_FILENAME, t->value);
        if (t = av_dict_get(st->metadata, "mimetype", NULL, 0))
            mimetype = t->value;
        else if (st->codec->codec_id != AV_CODEC_ID_NONE ) {
            int i;
            for (i = 0; ff_mkv_mime_tags[i].id != AV_CODEC_ID_NONE; i++)
                if (ff_mkv_mime_tags[i].id == st->codec->codec_id) {
                    mimetype = ff_mkv_mime_tags[i].str;
                    break;
                }
        }
        if (!mimetype) {
            av_log(s, AV_LOG_ERROR, "Attachment stream %d has no mimetype tag and "
                                    "it cannot be deduced from the codec id.\n", i);
            return AVERROR(EINVAL);
        }

        if (st->codec->flags & CODEC_FLAG_BITEXACT) {
            struct AVSHA *sha = av_sha_alloc();
            uint8_t digest[20];
            if (!sha)
                return AVERROR(ENOMEM);
            av_sha_init(sha, 160);
            av_sha_update(sha, st->codec->extradata, st->codec->extradata_size);
            av_sha_final(sha, digest);
            av_free(sha);
            fileuid = AV_RL64(digest);
        } else {
            fileuid = av_lfg_get(&c);
        }
        av_log(s, AV_LOG_VERBOSE, "Using %.16"PRIx64" for attachment %d\n",
               fileuid, i);

        put_ebml_string(pb, MATROSKA_ID_FILEMIMETYPE, mimetype);
        put_ebml_binary(pb, MATROSKA_ID_FILEDATA, st->codec->extradata, st->codec->extradata_size);
        put_ebml_uint(pb, MATROSKA_ID_FILEUID, fileuid);
        end_ebml_master(pb, attached_file);
    }
    end_ebml_master(pb, attachments);

    return 0;
}

static int mkv_write_header(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master ebml_header, segment_info;
    AVDictionaryEntry *tag;
    int ret, i;

    if (!strcmp(s->oformat->name, "webm")) mkv->mode = MODE_WEBM;
    else                                   mkv->mode = MODE_MATROSKAv2;

    if (s->avoid_negative_ts < 0)
        s->avoid_negative_ts = 1;

    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->codec->codec_id == AV_CODEC_ID_ATRAC3 ||
            s->streams[i]->codec->codec_id == AV_CODEC_ID_COOK ||
            s->streams[i]->codec->codec_id == AV_CODEC_ID_RA_288 ||
            s->streams[i]->codec->codec_id == AV_CODEC_ID_SIPR ||
            s->streams[i]->codec->codec_id == AV_CODEC_ID_RV10 ||
            s->streams[i]->codec->codec_id == AV_CODEC_ID_RV20) {
            av_log(s, AV_LOG_ERROR,
                   "The Matroska muxer does not yet support muxing %s\n",
                   avcodec_get_name(s->streams[i]->codec->codec_id));
            return AVERROR_PATCHWELCOME;
        }

    mkv->tracks = av_mallocz(s->nb_streams * sizeof(*mkv->tracks));
    if (!mkv->tracks)
        return AVERROR(ENOMEM);

    ebml_header = start_ebml_master(pb, EBML_ID_HEADER, 0);
    put_ebml_uint   (pb, EBML_ID_EBMLVERSION        ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLREADVERSION    ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXIDLENGTH    ,           4);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXSIZELENGTH  ,           8);
    put_ebml_string (pb, EBML_ID_DOCTYPE            , s->oformat->name);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEVERSION     ,           2);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEREADVERSION ,           2);
    end_ebml_master(pb, ebml_header);

    mkv->segment = start_ebml_master(pb, MATROSKA_ID_SEGMENT, 0);
    mkv->segment_offset = avio_tell(pb);

    // we write 2 seek heads - one at the end of the file to point to each
    // cluster, and one at the beginning to point to all other level one
    // elements (including the seek head at the end of the file), which
    // isn't more than 10 elements if we only write one of each other
    // currently defined level 1 element
    mkv->main_seekhead    = mkv_start_seekhead(pb, mkv->segment_offset, 10);
    if (!mkv->main_seekhead)
        return AVERROR(ENOMEM);

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_INFO, avio_tell(pb));
    if (ret < 0) return ret;

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO, 0);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if ((tag = av_dict_get(s->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TITLE, tag->value);
    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        uint32_t segment_uid[4];
        AVLFG lfg;

        av_lfg_init(&lfg, av_get_random_seed());

        for (i = 0; i < 4; i++)
            segment_uid[i] = av_lfg_get(&lfg);

        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP , LIBAVFORMAT_IDENT);
        put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);
        put_ebml_binary(pb, MATROSKA_ID_SEGMENTUID, segment_uid, 16);
    }

    if (tag = av_dict_get(s->metadata, "creation_time", NULL, 0)) {
        // Adjust time so it's relative to 2001-01-01 and convert to nanoseconds.
        int64_t date_utc = (ff_iso8601_to_unix_time(tag->value) - 978307200) * 1000000000;
        uint8_t date_utc_buf[8];
        AV_WB64(date_utc_buf, date_utc);
        put_ebml_binary(pb, MATROSKA_ID_DATEUTC, date_utc_buf, 8);
    }

    // reserve space for the duration
    mkv->duration = 0;
    mkv->duration_offset = avio_tell(pb);
    put_ebml_void(pb, 11);                  // assumes double-precision float to be written
    end_ebml_master(pb, segment_info);

    ret = mkv_write_tracks(s);
    if (ret < 0) return ret;

    if (mkv->mode != MODE_WEBM) {
        ret = mkv_write_chapters(s);
        if (ret < 0) return ret;

        ret = mkv_write_tags(s);
        if (ret < 0) return ret;

        ret = mkv_write_attachments(s);
        if (ret < 0) return ret;
    }

    if (!s->pb->seekable)
        mkv_write_seekhead(pb, mkv->main_seekhead);

    mkv->cues = mkv_start_cues(mkv->segment_offset);
    if (mkv->cues == NULL)
        return AVERROR(ENOMEM);

    if (pb->seekable && mkv->reserve_cues_space) {
        mkv->cues_pos = avio_tell(pb);
        put_ebml_void(pb, mkv->reserve_cues_space);
    }

    av_init_packet(&mkv->cur_audio_pkt);
    mkv->cur_audio_pkt.size = 0;
    mkv->cluster_pos = -1;

    avio_flush(pb);
    return 0;
}

static int mkv_blockgroup_size(int pkt_size)
{
    int size = pkt_size + 4;
    size += ebml_num_size(size);
    size += 2;              // EBML ID for block and block duration
    size += 8;              // max size of block duration
    size += ebml_num_size(size);
    size += 1;              // blockgroup EBML ID
    return size;
}

static int ass_get_duration(const uint8_t *p)
{
    int sh, sm, ss, sc, eh, em, es, ec;
    uint64_t start, end;

    if (sscanf(p, "%*[^,],%d:%d:%d%*c%d,%d:%d:%d%*c%d",
               &sh, &sm, &ss, &sc, &eh, &em, &es, &ec) != 8)
        return 0;
    start = 3600000LL*sh + 60000LL*sm + 1000LL*ss + 10LL*sc;
    end   = 3600000LL*eh + 60000LL*em + 1000LL*es + 10LL*ec;
    return end - start;
}

#if FF_API_ASS_SSA
static int mkv_write_ass_blocks(AVFormatContext *s, AVIOContext *pb, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int i, layer = 0, max_duration = 0, size, line_size, data_size = pkt->size;
    uint8_t *start, *end, *data = pkt->data;
    ebml_master blockgroup;
    char buffer[2048];

    while (data_size) {
        int duration = ass_get_duration(data);
        max_duration = FFMAX(duration, max_duration);
        end = memchr(data, '\n', data_size);
        size = line_size = end ? end-data+1 : data_size;
        size -= end ? (end[-1]=='\r')+1 : 0;
        start = data;
        for (i=0; i<3; i++, start++)
            if (!(start = memchr(start, ',', size-(start-data))))
                return max_duration;
        size -= start - data;
        sscanf(data, "Dialogue: %d,", &layer);
        i = snprintf(buffer, sizeof(buffer), "%"PRId64",%d,",
                     s->streams[pkt->stream_index]->nb_frames, layer);
        size = FFMIN(i+size, sizeof(buffer));
        memcpy(buffer+i, start, size-i);

        av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
               "pts %" PRId64 ", duration %d\n",
               avio_tell(pb), size, pkt->pts, duration);
        blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(size));
        put_ebml_id(pb, MATROSKA_ID_BLOCK);
        put_ebml_num(pb, size+4, 0);
        avio_w8(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
        avio_wb16(pb, pkt->pts - mkv->cluster_pts);
        avio_w8(pb, 0);
        avio_write(pb, buffer, size);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
        end_ebml_master(pb, blockgroup);

        data += line_size;
        data_size -= line_size;
    }

    return max_duration;
}
#endif

static int mkv_strip_wavpack(const uint8_t *src, uint8_t **pdst, int *size)
{
    uint8_t *dst;
    int srclen = *size;
    int offset = 0;
    int ret;

    dst = av_malloc(srclen);
    if (!dst)
        return AVERROR(ENOMEM);

    while (srclen >= WV_HEADER_SIZE) {
        WvHeader header;

        ret = ff_wv_parse_header(&header, src);
        if (ret < 0)
            goto fail;
        src    += WV_HEADER_SIZE;
        srclen -= WV_HEADER_SIZE;

        if (srclen < header.blocksize) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        if (header.initial) {
            AV_WL32(dst + offset, header.samples);
            offset += 4;
        }
        AV_WL32(dst + offset,     header.flags);
        AV_WL32(dst + offset + 4, header.crc);
        offset += 8;

        if (!(header.initial && header.final)) {
            AV_WL32(dst + offset, header.blocksize);
            offset += 4;
        }

        memcpy(dst + offset, src, header.blocksize);
        src    += header.blocksize;
        srclen -= header.blocksize;
        offset += header.blocksize;
    }

    *pdst = dst;
    *size = offset;

    return 0;
fail:
    av_freep(&dst);
    return ret;
}

static void mkv_write_block(AVFormatContext *s, AVIOContext *pb,
                            unsigned int blockid, AVPacket *pkt, int flags)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    uint8_t *data = NULL, *side_data = NULL;
    int offset = 0, size = pkt->size, side_data_size = 0;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;
    uint64_t additional_id = 0;
    ebml_master block_group, block_additions, block_more;

    av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
           "pts %" PRId64 ", dts %" PRId64 ", duration %d, flags %d\n",
           avio_tell(pb), pkt->size, pkt->pts, pkt->dts, pkt->duration, flags);
    if (codec->codec_id == AV_CODEC_ID_H264 && codec->extradata_size > 0 &&
        (AV_RB24(codec->extradata) == 1 || AV_RB32(codec->extradata) == 1))
        ff_avc_parse_nal_units_buf(pkt->data, &data, &size);
    else if (codec->codec_id == AV_CODEC_ID_WAVPACK) {
        int ret = mkv_strip_wavpack(pkt->data, &data, &size);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Error stripping a WavPack packet.\n");
            return;
        }
    } else
        data = pkt->data;

    if (codec->codec_id == AV_CODEC_ID_PRORES) {
        /* Matroska specification requires to remove the first QuickTime atom
         */
        size -= 8;
        offset = 8;
    }

    side_data = av_packet_get_side_data(pkt,
                                        AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,
                                        &side_data_size);
    if (side_data) {
        additional_id = AV_RB64(side_data);
        side_data += 8;
        side_data_size -= 8;
    }

    if (side_data_size && additional_id == 1) {
        block_group = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, 0);
        blockid = MATROSKA_ID_BLOCK;
    }

    put_ebml_id(pb, blockid);
    put_ebml_num(pb, size+4, 0);
    avio_w8(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
    avio_wb16(pb, ts - mkv->cluster_pts);
    avio_w8(pb, flags);
    avio_write(pb, data + offset, size);
    if (data != pkt->data)
        av_free(data);

    if (side_data_size && additional_id == 1) {
        block_additions = start_ebml_master(pb, MATROSKA_ID_BLOCKADDITIONS, 0);
        block_more = start_ebml_master(pb, MATROSKA_ID_BLOCKMORE, 0);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKADDID, 1);
        put_ebml_id(pb, MATROSKA_ID_BLOCKADDITIONAL);
        put_ebml_num(pb, side_data_size, 0);
        avio_write(pb, side_data, side_data_size);
        end_ebml_master(pb, block_more);
        end_ebml_master(pb, block_additions);
        end_ebml_master(pb, block_group);
    }
}

static int srt_get_duration(uint8_t **buf)
{
    int i, duration = 0;

    for (i=0; i<2 && !duration; i++) {
        int s_hour, s_min, s_sec, s_hsec, e_hour, e_min, e_sec, e_hsec;
        if (sscanf(*buf, "%d:%2d:%2d%*1[,.]%3d --> %d:%2d:%2d%*1[,.]%3d",
                   &s_hour, &s_min, &s_sec, &s_hsec,
                   &e_hour, &e_min, &e_sec, &e_hsec) == 8) {
            s_min  +=   60*s_hour;      e_min  +=   60*e_hour;
            s_sec  +=   60*s_min;       e_sec  +=   60*e_min;
            s_hsec += 1000*s_sec;       e_hsec += 1000*e_sec;
            duration = e_hsec - s_hsec;
        }
        *buf += strcspn(*buf, "\n") + 1;
    }
    return duration;
}

static int mkv_write_srt_blocks(AVFormatContext *s, AVIOContext *pb, AVPacket *pkt)
{
    ebml_master blockgroup;
    AVPacket pkt2 = *pkt;
    int64_t duration = srt_get_duration(&pkt2.data);
    pkt2.size -= pkt2.data - pkt->data;

    blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP,
                                   mkv_blockgroup_size(pkt2.size));
    mkv_write_block(s, pb, MATROSKA_ID_BLOCK, &pkt2, 0);
    put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
    end_ebml_master(pb, blockgroup);

    return duration;
}

static int mkv_write_vtt_blocks(AVFormatContext *s, AVIOContext *pb, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ebml_master blockgroup;
    int id_size, settings_size, size;
    uint8_t *id, *settings;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;
    const int flags = 0;

    id_size = 0;
    id = av_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_IDENTIFIER,
                                 &id_size);

    settings_size = 0;
    settings = av_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_SETTINGS,
                                       &settings_size);

    size = id_size + 1 + settings_size + 1 + pkt->size;

    av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
           "pts %" PRId64 ", dts %" PRId64 ", duration %d, flags %d\n",
           avio_tell(pb), size, pkt->pts, pkt->dts, pkt->duration, flags);

    blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(size));

    put_ebml_id(pb, MATROSKA_ID_BLOCK);
    put_ebml_num(pb, size+4, 0);
    avio_w8(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
    avio_wb16(pb, ts - mkv->cluster_pts);
    avio_w8(pb, flags);
    avio_printf(pb, "%.*s\n%.*s\n%.*s", id_size, id, settings_size, settings, pkt->size, pkt->data);

    put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, pkt->duration);
    end_ebml_master(pb, blockgroup);

    return pkt->duration;
}

static void mkv_flush_dynbuf(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int bufsize;
    uint8_t *dyn_buf;

    if (!mkv->dyn_bc)
        return;

    bufsize = avio_close_dyn_buf(mkv->dyn_bc, &dyn_buf);
    avio_write(s->pb, dyn_buf, bufsize);
    av_free(dyn_buf);
    mkv->dyn_bc = NULL;
}

static int mkv_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    int keyframe = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int duration = pkt->duration;
    int ret;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;

    if (ts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_ERROR, "Can't write packet with unknown timestamp\n");
        return AVERROR(EINVAL);
    }

    if (!s->pb->seekable) {
        if (!mkv->dyn_bc) {
            if ((ret = avio_open_dyn_buf(&mkv->dyn_bc)) < 0) {
                av_log(s, AV_LOG_ERROR, "Failed to open dynamic buffer\n");
                return ret;
            }
        }
        pb = mkv->dyn_bc;
    }

    if (mkv->cluster_pos == -1) {
        mkv->cluster_pos = avio_tell(s->pb);
        mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER, 0);
        put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, FFMAX(0, ts));
        mkv->cluster_pts = FFMAX(0, ts);
    }

    if (codec->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        mkv_write_block(s, pb, MATROSKA_ID_SIMPLEBLOCK, pkt, keyframe << 7);
#if FF_API_ASS_SSA
    } else if (codec->codec_id == AV_CODEC_ID_SSA) {
        duration = mkv_write_ass_blocks(s, pb, pkt);
#endif
    } else if (codec->codec_id == AV_CODEC_ID_SRT) {
        duration = mkv_write_srt_blocks(s, pb, pkt);
    } else if (codec->codec_id == AV_CODEC_ID_WEBVTT) {
        duration = mkv_write_vtt_blocks(s, pb, pkt);
    } else {
        ebml_master blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(pkt->size));
        /* For backward compatibility, prefer convergence_duration. */
        if (pkt->convergence_duration > 0) {
            duration = pkt->convergence_duration;
        }
        mkv_write_block(s, pb, MATROSKA_ID_BLOCK, pkt, 0);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
        end_ebml_master(pb, blockgroup);
    }

    if (codec->codec_type == AVMEDIA_TYPE_VIDEO && keyframe) {
        ret = mkv_add_cuepoint(mkv->cues, pkt->stream_index, ts, mkv->cluster_pos);
        if (ret < 0) return ret;
    }

    mkv->duration = FFMAX(mkv->duration, ts + duration);
    return 0;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb->seekable ? s->pb : mkv->dyn_bc;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    int ret, keyframe = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;
    int cluster_size = avio_tell(pb) - (s->pb->seekable ? mkv->cluster_pos : 0);

    // start a new cluster every 5 MB or 5 sec, or 32k / 1 sec for streaming or
    // after 4k and on a keyframe
    if (mkv->cluster_pos != -1 &&
        ((!s->pb->seekable && (cluster_size > 32*1024 || ts > mkv->cluster_pts + 1000))
         ||                      cluster_size > 5*1024*1024 || ts > mkv->cluster_pts + 5000
         || (codec->codec_type == AVMEDIA_TYPE_VIDEO && keyframe && cluster_size > 4*1024))) {
        av_log(s, AV_LOG_DEBUG, "Starting new cluster at offset %" PRIu64
               " bytes, pts %" PRIu64 "\n", avio_tell(pb), ts);
        end_ebml_master(pb, mkv->cluster);
        mkv->cluster_pos = -1;
        if (mkv->dyn_bc)
            mkv_flush_dynbuf(s);
    }

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt);
        av_free_packet(&mkv->cur_audio_pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Could not write cached audio packet ret:%d\n", ret);
            return ret;
        }
    }

    // buffer an audio packet to ensure the packet containing the video
    // keyframe's timecode is contained in the same cluster for WebM
    if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        mkv->cur_audio_pkt = *pkt;
        if (pkt->buf) {
            mkv->cur_audio_pkt.buf = av_buffer_ref(pkt->buf);
            ret = mkv->cur_audio_pkt.buf ? 0 : AVERROR(ENOMEM);
        } else
            ret = av_dup_packet(&mkv->cur_audio_pkt);
    } else
        ret = mkv_write_packet_internal(s, pkt);
    return ret;
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t currentpos, cuespos;
    int ret;

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt);
        av_free_packet(&mkv->cur_audio_pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Could not write cached audio packet ret:%d\n", ret);
            return ret;
        }
    }

    if (mkv->dyn_bc) {
        end_ebml_master(mkv->dyn_bc, mkv->cluster);
        mkv_flush_dynbuf(s);
    } else if (mkv->cluster_pos != -1) {
        end_ebml_master(pb, mkv->cluster);
    }

    if (pb->seekable) {
        if (mkv->cues->num_entries) {
            if (mkv->reserve_cues_space) {
                int64_t cues_end;

                currentpos = avio_tell(pb);
                avio_seek(pb, mkv->cues_pos, SEEK_SET);

                cuespos = mkv_write_cues(pb, mkv->cues, mkv->tracks, s->nb_streams);
                cues_end = avio_tell(pb);
                if (cues_end > cuespos + mkv->reserve_cues_space) {
                    av_log(s, AV_LOG_ERROR, "Insufficient space reserved for cues: %d "
                           "(needed: %"PRId64").\n", mkv->reserve_cues_space,
                           cues_end - cuespos);
                    return AVERROR(EINVAL);
                }

                if (cues_end < cuespos + mkv->reserve_cues_space)
                    put_ebml_void(pb, mkv->reserve_cues_space - (cues_end - cuespos));

                avio_seek(pb, currentpos, SEEK_SET);
            } else {
                cuespos = mkv_write_cues(pb, mkv->cues, mkv->tracks, s->nb_streams);
            }

            ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CUES, cuespos);
            if (ret < 0) return ret;
        }

        mkv_write_seekhead(pb, mkv->main_seekhead);

        // update the duration
        av_log(s, AV_LOG_DEBUG, "end duration = %" PRIu64 "\n", mkv->duration);
        currentpos = avio_tell(pb);
        avio_seek(pb, mkv->duration_offset, SEEK_SET);
        put_ebml_float(pb, MATROSKA_ID_DURATION, mkv->duration);

        avio_seek(pb, currentpos, SEEK_SET);
    }

    end_ebml_master(pb, mkv->segment);
    av_free(mkv->tracks);
    av_freep(&mkv->cues->entries);
    av_freep(&mkv->cues);

    return 0;
}

static int mkv_query_codec(enum AVCodecID codec_id, int std_compliance)
{
    int i;
    for (i = 0; ff_mkv_codec_tags[i].id != AV_CODEC_ID_NONE; i++)
        if (ff_mkv_codec_tags[i].id == codec_id)
            return 1;

    if (std_compliance < FF_COMPLIANCE_NORMAL) {                // mkv theoretically supports any
        enum AVMediaType type = avcodec_get_type(codec_id);     // video/audio through VFW/ACM
        if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO)
            return 1;
    }

    return 0;
}

const AVCodecTag additional_audio_tags[] = {
    { AV_CODEC_ID_ALAC,      0XFFFFFFFF },
    { AV_CODEC_ID_EAC3,      0XFFFFFFFF },
    { AV_CODEC_ID_MLP,       0xFFFFFFFF },
    { AV_CODEC_ID_OPUS,      0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S16BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S24BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S32BE, 0xFFFFFFFF },
    { AV_CODEC_ID_QDM2,      0xFFFFFFFF },
    { AV_CODEC_ID_RA_144,    0xFFFFFFFF },
    { AV_CODEC_ID_RA_288,    0xFFFFFFFF },
    { AV_CODEC_ID_COOK,      0xFFFFFFFF },
    { AV_CODEC_ID_TRUEHD,    0xFFFFFFFF },
    { AV_CODEC_ID_WAVPACK,   0xFFFFFFFF },
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

const AVCodecTag additional_video_tags[] = {
    { AV_CODEC_ID_PRORES,    0xFFFFFFFF },
    { AV_CODEC_ID_RV10,      0xFFFFFFFF },
    { AV_CODEC_ID_RV20,      0xFFFFFFFF },
    { AV_CODEC_ID_RV30,      0xFFFFFFFF },
    { AV_CODEC_ID_RV40,      0xFFFFFFFF },
    { AV_CODEC_ID_VP9,       0xFFFFFFFF },
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

#define OFFSET(x) offsetof(MatroskaMuxContext, x)
#define FLAGS AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reserve_index_space", "Reserve a given amount of space (in bytes) at the beginning "
        "of the file for the index (cues).", OFFSET(reserve_cues_space), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};

#if CONFIG_MATROSKA_MUXER
static const AVClass matroska_class = {
    .class_name = "matroska muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_matroska_muxer = {
    .name              = "matroska",
    .long_name         = NULL_IF_CONFIG_SMALL("Matroska"),
    .mime_type         = "video/x-matroska",
    .extensions        = "mkv",
    .priv_data_size    = sizeof(MatroskaMuxContext),
    .audio_codec       = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_AC3,
    .video_codec       = CONFIG_LIBX264_ENCODER ?
                         AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG4,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_packet,
    .write_trailer     = mkv_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT,
    .codec_tag         = (const AVCodecTag* const []){
         ff_codec_bmp_tags, ff_codec_wav_tags,
         additional_audio_tags, additional_video_tags, 0
    },
#if FF_API_ASS_SSA
    .subtitle_codec    = AV_CODEC_ID_SSA,
#else
    .subtitle_codec    = AV_CODEC_ID_ASS,
#endif
    .query_codec       = mkv_query_codec,
    .priv_class        = &matroska_class,
};
#endif

#if CONFIG_WEBM_MUXER
static const AVClass webm_class = {
    .class_name = "webm muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_webm_muxer = {
    .name              = "webm",
    .long_name         = NULL_IF_CONFIG_SMALL("WebM"),
    .mime_type         = "video/webm",
    .extensions        = "webm",
    .priv_data_size    = sizeof(MatroskaMuxContext),
    .audio_codec       = AV_CODEC_ID_VORBIS,
    .video_codec       = AV_CODEC_ID_VP8,
    .subtitle_codec    = AV_CODEC_ID_WEBVTT,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_packet,
    .write_trailer     = mkv_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT,
    .priv_class        = &webm_class,
};
#endif

#if CONFIG_MATROSKA_AUDIO_MUXER
static const AVClass mka_class = {
    .class_name = "matroska audio muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVOutputFormat ff_matroska_audio_muxer = {
    .name              = "matroska",
    .long_name         = NULL_IF_CONFIG_SMALL("Matroska"),
    .mime_type         = "audio/x-matroska",
    .extensions        = "mka",
    .priv_data_size    = sizeof(MatroskaMuxContext),
    .audio_codec       = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_AC3,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_packet,
    .write_trailer     = mkv_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_TS_NONSTRICT,
    .codec_tag         = (const AVCodecTag* const []){
        ff_codec_wav_tags, additional_audio_tags, 0
    },
    .priv_class        = &mka_class,
};
#endif
