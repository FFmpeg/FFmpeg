/*
 * CRYO APC audio format demuxer
 * Copyright (c) 2007 Anssi Hannula <anssi.hannula@gmail.com>
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

#include <string.h>
#include "avformat.h"

static int apc_probe(AVProbeData *p)
{
    if (!strncmp(p->buf, "CRYO_APC", 8))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int apc_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVIOContext *pb = s->pb;
    AVStream *st;

    avio_rl32(pb); /* CRYO */
    avio_rl32(pb); /* _APC */
    avio_rl32(pb); /* 1.20 */

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_ADPCM_IMA_APC;

    avio_rl32(pb); /* number of samples */
    st->codec->sample_rate = avio_rl32(pb);

    st->codec->extradata_size = 2 * 4;
    st->codec->extradata = av_malloc(st->codec->extradata_size +
                                     FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);

    /* initial predictor values for adpcm decoder */
    avio_read(pb, st->codec->extradata, 2 * 4);

    st->codec->channels = 1;
    if (avio_rl32(pb))
        st->codec->channels = 2;

    st->codec->bits_per_coded_sample = 4;
    st->codec->bit_rate = st->codec->bits_per_coded_sample * st->codec->channels
                          * st->codec->sample_rate;
    st->codec->block_align = 1;

    return 0;
}

#define MAX_READ_SIZE 4096

static int apc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (av_get_packet(s->pb, pkt, MAX_READ_SIZE) <= 0)
        return AVERROR(EIO);
    pkt->stream_index = 0;
    return 0;
}

AVInputFormat ff_apc_demuxer = {
    .name           = "apc",
    .long_name      = NULL_IF_CONFIG_SMALL("CRYO APC format"),
    .read_probe     = apc_probe,
    .read_header    = apc_read_header,
    .read_packet    = apc_read_packet,
};
