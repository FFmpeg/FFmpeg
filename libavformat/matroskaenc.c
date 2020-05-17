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

#include "av1.h"
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
#include "vorbiscomment.h"
#include "wv.h"

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"
#include "libavutil/stereo3d.h"

#include "libavcodec/xiph.h"
#include "libavcodec/mpeg4audio.h"

/* Level 1 elements we create a SeekHead entry for:
 * Info, Tracks, Chapters, Attachments, Tags (potentially twice) and Cues */
#define MAX_SEEKHEAD_ENTRIES 7

#define IS_SEEKABLE(pb, mkv) (((pb)->seekable & AVIO_SEEKABLE_NORMAL) && \
                              !(mkv)->is_live)

enum {
    DEFAULT_MODE_INFER,
    DEFAULT_MODE_INFER_NO_SUBS,
    DEFAULT_MODE_PASSTHROUGH,
};

typedef struct ebml_master {
    int64_t         pos;                ///< absolute offset in the containing AVIOContext where the master's elements start
    int             sizebytes;          ///< how many bytes were reserved for the size
} ebml_master;

typedef struct ebml_stored_master {
    AVIOContext    *bc;
    int64_t         pos;
} ebml_stored_master;

typedef struct mkv_seekhead_entry {
    uint32_t        elementid;
    uint64_t        segmentpos;
} mkv_seekhead_entry;

typedef struct mkv_seekhead {
    int64_t                 filepos;
    mkv_seekhead_entry      entries[MAX_SEEKHEAD_ENTRIES];
    int                     num_entries;
    int                     reserved_size;
} mkv_seekhead;

typedef struct mkv_cuepoint {
    uint64_t        pts;
    int             stream_idx;
    int64_t         cluster_pos;        ///< offset of the cluster containing the block relative to the segment
    int64_t         relative_pos;       ///< relative offset from the position of the cluster containing the block
    int64_t         duration;           ///< duration of the block according to time base
} mkv_cuepoint;

typedef struct mkv_cues {
    mkv_cuepoint   *entries;
    int             num_entries;
} mkv_cues;

typedef struct mkv_track {
    int             write_dts;
    int             has_cue;
    uint64_t        uid;
    unsigned        track_num;
    int             track_num_size;
    int             sample_rate;
    int64_t         sample_rate_offset;
    int64_t         last_timestamp;
    int64_t         duration;
    int64_t         duration_offset;
    int64_t         codecpriv_offset;
    int64_t         ts_offset;
} mkv_track;

#define MODE_MATROSKAv2 0x01
#define MODE_WEBM       0x02

typedef struct MatroskaMuxContext {
    const AVClass      *class;
    int                 mode;
    ebml_stored_master  info;
    ebml_stored_master  track;
    ebml_stored_master  tags;
    int64_t             segment_offset;
    AVIOContext        *cluster_bc;
    int64_t             cluster_pos;    ///< file offset of the current Cluster
    int64_t             cluster_pts;
    int64_t             duration_offset;
    int64_t             duration;
    mkv_track          *tracks;
    mkv_seekhead        seekhead;
    mkv_cues            cues;
    int64_t             cues_pos;

    AVPacket            cur_audio_pkt;

    unsigned            nb_attachments;
    int                 have_video;

    int                 wrote_chapters;
    int                 wrote_tags;

    int                 reserve_cues_space;
    int                 cluster_size_limit;
    int64_t             cluster_time_limit;
    int                 write_crc;
    int                 is_live;

    int                 is_dash;
    int                 dash_track_number;
    int                 allow_raw_vfw;
    int                 default_mode;

    uint32_t            segment_uid[4];
} MatroskaMuxContext;

/** 2 bytes * 7 for EBML IDs, 7 1-byte EBML lengths, 6 1-byte uint,
 * 8 byte for "matroska" doctype string */
#define MAX_EBML_HEADER_SIZE 35

/** 2 bytes * 3 for EBML IDs, 3 1-byte EBML lengths, 8 bytes for 64 bit
 * offset, 4 bytes for target EBML ID */
#define MAX_SEEKENTRY_SIZE 21

/** 4 * (1-byte EBML ID, 1-byte EBML size, 8-byte uint max) */
#define MAX_CUETRACKPOS_SIZE 40

/** Seek preroll value for opus */
#define OPUS_SEEK_PREROLL 80000000

static int ebml_id_size(uint32_t id)
{
    return (av_log2(id) + 7U) / 8;
}

static void put_ebml_id(AVIOContext *pb, uint32_t id)
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
 * Returns how many bytes are needed to represent a number
 * as EBML variable length integer.
 */
static int ebml_num_size(uint64_t num)
{
    int bytes = 0;
    do {
        bytes++;
    } while (num >>= 7);
    return bytes;
}

/**
 * Calculate how many bytes are needed to represent the length field
 * of an EBML element whose payload has a given length.
 */
static int ebml_length_size(uint64_t length)
{
    return ebml_num_size(length + 1);
}

/**
 * Write a number as EBML variable length integer on `bytes` bytes.
 * `bytes` is taken literally without checking.
 */
static void put_ebml_num(AVIOContext *pb, uint64_t num, int bytes)
{
    num |= 1ULL << bytes * 7;
    for (int i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(num >> i * 8));
}

/**
 * Write a length as EBML variable length integer.
 *
 * @param bytes The number of bytes that need to be used to write the number.
 *              If zero, the minimal number of bytes will be used.
 */
static void put_ebml_length(AVIOContext *pb, uint64_t length, int bytes)
{
    int needed_bytes = ebml_length_size(length);

    // sizes larger than this are currently undefined in EBML
    av_assert0(length < (1ULL << 56) - 1);

    if (bytes == 0)
        bytes = needed_bytes;
    // The bytes needed to write the given size must not exceed
    // the bytes that we ought to use.
    av_assert0(bytes >= needed_bytes);
    put_ebml_num(pb, length, bytes);
}

/**
 * Write a (random) UID with fixed size to make the output more deterministic
 */
static void put_ebml_uid(AVIOContext *pb, uint32_t elementid, uint64_t uid)
{
    put_ebml_id(pb, elementid);
    put_ebml_length(pb, 8, 0);
    avio_wb64(pb, uid);
}

static void put_ebml_uint(AVIOContext *pb, uint32_t elementid, uint64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = val;
    while (tmp >>= 8)
        bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_length(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(val >> i * 8));
}

static void put_ebml_sint(AVIOContext *pb, uint32_t elementid, int64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = 2*(val < 0 ? val^-1 : val);

    while (tmp >>= 8)
        bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_length(pb, bytes, 0);
    for (i = bytes - 1; i >= 0; i--)
        avio_w8(pb, (uint8_t)(val >> i * 8));
}

static void put_ebml_float(AVIOContext *pb, uint32_t elementid, double val)
{
    put_ebml_id(pb, elementid);
    put_ebml_length(pb, 8, 0);
    avio_wb64(pb, av_double2int(val));
}

static void put_ebml_binary(AVIOContext *pb, uint32_t elementid,
                            const void *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_length(pb, size, 0);
    avio_write(pb, buf, size);
}

static void put_ebml_string(AVIOContext *pb, uint32_t elementid,
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
static void put_ebml_void(AVIOContext *pb, int size)
{
    av_assert0(size >= 2);

    put_ebml_id(pb, EBML_ID_VOID);
    // we need to subtract the length needed to store the size from the
    // size we need to reserve so 2 cases, we use 8 bytes to store the
    // size if possible, 1 byte otherwise
    if (size < 10) {
        size -= 2;
        put_ebml_length(pb, size, 0);
    } else {
        size -= 9;
        put_ebml_length(pb, size, 8);
    }
    ffio_fill(pb, 0, size);
}

static ebml_master start_ebml_master(AVIOContext *pb, uint32_t elementid,
                                     uint64_t expectedsize)
{
    int bytes = expectedsize ? ebml_length_size(expectedsize) : 8;

    put_ebml_id(pb, elementid);
    put_ebml_size_unknown(pb, bytes);
    return (ebml_master) { avio_tell(pb), bytes };
}

static void end_ebml_master(AVIOContext *pb, ebml_master master)
{
    int64_t pos = avio_tell(pb);

    if (avio_seek(pb, master.pos - master.sizebytes, SEEK_SET) < 0)
        return;
    put_ebml_length(pb, pos - master.pos, master.sizebytes);
    avio_seek(pb, pos, SEEK_SET);
}

static void mkv_add_seekhead_entry(MatroskaMuxContext *mkv, uint32_t elementid,
                                   uint64_t filepos)
{
    mkv_seekhead *seekhead = &mkv->seekhead;

    av_assert1(seekhead->num_entries < MAX_SEEKHEAD_ENTRIES);

    seekhead->entries[seekhead->num_entries].elementid    = elementid;
    seekhead->entries[seekhead->num_entries++].segmentpos = filepos - mkv->segment_offset;
}

static int start_ebml_master_crc32(AVIOContext **dyn_cp, MatroskaMuxContext *mkv)
{
    int ret;

    if (!*dyn_cp && (ret = avio_open_dyn_buf(dyn_cp)) < 0)
        return ret;

    if (mkv->write_crc)
        put_ebml_void(*dyn_cp, 6); /* Reserve space for CRC32 so position/size calculations using avio_tell() take it into account */

    return 0;
}

static int end_ebml_master_crc32(AVIOContext *pb, AVIOContext **dyn_cp,
                                 MatroskaMuxContext *mkv, uint32_t id,
                                 int length_size, int keep_buffer,
                                 int add_seekentry)
{
    uint8_t *buf, crc[4];
    int ret, size, skip = 0;

    size = avio_get_dyn_buf(*dyn_cp, &buf);
    if ((ret = (*dyn_cp)->error) < 0)
        goto fail;

    if (add_seekentry)
        mkv_add_seekhead_entry(mkv, id, avio_tell(pb));

    put_ebml_id(pb, id);
    put_ebml_length(pb, size, length_size);
    if (mkv->write_crc) {
        skip = 6; /* Skip reserved 6-byte long void element from the dynamic buffer. */
        AV_WL32(crc, av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), UINT32_MAX, buf + skip, size - skip) ^ UINT32_MAX);
        put_ebml_binary(pb, EBML_ID_CRC32, crc, sizeof(crc));
    }
    avio_write(pb, buf + skip, size - skip);

fail:
    if (keep_buffer) {
        ffio_reset_dyn_buf(*dyn_cp);
    } else {
        ffio_free_dyn_buf(dyn_cp);
    }
    return ret;
}

/**
 * Output EBML master. Keep the buffer if seekable, allowing for later updates.
 * Furthermore always add a SeekHead Entry for this element.
 */
static int end_ebml_master_crc32_tentatively(AVIOContext *pb,
                                             ebml_stored_master *elem,
                                             MatroskaMuxContext *mkv, uint32_t id)
{
    if (IS_SEEKABLE(pb, mkv)) {
        uint8_t *buf;
        int size = avio_get_dyn_buf(elem->bc, &buf);

        if (elem->bc->error < 0)
            return elem->bc->error;

        elem->pos = avio_tell(pb);
        mkv_add_seekhead_entry(mkv, id, elem->pos);

        put_ebml_id(pb, id);
        put_ebml_length(pb, size, 0);
        avio_write(pb, buf, size);

        return 0;
    } else
        return end_ebml_master_crc32(pb, &elem->bc, mkv, id, 0, 0, 1);
}

static void put_xiph_size(AVIOContext *pb, int size)
{
    ffio_fill(pb, 255, size / 255);
    avio_w8(pb, size % 255);
}

/**
 * Free the members allocated in the mux context.
 */
static void mkv_deinit(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;

    av_packet_unref(&mkv->cur_audio_pkt);

    ffio_free_dyn_buf(&mkv->cluster_bc);
    ffio_free_dyn_buf(&mkv->info.bc);
    ffio_free_dyn_buf(&mkv->track.bc);
    ffio_free_dyn_buf(&mkv->tags.bc);

    av_freep(&mkv->cues.entries);
    av_freep(&mkv->tracks);
}

/**
 * Initialize the SeekHead element to be ready to index level 1 Matroska
 * elements. Enough space to write MAX_SEEKHEAD_ENTRIES SeekHead entries
 * will be reserved at the current file location.
 */
static void mkv_start_seekhead(MatroskaMuxContext *mkv, AVIOContext *pb)
{
    mkv->seekhead.filepos = avio_tell(pb);
    // 21 bytes max for a Seek entry, 6 bytes max for the SeekHead ID
    // and size, 6 bytes for a CRC32 element, and 2 bytes to guarantee
    // that an EBML void element will fit afterwards
    mkv->seekhead.reserved_size = MAX_SEEKHEAD_ENTRIES * MAX_SEEKENTRY_SIZE + 14;
    put_ebml_void(pb, mkv->seekhead.reserved_size);
}

/**
 * Write the SeekHead to the file at the location reserved for it
 * and seek to destpos afterwards. When error_on_seek_failure
 * is not set, failure to seek to the position designated for the
 * SeekHead is not considered an error and it is presumed that
 * destpos is the current position; failure to seek to destpos
 * afterwards is always an error.
 *
 * @return 0 on success, < 0 on error.
 */
static int mkv_write_seekhead(AVIOContext *pb, MatroskaMuxContext *mkv,
                              int error_on_seek_failure, int64_t destpos)
{
    AVIOContext *dyn_cp = NULL;
    mkv_seekhead *seekhead = &mkv->seekhead;
    int64_t remaining, ret64;
    int i, ret;

    if ((ret64 = avio_seek(pb, seekhead->filepos, SEEK_SET)) < 0)
        return error_on_seek_failure ? ret64 : 0;

    ret = start_ebml_master_crc32(&dyn_cp, mkv);
    if (ret < 0)
        return ret;

    for (i = 0; i < seekhead->num_entries; i++) {
        mkv_seekhead_entry *entry = &seekhead->entries[i];
        ebml_master seekentry = start_ebml_master(dyn_cp, MATROSKA_ID_SEEKENTRY,
                                                  MAX_SEEKENTRY_SIZE);

        put_ebml_id(dyn_cp, MATROSKA_ID_SEEKID);
        put_ebml_length(dyn_cp, ebml_id_size(entry->elementid), 0);
        put_ebml_id(dyn_cp, entry->elementid);

        put_ebml_uint(dyn_cp, MATROSKA_ID_SEEKPOSITION, entry->segmentpos);
        end_ebml_master(dyn_cp, seekentry);
    }
    ret = end_ebml_master_crc32(pb, &dyn_cp, mkv,
                                MATROSKA_ID_SEEKHEAD, 0, 0, 0);
    if (ret < 0)
        return ret;

    remaining = seekhead->filepos + seekhead->reserved_size - avio_tell(pb);
    put_ebml_void(pb, remaining);

    if ((ret64 = avio_seek(pb, destpos, SEEK_SET)) < 0)
        return ret64;

    return 0;
}

static int mkv_add_cuepoint(MatroskaMuxContext *mkv, int stream, int64_t ts,
                            int64_t cluster_pos, int64_t relative_pos, int64_t duration)
{
    mkv_cues *cues = &mkv->cues;
    mkv_cuepoint *entries = cues->entries;

    if (ts < 0)
        return 0;

    entries = av_realloc_array(entries, cues->num_entries + 1, sizeof(mkv_cuepoint));
    if (!entries)
        return AVERROR(ENOMEM);
    cues->entries = entries;

    cues->entries[cues->num_entries].pts           = ts;
    cues->entries[cues->num_entries].stream_idx    = stream;
    cues->entries[cues->num_entries].cluster_pos   = cluster_pos - mkv->segment_offset;
    cues->entries[cues->num_entries].relative_pos  = relative_pos;
    cues->entries[cues->num_entries++].duration    = duration;

    return 0;
}

static int mkv_assemble_cues(AVStream **streams, AVIOContext *dyn_cp,
                             mkv_cues *cues, mkv_track *tracks, int num_tracks)
{
    AVIOContext *cuepoint;
    int ret;

    ret = avio_open_dyn_buf(&cuepoint);
    if (ret < 0)
        return ret;

    for (mkv_cuepoint *entry = cues->entries, *end = entry + cues->num_entries;
         entry < end;) {
        uint64_t pts = entry->pts;
        uint8_t *buf;
        int size;

        put_ebml_uint(cuepoint, MATROSKA_ID_CUETIME, pts);

        // put all the entries from different tracks that have the exact same
        // timestamp into the same CuePoint
        for (int j = 0; j < num_tracks; j++)
            tracks[j].has_cue = 0;
        do {
            ebml_master track_positions;
            int idx = entry->stream_idx;

            av_assert0(idx >= 0 && idx < num_tracks);
            if (tracks[idx].has_cue && streams[idx]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
                continue;
            tracks[idx].has_cue = 1;
            track_positions = start_ebml_master(cuepoint, MATROSKA_ID_CUETRACKPOSITION, MAX_CUETRACKPOS_SIZE);
            put_ebml_uint(cuepoint, MATROSKA_ID_CUETRACK           , tracks[idx].track_num);
            put_ebml_uint(cuepoint, MATROSKA_ID_CUECLUSTERPOSITION , entry->cluster_pos);
            put_ebml_uint(cuepoint, MATROSKA_ID_CUERELATIVEPOSITION, entry->relative_pos);
            if (entry->duration != -1)
                put_ebml_uint(cuepoint, MATROSKA_ID_CUEDURATION    , entry->duration);
            end_ebml_master(cuepoint, track_positions);
        } while (++entry < end && entry->pts == pts);
        size = avio_get_dyn_buf(cuepoint, &buf);
        if ((ret = cuepoint->error) < 0)
            break;
        put_ebml_binary(dyn_cp, MATROSKA_ID_POINTENTRY, buf, size);
        ffio_reset_dyn_buf(cuepoint);
    }
    ffio_free_dyn_buf(&cuepoint);

    return ret;
}

static int put_xiph_codecpriv(AVFormatContext *s, AVIOContext *pb,
                              const AVCodecParameters *par)
{
    const uint8_t *header_start[3];
    int header_len[3];
    int first_header_size;
    int err, j;

    if (par->codec_id == AV_CODEC_ID_VORBIS)
        first_header_size = 30;
    else
        first_header_size = 42;

    err = avpriv_split_xiph_headers(par->extradata, par->extradata_size,
                                    first_header_size, header_start, header_len);
    if (err < 0) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
        return err;
    }

    avio_w8(pb, 2);                    // number packets - 1
    for (j = 0; j < 2; j++) {
        put_xiph_size(pb, header_len[j]);
    }
    for (j = 0; j < 3; j++)
        avio_write(pb, header_start[j], header_len[j]);

    return 0;
}

static int put_wv_codecpriv(AVIOContext *pb, const AVCodecParameters *par)
{
    if (par->extradata && par->extradata_size == 2)
        avio_write(pb, par->extradata, 2);
    else
        avio_wl16(pb, 0x410); // fallback to the most recent version
    return 0;
}

static int put_flac_codecpriv(AVFormatContext *s, AVIOContext *pb,
                              const AVCodecParameters *par)
{
    int write_comment = (par->channel_layout &&
                         !(par->channel_layout & ~0x3ffffULL) &&
                         !ff_flac_is_native_layout(par->channel_layout));
    int ret = ff_flac_write_header(pb, par->extradata, par->extradata_size,
                                   !write_comment);

    if (ret < 0)
        return ret;

    if (write_comment) {
        const char *vendor = (s->flags & AVFMT_FLAG_BITEXACT) ?
                             "Lavf" : LIBAVFORMAT_IDENT;
        AVDictionary *dict = NULL;
        uint8_t buf[32];
        int64_t len;

        snprintf(buf, sizeof(buf), "0x%"PRIx64, par->channel_layout);
        av_dict_set(&dict, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", buf, 0);

        len = ff_vorbiscomment_length(dict, vendor, NULL, 0);
        av_assert1(len < (1 << 24) - 4);

        avio_w8(pb, 0x84);
        avio_wb24(pb, len);

        ff_vorbiscomment_write(pb, dict, vendor, NULL, 0);

        av_dict_free(&dict);
    }

    return 0;
}

static int get_aac_sample_rates(AVFormatContext *s, MatroskaMuxContext *mkv,
                                const uint8_t *extradata, int extradata_size,
                                int *sample_rate, int *output_sample_rate)
{
    MPEG4AudioConfig mp4ac;
    int ret;

    ret = avpriv_mpeg4audio_get_config2(&mp4ac, extradata, extradata_size, 1, s);
    /* Don't abort if the failure is because of missing extradata. Assume in that
     * case a bitstream filter will provide the muxer with the extradata in the
     * first packet.
     * Abort however if s->pb is not seekable, as we would not be able to seek back
     * to write the sample rate elements once the extradata shows up, anyway. */
    if (ret < 0 && (extradata_size || !IS_SEEKABLE(s->pb, mkv))) {
        av_log(s, AV_LOG_ERROR,
               "Error parsing AAC extradata, unable to determine samplerate.\n");
        return AVERROR(EINVAL);
    }

    if (ret < 0) {
        /* This will only happen when this function is called while writing the
         * header and no extradata is available. The space for this element has
         * to be reserved for when this function is called again after the
         * extradata shows up in the first packet, as there's no way to know if
         * output_sample_rate will be different than sample_rate or not. */
        *output_sample_rate = *sample_rate;
    } else {
        *sample_rate        = mp4ac.sample_rate;
        *output_sample_rate = mp4ac.ext_sample_rate;
    }
    return 0;
}

static int mkv_write_native_codecprivate(AVFormatContext *s, AVIOContext *pb,
                                         const AVCodecParameters *par,
                                         AVIOContext *dyn_cp)
{
    switch (par->codec_id) {
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_THEORA:
        return put_xiph_codecpriv(s, dyn_cp, par);
    case AV_CODEC_ID_FLAC:
        return put_flac_codecpriv(s, dyn_cp, par);
    case AV_CODEC_ID_WAVPACK:
        return put_wv_codecpriv(dyn_cp, par);
    case AV_CODEC_ID_H264:
        return ff_isom_write_avcc(dyn_cp, par->extradata,
                                  par->extradata_size);
    case AV_CODEC_ID_HEVC:
        return ff_isom_write_hvcc(dyn_cp, par->extradata,
                                  par->extradata_size, 0);
    case AV_CODEC_ID_AV1:
        if (par->extradata_size)
            return ff_isom_write_av1c(dyn_cp, par->extradata,
                                      par->extradata_size);
        else
            put_ebml_void(pb, 4 + 3);
        break;
    case AV_CODEC_ID_ALAC:
        if (par->extradata_size < 36) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid extradata found, ALAC expects a 36-byte "
                   "QuickTime atom.");
            return AVERROR_INVALIDDATA;
        } else
            avio_write(dyn_cp, par->extradata + 12,
                       par->extradata_size - 12);
        break;
    case AV_CODEC_ID_AAC:
        if (par->extradata_size)
            avio_write(dyn_cp, par->extradata, par->extradata_size);
        else
            put_ebml_void(pb, MAX_PCE_SIZE + 2 + 4);
        break;
    default:
        if (par->codec_id == AV_CODEC_ID_PRORES &&
            ff_codec_get_id(ff_codec_movvideo_tags, par->codec_tag) == AV_CODEC_ID_PRORES) {
            avio_wl32(dyn_cp, par->codec_tag);
        } else if (par->extradata_size && par->codec_id != AV_CODEC_ID_TTA)
            avio_write(dyn_cp, par->extradata, par->extradata_size);
    }

    return 0;
}

static int mkv_write_codecprivate(AVFormatContext *s, AVIOContext *pb,
                                  AVCodecParameters *par,
                                  int native_id, int qt_id)
{
    AVIOContext *dyn_cp;
    uint8_t *codecpriv;
    int ret, codecpriv_size;

    ret = avio_open_dyn_buf(&dyn_cp);
    if (ret < 0)
        return ret;

    if (native_id) {
        ret = mkv_write_native_codecprivate(s, pb, par, dyn_cp);
    } else if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (qt_id) {
            if (!par->codec_tag)
                par->codec_tag = ff_codec_get_tag(ff_codec_movvideo_tags,
                                                    par->codec_id);
            if (   ff_codec_get_id(ff_codec_movvideo_tags, par->codec_tag) == par->codec_id
                && (!par->extradata_size || ff_codec_get_id(ff_codec_movvideo_tags, AV_RL32(par->extradata + 4)) != par->codec_id)
            ) {
                int i;
                avio_wb32(dyn_cp, 0x5a + par->extradata_size);
                avio_wl32(dyn_cp, par->codec_tag);
                for(i = 0; i < 0x5a - 8; i++)
                    avio_w8(dyn_cp, 0);
            }
            avio_write(dyn_cp, par->extradata, par->extradata_size);
        } else {
            if (!ff_codec_get_tag(ff_codec_bmp_tags, par->codec_id))
                av_log(s, AV_LOG_WARNING, "codec %s is not supported by this format\n",
                       avcodec_get_name(par->codec_id));

            if (!par->codec_tag)
                par->codec_tag = ff_codec_get_tag(ff_codec_bmp_tags,
                                                  par->codec_id);
            if (!par->codec_tag && par->codec_id != AV_CODEC_ID_RAWVIDEO) {
                av_log(s, AV_LOG_ERROR, "No bmp codec tag found for codec %s\n",
                       avcodec_get_name(par->codec_id));
                ret = AVERROR(EINVAL);
            }

            ff_put_bmp_header(dyn_cp, par, 0, 0);
        }
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        unsigned int tag;
        tag = ff_codec_get_tag(ff_codec_wav_tags, par->codec_id);
        if (!tag) {
            av_log(s, AV_LOG_ERROR, "No wav codec tag found for codec %s\n",
                   avcodec_get_name(par->codec_id));
            ret = AVERROR(EINVAL);
        }
        if (!par->codec_tag)
            par->codec_tag = tag;

        ff_put_wav_header(s, dyn_cp, par, FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX);
    }

    if (ret >= 0) {
        codecpriv_size = avio_get_dyn_buf(dyn_cp, &codecpriv);
        if ((ret = dyn_cp->error) >= 0 && codecpriv_size)
            put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codecpriv,
                            codecpriv_size);
    }
    ffio_free_dyn_buf(&dyn_cp);
    return ret;
}

static void mkv_write_video_color(AVIOContext *pb, const AVStream *st,
                                  const AVCodecParameters *par)
{
    /* 18 Elements with two bytes ID, one byte length field, 8 bytes payload
     * a master element with two bytes ID and one byte length field
     * plus another byte to stay clear of the end. */
    uint8_t colour[(2 + 1 + 8) * 18 + (2 + 1) + 1];
    AVIOContext buf, *dyn_cp = &buf;
    int colorinfo_size;
    const void *side_data;

    ffio_init_context(dyn_cp, colour, sizeof(colour), 1, NULL, NULL, NULL, NULL);

    if (par->color_trc != AVCOL_TRC_UNSPECIFIED &&
        par->color_trc < AVCOL_TRC_NB) {
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORTRANSFERCHARACTERISTICS,
                      par->color_trc);
    }
    if (par->color_space != AVCOL_SPC_UNSPECIFIED &&
        par->color_space < AVCOL_SPC_NB) {
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORMATRIXCOEFF, par->color_space);
    }
    if (par->color_primaries != AVCOL_PRI_UNSPECIFIED &&
        par->color_primaries < AVCOL_PRI_NB) {
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORPRIMARIES, par->color_primaries);
    }
    if (par->color_range != AVCOL_RANGE_UNSPECIFIED &&
        par->color_range < AVCOL_RANGE_NB) {
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORRANGE, par->color_range);
    }
    if (par->chroma_location != AVCHROMA_LOC_UNSPECIFIED &&
        par->chroma_location <= AVCHROMA_LOC_TOP) {
        int xpos, ypos;

        avcodec_enum_to_chroma_pos(&xpos, &ypos, par->chroma_location);
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORCHROMASITINGHORZ, (xpos >> 7) + 1);
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORCHROMASITINGVERT, (ypos >> 7) + 1);
    }

    side_data = av_stream_get_side_data(st, AV_PKT_DATA_CONTENT_LIGHT_LEVEL,
                                        NULL);
    if (side_data) {
        const AVContentLightMetadata *metadata = side_data;
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORMAXCLL,  metadata->MaxCLL);
        put_ebml_uint(dyn_cp, MATROSKA_ID_VIDEOCOLORMAXFALL, metadata->MaxFALL);
    }

    side_data = av_stream_get_side_data(st, AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
                                        NULL);
    if (side_data) {
        ebml_master meta_element = start_ebml_master(
            dyn_cp, MATROSKA_ID_VIDEOCOLORMASTERINGMETA, 10 * (2 + 1 + 8));
        const AVMasteringDisplayMetadata *metadata = side_data;
        if (metadata->has_primaries) {
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_RX,
                           av_q2d(metadata->display_primaries[0][0]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_RY,
                           av_q2d(metadata->display_primaries[0][1]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_GX,
                           av_q2d(metadata->display_primaries[1][0]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_GY,
                           av_q2d(metadata->display_primaries[1][1]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_BX,
                           av_q2d(metadata->display_primaries[2][0]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_BY,
                           av_q2d(metadata->display_primaries[2][1]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_WHITEX,
                           av_q2d(metadata->white_point[0]));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_WHITEY,
                           av_q2d(metadata->white_point[1]));
        }
        if (metadata->has_luminance) {
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_LUMINANCEMAX,
                           av_q2d(metadata->max_luminance));
            put_ebml_float(dyn_cp, MATROSKA_ID_VIDEOCOLOR_LUMINANCEMIN,
                           av_q2d(metadata->min_luminance));
        }
        end_ebml_master(dyn_cp, meta_element);
    }

    colorinfo_size = avio_tell(dyn_cp);
    if (colorinfo_size)
        put_ebml_binary(pb, MATROSKA_ID_VIDEOCOLOR, colour, colorinfo_size);
}

static void mkv_write_video_projection(AVFormatContext *s, AVIOContext *pb,
                                       const AVStream *st)
{
    ebml_master projection;
    uint8_t private[20];

    const AVSphericalMapping *spherical =
        (const AVSphericalMapping *)av_stream_get_side_data(st, AV_PKT_DATA_SPHERICAL,
                                                            NULL);

    if (!spherical)
        return;

    if (spherical->projection != AV_SPHERICAL_EQUIRECTANGULAR      &&
        spherical->projection != AV_SPHERICAL_EQUIRECTANGULAR_TILE &&
        spherical->projection != AV_SPHERICAL_CUBEMAP) {
        av_log(s, AV_LOG_WARNING, "Unknown projection type\n");
        return;
    }

    // Maximally 4 8-byte elements with id-length 2 + 1 byte length field
    // and the private data of the AV_SPHERICAL_EQUIRECTANGULAR_TILE case
    projection = start_ebml_master(pb, MATROSKA_ID_VIDEOPROJECTION,
                                   4 * (2 + 1 + 8) + (2 + 1 + 20));

    switch (spherical->projection) {
    case AV_SPHERICAL_EQUIRECTANGULAR:
        put_ebml_uint(pb, MATROSKA_ID_VIDEOPROJECTIONTYPE,
                      MATROSKA_VIDEO_PROJECTION_TYPE_EQUIRECTANGULAR);
        break;
    case AV_SPHERICAL_EQUIRECTANGULAR_TILE:
        put_ebml_uint(pb, MATROSKA_ID_VIDEOPROJECTIONTYPE,
                      MATROSKA_VIDEO_PROJECTION_TYPE_EQUIRECTANGULAR);
        AV_WB32(private,      0); // version + flags
        AV_WB32(private +  4, spherical->bound_top);
        AV_WB32(private +  8, spherical->bound_bottom);
        AV_WB32(private + 12, spherical->bound_left);
        AV_WB32(private + 16, spherical->bound_right);
        put_ebml_binary(pb, MATROSKA_ID_VIDEOPROJECTIONPRIVATE,
                        private, 20);
        break;
    case AV_SPHERICAL_CUBEMAP:
        put_ebml_uint(pb, MATROSKA_ID_VIDEOPROJECTIONTYPE,
                      MATROSKA_VIDEO_PROJECTION_TYPE_CUBEMAP);
        AV_WB32(private,     0); // version + flags
        AV_WB32(private + 4, 0); // layout
        AV_WB32(private + 8, spherical->padding);
        put_ebml_binary(pb, MATROSKA_ID_VIDEOPROJECTIONPRIVATE,
                        private, 12);
        break;
    default:
        av_assert0(0);
    }

    if (spherical->yaw)
        put_ebml_float(pb, MATROSKA_ID_VIDEOPROJECTIONPOSEYAW,
                       (double) spherical->yaw   / (1 << 16));
    if (spherical->pitch)
        put_ebml_float(pb, MATROSKA_ID_VIDEOPROJECTIONPOSEPITCH,
                       (double) spherical->pitch / (1 << 16));
    if (spherical->roll)
        put_ebml_float(pb, MATROSKA_ID_VIDEOPROJECTIONPOSEROLL,
                       (double) spherical->roll  / (1 << 16));

    end_ebml_master(pb, projection);
}

static void mkv_write_field_order(AVIOContext *pb, int mode,
                                  enum AVFieldOrder field_order)
{
    switch (field_order) {
    case AV_FIELD_UNKNOWN:
        break;
    case AV_FIELD_PROGRESSIVE:
        put_ebml_uint(pb, MATROSKA_ID_VIDEOFLAGINTERLACED,
                      MATROSKA_VIDEO_INTERLACE_FLAG_PROGRESSIVE);
        break;
    case AV_FIELD_TT:
    case AV_FIELD_BB:
    case AV_FIELD_TB:
    case AV_FIELD_BT:
        put_ebml_uint(pb, MATROSKA_ID_VIDEOFLAGINTERLACED,
                      MATROSKA_VIDEO_INTERLACE_FLAG_INTERLACED);
        if (mode != MODE_WEBM) {
            switch (field_order) {
            case AV_FIELD_TT:
                put_ebml_uint(pb, MATROSKA_ID_VIDEOFIELDORDER,
                              MATROSKA_VIDEO_FIELDORDER_TT);
                break;
            case AV_FIELD_BB:
                put_ebml_uint(pb, MATROSKA_ID_VIDEOFIELDORDER,
                              MATROSKA_VIDEO_FIELDORDER_BB);
                break;
            case AV_FIELD_TB:
                put_ebml_uint(pb, MATROSKA_ID_VIDEOFIELDORDER,
                              MATROSKA_VIDEO_FIELDORDER_TB);
                break;
            case AV_FIELD_BT:
                put_ebml_uint(pb, MATROSKA_ID_VIDEOFIELDORDER,
                              MATROSKA_VIDEO_FIELDORDER_BT);
                break;
            }
        }
    }
}

static int mkv_write_stereo_mode(AVFormatContext *s, AVIOContext *pb,
                                 AVStream *st, int mode, int *h_width, int *h_height)
{
    const AVDictionaryEntry *tag;
    MatroskaVideoStereoModeType format = MATROSKA_VIDEO_STEREOMODE_TYPE_NB;
    const AVStereo3D *stereo;

    *h_width = 1;
    *h_height = 1;
    // convert metadata into proper side data and add it to the stream
    if ((tag = av_dict_get(st->metadata, "stereo_mode", NULL, 0)) ||
        (tag = av_dict_get( s->metadata, "stereo_mode", NULL, 0))) {
        int stereo_mode = atoi(tag->value);

        for (int i = 0; i < MATROSKA_VIDEO_STEREOMODE_TYPE_NB; i++)
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

    stereo = (const AVStereo3D*)av_stream_get_side_data(st, AV_PKT_DATA_STEREO3D,
                                                        NULL);
    if (stereo) {
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
    }

    if (format == MATROSKA_VIDEO_STEREOMODE_TYPE_NB)
        return 0;

    // if webm, do not write unsupported modes
    if ((mode == MODE_WEBM &&
        format > MATROSKA_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM &&
        format != MATROSKA_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT)
        || format >= MATROSKA_VIDEO_STEREOMODE_TYPE_NB) {
        av_log(s, AV_LOG_ERROR,
               "The specified stereo mode is not valid.\n");
        return AVERROR(EINVAL);
    }

    // write StereoMode if format is valid
    put_ebml_uint(pb, MATROSKA_ID_VIDEOSTEREOMODE, format);

    return 0;
}

static int mkv_write_track(AVFormatContext *s, MatroskaMuxContext *mkv,
                           AVStream *st, mkv_track *track, AVIOContext *pb,
                           int is_default)
{
    AVCodecParameters *par = st->codecpar;
    ebml_master subinfo, track_master;
    int native_id = 0;
    int qt_id = 0;
    int bit_depth;
    int sample_rate = par->sample_rate;
    int output_sample_rate = 0;
    int display_width_div = 1;
    int display_height_div = 1;
    int j, ret;
    const AVDictionaryEntry *tag;

    if (par->codec_type == AVMEDIA_TYPE_ATTACHMENT)
        return 0;

    if (par->codec_id == AV_CODEC_ID_AAC) {
        ret = get_aac_sample_rates(s, mkv, par->extradata, par->extradata_size,
                                   &sample_rate, &output_sample_rate);
        if (ret < 0)
            return ret;
    }

    track_master = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY, 0);
    put_ebml_uint(pb, MATROSKA_ID_TRACKNUMBER, track->track_num);
    put_ebml_uid (pb, MATROSKA_ID_TRACKUID,    track->uid);
    put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGLACING, 0);    // no lacing (yet)

    if ((tag = av_dict_get(st->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TRACKNAME, tag->value);
    tag = av_dict_get(st->metadata, "language", NULL, 0);
    put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE,
                    tag && tag->value ? tag->value : "und");

    // The default value for TRACKFLAGDEFAULT is 1, so add element
    // if we need to clear it.
    if (!is_default)
        put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGDEFAULT, 0);

    if (st->disposition & AV_DISPOSITION_FORCED)
        put_ebml_uint(pb, MATROSKA_ID_TRACKFLAGFORCED, 1);

    if (mkv->mode == MODE_WEBM) {
        const char *codec_id;
        if (par->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            for (j = 0; ff_webm_codec_tags[j].id != AV_CODEC_ID_NONE; j++) {
                if (ff_webm_codec_tags[j].id == par->codec_id) {
                    codec_id = ff_webm_codec_tags[j].str;
                    native_id = 1;
                    break;
                }
            }
        } else if (par->codec_id == AV_CODEC_ID_WEBVTT) {
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
        }

        if (!native_id) {
            av_log(s, AV_LOG_ERROR,
                   "Only VP8 or VP9 or AV1 video and Vorbis or Opus audio and WebVTT subtitles are supported for WebM.\n");
            return AVERROR(EINVAL);
        }

        put_ebml_string(pb, MATROSKA_ID_CODECID, codec_id);
    } else {
        // look for a codec ID string specific to mkv to use,
        // if none are found, use AVI codes
        if (par->codec_id != AV_CODEC_ID_RAWVIDEO || par->codec_tag) {
            for (j = 0; ff_mkv_codec_tags[j].id != AV_CODEC_ID_NONE; j++) {
                if (ff_mkv_codec_tags[j].id == par->codec_id && par->codec_id != AV_CODEC_ID_FFV1) {
                    put_ebml_string(pb, MATROSKA_ID_CODECID, ff_mkv_codec_tags[j].str);
                    native_id = 1;
                    break;
                }
            }
        } else {
            if (mkv->allow_raw_vfw) {
                native_id = 0;
            } else {
                av_log(s, AV_LOG_ERROR, "Raw RGB is not supported Natively in Matroska, you can use AVI or NUT or\n"
                                        "If you would like to store it anyway using VFW mode, enable allow_raw_vfw (-allow_raw_vfw 1)\n");
                return AVERROR(EINVAL);
            }
        }
    }

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        mkv->have_video = 1;
        put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);

        if(   st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0
           && av_cmp_q(av_inv_q(st->avg_frame_rate), st->time_base) > 0)
            put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, 1000000000LL * st->avg_frame_rate.den / st->avg_frame_rate.num);
        else if(   st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0
                && av_cmp_q(av_inv_q(st->r_frame_rate), st->time_base) > 0)
            put_ebml_uint(pb, MATROSKA_ID_TRACKDEFAULTDURATION, 1000000000LL * st->r_frame_rate.den / st->r_frame_rate.num);

        if (!native_id &&
            ff_codec_get_tag(ff_codec_movvideo_tags, par->codec_id) &&
            ((!ff_codec_get_tag(ff_codec_bmp_tags,   par->codec_id) && par->codec_id != AV_CODEC_ID_RAWVIDEO) ||
             par->codec_id == AV_CODEC_ID_SVQ1 ||
             par->codec_id == AV_CODEC_ID_SVQ3 ||
             par->codec_id == AV_CODEC_ID_CINEPAK))
            qt_id = 1;

        if (qt_id)
            put_ebml_string(pb, MATROSKA_ID_CODECID, "V_QUICKTIME");
        else if (!native_id) {
            // if there is no mkv-specific codec ID, use VFW mode
            put_ebml_string(pb, MATROSKA_ID_CODECID, "V_MS/VFW/FOURCC");
            track->write_dts = 1;
            s->internal->avoid_negative_ts_use_pts = 0;
        }

        subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKVIDEO, 0);

        put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELWIDTH , par->width);
        put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELHEIGHT, par->height);

        mkv_write_field_order(pb, mkv->mode, par->field_order);

        // check both side data and metadata for stereo information,
        // write the result to the bitstream if any is found
        ret = mkv_write_stereo_mode(s, pb, st, mkv->mode,
                                    &display_width_div,
                                    &display_height_div);
        if (ret < 0)
            return ret;

        if (((tag = av_dict_get(st->metadata, "alpha_mode", NULL, 0)) && atoi(tag->value)) ||
            ((tag = av_dict_get( s->metadata, "alpha_mode", NULL, 0)) && atoi(tag->value)) ||
            (par->format == AV_PIX_FMT_YUVA420P)) {
            put_ebml_uint(pb, MATROSKA_ID_VIDEOALPHAMODE, 1);
        }

        // write DisplayWidth and DisplayHeight, they contain the size of
        // a single source view and/or the display aspect ratio
        if (st->sample_aspect_ratio.num) {
            int64_t d_width = av_rescale(par->width, st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
            if (d_width > INT_MAX) {
                av_log(s, AV_LOG_ERROR, "Overflow in display width\n");
                return AVERROR(EINVAL);
            }
            if (d_width != par->width || display_width_div != 1 || display_height_div != 1) {
                if (mkv->mode == MODE_WEBM || display_width_div != 1 || display_height_div != 1) {
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , d_width / display_width_div);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, par->height / display_height_div);
                } else {
                    AVRational display_aspect_ratio;
                    av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                              par->width  * (int64_t)st->sample_aspect_ratio.num,
                              par->height * (int64_t)st->sample_aspect_ratio.den,
                              1024 * 1024);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH,  display_aspect_ratio.num);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, display_aspect_ratio.den);
                    put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYUNIT, MATROSKA_VIDEO_DISPLAYUNIT_DAR);
                }
            }
        } else if (display_width_div != 1 || display_height_div != 1) {
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYWIDTH , par->width / display_width_div);
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYHEIGHT, par->height / display_height_div);
        } else if (mkv->mode != MODE_WEBM)
            put_ebml_uint(pb, MATROSKA_ID_VIDEODISPLAYUNIT, MATROSKA_VIDEO_DISPLAYUNIT_UNKNOWN);

        if (par->codec_id == AV_CODEC_ID_RAWVIDEO) {
            uint32_t color_space = av_le2ne32(par->codec_tag);
            put_ebml_binary(pb, MATROSKA_ID_VIDEOCOLORSPACE, &color_space, sizeof(color_space));
        }
        mkv_write_video_color(pb, st, par);
        mkv_write_video_projection(s, pb, st);

        end_ebml_master(pb, subinfo);
        break;

    case AVMEDIA_TYPE_AUDIO:
        if (par->initial_padding && par->codec_id == AV_CODEC_ID_OPUS) {
            int64_t codecdelay = av_rescale_q(par->initial_padding,
                                              (AVRational){ 1, 48000 },
                                              (AVRational){ 1, 1000000000 });
            if (codecdelay < 0) {
                av_log(s, AV_LOG_ERROR, "Initial padding is invalid\n");
                return AVERROR(EINVAL);
            }
//            track->ts_offset = av_rescale_q(par->initial_padding,
//                                            (AVRational){ 1, par->sample_rate },
//                                            st->time_base);

            put_ebml_uint(pb, MATROSKA_ID_CODECDELAY, codecdelay);
        }
        if (par->codec_id == AV_CODEC_ID_OPUS)
            put_ebml_uint(pb, MATROSKA_ID_SEEKPREROLL, OPUS_SEEK_PREROLL);

        put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_AUDIO);

        if (!native_id)
            // no mkv-specific ID, use ACM mode
            put_ebml_string(pb, MATROSKA_ID_CODECID, "A_MS/ACM");

        subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKAUDIO, 6 + 4 * 9);
        put_ebml_uint  (pb, MATROSKA_ID_AUDIOCHANNELS    , par->channels);

        track->sample_rate_offset = avio_tell(pb);
        put_ebml_float (pb, MATROSKA_ID_AUDIOSAMPLINGFREQ, sample_rate);
        if (output_sample_rate)
            put_ebml_float(pb, MATROSKA_ID_AUDIOOUTSAMPLINGFREQ, output_sample_rate);

        bit_depth = av_get_bits_per_sample(par->codec_id);
        if (!bit_depth && par->codec_id != AV_CODEC_ID_ADPCM_G726) {
            if (par->bits_per_raw_sample)
                bit_depth = par->bits_per_raw_sample;
            else
                bit_depth = av_get_bytes_per_sample(par->format) << 3;
        }
        if (!bit_depth)
            bit_depth = par->bits_per_coded_sample;
        if (bit_depth)
            put_ebml_uint(pb, MATROSKA_ID_AUDIOBITDEPTH, bit_depth);
        end_ebml_master(pb, subinfo);
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        if (!native_id) {
            av_log(s, AV_LOG_ERROR, "Subtitle codec %d is not supported.\n", par->codec_id);
            return AVERROR(ENOSYS);
        }

        if (mkv->mode != MODE_WEBM || par->codec_id != AV_CODEC_ID_WEBVTT)
            native_id = MATROSKA_TRACK_TYPE_SUBTITLE;

        put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, native_id);
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Only audio, video, and subtitles are supported for Matroska.\n");
        return AVERROR(EINVAL);
    }

    if (mkv->mode != MODE_WEBM || par->codec_id != AV_CODEC_ID_WEBVTT) {
        track->codecpriv_offset = avio_tell(pb);
        ret = mkv_write_codecprivate(s, pb, par, native_id, qt_id);
        if (ret < 0)
            return ret;
    }

    end_ebml_master(pb, track_master);

    return 0;
}

static int mkv_write_tracks(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, ret, video_default_idx, audio_default_idx, subtitle_default_idx;

    if (mkv->nb_attachments == s->nb_streams)
        return 0;

    ret = start_ebml_master_crc32(&mkv->track.bc, mkv);
    if (ret < 0)
        return ret;

    if (mkv->default_mode != DEFAULT_MODE_PASSTHROUGH) {
        int video_idx, audio_idx, subtitle_idx;

        video_idx    = video_default_idx    =
        audio_idx    = audio_default_idx    =
        subtitle_idx = subtitle_default_idx = -1;

        for (i = s->nb_streams - 1; i >= 0; i--) {
            AVStream *st = s->streams[i];

            switch (st->codecpar->codec_type) {
#define CASE(type, variable)                                  \
            case AVMEDIA_TYPE_ ## type:                       \
                variable ## _idx = i;                         \
                if (st->disposition & AV_DISPOSITION_DEFAULT) \
                    variable ## _default_idx = i;             \
                break;
            CASE(VIDEO,    video)
            CASE(AUDIO,    audio)
            CASE(SUBTITLE, subtitle)
#undef CASE
            }
        }

        video_default_idx = FFMAX(video_default_idx, video_idx);
        audio_default_idx = FFMAX(audio_default_idx, audio_idx);
        if (mkv->default_mode != DEFAULT_MODE_INFER_NO_SUBS)
            subtitle_default_idx = FFMAX(subtitle_default_idx, subtitle_idx);
    }
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        int is_default = mkv->default_mode == DEFAULT_MODE_PASSTHROUGH ?
                             st->disposition & AV_DISPOSITION_DEFAULT  :
                             i == video_default_idx || i == audio_default_idx ||
                             i == subtitle_default_idx;
        ret = mkv_write_track(s, mkv, st, &mkv->tracks[i],
                              mkv->track.bc, is_default);
        if (ret < 0)
            return ret;
    }

    return end_ebml_master_crc32_tentatively(pb, &mkv->track, mkv,
                                             MATROSKA_ID_TRACKS);
}

static int mkv_write_simpletag(AVIOContext *pb, const AVDictionaryEntry *t)
{
    uint8_t *key = av_strdup(t->key);
    uint8_t *p   = key;
    const uint8_t *lang = NULL;
    ebml_master tag;

    if (!key)
        return AVERROR(ENOMEM);

    if ((p = strrchr(p, '-')) &&
        (lang = ff_convert_lang_to(p + 1, AV_LANG_ISO639_2_BIBL)))
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

static int mkv_write_tag_targets(MatroskaMuxContext *mkv, AVIOContext **pb,
                                 ebml_master *tag, uint32_t elementid, uint64_t uid)
{
    ebml_master targets;
    int ret;

    if (!*pb) {
        ret = start_ebml_master_crc32(pb, mkv);
        if (ret < 0)
            return ret;
    }

    *tag    = start_ebml_master(*pb, MATROSKA_ID_TAG,        0);
    targets = start_ebml_master(*pb, MATROSKA_ID_TAGTARGETS, 4 + 1 + 8);
    if (elementid)
        put_ebml_uid(*pb, elementid, uid);
    end_ebml_master(*pb, targets);
    return 0;
}

static int mkv_check_tag_name(const char *name, uint32_t elementid)
{
    return av_strcasecmp(name, "title") &&
           av_strcasecmp(name, "stereo_mode") &&
           av_strcasecmp(name, "creation_time") &&
           av_strcasecmp(name, "encoding_tool") &&
           av_strcasecmp(name, "duration") &&
           (elementid != MATROSKA_ID_TAGTARGETS_TRACKUID ||
            av_strcasecmp(name, "language")) &&
           (elementid != MATROSKA_ID_TAGTARGETS_ATTACHUID ||
            (av_strcasecmp(name, "filename") &&
             av_strcasecmp(name, "mimetype")));
}

static int mkv_write_tag(MatroskaMuxContext *mkv, const AVDictionary *m,
                         AVIOContext **pb, ebml_master *tag,
                         uint32_t elementid, uint64_t uid)
{
    const AVDictionaryEntry *t = NULL;
    ebml_master tag2;
    int ret;

    ret = mkv_write_tag_targets(mkv, pb, tag ? tag : &tag2, elementid, uid);
    if (ret < 0)
        return ret;

    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if (mkv_check_tag_name(t->key, elementid)) {
            ret = mkv_write_simpletag(*pb, t);
            if (ret < 0)
                return ret;
        }
    }

    if (!tag)
        end_ebml_master(*pb, tag2);

    return 0;
}

static int mkv_check_tag(const AVDictionary *m, uint32_t elementid)
{
    const AVDictionaryEntry *t = NULL;

    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX)))
        if (mkv_check_tag_name(t->key, elementid))
            return 1;

    return 0;
}

static int mkv_write_tags(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ebml_master tag, *tagp = IS_SEEKABLE(s->pb, mkv) ? &tag : NULL;
    int i, ret;

    mkv->wrote_tags = 1;

    ff_metadata_conv_ctx(s, ff_mkv_metadata_conv, NULL);

    if (mkv_check_tag(s->metadata, 0)) {
        ret = mkv_write_tag(mkv, s->metadata, &mkv->tags.bc, NULL, 0, 0);
        if (ret < 0)
            return ret;
    }

    for (i = 0; i < s->nb_streams; i++) {
        const AVStream *st = s->streams[i];
        mkv_track *track = &mkv->tracks[i];

        if (st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT)
            continue;

        if (!tagp && !mkv_check_tag(st->metadata, MATROSKA_ID_TAGTARGETS_TRACKUID))
            continue;

        ret = mkv_write_tag(mkv, st->metadata, &mkv->tags.bc, tagp,
                            MATROSKA_ID_TAGTARGETS_TRACKUID, track->uid);
        if (ret < 0)
            return ret;

        if (tagp) {
            AVIOContext *pb = mkv->tags.bc;
            ebml_master simpletag;

            simpletag = start_ebml_master(pb, MATROSKA_ID_SIMPLETAG,
                                          2 + 1 + 8 + 23);
            put_ebml_string(pb, MATROSKA_ID_TAGNAME, "DURATION");
            track->duration_offset = avio_tell(pb);

            // Reserve space to write duration as a 20-byte string.
            // 2 (ebml id) + 1 (data size) + 20 (data)
            put_ebml_void(pb, 23);
            end_ebml_master(pb, simpletag);
            end_ebml_master(pb, tag);
        }
    }

    if (mkv->nb_attachments && mkv->mode != MODE_WEBM) {
        for (i = 0; i < s->nb_streams; i++) {
            const mkv_track *track = &mkv->tracks[i];
            const AVStream     *st = s->streams[i];

            if (st->codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT)
                continue;

            if (!mkv_check_tag(st->metadata, MATROSKA_ID_TAGTARGETS_ATTACHUID))
                continue;

            ret = mkv_write_tag(mkv, st->metadata, &mkv->tags.bc, NULL,
                                MATROSKA_ID_TAGTARGETS_ATTACHUID, track->uid);
            if (ret < 0)
                return ret;
        }
    }

    if (mkv->tags.bc) {
        return end_ebml_master_crc32_tentatively(s->pb, &mkv->tags, mkv,
                                                 MATROSKA_ID_TAGS);
    }
    return 0;
}

static int mkv_write_chapters(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *dyn_cp = NULL, *dyn_tags = NULL, **tags, *pb = s->pb;
    ebml_master editionentry;
    uint64_t chapter_id_offset = 0;
    AVRational scale = {1, 1E9};
    int i, ret;

    if (!s->nb_chapters || mkv->wrote_chapters)
        return 0;

    for (i = 0; i < s->nb_chapters; i++)
        if (!s->chapters[i]->id) {
            chapter_id_offset = 1;
            break;
        }

    ret = start_ebml_master_crc32(&dyn_cp, mkv);
    if (ret < 0)
        return ret;

    editionentry = start_ebml_master(dyn_cp, MATROSKA_ID_EDITIONENTRY, 0);
    if (mkv->mode != MODE_WEBM) {
        put_ebml_uint(dyn_cp, MATROSKA_ID_EDITIONFLAGDEFAULT, 1);
        /* If mkv_write_tags() has already been called, then any tags
         * corresponding to chapters will be put into a new Tags element. */
        tags = mkv->wrote_tags ? &dyn_tags : &mkv->tags.bc;
    } else
        tags = NULL;

    for (i = 0; i < s->nb_chapters; i++) {
        ebml_master chapteratom, chapterdisplay;
        const AVChapter *c   = s->chapters[i];
        int64_t chapterstart = av_rescale_q(c->start, c->time_base, scale);
        int64_t chapterend   = av_rescale_q(c->end,   c->time_base, scale);
        const AVDictionaryEntry *t;
        if (chapterstart < 0 || chapterstart > chapterend || chapterend < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid chapter start (%"PRId64") or end (%"PRId64").\n",
                   chapterstart, chapterend);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        chapteratom = start_ebml_master(dyn_cp, MATROSKA_ID_CHAPTERATOM, 0);
        put_ebml_uint(dyn_cp, MATROSKA_ID_CHAPTERUID,
                      (uint32_t)c->id + chapter_id_offset);
        put_ebml_uint(dyn_cp, MATROSKA_ID_CHAPTERTIMESTART, chapterstart);
        put_ebml_uint(dyn_cp, MATROSKA_ID_CHAPTERTIMEEND, chapterend);
        if ((t = av_dict_get(c->metadata, "title", NULL, 0))) {
            chapterdisplay = start_ebml_master(dyn_cp, MATROSKA_ID_CHAPTERDISPLAY, 0);
            put_ebml_string(dyn_cp, MATROSKA_ID_CHAPSTRING, t->value);
            put_ebml_string(dyn_cp, MATROSKA_ID_CHAPLANG  , "und");
            end_ebml_master(dyn_cp, chapterdisplay);
        }
        end_ebml_master(dyn_cp, chapteratom);

        if (tags && mkv_check_tag(c->metadata, MATROSKA_ID_TAGTARGETS_CHAPTERUID)) {
            ret = mkv_write_tag(mkv, c->metadata, tags, NULL,
                                MATROSKA_ID_TAGTARGETS_CHAPTERUID,
                                (uint32_t)c->id + chapter_id_offset);
            if (ret < 0)
                goto fail;
        }
    }
    end_ebml_master(dyn_cp, editionentry);
    mkv->wrote_chapters = 1;

    ret = end_ebml_master_crc32(pb, &dyn_cp, mkv, MATROSKA_ID_CHAPTERS, 0, 0, 1);
    if (ret < 0)
        goto fail;
    if (dyn_tags)
        return end_ebml_master_crc32(pb, &dyn_tags, mkv,
                                     MATROSKA_ID_TAGS, 0, 0, 1);
    return 0;

fail:
    if (tags) {
        /* tags == &mkv->tags.bc can only happen if mkv->tags.bc was
         * initially NULL, so we never free older tags. */
        ffio_free_dyn_buf(tags);
    }
    ffio_free_dyn_buf(&dyn_cp);
    return ret;
}

static const char *get_mimetype(const AVStream *st)
{
    const AVDictionaryEntry *t;

    if (t = av_dict_get(st->metadata, "mimetype", NULL, 0))
        return t->value;
    if (st->codecpar->codec_id != AV_CODEC_ID_NONE) {
        const AVCodecDescriptor *desc = avcodec_descriptor_get(st->codecpar->codec_id);
        if (desc && desc->mime_types) {
            return desc->mime_types[0];
        } else if (st->codecpar->codec_id == AV_CODEC_ID_TEXT)
            return "text/plain";
    }

    return NULL;
}

static int mkv_write_attachments(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *dyn_cp = NULL, *pb = s->pb;
    int i, ret;

    if (!mkv->nb_attachments)
        return 0;

    ret = start_ebml_master_crc32(&dyn_cp, mkv);
    if (ret < 0)
        return ret;

    for (i = 0; i < s->nb_streams; i++) {
        const AVStream *st = s->streams[i];
        mkv_track *track = &mkv->tracks[i];
        ebml_master attached_file;
        const AVDictionaryEntry *t;
        const char *mimetype;

        if (st->codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT)
            continue;

        attached_file = start_ebml_master(dyn_cp, MATROSKA_ID_ATTACHEDFILE, 0);

        if (t = av_dict_get(st->metadata, "title", NULL, 0))
            put_ebml_string(dyn_cp, MATROSKA_ID_FILEDESC, t->value);
        if (!(t = av_dict_get(st->metadata, "filename", NULL, 0))) {
            av_log(s, AV_LOG_ERROR, "Attachment stream %d has no filename tag.\n", i);
            return AVERROR(EINVAL);
        }
        put_ebml_string(dyn_cp, MATROSKA_ID_FILENAME, t->value);

        mimetype = get_mimetype(st);
        av_assert0(mimetype);
        put_ebml_string(dyn_cp, MATROSKA_ID_FILEMIMETYPE, mimetype);
        put_ebml_binary(dyn_cp, MATROSKA_ID_FILEDATA, st->codecpar->extradata, st->codecpar->extradata_size);
        put_ebml_uid(dyn_cp, MATROSKA_ID_FILEUID, track->uid);
        end_ebml_master(dyn_cp, attached_file);
    }
    return end_ebml_master_crc32(pb, &dyn_cp, mkv,
                                 MATROSKA_ID_ATTACHMENTS, 0, 0, 1);
}

static int64_t get_metadata_duration(AVFormatContext *s)
{
    const AVDictionaryEntry *duration = av_dict_get(s->metadata, "DURATION",
                                                    NULL, 0);
    int64_t max = 0;
    int64_t us;

    if (duration && (av_parse_time(&us, duration->value, 1) == 0) && us > 0) {
        av_log(s, AV_LOG_DEBUG, "get_metadata_duration found duration in context metadata: %" PRId64 "\n", us);
        return us;
    }

    for (unsigned i = 0; i < s->nb_streams; i++) {
        int64_t us;
        duration = av_dict_get(s->streams[i]->metadata, "DURATION", NULL, 0);

        if (duration && (av_parse_time(&us, duration->value, 1) == 0))
            max = FFMAX(max, us);
    }

    av_log(s, AV_LOG_DEBUG, "get_metadata_duration returned: %" PRId64 "\n", max);
    return max;
}

static int mkv_write_header(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    ebml_master ebml_header;
    const AVDictionaryEntry *tag;
    int ret, i, version = 2;
    int64_t creation_time;

    if (mkv->mode != MODE_WEBM ||
        av_dict_get(s->metadata, "stereo_mode", NULL, 0) ||
        av_dict_get(s->metadata, "alpha_mode", NULL, 0))
        version = 4;

    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_OPUS ||
            av_dict_get(s->streams[i]->metadata, "stereo_mode", NULL, 0) ||
            av_dict_get(s->streams[i]->metadata, "alpha_mode", NULL, 0))
            version = 4;
    }

    ebml_header = start_ebml_master(pb, EBML_ID_HEADER, MAX_EBML_HEADER_SIZE);
    put_ebml_uint  (pb, EBML_ID_EBMLVERSION       ,           1);
    put_ebml_uint  (pb, EBML_ID_EBMLREADVERSION   ,           1);
    put_ebml_uint  (pb, EBML_ID_EBMLMAXIDLENGTH   ,           4);
    put_ebml_uint  (pb, EBML_ID_EBMLMAXSIZELENGTH ,           8);
    put_ebml_string(pb, EBML_ID_DOCTYPE           , s->oformat->name);
    put_ebml_uint  (pb, EBML_ID_DOCTYPEVERSION    ,     version);
    put_ebml_uint  (pb, EBML_ID_DOCTYPEREADVERSION,           2);
    end_ebml_master(pb, ebml_header);

    put_ebml_id(pb, MATROSKA_ID_SEGMENT);
    put_ebml_size_unknown(pb, 8);
    mkv->segment_offset = avio_tell(pb);

    // We write a SeekHead at the beginning to point to all other level
    // one elements (except Clusters).
    mkv_start_seekhead(mkv, pb);

    ret = start_ebml_master_crc32(&mkv->info.bc, mkv);
    if (ret < 0)
        return ret;
    pb = mkv->info.bc;

    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if ((tag = av_dict_get(s->metadata, "title", NULL, 0)))
        put_ebml_string(pb, MATROSKA_ID_TITLE, tag->value);
    if (!(s->flags & AVFMT_FLAG_BITEXACT)) {
        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP, LIBAVFORMAT_IDENT);
        if ((tag = av_dict_get(s->metadata, "encoding_tool", NULL, 0)))
            put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, tag->value);
        else
            put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);

        if (mkv->mode != MODE_WEBM)
            put_ebml_binary(pb, MATROSKA_ID_SEGMENTUID, mkv->segment_uid, 16);
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
        int64_t metadata_duration = get_metadata_duration(s);

        if (s->duration > 0) {
            int64_t scaledDuration = av_rescale(s->duration, 1000, AV_TIME_BASE);
            put_ebml_float(pb, MATROSKA_ID_DURATION, scaledDuration);
            av_log(s, AV_LOG_DEBUG, "Write early duration from recording time = %" PRIu64 "\n", scaledDuration);
        } else if (metadata_duration > 0) {
            int64_t scaledDuration = av_rescale(metadata_duration, 1000, AV_TIME_BASE);
            put_ebml_float(pb, MATROSKA_ID_DURATION, scaledDuration);
            av_log(s, AV_LOG_DEBUG, "Write early duration from metadata = %" PRIu64 "\n", scaledDuration);
        } else if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
            put_ebml_void(pb, 11);              // assumes double-precision float to be written
        }
    }
    ret = end_ebml_master_crc32_tentatively(s->pb, &mkv->info,
                                            mkv, MATROSKA_ID_INFO);
    if (ret < 0)
        return ret;
    pb = s->pb;

    ret = mkv_write_tracks(s);
    if (ret < 0)
        return ret;

    ret = mkv_write_chapters(s);
    if (ret < 0)
        return ret;

    if (mkv->mode != MODE_WEBM) {
        ret = mkv_write_attachments(s);
        if (ret < 0)
            return ret;
    }

    /* Must come after mkv_write_chapters() to write chapter tags
     * into the same Tags element as the other tags. */
    ret = mkv_write_tags(s);
    if (ret < 0)
        return ret;

    if (!IS_SEEKABLE(pb, mkv)) {
        ret = mkv_write_seekhead(pb, mkv, 0, avio_tell(pb));
        if (ret < 0)
            return ret;
    }

    if (s->metadata_header_padding > 0) {
        if (s->metadata_header_padding == 1)
            s->metadata_header_padding++;
        put_ebml_void(pb, s->metadata_header_padding);
    }

    if (mkv->reserve_cues_space) {
        if (IS_SEEKABLE(pb, mkv)) {
            mkv->cues_pos = avio_tell(pb);
            if (mkv->reserve_cues_space == 1)
                mkv->reserve_cues_space++;
            put_ebml_void(pb, mkv->reserve_cues_space);
        } else
            mkv->reserve_cues_space = -1;
    }

    av_init_packet(&mkv->cur_audio_pkt);
    mkv->cur_audio_pkt.size = 0;
    mkv->cluster_pos = -1;

    // start a new cluster every 5 MB or 5 sec, or 32k / 1 sec for streaming or
    // after 4k and on a keyframe
    if (IS_SEEKABLE(pb, mkv)) {
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
}

static int mkv_blockgroup_size(int pkt_size, int track_num_size)
{
    int size = pkt_size + track_num_size + 3;
    size += ebml_length_size(size);
    size += 2;              // EBML ID for block and block duration
    size += 9;              // max size of block duration incl. length field
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

static int mkv_write_block(AVFormatContext *s, AVIOContext *pb,
                           uint32_t blockid, const AVPacket *pkt, int keyframe)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    mkv_track *track = &mkv->tracks[pkt->stream_index];
    uint8_t *data = NULL, *side_data = NULL;
    int err = 0, offset = 0, size = pkt->size, side_data_size = 0;
    int64_t ts = track->write_dts ? pkt->dts : pkt->pts;
    uint64_t additional_id;
    int64_t discard_padding = 0;
    unsigned track_number = track->track_num;
    ebml_master block_group, block_additions, block_more;

    ts += track->ts_offset;

    /* The following string is identical to the one in mkv_write_vtt_blocks
     * so that only one copy needs to exist in binaries. */
    av_log(s, AV_LOG_DEBUG,
           "Writing block of size %d with pts %" PRId64 ", dts %" PRId64 ", "
           "duration %" PRId64 " at relative offset %" PRId64 " in cluster "
           "at offset %" PRId64 ". TrackNumber %u, keyframe %d\n",
           pkt->size, pkt->pts, pkt->dts, pkt->duration, avio_tell(pb),
           mkv->cluster_pos, track_number, keyframe != 0);

    if (par->codec_id == AV_CODEC_ID_H264 && par->extradata_size > 0 &&
        (AV_RB24(par->extradata) == 1 || AV_RB32(par->extradata) == 1)) {
        err = ff_avc_parse_nal_units_buf(pkt->data, &data, &size);
    } else if (par->codec_id == AV_CODEC_ID_HEVC && par->extradata_size > 6 &&
               (AV_RB24(par->extradata) == 1 || AV_RB32(par->extradata) == 1)) {
        /* extradata is Annex B, assume the bitstream is too and convert it */
        err = ff_hevc_annexb2mp4_buf(pkt->data, &data, &size, 0, NULL);
    } else if (par->codec_id == AV_CODEC_ID_AV1) {
        err = ff_av1_filter_obus_buf(pkt->data, &data, &size, &offset);
    } else if (par->codec_id == AV_CODEC_ID_WAVPACK) {
        err = mkv_strip_wavpack(pkt->data, &data, &size);
    } else
        data = pkt->data;

    if (err < 0) {
        av_log(s, AV_LOG_ERROR, "Error when reformatting data of "
               "a packet from stream %d.\n", pkt->stream_index);
        return err;
    }

    if (par->codec_id == AV_CODEC_ID_PRORES && size >= 8) {
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
                                       (AVRational){1, par->sample_rate},
                                       (AVRational){1, 1000000000});
    }

    side_data = av_packet_get_side_data(pkt,
                                        AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,
                                        &side_data_size);
    if (side_data) {
        // Only the Codec-specific BlockMore (id == 1) is currently supported.
        if (side_data_size < 8 || (additional_id = AV_RB64(side_data)) != 1) {
            side_data_size = 0;
        } else {
            side_data      += 8;
            side_data_size -= 8;
        }
    }

    if (side_data_size || discard_padding) {
        block_group = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP, 0);
        blockid = MATROSKA_ID_BLOCK;
    }

    put_ebml_id(pb, blockid);
    put_ebml_length(pb, size + track->track_num_size + 3, 0);
    put_ebml_num(pb, track_number, track->track_num_size);
    avio_wb16(pb, ts - mkv->cluster_pts);
    avio_w8(pb, (blockid == MATROSKA_ID_SIMPLEBLOCK && keyframe) ? (1 << 7) : 0);
    avio_write(pb, data + offset, size);
    if (data != pkt->data)
        av_free(data);

    if (blockid == MATROSKA_ID_BLOCK && !keyframe)
        put_ebml_sint(pb, MATROSKA_ID_BLOCKREFERENCE, track->last_timestamp - ts);
    track->last_timestamp = ts;

    if (discard_padding)
        put_ebml_sint(pb, MATROSKA_ID_DISCARDPADDING, discard_padding);

    if (side_data_size) {
        block_additions = start_ebml_master(pb, MATROSKA_ID_BLOCKADDITIONS, 0);
        block_more = start_ebml_master(pb, MATROSKA_ID_BLOCKMORE, 0);
        /* Until dbc50f8a our demuxer used a wrong default value
         * of BlockAddID, so we write it unconditionally. */
        put_ebml_uint  (pb, MATROSKA_ID_BLOCKADDID, additional_id);
        put_ebml_binary(pb, MATROSKA_ID_BLOCKADDITIONAL,
                        side_data, side_data_size);
        end_ebml_master(pb, block_more);
        end_ebml_master(pb, block_additions);
    }
    if (side_data_size || discard_padding)
        end_ebml_master(pb, block_group);

    return 0;
}

static int mkv_write_vtt_blocks(AVFormatContext *s, AVIOContext *pb, const AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    mkv_track *track = &mkv->tracks[pkt->stream_index];
    ebml_master blockgroup;
    int id_size, settings_size, size;
    uint8_t *id, *settings;
    int64_t ts = track->write_dts ? pkt->dts : pkt->pts;
    const int flags = 0;

    id_size = 0;
    id = av_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_IDENTIFIER,
                                 &id_size);

    settings_size = 0;
    settings = av_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_SETTINGS,
                                       &settings_size);

    size = id_size + 1 + settings_size + 1 + pkt->size;

    /* The following string is identical to the one in mkv_write_block so that
     * only one copy needs to exist in binaries. */
    av_log(s, AV_LOG_DEBUG,
           "Writing block of size %d with pts %" PRId64 ", dts %" PRId64 ", "
           "duration %" PRId64 " at relative offset %" PRId64 " in cluster "
           "at offset %" PRId64 ". TrackNumber %u, keyframe %d\n",
           size, pkt->pts, pkt->dts, pkt->duration, avio_tell(pb),
           mkv->cluster_pos, track->track_num, 1);

    blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP,
                                   mkv_blockgroup_size(size, track->track_num_size));

    put_ebml_id(pb, MATROSKA_ID_BLOCK);
    put_ebml_length(pb, size + track->track_num_size + 3, 0);
    put_ebml_num(pb, track->track_num, track->track_num_size);
    avio_wb16(pb, ts - mkv->cluster_pts);
    avio_w8(pb, flags);
    avio_printf(pb, "%.*s\n%.*s\n%.*s", id_size, id, settings_size, settings, pkt->size, pkt->data);

    put_ebml_uint(pb, MATROSKA_ID_BLOCKDURATION, pkt->duration);
    end_ebml_master(pb, blockgroup);

    return pkt->duration;
}

static int mkv_end_cluster(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int ret;

    if (!mkv->have_video) {
        for (unsigned i = 0; i < s->nb_streams; i++)
            mkv->tracks[i].has_cue = 0;
    }
    mkv->cluster_pos = -1;
    ret = end_ebml_master_crc32(s->pb, &mkv->cluster_bc, mkv,
                                MATROSKA_ID_CLUSTER, 0, 1, 0);
    if (ret < 0)
        return ret;

    avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
    return 0;
}

static int mkv_check_new_extra_data(AVFormatContext *s, const AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    mkv_track *track        = &mkv->tracks[pkt->stream_index];
    AVCodecParameters *par  = s->streams[pkt->stream_index]->codecpar;
    uint8_t *side_data;
    int side_data_size = 0, ret;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                        &side_data_size);

    switch (par->codec_id) {
    case AV_CODEC_ID_AAC:
        if (side_data_size && mkv->track.bc) {
            int filler, output_sample_rate = 0;
            ret = get_aac_sample_rates(s, mkv, side_data, side_data_size,
                                       &track->sample_rate, &output_sample_rate);
            if (ret < 0)
                return ret;
            if (!output_sample_rate)
                output_sample_rate = track->sample_rate; // Space is already reserved, so it's this or a void element.
            ret = ff_alloc_extradata(par, side_data_size);
            if (ret < 0)
                return ret;
            memcpy(par->extradata, side_data, side_data_size);
            avio_seek(mkv->track.bc, track->codecpriv_offset, SEEK_SET);
            mkv_write_codecprivate(s, mkv->track.bc, par, 1, 0);
            filler = MAX_PCE_SIZE + 2 + 4 - (avio_tell(mkv->track.bc) - track->codecpriv_offset);
            if (filler)
                put_ebml_void(mkv->track.bc, filler);
            avio_seek(mkv->track.bc, track->sample_rate_offset, SEEK_SET);
            put_ebml_float(mkv->track.bc, MATROSKA_ID_AUDIOSAMPLINGFREQ, track->sample_rate);
            put_ebml_float(mkv->track.bc, MATROSKA_ID_AUDIOOUTSAMPLINGFREQ, output_sample_rate);
        } else if (!par->extradata_size && !track->sample_rate) {
            // No extradata (codecpar or packet side data).
            av_log(s, AV_LOG_ERROR, "Error parsing AAC extradata, unable to determine samplerate.\n");
            return AVERROR(EINVAL);
        }
        break;
    case AV_CODEC_ID_FLAC:
        if (side_data_size && mkv->track.bc) {
            uint8_t *old_extradata = par->extradata;
            if (side_data_size != par->extradata_size) {
                av_log(s, AV_LOG_ERROR, "Invalid FLAC STREAMINFO metadata for output stream %d\n",
                       pkt->stream_index);
                return AVERROR(EINVAL);
            }
            par->extradata = side_data;
            avio_seek(mkv->track.bc, track->codecpriv_offset, SEEK_SET);
            mkv_write_codecprivate(s, mkv->track.bc, par, 1, 0);
            par->extradata = old_extradata;
        }
        break;
    // FIXME: Remove the following once libaom starts propagating extradata during init()
    //        See https://bugs.chromium.org/p/aomedia/issues/detail?id=2012
    case AV_CODEC_ID_AV1:
        if (side_data_size && mkv->track.bc && !par->extradata_size) {
            AVIOContext *dyn_cp;
            uint8_t *codecpriv;
            int codecpriv_size;
            ret = avio_open_dyn_buf(&dyn_cp);
            if (ret < 0)
                return ret;
            ff_isom_write_av1c(dyn_cp, side_data, side_data_size);
            codecpriv_size = avio_get_dyn_buf(dyn_cp, &codecpriv);
            if ((ret = dyn_cp->error) < 0 ||
                !codecpriv_size && (ret = AVERROR_INVALIDDATA)) {
                ffio_free_dyn_buf(&dyn_cp);
                return ret;
            }
            avio_seek(mkv->track.bc, track->codecpriv_offset, SEEK_SET);
            // Do not write the OBUs as we don't have space saved for them
            put_ebml_binary(mkv->track.bc, MATROSKA_ID_CODECPRIVATE, codecpriv, 4);
            ffio_free_dyn_buf(&dyn_cp);
            ret = ff_alloc_extradata(par, side_data_size);
            if (ret < 0)
                return ret;
            memcpy(par->extradata, side_data, side_data_size);
        } else if (!par->extradata_size)
            return AVERROR_INVALIDDATA;
        break;
    default:
        if (side_data_size)
            av_log(s, AV_LOG_DEBUG, "Ignoring new extradata in a packet for stream %d.\n", pkt->stream_index);
        break;
    }

    return 0;
}

static int mkv_write_packet_internal(AVFormatContext *s, const AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb;
    AVCodecParameters *par  = s->streams[pkt->stream_index]->codecpar;
    mkv_track *track        = &mkv->tracks[pkt->stream_index];
    int keyframe            = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int duration            = pkt->duration;
    int ret;
    int64_t ts = track->write_dts ? pkt->dts : pkt->pts;
    int64_t relative_packet_pos;

    if (ts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_ERROR, "Can't write packet with unknown timestamp\n");
        return AVERROR(EINVAL);
    }
    ts += track->ts_offset;

    if (mkv->cluster_pos != -1) {
        int64_t cluster_time = ts - mkv->cluster_pts;
        if ((int16_t)cluster_time != cluster_time) {
            ret = mkv_end_cluster(s);
            if (ret < 0)
                return ret;
            av_log(s, AV_LOG_WARNING, "Starting new cluster due to timestamp\n");
        }
    }

    if (mkv->cluster_pos == -1) {
        ret = start_ebml_master_crc32(&mkv->cluster_bc, mkv);
        if (ret < 0)
            return ret;
        mkv->cluster_pos = avio_tell(s->pb);
        put_ebml_uint(mkv->cluster_bc, MATROSKA_ID_CLUSTERTIMECODE, FFMAX(0, ts));
        mkv->cluster_pts = FFMAX(0, ts);
        av_log(s, AV_LOG_DEBUG,
               "Starting new cluster with timestamp "
               "%" PRId64 " at offset %" PRId64 " bytes\n",
               mkv->cluster_pts, mkv->cluster_pos);
    }
    pb = mkv->cluster_bc;

    relative_packet_pos = avio_tell(pb);

    if (par->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        ret = mkv_write_block(s, pb, MATROSKA_ID_SIMPLEBLOCK, pkt, keyframe);
        if (ret < 0)
            return ret;
        if (keyframe && IS_SEEKABLE(s->pb, mkv) &&
            (par->codec_type == AVMEDIA_TYPE_VIDEO || !mkv->have_video && !track->has_cue)) {
            ret = mkv_add_cuepoint(mkv, pkt->stream_index, ts,
                                   mkv->cluster_pos, relative_packet_pos, -1);
            if (ret < 0)
                return ret;
            track->has_cue = 1;
        }
    } else {
        if (par->codec_id == AV_CODEC_ID_WEBVTT) {
            duration = mkv_write_vtt_blocks(s, pb, pkt);
        } else {
            ebml_master blockgroup = start_ebml_master(pb, MATROSKA_ID_BLOCKGROUP,
                                                       mkv_blockgroup_size(pkt->size,
                                                                           track->track_num_size));

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

        if (IS_SEEKABLE(s->pb, mkv)) {
            ret = mkv_add_cuepoint(mkv, pkt->stream_index, ts,
                                   mkv->cluster_pos, relative_packet_pos, duration);
            if (ret < 0)
                return ret;
        }
    }

    mkv->duration   = FFMAX(mkv->duration,   ts + duration);
    track->duration = FFMAX(track->duration, ts + duration);

    return 0;
}

static int mkv_write_packet(AVFormatContext *s, const AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    int codec_type          = s->streams[pkt->stream_index]->codecpar->codec_type;
    int keyframe            = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int cluster_size;
    int64_t cluster_time;
    int ret;
    int start_new_cluster;

    ret = mkv_check_new_extra_data(s, pkt);
    if (ret < 0)
        return ret;

    if (mkv->cluster_pos != -1) {
        if (mkv->tracks[pkt->stream_index].write_dts)
            cluster_time = pkt->dts - mkv->cluster_pts;
        else
            cluster_time = pkt->pts - mkv->cluster_pts;
        cluster_time += mkv->tracks[pkt->stream_index].ts_offset;

        cluster_size  = avio_tell(mkv->cluster_bc);

        if (mkv->is_dash && codec_type == AVMEDIA_TYPE_VIDEO) {
            // WebM DASH specification states that the first block of
            // every Cluster has to be a key frame. So for DASH video,
            // we only create a Cluster on seeing key frames.
            start_new_cluster = keyframe;
        } else if (mkv->is_dash && codec_type == AVMEDIA_TYPE_AUDIO &&
                   cluster_time > mkv->cluster_time_limit) {
            // For DASH audio, we create a Cluster based on cluster_time_limit.
            start_new_cluster = 1;
        } else if (!mkv->is_dash &&
                   (cluster_size > mkv->cluster_size_limit ||
                    cluster_time > mkv->cluster_time_limit ||
                    (codec_type == AVMEDIA_TYPE_VIDEO && keyframe &&
                     cluster_size > 4 * 1024))) {
            start_new_cluster = 1;
        } else
            start_new_cluster = 0;

        if (start_new_cluster) {
            ret = mkv_end_cluster(s);
            if (ret < 0)
                return ret;
        }
    }

    if (!mkv->cluster_pos)
        avio_write_marker(s->pb,
                          av_rescale_q(pkt->dts, s->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q),
                          keyframe && (mkv->have_video ? codec_type == AVMEDIA_TYPE_VIDEO : 1) ? AVIO_DATA_MARKER_SYNC_POINT : AVIO_DATA_MARKER_BOUNDARY_POINT);

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt);
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
        if (pkt->size > 0)
            ret = av_packet_ref(&mkv->cur_audio_pkt, pkt);
    } else
        ret = mkv_write_packet_internal(s, pkt);
    return ret;
}

static int mkv_write_flush_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;

    if (!pkt) {
        if (mkv->cluster_pos != -1) {
            int ret = mkv_end_cluster(s);
            if (ret < 0)
                return ret;
            av_log(s, AV_LOG_DEBUG,
                   "Flushing cluster at offset %" PRIu64 " bytes\n",
                   avio_tell(s->pb));
        }
        return 1;
    }
    return mkv_write_packet(s, pkt);
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t endpos, ret64;
    int ret, ret2 = 0;

    // check if we have an audio packet cached
    if (mkv->cur_audio_pkt.size > 0) {
        ret = mkv_write_packet_internal(s, &mkv->cur_audio_pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Could not write cached audio packet ret:%d\n", ret);
            return ret;
        }
    }

    if (mkv->cluster_pos != -1) {
        ret = end_ebml_master_crc32(pb, &mkv->cluster_bc, mkv,
                                    MATROSKA_ID_CLUSTER, 0, 0, 0);
        if (ret < 0)
            return ret;
    }

    ret = mkv_write_chapters(s);
    if (ret < 0)
        return ret;

    if (!IS_SEEKABLE(pb, mkv))
        return 0;

    endpos = avio_tell(pb);

    if (mkv->cues.num_entries && mkv->reserve_cues_space >= 0) {
        AVIOContext *cues = NULL;
        uint64_t size;
        int length_size = 0;

        ret = start_ebml_master_crc32(&cues, mkv);
        if (ret < 0)
            return ret;

        ret = mkv_assemble_cues(s->streams, cues, &mkv->cues,
                                mkv->tracks, s->nb_streams);
        if (ret < 0) {
            ffio_free_dyn_buf(&cues);
            return ret;
        }

        if (mkv->reserve_cues_space) {
            size  = avio_tell(cues);
            length_size = ebml_length_size(size);
            size += 4 + length_size;
            if (mkv->reserve_cues_space < size) {
                av_log(s, AV_LOG_WARNING,
                       "Insufficient space reserved for Cues: "
                       "%d < %"PRIu64". No Cues will be output.\n",
                       mkv->reserve_cues_space, size);
                ret2 = AVERROR(EINVAL);
                goto after_cues;
            } else {
                if ((ret64 = avio_seek(pb, mkv->cues_pos, SEEK_SET)) < 0) {
                    ffio_free_dyn_buf(&cues);
                    return ret64;
                }
                if (mkv->reserve_cues_space == size + 1) {
                    /* There is no way to reserve a single byte because
                     * the minimal size of an EBML Void element is 2
                     * (1 byte ID, 1 byte length field). This problem
                     * is solved by writing the Cues' length field on
                     * one byte more than necessary. */
                    length_size++;
                    size++;
                }
            }
        }
        ret = end_ebml_master_crc32(pb, &cues, mkv, MATROSKA_ID_CUES,
                                    length_size, 0, 1);
        if (ret < 0)
            return ret;
        if (mkv->reserve_cues_space) {
            if (size < mkv->reserve_cues_space)
                put_ebml_void(pb, mkv->reserve_cues_space - size);
        } else
            endpos = avio_tell(pb);
    }

after_cues:
    /* Lengths greater than (1ULL << 56) - 1 can't be represented
     * via an EBML number, so leave the unknown length field. */
    if (endpos - mkv->segment_offset < (1ULL << 56) - 1) {
        if ((ret64 = avio_seek(pb, mkv->segment_offset - 8, SEEK_SET)) < 0)
            return ret64;
        put_ebml_length(pb, endpos - mkv->segment_offset, 8);
    }

    ret = mkv_write_seekhead(pb, mkv, 1, mkv->info.pos);
    if (ret < 0)
        return ret;

    if (mkv->info.bc) {
        // update the duration
        av_log(s, AV_LOG_DEBUG, "end duration = %" PRIu64 "\n", mkv->duration);
        avio_seek(mkv->info.bc, mkv->duration_offset, SEEK_SET);
        put_ebml_float(mkv->info.bc, MATROSKA_ID_DURATION, mkv->duration);
        ret = end_ebml_master_crc32(pb, &mkv->info.bc, mkv,
                                    MATROSKA_ID_INFO, 0, 0, 0);
        if (ret < 0)
            return ret;
    }

    if (mkv->track.bc) {
        // write Tracks master
        avio_seek(pb, mkv->track.pos, SEEK_SET);
        ret = end_ebml_master_crc32(pb, &mkv->track.bc, mkv,
                                    MATROSKA_ID_TRACKS, 0, 0, 0);
        if (ret < 0)
            return ret;
    }

    // update stream durations
    if (mkv->tags.bc) {
        int i;
        for (i = 0; i < s->nb_streams; ++i) {
            const AVStream     *st = s->streams[i];
            const mkv_track *track = &mkv->tracks[i];

            if (track->duration_offset > 0) {
                double duration_sec = track->duration * av_q2d(st->time_base);
                char duration_string[20] = "";

                av_log(s, AV_LOG_DEBUG, "stream %d end duration = %" PRIu64 "\n", i,
                       track->duration);

                avio_seek(mkv->tags.bc, track->duration_offset, SEEK_SET);

                snprintf(duration_string, 20, "%02d:%02d:%012.9f",
                         (int) duration_sec / 3600, ((int) duration_sec / 60) % 60,
                         fmod(duration_sec, 60));

                put_ebml_binary(mkv->tags.bc, MATROSKA_ID_TAGSTRING, duration_string, 20);
            }
        }

        avio_seek(pb, mkv->tags.pos, SEEK_SET);
        ret = end_ebml_master_crc32(pb, &mkv->tags.bc, mkv,
                                    MATROSKA_ID_TAGS, 0, 0, 0);
        if (ret < 0)
            return ret;
    }

    avio_seek(pb, endpos, SEEK_SET);

    return ret2;
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

static int webm_query_codec(enum AVCodecID codec_id, int std_compliance)
{
    int i;
    for (i = 0; ff_webm_codec_tags[i].id != AV_CODEC_ID_NONE; i++)
        if (ff_webm_codec_tags[i].id == codec_id)
            return 1;

    return 0;
}

static uint64_t mkv_get_uid(const mkv_track *tracks, int i, AVLFG *c)
{
    while (1) {
        uint64_t uid;
        int k;
        uid  = (uint64_t)av_lfg_get(c) << 32;
        uid |= av_lfg_get(c);
        if (!uid)
            continue;
        for (k = 0; k < i; k++) {
            if (tracks[k].uid == uid)
                break;
        }
        if (k == i)
            return uid;
    }
}

static int mkv_init(struct AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    AVLFG c;
    unsigned nb_tracks = 0;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_ATRAC3 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_COOK ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_RA_288 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_SIPR ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_RV10 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_RV20) {
            av_log(s, AV_LOG_ERROR,
                   "The Matroska muxer does not yet support muxing %s\n",
                   avcodec_get_name(s->streams[i]->codecpar->codec_id));
            return AVERROR_PATCHWELCOME;
        }
    }

    if (s->avoid_negative_ts < 0) {
        s->avoid_negative_ts = 1;
        s->internal->avoid_negative_ts_use_pts = 1;
    }

    if (!strcmp(s->oformat->name, "webm")) {
        mkv->mode      = MODE_WEBM;
        mkv->write_crc = 0;
    } else
        mkv->mode = MODE_MATROSKAv2;

    mkv->tracks = av_mallocz_array(s->nb_streams, sizeof(*mkv->tracks));
    if (!mkv->tracks)
        return AVERROR(ENOMEM);

    if (!(s->flags & AVFMT_FLAG_BITEXACT)) {
        av_lfg_init(&c, av_get_random_seed());

        // Calculate the SegmentUID now in order not to waste our random seed.
        for (i = 0; i < 4; i++)
            mkv->segment_uid[i] = av_lfg_get(&c);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        mkv_track *track = &mkv->tracks[i];

        if (s->flags & AVFMT_FLAG_BITEXACT) {
            track->uid = i + 1;
        } else {
            track->uid = mkv_get_uid(mkv->tracks, i, &c);
        }

        // ms precision is the de-facto standard timescale for mkv files
        avpriv_set_pts_info(st, 64, 1, 1000);

        if (st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
            if (mkv->mode == MODE_WEBM) {
                av_log(s, AV_LOG_WARNING, "Stream %d will be ignored "
                       "as WebM doesn't support attachments.\n", i);
            } else if (!get_mimetype(st)) {
                av_log(s, AV_LOG_ERROR, "Attachment stream %d has no mimetype "
                       "tag and it cannot be deduced from the codec id.\n", i);
                return AVERROR(EINVAL);
            }
            mkv->nb_attachments++;
            continue;
        }

        nb_tracks++;
        track->track_num = mkv->is_dash ? mkv->dash_track_number : nb_tracks;
        track->track_num_size = ebml_num_size(track->track_num);
    }

    if (mkv->is_dash && nb_tracks != 1)
        return AVERROR(EINVAL);

    return 0;
}

static int mkv_check_bitstream(struct AVFormatContext *s, const AVPacket *pkt)
{
    int ret = 1;
    AVStream *st = s->streams[pkt->stream_index];

    if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        if (pkt->size > 2 && (AV_RB16(pkt->data) & 0xfff0) == 0xfff0)
            ret = ff_stream_add_bitstream_filter(st, "aac_adtstoasc", NULL);
    } else if (st->codecpar->codec_id == AV_CODEC_ID_VP9) {
        ret = ff_stream_add_bitstream_filter(st, "vp9_superframe", NULL);
    }

    return ret;
}

static const AVCodecTag additional_audio_tags[] = {
    { AV_CODEC_ID_ALAC,      0XFFFFFFFF },
    { AV_CODEC_ID_MLP,       0xFFFFFFFF },
    { AV_CODEC_ID_OPUS,      0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S16BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S24BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S32BE, 0xFFFFFFFF },
    { AV_CODEC_ID_QDMC,      0xFFFFFFFF },
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
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

static const AVCodecTag additional_subtitle_tags[] = {
    { AV_CODEC_ID_DVB_SUBTITLE,      0xFFFFFFFF },
    { AV_CODEC_ID_DVD_SUBTITLE,      0xFFFFFFFF },
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
    { "dash_track_number", "Track number for the DASH stream", OFFSET(dash_track_number), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, INT_MAX, FLAGS },
    { "live", "Write files assuming it is a live stream.", OFFSET(is_live), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "allow_raw_vfw", "allow RAW VFW mode", OFFSET(allow_raw_vfw), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "write_crc32", "write a CRC32 element inside every Level 1 element", OFFSET(write_crc), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "default_mode", "Controls how a track's FlagDefault is inferred", OFFSET(default_mode), AV_OPT_TYPE_INT, { .i64 = DEFAULT_MODE_INFER }, DEFAULT_MODE_INFER, DEFAULT_MODE_PASSTHROUGH, FLAGS, "default_mode" },
    { "infer", "For each track type, mark the first track of disposition default as default; if none exists, mark the first track as default.", 0, AV_OPT_TYPE_CONST, { .i64 = DEFAULT_MODE_INFER }, 0, 0, FLAGS, "default_mode" },
    { "infer_no_subs", "For each track type, mark the first track of disposition default as default; for audio and video: if none exists, mark the first track as default.", 0, AV_OPT_TYPE_CONST, { .i64 = DEFAULT_MODE_INFER_NO_SUBS }, 0, 0, FLAGS, "default_mode" },
    { "passthrough", "Use the disposition flag as-is", 0, AV_OPT_TYPE_CONST, { .i64 = DEFAULT_MODE_PASSTHROUGH }, 0, 0, FLAGS, "default_mode" },
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
    .deinit            = mkv_deinit,
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
    .deinit            = mkv_deinit,
    .write_header      = mkv_write_header,
    .write_packet      = mkv_write_flush_packet,
    .write_trailer     = mkv_write_trailer,
    .query_codec       = webm_query_codec,
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
    .deinit            = mkv_deinit,
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
