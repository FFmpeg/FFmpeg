/*
 * WebP parser
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
 * WebP parser
 */

#include "libavutil/bswap.h"
#include "libavutil/common.h"

#include "parser.h"

typedef struct WebPParseContext {
    ParseContext pc;
    uint32_t fsize;
    uint32_t remaining_size;
} WebPParseContext;

static int webp_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    WebPParseContext *ctx = s->priv_data;
    uint64_t state = ctx->pc.state64;
    int next = END_NOT_FOUND;
    int i = 0;

    *poutbuf      = NULL;
    *poutbuf_size = 0;

restart:
    if (ctx->pc.frame_start_found <= 8) {
        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (ctx->pc.frame_start_found == 0) {
                if ((state >> 32) == MKBETAG('R', 'I', 'F', 'F')) {
                    ctx->fsize = av_bswap32(state);
                    if (ctx->fsize > 15 && ctx->fsize <= UINT32_MAX - 10) {
                        ctx->pc.frame_start_found = 1;
                        ctx->fsize += 8;
                    }
                }
            } else if (ctx->pc.frame_start_found == 8) {
                if ((state >> 32) != MKBETAG('W', 'E', 'B', 'P')) {
                    ctx->pc.frame_start_found = 0;
                    continue;
                }
                ctx->pc.frame_start_found++;
                ctx->remaining_size = ctx->fsize + i - 15;
                if (ctx->pc.index + i > 15) {
                    next = i - 15;
                    state = 0;
                    break;
                } else {
                    ctx->pc.state64 = 0;
                    goto restart;
                }
            } else if (ctx->pc.frame_start_found)
                ctx->pc.frame_start_found++;
        }
        ctx->pc.state64 = state;
    } else {
        if (ctx->remaining_size) {
            i = FFMIN(ctx->remaining_size, buf_size);
            ctx->remaining_size -= i;
            if (ctx->remaining_size)
                goto flush;

            ctx->pc.frame_start_found = 0;
            goto restart;
        }
    }

flush:
    if (ff_combine_frame(&ctx->pc, next, &buf, &buf_size) < 0)
        return buf_size;

    if (next != END_NOT_FOUND && next < 0)
        ctx->pc.frame_start_found = FFMAX(ctx->pc.frame_start_found - i - 1, 0);
    else
        ctx->pc.frame_start_found = 0;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

AVCodecParser ff_webp_parser = {
    .codec_ids      = { AV_CODEC_ID_WEBP },
    .priv_data_size = sizeof(WebPParseContext),
    .parser_parse   = webp_parse,
    .parser_close   = ff_parse_close,
};
