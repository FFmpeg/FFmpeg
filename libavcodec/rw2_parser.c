/*
 * Panasonic RW2 parser
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

/*
 * Delimits frames on the "IIU\0" magic and reads IFD0 to set the dimensions
 * and pixel format without decoding.
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "parser.h"
#include "parser_internal.h"

typedef struct RW2ParseContext {
    ParseContext pc;
} RW2ParseContext;

static void rw2_set_params(AVCodecParserContext *s, AVCodecContext *avctx,
                           const uint8_t *buf, int buf_size)
{
    static const enum AVPixelFormat cfa_fmts[] = {
        AV_PIX_FMT_BAYER_RGGB16, AV_PIX_FMT_BAYER_GRBG16,
        AV_PIX_FMT_BAYER_GBRG16, AV_PIX_FMT_BAYER_BGGR16,
    };

    if (buf_size < 8 || AV_RL32(buf) != MKTAG('I', 'I', 'U', 0))
        return;

    int64_t ifd = AV_RL32(buf + 4);
    if (ifd < 8 || ifd + 2 > buf_size)
        return;

    int width = 0, height = 0, cfa = 0, bpp = 0;
    int n = FFMIN(AV_RL16(buf + ifd), (buf_size - ifd - 2) / 12);
    for (int i = 0; i < n; i++) {
        const uint8_t *e = buf + ifd + 2 + 12*i;
        unsigned tag = AV_RL16(e), type = AV_RL16(e + 2), val = AV_RL32(e + 8);
        unsigned v = type == 3 ? val & 0xFFFF : val;

        switch (tag) {
        case 0x02: width  = v; break;
        case 0x03: height = v; break;
        case 0x09: cfa    = v; break;
        case 0x0A: bpp    = v; break;
        }
    }

    if (width > 0 && height > 0 && cfa >= 1 && cfa <= 4) {
        s->width         = width;
        s->height        = height;
        s->format        = cfa_fmts[cfa - 1];
        avctx->color_trc = AVCOL_TRC_LINEAR;
        if (bpp >= 8 && bpp <= 16)
            avctx->bits_per_raw_sample = bpp;
    }
}

static int rw2_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    RW2ParseContext *ipc = s->priv_data;
    uint32_t state = ipc->pc.state;
    int next = END_NOT_FOUND;

    s->key_frame = 1;
    s->pict_type = AV_PICTURE_TYPE_I;

    /* The header lands in the first chunk, so the parameters are known without
     * waiting for, or decoding, the whole frame */
    if (buf_size >= 8 && AV_RL32(buf) == MKTAG('I', 'I', 'U', 0))
        rw2_set_params(s, avctx, buf, buf_size);

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        for (int i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (state == MKBETAG('I', 'I', 'U', 0)) {
                /* The next frame's magic ends this one */
                if (ipc->pc.frame_start_found) {
                    next = i - 3;
                    ipc->pc.frame_start_found = 0;
                    break;
                }
                ipc->pc.frame_start_found = 1;
            }
        }
        ipc->pc.state = state;
        if (ff_combine_frame(&ipc->pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const FFCodecParser ff_rw2_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_RW2),
    .priv_data_size = sizeof(RW2ParseContext),
    .parse          = rw2_parse,
    .close          = ff_parse_close,
};
