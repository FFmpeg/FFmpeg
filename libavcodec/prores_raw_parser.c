/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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
#include "bytestream.h"

static int prores_raw_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                            const uint8_t **poutbuf, int *poutbuf_size,
                            const uint8_t *buf, int buf_size)
{
    GetByteContext gb;

    switch (avctx->codec_tag) {
    case 0:
        break;
    case MKTAG('a','p','r','n'):
        avctx->profile = AV_PROFILE_PRORES_RAW;
        break;
    case MKTAG('a','p','r','h'):
        avctx->profile = AV_PROFILE_PRORES_RAW_HQ;
        break;
    default:
        avpriv_request_sample(avctx, "Profile %d", avctx->codec_tag);
        return buf_size;
        break;
    }

    bytestream2_init(&gb, buf, buf_size);
    if (bytestream2_get_be32(&gb) != buf_size) /* Packet size */
        return buf_size;

    if (bytestream2_get_le32(&gb) != MKTAG('p','r','r','f')) /* Frame header */
        return buf_size;

    int header_size = bytestream2_get_be16(&gb);
    if (header_size < 62)
        return buf_size;

    bytestream2_skip(&gb, 1);
    int version = bytestream2_get_byte(&gb);
    if (version > 1) {
        avpriv_request_sample(avctx, "Version %d", version);
        return buf_size;
    }

    /* Vendor header (e.g. "peac" for Panasonic or "atm0" for Atmos) */
    bytestream2_skip(&gb, 4);

    s->width = bytestream2_get_be16(&gb);
    s->height = bytestream2_get_be16(&gb);
    s->coded_width  = FFALIGN(s->width, 16);
    s->coded_height = FFALIGN(s->height, 16);
    s->format = AV_PIX_FMT_BAYER_RGGB16;
    s->key_frame = 1;
    s->pict_type = AV_PICTURE_TYPE_I;
    s->field_order = AV_FIELD_PROGRESSIVE;
    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;

    /* This parser only performs analysis */
    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return buf_size;
}

const AVCodecParser ff_prores_raw_parser = {
    .codec_ids      = { AV_CODEC_ID_PRORES_RAW },
    .parser_parse   = prores_raw_parse,
};
