/*
 * WavPack muxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "apetag.h"

typedef struct WVMuxContext {
    int64_t samples;
} WVMuxContext;

static int write_header(AVFormatContext *s)
{
    AVCodecContext *codec = s->streams[0]->codec;

    if (s->nb_streams > 1) {
        av_log(s, AV_LOG_ERROR, "only one stream is supported\n");
        return AVERROR(EINVAL);
    }
    if (codec->codec_id != AV_CODEC_ID_WAVPACK) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    WVMuxContext *s = ctx->priv_data;

    if (pkt->size >= 24)
        s->samples += AV_RL32(pkt->data + 20);
    avio_write(ctx->pb, pkt->data, pkt->size);

    return 0;
}

static int write_trailer(AVFormatContext *ctx)
{
    WVMuxContext *s = ctx->priv_data;

    ff_ape_write(ctx);

    if (ctx->pb->seekable && s->samples) {
        avio_seek(ctx->pb, 12, SEEK_SET);
        if (s->samples < 0xFFFFFFFFu)
            avio_wl32(ctx->pb, s->samples);
        else
            avio_wl32(ctx->pb, 0xFFFFFFFFu);
        avio_flush(ctx->pb);
    }

    return 0;
}

AVOutputFormat ff_wv_muxer = {
    .name              = "wv",
    .long_name         = NULL_IF_CONFIG_SMALL("WavPack"),
    .priv_data_size    = sizeof(WVMuxContext),
    .extensions        = "wv",
    .audio_codec       = AV_CODEC_ID_WAVPACK,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
};
