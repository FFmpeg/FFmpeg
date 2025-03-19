/*
 * PDV demuxer
 * Copyright (c) 2023 Paul B Mahol
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

#include "libavutil/intfloat.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define PDV_MAGIC "Playdate VID\x00\x00\x00\x00"

typedef struct PDVDemuxContext {
    int current_frame;
    uint8_t *frame_flags;
    uint32_t *frame_offsets;
} PDVDemuxContext;

static int pdv_probe(const AVProbeData *pd)
{
    if (strncmp(pd->buf, PDV_MAGIC, sizeof(PDV_MAGIC) - 1) == 0)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int pdv_read_header(AVFormatContext *s)
{
    PDVDemuxContext *p = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    AVStream *st;
    uint64_t start;
    uint32_t fps;

    avio_skip(pb, 16);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    par              = st->codecpar;
    par->codec_type  = AVMEDIA_TYPE_VIDEO;
    par->codec_id    = AV_CODEC_ID_PDV;
    st->start_time   = 0;
    st->duration     =
    st->nb_frames    = avio_rl16(pb);
    avio_skip(pb, 2);
    fps = avio_rl32(pb);
    st->avg_frame_rate = av_d2q(av_int2float(fps), INT_MAX);
    par->width       = avio_rl16(pb);
    par->height      = avio_rl16(pb);

    avpriv_set_pts_info(st, 64, st->avg_frame_rate.den, st->avg_frame_rate.num);

    p->current_frame = 0;
    p->frame_flags = av_calloc(st->nb_frames + 1, sizeof(*p->frame_flags));
    p->frame_offsets = av_calloc(st->nb_frames + 1, sizeof(*p->frame_offsets));

    if (!p->frame_flags || !p->frame_offsets)
        return AVERROR(ENOMEM);

    for (int n = 0; n <= st->nb_frames; n++) {
        const uint32_t entry = avio_rl32(pb);

        p->frame_flags[n] = entry & 3;
        p->frame_offsets[n] = entry >> 2;
    }

    start = avio_tell(pb);

    for (int n = 0; n < st->nb_frames; n++) {
        const uint64_t pos = start + p->frame_offsets[n];
        const int32_t size = p->frame_offsets[n+1] - p->frame_offsets[n];
        const int flags = p->frame_flags[n] & 1 ? AVINDEX_KEYFRAME : 0;

        if (p->frame_flags[n] == 0 || size <= 0 ||
            ((pb->seekable & AVIO_SEEKABLE_NORMAL) && pos + size > avio_size(pb)))
            break;
        av_add_index_entry(st, pos, n, size, 0, flags);
    }

    return 0;
}

static int pdv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PDVDemuxContext *p = s->priv_data;
    AVStream *st = s->streams[0];
    FFStream *const sti = ffstream(st);
    AVIOContext *pb = s->pb;
    int32_t size, flags, ret;
    int64_t pos;

    if (p->current_frame >= st->nb_frames)
        return AVERROR_EOF;

    if (p->current_frame >= sti->nb_index_entries)
        return AVERROR(EIO);

    pos   = sti->index_entries[p->current_frame].pos;
    flags = sti->index_entries[p->current_frame].flags;
    size  = sti->index_entries[p->current_frame].size;

    avio_seek(pb, pos, SEEK_SET);
    if (avio_feof(pb) || ((pb->seekable & AVIO_SEEKABLE_NORMAL) && pos + size > avio_size(pb)) || size == 0)
        return AVERROR_EOF;

    ret = av_get_packet(pb, pkt, size);
    if (ret < 0)
        return ret;

    if (flags & AVINDEX_KEYFRAME)
        pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->stream_index = 0;
    pkt->pts = p->current_frame++;
    pkt->duration = 1;

    return 0;
}

static int pdv_read_close(AVFormatContext *s)
{
    PDVDemuxContext *p = s->priv_data;

    av_freep(&p->frame_flags);
    av_freep(&p->frame_offsets);

    return 0;
}

static int pdv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    PDVDemuxContext *p = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);

    if (index < 0)
        return -1;

    if (avio_seek(s->pb, ffstream(st)->index_entries[index].pos, SEEK_SET) < 0)
        return -1;

    p->current_frame = index;

    return 0;
}

const FFInputFormat ff_pdv_demuxer = {
    .p.name         = "pdv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PlayDate Video"),
    .p.extensions   = "pdv",
    .priv_data_size = sizeof(PDVDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = pdv_probe,
    .read_header    = pdv_read_header,
    .read_packet    = pdv_read_packet,
    .read_close     = pdv_read_close,
    .read_seek      = pdv_read_seek,
};
