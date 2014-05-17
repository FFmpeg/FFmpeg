/*
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * Opus parser
 *
 * Determines the duration for each packet.
 */

#include "avcodec.h"
#include "opus.h"

typedef struct OpusParseContext {
    OpusContext ctx;
    OpusPacket pkt;
    int extradata_parsed;
} OpusParseContext;

static int opus_parse(AVCodecParserContext *ctx, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    OpusParseContext *s = ctx->priv_data;
    int ret;

    if (!buf_size)
        return 0;

    if (avctx->extradata && !s->extradata_parsed) {
        ret = ff_opus_parse_extradata(avctx, &s->ctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error parsing Ogg extradata.\n");
            goto fail;
        }
        av_freep(&s->ctx.channel_maps);
        s->extradata_parsed = 1;
    }

    ret = ff_opus_parse_packet(&s->pkt, buf, buf_size, s->ctx.nb_streams > 1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing Opus packet header.\n");
        goto fail;
    }

    ctx->duration = s->pkt.frame_count * s->pkt.frame_duration;

fail:
    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return buf_size;
}

AVCodecParser ff_opus_parser = {
    .codec_ids      = { AV_CODEC_ID_OPUS },
    .priv_data_size = sizeof(OpusParseContext),
    .parser_parse   = opus_parse,
};
