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

#include "avformat.h"
#include "riff.h"
#include "isom.h"
#include "matroska.h"
#include "avc.h"
#include "flacenc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/md5.h"
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
} mkv_track;

#define MODE_MATROSKAv2 0x01
#define MODE_WEBM       0x02

typedef struct MatroskaMuxContext {
    int             mode;
    ByteIOContext   *dyn_bc;
    ebml_master     segment;
    int64_t         segment_offset;
    int64_t         segment_uid;
    ebml_master     cluster;
    int64_t         cluster_pos;        ///< file offset of the current cluster
    int64_t         cluster_pts;
    int64_t         duration_offset;
    int64_t         duration;
    mkv_seekhead    *main_seekhead;
    mkv_seekhead    *cluster_seekhead;
    mkv_cues        *cues;
    mkv_track       *tracks;

    struct AVMD5    *md5_ctx;
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

static void put_ebml_id(ByteIOContext *pb, unsigned int id)
{
    int i = ebml_id_size(id);
    while (i--)
        put_byte(pb, id >> (i*8));
}

/**
 * Write an EBML size meaning "unknown size".
 *
 * @param bytes The number of bytes the size should occupy (maximum: 8).
 */
static void put_ebml_size_unknown(ByteIOContext *pb, int bytes)
{
    assert(bytes <= 8);
    put_byte(pb, 0x1ff >> bytes);
    while (--bytes)
        put_byte(pb, 0xff);
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
static void put_ebml_num(ByteIOContext *pb, uint64_t num, int bytes)
{
    int i, needed_bytes = ebml_num_size(num);

    // sizes larger than this are currently undefined in EBML
    assert(num < (1ULL<<56)-1);

    if (bytes == 0)
        // don't care how many bytes are used, so use the min
        bytes = needed_bytes;
    // the bytes needed to write the given size would exceed the bytes
    // that we need to use, so write unknown size. This shouldn't happen.
    assert(bytes >= needed_bytes);

    num |= 1ULL << bytes*7;
    for (i = bytes - 1; i >= 0; i--)
        put_byte(pb, num >> i*8);
}

static void put_ebml_uint(ByteIOContext *pb, unsigned int elementid, uint64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = val;
    while (tmp>>=8) bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_num(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        put_byte(pb, val >> i*8);
}

static void put_ebml_float(ByteIOContext *pb, unsigned int elementid, double val)
{
    put_ebml_id(pb, elementid);
    put_ebml_num(pb, 8, 0);
    put_be64(pb, av_dbl2int(val));
}

static void put_ebml_binary(ByteIOContext *pb, unsigned int elementid,
                            const uint8_t *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_num(pb, size, 0);
    put_buffer(pb, buf, size);
}

static void put_ebml_string(ByteIOContext *pb, unsigned int elementid, const char *str)
{
    put_ebml_binary(pb, elementid, str, strlen(str));
}

/**
 * Writes a void element of a given size. Useful for reserving space in
 * the file to be written to later.
 *
 * @param size The number of bytes to reserve, which must be at least 2.
 */
static void put_ebml_void(ByteIOContext *pb, uint64_t size)
{
    int64_t currentpos = url_ftell(pb);

    assert(size >= 2);

    put_ebml_id(pb, EBML_ID_VOID);
    // we need to subtract the length needed to store the size from the
    // size we need to reserve so 2 cases, we use 8 bytes to store the
    // size if possible, 1 byte otherwise
    if (size < 10)
        put_ebml_num(pb, size-1, 0);
    else
        put_ebml_num(pb, size-9, 8);
    while(url_ftell(pb) < currentpos + size)
        put_byte(pb, 0);
}

static ebml_master start_ebml_master(ByteIOContext *pb, unsigned int elementid, uint64_t expectedsize)
{
    int bytes = expectedsize ? ebml_num_size(expectedsize) : 8;
    put_ebml_id(pb, elementid);
    put_ebml_size_unknown(pb, bytes);
    return (ebml_master){ url_ftell(pb), bytes };
}

static void end_ebml_master(ByteIOContext *pb, ebml_master master)
{
    int64_t pos = url_ftell(pb);

    if (url_fseek(pb, master.pos - master.sizebytes, SEEK_SET) < 0)
        return;
    put_ebml_num(pb, pos - master.pos, master.sizebytes);
    url_fseek(pb, pos, SEEK_SET);
}

static void put_xiph_size(ByteIOContext *pb, int size)
{
    int i;
    for (i = 0; i < size / 255; i++)
        put_byte(pb, 255);
    put_byte(pb, size % 255);
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
static mkv_seekhead * mkv_start_seekhead(ByteIOContext *pb, int64_t segment_offset, int numelements)
{
    mkv_seekhead *new_seekhead = av_mallocz(sizeof(mkv_seekhead));
    if (new_seekhead == NULL)
        return NULL;

    new_seekhead->segment_offset = segment_offset;

    if (numelements > 0) {
        new_seekhead->filepos = url_ftell(pb);
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
static int64_t mkv_write_seekhead(ByteIOContext *pb, mkv_seekhead *seekhead)
{
    ebml_master metaseek, seekentry;
    int64_t currentpos;
    int i;

    currentpos = url_ftell(pb);

    if (seekhead->reserved_size > 0)
        if (url_fseek(pb, seekhead->filepos, SEEK_SET) < 0)
            return -1;

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
        uint64_t remaining = seekhead->filepos + seekhead->reserved_size - url_ftell(pb);
        put_ebml_void(pb, remaining);
        url_fseek(pb, currentpos, SEEK_SET);

        currentpos = seekhead->filepos;
    }
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

    entries = av_realloc(entries, (cues->num_entries + 1) * sizeof(mkv_cuepoint));
    if (entries == NULL)
        return AVERROR(ENOMEM);

    if (ts < 0)
        return 0;

    entries[cues->num_entries  ].pts = ts;
    entries[cues->num_entries  ].tracknum = stream + 1;
    entries[cues->num_entries++].cluster_pos = cluster_pos - cues->segment_offset;

    cues->entries = entries;
    return 0;
}

static int64_t mkv_write_cues(ByteIOContext *pb, mkv_cues *cues, int num_tracks)
{
    ebml_master cues_element;
    int64_t currentpos;
    int i, j;

    currentpos = url_ftell(pb);
    cues_element = start_ebml_master(pb, MATROSKA_ID_CUES, 0);

    for (i = 0; i < cues->num_entries; i++) {
        ebml_master cuepoint, track_positions;
        mkv_cuepoint *entry = &cues->entries[i];
        uint64_t pts = entry->pts;

        cuepoint = start_ebml_master(pb, MATROSKA_ID_POINTENTRY, MAX_CUEPOINT_SIZE(num_tracks));
        put_ebml_uint(pb, MATROSKA_ID_CUETIME, pts);

        // put all the entries from different tracks that have the exact same
        // timestamp into the same CuePoint
        for (j = 0; j < cues->num_entries - i && entry[j].pts == pts; j++) {
            track_positions = start_ebml_master(pb, MATROSKA_ID_CUETRACKPOSITION, MAX_CUETRACKPOS_SIZE);
            put_ebml_uint(pb, MATROSKA_ID_CUETRACK          , entry[j].tracknum   );
            put_ebml_uint(pb, MATROSKA_ID_CUECLUSTERPOSITION, entry[j].cluster_pos);
            end_ebml_master(pb, track_positions);
        }
        i += j - 1;
        end_ebml_master(pb, cuepoint);
    }
    end_ebml_master(pb, cues_element);

    av_free(cues->entries);
    av_free(cues);
    return currentpos;
}

static int put_xiph_codecpriv(AVFormatContext *s, ByteIOContext *pb, AVCodecContext *codec)
{
    uint8_t *header_start[3];
    int header_len[3];
    int first_header_size;
    int j;

    if (codec->codec_id == CODEC_ID_VORBIS)
        first_header_size = 30;
    else
        first_header_size = 42;

    if (ff_split_xiph_headers(codec->extradata, codec->extradata_size,
                              first_header_size, header_start, header_len) < 0) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    put_byte(pb, 2);                    // number packets - 1
    for (j = 0; j < 2; j++) {
        put_xiph_size(pb, header_len[j]);
    }
    for (j = 0; j < 3; j++)
        put_buffer(pb, header_start[j], header_len[j]);

    return 0;
}

static void get_aac_sample_rates(AVFormatContext *s, AVCodecContext *codec, int *sample_rate, int *output_sample_rate)
{
    int sri;

    if (codec->extradata_size < 2) {
        av_log(s, AV_LOG_WARNING, "No AAC extradata, unable to determine samplerate.\n");
        return;
    }

    sri = ((codec->extradata[0] << 1) & 0xE) | (codec->extradata[1] >> 7);
    if (sri > 12) {
        av_log(s, AV_LOG_WARNING, "AAC samplerate index out of bounds\n");
        return;
    }
    *sample_rate = ff_mpeg4audio_sample_rates[sri];

    // if sbr, get output sample rate as well
    if (codec->extradata_size == 5) {
        sri = (codec->extradata[4] >> 3) & 0xF;
        if (sri > 12) {
            av_log(s, AV_LOG_WARNING, "AAC output samplerate index out of bounds\n");
            return;
        }
        *output_sample_rate = ff_mpeg4audio_sample_rates[sri];
    }
}

static int mkv_write_codecprivate(AVFormatContext *s, ByteIOContext *pb, AVCodecContext *codec, int native_id, int qt_id)
{
    ByteIOContext *dyn_cp;
    uint8_t *codecpriv;
    int ret, codecpriv_size;

    ret = url_open_dyn_buf(&dyn_cp);
    if(ret < 0)
        return ret;

    if (native_id) {
        if (codec->codec_id == CODEC_ID_VORBIS || codec->codec_id == CODEC_ID_THEORA)
            ret = put_xiph_codecpriv(s, dyn_cp, codec);
        else if (codec->codec_id == CODEC_ID_FLAC)
            ret = ff_flac_write_header(dyn_cp, codec, 1);
        else if (codec->codec_id == CODEC_ID_H264)
            ret = ff_isom_write_avcc(dyn_cp, codec->extradata, codec->extradata_size);
        else if (codec->extradata_size)
            put_buffer(dyn_cp, codec->extradata, codec->extradata_size);
    } else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (qt_id) {
            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(codec_movvideo_tags, codec->codec_id);
            if (codec->extradata_size)
                put_buffer(dyn_cp, codec->extradata, codec->extradata_size);
        } else {
            if (!codec->codec_tag)
                codec->codec_tag = ff_codec_get_tag(ff_codec_bmp_tags, codec->codec_id);
            if (!codec->codec_tag) {
                av_log(s, AV_LOG_ERROR, "No bmp codec ID found.\n");
                ret = -1;
            }

            ff_put_bmp_header(dyn_cp, codec, ff_codec_bmp_tags, 0);
        }

    } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        unsigned int tag;
        tag = ff_codec_get_tag(ff_codec_wav_tags, codec->codec_id);
        if (!tag) {
            av_log(s, AV_LOG_ERROR, "No wav codec ID found.\n");
            ret = -1;
        }
        if (!codec->codec_tag)
            codec->codec_tag = tag;

        ff_put_wav_header(dyn_cp, codec);
    }

    codecpriv_size = url_close_dyn_buf(dyn_cp, &codecpriv);
    if (codecpriv_size)
        put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codecpriv, codecpriv_size);
    av_free(codecpriv);
    return ret;
}

static int mkv_write_tracks(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = s->pb;
    ebml_master tracks;
    int i, j, ret;

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TRACKS, url_ftell(pb));
    if (ret < 0) return ret;

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS, 0);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecContext *codec = st->codec;
        ebml_master subinfo, track;
        int native_id = 0;
        int qt_id = 0;
        int bit_depth = av_get_bits_per_sample(codec->codec_id);
        int sample_rate = codec->sample_rate;
        int output_sample_rate = 0;
        AVMetadataTag *tag;

        if (!bit_depth)
            bit_depth = av_get_bits_per_sample_format(codec->sample_fmt);

        if (codec->codec_id == CODEC_ID_AAC)
            get_aac_sample_rates(s, codec, &sample_rate, &output_sample_rate);

        track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY, 0);
        put_ebml_uint (pb, MATROSKA_ID_TRACKNUMBER     , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKUID        , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKFLAGLACING , 0);    // no lacing (yet)

        if ((tag = av_metadata_get(st->metadata, "title", NULL, 0)))
            put_ebml_string(pb, MATROSKA_ID_TRACKNAME, tag->value);
        tag = av_metadata_get(st->metadata, "language", NULL, 0);
        put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, tag ? tag->value:"und");

        if (st->disposition)
            put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGDEFAULT, !!(st->disposition & AV_DISPOSITION_DEFAULT));

        // look for a codec ID string specific to mkv to use,
        // if none are found, use AVI codes
        for (j = 0; ff_mkv_codec_tags[j].id != CODEC_ID_NONE; j++) {
            if (ff_mkv_codec_tags[j].id == codec->codec_id) {
                put_ebml_string(pb, MATROSKA_ID_CODECID, ff_mkv_codec_tags[j].str);
                native_id = 1;
                break;
            }
        }

        if (mkv->mode == MODE_WEBM && !(codec->codec_id == CODEC_ID_VP8 ||
                                        codec->codec_id == CODEC_ID_VORBIS)) {
            av_log(s, AV_LOG_ERROR,
                   "Only VP8 video and Vorbis audio are supported for WebM.\n");
            return AVERROR(EINVAL);
        }

        switch (codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);
                put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, av_q2d(codec->time_base)*1E9);

                if (!native_id &&
                      ff_codec_get_tag(codec_movvideo_tags, codec->codec_id) &&
                    (!ff_codec_get_tag(ff_codec_bmp_tags,   codec->codec_id)
                     || codec->codec_id == CODEC_ID_SVQ1
                     || codec->codec_id == CODEC_ID_SVQ3
                     || codec->codec_id == CODEC_ID_CINEPAK))
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
                if (st->sample_aspect_ratio.num) {
                    int d_width = codec->width*av_q2d(st->sample_aspect_ratio);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , d_width);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, codec->height);
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
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_SUBTITLE);
                break;
            default:
                av_log(s, AV_LOG_ERROR, "Only audio, video, and subtitles are supported for Matroska.");
                break;
        }
        ret = mkv_write_codecprivate(s, pb, codec, native_id, qt_id);
        if (ret < 0) return ret;

        end_ebml_master(pb, track);

        // ms precision is the de-facto standard timescale for mkv files
        av_set_pts_info(st, 64, 1, 1000);
    }
    end_ebml_master(pb, tracks);
    return 0;
}

static int mkv_write_chapters(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = s->pb;
    ebml_master chapters, editionentry;
    AVRational scale = {1, 1E9};
    int i, ret;

    if (!s->nb_chapters)
        return 0;

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CHAPTERS, url_ftell(pb));
    if (ret < 0) return ret;

    chapters     = start_ebml_master(pb, MATROSKA_ID_CHAPTERS    , 0);
    editionentry = start_ebml_master(pb, MATROSKA_ID_EDITIONENTRY, 0);
    put_ebml_uint(pb, MATROSKA_ID_EDITIONFLAGDEFAULT, 1);
    put_ebml_uint(pb, MATROSKA_ID_EDITIONFLAGHIDDEN , 0);
    for (i = 0; i < s->nb_chapters; i++) {
        ebml_master chapteratom, chapterdisplay;
        AVChapter *c     = s->chapters[i];
        AVMetadataTag *t = NULL;

        chapteratom = start_ebml_master(pb, MATROSKA_ID_CHAPTERATOM, 0);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERUID, c->id);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMESTART,
                      av_rescale_q(c->start, c->time_base, scale));
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERTIMEEND,
                      av_rescale_q(c->end,   c->time_base, scale));
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERFLAGHIDDEN , 0);
        put_ebml_uint(pb, MATROSKA_ID_CHAPTERFLAGENABLED, 1);
        if ((t = av_metadata_get(c->metadata, "title", NULL, 0))) {
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

static int mkv_write_header(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = s->pb;
    ebml_master ebml_header, segment_info;
    AVMetadataTag *tag;
    int ret;

    if (!strcmp(s->oformat->name, "webm")) mkv->mode = MODE_WEBM;
    else                                   mkv->mode = MODE_MATROSKAv2;

    mkv->md5_ctx = av_mallocz(av_md5_size);
    av_md5_init(mkv->md5_ctx);
    mkv->tracks = av_mallocz(s->nb_streams * sizeof(*mkv->tracks));

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
    mkv->segment_offset = url_ftell(pb);

    // we write 2 seek heads - one at the end of the file to point to each
    // cluster, and one at the beginning to point to all other level one
    // elements (including the seek head at the end of the file), which
    // isn't more than 10 elements if we only write one of each other
    // currently defined level 1 element
    mkv->main_seekhead    = mkv_start_seekhead(pb, mkv->segment_offset, 10);
    mkv->cluster_seekhead = mkv_start_seekhead(pb, mkv->segment_offset, 0);
    if (mkv->main_seekhead == NULL || mkv->cluster_seekhead == NULL)
        return AVERROR(ENOMEM);

    ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_INFO, url_ftell(pb));
    if (ret < 0) return ret;

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO, 0);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if ((tag = av_metadata_get(s->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TITLE, tag->value);
    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP , LIBAVFORMAT_IDENT);
        put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);

        // reserve space to write the segment UID later
        mkv->segment_uid = url_ftell(pb);
        put_ebml_void(pb, 19);
    }

    // reserve space for the duration
    mkv->duration = 0;
    mkv->duration_offset = url_ftell(pb);
    put_ebml_void(pb, 11);                  // assumes double-precision float to be written
    end_ebml_master(pb, segment_info);

    ret = mkv_write_tracks(s);
    if (ret < 0) return ret;

    if (mkv->mode != MODE_WEBM) {
        ret = mkv_write_chapters(s);
        if (ret < 0) return ret;
    }

    if (url_is_streamed(s->pb))
        mkv_write_seekhead(pb, mkv->main_seekhead);

    mkv->cues = mkv_start_cues(mkv->segment_offset);
    if (mkv->cues == NULL)
        return AVERROR(ENOMEM);

    put_flush_packet(pb);
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
    start = 3600000*sh + 60000*sm + 1000*ss + 10*sc;
    end   = 3600000*eh + 60000*em + 1000*es + 10*ec;
    return end - start;
}

static int mkv_write_ass_blocks(AVFormatContext *s, ByteIOContext *pb, AVPacket *pkt)
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
                     s->streams[pkt->stream_index]->nb_frames++, layer);
        size = FFMIN(i+size, sizeof(buffer));
        memcpy(buffer+i, start, size-i);

        av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
               "pts %" PRId64 ", duration %d\n",
               url_ftell(pb), size, pkt->pts, duration);
        blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(size));
        put_ebml_id(pb, MATROSKA_ID_BLOCK);
        put_ebml_num(pb, size+4, 0);
        put_byte(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
        put_be16(pb, pkt->pts - mkv->cluster_pts);
        put_byte(pb, 0);
        put_buffer(pb, buffer, size);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
        end_ebml_master(pb, blockgroup);

        data += line_size;
        data_size -= line_size;
    }

    return max_duration;
}

static void mkv_write_block(AVFormatContext *s, ByteIOContext *pb,
                            unsigned int blockid, AVPacket *pkt, int flags)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    uint8_t *data = NULL;
    int size = pkt->size;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;

    av_log(s, AV_LOG_DEBUG, "Writing block at offset %" PRIu64 ", size %d, "
           "pts %" PRId64 ", dts %" PRId64 ", duration %d, flags %d\n",
           url_ftell(pb), pkt->size, pkt->pts, pkt->dts, pkt->duration, flags);
    if (codec->codec_id == CODEC_ID_H264 && codec->extradata_size > 0 &&
        (AV_RB24(codec->extradata) == 1 || AV_RB32(codec->extradata) == 1))
        ff_avc_parse_nal_units_buf(pkt->data, &data, &size);
    else
        data = pkt->data;
    put_ebml_id(pb, blockid);
    put_ebml_num(pb, size+4, 0);
    put_byte(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
    put_be16(pb, ts - mkv->cluster_pts);
    put_byte(pb, flags);
    put_buffer(pb, data, size);
    if (data != pkt->data)
        av_free(data);
}

static void mkv_flush_dynbuf(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int bufsize;
    uint8_t *dyn_buf;

    if (!mkv->dyn_bc)
        return;

    bufsize = url_close_dyn_buf(mkv->dyn_bc, &dyn_buf);
    put_buffer(s->pb, dyn_buf, bufsize);
    av_free(dyn_buf);
    mkv->dyn_bc = NULL;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    int keyframe = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int duration = pkt->duration;
    int ret;
    int64_t ts = mkv->tracks[pkt->stream_index].write_dts ? pkt->dts : pkt->pts;

    if (ts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_ERROR, "Can't write packet with unknown timestamp\n");
        return AVERROR(EINVAL);
    }

    if (url_is_streamed(s->pb)) {
        if (!mkv->dyn_bc)
            url_open_dyn_buf(&mkv->dyn_bc);
        pb = mkv->dyn_bc;
    }

    if (!mkv->cluster_pos) {
        ret = mkv_add_seekhead_entry(mkv->cluster_seekhead, MATROSKA_ID_CLUSTER, url_ftell(pb));
        if (ret < 0) return ret;

        mkv->cluster_pos = url_ftell(s->pb);
        mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER, 0);
        put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, FFMAX(0, ts));
        mkv->cluster_pts = FFMAX(0, ts);
        av_md5_update(mkv->md5_ctx, pkt->data, FFMIN(200, pkt->size));
    }

    if (codec->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        mkv_write_block(s, pb, MATROSKA_ID_SIMPLEBLOCK, pkt, keyframe << 7);
    } else if (codec->codec_id == CODEC_ID_SSA) {
        duration = mkv_write_ass_blocks(s, pb, pkt);
    } else {
        ebml_master blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, mkv_blockgroup_size(pkt->size));
        duration = pkt->convergence_duration;
        mkv_write_block(s, pb, MATROSKA_ID_BLOCK, pkt, 0);
        put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, duration);
        end_ebml_master(pb, blockgroup);
    }

    if (codec->codec_type == AVMEDIA_TYPE_VIDEO && keyframe) {
        ret = mkv_add_cuepoint(mkv->cues, pkt->stream_index, ts, mkv->cluster_pos);
        if (ret < 0) return ret;
    }

    // start a new cluster every 5 MB or 5 sec, or 32k / 1 sec for streaming
    if ((url_is_streamed(s->pb) && (url_ftell(pb) > 32*1024 || ts > mkv->cluster_pts + 1000))
        ||  url_ftell(pb) > mkv->cluster_pos + 5*1024*1024 || ts > mkv->cluster_pts + 5000) {
        av_log(s, AV_LOG_DEBUG, "Starting new cluster at offset %" PRIu64
               " bytes, pts %" PRIu64 "\n", url_ftell(pb), ts);
        end_ebml_master(pb, mkv->cluster);
        mkv->cluster_pos = 0;
        if (mkv->dyn_bc)
            mkv_flush_dynbuf(s);
    }

    mkv->duration = FFMAX(mkv->duration, ts + duration);
    return 0;
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t currentpos, second_seekhead, cuespos;
    int ret;

    if (mkv->dyn_bc) {
        end_ebml_master(mkv->dyn_bc, mkv->cluster);
        mkv_flush_dynbuf(s);
    } else if (mkv->cluster_pos) {
        end_ebml_master(pb, mkv->cluster);
    }

    if (!url_is_streamed(pb)) {
        cuespos = mkv_write_cues(pb, mkv->cues, s->nb_streams);
        second_seekhead = mkv_write_seekhead(pb, mkv->cluster_seekhead);

        ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CUES    , cuespos);
        if (ret < 0) return ret;
        if (second_seekhead >= 0) {
            ret = mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_SEEKHEAD, second_seekhead);
            if (ret < 0) return ret;
        }
        mkv_write_seekhead(pb, mkv->main_seekhead);

        // update the duration
        av_log(s, AV_LOG_DEBUG, "end duration = %" PRIu64 "\n", mkv->duration);
        currentpos = url_ftell(pb);
        url_fseek(pb, mkv->duration_offset, SEEK_SET);
        put_ebml_float(pb, MATROSKA_ID_DURATION, mkv->duration);

        // write the md5sum of some frames as the segment UID
        if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
            uint8_t segment_uid[16];
            av_md5_final(mkv->md5_ctx, segment_uid);
            url_fseek(pb, mkv->segment_uid, SEEK_SET);
            put_ebml_binary(pb, MATROSKA_ID_SEGMENTUID, segment_uid, 16);
        }
        url_fseek(pb, currentpos, SEEK_SET);
    }

    end_ebml_master(pb, mkv->segment);
    av_free(mkv->md5_ctx);
    av_free(mkv->tracks);
    put_flush_packet(pb);
    return 0;
}

#if CONFIG_MATROSKA_MUXER
AVOutputFormat matroska_muxer = {
    "matroska",
    NULL_IF_CONFIG_SMALL("Matroska file format"),
    "video/x-matroska",
    "mkv",
    sizeof(MatroskaMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG4,
    mkv_write_header,
    mkv_write_packet,
    mkv_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
    .codec_tag = (const AVCodecTag* const []){ff_codec_bmp_tags, ff_codec_wav_tags, 0},
    .subtitle_codec = CODEC_ID_TEXT,
};
#endif

#if CONFIG_WEBM_MUXER
AVOutputFormat webm_muxer = {
    "webm",
    NULL_IF_CONFIG_SMALL("WebM file format"),
    "video/webm",
    "webm",
    sizeof(MatroskaMuxContext),
    CODEC_ID_VORBIS,
    CODEC_ID_VP8,
    mkv_write_header,
    mkv_write_packet,
    mkv_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
};
#endif

#if CONFIG_MATROSKA_AUDIO_MUXER
AVOutputFormat matroska_audio_muxer = {
    "matroska",
    NULL_IF_CONFIG_SMALL("Matroska file format"),
    "audio/x-matroska",
    "mka",
    sizeof(MatroskaMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_NONE,
    mkv_write_header,
    mkv_write_packet,
    mkv_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag = (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif
