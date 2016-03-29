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

#include <stdint.h>

#include "avc.h"
#include "hevc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "avlanguage.h"
#include "flacenc.h"
#include "internal.h"
#include "isom.h"
#include "matroska.h"
#include "riff.h"
#include "subtitles.h"
#include "vorbiscomment.h"
#include "wv.h"

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"
#include "libavutil/sha.h"
#include "libavutil/stereo3d.h"

#include "libavcodec/xiph.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/internal.h"

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

typedef struct mkv_cuepoint {
    uint64_t        pts;
    int             stream_idx;
    int             tracknum;
    int64_t         cluster_pos;        ///< file offset of the cluster containing the block
    int64_t         relative_pos;       ///< relative offset from the position of the cluster containing the block
    int64_t         duration;           ///< duration of the block according to time base
} mkv_cuepoint;

typedef struct mkv_cues {
    int64_t         segment_offset;
    mkv_cuepoint    *entries;
    int             num_entries;
} mkv_cues;

typedef struct mkv_track {
    int             write_dts;
    int             has_cue;
    int64_t         ts_offset;
} mkv_track;

#define MODE_MATROSKAv2 0x01
#define MODE_WEBM       0x02

/** Maximum number of tracks allowed in a Matroska file (with track numbers in
 * range 1 to 126 (inclusive) */
#define MAX_TRACKS 126

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
    int cluster_size_limit;
    int64_t cues_pos;
    int64_t cluster_time_limit;
    int is_dash;
    int dash_track_number;
    int is_live;

    uint32_t chapter_id_offset;
    int wrote_chapters;

    int64_t last_track_timestamp[MAX_TRACKS];

    int64_t* stream_durations;
    int64_t* stream_duration_offsets;

    int allow_raw_vfw;
} MatroskaMuxContext;


/** 2 bytes * 3 for EBML IDs, 3 1-byte EBML lengths, 8 bytes for 64 bit
 * offset, 4 bytes for target EBML ID */
#define MAX_SEEKENTRY_SIZE 21

/** per-cuepoint-track - 5 1-byte EBML IDs, 5 1-byte EBML sizes, 4
 * 8-byte uint max */
#define MAX_CUETRACKPOS_SIZE 42

/** per-cuepoint - 2 1-byte EBML IDs, 2 1-byte EBML sizes, 8-byte uint max */
#define MAX_CUEPOINT_SIZE(num_tracks) 12 + MAX_CUETRACKPOS_SIZE * num_tracks

/** Seek preroll value for opus */
#define OPUS_SEEK_PREROLL 80000000

static int ebml_id_size(unsigned int id)
{
    return (av_log2(id + 1) - 1) / 7 + 1;
}

static void put_ebml_id(AVIOContext *pb, unsigned int id)
{
    int i = ebml_id_size(id);
    while (i--)
        avio_w8(pb, (uint8_t)(id >> (i * 8)));
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
    ffio_fill(pb, 0xff, bytes - 1);
}

/**
 * Calculate how many bytes are needed to represent a given number in EBML.
 */
static int ebml_num_size(uint64_t num)
{
    int bytes = 1;
    while ((num + 1) >> bytes * 7)
        bytes++;
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
    av_assert0(num < (1ULL << 56) - 1);

    if (bytes == 0)
        // don't care how many bytes are used, so use the min
        bytes = needed_bytes;
    // the bytes needed to write the given size would exceed the bytes
    // that we need to use, so write unknown size. This shouldn't happen.
    av_assert0(bytes >= needed_bytes);

    num |= 1ULL << bytes * 7;
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(num >> i * 8));
}

static void put_ebml_uint(AVIOContext *pb, unsigned int elementid, uint64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = val;
    while (tmp >>= 8)
        bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_num(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(val >> i * 8));
}

static void put_ebml_sint(AVIOContext *pb, unsigned int elementid, int64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = 2*(val < 0 ? val^-1 : val);

    while (tmp>>=8) bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_num(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(val >> i * 8));
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

static void put_ebml_string(AVIOContext *pb, unsigned int elementid,
                            const char *str)
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
        put_ebml_num(pb, size - 1, 0);
    else
        put_ebml_num(pb, size - 9, 8);
    ffio_fill(pb, 0, currentpos + size - avio_tell(pb));
}

static ebml_master start_ebml_master(AVIOContext *pb, unsigned int elementid,
                                     uint64_t expectedsize)
{
    int bytes = expectedsize ? ebml_num_size(expectedsize) : 8;
    put_ebml_id(pb, elementid);
    put_ebml_size_unknown(pb, bytes);
    return (ebml_master) {avio_tell(pb), bytes };
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
    ffio_fill(pb, 255, size / 255);
    avio_w8(pb, size % 255);
}

/**
 * Free the members allocated in the mux context.
 */
static void mkv_free(MatroskaMuxContext *mkv) {
    if (mkv->main_seekhead) {
        av_freep(&mkv->main_seekhead->entries);
        av_freep(&mkv->main_seekhead);
    }
    if (mkv->cues) {
        av_freep(&mkv->cues->entries);
        av_freep(&mkv->cues);
    }
    av_freep(&mkv->tracks);
    av_freep(&mkv->stream_durations);
    av_freep(&mkv->stream_duration_offsets);
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
static mkv_seekhead *mkv_start_seekhead(AVIOContext *pb, int64_t segment_offset,
                                        int numelements)
{
    mkv_seekhead *new_seekhead = av_mallocz(sizeof(mkv_seekhead));
    if (!new_seekhead)
        return NULL;

    new_seekhead->segment_offset = segment_offset;

    if (numelements > 0) {
        new_seekhead->filepos = avio_tell(pb);
        // 21 bytes max for a seek entry, 10 bytes max for the SeekHead ID
        // and size, and 3 bytes to guarantee that an EBML void element
        // will fit afterwards
        new_seekhead->reserved_size = numelements * MAX_SEEKENTRY_SIZE + 13;
        new_seekhead->max_entries   = numelements;
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

    entries = av_realloc_array(entries, seekhead->num_entries + 1, sizeof(mkv_seekhead_entry));
    if (!entries)
        return AVERROR(ENOMEM);
    seekhead->entries = entries;

    seekhead->entries[seekhead->num_entries].elementid    = elementid;
    seekhead->entries[seekhead->num_entries++].segmentpos = filepos - seekhead->segment_offset;

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
static int64_t mkv_write_seekhead(AVIOContext *pb, MatroskaMuxContext *mkv)
{
    mkv_seekhead *seekhead = mkv->main_seekhead;
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
    av_freep(&mkv->main_seekhead->entries);
    av_freep(&mkv->main_seekhead);

    return currentpos;
}

static mkv_cues *mkv_start_cues(int64_t segment_offset)
{
    mkv_cues *cues = av_mallocz(sizeof(mkv_cues));
    if (!cues)
        return NULL;

    cues->segment_offset = segment_offset;
    return cues;
}

static int mkv_add_cuepoint(mkv_cues *cues, int stream, int tracknum, int64_t ts,
                            int64_t cluster_pos, int64_t relative_pos, int64_t duration)
{
    mkv_cuepoint *entries = cues->entries;

    if (ts < 0)
        return 0;

    entries = av_realloc_array(entries, cues->num_entries + 1, sizeof(mkv_cuepoint));
    if (!entries)
        return AVERROR(ENOMEM);
    cues->entries = entries;

    cues->entries[cues->num_entries].pts           = ts;
    cues->entries[cues->num_entries].stream_idx    = stream;
    cues->entries[cues->num_entries].tracknum      = tracknum;
    cues->entries[cues->num_entries].cluster_pos   = cluster_pos - cues->segment_offset;
    cues->entries[cues->num_entries].relative_pos  = relative_pos;
    cues->entries[cues->num_entries++].duration    = duration;

    return 0;
}

static int64_t mkv_write_cues(AVFormatContext *s, mkv_cues *cues, mkv_track *tracks, int num_tracks)
{
    AVIOContext *pb = s->pb;
    ebml_master cues_element;
    int64_t currentpos;
    int i, j;

    currentpos = avio_tell(pb);
    cues_element = start_ebml_master(pb, MATROSKA_ID_CUES, 0);

    for (i = 0; i < cues->num_entries; i++) {
        ebml_master cuepoint, track_positions;
        mkv_cuepoint *entry = &cues->entries[i];
        uint64_t pts = entry->pts;
        int ctp_nb = 0;

        // Calculate the number of entries, so we know the element size
        for (j = 0; j < num_tracks; j++)
            tracks[j].has_cue = 0;
        for (j = 0; j < cues->num_entries - i && entry[j].pts == pts; j++) {
            int tracknum = entry[j].stream_idx;
            av_assert0(tracknum>=0 && tracknum<num_tracks);
            if (tracks[tracknum].has_cue && s->streams[tracknum]->codec->codec_type != AVMEDIA_TYPE_SUBTITLE)
                continue;
            tracks[tracknum].has_cue = 1;
            ctp_nb ++;
        }

        cuepoint = start_ebml_master(pb, MATROSKA_ID_POINTENTRY, MAX_CUEPOINT_SIZE(ctp_nb));
        put_ebml_uint(pb, MATROSKA_ID_CUETIME, pts);

        // put all the entries from different tracks that have the exact same
        // timestamp into the same CuePoint
        for (j = 0; j < num_tracks; j++)
            tracks[j].has_cue = 0;
        for (j = 0; j < cues->num_entries - i && entry[j].pts == pts; j++) {
            int tracknum = entry[j].stream_idx;
            av_assert0(tracknum>=0 && tracknum<num_tracks);
            if (tracks[tracknum].has_cue && s->streams[tracknum]->codec->codec_type != AVMEDIA_TYPE_SUBTITLE)
                continue;
            tracks[tracknum].has_cue = 1;
            track_positions = start_ebml_master(pb, MATROSKA_ID_CUETRACKPOSITION, MAX_CUETRACKPOS_SIZE);
            put_ebml_uint(pb, MATROSKA_ID_CUETRACK           , entry[j].tracknum   );
            put_ebml_uint(pb, MATROSKA_ID_CUECLUSTERPOSITION , entry[j].cluster_pos);
            put_ebml_uint(pb, MATROSKA_ID_CUERELATIVEPOSITION, entry[j].relative_pos);
            if (entry[j].duration != -1)
                put_ebml_uint(pb, MATROSKA_ID_CUEDURATION    , entry[j].duration);
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
    const uint8_t *header_start[3];
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

static int put_flac_codecpriv(AVFormatContext *s,
                              AVIOContext *pb, AVCodecContext *codec)
{
    int write_comment = (codec->channel_layout &&
                         !(codec->channel_layout & ~0x3ffffULL) &&
                         !ff_flac_is_native_layout(codec->channel_layout));
    int ret = ff_flac_write_header(pb, codec->extradata, codec->extradata_size,
                                   !write_comment);

    if (ret < 0)
        return ret;

    if (write_comment) {
        const char *vendor = (s->flags & AVFMT_FLAG_BITEXACT) ?
                             "Lavf" : LIBAVFORMAT_IDENT;
        AVDictionary *dict = NULL;
        uint8_t buf[32], *data, *p;
        int64_t len;

        snprintf(buf, sizeof(buf), "0x%"PRIx64, codec->channel_layout);
        av_dict_set(&dict, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", buf, 0);

        len = ff_vorbiscomment_length(dict, vendor);
        if (len >= ((1<<24) - 4))
            return AVERROR(EINVAL);

        data = av_malloc(len + 4);
        if (!data) {
            av_dict_free(&dict);
            return AVERROR(ENOMEM);
        }

        data[0] = 0x84;
        AV_WB24(data + 1, len);

        p = data + 4;
        ff_vorbiscomment_write(&p, &dict, vendor);

        avio_write(pb, data, len + 4);

        av_freep(&data);
        av_dict_free(&dict);
    }

    return 0;
}

static int get_aac_sample_rates(AVFormatContext *s, AVCodecContext *codec,
                                int *sample_rate, int *output_sample_rate)
{
    MPEG4AudioConfig mp4ac;

    if (avpriv_mpeg4audio_get_config(&mp4ac, codec->extradata,
                                     codec->extradata_size * 8, 1) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Error parsing AAC extradata, unable to determine samplerate.\n");
        return AVERROR(EINVAL);
    }

    *sample_rate        = mp4ac.sample_rate;
    *output_sample_rate = mp4ac.ext_sample_rate;
    return 0;
}

static int mkv_write_native_codecprivate(AVFormatContext *s,
                                         AVCodecContext *codec,
                                         AVIOContext *dyn_cp)
{
    switch (codec->codec_id) {
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_THEORA:
        return put_xiph_codecpriv(s, dyn_cp, codec);
    case AV_CODEC_ID_FLAC:
        return put_flac_codecpriv(s, dyn_cp, codec);
    case AV_CODEC_ID_WAVPACK:
        return put_wv_codecpriv(dyn_cp, codec);
    case AV_CODEC_ID_H264:
        return ff_isom_write_avcc(dyn_cp, codec->extradata,
                                  codec->extradata_size);
    case AV_CODEC_ID_HEVC:
        ff_isom_write_hvcc(dyn_cp, codec->extradata,
                           codec->extradata_size, 0);
        return 0;
    case AV_CODEC_ID_ALAC:
        if (codec->extradata_size < 36) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid extradata found, ALAC expects a 36-byte "
                   "QuickTime atom.");
            return AVERROR_INVALIDDATA;
        } else
            avio_write(dyn_cp, codec->extradata + 12,
                       codec->extradata_size - 12);
        break;
    default:
        if (codec->codec_id == AV_CODEC_ID_PRORES &&
            ff_codec_get_id(ff_codec_movvideo_tags, codec->codec_tag) == AV_CODEC_ID_PRORES) {
            avio_wl32(dyn_cp, codec->codec_tag);
        } else if (codec->extradata_size && codec->codec_id != AV_CODEC_ID_TTA)
            avio_write(dyn_cp, codec->extradata, codec->extradata_size);
    }

    return 0;
}

static int mkv_write_codecprivate(AVFormatContext *s, AVIOContext *pb,
                                  AVCodecContext *codec, int native_id,
                                  int qt_id)
{
    AVIOContext *dyn_cp;
    uint8_t *codecpriv;
    int ret, codecpriv_size;

    ret = avio_open_dyn_buf(&dyn_cp);
    if (ret < 0)
        return ret;

    if (native_id) {
        ret = mkv_write_native_codecprivate(s, codec, dyn_cp);
    } else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (qt_id) {
            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(ff_codec_movvideo_tags,
                                                    codec->codec_id);
            if (codec->extradata_size) {
                if (   ff_codec_get_id(ff_codec_movvideo_tags, codec->codec_tag) == codec->codec_id
                    && ff_codec_get_id(ff_codec_movvideo_tags, AV_RL32(codec->extradata + 4)) != codec->codec_id
                ) {
                    int i;
                    avio_wb32(dyn_cp, 0x5a + codec->extradata_size);
                    avio_wl32(dyn_cp, codec->codec_tag);
                    for(i = 0; i < 0x5a - 8; i++)
                        avio_w8(dyn_cp, 0);
                }
                avio_write(dyn_cp, codec->extradata, codec->extradata_size);
            }
        } else {
            if (!ff_codec_get_tag(ff_codec_bmp_tags, codec->codec_id))
                av_log(s, AV_LOG_WARNING, "codec %s is not supported by this format\n",
                       avcodec_get_name(codec->codec_id));

            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(ff_codec_bmp_tags,
                                                    codec->codec_id);
            if (!codec->codec_tag && codec->codec_id != AV_CODEC_ID_RAWVIDEO) {
                av_log(s, AV_LOG_ERROR, "No bmp codec tag found for codec %s\n",
                       avcodec_get_name(codec->codec_id));
                ret = AVERROR(EINVAL);
            }

            ff_put_bmp_header(dyn_cp, codec, ff_codec_bmp_tags, 0, 0);
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

        ff_put_wav_header(dyn_cp, codec, FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX);
    }

    codecpriv_size = avio_close_dyn_buf(dyn_cp, &codecpriv);
    if (codecpriv_size)
        put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codecpriv,
                        codecpriv_size);
    av_free(codecpriv);
    return ret;
}

static int mkv_write_video_color(AVIOContext *pb, AVCodecContext *codec, AVStream *st) {
    int side_data_size = 0;
    const uint8_t *side_data = av_stream_get_side_data(
        st, AV_PKT_DATA_MASTERING_DISPLAY_METADATA, &side_data_size);
    ebml_master colorinfo = start_ebml_master(pb, MATROSKA_ID_VIDEOCOLOR, 0);

    if (codec->color_trc != AVCOL_TRC_UNSPECIFIED &&
        codec->color_trc < AVCOL_TRC_NB) {
        put_ebml_uint(pb, MATROSKA_ID_VIDEOCOLORTRANSFERCHARACTERISTICS,
                      codec->color_trc);
    }
    if (codec->colorspace != AVCOL_SPC_UNSPECIFIED &&
        codec->colorspace < AVCOL_SPC_NB) {
        put_ebml_uint(pb, MATROSKA_ID_VIDEOCOLORMATRIXCOEFF, codec->colorspace);
    }
    if (codec->color_primaries != AVCOL_PRI_UNSPECIFIED &&
        codec->color_primaries < AVCOL_PRI_NB) {
        put_ebml_uint(pb, MATROSKA_ID_VIDEOCOLORPRIMARIES, codec->color_primaries);
    }
    if (codec->color_range != AVCOL_RANGE_UNSPECIFIED &&
        codec->color_range < AVCOL_RANGE_NB) {
        put_ebml_uint(pb, MATROSKA_ID_VIDEOCOLORRANGE, codec->color_range);
    }
    if (side_data_size == sizeof(AVMasteringDisplayMetadata)) {
        ebml_master meta_element = start_ebml_master(
            pb, MATROSKA_ID_VIDEOCOLORMASTERINGMETA, 0);
        const AVMasteringDisplayMetadata *metadata =
            (const AVMasteringDisplayMetadata*)side_data;
        if (metadata->has_primaries) {
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_RX,
                           av_q2d(metadata->display_primaries[0][0]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_RY,
                           av_q2d(metadata->display_primaries[0][1]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_GX,
                           av_q2d(metadata->display_primaries[1][0]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_GY,
                           av_q2d(metadata->display_primaries[1][1]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_BX,
                           av_q2d(metadata->display_primaries[2][0]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_BY,
                           av_q2d(metadata->display_primaries[2][1]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_WHITEX,
                           av_q2d(metadata->white_point[0]));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_WHITEY,
                           av_q2d(metadata->white_point[1]));
        }
        if (metadata->has_luminance) {
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_LUMINANCEMAX,
                           av_q2d(metadata->max_luminance));
            put_ebml_float(pb, MATROSKA_ID_VIDEOCOLOR_LUMINANCEMIN,
                           av_q2d(metadata->min_luminance));
        }
        end_ebml_master(pb, meta_element);
    }
    end_ebml_master(pb, colorinfo);
    return 0;
}


static int mkv_write_stereo_mode(AVFormatContext *s, AVIOContext *pb,
                                 AVStream *st, int mode, int *h_width, int *h_height)
{
    int i;
    int ret = 0;
    AVDictionaryEntry *tag;
    MatroskaVideoStereoModeType format = MATROSKA_VIDEO_STEREOMODE_TYPE_NB;

    *h_width = 1;
    *h_height = 1;
    // convert metadata into proper side data and add it to the stream
    if ((tag = av_dict_get(st->metadata, "stereo_mode", NULL, 0)) ||
        (tag = av_dict_get( s->metadata, "stereo_mode", NULL, 0))) {
        int stereo_mode = atoi(tag->value);

        for (i=0; i<MATROSKA_VIDEO_STEREOMODE_TYPE_NB; i++)
            if (!strcmp(tag->value, ff_matroska_video_stereo_mode[i])){
                stereo_mode = i;
                break;
            }

        if (stereo_mode < MATROSKA_VIDEO_STEREOMODE_TYPE_NB &&
            stereo_mode != 10 && stereo_mode != 12) {
            int ret = ff_mkv_stereo3d_conv(st, stereo_mode);
            if (ret < 0)
                return ret;
        }
    }

    // iterate to find the stereo3d side data
    for (i = 0; i < st->nb_side_data; i++) {
        AVPacketSideData sd = st->side_data[i];
        if (sd.type == AV_PKT_DATA_STEREO3D) {
            AVStereo3D *stereo = (AVStereo3D *)sd.data;

            switch (stereo->type) {
            case AV_STEREO3D_2D:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_MONO;
                break;
            case AV_STEREO3D_SIDEBYSIDE:
                format = (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    ? MATROSKA_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT
                    : MATROSKA_VIDEO_STEREOMODE_TYPE_LEFT_RIGHT;
                *h_width = 2;
                break;
            case AV_STEREO3D_TOPBOTTOM:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM;
                if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    format--;
                *h_height = 2;
                break;
            case AV_STEREO3D_CHECKERBOARD:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_LR;
                if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    format--;
                break;
            case AV_STEREO3D_LINES:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_LR;
                if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    format--;
                *h_height = 2;
                break;
            case AV_STEREO3D_COLUMNS:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_LR;
                if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    format--;
                *h_width = 2;
                break;
            case AV_STEREO3D_FRAMESEQUENCE:
                format = MATROSKA_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_LR;
                if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
                    format++;
                break;
            }
            break;
        }
    }

    if (format == MATROSKA_VIDEO_STEREOMODE_TYPE_NB)
        return ret;

    // if webm, do not write unsupported modes
    if ((mode == MODE_WEBM &&
        format > MATROSKA_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM &&
        format != MATROSKA_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT)
        || format >= MATROSKA_VIDEO_STEREOMODE_TYPE_NB) {
        av_log(s, AV_LOG_ERROR,
               "The specified stereo mode is not valid.\n");
        format = MATROSKA_VIDEO_STEREOMODE_TYPE_NB;
        return AVERROR(EINVAL);
    }

    // write StereoMode if format is valid
    put_ebml_uint(pb, MATROSKA_ID_VIDEOSTEREOMODE, format);

    return ret;
}

static int mkv_write_track(AVFormatContext *s, MatroskaMuxContext *mkv,
                           int i, AVIOContext *pb, int default_stream_exists)
{
    AVStream *st = s->streams[i];
    AVCodecContext *codec = st->codec;
    ebml_master subinfo, track;
    int native_id = 0;
    int qt_id = 0;
    int bit_depth = av_get_bits_per_sample(codec->codec_id);
    int sample_rate = codec->sample_rate;
    int output_sample_rate = 0;
    int display_width_div = 1;
    int display_height_div = 1;
    int j, ret;
    AVDictionaryEntry *tag;

    if (codec->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
        mkv->have_attachments = 1;
        return 0;
    }

    if (!bit_depth && codec->codec_id != AV_CODEC_ID_ADPCM_G726) {
        if (codec->bits_per_raw_sample)
            bit_depth = codec->bits_per_raw_sample;
        else
            bit_depth = av_get_bytes_per_sample(codec->sample_fmt) << 3;
    }
    if (!bit_depth)
        bit_depth = codec->bits_per_coded_sample;

    if (codec->codec_id == AV_CODEC_ID_AAC) {
        ret = get_aac_sample_rates(s, codec, &sample_rate, &output_sample_rate);
        if (ret < 0)
            return ret;
    }

    track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY, 0);
    put_ebml_uint (pb, MATROSKA_ID_TRACKNUMBER,
                   mkv->is_dash ? mkv->dash_track_number : i + 1);
    put_ebml_uint (pb, MATROSKA_ID_TRACKUID,
                   mkv->is_dash ? mkv->dash_track_number : i + 1);
    put_ebml_uint (pb, MATROSKA_ID_TRACKFLAGLACING , 0);    // no lacing (yet)

    if ((tag = av_dict_get(st->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TRACKNAME, tag->value);
    tag = av_dict_get(st->metadata, "language", NULL, 0);
    if (mkv->mode != MODE_WEBM || codec->codec_id != AV_CODEC_ID_WEBVTT) {
        put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, tag && tag->value ? tag->value:"und");
    } else if (tag && tag->value) {
        put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, tag->value);
    }

    // The default value for TRACKFLAGDEFAULT is 1, so add element
    // if we need to clear it.
    if (default_stream_exists && !(st->disposition & AV_DISPOSITION_DEFAULT))
        put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGDEFAULT, !!(st->disposition & AV_DISPOSITION_DEFAULT));

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
        if (codec->codec_id == AV_CODEC_ID_RAWVIDEO && !codec->codec_tag) {
            if (mkv->allow_raw_vfw) {
                native_id = 0;
            } else {
                av_log(s, AV_LOG_ERROR, "Raw RGB is not supported Natively in Matroska, you can use AVI or NUT or\n"
                                        "If you would like to store it anyway using VFW mode, enable allow_raw_vfw (-allow_raw_vfw 1)\n");
                return AVERROR(EINVAL);
            }
        }
    }

    if (codec->codec_type == AVMEDIA_TYPE_AUDIO && codec->initial_padding && codec->codec_id == AV_CODEC_ID_OPUS) {
        int64_t codecdelay = av_rescale_q(codec->initial_padding,
                                          (AVRational){ 1, 48000 },
                                          (AVRational){ 1, 1000000000 });
        if (codecdelay < 0) {
            av_log(s, AV_LOG_ERROR, "Initial padding is invalid\n");
            return AVERROR(EINVAL);
        }
//         mkv->tracks[i].ts_offset = av_rescale_q(codec->initial_padding,
//                                                 (AVRational){ 1, codec->sample_rate },
//                                                 st->time_base);

        put_ebml_uint(pb, MATROSKA_ID_CODECDELAY, codecdelay);
    }
    if (codec->codec_id == AV_CODEC_ID_OPUS) {
        put_ebml_uint(pb, MATROSKA_ID_SEEKPREROLL, OPUS_SEEK_PREROLL);
    }

    if (mkv->mode == MODE_WEBM && !(codec->codec_id == AV_CODEC_ID_VP8 ||
                                    codec->codec_id == AV_CODEC_ID_VP9 ||
                                    codec->codec_id == AV_CODEC_ID_OPUS ||
                                    codec->codec_id == AV_CODEC_ID_VORBIS ||
                                    codec->codec_id == AV_CODEC_ID_WEBVTT)) {
        av_log(s, AV_LOG_ERROR,
               "Only VP8 or VP9 video and Vorbis or Opus audio and WebVTT subtitles are supported for WebM.\n");
        return AVERROR(EINVAL);
    }

    switch (codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);

        if(   st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0
           && av_cmp_q(av_inv_q(st->avg_frame_rate), codec->time_base) > 0)
            put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, 1000000000LL * st->avg_frame_rate.den / st->avg_frame_rate.num);
        else
            put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, 1000000000LL * codec->time_base.num / codec->time_base.den);

        if (!native_id &&
            ff_codec_get_tag(ff_codec_movvideo_tags, codec->codec_id) &&
            ((!ff_codec_get_tag(ff_codec_bmp_tags,   codec->codec_id) && codec->codec_id != AV_CODEC_ID_RAWVIDEO) ||
             codec->codec_id == AV_CODEC_ID_SVQ1 ||
             codec->codec_id == AV_CODEC_ID_SVQ3 ||
             codec->codec_id == AV_CODEC_ID_CINEPAK))
            qt_id = 1;

        if (qt_id)
            put_ebml_string(pb, MATROSKA_ID_CODECID, "V_QUICKTIME");
        else if (!native_id) {
            // if there is no mkv-specific codec ID, use VFW mode
            put_ebml_string(pb, MATROSKA_ID_CODECID, "V_MS/VFW/FOURCC");
            mkv->tracks[i].write_dts = 1;
            s->internal->avoid_negative_ts_use_pts = 0;
        }

        subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKVIDEO, 0);
        // XXX: interlace flag?
        put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELWIDTH , codec->width);
        put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELHEIGHT, codec->height);

        // check both side data and metadata for stereo information,
        // write the result to the bitstream if any is found
        ret = mkv_write_stereo_mode(s, pb, st, mkv->mode,
                                    &display_width_div,
                                    &display_height_div);
        if (ret < 0)
            return ret;

        if (((tag = av_dict_get(st->metadata, "alpha_mode", NULL, 0)) && atoi(tag->value)) ||
            ((tag = av_dict_get( s->metadata, "alpha_mode", NULL, 0)) && atoi(tag->value)) ||
            (codec->pix_fmt == AV_PIX_FMT_YUVA420P)) {
            put_ebml_uint(pb, MATROSKA_ID_VIDEOALPHAMODE, 1);
        }

        // write DisplayWidth and DisplayHeight, they contain the size of
        // a single source view and/or the display aspect ratio
        if (st->sample_aspect_ratio.num) {
            int64_t d_width = av_rescale(codec->width, st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
            if (d_width > INT_MAX) {
                av_log(s, AV_LOG_ERROR, "Overflow in display width\n");
                return AVERROR(EINVAL);
            }
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , d_width / display_width_div);
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, codec->height / display_height_div);
        } else if (display_width_div != 1 || display_height_div != 1) {
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , codec->width / display_width_div);
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, codec->height / display_height_div);
        }

        if (codec->codec_id == AV_CODEC_ID_RAWVIDEO) {
            uint32_t color_space = av_le2ne32(codec->codec_tag);
            put_ebml_binary(pb, MATROSKA_ID_VIDEOCOLORSPACE, &color_space, sizeof(color_space));
        }
        if (s->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            mkv_write_video_color(pb, codec, st);
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
        if (ret < 0)
            return ret;
    }

    end_ebml_master(pb, track);

    return 0;
}

static int mkv_write_tracks(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master tracks;
    int i, ret, default_stream_exists = 0;

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TRACKS, avio_tell(pb));
    if (ret < 0)
        return ret;

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS, 0);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        default_stream_exists |= st->disposition & AV_DISPOSITION_DEFAULT;
    }
    for (i = 0; i < s->nb_streams; i++) {
        ret = mkv_write_track(s, mkv, i, pb, default_stream_exists);
        if (ret < 0)
            return ret;
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

    if (!s->nb_chapters || mkv->wrote_chapters)
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
        int64_t chapterstart = av_rescale_q(c->start, c->time_base, scale);
        int64_t chapterend   = av_rescale_q(c->end,   c->time_base, scale);
        AVDictionaryEntry *t = NULL;
        if (chapterstart < 0 || chapterstart > chapterend || chapterend < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid chapter start (%"PRId64") or end (%"PRId64").\n",
                   chapterstart, chapterend);
            return AVERROR_INVALIDDATA;
        }

        chapteratom = start_ebml_master(pb, MATROSKA_ID_CHAPTERATOM, 0);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERUID, c->id + mkv->chapter_id_offset);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMESTART, chapterstart);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMEEND, chapterend);
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

    mkv->wrote_chapters = 1;
    return 0;
}

static int mkv_write_simpletag(AVIOContext *pb, AVDictionaryEntry *t)
{
    uint8_t *key = av_strdup(t->key);
    uint8_t *p   = key;
    const uint8_t *lang = NULL;
    ebml_master tag;

    if (!key)
        return AVERROR(ENOMEM);

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
    return 0;
}

static int mkv_write_tag_targets(AVFormatContext *s,
                                 unsigned int elementid, unsigned int uid,
                                 ebml_master *tags, ebml_master* tag)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ebml_master targets;
    int ret;

    if (!tags->pos) {
        ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TAGS, avio_tell(s->pb));
        if (ret < 0) return ret;

        *tags = start_ebml_master(s->pb, MATROSKA_ID_TAGS, 0);
    }

    *tag     = start_ebml_master(s->pb, MATROSKA_ID_TAG,        0);
    targets = start_ebml_master(s->pb, MATROSKA_ID_TAGTARGETS, 0);
    if (elementid)
        put_ebml_uint(s->pb, elementid, uid);
    end_ebml_master(s->pb, targets);
    return 0;
}

static int mkv_write_tag(AVFormatContext *s, AVDictionary *m, unsigned int elementid,
                         unsigned int uid, ebml_master *tags)
{
    ebml_master tag;
    int ret;
    AVDictionaryEntry *t = NULL;

    ret = mkv_write_tag_targets(s, elementid, uid, tags, &tag);
    if (ret < 0)
        return ret;

    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if (av_strcasecmp(t->key, "title") &&
            av_strcasecmp(t->key, "stereo_mode") &&
            av_strcasecmp(t->key, "creation_time") &&
            av_strcasecmp(t->key, "encoding_tool") &&
            (elementid != MATROSKA_ID_TAGTARGETS_TRACKUID ||
             av_strcasecmp(t->key, "language"))) {
            ret = mkv_write_simpletag(s->pb, t);
            if (ret < 0)
                return ret;
        }
    }

    end_ebml_master(s->pb, tag);
    return 0;
}

static int mkv_check_tag(AVDictionary *m)
{
    AVDictionaryEntry *t = NULL;

    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX)))
        if (av_strcasecmp(t->key, "title") && av_strcasecmp(t->key, "stereo_mode"))
            return 1;

    return 0;
}

static int mkv_write_tags(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ebml_master tags = {0};
    int i, ret;

    ff_metadata_conv_ctx(s, ff_mkv_metadata_conv, NULL);

    if (mkv_check_tag(s->metadata)) {
        ret = mkv_write_tag(s, s->metadata, 0, 0, &tags);
        if (ret < 0) return ret;
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        if (!mkv_check_tag(st->metadata))
            continue;

        ret = mkv_write_tag(s, st->metadata, MATROSKA_ID_TAGTARGETS_TRACKUID, i + 1, &tags);
        if (ret < 0) return ret;
    }

    if (!mkv->is_live) {
        for (i = 0; i < s->nb_streams; i++) {
            ebml_master tag_target;
            ebml_master tag;

            mkv_write_tag_targets(s, MATROSKA_ID_TAGTARGETS_TRACKUID, i + 1, &tags, &tag_target);

            tag = start_ebml_master(s->pb, MATROSKA_ID_SIMPLETAG, 0);
            put_ebml_string(s->pb, MATROSKA_ID_TAGNAME, "DURATION");
            mkv->stream_duration_offsets[i] = avio_tell(s->pb);

            // Reserve space to write duration as a 20-byte string.
            // 2 (ebml id) + 1 (data size) + 20 (data)
            put_ebml_void(s->pb, 23);
            end_ebml_master(s->pb, tag);
            end_ebml_master(s->pb, tag_target);
        }
    }

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *ch = s->chapters[i];

        if (!mkv_check_tag(ch->metadata))
            continue;

        ret = mkv_write_tag(s, ch->metadata, MATROSKA_ID_TAGTARGETS_CHAPTERUID, ch->id + mkv->chapter_id_offset, &tags);
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
            for (i = 0; ff_mkv_image_mime_tags[i].id != AV_CODEC_ID_NONE; i++)
                if (ff_mkv_image_mime_tags[i].id == st->codec->codec_id) {
                    mimetype = ff_mkv_image_mime_tags[i].str;
                    break;
                }
        }
        if (!mimetype) {
            av_log(s, AV_LOG_ERROR, "Attachment stream %d has no mimetype tag and "
                                    "it cannot be deduced from the codec id.\n", i);
            return AVERROR(EINVAL);
        }

        if (s->flags & AVFMT_FLAG_BITEXACT) {
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
    int ret, i, version = 2;
    int64_t creation_time;

    if (!strcmp(s->oformat->name, "webm"))
        mkv->mode = MODE_WEBM;
    else
        mkv->mode = MODE_MATROSKAv2;

    if (mkv->mode != MODE_WEBM ||
        av_dict_get(s->metadata, "stereo_mode", NULL, 0) ||
        av_dict_get(s->metadata, "alpha_mode", NULL, 0))
        version = 4;

    for (i = 0; i < s->nb_streams; i++) {
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
        if (s->streams[i]->codec->codec_id == AV_CODEC_ID_OPUS ||
            av_dict_get(s->streams[i]->metadata, "stereo_mode", NULL, 0) ||
            av_dict_get(s->streams[i]->metadata, "alpha_mode", NULL, 0))
            version = 4;
    }

    mkv->tracks = av_mallocz_array(s->nb_streams, sizeof(*mkv->tracks));
    if (!mkv->tracks) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ebml_header = start_ebml_master(pb, EBML_ID_HEADER, 0);
    put_ebml_uint   (pb, EBML_ID_EBMLVERSION        ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLREADVERSION    ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXIDLENGTH    ,           4);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXSIZELENGTH  ,           8);
    put_ebml_string (pb, EBML_ID_DOCTYPE            , s->oformat->name);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEVERSION     ,     version);
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
    if (!mkv->main_seekhead) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_INFO, avio_tell(pb));
    if (ret < 0) goto fail;

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO, 0);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if ((tag = av_dict_get(s->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TITLE, tag->value);
    if (!(s->flags & AVFMT_FLAG_BITEXACT)) {
        uint32_t segment_uid[4];
        AVLFG lfg;

        av_lfg_init(&lfg, av_get_random_seed());

        for (i = 0; i < 4; i++)
            segment_uid[i] = av_lfg_get(&lfg);

        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP, LIBAVFORMAT_IDENT);
        if ((tag = av_dict_get(s->metadata, "encoding_tool", NULL, 0)))
            put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, tag->value);
        else
            put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);
        put_ebml_binary(pb, MATROSKA_ID_SEGMENTUID, segment_uid, 16);
    } else {
        const char *ident = "Lavf";
        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP , ident);
        put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, ident);
    }

    if (ff_parse_creation_time_metadata(s, &creation_time, 0) > 0) {
        // Adjust time so it's relative to 2001-01-01 and convert to nanoseconds.
        int64_t date_utc = (creation_time - 978307200000000LL) * 1000;
        uint8_t date_utc_buf[8];
        AV_WB64(date_utc_buf, date_utc);
        put_ebml_binary(pb, MATROSKA_ID_DATEUTC, date_utc_buf, 8);
    }

    // reserve space for the duration
    mkv->duration = 0;
    mkv->duration_offset = avio_tell(pb);
    if (!mkv->is_live) {
        put_ebml_void(pb, 11);              // assumes double-precision float to be written
    }
    end_ebml_master(pb, segment_info);

    // initialize stream_duration fields
    mkv->stream_durations = av_mallocz(s->nb_streams * sizeof(int64_t));
    mkv->stream_duration_offsets = av_mallocz(s->nb_streams * sizeof(int64_t));

    ret = mkv_write_tracks(s);
    if (ret < 0)
        goto fail;

    for (i = 0; i < s->nb_chapters; i++)
        mkv->chapter_id_offset = FFMAX(mkv->chapter_id_offset, 1LL - s->chapters[i]->id);

    if (mkv->mode != MODE_WEBM) {
        ret = mkv_write_chapters(s);
        if (ret < 0)
            goto fail;

        ret = mkv_write_tags(s);
        if (ret < 0)
            goto fail;

        ret = mkv_write_attachments(s);
        if (ret < 0)
            goto fail;
    }

    if (!s->pb->seekable && !mkv->is_live)
        mkv_write_seekhead(pb, mkv);

    mkv->cues = mkv_start_cues(mkv->segment_offset);
    if (!mkv->cues) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if (pb->seekable && mkv->reserve_cues_space) {
        mkv->cues_pos = avio_tell(pb);
        put_ebml_void(pb, mkv->reserve_cues_space);
    }

    av_init_packet(&mkv->cur_audio_pkt);
    mkv->cur_audio_pkt.size = 0;
    mkv->cluster_pos = -1;

    avio_flush(pb);

    // start a new cluster every 5 MB or 5 sec, or 32k / 1 sec for streaming or
    // after 4k and on a keyframe
    if (pb->seekable) {
        if (mkv->cluster_time_limit < 0)
            mkv->cluster_time_limit = 5000;
        if (mkv->cluster_size_limit < 0)
            mkv->cluster_size_limit = 5 * 1024 * 1024;
    } else {
        if (mkv->cluster_time_limit < 0)
            mkv->cluster_time_limit = 1000;
        if (mkv->cluster_size_limit < 0)
            mkv->cluster_size_limit = 32 * 1024;
    }

    return 0;
fail:
    mkv_free(mkv);
    return ret;
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
                            unsigned int blockid, AVPacket *pkt, int keyframe)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    uint8_t *data = NULL, *side_data = NULL;
    int offset = 0, size = pkt->size, side_data_size = 0;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;
    uint64_t additional_id = 0;
    int64_t discard_padding = 0;
    uint8_t track_number = (mkv->is_dash ? mkv->dash_track_number : (pkt->stream_index + 1));
    ebml_master block_group, block_additions, block_more;

    av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
           "pts %" PRId64 ", dts %" PRId64 ", duration %" PRId64 ", keyframe %d\n",
           avio_tell(pb), pkt->size, pkt->pts, pkt->dts, pkt->duration,
           keyframe != 0);
    if (codec->codec_id == AV_CODEC_ID_H264 && codec->extradata_size > 0 &&
        (AV_RB24(codec->extradata) == 1 || AV_RB32(codec->extradata) == 1))
        ff_avc_parse_nal_units_buf(pkt->data, &data, &size);
    else if (codec->codec_id == AV_CODEC_ID_HEVC && codec->extradata_size > 6 &&
             (AV_RB24(codec->extradata) == 1 || AV_RB32(codec->extradata) == 1))
        /* extradata is Annex B, assume the bitstream is too and convert it */
        ff_hevc_annexb2mp4_buf(pkt->data, &data, &size, 0, NULL);
    else if (codec->codec_id == AV_CODEC_ID_WAVPACK) {
        int ret = mkv_strip_wavpack(pkt->data, &data, &size);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Error stripping a WavPack packet.\n");
            return;
        }
    } else
        data = pkt->data;

    if (codec->codec_id == AV_CODEC_ID_PRORES && size >= 8) {
        /* Matroska specification requires to remove the first QuickTime atom
         */
        size  -= 8;
        offset = 8;
    }

    side_data = av_packet_get_side_data(pkt,
                                        AV_PKT_DATA_SKIP_SAMPLES,
                                        &side_data_size);

    if (side_data && side_data_size >= 10) {
        discard_padding = av_rescale_q(AV_RL32(side_data + 4),
                                       (AVRational){1, codec->sample_rate},
                                       (AVRational){1, 1000000000});
    }

    side_data = av_packet_get_side_data(pkt,
                                        AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,
                                        &side_data_size);
    if (side_data) {
        additional_id = AV_RB64(side_data);
        side_data += 8;
        side_data_size -= 8;
    }

    if ((side_data_size && additional_id == 1) || discard_padding) {
        block_group = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, 0);
        blockid = MATROSKA_ID_BLOCK;
    }

    put_ebml_id(pb, blockid);
    put_ebml_num(pb, size + 4, 0);
    // this assumes stream_index is less than 126
    avio_w8(pb, 0x80 | track_number);
    avio_wb16(pb, ts - mkv->cluster_pts);
    avio_w8(pb, (blockid == MATROSKA_ID_SIMPLEBLOCK && keyframe) ? (1 << 7) : 0);
    avio_write(pb, data + offset, size);
    if (data != pkt->data)
        av_free(data);

    if (blockid == MATROSKA_ID_BLOCK && !keyframe) {
        put_ebml_sint(pb, MATROSKA_ID_BLOCKREFERENCE,
                      mkv->last_track_timestamp[track_number - 1]);
    }
    mkv->last_track_timestamp[track_number - 1] = ts - mkv->cluster_pts;

    if (discard_padding) {
        put_ebml_sint(pb, MATROSKA_ID_DISCARDPADDING, discard_padding);
    }

    if (side_data_size && additional_id == 1) {
        block_additions = start_ebml_master(pb, MATROSKA_ID_BLOCKADDITIONS, 0);
        block_more = start_ebml_master(pb, MATROSKA_ID_BLOCKMORE, 0);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKADDID, 1);
        put_ebml_id(pb, MATROSKA_ID_BLOCKADDITIONAL);
        put_ebml_num(pb, side_data_size, 0);
        avio_write(pb, side_data, side_data_size);
        end_ebml_master(pb, block_more);
        end_ebml_master(pb, block_additions);
    }
    if ((side_data_size && additional_id == 1) || discard_padding) {
        end_ebml_master(pb, block_group);
    }
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
           "pts %" PRId64 ", dts %" PRId64 ", duration %" PRId64 ", flags %d\n",
           avio_tell(pb), size, pkt->pts, pkt->dts, pkt->duration, flags);

    blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(size));

    put_ebml_id(pb, MATROSKA_ID_BLOCK);
    put_ebml_num(pb, size + 4, 0);
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

static void mkv_start_new_cluster(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb;

    if (s->pb->seekable) {
        pb = s->pb;
    } else {
        pb = mkv->dyn_bc;
    }

    av_log(s, AV_LOG_DEBUG,
            "Starting new cluster at offset %" PRIu64 " bytes, "
            "pts %" PRIu64 "dts %" PRIu64 "\n",
            avio_tell(pb), pkt->pts, pkt->dts);
    end_ebml_master(pb, mkv->cluster);
    mkv->cluster_pos = -1;
    if (mkv->dyn_bc)
        mkv_flush_dynbuf(s);
    avio_flush(s->pb);
}

static int mkv_write_packet_internal(AVFormatContext *s, AVPacket *pkt, int add_cue)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb         = s->pb;
    AVCodecContext *codec   = s->streams[pkt->stream_index]->codec;
    int keyframe            = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int duration            = pkt->duration;
    int ret;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;
    int64_t relative_packet_pos;
    int dash_tracknum = mkv->is_dash ? mkv->dash_track_number : pkt->stream_index + 1;

    if (ts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_ERROR, "Can't write packet with unknown timestamp\n");
        return AVERROR(EINVAL);
    }
    ts += mkv->tracks[pkt->stream_index].ts_offset;

    if (mkv->cluster_pos != -1) {
        int64_t cluster_time = ts - mkv->cluster_pts + mkv->tracks[pkt->stream_index].ts_offset;
        if ((int16_t)cluster_time != cluster_time) {
            av_log(s, AV_LOG_WARNING, "Starting new cluster due to timestamp\n");
            mkv_start_new_cluster(s, pkt);
        }
    }

    if (!s->pb->seekable) {
        if (!mkv->dyn_bc) {
            ret = avio_open_dyn_buf(&mkv->dyn_bc);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Failed to open dynamic buffer\n");
                return ret;
            }
        }
        pb = mkv->dyn_bc;
    }

    if (mkv->cluster_pos == -1) {
        mkv->cluster_pos = avio_tell(s->pb);
        mkv->cluster     = start_ebml_master(pb, MATROSKA_ID_CLUSTER, 0);
        put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, FFMAX(0, ts));
        mkv->cluster_pts = FFMAX(0, ts);
    }

    relative_packet_pos = avio_tell(s->pb) - mkv->cluster.pos;

    if (codec->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        mkv_write_block(s, pb, MATROSKA_ID_SIMPLEBLOCK, pkt, keyframe);
        if (s->pb->seekable && (codec->codec_type == AVMEDIA_TYPE_VIDEO && keyframe || add_cue)) {
            ret = mkv_add_cuepoint(mkv->cues, pkt->stream_index, dash_tracknum, ts, mkv->cluster_pos, relative_packet_pos, -1);
            if (ret < 0) return ret;
        }
    } else {
        if (codec->codec_id == AV_CODEC_ID_WEBVTT) {
            duration = mkv_write_vtt_blocks(s, pb, pkt);
        } else {
            ebml_master blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP,
                                                       mkv_blockgroup_size(pkt->size));

#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
            /* For backward compatibility, prefer convergence_duration. */
            if (pkt->convergence_duration > 0) {
                duration = pkt->convergence_duration;
            }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            /* All subtitle blocks are considered to be keyframes. */
            mkv_write_block(s, pb, MATROSKA_ID_BLOCK, pkt, 1);
            put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
            end_ebml_master(pb, blockgroup);
        }

        if (s->pb->seekable) {
            ret = mkv_add_cuepoint(mkv->cues, pkt->stream_index, dash_tracknum, ts,
                                   mkv->cluster_pos, relative_packet_pos, duration);
            if (ret < 0)
                return ret;
        }
    }

    mkv->duration = FFMAX(mkv->duration, ts + duration);

    if (mkv->stream_durations)
        mkv->stream_durations[pkt->stream_index] =
            FFMAX(mkv->stream_durations[pkt->stream_index], ts + duration);

    return 0;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int codec_type          = s->streams[pkt->stream_index]->codec->codec_type;
    int keyframe            = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int cluster_size;
    int64_t cluster_time;
    int ret;
    int start_new_cluster;

    if (mkv->tracks[pkt->stream_index].write_dts)
        cluster_time = pkt->dts - mkv->cluster_pts;
    else
        cluster_time = pkt->pts - mkv->cluster_pts;
    cluster_time += mkv->tracks[pkt->stream_index].ts_offset;

    // start a new cluster every 5 MB or 5 sec, or 32k / 1 sec for streaming or
    // after 4k and on a keyframe
    if (s->pb->seekable) {
        cluster_size = avio_tell(s->pb) - mkv->cluster_pos;
    } else {
        cluster_size = avio_tell(mkv->dyn_bc);
    }

    if (mkv->is_dash && codec_type == AVMEDIA_TYPE_VIDEO) {
        // WebM DASH specification states that the first block of every cluster
        // has to be a key frame. So for DASH video, we only create a cluster
        // on seeing key frames.
        start_new_cluster = keyframe;
    } else if (mkv->is_dash && codec_type == AVMEDIA_TYPE_AUDIO &&
               (mkv->cluster_pos == -1 ||
                cluster_time > mkv->cluster_time_limit)) {
        // For DASH audio, we create a Cluster based on cluster_time_limit
        start_new_cluster = 1;
    } else if (!mkv->is_dash &&
               (cluster_size > mkv->cluster_size_limit ||
                cluster_time > mkv->cluster_time_limit ||
                (codec_type == AVMEDIA_TYPE_VIDEO && keyframe &&
                 cluster_size > 4 * 1024))) {
        start_new_cluster = 1;
    } else {
        start_new_cluster = 0;
    }

    if (mkv->cluster_pos != -1 && start_new_cluster) {
        mkv_start_new_cluster(s, pkt);
    }

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        // for DASH audio, a CuePoint has to be added when there is a new cluster.
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt,
                                        mkv->is_dash ? start_new_cluster : 0);
        av_packet_unref(&mkv->cur_audio_pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Could not write cached audio packet ret:%d\n", ret);
            return ret;
        }
    }

    // buffer an audio packet to ensure the packet containing the video
    // keyframe's timecode is contained in the same cluster for WebM
    if (codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = av_packet_ref(&mkv->cur_audio_pkt, pkt);
    } else
        ret = mkv_write_packet_internal(s, pkt, 0);
    return ret;
}

static int mkv_write_flush_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb;
    if (s->pb->seekable)
        pb = s->pb;
    else
        pb = mkv->dyn_bc;
    if (!pkt) {
        if (mkv->cluster_pos != -1) {
            av_log(s, AV_LOG_DEBUG,
                   "Flushing cluster at offset %" PRIu64 " bytes\n",
                   avio_tell(pb));
            end_ebml_master(pb, mkv->cluster);
            mkv->cluster_pos = -1;
            if (mkv->dyn_bc)
                mkv_flush_dynbuf(s);
            avio_flush(s->pb);
        }
        return 1;
    }
    return mkv_write_packet(s, pkt);
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t currentpos, cuespos;
    int ret;

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt, 0);
        av_packet_unref(&mkv->cur_audio_pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Could not write cached audio packet ret:%d\n", ret);
            return ret;
        }
    }

    if (mkv->dyn_bc) {
        end_ebml_master(mkv->dyn_bc, mkv->cluster);
        mkv_flush_dynbuf(s);
    } else if (mkv->cluster_pos != -1) {
        end_ebml_master(pb, mkv->cluster);
    }

    if (mkv->mode != MODE_WEBM) {
        ret = mkv_write_chapters(s);
        if (ret < 0)
            return ret;
    }

    if (pb->seekable) {
        if (mkv->cues->num_entries) {
            if (mkv->reserve_cues_space) {
                int64_t cues_end;

                currentpos = avio_tell(pb);
                avio_seek(pb, mkv->cues_pos, SEEK_SET);

                cuespos  = mkv_write_cues(s, mkv->cues, mkv->tracks, s->nb_streams);
                cues_end = avio_tell(pb);
                if (cues_end > cuespos + mkv->reserve_cues_space) {
                    av_log(s, AV_LOG_ERROR,
                           "Insufficient space reserved for cues: %d "
                           "(needed: %" PRId64 ").\n",
                           mkv->reserve_cues_space, cues_end - cuespos);
                    return AVERROR(EINVAL);
                }

                if (cues_end < cuespos + mkv->reserve_cues_space)
                    put_ebml_void(pb, mkv->reserve_cues_space -
                                  (cues_end - cuespos));

                avio_seek(pb, currentpos, SEEK_SET);
            } else {
                cuespos = mkv_write_cues(s, mkv->cues, mkv->tracks, s->nb_streams);
            }

            ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CUES,
                                         cuespos);
            if (ret < 0)
                return ret;
        }

        mkv_write_seekhead(pb, mkv);

        // update the duration
        av_log(s, AV_LOG_DEBUG, "end duration = %" PRIu64 "\n", mkv->duration);
        currentpos = avio_tell(pb);
        avio_seek(pb, mkv->duration_offset, SEEK_SET);
        put_ebml_float(pb, MATROSKA_ID_DURATION, mkv->duration);

        // update stream durations
        if (mkv->stream_durations) {
            int i;
            for (i = 0; i < s->nb_streams; ++i) {
                AVStream *st = s->streams[i];
                double duration_sec = mkv->stream_durations[i] * av_q2d(st->time_base);
                char duration_string[20] = "";

                av_log(s, AV_LOG_DEBUG, "stream %d end duration = %" PRIu64 "\n", i,
                       mkv->stream_durations[i]);

                if (!mkv->is_live && mkv->stream_duration_offsets[i] > 0) {
                    avio_seek(pb, mkv->stream_duration_offsets[i], SEEK_SET);

                    snprintf(duration_string, 20, "%02d:%02d:%012.9f",
                             (int) duration_sec / 3600, ((int) duration_sec / 60) % 60,
                             fmod(duration_sec, 60));

                    put_ebml_binary(pb, MATROSKA_ID_TAGSTRING, duration_string, 20);
                }
            }
        }

        avio_seek(pb, currentpos, SEEK_SET);
    }

    if (!mkv->is_live) {
        end_ebml_master(pb, mkv->segment);
    }

    mkv_free(mkv);
    return 0;
}

static int mkv_query_codec(enum AVCodecID codec_id, int std_compliance)
{
    int i;
    for (i = 0; ff_mkv_codec_tags[i].id != AV_CODEC_ID_NONE; i++)
        if (ff_mkv_codec_tags[i].id == codec_id)
            return 1;

    if (std_compliance < FF_COMPLIANCE_NORMAL) {
        enum AVMediaType type = avcodec_get_type(codec_id);
        // mkv theoretically supports any video/audio through VFW/ACM
        if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO)
            return 1;
    }

    return 0;
}

static int mkv_init(struct AVFormatContext *s)
{
    int i;

    if (s->avoid_negative_ts < 0) {
        s->avoid_negative_ts = 1;
        s->internal->avoid_negative_ts_use_pts = 1;
    }

    for (i = 0; i < s->nb_streams; i++) {
        // ms precision is the de-facto standard timescale for mkv files
        avpriv_set_pts_info(s->streams[i], 64, 1, 1000);
    }

    return 0;
}

static int mkv_check_bitstream(struct AVFormatContext *s, const AVPacket *pkt)
{
    int ret = 1;
    AVStream *st = s->streams[pkt->stream_index];

    if (st->codec->codec_id == AV_CODEC_ID_AAC) {
        if (pkt->size > 2 && (AV_RB16(pkt->data) & 0xfff0) == 0xfff0)
            ret = ff_stream_add_bitstream_filter(st, "aac_adtstoasc", NULL);
    } else if (st->codec->codec_id == AV_CODEC_ID_VP9) {
        ret = ff_stream_add_bitstream_filter(st, "vp9_superframe", NULL);
    }

    return ret;
}

static const AVCodecTag additional_audio_tags[] = {
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
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

static const AVCodecTag additional_video_tags[] = {
    { AV_CODEC_ID_RV10,      0xFFFFFFFF },
    { AV_CODEC_ID_RV20,      0xFFFFFFFF },
    { AV_CODEC_ID_RV30,      0xFFFFFFFF },
    { AV_CODEC_ID_RV40,      0xFFFFFFFF },
    { AV_CODEC_ID_VP9,       0xFFFFFFFF },
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

static const AVCodecTag additional_subtitle_tags[] = {
    { AV_CODEC_ID_DVB_SUBTITLE,      0xFFFFFFFF },
    { AV_CODEC_ID_HDMV_PGS_SUBTITLE, 0xFFFFFFFF },
    { AV_CODEC_ID_NONE,              0xFFFFFFFF }
};

#define OFFSET(x) offsetof(MatroskaMuxContext, x)
#define FLAGS AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reserve_index_space", "Reserve a given amount of space (in bytes) at the beginning of the file for the index (cues).", OFFSET(reserve_cues_space), AV_OPT_TYPE_INT,   { .i64 = 0 },   0, INT_MAX,   FLAGS },
    { "cluster_size_limit",  "Store at most the provided amount of bytes in a cluster. ",                                     OFFSET(cluster_size_limit), AV_OPT_TYPE_INT  , { .i64 = -1 }, -1, INT_MAX,   FLAGS },
    { "cluster_time_limit",  "Store at most the provided number of milliseconds in a cluster.",                               OFFSET(cluster_time_limit), AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, FLAGS },
    { "dash", "Create a WebM file conforming to WebM DASH specification", OFFSET(is_dash), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "dash_track_number", "Track number for the DASH stream", OFFSET(dash_track_number), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 127, FLAGS },
    { "live", "Write files assuming it is a live stream.", OFFSET(is_live), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "allow_raw_vfw", "allow RAW VFW mode", OFFSET(allow_raw_vfw), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
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
    .init              = mkv_init,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_flush_packet,
    .write_trailer     = mkv_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT | AVFMT_ALLOW_FLUSH,
    .codec_tag         = (const AVCodecTag* const []){
         ff_codec_bmp_tags, ff_codec_wav_tags,
         additional_audio_tags, additional_video_tags, additional_subtitle_tags, 0
    },
    .subtitle_codec    = AV_CODEC_ID_ASS,
    .query_codec       = mkv_query_codec,
    .check_bitstream   = mkv_check_bitstream,
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
    .audio_codec       = CONFIG_LIBOPUS_ENCODER ? AV_CODEC_ID_OPUS : AV_CODEC_ID_VORBIS,
    .video_codec       = CONFIG_LIBVPX_VP9_ENCODER? AV_CODEC_ID_VP9 : AV_CODEC_ID_VP8,
    .subtitle_codec    = AV_CODEC_ID_WEBVTT,
    .init              = mkv_init,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_flush_packet,
    .write_trailer     = mkv_write_trailer,
    .check_bitstream   = mkv_check_bitstream,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT | AVFMT_ALLOW_FLUSH,
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
    .long_name         = NULL_IF_CONFIG_SMALL("Matroska Audio"),
    .mime_type         = "audio/x-matroska",
    .extensions        = "mka",
    .priv_data_size    = sizeof(MatroskaMuxContext),
    .audio_codec       = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_AC3,
    .video_codec       = AV_CODEC_ID_NONE,
    .init              = mkv_init,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_flush_packet,
    .write_trailer     = mkv_write_trailer,
    .check_bitstream   = mkv_check_bitstream,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_TS_NONSTRICT |
                         AVFMT_ALLOW_FLUSH,
    .codec_tag         = (const AVCodecTag* const []){
        ff_codec_wav_tags, additional_audio_tags, 0
    },
    .priv_class        = &mka_class,
};
#endif
