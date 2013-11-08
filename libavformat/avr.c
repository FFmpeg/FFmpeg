/*
 * AVR demuxer
 * Copyright (c) 2012 Paul B Mahol
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
#include "avformat.h"
#include "internal.h"
#include "pcm.h"

static int avr_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('2', 'B', 'I', 'T'))
        return AVPROBE_SCORE_EXTENSION;
    return 0;
}

static int avr_read_header(AVFormatContext *s)
{
    uint16_t chan, sign, bps;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

    avio_skip(s->pb, 4); // magic
    avio_skip(s->pb, 8); // sample_name

    chan = avio_rb16(s->pb);
    if (!chan) {
        st->codec->channels = 1;
    } else if (chan == 0xFFFFu) {
        st->codec->channels = 2;
    } else {
        avpriv_request_sample(s, "chan %d", chan);
        return AVERROR_PATCHWELCOME;
    }

    st->codec->bits_per_coded_sample = bps = avio_rb16(s->pb);

    sign = avio_rb16(s->pb);

    avio_skip(s->pb, 2); // loop
    avio_skip(s->pb, 2); // midi
    avio_skip(s->pb, 1); // replay speed

    st->codec->sample_rate = avio_rb24(s->pb);
    avio_skip(s->pb, 4 * 3);
    avio_skip(s->pb, 2 * 3);
    avio_skip(s->pb, 20);
    avio_skip(s->pb, 64);

    st->codec->codec_id = ff_get_pcm_codec_id(bps, 0, 1, sign);
    if (st->codec->codec_id == AV_CODEC_ID_NONE) {
        avpriv_request_sample(s, "Bps %d and sign %d", bps, sign);
        return AVERROR_PATCHWELCOME;
    }

    st->codec->block_align = bps * st->codec->channels / 8;

    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    return 0;
}

AVInputFormat ff_avr_demuxer = {
    .name           = "avr",
    .long_name      = NULL_IF_CONFIG_SMALL("AVR (Audio Visual Research)"),
    .read_probe     = avr_probe,
    .read_header    = avr_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .extensions     = "avr",
    .flags          = AVFMT_GENERIC_INDEX,
};
