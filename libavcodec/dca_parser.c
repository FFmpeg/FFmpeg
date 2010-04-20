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
 || state == DCA_MARKER_RAW_LE || state == DCA_MARKER_RAW_BE)

/**
 * finds the end of the current frame in the bitstream.
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
                if (pc1->lastmarker && state == pc1->lastmarker) {
                    start_found = 1;
                    break;
                } else if (!pc1->lastmarker) {
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
            if (state == pc1->lastmarker && IS_MARKER(state, i, buf, buf_size)) {
                if(pc1->framesize > pc1->size)
                    continue;
                if(!pc1->framesize){
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

static int dca_parse(AVCodecParserContext * s,
                     AVCodecContext * avctx,
                     const uint8_t ** poutbuf, int *poutbuf_size,
                     const uint8_t * buf, int buf_size)
{
    DCAParseContext *pc1 = s->priv_data;
    ParseContext *pc = &pc1->pc;
    int next;

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
    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser dca_parser = {
    {CODEC_ID_DTS},
    sizeof(DCAParseContext),
    dca_parse_init,
    dca_parse,
    ff_parse_close,
};
