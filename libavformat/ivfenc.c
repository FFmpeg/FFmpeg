/*
 * Copyright (c) 2010 Reimar Döffinger
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

typedef struct IVFEncContext {
    unsigned frame_cnt;
    uint64_t last_pts, sum_delta_pts, last_pkt_duration;
} IVFEncContext;

static int ivf_init(AVFormatContext *s)
{
    AVCodecParameters *par;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Format supports only exactly one video stream\n");
        return AVERROR(EINVAL);
    }
    par = s->streams[0]->codecpar;
    if (par->codec_type != AVMEDIA_TYPE_VIDEO ||
        !(par->codec_id == AV_CODEC_ID_AV1 ||
          par->codec_id == AV_CODEC_ID_VP8 ||
          par->codec_id == AV_CODEC_ID_VP9)) {
        av_log(s, AV_LOG_ERROR, "Currently only VP8, VP9 and AV1 are supported!\n");
        return AVERROR(EINVAL);
    }

    if (par->codec_id == AV_CODEC_ID_VP9) {
        int ret = ff_stream_add_bitstream_filter(s->streams[0], "vp9_superframe", NULL);
        if (ret < 0)
            return ret;
    } else if (par->codec_id == AV_CODEC_ID_AV1) {
        int ret = ff_stream_add_bitstream_filter(s->streams[0], "av1_metadata", "td=insert");
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int ivf_write_header(AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    AVIOContext *pb = s->pb;

    avio_write(pb, "DKIF", 4);
    avio_wl16(pb, 0); // version
    avio_wl16(pb, 32); // header length
    avio_wl32(pb,
              par->codec_id == AV_CODEC_ID_VP9 ? AV_RL32("VP90") :
              par->codec_id == AV_CODEC_ID_VP8 ? AV_RL32("VP80") : AV_RL32("AV01"));
    avio_wl16(pb, par->width);
    avio_wl16(pb, par->height);
    avio_wl32(pb, s->streams[0]->time_base.den);
    avio_wl32(pb, s->streams[0]->time_base.num);
    avio_wl64(pb, 0xFFFFFFFFFFFFFFFFULL); // length is overwritten at the end of muxing

    return 0;
}

static int ivf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    IVFEncContext *ctx = s->priv_data;

    avio_wl32(pb, pkt->size);
    avio_wl64(pb, pkt->pts);
    avio_write(pb, pkt->data, pkt->size);
    if (ctx->frame_cnt)
        ctx->sum_delta_pts += pkt->pts - ctx->last_pts;
    ctx->last_pkt_duration = pkt->duration;
    ctx->frame_cnt++;
    ctx->last_pts = pkt->pts;

    return 0;
}

static int ivf_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    IVFEncContext *ctx = s->priv_data;

    if ((pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        (ctx->frame_cnt > 1 || (ctx->frame_cnt == 1 && ctx->last_pkt_duration))) {
        int64_t end = avio_tell(pb);

        avio_seek(pb, 24, SEEK_SET);
        // overwrite the "length" field (duration)
        avio_wl32(pb, ctx->last_pkt_duration ?
                  ctx->sum_delta_pts + ctx->last_pkt_duration :
                  ctx->frame_cnt * ctx->sum_delta_pts / (ctx->frame_cnt - 1));
        avio_wl32(pb, 0); // zero out unused bytes
        avio_seek(pb, end, SEEK_SET);
    }

    return 0;
}

static const AVCodecTag codec_ivf_tags[] = {
    { AV_CODEC_ID_VP8,  MKTAG('V', 'P', '8', '0') },
    { AV_CODEC_ID_VP9,  MKTAG('V', 'P', '9', '0') },
    { AV_CODEC_ID_AV1,  MKTAG('A', 'V', '0', '1') },
    { AV_CODEC_ID_NONE, 0 }
};

AVOutputFormat ff_ivf_muxer = {
    .priv_data_size = sizeof(IVFEncContext),
    .name         = "ivf",
    .long_name    = NULL_IF_CONFIG_SMALL("On2 IVF"),
    .extensions   = "ivf",
    .audio_codec  = AV_CODEC_ID_NONE,
    .video_codec  = AV_CODEC_ID_VP8,
    .init         = ivf_init,
    .write_header = ivf_write_header,
    .write_packet = ivf_write_packet,
    .write_trailer = ivf_write_trailer,
    .codec_tag    = (const AVCodecTag* const []){ codec_ivf_tags, 0 },
};
