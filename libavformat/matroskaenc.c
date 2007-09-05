/*
 * Matroska file muxer
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
#include "xiph.h"
#include "matroska.h"

typedef struct mkv_seekhead_entry {
    unsigned int    elementid;
    uint64_t        segmentpos;
} mkv_seekhead_entry;

typedef struct mkv_seekhead {
    offset_t                filepos;
    offset_t                segment_offset;     ///< the file offset to the beginning of the segment
    int                     reserved_size;      ///< -1 if appending to file
    int                     max_entries;
    mkv_seekhead_entry      *entries;
    int                     num_entries;
} mkv_seekhead;

typedef struct {
    uint64_t        pts;
    int             tracknum;
    offset_t        cluster_pos;        ///< file offset of the cluster containing the block
} mkv_cuepoint;

typedef struct {
    offset_t        segment_offset;
    mkv_cuepoint    *entries;
    int             num_entries;
} mkv_cues;

typedef struct MatroskaMuxContext {
    offset_t        segment;
    offset_t        segment_offset;
    offset_t        cluster;
    offset_t        cluster_pos;        ///< file offset of the current cluster
    uint64_t        cluster_pts;
    offset_t        duration_offset;
    uint64_t        duration;
    mkv_seekhead    *main_seekhead;
    mkv_seekhead    *cluster_seekhead;
    mkv_cues        *cues;
} MatroskaMuxContext;

static void put_ebml_id(ByteIOContext *pb, unsigned int id)
{
    if (id >= 0x3fffff)
        put_byte(pb, id >> 24);
    if (id >= 0x7fff)
        put_byte(pb, id >> 16);
    if (id >= 0xff)
        put_byte(pb, id >> 8);
    put_byte(pb, id);
}

static int ebml_id_size(unsigned int id)
{
    if (id >= 0x3fffff)
        return 4;
    if (id >= 0x7fff)
        return 3;
    if (id >= 0xff)
        return 2;
    return 1;
}

// XXX: test this thoroughly and get rid of minbytes hack (currently needed to
// use up all of the space reserved in start_ebml_master)
static void put_ebml_size(ByteIOContext *pb, uint64_t size, int minbytes)
{
    int bytes = minbytes;

    // sizes larger than this are currently undefined in EBML
    // so write "unknown" size
    size = FFMIN(size, (1ULL<<56)-1);

    while (size >> (bytes*7 + 7)) bytes++;

    put_byte(pb, (0x80 >> bytes) | (size >> bytes*8));
    for (bytes -= 1; bytes >= 0; bytes--)
        put_byte(pb, size >> bytes*8);
}

static void put_ebml_uint(ByteIOContext *pb, unsigned int elementid, uint64_t val)
{
    int bytes = 1;
    while (val >> bytes*8) bytes++;

    put_ebml_id(pb, elementid);
    put_ebml_size(pb, bytes, 0);
    for (bytes -= 1; bytes >= 0; bytes--)
        put_byte(pb, val >> bytes*8);
}

static void put_ebml_float(ByteIOContext *pb, unsigned int elementid, double val)
{
    // XXX: single-precision floats?
    put_ebml_id(pb, elementid);
    put_ebml_size(pb, 8, 0);
    put_be64(pb, av_dbl2int(val));
}

static void put_ebml_binary(ByteIOContext *pb, unsigned int elementid,
                            const uint8_t *buf, int size)
{
    put_ebml_id(pb, elementid);
    put_ebml_size(pb, size, 0);
    put_buffer(pb, buf, size);
}

static void put_ebml_string(ByteIOContext *pb, unsigned int elementid, const char *str)
{
    put_ebml_binary(pb, elementid, str, strlen(str));
}

// this reserves exactly the amount of space specified by size, which must be at least 2
static void put_ebml_void(ByteIOContext *pb, uint64_t size)
{
    offset_t currentpos = url_ftell(pb);

    if (size < 2)
        return;

    put_ebml_id(pb, EBML_ID_VOID);
    // we need to subtract the length needed to store the size from the size we need to reserve
    // so 2 cases, we use 8 bytes to store the size if possible, 1 byte otherwise
    if (size < 10)
        put_ebml_size(pb, size-1, 0);
    else
        put_ebml_size(pb, size-9, 7);
    url_fseek(pb, currentpos + size, SEEK_SET);
}

static offset_t start_ebml_master(ByteIOContext *pb, unsigned int elementid)
{
    put_ebml_id(pb, elementid);
    // XXX: this always reserves the maximum needed space to store any size value
    // we should be smarter (additional parameter for expected size?)
    put_ebml_size(pb, (1ULL<<56)-1, 0);     // largest unknown size
    return url_ftell(pb);
}

static void end_ebml_master(ByteIOContext *pb, offset_t start)
{
    offset_t pos = url_ftell(pb);

    url_fseek(pb, start - 8, SEEK_SET);
    put_ebml_size(pb, pos - start, 7);
    url_fseek(pb, pos, SEEK_SET);
}

// initializes a mkv_seekhead element to be ready to index level 1 matroska elements
// if numelements is greater than 0, it reserves enough space for that many elements
// at the current file position and writes the seekhead there, otherwise the seekhead
// will be appended to the file when end_mkv_seekhead() is called
static mkv_seekhead * mkv_start_seekhead(ByteIOContext *pb, offset_t segment_offset, int numelements)
{
    mkv_seekhead *new_seekhead = av_mallocz(sizeof(mkv_seekhead));
    if (new_seekhead == NULL)
        return NULL;

    new_seekhead->segment_offset = segment_offset;

    if (numelements > 0) {
        new_seekhead->filepos = url_ftell(pb);
        // 21 bytes max for a seek entry, 10 bytes max for the SeekHead ID and size,
        // and 3 bytes to guarantee that an EBML void element will fit afterwards
        // XXX: 28 bytes right now because begin_ebml_master() reserves more than necessary
        new_seekhead->reserved_size = numelements * 28 + 13;
        new_seekhead->max_entries = numelements;
        put_ebml_void(pb, new_seekhead->reserved_size);
    }
    return new_seekhead;
}

static int mkv_add_seekhead_entry(mkv_seekhead *seekhead, unsigned int elementid, uint64_t filepos)
{
    mkv_seekhead_entry *entries = seekhead->entries;
    int new_entry = seekhead->num_entries;

    // don't store more elements than we reserved space for
    if (seekhead->max_entries > 0 && seekhead->max_entries <= seekhead->num_entries)
        return -1;

    entries = av_realloc(entries, (seekhead->num_entries + 1) * sizeof(mkv_seekhead_entry));
    if (entries == NULL)
        return -1;

    entries[new_entry].elementid = elementid;
    entries[new_entry].segmentpos = filepos - seekhead->segment_offset;

    seekhead->entries = entries;
    seekhead->num_entries++;

    return 0;
}

// returns the file offset where the seekhead was written and frees the seekhead
static offset_t mkv_write_seekhead(ByteIOContext *pb, mkv_seekhead *seekhead)
{
    offset_t metaseek, seekentry, currentpos;
    int i;

    currentpos = url_ftell(pb);

    if (seekhead->reserved_size > 0)
        url_fseek(pb, seekhead->filepos, SEEK_SET);

    metaseek = start_ebml_master(pb, MATROSKA_ID_SEEKHEAD);
    for (i = 0; i < seekhead->num_entries; i++) {
        mkv_seekhead_entry *entry = &seekhead->entries[i];

        seekentry = start_ebml_master(pb, MATROSKA_ID_SEEKENTRY);

        put_ebml_id(pb, MATROSKA_ID_SEEKID);
        put_ebml_size(pb, ebml_id_size(entry->elementid), 0);
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

static mkv_cues * mkv_start_cues(offset_t segment_offset)
{
    mkv_cues *cues = av_mallocz(sizeof(mkv_cues));
    if (cues == NULL)
        return NULL;

    cues->segment_offset = segment_offset;
    return cues;
}

static int mkv_add_cuepoint(mkv_cues *cues, AVPacket *pkt, offset_t cluster_pos)
{
    mkv_cuepoint *entries = cues->entries;
    int new_entry = cues->num_entries;

    entries = av_realloc(entries, (cues->num_entries + 1) * sizeof(mkv_cuepoint));
    if (entries == NULL)
        return -1;

    entries[new_entry].pts = pkt->pts;
    entries[new_entry].tracknum = pkt->stream_index + 1;
    entries[new_entry].cluster_pos = cluster_pos - cues->segment_offset;

    cues->entries = entries;
    cues->num_entries++;
    return 0;
}

static offset_t mkv_write_cues(ByteIOContext *pb, mkv_cues *cues)
{
    offset_t currentpos, cues_element;
    int i, j;

    currentpos = url_ftell(pb);
    cues_element = start_ebml_master(pb, MATROSKA_ID_CUES);

    for (i = 0; i < cues->num_entries; i++) {
        offset_t cuepoint, track_positions;
        mkv_cuepoint *entry = &cues->entries[i];
        uint64_t pts = entry->pts;

        cuepoint = start_ebml_master(pb, MATROSKA_ID_POINTENTRY);
        put_ebml_uint(pb, MATROSKA_ID_CUETIME, pts);

        // put all the entries from different tracks that have the exact same
        // timestamp into the same CuePoint
        for (j = 0; j < cues->num_entries - i && entry[j].pts == pts; j++) {
            track_positions = start_ebml_master(pb, MATROSKA_ID_CUETRACKPOSITION);
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

static int put_xiph_codecpriv(ByteIOContext *pb, AVCodecContext *codec)
{
    offset_t codecprivate;
    uint8_t *header_start[3];
    int header_len[3];
    int first_header_size;
    int j, k;

    if (codec->codec_id == CODEC_ID_VORBIS)
        first_header_size = 30;
    else
        first_header_size = 42;

    if (ff_split_xiph_headers(codec->extradata, codec->extradata_size,
                              first_header_size, header_start, header_len) < 0) {
        av_log(codec, AV_LOG_ERROR, "Extradata corrupt.\n");
        return -1;
    }

    codecprivate = start_ebml_master(pb, MATROSKA_ID_CODECPRIVATE);
    put_byte(pb, 2);                    // number packets - 1
    for (j = 0; j < 2; j++) {
        for (k = 0; k < header_len[j] / 255; k++)
            put_byte(pb, 255);
        put_byte(pb, header_len[j] % 255);
    }
    for (j = 0; j < 3; j++)
        put_buffer(pb, header_start[j], header_len[j]);
    end_ebml_master(pb, codecprivate);

    return 0;
}

static int mkv_write_tracks(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t tracks;
    int i, j;

    if (mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_TRACKS, url_ftell(pb)) < 0)
        return -1;

    tracks = start_ebml_master(pb, MATROSKA_ID_TRACKS);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecContext *codec = st->codec;
        offset_t subinfo, track;
        int native_id = 0;
        int bit_depth = 0;

        track = start_ebml_master(pb, MATROSKA_ID_TRACKENTRY);
        put_ebml_uint (pb, MATROSKA_ID_TRACKNUMBER     , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKUID        , i + 1);
        put_ebml_uint (pb, MATROSKA_ID_TRACKFLAGLACING , 0);    // no lacing (yet)

        if (st->language[0])
            put_ebml_string(pb, MATROSKA_ID_TRACKLANGUAGE, st->language);

        // look for a codec id string specific to mkv to use, if none are found, use AVI codes
        for (j = 0; ff_mkv_codec_tags[j].id != CODEC_ID_NONE; j++) {
            if (ff_mkv_codec_tags[j].id == codec->codec_id) {
                put_ebml_string(pb, MATROSKA_ID_CODECID, ff_mkv_codec_tags[j].str);
                native_id = 1;
                break;
            }
        }

        switch (codec->codec_id) {
            case CODEC_ID_PCM_S16LE:
            case CODEC_ID_PCM_S16BE:
            case CODEC_ID_PCM_U16LE:
            case CODEC_ID_PCM_U16BE:
                bit_depth = 16;
                break;
            case CODEC_ID_PCM_S8:
            case CODEC_ID_PCM_U8:
                bit_depth = 8;
                break;
        }

        // XXX: CodecPrivate for vorbis, theora, aac, native mpeg4, ...
        if (native_id) {
            if (codec->codec_id == CODEC_ID_VORBIS || codec->codec_id == CODEC_ID_THEORA) {
                if (put_xiph_codecpriv(pb, codec) < 0)
                    return -1;
            } else {
                put_ebml_binary(pb, MATROSKA_ID_CODECPRIVATE, codec->extradata, codec->extradata_size);
            }
        }

        switch (codec->codec_type) {
            case CODEC_TYPE_VIDEO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);

                if (!native_id) {
                    offset_t bmp_header;
                    // if there is no mkv-specific codec id, use VFW mode
                    if (!codec->codec_tag)
                        codec->codec_tag = codec_get_tag(codec_bmp_tags, codec->codec_id);

                    put_ebml_string(pb, MATROSKA_ID_CODECID, MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC);
                    // XXX: codec private isn't a master; is there a better way to re-use put_bmp_header?
                    bmp_header = start_ebml_master(pb, MATROSKA_ID_CODECPRIVATE);
                    put_bmp_header(pb, codec, codec_bmp_tags, 0);
                    end_ebml_master(pb, bmp_header);
                }
                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKVIDEO);
                // XXX: interlace flag?
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELWIDTH , codec->width);
                put_ebml_uint (pb, MATROSKA_ID_VIDEOPIXELHEIGHT, codec->height);
                // XXX: display width/height
                end_ebml_master(pb, subinfo);
                break;

            case CODEC_TYPE_AUDIO:
                put_ebml_uint(pb, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_AUDIO);

                if (!native_id) {
                    offset_t wav_header;
                    // no mkv-specific ID, use ACM mode
                    codec->codec_tag = codec_get_tag(codec_wav_tags, codec->codec_id);
                    if (!codec->codec_tag) {
                        av_log(s, AV_LOG_ERROR, "no codec id found for stream %d", i);
                        return -1;
                    }

                    put_ebml_string(pb, MATROSKA_ID_CODECID, MATROSKA_CODEC_ID_AUDIO_ACM);
                    wav_header = start_ebml_master(pb, MATROSKA_ID_CODECPRIVATE);
                    put_wav_header(pb, codec);
                    end_ebml_master(pb, wav_header);
                }
                subinfo = start_ebml_master(pb, MATROSKA_ID_TRACKAUDIO);
                put_ebml_uint  (pb, MATROSKA_ID_AUDIOCHANNELS    , codec->channels);
                put_ebml_float (pb, MATROSKA_ID_AUDIOSAMPLINGFREQ, codec->sample_rate);
                // XXX: output sample freq (for sbr)
                if (bit_depth)
                    put_ebml_uint(pb, MATROSKA_ID_AUDIOBITDEPTH, bit_depth);
                end_ebml_master(pb, subinfo);
                break;

            default:
                av_log(s, AV_LOG_ERROR, "Only audio and video are supported for Matroska.");
                break;
        }
        end_ebml_master(pb, track);

        // ms precision is the de-facto standard timescale for mkv files
        av_set_pts_info(st, 64, 1, 1000);
    }
    end_ebml_master(pb, tracks);
    return 0;
}

static int mkv_write_header(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t ebml_header, segment_info;

    ebml_header = start_ebml_master(pb, EBML_ID_HEADER);
    put_ebml_uint   (pb, EBML_ID_EBMLVERSION        ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLREADVERSION    ,           1);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXIDLENGTH    ,           4);
    put_ebml_uint   (pb, EBML_ID_EBMLMAXSIZELENGTH  ,           8);
    put_ebml_string (pb, EBML_ID_DOCTYPE            ,  "matroska");
    put_ebml_uint   (pb, EBML_ID_DOCTYPEVERSION     ,           2);
    put_ebml_uint   (pb, EBML_ID_DOCTYPEREADVERSION ,           2);
    end_ebml_master(pb, ebml_header);

    mkv->segment = start_ebml_master(pb, MATROSKA_ID_SEGMENT);
    mkv->segment_offset = url_ftell(pb);

    // we write 2 seek heads - one at the end of the file to point to each cluster, and
    // one at the beginning to point to all other level one elements (including the seek
    // head at the end of the file), which isn't more than 10 elements if we only write one
    // of each other currently defined level 1 element
    mkv->main_seekhead    = mkv_start_seekhead(pb, mkv->segment_offset, 10);
    mkv->cluster_seekhead = mkv_start_seekhead(pb, mkv->segment_offset, 0);

    if (mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_INFO, url_ftell(pb)) < 0)
        return -1;

    segment_info = start_ebml_master(pb, MATROSKA_ID_INFO);
    put_ebml_uint(pb, MATROSKA_ID_TIMECODESCALE, 1000000);
    if (strlen(s->title))
        put_ebml_string(pb, MATROSKA_ID_TITLE, s->title);
    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
        put_ebml_string(pb, MATROSKA_ID_MUXINGAPP, LIBAVFORMAT_IDENT);
        // XXX: both are required; something better for writing app?
        put_ebml_string(pb, MATROSKA_ID_WRITINGAPP, LIBAVFORMAT_IDENT);
    }
    // XXX: segment UID
    // reserve space for the duration
    mkv->duration = 0;
    mkv->duration_offset = url_ftell(pb);
    put_ebml_void(pb, 11);                  // assumes double-precision float to be written
    end_ebml_master(pb, segment_info);

    if (mkv_write_tracks(s) < 0)
        return -1;

    if (mkv_add_seekhead_entry(mkv->cluster_seekhead, MATROSKA_ID_CLUSTER, url_ftell(pb)) < 0)
        return -1;

    mkv->cluster_pos = url_ftell(pb);
    mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER);
    put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, 0);
    mkv->cluster_pts = 0;

    mkv->cues = mkv_start_cues(mkv->segment_offset);
    if (mkv->cues == NULL)
        return -1;

    return 0;
}

static int mkv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;

    // start a new cluster every 5 MB or 5 sec
    if (url_ftell(pb) > mkv->cluster + 5*1024*1024 || pkt->pts > mkv->cluster_pts + 5000) {
        end_ebml_master(pb, mkv->cluster);

        if (mkv_add_seekhead_entry(mkv->cluster_seekhead, MATROSKA_ID_CLUSTER, url_ftell(pb)) < 0)
            return -1;

        mkv->cluster_pos = url_ftell(pb);
        mkv->cluster = start_ebml_master(pb, MATROSKA_ID_CLUSTER);
        put_ebml_uint(pb, MATROSKA_ID_CLUSTERTIMECODE, pkt->pts);
        mkv->cluster_pts = pkt->pts;
    }

    put_ebml_id(pb, MATROSKA_ID_SIMPLEBLOCK);
    put_ebml_size(pb, pkt->size + 4, 0);
    put_byte(pb, 0x80 | (pkt->stream_index + 1));     // this assumes stream_index is less than 126
    put_be16(pb, pkt->pts - mkv->cluster_pts);
    put_byte(pb, !!(pkt->flags & PKT_FLAG_KEY));
    put_buffer(pb, pkt->data, pkt->size);

    if (s->streams[pkt->stream_index]->codec->codec_type == CODEC_TYPE_VIDEO && pkt->flags & PKT_FLAG_KEY) {
        if (mkv_add_cuepoint(mkv->cues, pkt, mkv->cluster_pos) < 0)
            return -1;
    }

    mkv->duration = pkt->pts + pkt->duration;
    return 0;
}

static int mkv_write_trailer(AVFormatContext *s)
{
    MatroskaMuxContext *mkv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t currentpos, second_seekhead, cuespos;

    end_ebml_master(pb, mkv->cluster);

    cuespos = mkv_write_cues(pb, mkv->cues);
    second_seekhead = mkv_write_seekhead(pb, mkv->cluster_seekhead);

    mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_CUES    , cuespos);
    mkv_add_seekhead_entry(mkv->main_seekhead, MATROSKA_ID_SEEKHEAD, second_seekhead);
    mkv_write_seekhead(pb, mkv->main_seekhead);

    // update the duration
    currentpos = url_ftell(pb);
    url_fseek(pb, mkv->duration_offset, SEEK_SET);
    put_ebml_float(pb, MATROSKA_ID_DURATION, mkv->duration);
    url_fseek(pb, currentpos, SEEK_SET);

    end_ebml_master(pb, mkv->segment);
    return 0;
}

AVOutputFormat matroska_muxer = {
    "matroska",
    "Matroska File Format",
    "video/x-matroska",
    "mkv",
    sizeof(MatroskaMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG4,
    mkv_write_header,
    mkv_write_packet,
    mkv_write_trailer,
    .codec_tag = (const AVCodecTag*[]){codec_bmp_tags, codec_wav_tags, 0},
};
