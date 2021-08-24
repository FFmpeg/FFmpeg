/*
 * Simbiosis game demuxer
 *
 * Copyright (C) 2021 Paul B Mahol
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
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"

#define IMX_TAG MKTAG('I', 'M', 'A', 'X')

typedef struct SimbiosisIMXDemuxContext {
    uint8_t pal[AVPALETTE_SIZE];
    int pal_changed;
    int64_t first_video_packet_pos;
} SimbiosisIMXDemuxContext;

static int simbiosis_imx_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != IMX_TAG)
        return 0;
    if (AV_RN32(p->buf+4) == 0)
        return 0;
    if (AV_RN16(p->buf+8) == 0)
        return 0;
    if (AV_RL16(p->buf+10) != 0x102)
        return 0;

    return AVPROBE_SCORE_EXTENSION + 10;
}

static int simbiosis_imx_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *vst, *ast;
    int rate;

    vst = avformat_new_stream(s, NULL);
    ast = avformat_new_stream(s, NULL);
    if (!vst || !ast)
        return AVERROR(ENOMEM);

    avio_skip(pb, 4);

    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_tag  = 0;
    vst->codecpar->format     = AV_PIX_FMT_PAL8;
    vst->codecpar->codec_id   = AV_CODEC_ID_SIMBIOSIS_IMX;
    vst->start_time = 0;
    vst->duration =
    vst->nb_frames = avio_rl32(pb);
    rate = avio_rl16(pb);
    avio_skip(pb, 12);

    avpriv_set_pts_info(vst, 64, 1, rate);

    ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->codec_tag  = 0;
    ast->codecpar->codec_id   = AV_CODEC_ID_PCM_U8;
    ast->codecpar->ch_layout  = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    ast->codecpar->sample_rate = 22050;
    ast->start_time = 0;

    avpriv_set_pts_info(ast, 64, 1, 22050);

    return 0;
}

static int simbiosis_imx_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    SimbiosisIMXDemuxContext *imx = s->priv_data;
    uint32_t chunk_size, chunk_type;
    int64_t pos = avio_tell(pb);
    int ret, idx = -1;

retry:
    if (avio_feof(pb))
        return AVERROR_EOF;

    chunk_size = avio_rl32(pb);
    chunk_type = avio_rl32(pb);

    switch (chunk_type) {
    case 0xAAFF:
        return AVERROR_EOF;
    case 0xAA99:
        idx = 1;
        break;
    case 0xAA97:
        idx = 0;
        if (!imx->first_video_packet_pos)
            imx->first_video_packet_pos = pos;
        break;
    case 0xAA98:
        if (chunk_size > 256 * 3)
            return AVERROR_INVALIDDATA;
        for (int i = 0; i < chunk_size / 3; i++) {
            unsigned r = avio_r8(pb) << 18;
            unsigned g = avio_r8(pb) << 10;
            unsigned b = avio_r8(pb) <<  2;

            AV_WL32(imx->pal + i * 4, (0xFFU << 24) | r | g | b);
        }
        imx->pal_changed = 1;
        idx = -1;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    if (idx == -1)
        goto retry;

    ret = av_get_packet(pb, pkt, chunk_size);
    if (ret < 0)
        return ret;

    if (imx->pal_changed && idx == 0) {
        uint8_t *pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                               AVPALETTE_SIZE);
        if (!pal)
            return AVERROR(ENOMEM);
        memcpy(pal, imx->pal, AVPALETTE_SIZE);
        imx->pal_changed = 0;
        if (pos <= imx->first_video_packet_pos)
            pkt->flags |= AV_PKT_FLAG_KEY;
    } else if (idx == 1) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    pkt->pos = pos;
    pkt->stream_index = idx;
    pkt->duration = idx ? chunk_size : 1;

    return ret;
}

const AVInputFormat ff_simbiosis_imx_demuxer = {
    .name           = "simbiosis_imx",
    .long_name      = NULL_IF_CONFIG_SMALL("Simbiosis Interactive IMX"),
    .priv_data_size = sizeof(SimbiosisIMXDemuxContext),
    .read_probe     = simbiosis_imx_probe,
    .read_header    = simbiosis_imx_read_header,
    .read_packet    = simbiosis_imx_read_packet,
    .extensions     = "imx",
    .flags          = AVFMT_GENERIC_INDEX,
};
