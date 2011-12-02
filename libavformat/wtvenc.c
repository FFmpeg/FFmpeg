/*
 * Windows Television (WTV) muxer
 * Copyright (c) 2011 Zhentan Feng <spyfeng at gmail dot com>
 * Copyright (c) 2011 Peter Ross <pross@xvid.org>
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

/**
 * @file
 * Windows Television (WTV) demuxer
 * @author Zhentan Feng <spyfeng at gmail dot com>
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include "avformat.h"
#include "internal.h"
#include "wtv.h"
#include "asf.h"

#define WTV_BIGSECTOR_SIZE (1 << WTV_BIGSECTOR_BITS)
#define INDEX_BASE 0x2
#define MAX_NB_INDEX 10

/* declare utf16le strings */
#define _ , 0,
static const uint8_t timeline_table_0_header_events[] =
    {'t'_'i'_'m'_'e'_'l'_'i'_'n'_'e'_'.'_'t'_'a'_'b'_'l'_'e'_'.'_'0'_'.'_'h'_'e'_'a'_'d'_'e'_'r'_'.'_'E'_'v'_'e'_'n'_'t'_'s', 0};
static const uint8_t table_0_header_legacy_attrib[] =
    {'t'_'a'_'b'_'l'_'e'_'.'_'0'_'.'_'h'_'e'_'a'_'d'_'e'_'r'_'.'_'l'_'e'_'g'_'a'_'c'_'y'_'_'_'a'_'t'_'t'_'r'_'i'_'b', 0};
static const uint8_t table_0_redirector_legacy_attrib[] =
    {'t'_'a'_'b'_'l'_'e'_'.'_'0'_'.'_'r'_'e'_'d'_'i'_'r'_'e'_'c'_'t'_'o'_'r'_'.'_'l'_'e'_'g'_'a'_'c'_'y'_'_'_'a'_'t'_'t'_'r'_'i'_'b', 0};
static const uint8_t table_0_header_time[] =
    {'t'_'a'_'b'_'l'_'e'_'.'_'0'_'.'_'h'_'e'_'a'_'d'_'e'_'r'_'.'_'t'_'i'_'m'_'e', 0};
static const uint8_t legacy_attrib[] =
    {'l'_'e'_'g'_'a'_'c'_'y'_'_'_'a'_'t'_'t'_'r'_'i'_'b', 0};
#undef _

static const ff_asf_guid sub_wtv_guid =
    {0x8C,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid stream1_guid =
    {0xA1,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid sync_guid =
    {0x97,0xC3,0xD2,0xC2,0x7E,0x9A,0xDA,0x11,0x8B,0xF7,0x00,0x07,0xE9,0x5E,0xAD,0x8D};
static const ff_asf_guid index_guid =
    {0x96,0xc3,0xd2,0xc2,0x7e,0x9a,0xda,0x11,0x8b,0xf7,0x00,0x07,0xe9,0x5e,0xad,0x8d};

enum WtvFileIndex {
    WTV_TIMELINE_TABLE_0_HEADER_EVENTS = 0,
    WTV_TIMELINE_TABLE_0_ENTRIES_EVENTS,
    WTV_TIMELINE,
    WTV_TABLE_0_HEADER_LEGACY_ATTRIB,
    WTV_TABLE_0_ENTRIES_LEGACY_ATTRIB,
    WTV_TABLE_0_REDIRECTOR_LEGACY_ATTRIB,
    WTV_TABLE_0_HEADER_TIME,
    WTV_TABLE_0_ENTRIES_TIME,
    WTV_FILES
};

typedef struct {
    int64_t length;
    const void *header;
    int depth;
    int first_sector;
} WtvFile;

typedef struct {
    int64_t             pos;
    int64_t             serial;
    const ff_asf_guid * guid;
    int                 stream_id;
} WtvChunkEntry;

typedef struct {
    int64_t timeline_start_pos;
    WtvFile file[WTV_FILES];
    int64_t serial;         /** chunk serial number */
    int64_t last_chunk_pos; /** last chunk position */
    int64_t frame_nb;

    WtvChunkEntry index[MAX_NB_INDEX];
    int nb_index;
    int first_video_flag;
    int64_t sync_pos;
} WtvContext;

typedef int WTVHeaderWriteFunc(AVIOContext *pb);

typedef struct {
    const uint8_t *header;
    int header_size;
    WTVHeaderWriteFunc *write_header;
} WTVRootEntryTable;

static int write_pad(AVIOContext *pb, int size)
{
    for (; size > 0; size--)
        avio_w8(pb, 0);
    return 0;
}

static const ff_asf_guid *get_codec_guid(enum CodecID id, const AVCodecGuid *av_guid)
{
    int i;
    for (i = 0; av_guid[i].id != CODEC_ID_NONE; i++) {
        if (id == av_guid[i].id)
            return &(av_guid[i].guid);
    }
    return NULL;
}

/**
 * Write chunk header. If header chunk (0x80000000 set) then add to list of header chunks
 */
static void write_chunk_header(AVFormatContext *s, const ff_asf_guid *guid, int length, int stream_id)
{
    WtvContext *wctx = s->priv_data;
    AVIOContext *pb = s->pb;

    wctx->last_chunk_pos = avio_tell(pb) - wctx->timeline_start_pos;
    ff_put_guid(pb, guid);
    avio_wl32(pb, 32 + length);
    avio_wl32(pb, stream_id);
    avio_wl64(pb, wctx->serial);

    if ((stream_id & 0x80000000) && guid != &index_guid) {
        WtvChunkEntry *t = wctx->index + wctx->nb_index;
        av_assert0(wctx->nb_index < MAX_NB_INDEX);
        t->pos       = wctx->last_chunk_pos;
        t->serial    = wctx->serial;
        t->guid      = guid;
        t->stream_id = stream_id & 0x3FFFFFFF;
        wctx->nb_index++;
    }
}

static void write_chunk_header2(AVFormatContext *s, const ff_asf_guid *guid, int stream_id)
{
    WtvContext *wctx = s->priv_data;
    AVIOContext *pb = s->pb;

    int64_t last_chunk_pos = wctx->last_chunk_pos;
    write_chunk_header(s, guid, 0, stream_id); // length updated later
    avio_wl64(pb, last_chunk_pos);
}

static void finish_chunk_noindex(AVFormatContext *s)
{
    WtvContext *wctx = s->priv_data;
    AVIOContext *pb = s->pb;

    // update the chunk_len field and pad.
    int64_t chunk_len = avio_tell(pb) - (wctx->last_chunk_pos + wctx->timeline_start_pos);
    avio_seek(pb, -(chunk_len - 16), SEEK_CUR);
    avio_wl32(pb, chunk_len);
    avio_seek(pb, chunk_len - (16 + 4), SEEK_CUR);

    write_pad(pb, WTV_PAD8(chunk_len) - chunk_len);
    wctx->serial++;
}

static void write_index(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    WtvContext *wctx = s->priv_data;
    int i;

    write_chunk_header2(s, &index_guid, 0x80000000);
    avio_wl32(pb, 0);
    avio_wl32(pb, 0);

    for (i = 0; i < wctx->nb_index; i++) {
        WtvChunkEntry *t = wctx->index + i;
        ff_put_guid(pb,  t->guid);
        avio_wl64(pb, t->pos);
        avio_wl32(pb, t->stream_id);
        avio_wl32(pb, 0); // checksum?
        avio_wl64(pb, t->serial);
    }
    wctx->nb_index = 0;   // reset index
    finish_chunk_noindex(s);
}

static void finish_chunk(AVFormatContext *s)
{
    WtvContext *wctx = s->priv_data;
    finish_chunk_noindex(s);
    if (wctx->nb_index == MAX_NB_INDEX)
        write_index(s);
}

static int write_stream_codec_info(AVFormatContext *s, AVStream *st)
{
    WtvContext *wctx = s->priv_data;
    const ff_asf_guid *g, *media_type, *format_type;
    AVIOContext *pb = s->pb;
    int64_t  hdr_pos_start;
    int hdr_size = 0;

    if (st->codec->codec_type  == AVMEDIA_TYPE_VIDEO) {
        g = get_codec_guid(st->codec->codec_id, ff_video_guids);
        media_type = &ff_mediatype_video;
        format_type = &ff_format_mpeg2_video;
    } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        g = get_codec_guid(st->codec->codec_id, ff_codec_wav_guids);
        media_type = &ff_mediatype_audio;
        format_type = &ff_format_waveformatex;
    } else {
        av_log(s, AV_LOG_ERROR, "unknown codec_type (0x%x)\n", st->codec->codec_type);
        return -1;
    }

    if (g == NULL) {
        av_log(s, AV_LOG_ERROR, "can't get video codec_id (0x%x) guid.\n", st->codec->codec_id);
        return -1;
    }

    ff_put_guid(pb, media_type); // mediatype
    ff_put_guid(pb, &ff_mediasubtype_cpfilters_processed); // subtype
    write_pad(pb, 12);
    ff_put_guid(pb,&ff_format_cpfilters_processed); // format type
    avio_wl32(pb, 0); // size

    hdr_pos_start = avio_tell(pb);
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (wctx->first_video_flag) {
            write_pad(pb, 216); //The size is sensitive.
            wctx->first_video_flag = 0;
        } else {
            write_pad(pb, 72); // aspect ratio
            ff_put_bmp_header(pb, st->codec, ff_codec_bmp_tags, 0);
        }
    } else {
        ff_put_wav_header(pb, st->codec);
    }
    hdr_size = avio_tell(pb) - hdr_pos_start;

    // seek back write hdr_size
    avio_seek(pb, -(hdr_size + 4), SEEK_CUR);
    avio_wl32(pb, hdr_size + 32);
    avio_seek(pb, hdr_size, SEEK_CUR);
    ff_put_guid(pb, g);           // actual_subtype
    ff_put_guid(pb, format_type); // actual_formattype

    return 0;
}

static int write_stream_codec(AVFormatContext *s, AVStream * st)
{
    AVIOContext *pb = s->pb;
    int ret;
    write_chunk_header2(s, &stream1_guid, 0x80000000 | 0x01);

    avio_wl32(pb,  0x01);
    write_pad(pb, 4);
    write_pad(pb, 4);

    ret = write_stream_codec_info(s, st);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "write stream codec info failed codec_type(0x%x)\n", st->codec->codec_type);
        return -1;
    }

    finish_chunk(s);
    return 0;
}

static void write_sync(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    WtvContext *wctx = s->priv_data;
    int64_t last_chunk_pos = wctx->last_chunk_pos;
    wctx->sync_pos = avio_tell(pb) - wctx->timeline_start_pos;

    write_chunk_header(s, &sync_guid, 0x18, 0);
    write_pad(pb, 24);

    finish_chunk(s);

    wctx->last_chunk_pos = last_chunk_pos;
}

static void write_DSATTRIB_TRANSPORT_PROPERTIES_init(AVFormatContext *s, int stream_index)
{
    AVIOContext *pb = s->pb;
    write_chunk_header2(s, &ff_DSATTRIB_TRANSPORT_PROPERTIES, 0x80000000 | stream_index);
    avio_wl64(pb, stream_index);
    avio_wl64(pb, -1);
    avio_wl64(pb, 0);
    finish_chunk(s);
}

static int write_stream_data(AVFormatContext *s, AVStream *st, int flag)
{
    AVIOContext *pb = s->pb;
    int ret;

    if (!flag) {
        write_chunk_header2(s, &ff_stream_guid, 0x80000000 | (st->index + INDEX_BASE));
        avio_wl32(pb, 0x00000001);
        avio_wl32(pb, st->index + INDEX_BASE); //stream_id
        avio_wl32(pb, 0x00000001);
        write_pad(pb, 8);
    } else {
        write_chunk_header2(s, &ff_stream2_guid, 0x80000000 | (st->index + INDEX_BASE));
        write_pad(pb, 4);
    }

    ret = write_stream_codec_info(s, st);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "write stream codec info failed codec_type(0x%x)\n", st->codec->codec_type);
        return -1;
    }
    finish_chunk(s);

    avpriv_set_pts_info(st, 64, 1, 10000000);

    return 0;
}

static int write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    WtvContext *wctx = s->priv_data;
    int i, pad, ret;
    AVStream *st;

    ff_put_guid(pb, &ff_wtv_guid);
    ff_put_guid(pb, &sub_wtv_guid);

    avio_wl32(pb, 0x01);
    avio_wl32(pb, 0x02);
    avio_wl32(pb, 1 << WTV_SECTOR_BITS);
    avio_wl32(pb, 1 << WTV_BIGSECTOR_BITS);

    //write initial root fields
    avio_wl32(pb, 0); // root_size, update later
    write_pad(pb, 4);
    avio_wl32(pb, 0); // root_sector, update it later.

    write_pad(pb, 32);
    avio_wl32(pb, 0); // file ends pointer, update it later.

    pad = (1 << WTV_SECTOR_BITS) - avio_tell(pb);
    write_pad(pb, pad);
    wctx->timeline_start_pos = avio_tell(pb);

    wctx->serial = 1;
    wctx->last_chunk_pos = -1;
    wctx->first_video_flag = 1;

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ret = write_stream_codec(s, st);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "write stream codec failed codec_type(0x%x)\n", st->codec->codec_type);
            return -1;
        }
        if (i + 1 < s->nb_streams) {
            write_sync(s);
        }
    }

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ret  = write_stream_data(s, st, 0);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "write stream data failed codec_type(0x%x)\n", st->codec->codec_type);
            return -1;
        }
        ret = write_stream_data(s, st, 1);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "write stream2 data failed codec_type(0x%x)\n", st->codec->codec_type);
            return -1;
        }
    }

    for (i = 0; i < s->nb_streams; i++)
        write_DSATTRIB_TRANSPORT_PROPERTIES_init(s, INDEX_BASE + i);

    if (wctx->nb_index)
        write_index(s);

    return 0;
}

static void write_timestamp(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    WtvContext  *wctx = s->priv_data;
    AVCodecContext *enc = s->streams[pkt->stream_index]->codec;
    int flag = 0;
    int64_t frame_number = 0;

    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        wctx->frame_nb++;
        frame_number = wctx->frame_nb;
        flag = pkt->flags & AV_PKT_FLAG_KEY ? 1 : 0;
    }
    write_chunk_header(s, &ff_timestamp_guid, 56, 0x40000000 | (INDEX_BASE + pkt->stream_index));
    write_pad(pb, 8);
    avio_wl64(pb, pkt->pts == AV_NOPTS_VALUE ? -1 : pkt->pts);
    avio_wl64(pb, pkt->pts == AV_NOPTS_VALUE ? -1 : pkt->pts);

    avio_wl64(pb, frame_number);
    avio_wl64(pb, 0);
    avio_wl64(pb, flag);
    avio_wl64(pb, 0);
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    WtvContext  *wctx = s->priv_data;

    // write timestamp chunk
    write_timestamp(s, pkt);

    write_chunk_header(s, &ff_data_guid, pkt->size, INDEX_BASE + pkt->stream_index);
    avio_write(pb, pkt->data, pkt->size);
    write_pad(pb, WTV_PAD8(pkt->size) - pkt->size);

    wctx->serial++;
    avio_flush(pb);
    return 0;
}

static int write_table0_header_envents(AVIOContext *pb)
{
    avio_wl32(pb, 0x10);
    write_pad(pb, 84);
    avio_wl64(pb, 0x32);
    return 96;
}

static int write_table0_header_legacy_attrib(AVIOContext *pb)
{
    int pad = 0;
    avio_wl32(pb, 0xFFFFFFFF);
    write_pad(pb, 12);
    avio_write(pb, legacy_attrib, sizeof(legacy_attrib));
    pad = WTV_PAD8(sizeof(legacy_attrib)) - sizeof(legacy_attrib);
    write_pad(pb, pad);
    write_pad(pb, 32);
    return 48 + WTV_PAD8(sizeof(legacy_attrib));
}

static int write_table0_header_time(AVIOContext *pb)
{
    avio_wl32(pb, 0x10);
    write_pad(pb, 76);
    avio_wl64(pb, 0x40);
    return 88;
}

static const WTVRootEntryTable wtv_root_entry_table[] = {
    { timeline_table_0_header_events,          sizeof(timeline_table_0_header_events),          write_table0_header_envents},
    { ff_timeline_table_0_entries_Events_le16, sizeof(ff_timeline_table_0_entries_Events_le16), NULL},
    { ff_timeline_le16,                        sizeof(ff_timeline_le16),                        NULL},
    { table_0_header_legacy_attrib,            sizeof(table_0_header_legacy_attrib),            write_table0_header_legacy_attrib},
    { ff_table_0_entries_legacy_attrib_le16,   sizeof(ff_table_0_entries_legacy_attrib_le16),   NULL},
    { table_0_redirector_legacy_attrib,        sizeof(table_0_redirector_legacy_attrib),        NULL},
    { table_0_header_time,                     sizeof(table_0_header_time),                     write_table0_header_time},
    { ff_table_0_entries_time_le16,            sizeof(ff_table_0_entries_time_le16),            NULL},
};

static int write_root_table(AVFormatContext *s, int64_t sector_pos)
{
    AVIOContext *pb = s->pb;
    WtvContext  *wctx = s->priv_data;
    int size, pad;
    int i;

    const WTVRootEntryTable *h = wtv_root_entry_table;
    for (i = 0; i < sizeof(wtv_root_entry_table)/sizeof(WTVRootEntryTable); i++, h++) {
        WtvFile *w = &wctx->file[i];
        int filename_padding = WTV_PAD8(h->header_size) - h->header_size;
        WTVHeaderWriteFunc *write = h->write_header;
        int len = 0;
        int64_t len_pos;

        ff_put_guid(pb, &ff_dir_entry_guid);
        len_pos = avio_tell(pb);
        avio_wl16(pb, 40 + h->header_size + filename_padding + 8); // maybe updated later
        write_pad(pb, 6);
        avio_wl64(pb, write ? 0 : w->length);// maybe update later
        avio_wl32(pb, (h->header_size + filename_padding) >> 1);
        write_pad(pb, 4);

        avio_write(pb, h->header, h->header_size);
        write_pad(pb, filename_padding);

        if (write) {
            len = write(pb);
            // update length field
            avio_seek(pb, len_pos, SEEK_SET);
            avio_wl64(pb, 40 + h->header_size + filename_padding + len);
            avio_wl64(pb, len |(1ULL<<62) | (1ULL<<60));
            avio_seek(pb, 8 + h->header_size + filename_padding + len, SEEK_CUR);
        } else {
            avio_wl32(pb, w->first_sector);
            avio_wl32(pb, w->depth);
        }
    }

    // caculate root table size
    size = avio_tell(pb) - sector_pos;
    pad = WTV_SECTOR_SIZE- size;
    write_pad(pb, pad);

    return size;
}

static void write_fat(AVIOContext *pb, int start_sector, int nb_sectors, int shift)
{
    int i;
    for (i = 0; i < nb_sectors; i++) {
        avio_wl32(pb, start_sector + (i << shift));
    }
    // pad left sector pointer size
    write_pad(pb, WTV_SECTOR_SIZE - ((nb_sectors << 2) % WTV_SECTOR_SIZE));
}

static int write_fat_sector(AVFormatContext *s, int64_t start_pos, int nb_sectors, int sector_bits, int depth)
{
    int64_t start_sector = start_pos >> WTV_SECTOR_BITS;
    int shift = sector_bits - WTV_SECTOR_BITS;

    int64_t fat = avio_tell(s->pb);
    write_fat(s->pb, start_sector, nb_sectors, shift);

    if (depth == 2) {
        int64_t start_sector1 = fat >> WTV_SECTOR_BITS;
        int nb_sectors1 = ((nb_sectors << 2) + WTV_SECTOR_SIZE - 1) / WTV_SECTOR_SIZE;
        int64_t fat1 = avio_tell(s->pb);

       write_fat(s->pb, start_sector1, nb_sectors1, 0);
       return fat1;
    }

    return fat;
}

static void write_table_entries_events(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    WtvContext *wctx = s->priv_data;

    //FIXME: output frame_nb, position pairs.
    //We only set the first sync_chunk position here.
    avio_wl64(pb, 0x2);   avio_wl64(pb, wctx->sync_pos);
}

static void write_tag(AVIOContext *pb, const char *key, const char *value)
{
    ff_put_guid(pb, &ff_metadata_guid);
    avio_wl32(pb, 1);
    avio_wl32(pb, strlen(value)*2 + 2);
    avio_put_str16le(pb, key);
    avio_put_str16le(pb, value);
}

static void write_table_entries_attrib(AVFormatContext *s)
{
    AVDictionaryEntry *tag = 0;

    //FIXME: translate special tags (e.g. WM/Bitrate) to binary representation
    ff_metadata_conv(&s->metadata, ff_asf_metadata_conv, NULL);
    while ((tag = av_dict_get(s->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        write_tag(s->pb, tag->key, tag->value);
}

static void write_table_redirector_legacy_attrib(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVDictionaryEntry *tag = 0;
    int64_t pos = 0;

    //FIXME: translate special tags to binary representation
    while ((tag = av_dict_get(s->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        avio_wl64(pb, pos);
        pos += 16 + 4 + 4 + strlen(tag->key)*2 + 2 + strlen(tag->value)*2 + 2;
    }
}

/**
 * Pad the remainder of a file
 * Write out fat table
 * @return <0 on error
 */
static int finish_file(AVFormatContext *s, enum WtvFileIndex index, int64_t start_pos)
{
    WtvContext *wctx = s->priv_data;
    AVIOContext *pb = s->pb;
    WtvFile *w = &wctx->file[index];
    int64_t end_pos = avio_tell(pb);
    int sector_bits, nb_sectors, pad;

    av_assert0(index < WTV_FILES);

    w->length = (end_pos - start_pos);

    // determine optimal fat table depth, sector_bits, nb_sectors
    if (w->length <= WTV_SECTOR_SIZE) {
        w->depth = 0;
        sector_bits = WTV_SECTOR_BITS;
    } else if (w->length <= (WTV_SECTOR_SIZE / 4) * WTV_SECTOR_SIZE) {
        w->depth = 1;
        sector_bits = WTV_SECTOR_BITS;
    } else if (w->length <= (WTV_SECTOR_SIZE / 4) * WTV_BIGSECTOR_SIZE) {
        w->depth = 1;
        sector_bits = WTV_BIGSECTOR_BITS;
    } else if (w->length <= (int64_t)(WTV_SECTOR_SIZE / 4) * (WTV_SECTOR_SIZE / 4) * WTV_SECTOR_SIZE) {
        w->depth = 2;
        sector_bits = WTV_SECTOR_BITS;
    } else if (w->length <= (int64_t)(WTV_SECTOR_SIZE / 4) * (WTV_SECTOR_SIZE / 4) * WTV_BIGSECTOR_SIZE) {
        w->depth = 2;
        sector_bits = WTV_BIGSECTOR_BITS;
    } else {
        av_log(s, AV_LOG_ERROR, "unsupported file allocation table depth (%"PRIi64" bytes)\n", w->length);
        return -1;
    }

    // determine the nb_sectors
    nb_sectors = (int)(w->length >> sector_bits);

    // pad sector of timeline
    pad = (1 << sector_bits) - (w->length % (1 << sector_bits));
    if (pad) {
        nb_sectors++;
        write_pad(pb, pad);
    }

    //write fat table
    if (w->depth > 0) {
        w->first_sector = write_fat_sector(s, start_pos, nb_sectors, sector_bits, w->depth);
    } else {
        w->first_sector = start_pos;
    }
    w->first_sector >>= WTV_SECTOR_BITS;

    w->length |= 1ULL<<60;
    if (sector_bits == WTV_SECTOR_BITS)
        w->length |= 1ULL<<63;

    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    WtvContext *wctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int root_size;
    int64_t sector_pos;
    int64_t start_pos, file_end_pos;

    if (finish_file(s, WTV_TIMELINE, wctx->timeline_start_pos) < 0)
        return -1;

    start_pos = avio_tell(pb);
    write_table_entries_events(s);
    if (finish_file(s, WTV_TIMELINE_TABLE_0_ENTRIES_EVENTS, start_pos) < 0)
        return -1;

    start_pos = avio_tell(pb);
    write_table_entries_attrib(s);
    if (finish_file(s, WTV_TABLE_0_ENTRIES_LEGACY_ATTRIB, start_pos) < 0)
        return -1;

    start_pos = avio_tell(pb);
    write_table_redirector_legacy_attrib(s);
    if (finish_file(s, WTV_TABLE_0_REDIRECTOR_LEGACY_ATTRIB, start_pos) < 0)
        return -1;

    start_pos = avio_tell(pb);
    //FIXME: output timestamp, frame_nb pairs here.
    if (finish_file(s, WTV_TABLE_0_ENTRIES_TIME, start_pos) < 0)
        return -1;

    // write root table
    sector_pos = avio_tell(pb);
    root_size = write_root_table(s, sector_pos);

    file_end_pos = avio_tell(pb);
    // update root value
    avio_seek(pb, 0x30, SEEK_SET);
    avio_wl32(pb, root_size);
    avio_seek(pb, 4, SEEK_CUR);
    avio_wl32(pb, sector_pos >> WTV_SECTOR_BITS);
    avio_seek(pb, 0x5c, SEEK_SET);
    avio_wl32(pb, file_end_pos >> WTV_SECTOR_BITS);

    avio_flush(pb);
    return 0;
}

AVOutputFormat ff_wtv_muxer = {
    .name           = "wtv",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Television (WTV)"),
    .extensions     = "wtv",
    .priv_data_size = sizeof(WtvContext),
    .audio_codec    = CODEC_ID_AC3,
    .video_codec    = CODEC_ID_MPEG2VIDEO,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .write_trailer  = write_trailer,
    .codec_tag      = (const AVCodecTag* const []){ ff_codec_bmp_tags,
                                                    ff_codec_wav_tags, 0 },
};
