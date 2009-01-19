/*
 * AVCodecParser prototypes and definitions
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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

#ifndef AVCODEC_PARSER_H
#define AVCODEC_PARSER_H

#include "avcodec.h"

typedef struct ParseContext{
    uint8_t *buffer;
    int index;
    int last_index;
    unsigned int buffer_size;
    uint32_t state;             ///< contains the last few bytes in MSB order
    int frame_start_found;
    int overread;               ///< the number of bytes which where irreversibly read from the next frame
    int overread_index;         ///< the index into ParseContext.buffer of the overread bytes
    uint64_t state64;           ///< contains the last 8 bytes in MSB order
} ParseContext;

struct MpegEncContext;

typedef struct ParseContext1{
    ParseContext pc;
/* XXX/FIXME PC1 vs. PC */
    /* MPEG-2-specific */
    AVRational frame_rate;
    int progressive_sequence;
    int width, height;

    /* XXX: suppress that, needed by MPEG-4 */
    struct MpegEncContext *enc;
    int first_picture;
} ParseContext1;

#define END_NOT_FOUND (-100)

int ff_combine_frame(ParseContext *pc, int next, const uint8_t **buf, int *buf_size);
int ff_mpeg4video_split(AVCodecContext *avctx, const uint8_t *buf,
                        int buf_size);
void ff_parse_close(AVCodecParserContext *s);
void ff_parse1_close(AVCodecParserContext *s);

/**
 * Fetches timestamps for a specific byte within the current access unit.
 * @param off byte position within the access unit
 * @param remove Found timestamps will be removed if set to 1, kept if set to 0.
 */
void ff_fetch_timestamp(AVCodecParserContext *s, int off, int remove);

#endif /* AVCODEC_PARSER_H */
