/*
 * Simon & Schuster Interactive VAG demuxer
 *
 * Copyright (C) 2020 Zane van Iperen (zane@zanevaniperen.com)
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
#include "internal.h"
#include "libavutil/intreadwrite.h"

#define KVAG_TAG            MKTAG('K', 'V', 'A', 'G')
#define KVAG_HEADER_SIZE    14
#define KVAG_MAX_READ_SIZE  4096

typedef struct KVAGHeader {
    uint32_t    magic;
    uint32_t    data_size;
    uint32_t    sample_rate;
    uint16_t    stereo;
} KVAGHeader;

static int kvag_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != KVAG_TAG)
        return 0;

    return AVPROBE_SCORE_EXTENSION + 1;
}

static int kvag_read_header(AVFormatContext *s)
{
    int ret;
    AVStream *st;
    KVAGHeader hdr;
    AVCodecParameters *par;
    uint8_t buf[KVAG_HEADER_SIZE];

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    if ((ret = avio_read(s->pb, buf, KVAG_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != KVAG_HEADER_SIZE)
        return AVERROR(EIO);

    hdr.magic                   = AV_RL32(buf +  0);
    hdr.data_size               = AV_RL32(buf +  4);
    hdr.sample_rate             = AV_RL32(buf +  8);
    hdr.stereo                  = AV_RL16(buf + 12);

    par                         = st->codecpar;
    par->codec_type             = AVMEDIA_TYPE_AUDIO;
    par->codec_id               = AV_CODEC_ID_ADPCM_IMA_SSI;
    par->format                 = AV_SAMPLE_FMT_S16;

    if (hdr.stereo) {
        par->channel_layout     = AV_CH_LAYOUT_STEREO;
        par->channels           = 2;
    } else {
        par->channel_layout     = AV_CH_LAYOUT_MONO;
        par->channels           = 1;
    }

    par->sample_rate            = hdr.sample_rate;
    par->bits_per_coded_sample  = 4;
    par->bits_per_raw_sample    = 16;
    par->block_align            = 1;
    par->bit_rate               = par->channels *
                                  par->sample_rate *
                                  par->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    st->start_time              = 0;
    st->duration                = hdr.data_size *
                                  (8 / par->bits_per_coded_sample) /
                                  par->channels;

    return 0;
}

static int kvag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if ((ret = av_get_packet(s->pb, pkt, KVAG_MAX_READ_SIZE)) < 0)
        return ret;

    pkt->flags          &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index   = 0;
    pkt->duration       = ret * (8 / par->bits_per_coded_sample) / par->channels;

    return 0;
}

AVInputFormat ff_kvag_demuxer = {
    .name           = "kvag",
    .long_name      = NULL_IF_CONFIG_SMALL("Simon & Schuster Interactive VAG"),
    .read_probe     = kvag_probe,
    .read_header    = kvag_read_header,
    .read_packet    = kvag_read_packet
};
