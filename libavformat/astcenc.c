/*
 * ASTC muxer (.astc container)
 * Copyright (c) 2026 Jun Zhao
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
 * ASTC (Adaptive Scalable Texture Compression) muxer.
 *
 * The .astc container is a 16-byte header followed by the raw ASTC
 * bitstream. The header is taken verbatim from the encoder's extradata
 * (which it publishes in .astc form).
 */

#include "avformat.h"
#include "avio.h"
#include "mux.h"

#define ASTC_HEADER_SIZE 16

typedef struct ASTCMuxerContext {
    int wrote_image;
} ASTCMuxerContext;

static int astc_write_header(AVFormatContext *s)
{
    AVStream *st = s->streams[0];

    if (st->codecpar->extradata_size < ASTC_HEADER_SIZE) {
        av_log(s, AV_LOG_ERROR, ".astc muxer requires 16-byte extradata "
               "(block size / dimensions) from the encoder.\n");
        return AVERROR(EINVAL);
    }

    avio_write(s->pb, st->codecpar->extradata, ASTC_HEADER_SIZE);
    return 0;
}

static int astc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASTCMuxerContext *ctx = s->priv_data;

    /* The container holds a single image; FF_OFMT_FLAG_MAX_ONE_OF_EACH only
     * limits the number of streams, so packets must be rejected here. */
    if (ctx->wrote_image) {
        av_log(s, AV_LOG_ERROR,
               ".astc muxer supports a single image per file.\n");
        return AVERROR(EINVAL);
    }
    ctx->wrote_image = 1;

    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

const FFOutputFormat ff_astc_muxer = {
    .p.name         = "astc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("ASTC (Adaptive Scalable Texture Compression)"),
    .p.mime_type    = "image/astc",
    .p.extensions   = "astc",
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_ASTC,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                      FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .priv_data_size = sizeof(ASTCMuxerContext),
    .write_header   = astc_write_header,
    .write_packet   = astc_write_packet,
    .p.flags        = AVFMT_NOTIMESTAMPS,
};
