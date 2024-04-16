/*
 * LC3 muxer and demuxer
 * Copyright (C) 2024  Antoine Soulier <asoulier@google.com>
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

/**
 * @file
 * Based on the file format specified by :
 *
 * - Bluetooth SIG - Low Complexity Communication Codec Test Suite
 *   https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=502301
 *   3.2.8.2 Reference LC3 Codec Bitstream Format
 *
 * - ETSI TI 103 634 V1.4.1 - Low Complexity Communication Codec plus
 *   https://www.etsi.org/deliver/etsi_ts/103600_103699/103634/01.04.01_60/ts_103634v010401p.pdf
 *   LC3plus conformance script package
 */

#include "config_components.h"

#include "libavcodec/packet.h"
#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "avio.h"
#include "demux.h"
#include "internal.h"
#include "mux.h"

static int check_frame_length(void *avcl, int srate_hz, int frame_us)
{
    if (srate_hz !=  8000 && srate_hz != 16000 && srate_hz != 24000 &&
        srate_hz != 32000 && srate_hz != 48000 && srate_hz != 96000) {
        if (avcl)
            av_log(avcl, AV_LOG_ERROR,
                   "Invalid LC3 sample rate: %d Hz.\n", srate_hz);
        return -1;
    }

    if (frame_us != 2500 && frame_us !=  5000 &&
        frame_us != 7500 && frame_us != 10000) {
        if (avcl)
            av_log(avcl, AV_LOG_ERROR,
                   "Invalid LC3 frame duration: %.1f ms.\n", frame_us / 1000.f);
        return -1;
    }

    return 0;
}

#if CONFIG_LC3_DEMUXER

typedef struct LC3DemuxContext {
    int frame_samples;
    int64_t end_dts;
} LC3DemuxContext;

static int lc3_read_probe(const AVProbeData *p)
{
    int frame_us, srate_hz;

    if (p->buf_size < 12)
        return 0;

    if (AV_RB16(p->buf + 0) != 0x1ccc ||
        AV_RL16(p->buf + 2) <  9 * sizeof(uint16_t))
        return 0;

    srate_hz = AV_RL16(p->buf + 4) * 100;
    frame_us = AV_RL16(p->buf + 10) * 10;
    if (check_frame_length(NULL, srate_hz, frame_us) < 0)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int lc3_read_header(AVFormatContext *s)
{
    LC3DemuxContext *lc3 = s->priv_data;
    AVStream *st = NULL;
    uint16_t tag, hdr_size;
    uint32_t length;
    int srate_hz, frame_us, channels, bit_rate;
    int ep_mode, hr_mode;
    int num_extra_params;
    int delay, ret;

    tag = avio_rb16(s->pb);
    hdr_size = avio_rl16(s->pb);

    if (tag != 0x1ccc || hdr_size < 9 * sizeof(uint16_t))
        return AVERROR_INVALIDDATA;

    num_extra_params = hdr_size / sizeof(uint16_t) - 9;

    srate_hz = avio_rl16(s->pb) * 100;
    bit_rate = avio_rl16(s->pb) * 100;
    channels = avio_rl16(s->pb);
    frame_us = avio_rl16(s->pb) * 10;
    ep_mode  = avio_rl16(s->pb) != 0;
    length   = avio_rl32(s->pb);
    hr_mode  = num_extra_params >= 1 && avio_rl16(s->pb);

    if (check_frame_length(s, srate_hz, frame_us) < 0)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, srate_hz);
    avpriv_update_cur_dts(s, st, 0);
    st->duration = length;

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_LC3;
    st->codecpar->sample_rate = srate_hz;
    st->codecpar->bit_rate = bit_rate;
    st->codecpar->ch_layout.nb_channels = channels;

    if ((ret = ff_alloc_extradata(st->codecpar, 6)) < 0)
        return ret;

    AV_WL16(st->codecpar->extradata + 0, frame_us / 10);
    AV_WL16(st->codecpar->extradata + 2, ep_mode);
    AV_WL16(st->codecpar->extradata + 4, hr_mode);

    lc3->frame_samples = av_rescale(frame_us, srate_hz, 1000*1000);

    delay = av_rescale(frame_us == 7500 ? 4000 : 2500, srate_hz, 1000*1000);
    lc3->end_dts = length ? length + delay : -1;

    return 0;
}

static int lc3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    LC3DemuxContext *lc3 = s->priv_data;
    AVStream *st = s->streams[0];
    AVIOContext *pb = s->pb;
    int64_t pos = avio_tell(pb);
    int64_t remaining_samples;
    int ret;

    ret = av_get_packet(s->pb, pkt, avio_rl16(pb));
    if (ret < 0)
        return ret;

    pkt->pos = pos;

    remaining_samples = lc3->end_dts < 0 ? lc3->frame_samples :
                        FFMAX(lc3->end_dts - ffstream(st)->cur_dts, 0);
    pkt->duration = FFMIN(lc3->frame_samples, remaining_samples);

    return 0;
}

const FFInputFormat ff_lc3_demuxer = {
    .p.name         = "lc3",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LC3 (Low Complexity Communication Codec)"),
    .p.extensions   = "lc3",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .priv_data_size = sizeof(LC3DemuxContext),
    .read_probe     = lc3_read_probe,
    .read_header    = lc3_read_header,
    .read_packet    = lc3_read_packet,
};

#endif /* CONFIG_LC3_DEMUXER */

#if CONFIG_LC3_MUXER

static int lc3_write_header(AVFormatContext *s)
{
    AVStream *st = s->streams[0];
    int channels = st->codecpar->ch_layout.nb_channels;
    int srate_hz = st->codecpar->sample_rate;
    int bit_rate = st->codecpar->bit_rate;
    int frame_us, ep_mode, hr_mode;
    uint32_t nb_samples = av_rescale_q(
        st->duration, st->time_base, (AVRational){ 1, srate_hz });

    if (st->codecpar->extradata_size < 6)
        return AVERROR_INVALIDDATA;

    frame_us = AV_RL16(st->codecpar->extradata + 0) * 10;
    ep_mode = AV_RL16(st->codecpar->extradata + 2) != 0;
    hr_mode = AV_RL16(st->codecpar->extradata + 4) != 0;

    if (check_frame_length(s, srate_hz, frame_us) < 0)
        return AVERROR_INVALIDDATA;

    avio_wb16(s->pb, 0x1ccc);
    avio_wl16(s->pb, (9 + hr_mode) * sizeof(uint16_t));
    avio_wl16(s->pb, srate_hz / 100);
    avio_wl16(s->pb, bit_rate / 100);
    avio_wl16(s->pb, channels);
    avio_wl16(s->pb, frame_us / 10);
    avio_wl16(s->pb, ep_mode);
    avio_wl32(s->pb, nb_samples);
    if (hr_mode)
        avio_wl16(s->pb, hr_mode);

    return 0;
}

static int lc3_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_wl16(s->pb, pkt->size);
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

const FFOutputFormat ff_lc3_muxer = {
    .p.name        = "lc3",
    .p.long_name   = NULL_IF_CONFIG_SMALL("LC3 (Low Complexity Communication Codec)"),
    .p.extensions  = "lc3",
    .p.audio_codec = AV_CODEC_ID_LC3,
    .p.video_codec = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .p.flags       = AVFMT_NOTIMESTAMPS,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .write_header  = lc3_write_header,
    .write_packet  = lc3_write_packet,
};

#endif /* CONFIG_LC3_MUXER */
