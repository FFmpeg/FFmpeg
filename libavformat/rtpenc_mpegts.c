/*
 * RTP/mpegts muxer
 * Copyright (c) 2011 Martin Storsjo
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

#include "libavutil/mathematics.h"
#include "avformat.h"
#include "avio_internal.h"

struct MuxChain {
    AVFormatContext *mpegts_ctx;
    AVFormatContext *rtp_ctx;
};

static int rtp_mpegts_write_close(AVFormatContext *s)
{
    struct MuxChain *chain = s->priv_data;

    if (chain->mpegts_ctx) {
        av_write_trailer(chain->mpegts_ctx);
        ffio_free_dyn_buf(&chain->mpegts_ctx->pb);
        avformat_free_context(chain->mpegts_ctx);
    }
    if (chain->rtp_ctx) {
        av_write_trailer(chain->rtp_ctx);
        avformat_free_context(chain->rtp_ctx);
    }
    return 0;
}

static int rtp_mpegts_write_header(AVFormatContext *s)
{
    struct MuxChain *chain = s->priv_data;
    AVFormatContext *mpegts_ctx = NULL, *rtp_ctx = NULL;
    AVOutputFormat *mpegts_format = av_guess_format("mpegts", NULL, NULL);
    AVOutputFormat *rtp_format    = av_guess_format("rtp", NULL, NULL);
    int i, ret = AVERROR(ENOMEM);
    AVStream *st;

    if (!mpegts_format || !rtp_format)
        return AVERROR(ENOSYS);
    mpegts_ctx = avformat_alloc_context();
    if (!mpegts_ctx)
        return AVERROR(ENOMEM);
    mpegts_ctx->oformat   = mpegts_format;
    mpegts_ctx->max_delay = s->max_delay;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream* st = avformat_new_stream(mpegts_ctx, NULL);
        if (!st)
            goto fail;
        st->time_base           = s->streams[i]->time_base;
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        avcodec_copy_context(st->codec, s->streams[i]->codec);
    }
    if ((ret = avio_open_dyn_buf(&mpegts_ctx->pb)) < 0)
        goto fail;
    if ((ret = avformat_write_header(mpegts_ctx, NULL)) < 0)
        goto fail;
    for (i = 0; i < s->nb_streams; i++)
        s->streams[i]->time_base = mpegts_ctx->streams[i]->time_base;

    chain->mpegts_ctx = mpegts_ctx;
    mpegts_ctx = NULL;

    rtp_ctx = avformat_alloc_context();
    if (!rtp_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    rtp_ctx->oformat = rtp_format;
    st = avformat_new_stream(rtp_ctx, NULL);
    st->time_base.num   = 1;
    st->time_base.den   = 90000;
    st->codec->codec_id = AV_CODEC_ID_MPEG2TS;
    rtp_ctx->pb = s->pb;
    if ((ret = avformat_write_header(rtp_ctx, NULL)) < 0)
        goto fail;
    chain->rtp_ctx = rtp_ctx;

    return 0;

fail:
    if (mpegts_ctx) {
        ffio_free_dyn_buf(&mpegts_ctx->pb);
        avformat_free_context(mpegts_ctx);
    }
    if (rtp_ctx)
        avformat_free_context(rtp_ctx);
    rtp_mpegts_write_close(s);
    return ret;
}

static int rtp_mpegts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct MuxChain *chain = s->priv_data;
    int ret = 0, size;
    uint8_t *buf;
    AVPacket local_pkt;

    if (!chain->mpegts_ctx->pb) {
        if ((ret = avio_open_dyn_buf(&chain->mpegts_ctx->pb)) < 0)
            return ret;
    }
    if ((ret = av_write_frame(chain->mpegts_ctx, pkt)) < 0)
        return ret;
    size = avio_close_dyn_buf(chain->mpegts_ctx->pb, &buf);
    chain->mpegts_ctx->pb = NULL;
    if (size == 0) {
        av_free(buf);
        return 0;
    }
    av_init_packet(&local_pkt);
    local_pkt.data         = buf;
    local_pkt.size         = size;
    local_pkt.stream_index = 0;
    if (pkt->pts != AV_NOPTS_VALUE)
        local_pkt.pts = av_rescale_q(pkt->pts,
                                     s->streams[pkt->stream_index]->time_base,
                                     chain->rtp_ctx->streams[0]->time_base);
    if (pkt->dts != AV_NOPTS_VALUE)
        local_pkt.dts = av_rescale_q(pkt->dts,
                                     s->streams[pkt->stream_index]->time_base,
                                     chain->rtp_ctx->streams[0]->time_base);
    ret = av_write_frame(chain->rtp_ctx, &local_pkt);
    av_free(buf);

    return ret;
}

AVOutputFormat ff_rtp_mpegts_muxer = {
    .name              = "rtp_mpegts",
    .long_name         = NULL_IF_CONFIG_SMALL("RTP/mpegts output format"),
    .priv_data_size    = sizeof(struct MuxChain),
    .audio_codec       = AV_CODEC_ID_AAC,
    .video_codec       = AV_CODEC_ID_MPEG4,
    .write_header      = rtp_mpegts_write_header,
    .write_packet      = rtp_mpegts_write_packet,
    .write_trailer     = rtp_mpegts_write_close,
};
