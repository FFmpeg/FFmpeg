/*
 * Core Audio Format demuxer
 * Copyright (c) 2007 Justin Ruggles
 * Copyright (c) 2009 Peter Ross
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

/**
 * @file libavformat/cafdec.c
 * Core Audio Format demuxer
 */

#include "avformat.h"
#include "riff.h"
#include "isom.h"
#include "libavutil/intreadwrite.h"
#include "caf.h"

typedef struct {
    int bytes_per_packet;           ///< bytes in a packet, or 0 if variable
    int frames_per_packet;          ///< frames in a packet, or 0 if variable
    int64_t num_bytes;              ///< total number of bytes in stream

    int64_t packet_cnt;             ///< packet counter
    int64_t frame_cnt;              ///< frame counter

    int64_t data_start;             ///< data start position, in bytes
    int64_t data_size;              ///< raw data size, in bytes
} CaffContext;

static int probe(AVProbeData *p)
{
    if (AV_RB32(p->buf) == MKBETAG('c','a','f','f') && AV_RB16(&p->buf[4]) == 1)
        return AVPROBE_SCORE_MAX;
    return 0;
}

/** Read audio description chunk */
static int read_desc_chunk(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    CaffContext *caf  = s->priv_data;
    AVStream *st;
    int flags;

    /* new audio stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    /* parse format description */
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->sample_rate = av_int2dbl(get_be64(pb));
    st->codec->codec_tag   = get_be32(pb);
    flags = get_be32(pb);
    caf->bytes_per_packet  = get_be32(pb);
    st->codec->block_align = caf->bytes_per_packet;
    caf->frames_per_packet = get_be32(pb);
    st->codec->channels    = get_be32(pb);
    st->codec->bits_per_coded_sample = get_be32(pb);

    /* calculate bit rate for constant size packets */
    if (caf->frames_per_packet > 0 && caf->bytes_per_packet > 0) {
        st->codec->bit_rate = (uint64_t)st->codec->sample_rate * (uint64_t)caf->bytes_per_packet * 8
                              / (uint64_t)caf->frames_per_packet;
    } else {
        st->codec->bit_rate = 0;
    }

    /* determine codec */
    if (st->codec->codec_tag == MKBETAG('l','p','c','m'))
        st->codec->codec_id = ff_mov_get_lpcm_codec_id(st->codec->bits_per_coded_sample, (flags ^ 0x2) | 0x4);
    else
        st->codec->codec_id = ff_codec_get_id(ff_codec_caf_tags, st->codec->codec_tag);
    return 0;
}

/** Read magic cookie chunk */
static int read_kuki_chunk(AVFormatContext *s, int64_t size)
{
    ByteIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];

    if (size < 0 || size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE)
        return -1;

    if (st->codec->codec_id == CODEC_ID_AAC) {
        /* The magic cookie format for AAC is an mp4 esds atom.
           The lavc AAC decoder requires the data from the codec specific
           description as extradata input. */
        int strt, skip;
        MOVAtom atom;

        strt = url_ftell(pb);
        ff_mov_read_esds(s, pb, atom);
        skip = size - (url_ftell(pb) - strt);
        if (skip < 0 || !st->codec->extradata ||
            st->codec->codec_id != CODEC_ID_AAC) {
            av_log(s, AV_LOG_ERROR, "invalid AAC magic cookie\n");
            return AVERROR_INVALIDDATA;
        }
        url_fskip(pb, skip);
    } else {
        st->codec->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codec->extradata)
            return AVERROR(ENOMEM);
        get_buffer(pb, st->codec->extradata, size);
        st->codec->extradata_size = size;
    }

    return 0;
}

/** Read packet table chunk */
static int read_pakt_chunk(AVFormatContext *s, int64_t size)
{
    ByteIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];
    CaffContext *caf  = s->priv_data;
    int64_t pos = 0, ccount;
    int num_packets, i;

    ccount = url_ftell(pb);

    num_packets = get_be64(pb);
    if (num_packets < 0 || INT32_MAX / sizeof(AVIndexEntry) < num_packets)
        return AVERROR_INVALIDDATA;

    st->nb_frames  = get_be64(pb); /* valid frames */
    st->nb_frames += get_be32(pb); /* priming frames */
    st->nb_frames += get_be32(pb); /* remainder frames */

    st->duration = 0;
    for (i = 0; i < num_packets; i++) {
        av_add_index_entry(s->streams[0], pos, st->duration, 0, 0, AVINDEX_KEYFRAME);
        pos += caf->bytes_per_packet ? caf->bytes_per_packet : ff_mp4_read_descr_len(pb);
        st->duration += caf->frames_per_packet ? caf->frames_per_packet : ff_mp4_read_descr_len(pb);
    }

    if (url_ftell(pb) - ccount != size) {
        av_log(s, AV_LOG_ERROR, "error reading packet table\n");
        return -1;
    }

    caf->num_bytes = pos;
    return 0;
}

/** Read information chunk */
static void read_info_chunk(AVFormatContext *s, int64_t size)
{
    ByteIOContext *pb = s->pb;
    unsigned int i;
    unsigned int nb_entries = get_be32(pb);
    for (i = 0; i < nb_entries; i++) {
        char key[32];
        char value[1024];
        get_strz(pb, key, sizeof(key));
        get_strz(pb, value, sizeof(value));
        av_metadata_set(&s->metadata, key, value);
    }
}

static int read_header(AVFormatContext *s,
                       AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    CaffContext *caf  = s->priv_data;
    AVStream *st;
    uint32_t tag = 0;
    int found_data, ret;
    int64_t size;

    url_fskip(pb, 8); /* magic, version, file flags */

    /* audio description chunk */
    if (get_be32(pb) != MKBETAG('d','e','s','c')) {
        av_log(s, AV_LOG_ERROR, "desc chunk not present\n");
        return AVERROR_INVALIDDATA;
    }
    size = get_be64(pb);
    if (size != 32)
        return AVERROR_INVALIDDATA;

    ret = read_desc_chunk(s);
    if (ret)
        return ret;
    st = s->streams[0];

    /* parse each chunk */
    found_data = 0;
    while (!url_feof(pb)) {

        /* stop at data chunk if seeking is not supported or
           data chunk size is unknown */
        if (found_data && (caf->data_size < 0 || url_is_streamed(pb)))
            break;

        tag  = get_be32(pb);
        size = get_be64(pb);
        if (url_feof(pb))
            break;

        switch (tag) {
        case MKBETAG('d','a','t','a'):
            url_fskip(pb, 4); /* edit count */
            caf->data_start = url_ftell(pb);
            caf->data_size  = size < 0 ? -1 : size - 4;
            if (caf->data_size > 0 && !url_is_streamed(pb))
                url_fskip(pb, caf->data_size);
            found_data = 1;
            break;

        /* magic cookie chunk */
        case MKBETAG('k','u','k','i'):
            if (read_kuki_chunk(s, size))
                return AVERROR_INVALIDDATA;
            break;

        /* packet table chunk */
        case MKBETAG('p','a','k','t'):
            if (read_pakt_chunk(s, size))
                return AVERROR_INVALIDDATA;
            break;

        case MKBETAG('i','n','f','o'):
            read_info_chunk(s, size);
            break;

        default:
#define _(x) ((x) >= ' ' ? (x) : ' ')
            av_log(s, AV_LOG_WARNING, "skipping CAF chunk: %08X (%c%c%c%c)\n",
                tag, _(tag>>24), _((tag>>16)&0xFF), _((tag>>8)&0xFF), _(tag&0xFF));
#undef _
        case MKBETAG('f','r','e','e'):
            if (size < 0)
                return AVERROR_INVALIDDATA;
            url_fskip(pb, size);
            break;
        }
    }

    if (!found_data)
        return AVERROR_INVALIDDATA;

    if (caf->bytes_per_packet > 0 && caf->frames_per_packet > 0) {
        if (caf->data_size > 0)
            st->nb_frames = (caf->data_size / caf->bytes_per_packet) * caf->frames_per_packet;
    } else if (st->nb_index_entries) {
        st->codec->bit_rate = st->codec->sample_rate * caf->data_size * 8 /
                              st->duration;
    } else {
        av_log(s, AV_LOG_ERROR, "Missing packet table. It is required when "
                                "block size or frame size are variable.\n");
        return AVERROR_INVALIDDATA;
    }
    s->file_size = url_fsize(pb);
    s->file_size = FFMAX(0, s->file_size);

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);
    st->start_time = 0;

    /* position the stream at the start of data */
    if (caf->data_size >= 0)
        url_fseek(pb, caf->data_start, SEEK_SET);

    return 0;
}

#define CAF_MAX_PKT_SIZE 4096

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];
    CaffContext *caf  = s->priv_data;
    int res, pkt_size = 0, pkt_frames = 0;
    int64_t left      = CAF_MAX_PKT_SIZE;

    if (url_feof(pb))
        return AVERROR(EIO);

    /* don't read past end of data chunk */
    if (caf->data_size > 0) {
        left = (caf->data_start + caf->data_size) - url_ftell(pb);
        if (left <= 0)
            return AVERROR(EIO);
    }

    pkt_frames = caf->frames_per_packet;
    pkt_size   = caf->bytes_per_packet;

    if (pkt_size > 0 && pkt_frames == 1) {
        pkt_size   = (CAF_MAX_PKT_SIZE / pkt_size) * pkt_size;
        pkt_size   = FFMIN(pkt_size, left);
        pkt_frames = pkt_size / caf->bytes_per_packet;
    } else if (st->nb_index_entries) {
        if (caf->packet_cnt < st->nb_index_entries - 1) {
            pkt_size   = st->index_entries[caf->packet_cnt + 1].pos       - st->index_entries[caf->packet_cnt].pos;
            pkt_frames = st->index_entries[caf->packet_cnt + 1].timestamp - st->index_entries[caf->packet_cnt].timestamp;
        } else if (caf->packet_cnt == st->nb_index_entries - 1) {
            pkt_size   = caf->num_bytes - st->index_entries[caf->packet_cnt].pos;
            pkt_frames = st->duration   - st->index_entries[caf->packet_cnt].timestamp;
        } else {
            return AVERROR(EIO);
        }
    }

    if (pkt_size == 0 || pkt_frames == 0 || pkt_size > left)
        return AVERROR(EIO);

    res = av_get_packet(pb, pkt, pkt_size);
    if (res < 0)
        return res;

    pkt->size           = res;
    pkt->stream_index   = 0;
    pkt->dts = pkt->pts = caf->frame_cnt;

    caf->packet_cnt++;
    caf->frame_cnt += pkt_frames;

    return 0;
}

static int read_seek(AVFormatContext *s, int stream_index,
                     int64_t timestamp, int flags)
{
    AVStream *st = s->streams[0];
    CaffContext *caf = s->priv_data;
    int64_t pos;

    timestamp = FFMAX(timestamp, 0);

    if (caf->frames_per_packet > 0 && caf->bytes_per_packet > 0) {
        /* calculate new byte position based on target frame position */
        pos = caf->bytes_per_packet * timestamp / caf->frames_per_packet;
        if (caf->data_size > 0)
            pos = FFMIN(pos, caf->data_size);
        caf->packet_cnt = pos / caf->bytes_per_packet;
        caf->frame_cnt  = caf->frames_per_packet * caf->packet_cnt;
    } else if (st->nb_index_entries) {
        caf->packet_cnt = av_index_search_timestamp(st, timestamp, flags);
        caf->frame_cnt  = st->index_entries[caf->packet_cnt].timestamp;
        pos             = st->index_entries[caf->packet_cnt].pos;
    } else {
        return -1;
    }

    url_fseek(s->pb, pos + caf->data_start, SEEK_SET);
    return 0;
}

AVInputFormat caf_demuxer = {
    "caf",
    NULL_IF_CONFIG_SMALL("Apple Core Audio Format"),
    sizeof(CaffContext),
    probe,
    read_header,
    read_packet,
    NULL,
    read_seek,
    .codec_tag = (const AVCodecTag*[]){ff_codec_caf_tags, 0},
};
