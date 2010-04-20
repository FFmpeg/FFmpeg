/*
 * Dirac parser
 *
 * Copyright (c) 2007-2008 Marco Gerards <marco@gnu.org>
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju@gmail.com>
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
 * Dirac Parser
 * @author Marco Gerards <marco@gnu.org>
 */

#include "libavutil/intreadwrite.h"
#include "parser.h"

#define DIRAC_PARSE_INFO_PREFIX 0x42424344

/**
 * Finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame or -1
 */
typedef struct DiracParseContext {
    int state;
    int is_synced;
    int sync_offset;
    int header_bytes_needed;
    int overread_index;
    int buffer_size;
    int index;
    uint8_t *buffer;
    int dirac_unit_size;
    uint8_t *dirac_unit;
} DiracParseContext;

static int find_frame_end(DiracParseContext *pc,
                          const uint8_t *buf, int buf_size)
{
    uint32_t state = pc->state;
    int i = 0;

    if (!pc->is_synced) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (state == DIRAC_PARSE_INFO_PREFIX) {
                state                   = -1;
                pc->is_synced           = 1;
                pc->header_bytes_needed = 9;
                pc->sync_offset         = i;
                break;
            }
        }
    }

    if (pc->is_synced) {
        pc->sync_offset = 0;
        for (; i < buf_size; i++) {
            if (state == DIRAC_PARSE_INFO_PREFIX) {
                if ((buf_size-i) >= pc->header_bytes_needed) {
                    pc->state = -1;
                    return i + pc->header_bytes_needed;
                } else {
                    pc->header_bytes_needed = 9-(buf_size-i);
                    break;
                }
            } else
              state = (state << 8) | buf[i];
        }
    }
    pc->state = state;
    return -1;
}

typedef struct DiracParseUnit
{
    int next_pu_offset;
    int prev_pu_offset;
    uint8_t pu_type;
} DiracParseUnit;

static int unpack_parse_unit(DiracParseUnit *pu, DiracParseContext *pc,
                             int offset)
{
    uint8_t *start = pc->buffer + offset;
    uint8_t *end   = pc->buffer + pc->index;
    if (start < pc->buffer || (start+13 > end))
        return 0;
    pu->pu_type = start[4];

    pu->next_pu_offset = AV_RB32(start+5);
    pu->prev_pu_offset = AV_RB32(start+9);

    if (pu->pu_type == 0x10 && pu->next_pu_offset == 0)
        pu->next_pu_offset = 13;

    return 1;
}

static int dirac_combine_frame(AVCodecParserContext *s, AVCodecContext *avctx,
                               int next, const uint8_t **buf, int *buf_size)
{
    int parse_timing_info = (s->pts == AV_NOPTS_VALUE &&
                             s->dts == AV_NOPTS_VALUE);
    DiracParseContext *pc = s->priv_data;

    if (pc->overread_index) {
        memcpy(pc->buffer, pc->buffer + pc->overread_index,
               pc->index - pc->overread_index);
        pc->index -= pc->overread_index;
        pc->overread_index = 0;
        if (*buf_size == 0 && pc->buffer[4] == 0x10) {
            *buf      = pc->buffer;
            *buf_size = pc->index;
            return 0;
        }
    }

    if ( next == -1) {
        /* Found a possible frame start but not a frame end */
        void *new_buffer = av_fast_realloc(pc->buffer, &pc->buffer_size,
                                           pc->index + (*buf_size -
                                                        pc->sync_offset));
        pc->buffer = new_buffer;
        memcpy(pc->buffer+pc->index, (*buf + pc->sync_offset),
               *buf_size - pc->sync_offset);
        pc->index += *buf_size - pc->sync_offset;
        return -1;
    } else {
        /* Found a possible frame start and a  possible frame end */
        DiracParseUnit pu1, pu;
        void *new_buffer = av_fast_realloc(pc->buffer, &pc->buffer_size,
                                           pc->index + next);
        pc->buffer = new_buffer;
        memcpy(pc->buffer + pc->index, *buf, next);
        pc->index += next;

        /* Need to check if we have a valid Parse Unit. We can't go by the
         * sync pattern 'BBCD' alone because arithmetic coding of the residual
         * and motion data can cause the pattern triggering a false start of
         * frame. So check if the previous parse offset of the next parse unit
         * is equal to the next parse offset of the current parse unit then
         * we can be pretty sure that we have a valid parse unit */
        if (!unpack_parse_unit(&pu1, pc, pc->index - 13)                     ||
            !unpack_parse_unit(&pu, pc, pc->index - 13 - pu1.prev_pu_offset) ||
            pu.next_pu_offset != pu1.prev_pu_offset) {
            pc->index -= 9;
            *buf_size = next-9;
            pc->header_bytes_needed = 9;
            return -1;
        }

        /* All non-frame data must be accompanied by frame data. This is to
         * ensure that pts is set correctly. So if the current parse unit is
         * not frame data, wait for frame data to come along */

        pc->dirac_unit = pc->buffer + pc->index - 13 -
                         pu1.prev_pu_offset - pc->dirac_unit_size;

        pc->dirac_unit_size += pu.next_pu_offset;

        if ((pu.pu_type&0x08) != 0x08) {
            pc->header_bytes_needed = 9;
            *buf_size = next;
            return -1;
        }

        /* Get the picture number to set the pts and dts*/
        if (parse_timing_info) {
            uint8_t *cur_pu = pc->buffer +
                              pc->index - 13 - pu1.prev_pu_offset;
            int pts =  AV_RB32(cur_pu + 13);
            if (s->last_pts == 0 && s->last_dts == 0)
                s->dts = pts - 1;
            else
                s->dts = s->last_dts+1;
            s->pts = pts;
            if (!avctx->has_b_frames && (cur_pu[4] & 0x03))
                avctx->has_b_frames = 1;
        }
        if (avctx->has_b_frames && s->pts == s->dts)
             s->pict_type = FF_B_TYPE;

        /* Finally have a complete Dirac data unit */
        *buf      = pc->dirac_unit;
        *buf_size = pc->dirac_unit_size;

        pc->dirac_unit_size     = 0;
        pc->overread_index      = pc->index-13;
        pc->header_bytes_needed = 9;
    }
    return next;
}

static int dirac_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                       const uint8_t **poutbuf, int *poutbuf_size,
                       const uint8_t *buf, int buf_size)
{
    DiracParseContext *pc = s->priv_data;
    int next;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
        *poutbuf = buf;
        *poutbuf_size = buf_size;
        /* Assume that data has been packetized into an encapsulation unit. */
    } else {
        next = find_frame_end(pc, buf, buf_size);
        if (!pc->is_synced && next == -1) {
            /* No frame start found yet. So throw away the entire buffer. */
            return buf_size;
        }

        if (dirac_combine_frame(s, avctx, next, &buf, &buf_size) < 0) {
            return buf_size;
        }
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

static void dirac_parse_close(AVCodecParserContext *s)
{
    DiracParseContext *pc = s->priv_data;

    if (pc->buffer_size > 0)
        av_free(pc->buffer);
}

AVCodecParser dirac_parser = {
    { CODEC_ID_DIRAC },
    sizeof(DiracParseContext),
    NULL,
    dirac_parse,
    dirac_parse_close,
};
