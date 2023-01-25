/*
 * Bonk demuxer
 * Copyright (c) 2016 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "avio_internal.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "id3v2.h"
#include "rawdec.h"

static int bonk_probe(const AVProbeData *p)
{
    for (int i = 0; i < p->buf_size - 22; i++) {
        if (!p->buf[i] && AV_RL32(p->buf + i + 1) == MKTAG('B','O','N','K')) {
            if (p->buf[i + 5])
                return 0;
            if (AV_RL32(p->buf + i + 6) == 0)
                return 0;
            if (AV_RL32(p->buf + i + 10) == 0)
                return 0;
            if (p->buf[i + 14] == 0)
                return 0;
            if (AV_RL16(p->buf + i + 17) == 0 ||
                AV_RL16(p->buf + i + 17) > 2048)
                return 0;
            if (p->buf[i + 19] == 0)
                return 0;
            if (AV_RL16(p->buf + i + 20) == 0)
                return 0;
            return AVPROBE_SCORE_MAX;
        }
    }

    return 0;
}

static int bonk_read_header(AVFormatContext *s)
{
    ID3v2ExtraMeta *extra_meta;
    AVStream *st;
    int ret;

    while (!avio_feof(s->pb)) {
        const int b = avio_r8(s->pb);
        if (!b) {
            uint32_t t;
            int ret = ffio_ensure_seekback(s->pb, 3);

            if (ret < 0)
                return ret;

            t = avio_rl32(s->pb);
            if (t == MKTAG('B','O','N','K')) {
                break;
            } else if (t == MKTAG(' ','I','D','3')) {
                avio_seek(s->pb, -3, SEEK_CUR);
                ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &extra_meta, 0);
                if (extra_meta) {
                    ff_id3v2_parse_apic(s, extra_meta);
                    ff_id3v2_parse_priv(s, extra_meta);
                    ff_id3v2_free_extra_meta(&extra_meta);
                }
                avio_skip(s->pb, 8);
            } else {
                return AVERROR_INVALIDDATA;
            }
        }
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    if ((ret = ff_get_extradata(s, st->codecpar, s->pb, 17)) < 0)
        return ret;
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_BONK;
    st->codecpar->sample_rate = AV_RL32(st->codecpar->extradata + 5);
    st->codecpar->ch_layout.nb_channels = st->codecpar->extradata[9];
    if (st->codecpar->ch_layout.nb_channels == 0)
        return AVERROR_INVALIDDATA;
    st->duration              = AV_RL32(st->codecpar->extradata + 1) / st->codecpar->ch_layout.nb_channels;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

const AVInputFormat ff_bonk_demuxer = {
    .name           = "bonk",
    .long_name      = NULL_IF_CONFIG_SMALL("raw Bonk"),
    .read_probe     = bonk_probe,
    .read_header    = bonk_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .extensions     = "bonk",
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
    .raw_codec_id   = AV_CODEC_ID_BONK,
    .priv_data_size = sizeof(FFRawDemuxerContext),
    .priv_class     = &ff_raw_demuxer_class,
};
