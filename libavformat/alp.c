/*
 * LEGO Racers ALP (.tun & .pcm) demuxer
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
#include "libavutil/internal.h"

#define ALP_TAG            MKTAG('A', 'L', 'P', ' ')
#define ALP_MAX_READ_SIZE  4096

typedef struct ALPHeader {
    uint32_t    magic;          /*< Magic Number, {'A', 'L', 'P', ' '} */
    uint32_t    header_size;    /*< Header size (after this). */
    char        adpcm[6];       /*< "ADPCM" */
    uint8_t     unk1;           /*< Unknown */
    uint8_t     num_channels;   /*< Channel Count. */
    uint32_t    sample_rate;    /*< Sample rate, only if header_size >= 12. */
} ALPHeader;

static int alp_probe(const AVProbeData *p)
{
    uint32_t i;

    if (AV_RL32(p->buf) != ALP_TAG)
        return 0;

    /* Only allowed header sizes are 8 and 12. */
    i = AV_RL32(p->buf + 4);
    if (i != 8 && i != 12)
        return 0;

    if (strncmp("ADPCM", p->buf + 8, 6) != 0)
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int alp_read_header(AVFormatContext *s)
{
    int ret;
    AVStream *st;
    ALPHeader hdr;
    AVCodecParameters *par;

    if ((hdr.magic = avio_rl32(s->pb)) != ALP_TAG)
        return AVERROR_INVALIDDATA;

    hdr.header_size = avio_rl32(s->pb);

    if (hdr.header_size != 8 && hdr.header_size != 12) {
        return AVERROR_INVALIDDATA;
    }

    if ((ret = avio_read(s->pb, hdr.adpcm, sizeof(hdr.adpcm))) < 0)
        return ret;
    else if (ret != sizeof(hdr.adpcm))
        return AVERROR(EIO);

    if (strncmp("ADPCM", hdr.adpcm, sizeof(hdr.adpcm)) != 0)
        return AVERROR_INVALIDDATA;

    hdr.unk1                    = avio_r8(s->pb);
    hdr.num_channels            = avio_r8(s->pb);

    if (hdr.header_size == 8) {
        /* .TUN music file */
        hdr.sample_rate         = 11025 * hdr.num_channels;
    } else {
        /* .PCM sound file */
        hdr.sample_rate         = avio_rl32(s->pb);
    }

    if (hdr.sample_rate > 44100) {
        avpriv_request_sample(s, "Sample Rate > 44100");
        return AVERROR_PATCHWELCOME;
    }

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    par                         = st->codecpar;
    par->codec_type             = AVMEDIA_TYPE_AUDIO;
    par->codec_id               = AV_CODEC_ID_ADPCM_IMA_ALP;
    par->format                 = AV_SAMPLE_FMT_S16;
    par->sample_rate            = hdr.sample_rate;
    par->channels               = hdr.num_channels;

    if (hdr.num_channels == 1)
        par->channel_layout     = AV_CH_LAYOUT_MONO;
    else if (hdr.num_channels == 2)
        par->channel_layout     = AV_CH_LAYOUT_STEREO;
    else
        return AVERROR_INVALIDDATA;

    par->bits_per_coded_sample  = 4;
    par->bits_per_raw_sample    = 16;
    par->block_align            = 1;
    par->bit_rate               = par->channels *
                                  par->sample_rate *
                                  par->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    return 0;
}

static int alp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if ((ret = av_get_packet(s->pb, pkt, ALP_MAX_READ_SIZE)) < 0)
        return ret;

    pkt->flags         &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index   = 0;
    pkt->duration       = ret * 2 / par->channels;

    return 0;
}

AVInputFormat ff_alp_demuxer = {
    .name           = "alp",
    .long_name      = NULL_IF_CONFIG_SMALL("LEGO Racers ALP"),
    .read_probe     = alp_probe,
    .read_header    = alp_read_header,
    .read_packet    = alp_read_packet
};
