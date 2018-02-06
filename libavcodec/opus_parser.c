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
#include "bytestream.h"
#include "opus.h"
#include "parser.h"

typedef struct OpusParseContext {
    ParseContext pc;
    OpusContext ctx;
    OpusPacket pkt;
    int extradata_parsed;
    int ts_framing;
} OpusParseContext;

static const uint8_t *parse_opus_ts_header(const uint8_t *start, int *payload_len, int buf_len)
{
    const uint8_t *buf = start + 1;
    int start_trim_flag, end_trim_flag, control_extension_flag, control_extension_length;
    uint8_t flags;
    uint64_t payload_len_tmp;

    GetByteContext gb;
    bytestream2_init(&gb, buf, buf_len);

    flags = bytestream2_get_byte(&gb);
    start_trim_flag        = (flags >> 4) & 1;
    end_trim_flag          = (flags >> 3) & 1;
    control_extension_flag = (flags >> 2) & 1;

    payload_len_tmp = *payload_len = 0;
    while (bytestream2_peek_byte(&gb) == 0xff)
        payload_len_tmp += bytestream2_get_byte(&gb);

    payload_len_tmp += bytestream2_get_byte(&gb);

    if (start_trim_flag)
        bytestream2_skip(&gb, 2);
    if (end_trim_flag)
        bytestream2_skip(&gb, 2);
    if (control_extension_flag) {
        control_extension_length = bytestream2_get_byte(&gb);
        bytestream2_skip(&gb, control_extension_length);
    }

    if (bytestream2_tell(&gb) + payload_len_tmp > buf_len)
        return NULL;

    *payload_len = payload_len_tmp;

    return buf + bytestream2_tell(&gb);
}

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int opus_find_frame_end(AVCodecParserContext *ctx, AVCodecContext *avctx,
                               const uint8_t *buf, int buf_size, int *header_len)
{
    OpusParseContext *s = ctx->priv_data;
    ParseContext *pc    = &s->pc;
    int ret, start_found, i = 0, payload_len = 0;
    const uint8_t *payload;
    uint32_t state;
    uint16_t hdr;
    *header_len = 0;

    if (!buf_size)
        return 0;

    start_found = pc->frame_start_found;
    state = pc->state;
    payload = buf;

    /* Check if we're using Opus in MPEG-TS framing */
    if (!s->ts_framing && buf_size > 2) {
        hdr = AV_RB16(buf);
        if ((hdr & OPUS_TS_MASK) == OPUS_TS_HEADER)
            s->ts_framing = 1;
    }

    if (s->ts_framing && !start_found) {
        for (i = 0; i < buf_size-2; i++) {
            state = (state << 8) | payload[i];
            if ((state & OPUS_TS_MASK) == OPUS_TS_HEADER) {
                payload = parse_opus_ts_header(payload, &payload_len, buf_size - i);
                if (!payload) {
                    av_log(avctx, AV_LOG_ERROR, "Error parsing Ogg TS header.\n");
                    return AVERROR_INVALIDDATA;
                }
                *header_len = payload - buf;
                start_found = 1;
                break;
            }
        }
    }

    if (!s->ts_framing)
        payload_len = buf_size;

    if (avctx->extradata && !s->extradata_parsed) {
        ret = ff_opus_parse_extradata(avctx, &s->ctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error parsing Ogg extradata.\n");
            return AVERROR_INVALIDDATA;
        }
        av_freep(&s->ctx.channel_maps);
        s->extradata_parsed = 1;
    }

    if (payload_len <= buf_size && (!s->ts_framing || start_found)) {
        ret = ff_opus_parse_packet(&s->pkt, payload, payload_len, s->ctx.nb_streams > 1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error parsing Opus packet header.\n");
            pc->frame_start_found = 0;
            return AVERROR_INVALIDDATA;
        }

        ctx->duration = s->pkt.frame_count * s->pkt.frame_duration;
    }

    if (s->ts_framing) {
        if (start_found) {
            if (payload_len + *header_len <= buf_size) {
                pc->frame_start_found = 0;
                pc->state             = -1;
                return payload_len + *header_len;
            }
        }

        pc->frame_start_found = start_found;
        pc->state = state;
        return END_NOT_FOUND;
    }

    return buf_size;
}

static int opus_parse(AVCodecParserContext *ctx, AVCodecContext *avctx,
                       const uint8_t **poutbuf, int *poutbuf_size,
                       const uint8_t *buf, int buf_size)
{
    OpusParseContext *s = ctx->priv_data;
    ParseContext *pc    = &s->pc;
    int next, header_len;

    next = opus_find_frame_end(ctx, avctx, buf, buf_size, &header_len);

    if (s->ts_framing && next != AVERROR_INVALIDDATA &&
        ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    if (next == AVERROR_INVALIDDATA){
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf      = buf + header_len;
    *poutbuf_size = buf_size - header_len;
    return next;
}

AVCodecParser ff_opus_parser = {
    .codec_ids      = { AV_CODEC_ID_OPUS },
    .priv_data_size = sizeof(OpusParseContext),
    .parser_parse   = opus_parse,
    .parser_close   = ff_parse_close
};
