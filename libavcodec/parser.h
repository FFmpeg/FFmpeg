/*
 * AVCodecParser prototypes and definitions
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
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

#ifndef FFMPEG_PARSER_H
#define FFMPEG_PARSER_H

#include "avcodec.h"
#include "ac3.h"

typedef struct ParseContext{
    uint8_t *buffer;
    int index;
    int last_index;
    unsigned int buffer_size;
    uint32_t state;             ///< contains the last few bytes in MSB order
    int frame_start_found;
    int overread;               ///< the number of bytes which where irreversibly read from the next frame
    int overread_index;         ///< the index into ParseContext.buffer of the overread bytes
} ParseContext;

struct MpegEncContext;

typedef struct ParseContext1{
    ParseContext pc;
/* XXX/FIXME PC1 vs. PC */
    /* MPEG2 specific */
    AVRational frame_rate;
    int progressive_sequence;
    int width, height;

    /* XXX: suppress that, needed by MPEG4 */
    struct MpegEncContext *enc;
    int first_picture;
} ParseContext1;

#define END_NOT_FOUND (-100)

int ff_combine_frame(ParseContext *pc, int next, uint8_t **buf, int *buf_size);
int ff_mpeg4video_split(AVCodecContext *avctx, const uint8_t *buf,
                        int buf_size);
void ff_parse_close(AVCodecParserContext *s);
void ff_parse1_close(AVCodecParserContext *s);

/* h263dec.c */
int ff_mpeg4_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size);

/**
 * Parses AC-3 frame header.
 * Parses the header up to the lfeon element, which is the first 52 or 54 bits
 * depending on the audio coding mode.
 * @param buf[in] Array containing the first 7 bytes of the frame.
 * @param hdr[out] Pointer to struct where header info is written.
 * @return Returns 0 on success, -1 if there is a sync word mismatch,
 * -2 if the bsid (version) element is invalid, -3 if the fscod (sample rate)
 * element is invalid, or -4 if the frmsizecod (bit rate) element is invalid.
 */
int ff_ac3_parse_header(const uint8_t buf[7], AC3HeaderInfo *hdr);

#endif /* !FFMPEG_PARSER_H */
