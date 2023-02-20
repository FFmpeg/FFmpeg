/*
 * RKA demuxer
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

#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"

#include "apetag.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

typedef struct RKAContext {
    int total_frames, currentframe;
    int frame_size;
    int last_frame_size;
} RKAContext;

static int rka_probe(const AVProbeData *p)
{
    if (AV_RL32(&p->buf[0]) == MKTAG('R', 'K', 'A', '7') &&
        AV_RL32(&p->buf[4]) > 0 &&
        AV_RL32(&p->buf[8]) > 0 &&
        p->buf[12] > 0 &&
        p->buf[12] <= 2 &&
        (p->buf[13] == 8 || p->buf[13] == 16) &&
        (p->buf[15] & 2) != 0)
        return AVPROBE_SCORE_EXTENSION + 30;
    return 0;
}

static int rka_read_header(AVFormatContext *s)
{
    int64_t nb_samples, size_offset;
    RKAContext *c = s->priv_data;
    int channels, bps, samplerate;
    AVCodecParameters *par;
    int64_t framepos;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    par = st->codecpar;
    ret = ff_get_extradata(s, par, s->pb, 16);
    if (ret < 0)
        return ret;

    nb_samples = AV_RL32(par->extradata + 4);
    samplerate = AV_RL32(par->extradata + 8);
    channels = par->extradata[12];
    if (channels == 0)
        return AVERROR_INVALIDDATA;
    bps = par->extradata[13];
    if (bps == 0)
        return AVERROR_INVALIDDATA;
    size_offset = avio_rl32(s->pb);
    framepos = avio_tell(s->pb);
    c->frame_size = 131072;

    avpriv_set_pts_info(st, 64, 1, samplerate);
    st->start_time = 0;

    avio_seek(s->pb, size_offset, SEEK_SET);
    c->total_frames = (nb_samples + c->frame_size - 1) / c->frame_size;
    c->last_frame_size = nb_samples % c->frame_size;

    for (int i = 0; i < c->total_frames; i++) {
        int r, end = 0;
        int64_t size;

        if (avio_feof(s->pb))
            break;

        size = avio_rl24(s->pb);
        if (size == 0) {
            end = 1;
            size = size_offset - framepos;
            if (size <= 0)
                break;
        }

        if ((r = av_add_index_entry(st, framepos, (i * 131072LL) / (channels * (bps >> 3)),
                                    size, 0, AVINDEX_KEYFRAME)) < 0)
            return r;
        framepos += size;

        if (end)
            break;
    }

    par->codec_type = AVMEDIA_TYPE_AUDIO;
    par->codec_id = AV_CODEC_ID_RKA;
    par->ch_layout.nb_channels = channels;
    par->sample_rate = samplerate;
    par->bits_per_raw_sample = bps;
    st->duration = 8LL*nb_samples / (channels * bps);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL)
        ff_ape_parse_tag(s);

    avio_seek(s->pb, 20, SEEK_SET);

    return 0;
}

static int rka_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RKAContext *c = s->priv_data;
    AVStream *st = s->streams[0];
    FFStream *const sti = ffstream(st);
    int size, ret;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    if (c->currentframe >= sti->nb_index_entries)
        return AVERROR_EOF;

    size = sti->index_entries[c->currentframe].size;

    ret = av_get_packet(s->pb, pkt, size);
    pkt->dts = sti->index_entries[c->currentframe++].timestamp;
    pkt->duration = c->currentframe == c->total_frames ? c->last_frame_size :
                                                         131072;
    return ret;
}

static int rka_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    RKAContext *c = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return -1;
    if (avio_seek(s->pb, ffstream(st)->index_entries[index].pos, SEEK_SET) < 0)
        return -1;

    c->currentframe = index;

    return 0;
}

const AVInputFormat ff_rka_demuxer = {
    .name           = "rka",
    .long_name      = NULL_IF_CONFIG_SMALL("RKA (RK Audio)"),
    .priv_data_size = sizeof(RKAContext),
    .read_probe     = rka_probe,
    .read_header    = rka_read_header,
    .read_packet    = rka_read_packet,
    .read_seek      = rka_read_seek,
    .extensions     = "rka",
};
