/*
 * Rayman 2 APM Demuxer
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
#include "riff.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#define APM_FILE_HEADER_SIZE    20
#define APM_VS12_CHUNK_SIZE     76
#define APM_MAX_READ_SIZE       4096

#define APM_TAG_VS12            MKTAG('v', 's', '1', '2')
#define APM_TAG_DATA            MKTAG('D', 'A', 'T', 'A')

typedef struct APMState {
    int32_t     has_saved;
    int32_t     predictor_r;
    int32_t     step_index_r;
    int32_t     saved_r;
    int32_t     predictor_l;
    int32_t     step_index_l;
    int32_t     saved_l;
} APMState;

typedef struct APMVS12Chunk {
    uint32_t    magic;
    uint32_t    file_size;
    uint32_t    data_size;
    uint32_t    unk1;
    uint32_t    unk2;
    APMState    state;
    uint32_t    pad[7];
} APMVS12Chunk;

static void apm_parse_vs12(APMVS12Chunk *vs12, const uint8_t *buf)
{
    vs12->magic                 = AV_RL32(buf + 0);
    vs12->file_size             = AV_RL32(buf + 4);
    vs12->data_size             = AV_RL32(buf + 8);
    vs12->unk1                  = AV_RL32(buf + 12);
    vs12->unk2                  = AV_RL32(buf + 16);

    vs12->state.has_saved       = AV_RL32(buf + 20);
    vs12->state.predictor_r     = AV_RL32(buf + 24);
    vs12->state.step_index_r    = AV_RL32(buf + 28);
    vs12->state.saved_r         = AV_RL32(buf + 32);
    vs12->state.predictor_l     = AV_RL32(buf + 36);
    vs12->state.step_index_l    = AV_RL32(buf + 40);
    vs12->state.saved_l         = AV_RL32(buf + 44);

    for (int i = 0; i < FF_ARRAY_ELEMS(vs12->pad); i++)
        vs12->pad[i]            = AV_RL32(buf + 48 + (i * 4));
}

static int apm_probe(const AVProbeData *p)
{
    if (p->buf_size < 100)
        return 0;

    if (AV_RL32(p->buf + 20) != APM_TAG_VS12)
        return 0;

    if (AV_RL32(p->buf + 96) != APM_TAG_DATA)
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int apm_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVStream *st;
    APMVS12Chunk vs12;
    uint8_t buf[APM_VS12_CHUNK_SIZE];

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    /* The header starts with a WAVEFORMATEX */
    if ((ret = ff_get_wav_header(s, s->pb, st->codecpar, APM_FILE_HEADER_SIZE, 0)) < 0)
        return ret;

    if (st->codecpar->bits_per_coded_sample != 4)
        return AVERROR_INVALIDDATA;

    if (st->codecpar->codec_tag != 0x2000)
        return AVERROR_INVALIDDATA;

    /* ff_get_wav_header() does most of the work, but we need to fix a few things. */
    st->codecpar->codec_id              = AV_CODEC_ID_ADPCM_IMA_APM;
    st->codecpar->codec_tag             = 0;

    if (st->codecpar->channels == 2)
        st->codecpar->channel_layout    = AV_CH_LAYOUT_STEREO;
    else if (st->codecpar->channels == 1)
        st->codecpar->channel_layout    = AV_CH_LAYOUT_MONO;
    else
        return AVERROR_INVALIDDATA;

    st->codecpar->format                = AV_SAMPLE_FMT_S16;
    st->codecpar->bits_per_raw_sample   = 16;
    st->codecpar->bit_rate              = st->codecpar->channels *
                                          st->codecpar->sample_rate *
                                          st->codecpar->bits_per_coded_sample;

    if ((ret = avio_read(s->pb, buf, APM_VS12_CHUNK_SIZE)) < 0)
        return ret;
    else if (ret != APM_VS12_CHUNK_SIZE)
        return AVERROR(EIO);

    apm_parse_vs12(&vs12, buf);

    if (vs12.magic != APM_TAG_VS12) {
        return AVERROR_INVALIDDATA;
    }

    if (vs12.state.has_saved) {
        avpriv_request_sample(s, "Saved Samples");
        return AVERROR_PATCHWELCOME;
    }

    if (avio_rl32(s->pb) != APM_TAG_DATA)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_alloc_extradata(st->codecpar, 16)) < 0)
        return ret;

    AV_WL32(st->codecpar->extradata +  0, vs12.state.predictor_l);
    AV_WL32(st->codecpar->extradata +  4, vs12.state.step_index_l);
    AV_WL32(st->codecpar->extradata +  8, vs12.state.predictor_r);
    AV_WL32(st->codecpar->extradata + 12, vs12.state.step_index_r);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time  = 0;
    st->duration    = vs12.data_size *
                      (8 / st->codecpar->bits_per_coded_sample) /
                      st->codecpar->channels;
    return 0;
}

static int apm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    /*
     * For future reference: if files with the `has_saved` field set ever
     * surface, `saved_l`, and `saved_r` will each contain 8 "saved" samples
     * that should be sent to the decoder before the actual data.
     */

    if ((ret = av_get_packet(s->pb, pkt, APM_MAX_READ_SIZE)) < 0)
        return ret;

    pkt->flags          &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index   = 0;
    pkt->duration       = ret * (8 / par->bits_per_coded_sample) / par->channels;

    return 0;
}

AVInputFormat ff_apm_demuxer = {
    .name           = "apm",
    .long_name      = NULL_IF_CONFIG_SMALL("Ubisoft Rayman 2 APM"),
    .read_probe     = apm_probe,
    .read_header    = apm_read_header,
    .read_packet    = apm_read_packet
};
