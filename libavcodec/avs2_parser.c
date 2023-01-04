/*
 * AVS2-P2/IEEE1857.4 video parser.
 * Copyright (c) 2018  Huiwen Ren <hwrenx@gmail.com>
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

#include "libavutil/avutil.h"
#include "avs2.h"
#include "get_bits.h"
#include "parser.h"

static int avs2_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size)
{
    int pic_found  = pc->frame_start_found;
    uint32_t state = pc->state;
    int cur = 0;

    if (!pic_found) {
        for (; cur < buf_size; ++cur) {
            state = (state << 8) | buf[cur];
            if ((state & 0xFFFFFF00) == 0x100 && AVS2_ISPIC(buf[cur])) {
                cur++;
                pic_found = 1;
                break;
            }
        }
    }

    if (pic_found) {
        if (!buf_size)
            return END_NOT_FOUND;
        for (; cur < buf_size; cur++) {
            state = (state << 8) | buf[cur];
            if ((state & 0xFFFFFF00) == 0x100 && AVS2_ISUNIT(buf[cur])) {
                pc->frame_start_found = 0;
                pc->state = -1;
                return cur - 3;
            }
        }
    }

    pc->frame_start_found = pic_found;
    pc->state = state;

    return END_NOT_FOUND;
}

static void parse_avs2_seq_header(AVCodecParserContext *s, const uint8_t *buf,
                                 int buf_size, AVCodecContext *avctx)
{
    GetBitContext gb;
    int profile, level;
    int width, height;
    int chroma, sample_precision, encoding_precision = 1;
    // sample_precision and encoding_precision is 3 bits
    static const uint8_t precision[8] = { 0, 8, 10 };
    unsigned aspect_ratio;
    unsigned frame_rate_code;
    int low_delay;
    // update buf_size_min if parse more deeper
    const int buf_size_min = 15;

    if (buf_size < buf_size_min)
        return;

    init_get_bits8(&gb, buf, buf_size_min);

    s->key_frame = 1;
    s->pict_type = AV_PICTURE_TYPE_I;

    profile = get_bits(&gb, 8);
    level = get_bits(&gb, 8);

    // progressive_sequence     u(1)
    // field_coded_sequence     u(1)
    skip_bits(&gb, 2);

    width = get_bits(&gb, 14);
    height = get_bits(&gb, 14);

    chroma = get_bits(&gb, 2);
    sample_precision = get_bits(&gb, 3);
    if (profile == AVS2_PROFILE_MAIN10)
        encoding_precision = get_bits(&gb, 3);

    aspect_ratio = get_bits(&gb, 4);
    frame_rate_code = get_bits(&gb, 4);

    // bit_rate_lower       u(18)
    // marker_bit           f(1)
    // bit_rate_upper       u(12)
    skip_bits(&gb, 31);

    low_delay = get_bits(&gb, 1);

    s->width = width;
    s->height = height;
    s->coded_width = FFALIGN(width, 8);
    s->coded_height = FFALIGN(height, 8);
    avctx->framerate.num =
        ff_avs2_frame_rate_tab[frame_rate_code].num;
    avctx->framerate.den =
        ff_avs2_frame_rate_tab[frame_rate_code].den;
    avctx->has_b_frames = FFMAX(avctx->has_b_frames, !low_delay);

    av_log(avctx, AV_LOG_DEBUG,
           "AVS2 parse seq HDR: profile %x, level %x, "
           "width %d, height %d, "
           "chroma %d, sample_precision %d bits, encoding_precision %d bits, "
           "aspect_ratio 0x%x, framerate %d/%d, low_delay %d\n",
           profile, level,
           width, height,
           chroma, precision[sample_precision], precision[encoding_precision],
           aspect_ratio, avctx->framerate.num, avctx->framerate.den, low_delay);
}

static void parse_avs2_units(AVCodecParserContext *s, const uint8_t *buf,
                             int buf_size, AVCodecContext *avctx)
{
    if (buf_size < 5)
        return;

    if (!(buf[0] == 0x0 && buf[1] == 0x0 && buf[2] == 0x1))
        return;

    switch (buf[3]) {
    case AVS2_SEQ_START_CODE:
        parse_avs2_seq_header(s, buf + 4, buf_size - 4, avctx);
        return;
    case AVS2_INTRA_PIC_START_CODE:
        s->key_frame = 1;
        s->pict_type = AV_PICTURE_TYPE_I;
        return;
    case AVS2_INTER_PIC_START_CODE:
        s->key_frame = 0;
        if (buf_size > 9) {
            int pic_code_type = buf[8] & 0x3;
            if (pic_code_type == 1)
                s->pict_type = AV_PICTURE_TYPE_P;
            else if (pic_code_type == 3)
                s->pict_type = AV_PICTURE_TYPE_S;
            else
                s->pict_type = AV_PICTURE_TYPE_B;
        }
        return;
    }
}

static int avs2_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES)  {
        next = buf_size;
    } else {
        next = avs2_find_frame_end(pc, buf, buf_size);
        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    parse_avs2_units(s, buf, buf_size, avctx);

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_avs2_parser = {
    .codec_ids      = { AV_CODEC_ID_AVS2 },
    .priv_data_size = sizeof(ParseContext),
    .parser_parse   = avs2_parse,
    .parser_close   = ff_parse_close,
};
