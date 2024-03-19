/*
 * D-Cinema audio muxer
 * Copyright (c) 2005 Reimar Döffinger
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
#include "mux.h"

static int daud_init(struct AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    int ret;

    if (par->ch_layout.nb_channels != 6) {
        av_log(s, AV_LOG_ERROR,
               "Invalid number of channels %d, must be exactly 6\n",
               par->ch_layout.nb_channels);
        return AVERROR(EINVAL);
    }

    if (par->sample_rate != 96000) {
        av_log(s, AV_LOG_ERROR,
               "Invalid sample rate %d, must be 96000\n",
               par->sample_rate);
        return AVERROR(EINVAL);
    }

    ret = ff_stream_add_bitstream_filter(s->streams[0], "pcm_rechunk", "n=2000:pad=0");
    if (ret < 0)
        return ret;

    return 0;
}

static int daud_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    avio_wb16(s->pb, pkt->size);
    avio_wb16(s->pb, 0x8010); // unknown
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

const FFOutputFormat ff_daud_muxer = {
    .p.name        = "daud",
    .p.long_name   = NULL_IF_CONFIG_SMALL("D-Cinema audio"),
    .p.extensions  = "302",
    .p.audio_codec = AV_CODEC_ID_PCM_S24DAUD,
    .p.video_codec = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .p.flags       = AVFMT_NOTIMESTAMPS,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .init         = daud_init,
    .write_packet = daud_write_packet,
};
