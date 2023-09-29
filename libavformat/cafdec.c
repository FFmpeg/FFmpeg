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
 * @file
 * Core Audio Format demuxer
 */

#include <inttypes.h>

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "isom.h"
#include "mov_chan.h"
#include "libavcodec/flac.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/dict.h"
#include "caf.h"

typedef struct CafContext {
    int bytes_per_packet;           ///< bytes in a packet, or 0 if variable
    int frames_per_packet;          ///< frames in a packet, or 0 if variable
    int64_t num_bytes;              ///< total number of bytes in stream

    int64_t packet_cnt;             ///< packet counter
    int64_t frame_cnt;              ///< frame counter

    int64_t data_start;             ///< data start position, in bytes
    int64_t data_size;              ///< raw data size, in bytes
} CafContext;

static int probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) != MKBETAG('c','a','f','f'))
        return 0;
    if (AV_RB16(&p->buf[4]) != 1)
        return 0;
    if (AV_RB32(p->buf + 8) != MKBETAG('d','e','s','c'))
        return 0;
    if (AV_RB64(p->buf + 12) != 32)
        return 0;
    return AVPROBE_SCORE_MAX;
}

/** Read audio description chunk */
static int read_desc_chunk(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    CafContext *caf = s->priv_data;
    AVStream *st;
    int flags;

    /* new audio stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    /* parse format description */
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->sample_rate = av_clipd(av_int2double(avio_rb64(pb)), 0, INT_MAX);
    st->codecpar->codec_tag   = avio_rl32(pb);
    flags = avio_rb32(pb);
    caf->bytes_per_packet  = avio_rb32(pb);
    st->codecpar->block_align = caf->bytes_per_packet;
    caf->frames_per_packet = avio_rb32(pb);
    st->codecpar->ch_layout.nb_channels = avio_rb32(pb);
    st->codecpar->bits_per_coded_sample = avio_rb32(pb);

    if (caf->bytes_per_packet < 0 || caf->frames_per_packet < 0 || st->codecpar->ch_layout.nb_channels < 0)
        return AVERROR_INVALIDDATA;

    /* calculate bit rate for constant size packets */
    if (caf->frames_per_packet > 0 && caf->bytes_per_packet > 0) {
        st->codecpar->bit_rate = (uint64_t)st->codecpar->sample_rate * (uint64_t)caf->bytes_per_packet * 8
                                 / (uint64_t)caf->frames_per_packet;
    } else {
        st->codecpar->bit_rate = 0;
    }

    /* determine codec */
    if (st->codecpar->codec_tag == MKTAG('l','p','c','m'))
        st->codecpar->codec_id = ff_mov_get_lpcm_codec_id(st->codecpar->bits_per_coded_sample, (flags ^ 0x2) | 0x4);
    else
        st->codecpar->codec_id = ff_codec_get_id(ff_codec_caf_tags, st->codecpar->codec_tag);
    return 0;
}

/** Read magic cookie chunk */
static int read_kuki_chunk(AVFormatContext *s, int64_t size)
{
    AVIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];
    int ret;

    if (size < 0 || size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return -1;

    if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        /* The magic cookie format for AAC is an mp4 esds atom.
           The lavc AAC decoder requires the data from the codec specific
           description as extradata input. */
        int strt, skip;

        strt = avio_tell(pb);
        ff_mov_read_esds(s, pb);
        skip = size - (avio_tell(pb) - strt);
        if (skip < 0 || !st->codecpar->extradata ||
            st->codecpar->codec_id != AV_CODEC_ID_AAC) {
            av_log(s, AV_LOG_ERROR, "invalid AAC magic cookie\n");
            return AVERROR_INVALIDDATA;
        }
        avio_skip(pb, skip);
    } else if (st->codecpar->codec_id == AV_CODEC_ID_ALAC) {
#define ALAC_PREAMBLE 12
#define ALAC_HEADER   36
#define ALAC_NEW_KUKI 24
        uint8_t preamble[12];
        if (size < ALAC_NEW_KUKI) {
            av_log(s, AV_LOG_ERROR, "invalid ALAC magic cookie\n");
            avio_skip(pb, size);
            return AVERROR_INVALIDDATA;
        }
        if (avio_read(pb, preamble, ALAC_PREAMBLE) != ALAC_PREAMBLE) {
            av_log(s, AV_LOG_ERROR, "failed to read preamble\n");
            return AVERROR_INVALIDDATA;
        }

        if ((ret = ff_alloc_extradata(st->codecpar, ALAC_HEADER)) < 0)
            return ret;

        /* For the old style cookie, we skip 12 bytes, then read 36 bytes.
         * The new style cookie only contains the last 24 bytes of what was
         * 36 bytes in the old style cookie, so we fabricate the first 12 bytes
         * in that case to maintain compatibility. */
        if (!memcmp(&preamble[4], "frmaalac", 8)) {
            if (size < ALAC_PREAMBLE + ALAC_HEADER) {
                av_log(s, AV_LOG_ERROR, "invalid ALAC magic cookie\n");
                av_freep(&st->codecpar->extradata);
                return AVERROR_INVALIDDATA;
            }
            if (avio_read(pb, st->codecpar->extradata, ALAC_HEADER) != ALAC_HEADER) {
                av_log(s, AV_LOG_ERROR, "failed to read kuki header\n");
                av_freep(&st->codecpar->extradata);
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, size - ALAC_PREAMBLE - ALAC_HEADER);
        } else {
            AV_WB32(st->codecpar->extradata, 36);
            memcpy(&st->codecpar->extradata[4], "alac", 4);
            AV_WB32(&st->codecpar->extradata[8], 0);
            memcpy(&st->codecpar->extradata[12], preamble, 12);
            if (avio_read(pb, &st->codecpar->extradata[24], ALAC_NEW_KUKI - 12) != ALAC_NEW_KUKI - 12) {
                av_log(s, AV_LOG_ERROR, "failed to read new kuki header\n");
                av_freep(&st->codecpar->extradata);
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, size - ALAC_NEW_KUKI);
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_FLAC) {
        int last, type, flac_metadata_size;
        uint8_t buf[4];
        /* The magic cookie format for FLAC consists mostly of an mp4 dfLa atom. */
        if (size < (16 + FLAC_STREAMINFO_SIZE)) {
            av_log(s, AV_LOG_ERROR, "invalid FLAC magic cookie\n");
            return AVERROR_INVALIDDATA;
        }
        /* Check cookie version. */
        if (avio_r8(pb) != 0) {
            av_log(s, AV_LOG_ERROR, "unknown FLAC magic cookie\n");
            return AVERROR_INVALIDDATA;
        }
        avio_rb24(pb); /* Flags */
        /* read dfLa fourcc */
        if (avio_read(pb, buf, 4) != 4) {
            av_log(s, AV_LOG_ERROR, "failed to read FLAC magic cookie\n");
            return pb->error < 0 ? pb->error : AVERROR_INVALIDDATA;
        }
        if (memcmp(buf, "dfLa", 4)) {
            av_log(s, AV_LOG_ERROR, "invalid FLAC magic cookie\n");
            return AVERROR_INVALIDDATA;
        }
        /* Check dfLa version. */
        if (avio_r8(pb) != 0) {
            av_log(s, AV_LOG_ERROR, "unknown dfLa version\n");
            return AVERROR_INVALIDDATA;
        }
        avio_rb24(pb); /* Flags */
        if (avio_read(pb, buf, sizeof(buf)) != sizeof(buf)) {
            av_log(s, AV_LOG_ERROR, "failed to read FLAC metadata block header\n");
            return pb->error < 0 ? pb->error : AVERROR_INVALIDDATA;
        }
        flac_parse_block_header(buf, &last, &type, &flac_metadata_size);
        if (type != FLAC_METADATA_TYPE_STREAMINFO || flac_metadata_size != FLAC_STREAMINFO_SIZE) {
            av_log(s, AV_LOG_ERROR, "STREAMINFO must be first FLACMetadataBlock\n");
            return AVERROR_INVALIDDATA;
        }
        ret = ff_get_extradata(s, st->codecpar, pb, FLAC_STREAMINFO_SIZE);
        if (ret < 0)
            return ret;
        if (!last)
            av_log(s, AV_LOG_WARNING, "non-STREAMINFO FLACMetadataBlock(s) ignored\n");
    } else if (st->codecpar->codec_id == AV_CODEC_ID_OPUS) {
        // The data layout for Opus is currently unknown, so we do not export
        // extradata at all. Multichannel streams are not supported.
        if (st->codecpar->ch_layout.nb_channels > 2) {
            avpriv_request_sample(s, "multichannel Opus in CAF");
            return AVERROR_PATCHWELCOME;
        }
        avio_skip(pb, size);
    } else if ((ret = ff_get_extradata(s, st->codecpar, pb, size)) < 0) {
        return ret;
    }

    return 0;
}

/** Read packet table chunk */
static int read_pakt_chunk(AVFormatContext *s, int64_t size)
{
    AVIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];
    CafContext *caf   = s->priv_data;
    int64_t pos = 0, ccount, num_packets;
    int i;
    int ret;

    ccount = avio_tell(pb);

    num_packets = avio_rb64(pb);
    if (num_packets < 0 || INT32_MAX / sizeof(AVIndexEntry) < num_packets)
        return AVERROR_INVALIDDATA;

    st->nb_frames  = avio_rb64(pb); /* valid frames */
    st->nb_frames += avio_rb32(pb); /* priming frames */
    st->nb_frames += avio_rb32(pb); /* remainder frames */

    if (caf->bytes_per_packet > 0 && caf->frames_per_packet > 0) {
        st->duration = caf->frames_per_packet * num_packets;
        pos          = caf-> bytes_per_packet * num_packets;
    } else {
        st->duration = 0;
        for (i = 0; i < num_packets; i++) {
            if (avio_feof(pb))
                return AVERROR_INVALIDDATA;
            ret = av_add_index_entry(s->streams[0], pos, st->duration, 0, 0, AVINDEX_KEYFRAME);
            if (ret < 0)
                return ret;
            pos += caf->bytes_per_packet ? caf->bytes_per_packet : ff_mp4_read_descr_len(pb);
            st->duration += caf->frames_per_packet ? caf->frames_per_packet : ff_mp4_read_descr_len(pb);
        }
    }

    if (avio_tell(pb) - ccount > size || size > INT64_MAX - ccount) {
        av_log(s, AV_LOG_ERROR, "error reading packet table\n");
        return AVERROR_INVALIDDATA;
    }
    avio_seek(pb, ccount + size, SEEK_SET);

    caf->num_bytes = pos;
    return 0;
}

/** Read information chunk */
static void read_info_chunk(AVFormatContext *s, int64_t size)
{
    AVIOContext *pb = s->pb;
    unsigned int i;
    unsigned int nb_entries = avio_rb32(pb);
    for (i = 0; i < nb_entries && !avio_feof(pb); i++) {
        char key[32];
        char value[1024];
        avio_get_str(pb, INT_MAX, key, sizeof(key));
        avio_get_str(pb, INT_MAX, value, sizeof(value));
        if (!*key)
            continue;
        av_dict_set(&s->metadata, key, value, 0);
    }
}

static int read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    CafContext *caf = s->priv_data;
    AVStream *st;
    uint32_t tag = 0;
    int found_data, ret;
    int64_t size, pos;

    avio_skip(pb, 8); /* magic, version, file flags */

    /* audio description chunk */
    if (avio_rb32(pb) != MKBETAG('d','e','s','c')) {
        av_log(s, AV_LOG_ERROR, "desc chunk not present\n");
        return AVERROR_INVALIDDATA;
    }
    size = avio_rb64(pb);
    if (size != 32)
        return AVERROR_INVALIDDATA;

    ret = read_desc_chunk(s);
    if (ret)
        return ret;
    st = s->streams[0];

    /* parse each chunk */
    found_data = 0;
    while (!avio_feof(pb)) {

        /* stop at data chunk if seeking is not supported or
           data chunk size is unknown */
        if (found_data && (caf->data_size < 0 || !(pb->seekable & AVIO_SEEKABLE_NORMAL)))
            break;

        tag  = avio_rb32(pb);
        size = avio_rb64(pb);
        pos  = avio_tell(pb);
        if (avio_feof(pb))
            break;

        switch (tag) {
        case MKBETAG('d','a','t','a'):
            avio_skip(pb, 4); /* edit count */
            caf->data_start = avio_tell(pb);
            caf->data_size  = size < 0 ? -1 : size - 4;
            if (caf->data_start < 0 || caf->data_size > INT64_MAX - caf->data_start)
                return AVERROR_INVALIDDATA;

            if (caf->data_size > 0 && (pb->seekable & AVIO_SEEKABLE_NORMAL))
                avio_skip(pb, caf->data_size);
            found_data = 1;
            break;

        case MKBETAG('c','h','a','n'):
            if ((ret = ff_mov_read_chan(s, s->pb, st, size)) < 0)
                return ret;
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
            av_log(s, AV_LOG_WARNING,
                   "skipping CAF chunk: %08"PRIX32" (%s), size %"PRId64"\n",
                   tag, av_fourcc2str(av_bswap32(tag)), size);
        case MKBETAG('f','r','e','e'):
            if (size < 0 && found_data)
                goto found_data;
            if (size < 0)
                return AVERROR_INVALIDDATA;
            break;
        }

        if (size > 0 && (pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            if (pos > INT64_MAX - size)
                return AVERROR_INVALIDDATA;
            avio_seek(pb, pos + size, SEEK_SET);
        }
    }

    if (!found_data)
        return AVERROR_INVALIDDATA;

found_data:
    if (caf->bytes_per_packet > 0 && caf->frames_per_packet > 0) {
        if (caf->data_size > 0 && caf->data_size / caf->bytes_per_packet < INT64_MAX / caf->frames_per_packet)
            st->nb_frames = (caf->data_size / caf->bytes_per_packet) * caf->frames_per_packet;
    } else if (ffstream(st)->nb_index_entries && st->duration > 0) {
        if (st->codecpar->sample_rate && caf->data_size / st->duration > INT64_MAX / st->codecpar->sample_rate / 8) {
            av_log(s, AV_LOG_ERROR, "Overflow during bit rate calculation %d * 8 * %"PRId64"\n",
                   st->codecpar->sample_rate, caf->data_size / st->duration);
            return AVERROR_INVALIDDATA;
        }
        st->codecpar->bit_rate = st->codecpar->sample_rate * 8LL *
                                 (caf->data_size / st->duration);
    } else {
        av_log(s, AV_LOG_ERROR, "Missing packet table. It is required when "
                                "block size or frame size are variable.\n");
        return AVERROR_INVALIDDATA;
    }

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time = 0;

    /* position the stream at the start of data */
    if (caf->data_size >= 0)
        avio_seek(pb, caf->data_start, SEEK_SET);

    return 0;
}

#define CAF_MAX_PKT_SIZE 4096

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    AVStream *st      = s->streams[0];
    FFStream *const sti = ffstream(st);
    CafContext *caf   = s->priv_data;
    int res, pkt_size = 0, pkt_frames = 0;
    int64_t left      = CAF_MAX_PKT_SIZE;

    if (avio_feof(pb))
        return AVERROR_EOF;

    /* don't read past end of data chunk */
    if (caf->data_size > 0) {
        left = (caf->data_start + caf->data_size) - avio_tell(pb);
        if (!left)
            return AVERROR_EOF;
        if (left < 0)
            return AVERROR(EIO);
    }

    pkt_frames = caf->frames_per_packet;
    pkt_size   = caf->bytes_per_packet;

    if (pkt_size > 0 && pkt_frames == 1) {
        pkt_size   = (CAF_MAX_PKT_SIZE / pkt_size) * pkt_size;
        pkt_size   = FFMIN(pkt_size, left);
        pkt_frames = pkt_size / caf->bytes_per_packet;
    } else if (sti->nb_index_entries) {
        if (caf->packet_cnt < sti->nb_index_entries - 1) {
            pkt_size   = sti->index_entries[caf->packet_cnt + 1].pos       - sti->index_entries[caf->packet_cnt].pos;
            pkt_frames = sti->index_entries[caf->packet_cnt + 1].timestamp - sti->index_entries[caf->packet_cnt].timestamp;
        } else if (caf->packet_cnt == sti->nb_index_entries - 1) {
            pkt_size   = caf->num_bytes - sti->index_entries[caf->packet_cnt].pos;
            pkt_frames = st->duration   - sti->index_entries[caf->packet_cnt].timestamp;
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
    FFStream *const sti = ffstream(st);
    CafContext *caf = s->priv_data;
    int64_t pos, packet_cnt, frame_cnt;

    timestamp = FFMAX(timestamp, 0);

    if (caf->frames_per_packet > 0 && caf->bytes_per_packet > 0) {
        /* calculate new byte position based on target frame position */
        pos = caf->bytes_per_packet * (timestamp / caf->frames_per_packet);
        if (caf->data_size > 0)
            pos = FFMIN(pos, caf->data_size);
        packet_cnt = pos / caf->bytes_per_packet;
        frame_cnt  = caf->frames_per_packet * packet_cnt;
    } else if (sti->nb_index_entries) {
        packet_cnt = av_index_search_timestamp(st, timestamp, flags);
        frame_cnt  = sti->index_entries[packet_cnt].timestamp;
        pos        = sti->index_entries[packet_cnt].pos;
    } else {
        return -1;
    }

    if (avio_seek(s->pb, pos + caf->data_start, SEEK_SET) < 0)
        return -1;

    caf->packet_cnt = packet_cnt;
    caf->frame_cnt  = frame_cnt;

    return 0;
}

const FFInputFormat ff_caf_demuxer = {
    .p.name         = "caf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Apple CAF (Core Audio Format)"),
    .p.codec_tag    = ff_caf_codec_tags_list,
    .priv_data_size = sizeof(CafContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_seek      = read_seek,
};
