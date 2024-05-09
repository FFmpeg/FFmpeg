/*
 * Audio and Video frame extraction
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

#include "parser.h"
#include "aac_ac3_parser.h"
#include "adts_header.h"
#include "adts_parser.h"
#include "libavutil/intreadwrite.h"

static int aac_sync(uint64_t state, int *need_next_header, int *new_frame_start)
{
    uint8_t tmp[8 + AV_INPUT_BUFFER_PADDING_SIZE];
    AACADTSHeaderInfo hdr;
    int size;

    AV_WB64(tmp, state);

    size = ff_adts_header_parse_buf(tmp + 8 - AV_AAC_ADTS_HEADER_SIZE, &hdr);
    if (size < 0)
        return 0;
    *need_next_header = 0;
    *new_frame_start  = 1;
    return size;
}

static av_cold int aac_parse_init(AVCodecParserContext *s1)
{
    AACAC3ParseContext *s = s1->priv_data;
    s->header_size = AV_AAC_ADTS_HEADER_SIZE;
    s->sync = aac_sync;
    return 0;
}


const AVCodecParser ff_aac_parser = {
    .codec_ids      = { AV_CODEC_ID_AAC },
    .priv_data_size = sizeof(AACAC3ParseContext),
    .parser_init    = aac_parse_init,
    .parser_parse   = ff_aac_ac3_parse,
    .parser_close   = ff_parse_close,
};
