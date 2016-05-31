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

#include "dca.h"
#include "dca_exss.h"
#include "dca_syncwords.h"
#include "get_bits.h"
#include "parser.h"

typedef struct DCAParseContext {
    ParseContext pc;
    uint32_t lastmarker;
    int size;
    int framesize;
    unsigned int startpos;
    DCAExssParser exss;
    unsigned int sr_code;
} DCAParseContext;

#define IS_CORE_MARKER(state) \
    (((state & 0xFFFFFFFFF0FF) == (((uint64_t)DCA_SYNCWORD_CORE_14B_LE << 16) | 0xF007)) || \
     ((state & 0xFFFFFFFFFFF0) == (((uint64_t)DCA_SYNCWORD_CORE_14B_BE << 16) | 0x07F0)) || \
     ((state & 0xFFFFFFFF00FC) == (((uint64_t)DCA_SYNCWORD_CORE_LE     << 16) | 0x00FC)) || \
     ((state & 0xFFFFFFFFFC00) == (((uint64_t)DCA_SYNCWORD_CORE_BE     << 16) | 0xFC00)))

#define IS_EXSS_MARKER(state)   ((state & 0xFFFFFFFF) == DCA_SYNCWORD_SUBSTREAM)

#define IS_MARKER(state)        (IS_CORE_MARKER(state) || IS_EXSS_MARKER(state))

#define CORE_MARKER(state)      ((state >> 16) & 0xFFFFFFFF)
#define EXSS_MARKER(state)      (state & 0xFFFFFFFF)

#define STATE_LE(state)     (((state & 0xFF00FF00) >> 8) | ((state & 0x00FF00FF) << 8))
#define STATE_14(state)     (((state & 0x3FFF0000) >> 8) | ((state & 0x00003FFF) >> 6))

#define CORE_FRAMESIZE(state)   (((state >> 4) & 0x3FFF) + 1)
#define EXSS_FRAMESIZE(state)   ((state & 0x2000000000) ? \
                                 ((state >>  5) & 0xFFFFF) + 1 : \
                                 ((state >> 13) & 0x0FFFF) + 1)

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int dca_find_frame_end(DCAParseContext *pc1, const uint8_t *buf,
                              int buf_size)
{
    int start_found, size, i;
    uint64_t state;
    ParseContext *pc = &pc1->pc;

    start_found = pc->frame_start_found;
    state       = pc->state64;
    size        = pc1->size;

    i = 0;
    if (!start_found) {
        for (; i < buf_size; i++) {
            size++;
            state = (state << 8) | buf[i];

            if (IS_MARKER(state) &&
                (!pc1->lastmarker ||
                  pc1->lastmarker == CORE_MARKER(state) ||
                  pc1->lastmarker == DCA_SYNCWORD_SUBSTREAM)) {
                if (!pc1->lastmarker)
                    pc1->startpos = IS_EXSS_MARKER(state) ? size - 4 : size - 6;

                if (IS_EXSS_MARKER(state))
                    pc1->lastmarker = EXSS_MARKER(state);
                else
                    pc1->lastmarker = CORE_MARKER(state);

                start_found = 1;
                size        = 0;

                i++;
                break;
            }
        }
    }

    if (start_found) {
        for (; i < buf_size; i++) {
            size++;
            state = (state << 8) | buf[i];

            if (start_found == 1) {
                switch (pc1->lastmarker) {
                case DCA_SYNCWORD_CORE_BE:
                    if (size == 2) {
                        pc1->framesize = CORE_FRAMESIZE(state);
                        start_found    = 2;
                    }
                    break;
                case DCA_SYNCWORD_CORE_LE:
                    if (size == 2) {
                        pc1->framesize = CORE_FRAMESIZE(STATE_LE(state));
                        start_found    = 4;
                    }
                    break;
                case DCA_SYNCWORD_CORE_14B_BE:
                    if (size == 4) {
                        pc1->framesize = CORE_FRAMESIZE(STATE_14(state)) * 8 / 14 * 2;
                        start_found    = 4;
                    }
                    break;
                case DCA_SYNCWORD_CORE_14B_LE:
                    if (size == 4) {
                        pc1->framesize = CORE_FRAMESIZE(STATE_14(STATE_LE(state))) * 8 / 14 * 2;
                        start_found    = 4;
                    }
                    break;
                case DCA_SYNCWORD_SUBSTREAM:
                    if (size == 6) {
                        pc1->framesize = EXSS_FRAMESIZE(state);
                        start_found    = 4;
                    }
                    break;
                default:
                    av_assert0(0);
                }
                continue;
            }

            if (start_found == 2 && IS_EXSS_MARKER(state) &&
                pc1->framesize <= size + 2) {
                pc1->framesize  = size + 2;
                start_found     = 3;
                continue;
            }

            if (start_found == 3) {
                if (size == pc1->framesize + 4) {
                    pc1->framesize += EXSS_FRAMESIZE(state);
                    start_found     = 4;
                }
                continue;
            }

            if (pc1->framesize > size)
                continue;

            if (IS_MARKER(state) &&
                (pc1->lastmarker == CORE_MARKER(state) ||
                 pc1->lastmarker == DCA_SYNCWORD_SUBSTREAM)) {
                pc->frame_start_found = 0;
                pc->state64           = -1;
                pc1->size             = 0;
                return IS_EXSS_MARKER(state) ? i - 3 : i - 5;
            }
        }
    }

    pc->frame_start_found = start_found;
    pc->state64           = state;
    pc1->size             = size;
    return END_NOT_FOUND;
}

static av_cold int dca_parse_init(AVCodecParserContext *s)
{
    DCAParseContext *pc1 = s->priv_data;

    pc1->lastmarker = 0;
    pc1->sr_code = -1;
    return 0;
}

static int dca_parse_params(DCAParseContext *pc1, const uint8_t *buf,
                            int buf_size, int *duration, int *sample_rate)
{
    GetBitContext gb;
    uint8_t hdr[12 + AV_INPUT_BUFFER_PADDING_SIZE] = { 0 };
    int ret, sample_blocks;

    if (buf_size < 12)
        return AVERROR_INVALIDDATA;

    if (AV_RB32(buf) == DCA_SYNCWORD_SUBSTREAM) {
        DCAExssAsset *asset = &pc1->exss.assets[0];

        if ((ret = ff_dca_exss_parse(&pc1->exss, buf, buf_size)) < 0)
            return ret;

        if (asset->extension_mask & DCA_EXSS_LBR) {
            if ((ret = init_get_bits8(&gb, buf + asset->lbr_offset, asset->lbr_size)) < 0)
                return ret;

            if (get_bits_long(&gb, 32) != DCA_SYNCWORD_LBR)
                return AVERROR_INVALIDDATA;

            switch (get_bits(&gb, 8)) {
            case 2:
                pc1->sr_code = get_bits(&gb, 8);
            case 1:
                break;
            default:
                return AVERROR_INVALIDDATA;
            }

            if (pc1->sr_code >= FF_ARRAY_ELEMS(ff_dca_sampling_freqs))
                return AVERROR_INVALIDDATA;

            *sample_rate = ff_dca_sampling_freqs[pc1->sr_code];
            *duration = 1024 << ff_dca_freq_ranges[pc1->sr_code];
            return 0;
        }

        if (asset->extension_mask & DCA_EXSS_XLL) {
            int nsamples_log2;

            if ((ret = init_get_bits8(&gb, buf + asset->xll_offset, asset->xll_size)) < 0)
                return ret;

            if (get_bits_long(&gb, 32) != DCA_SYNCWORD_XLL)
                return AVERROR_INVALIDDATA;

            if (get_bits(&gb, 4))
                return AVERROR_INVALIDDATA;

            skip_bits(&gb, 8);
            skip_bits_long(&gb, get_bits(&gb, 5) + 1);
            skip_bits(&gb, 4);
            nsamples_log2 = get_bits(&gb, 4) + get_bits(&gb, 4);
            if (nsamples_log2 > 24)
                return AVERROR_INVALIDDATA;

            *sample_rate = asset->max_sample_rate;
            *duration = (1 + (*sample_rate > 96000)) << nsamples_log2;
            return 0;
        }

        return AVERROR_INVALIDDATA;
    }

    if ((ret = avpriv_dca_convert_bitstream(buf, 12, hdr, 12)) < 0)
        return ret;

    init_get_bits(&gb, hdr, 96);

    skip_bits_long(&gb, 39);
    sample_blocks = get_bits(&gb, 7) + 1;
    if (sample_blocks < 8)
        return AVERROR_INVALIDDATA;
    *duration = 256 * (sample_blocks / 8);

    skip_bits(&gb, 20);
    *sample_rate = avpriv_dca_sample_rates[get_bits(&gb, 4)];
    if (*sample_rate == 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int dca_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    DCAParseContext *pc1 = s->priv_data;
    ParseContext *pc = &pc1->pc;
    int next, duration, sample_rate;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = dca_find_frame_end(pc1, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }

        /* skip initial padding */
        if (buf_size  > pc1->startpos) {
            buf      += pc1->startpos;
            buf_size -= pc1->startpos;
        }
        pc1->startpos = 0;
    }

    /* read the duration and sample rate from the frame header */
    if (!dca_parse_params(pc1, buf, buf_size, &duration, &sample_rate)) {
        if (!avctx->sample_rate)
            avctx->sample_rate = sample_rate;
        s->duration = av_rescale(duration, avctx->sample_rate, sample_rate);
    } else
        s->duration = 0;

    *poutbuf      = buf;
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
