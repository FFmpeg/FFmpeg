/*
 * IRCAM demuxer
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
#include "libavcodec/internal.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"
#include "ircam.h"

static int ircam_probe(const AVProbeData *p)
{
    if ((p->buf[0] == 0x64 && p->buf[1] == 0xA3 && p->buf[3] == 0x00 &&
         p->buf[2] >=    1 && p->buf[2] <= 4) ||
        (p->buf[3] == 0x64 && p->buf[2] == 0xA3 && p->buf[0] == 0x00 &&
         p->buf[1] >=    1 && p->buf[1] <= 3) &&
        AV_RN32(p->buf + 4) && AV_RN32(p->buf + 8))
        return AVPROBE_SCORE_MAX / 4 * 3;
    return 0;
}

static const struct endianess {
    uint32_t magic;
    int      is_le;
} table[] = {
  { 0x64A30100, 0 },
  { 0x64A30200, 1 },
  { 0x64A30300, 0 },
  { 0x64A30400, 1 },
  { 0x0001A364, 1 },
  { 0x0002A364, 0 },
  { 0x0003A364, 1 },
};

static int ircam_read_header(AVFormatContext *s)
{
    uint32_t magic, sample_rate, channels, tag;
    const AVCodecTag *tags;
    int le = -1, i;
    AVStream *st;

    magic = avio_rl32(s->pb);
    for (i = 0; i < 7; i++) {
        if (magic == table[i].magic) {
            le = table[i].is_le;
            break;
        }
    }

    if (le == 1) {
        sample_rate = lrintf(av_int2float(avio_rl32(s->pb)));
        channels    = avio_rl32(s->pb);
        tag         = avio_rl32(s->pb);
        tags        = ff_codec_ircam_le_tags;
    } else if (le == 0) {
        sample_rate = lrintf(av_int2float(avio_rb32(s->pb)));
        channels    = avio_rb32(s->pb);
        tag         = avio_rb32(s->pb);
        tags        = ff_codec_ircam_be_tags;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (!channels || !sample_rate)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->ch_layout.nb_channels = channels;
    if (st->codecpar->ch_layout.nb_channels > FF_SANE_NB_CHANNELS)
        return AVERROR(ENOSYS);
    st->codecpar->sample_rate = sample_rate;

    st->codecpar->codec_id = ff_codec_get_id(tags, tag);
    if (st->codecpar->codec_id == AV_CODEC_ID_NONE) {
        av_log(s, AV_LOG_ERROR, "unknown tag %"PRIx32"\n", tag);
        return AVERROR_INVALIDDATA;
    }

    st->codecpar->bits_per_coded_sample = av_get_bits_per_sample(st->codecpar->codec_id);
    st->codecpar->block_align = st->codecpar->bits_per_coded_sample *
                                st->codecpar->ch_layout.nb_channels / 8;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    avio_skip(s->pb, 1008);

    return 0;
}

const AVInputFormat ff_ircam_demuxer = {
    .name           = "ircam",
    .long_name      = NULL_IF_CONFIG_SMALL("Berkeley/IRCAM/CARL Sound Format"),
    .read_probe     = ircam_probe,
    .read_header    = ircam_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .extensions     = "sf,ircam",
    .flags          = AVFMT_GENERIC_INDEX,
};
