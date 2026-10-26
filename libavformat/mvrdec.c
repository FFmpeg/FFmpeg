/*
 * Copyright (c) 2026 Zhao Zhili
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

#include <limits.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

/*
 * Layout: [512 B header][index: N x 8 B (start_ms:u32, offset:u32)]
 *         [chunks: 16 B local header + H.264 Annex-B NALs]
 * Each chunk is a self-contained GOP (SPS+PPS+IDR + P-slices).
 */
#define MVR_HEADER_SIZE     512
#define MVR_ENTRY_SIZE      8
#define MVR_CHUNK_HEADER    16

typedef struct MVRContext {
    /* index of the chunk to read next */
    unsigned next_chunk;
} MVRContext;

static int mvr_probe(const AVProbeData *p)
{
    unsigned width, height;
    unsigned index_table_size, index_capacity, index_count;
    size_t index_size;

    if (p->buf_size < 0x40)
        return 0;

    if (AV_RL32(p->buf) != 4)
        return 0;

    width = AV_RL32(p->buf + 0x04);
    height = AV_RL32(p->buf + 0x08);
    if (width == 0 || width > 8192 || height == 0 || height > 8192)
        return 0;

    index_table_size = AV_RL32(p->buf + 0x34);
    index_capacity = AV_RL32(p->buf + 0x38);
    index_count = AV_RL32(p->buf + 0x3c);

    if (index_capacity == 0 ||
        av_size_mult(index_capacity, MVR_ENTRY_SIZE, &index_size) < 0 ||
        index_table_size != index_size ||
        index_count > index_capacity)
        return 0;

    /* Must outrank h264 */
    return AVPROBE_SCORE_EXTENSION + 2;
}

static int mvr_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    int64_t filesize;
    uint64_t time_meta_ms;
    uint32_t index_count, start_ms, offset;
    int64_t index_table_size, data_start;
    int ret;

    filesize = avio_size(pb);
    if (filesize < MVR_HEADER_SIZE + MVR_ENTRY_SIZE)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_H264;
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
    avpriv_set_pts_info(st, 64, 1, 1000);

    avio_skip(pb, 4);
    st->codecpar->width = avio_rl32(pb);
    st->codecpar->height = avio_rl32(pb);

    avio_skip(pb, 0x28 - 0x0c);
    time_meta_ms = avio_rl64(pb);
    ff_dict_set_timestamp(&s->metadata, "creation_time",
                          time_meta_ms * 1000);
    st->duration = avio_rl32(pb);

    /* Skip index_capacity; data starts after the full index table. */
    index_table_size = avio_rl32(pb);
    avio_skip(pb, 4);
    index_count = avio_rl32(pb);

    if (index_table_size == 0 ||
        index_table_size > UINT32_MAX - MVR_HEADER_SIZE ||
        index_table_size % MVR_ENTRY_SIZE != 0 ||
        index_table_size / MVR_ENTRY_SIZE < index_count)
        return AVERROR_INVALIDDATA;

    if (avio_seek(pb, MVR_HEADER_SIZE, SEEK_SET) < 0)
        return AVERROR_INVALIDDATA;

    data_start = MVR_HEADER_SIZE + index_table_size;
    if (filesize < data_start)
        return AVERROR_INVALIDDATA;

    start_ms = avio_rl32(pb);
    offset = avio_rl32(pb);
    for (unsigned i = 0; i < index_count; i++) {
        uint32_t next_start_ms;
        int64_t next_offset, chunk_size;

        if (i + 1 < index_count) {
            next_start_ms = avio_rl32(pb);
            next_offset = avio_rl32(pb);
        } else {
            next_start_ms = 0;
            next_offset = filesize;
        }

        if (offset < data_start || next_offset > filesize)
            return AVERROR_INVALIDDATA;

        chunk_size = next_offset - offset;
        if (chunk_size <= MVR_CHUNK_HEADER || chunk_size > INT_MAX)
            return AVERROR_INVALIDDATA;

        ret = av_add_index_entry(st, offset, start_ms,
                                 chunk_size, 0, AVINDEX_KEYFRAME);
        if (ret < 0)
            return ret;

        start_ms = next_start_ms;
        offset = next_offset;
    }

    return 0;
}

static int mvr_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MVRContext *mvr = s->priv_data;
    AVStream *st = s->streams[0];
    FFStream *const sti = ffstream(st);
    AVIndexEntry *e;
    int64_t h264_pos;
    int h264_size, ret;

    /* Resync after a generic seek. */
    if (s->io_repositioned) {
        int idx = av_index_search_timestamp(st, sti->cur_dts,
                                            AVSEEK_FLAG_BACKWARD);
        if (idx < 0)
            return AVERROR(EINVAL);
        mvr->next_chunk = idx;
        s->io_repositioned = 0;
    }

    if (mvr->next_chunk >= sti->nb_index_entries)
        return AVERROR_EOF;

    e = &sti->index_entries[mvr->next_chunk];
    h264_pos = e->pos + MVR_CHUNK_HEADER;
    h264_size = e->size - MVR_CHUNK_HEADER;

    if (avio_seek(s->pb, h264_pos, SEEK_SET) < 0)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, h264_size);
    if (ret < 0)
        return ret;

    pkt->pts = e->timestamp;
    pkt->dts = AV_NOPTS_VALUE;
    pkt->pos = e->pos;
    mvr->next_chunk++;

    return 0;
}

const FFInputFormat ff_mvr_demuxer = {
    .p.name         = "mvr",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MVR CCTV"),
    .p.extensions   = "mvr",
    .priv_data_size = sizeof(MVRContext),
    .read_probe     = mvr_probe,
    .read_header    = mvr_read_header,
    .read_packet    = mvr_read_packet,
};
