/*
 * Simon & Schuster Interactive VAG (de)muxer
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

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "rawenc.h"
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

#if CONFIG_KVAG_DEMUXER
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

    if ((ret = ffio_read_size(s->pb, buf, KVAG_HEADER_SIZE)) < 0)
        return ret;

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
    par->block_align            = 1;
    par->bit_rate               = par->channels *
                                  (uint64_t)par->sample_rate *
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

static int kvag_seek(AVFormatContext *s, int stream_index,
                     int64_t pts, int flags)
{
    if (pts != 0)
        return AVERROR(EINVAL);

    return avio_seek(s->pb, KVAG_HEADER_SIZE, SEEK_SET);
}

const AVInputFormat ff_kvag_demuxer = {
    .name           = "kvag",
    .long_name      = NULL_IF_CONFIG_SMALL("Simon & Schuster Interactive VAG"),
    .read_probe     = kvag_probe,
    .read_header    = kvag_read_header,
    .read_packet    = kvag_read_packet,
    .read_seek      = kvag_seek,
};
#endif

#if CONFIG_KVAG_MUXER
static int kvag_write_init(AVFormatContext *s)
{
    AVCodecParameters *par;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "KVAG files have exactly one stream\n");
        return AVERROR(EINVAL);
    }

    par = s->streams[0]->codecpar;

    if (par->codec_id != AV_CODEC_ID_ADPCM_IMA_SSI) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    if (par->channels > 2) {
        av_log(s, AV_LOG_ERROR, "KVAG files only support up to 2 channels\n");
        return AVERROR(EINVAL);
    }

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_WARNING, "Stream not seekable, unable to write output file\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int kvag_write_header(AVFormatContext *s)
{
    uint8_t buf[KVAG_HEADER_SIZE];
    AVCodecParameters *par = s->streams[0]->codecpar;

    AV_WL32(buf +  0, KVAG_TAG);
    AV_WL32(buf +  4, 0); /* Data size, we fix this up later. */
    AV_WL32(buf +  8, par->sample_rate);
    AV_WL16(buf + 12, par->channels == 2);

    avio_write(s->pb, buf, sizeof(buf));
    return 0;
}

static int kvag_write_trailer(AVFormatContext *s)
{
    int64_t file_size, data_size;

    file_size = avio_tell(s->pb);
    data_size = file_size - KVAG_HEADER_SIZE;
    if (data_size < UINT32_MAX) {
        avio_seek(s->pb, 4, SEEK_SET);
        avio_wl32(s->pb, (uint32_t)data_size);
        avio_seek(s->pb, file_size, SEEK_SET);
    } else {
        av_log(s, AV_LOG_WARNING,
               "Filesize %"PRId64" invalid for KVAG, output file will be broken\n",
               file_size);
    }

    return 0;
}

const AVOutputFormat ff_kvag_muxer = {
    .name           = "kvag",
    .long_name      = NULL_IF_CONFIG_SMALL("Simon & Schuster Interactive VAG"),
    .extensions     = "vag",
    .audio_codec    = AV_CODEC_ID_ADPCM_IMA_SSI,
    .video_codec    = AV_CODEC_ID_NONE,
    .init           = kvag_write_init,
    .write_header   = kvag_write_header,
    .write_packet   = ff_raw_write_packet,
    .write_trailer  = kvag_write_trailer
};
#endif
