/*
 * DCA parser
 * Copyright (C) 2004 Gildas Bazin
 * Copyright (C) 2004 Benjamin Zores
 * Copyright (C) 2006 Benjamin Larsson
 * Copyright (C) 2007 Konstantin Shishkov
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

#include "parser.h"
#include "dca.h"
#include "dca_parser.h"
#include "get_bits.h"
#include "put_bits.h"

typedef struct DCAParseContext {
    ParseContext pc;
    uint32_t lastmarker;
    int size;
    int framesize;
    int hd_pos;
} DCAParseContext;

#define IS_MARKER(state, i, buf, buf_size) \
 ((state == DCA_MARKER_14B_LE && (i < buf_size-2) && (buf[i+1] & 0xF0) == 0xF0 && buf[i+2] == 0x07) \
 || (state == DCA_MARKER_14B_BE && (i < buf_size-2) && buf[i+1] == 0x07 && (buf[i+2] & 0xF0) == 0xF0) \
 || state == DCA_MARKER_RAW_LE || state == DCA_MARKER_RAW_BE || state == DCA_HD_MARKER)

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int dca_find_frame_end(DCAParseContext * pc1, const uint8_t * buf,
                              int buf_size)
{
    int start_found, i;
    uint32_t state;
    ParseContext *pc = &pc1->pc;

    start_found = pc->frame_start_found;
    state = pc->state;

    i = 0;
    if (!start_found) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (IS_MARKER(state, i, buf, buf_size)) {
                if (!pc1->lastmarker || state == pc1->lastmarker || pc1->lastmarker == DCA_HD_MARKER) {
                    start_found = 1;
                    pc1->lastmarker = state;
                    break;
                }
            }
        }
    }
    if (start_found) {
        for (; i < buf_size; i++) {
            pc1->size++;
            state = (state << 8) | buf[i];
            if (state == DCA_HD_MARKER && !pc1->hd_pos)
                pc1->hd_pos = pc1->size;
            if (IS_MARKER(state, i, buf, buf_size) && (state == pc1->lastmarker || pc1->lastmarker == DCA_HD_MARKER)) {
                if(pc1->framesize > pc1->size)
                    continue;
                // We have to check that we really read a full frame here, and that it isn't a pure HD frame, because their size is not constant.
                if(!pc1->framesize && state == pc1->lastmarker && state != DCA_HD_MARKER){
                    pc1->framesize = pc1->hd_pos ? pc1->hd_pos : pc1->size;
                }
                pc->frame_start_found = 0;
                pc->state = -1;
                pc1->size = 0;
                return i - 3;
            }
        }
    }
    pc->frame_start_found = start_found;
    pc->state = state;
    return END_NOT_FOUND;
}

static av_cold int dca_parse_init(AVCodecParserContext * s)
{
    DCAParseContext *pc1 = s->priv_data;

    pc1->lastmarker = 0;
    return 0;
}

int ff_dca_convert_bitstream(const uint8_t *src, int src_size, uint8_t *dst,
                             int max_size)
{
    uint32_t mrk;
    int i, tmp;
    const uint16_t *ssrc = (const uint16_t *) src;
    uint16_t *sdst = (uint16_t *) dst;
    PutBitContext pb;

    if ((unsigned) src_size > (unsigned) max_size)
        src_size = max_size;

    mrk = AV_RB32(src);
    switch (mrk) {
    case DCA_MARKER_RAW_BE:
        memcpy(dst, src, src_size);
        return src_size;
    case DCA_MARKER_RAW_LE:
        for (i = 0; i < (src_size + 1) >> 1; i++)
            *sdst++ = av_bswap16(*ssrc++);
        return src_size;
    case DCA_MARKER_14B_BE:
    case DCA_MARKER_14B_LE:
        init_put_bits(&pb, dst, max_size);
        for (i = 0; i < (src_size + 1) >> 1; i++, src += 2) {
            tmp = ((mrk == DCA_MARKER_14B_BE) ? AV_RB16(src) : AV_RL16(src)) & 0x3FFF;
            put_bits(&pb, 14, tmp);
        }
        flush_put_bits(&pb);
        return (put_bits_count(&pb) + 7) >> 3;
    default:
        return AVERROR_INVALIDDATA;
    }
}

static int dca_parse_params(const uint8_t *buf, int buf_size, int *duration,
                            int *sample_rate)
{
    GetBitContext gb;
    uint8_t hdr[12 + FF_INPUT_BUFFER_PADDING_SIZE] = { 0 };
    int ret, sample_blocks, sr_code;

    if (buf_size < 12)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_dca_convert_bitstream(buf, 12, hdr, 12)) < 0)
        return ret;

    init_get_bits(&gb, hdr, 96);

    skip_bits_long(&gb, 39);
    sample_blocks = get_bits(&gb, 7) + 1;
    if (sample_blocks < 8)
        return AVERROR_INVALIDDATA;
    *duration = 256 * (sample_blocks / 8);

    skip_bits(&gb, 20);
    sr_code = get_bits(&gb, 4);
    *sample_rate = avpriv_dca_sample_rates[sr_code];
    if (*sample_rate == 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int dca_parse(AVCodecParserContext * s,
                     AVCodecContext * avctx,
                     const uint8_t ** poutbuf, int *poutbuf_size,
                     const uint8_t * buf, int buf_size)
{
    DCAParseContext *pc1 = s->priv_data;
    ParseContext *pc = &pc1->pc;
    int next, duration, sample_rate;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = dca_find_frame_end(pc1, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    /* read the duration and sample rate from the frame header */
    if (!dca_parse_params(buf, buf_size, &duration, &sample_rate)) {
        s->duration = duration;
        avctx->sample_rate = sample_rate;
    } else
        s->duration = 0;

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_dca_parser = {
    .codec_ids      = { AV_CODEC_ID_DTS },
    .priv_data_size = sizeof(DCAParseContext),
    .parser_init    = dca_parse_init,
    .parser_parse   = dca_parse,
    .parser_close   = ff_parse_close,
};
