/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) parser.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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
 * Chinese AVS video (AVS1-P2, JiZhun profile) parser
 * @author Stefan Gehrer <stefan.gehrer@gmx.de>
 */

#include "parser.h"
#include "cavs.h"
#include "get_bits.h"
#include "mpeg12data.h"
#include "parser_internal.h"
#include "startcode.h"


/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int cavs_find_frame_end(ParseContext *pc, const uint8_t *buf,
                               int buf_size) {
    int pic_found, i;
    uint32_t state;

    pic_found= pc->frame_start_found;
    state= pc->state;

    i=0;
    if(!pic_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == PIC_I_START_CODE || state == PIC_PB_START_CODE){
                i++;
                pic_found=1;
                break;
            }
        }
    }

    if(pic_found){
        /* EOF considered as end of frame */
        if (buf_size == 0)
            return 0;
        for(; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if (state == PIC_I_START_CODE || state == PIC_PB_START_CODE ||
                    state == CAVS_START_CODE) {
                pc->frame_start_found=0;
                pc->state=-1;
                return i-3;
            }
        }
    }
    pc->frame_start_found= pic_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int parse_seq_header(AVCodecParserContext *s, AVCodecContext *avctx,
                            GetBitContext *gb)
{
    int frame_rate_code;
    int width, height;
    int mb_width, mb_height;

    skip_bits(gb, 8); // profile
    skip_bits(gb, 8); // level
    skip_bits1(gb);   // progressive sequence

    width  = get_bits(gb, 14);
    height = get_bits(gb, 14);
    if (width <= 0 || height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Dimensions invalid\n");
        return AVERROR_INVALIDDATA;
    }
    mb_width  = (width  + 15) >> 4;
    mb_height = (height + 15) >> 4;

    skip_bits(gb, 2); // chroma format
    skip_bits(gb, 3); // sample_precision
    skip_bits(gb, 4); // aspect_ratio
    frame_rate_code = get_bits(gb, 4);
    if (frame_rate_code == 0 || frame_rate_code > 13) {
        av_log(avctx, AV_LOG_WARNING,
               "frame_rate_code %d is invalid\n", frame_rate_code);
        frame_rate_code = 1;
    }

    skip_bits(gb, 18); // bit_rate_lower
    skip_bits1(gb);    // marker_bit
    skip_bits(gb, 12); // bit_rate_upper
    skip_bits1(gb);    // low_delay

    s->width  = width;
    s->height = height;
    s->coded_width  = 16 * mb_width;
    s->coded_height = 16 * mb_height;
    avctx->framerate = ff_mpeg12_frame_rate_tab[frame_rate_code];

    return 0;
}

static int cavs_parse_frame(AVCodecParserContext *s, AVCodecContext *avctx,
                            const uint8_t *buf, int buf_size)
{
    GetBitContext gb;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;
    uint32_t stc = -1;

    s->key_frame = 0;
    s->pict_type = AV_PICTURE_TYPE_NONE;

    if (buf_size == 0)
        return 0;

    buf_ptr = buf;
    buf_end = buf + buf_size;
    for (;;) {
        buf_ptr = avpriv_find_start_code(buf_ptr, buf_end, &stc);
        if ((stc & 0xFFFFFE00) || buf_ptr == buf_end)
            return 0;
        switch (stc) {
        case CAVS_START_CODE:
            if (init_get_bits8(&gb, buf_ptr, buf_end - buf_ptr) < 0)
                return 0;
            parse_seq_header(s, avctx, &gb);
            break;
        case PIC_I_START_CODE:
            s->key_frame = 1;
            s->pict_type = AV_PICTURE_TYPE_I;
            break;
        default:
            break;
        }
    }
}

static int cavsvideo_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           const uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES){
        next= buf_size;
    }else{
        next= cavs_find_frame_end(pc, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    cavs_parse_frame(s, avctx, buf, buf_size);

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

const FFCodecParser ff_cavsvideo_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_CAVS),
    .priv_data_size = sizeof(ParseContext),
    .parse          = cavsvideo_parse,
    .close          = ff_parse_close,
};
