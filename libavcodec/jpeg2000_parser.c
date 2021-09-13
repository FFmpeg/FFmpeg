/*
 * JPEG2000 parser
 * Copyright (c) 2020 Gautam Ramakrishnan
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
 * JPEG2000 parser.
 */

#include "parser.h"

/* Whether frame is jp2 file or codestream
*/
enum frame_type {
    jp2_file = 1,
    j2k_cstream
};

typedef struct JPEG2000ParserContext {
    ParseContext pc;
    uint64_t bytes_read;
    uint64_t fheader_state;
    uint32_t skip_bytes; // skip bytes inside codestream data
    enum frame_type ft; // 1 if file, 2 if codestream
    uint8_t fheader_read; // are we reading
    uint8_t reading_file_header;
    uint8_t skipped_codestream;
    uint8_t read_tp;
    uint8_t in_codestream;
} JPEG2000ParserContext;

static inline void reset_context(JPEG2000ParserContext *m)
{
    ParseContext *pc = &m->pc;

    pc->frame_start_found= 0;
    pc->state = 0;
    m->bytes_read = 0;
    m->ft = 0;
    m->skipped_codestream = 0;
    m->fheader_read = 0;
    m->skip_bytes = 0;
    m->read_tp = 0;
    m->in_codestream = 0;
}

/* Returns 1 if marker has any data which can be skipped
*/
static uint8_t info_marker(uint16_t marker)
{
    if (marker == 0xFF92 || marker == 0xFF4F ||
        marker == 0xFF90 || marker == 0xFF93 ||
        marker == 0xFFD9)
        return 0;
    else
        if (marker > 0xFF00) return 1;
    return 0;
}

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int find_frame_end(JPEG2000ParserContext *m, const uint8_t *buf, int buf_size)
{
    ParseContext *pc= &m->pc;
    int i;
    uint32_t state, next_state;
    uint64_t state64;
    state= pc->state;
    state64 = pc->state64;
    if (buf_size == 0) {
        return 0;
    }

    for (i = 0; i < buf_size; i++) {
        state = state << 8 | buf[i];
        state64 = state64 << 8 | buf[i];
        m->bytes_read++;
        if (m->skip_bytes) {
            m->skip_bytes--;
            continue;
        }
        if (m->read_tp) { // Find out how many bytes inside Tile part codestream to skip.
            if (m->read_tp == 1) {
                m->skip_bytes = (state64 & 0xFFFFFFFF) - 9 > 0?
                                (state64 & 0xFFFFFFFF) - 9 : 0;
            }
            m->read_tp--;
            continue;
        }
        if (m->fheader_read) {
            if (m->fheader_read == 1) {
                if (state64 == 0x6A5020200D0A870A) { // JP2 signature box value.
                    if (pc->frame_start_found) {
                        pc->frame_start_found = 0;
                        reset_context(m);
                        return i - 11;
                    } else {
                        pc->frame_start_found = 1;
                        m->ft = jp2_file;
                    }
                }
            }
            m->fheader_read--;
        }
        if (state == 0x0000000C && m->bytes_read >= 3) { // Indicates start of JP2 file. Check signature next.
            m->fheader_read = 8;
        } else if ((state & 0xFFFF) == 0xFF4F) {
            m->in_codestream = 1;
            if (!pc->frame_start_found) {
                pc->frame_start_found = 1;
                m->ft = j2k_cstream;
            } else if (pc->frame_start_found && m->ft == jp2_file && m->skipped_codestream) {
                reset_context(m);
                return i - 1;
            }
        } else if ((state & 0xFFFF) == 0xFFD9) {
            if (pc->frame_start_found && m->ft == jp2_file) {
                m->skipped_codestream = 1;
            } else if (pc->frame_start_found && m->ft == j2k_cstream) {
                reset_context(m);
                return i + 1; // End of frame detected, return frame size.
            }
            m->in_codestream = 0;
        } else if (m->in_codestream && (state & 0xFFFF) == 0xFF90) { // Are we in tile part header?
            m->read_tp = 8;
        } else if (pc->frame_start_found && info_marker((state & 0xFFFF0000)>>16) && m->in_codestream && (state & 0xFFFF)) {
            // Calculate number of bytes to skip to get to end of the next marker.
            m->skip_bytes = (state & 0xFFFF)-1;

            // If the next marker is an info marker, skip to the end of of the marker length.
            if (i + m->skip_bytes + 1 < buf_size) {
                next_state = (buf[i + m->skip_bytes] << 8) | buf[i + m->skip_bytes + 1];
                if (info_marker(next_state)) {
                    // Skip an additional 2 bytes to get to the end of the marker length.
                    m->skip_bytes += 2;
                }
            }
        }
    }

    pc->state = state;
    pc->state64 = state64;
    return END_NOT_FOUND;
}

static int jpeg2000_parse(AVCodecParserContext *s,
                          AVCodecContext *avctx,
                          const uint8_t **poutbuf, int *poutbuf_size,
                          const uint8_t *buf, int buf_size)
{
    JPEG2000ParserContext *m = s->priv_data;
    ParseContext *pc = &m->pc;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next= buf_size;
    } else {
        next= find_frame_end(m, buf, buf_size);

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

const AVCodecParser ff_jpeg2000_parser = {
    .codec_ids      = { AV_CODEC_ID_JPEG2000 },
    .priv_data_size = sizeof(JPEG2000ParserContext),
    .parser_parse   = jpeg2000_parse,
    .parser_close   = ff_parse_close,
};
