/*
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

#include "libavutil/intreadwrite.h"
#include "bytestream.h"

#include "avcodec.h"

static int parse(AVCodecParserContext *s,
                 AVCodecContext *avctx,
                 const uint8_t **poutbuf, int *poutbuf_size,
                 const uint8_t *buf, int buf_size)
{
    GetByteContext gb;
    uint8_t flags, depth, chroma_format, alpha_channel_type;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    /* Frame fields + frame header size */
    if (buf_size < 28)
        return buf_size;

    bytestream2_init(&gb, buf, buf_size);

    /* Frame size */
    if (bytestream2_get_be32(&gb) != buf_size)
        return buf_size;

    /* Frame identifier */
    if (bytestream2_get_le32(&gb) != MKTAG('i','c','p','f'))
        return buf_size;

    /* Frame header size */
    if (bytestream2_get_be16(&gb) < 20)
        return buf_size;

    bytestream2_skip(&gb, 6); /* Bitstream version, encoder identifier */

    s->key_frame = 1;
    s->pict_type = AV_PICTURE_TYPE_I;

    s->width  = bytestream2_get_be16(&gb);
    s->height = bytestream2_get_be16(&gb);
    s->coded_width  = FFALIGN(s->width,  16);
    s->coded_height = FFALIGN(s->height, 16);

    flags = bytestream2_get_byte(&gb);

    /* Interlace mode */
    switch (flags >> 2 & 3) {
        case 0:
            s->field_order       = AV_FIELD_PROGRESSIVE;
            s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;
            break;
        case 1:
            s->field_order       = AV_FIELD_TT;
            s->picture_structure = AV_PICTURE_STRUCTURE_TOP_FIELD;
            break;
        case 2:
            s->field_order       = AV_FIELD_BB;
            s->picture_structure = AV_PICTURE_STRUCTURE_BOTTOM_FIELD;
            break;
        default:
            break;
    }

    bytestream2_skip(&gb, 4); /* Aspect ratio information, frame rate code, color primaries, transfer characteristic, matrix coefficients */

    /* Determine pixel format based on color depth, chroma format and alpha type */
    switch (avctx->codec_tag) {
        case MKTAG('a','p','c','o'):
        case MKTAG('a','p','c','s'):
        case MKTAG('a','p','c','n'):
        case MKTAG('a','p','c','h'):
            depth = 10;
            break;
        case MKTAG('a','p','4','h'):
        case MKTAG('a','p','4','x'):
            depth = 12;
            break;
        default:
            return buf_size;
    }

    chroma_format = flags >> 6 & 3;
    if (chroma_format < 2)
        return buf_size;

    alpha_channel_type = bytestream2_get_byte(&gb) & 0xf;

    switch (depth | (chroma_format << 4) | (alpha_channel_type << 8)) {
        case 10 | (2 << 4) | (0 << 8): s->format = AV_PIX_FMT_YUV422P10;  break;
        case 10 | (2 << 4) | (1 << 8):
        case 10 | (2 << 4) | (2 << 8): s->format = AV_PIX_FMT_YUVA422P10; break;
        case 10 | (3 << 4) | (0 << 8): s->format = AV_PIX_FMT_YUV444P10;  break;
        case 10 | (3 << 4) | (1 << 8):
        case 10 | (3 << 4) | (2 << 8): s->format = AV_PIX_FMT_YUVA444P10; break;
        case 12 | (2 << 4) | (0 << 8): s->format = AV_PIX_FMT_YUV422P12;  break;
        case 12 | (2 << 4) | (1 << 8):
        case 12 | (2 << 4) | (2 << 8): s->format = AV_PIX_FMT_YUVA422P12; break;
        case 12 | (3 << 4) | (0 << 8): s->format = AV_PIX_FMT_YUV444P12;  break;
        case 12 | (3 << 4) | (1 << 8):
        case 12 | (3 << 4) | (2 << 8): s->format = AV_PIX_FMT_YUVA444P12; break;
    }

    return buf_size;
}

const AVCodecParser ff_prores_parser = {
    .codec_ids    = { AV_CODEC_ID_PRORES },
    .parser_parse = parse,
};
