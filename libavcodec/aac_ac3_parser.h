/*
 * Common AAC and AC3 parser prototypes
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

#ifndef FFMPEG_AAC_AC3_PARSER_H
#define FFMPEG_AAC_AC3_PARSER_H

#include <stdint.h>
#include "avcodec.h"

typedef struct AACAC3ParseContext {
    uint8_t *inbuf_ptr;
    int frame_size;
    int header_size;
    int (*sync)(const uint8_t *buf, int *channels, int *sample_rate,
                int *bit_rate, int *samples);
    uint8_t inbuf[8192]; /* input buffer */
} AACAC3ParseContext;

int ff_aac_ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size);

#endif /* FFMPEG_AAC_AC3_PARSER_H */
