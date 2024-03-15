/*
 * MD STUDIO audio demuxer
 *
 * Copyright (c) 2009 Benjamin Larsson
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "pcm.h"

#define AT1_SU_SIZE 212

static int aea_read_probe(const AVProbeData *p)
{
    if (p->buf_size <= 2048+AT1_SU_SIZE)
        return 0;

    /* Magic is '00 08 00 00' in little-endian*/
    if (AV_RL32(p->buf)==0x800) {
        int ch, block_size, score = 0;
        ch = p->buf[264];

        if (ch != 1 && ch != 2)
            return 0;

        block_size = ch * AT1_SU_SIZE;
        /* Check so that the redundant bsm bytes and info bytes are valid
         * the block size mode bytes have to be the same
         * the info bytes have to be the same
         */
        for (int i = 2048 + block_size; i + block_size <= p->buf_size; i += block_size) {
            if (AV_RN16(p->buf+i) != AV_RN16(p->buf+i+AT1_SU_SIZE))
                return 0;
            score++;
        }
        return FFMIN(AVPROBE_SCORE_MAX / 4 + score, AVPROBE_SCORE_MAX);
    }
    return 0;
}

static int aea_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    char title[256 + 1];
    int channels, ret;
    if (!st)
        return AVERROR(ENOMEM);

    /* Read the title, parse the number of channels and skip to pos 2048(0x800) */
    avio_rl32(s->pb); // magic
    ret = ffio_read_size(s->pb, title, sizeof(title) - 1);
    if (ret < 0)
        return ret;
    title[sizeof(title) - 1] = '\0';
    if (title[0] != '\0')
        av_dict_set(&st->metadata, "title", title, 0);
    avio_rl32(s->pb); // Block count
    channels = avio_r8(s->pb);
    avio_skip(s->pb, 1783);


    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id       = AV_CODEC_ID_ATRAC1;
    st->codecpar->sample_rate    = 44100;
    st->codecpar->bit_rate       = 146000 * channels;

    if (channels != 1 && channels != 2) {
        av_log(s, AV_LOG_ERROR, "Channels %d not supported!\n", channels);
        return AVERROR_INVALIDDATA;
    }

    av_channel_layout_default(&st->codecpar->ch_layout, channels);

    st->codecpar->block_align = AT1_SU_SIZE * st->codecpar->ch_layout.nb_channels;
    avpriv_set_pts_info(st, 64, 1, 44100);
    return 0;
}

static int aea_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return av_get_packet(s->pb, pkt, s->streams[0]->codecpar->block_align);
}

const FFInputFormat ff_aea_demuxer = {
    .p.name         = "aea",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MD STUDIO audio"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "aea",
    .read_probe     = aea_read_probe,
    .read_header    = aea_read_header,
    .read_packet    = aea_read_packet,
    .read_seek      = ff_pcm_read_seek,
};
