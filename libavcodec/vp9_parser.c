/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/get_bits.h"
#include "parser.h"

static int parse(AVCodecParserContext *ctx,
                 AVCodecContext *avctx,
                 const uint8_t **out_data, int *out_size,
                 const uint8_t *data, int size)
{
    GetBitContext gb;
    int res, profile, keyframe;

    *out_data = data;
    *out_size = size;

    if (!size || (res = init_get_bits8(&gb, data, size)) < 0)
        return size; // parsers can't return errors
    get_bits(&gb, 2); // frame marker
    profile  = get_bits1(&gb);
    profile |= get_bits1(&gb) << 1;
    if (profile == 3) profile += get_bits1(&gb);
    if (profile > 3)
        return size;

    avctx->profile = profile;

    if (get_bits1(&gb)) {
        keyframe = 0;
    } else {
        keyframe  = !get_bits1(&gb);
    }

    if (!keyframe) {
        ctx->pict_type = AV_PICTURE_TYPE_P;
        ctx->key_frame = 0;
    } else {
        ctx->pict_type = AV_PICTURE_TYPE_I;
        ctx->key_frame = 1;
    }

    return size;
}

AVCodecParser ff_vp9_parser = {
    .codec_ids      = { AV_CODEC_ID_VP9 },
    .parser_parse   = parse,
};
