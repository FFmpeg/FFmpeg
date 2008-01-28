/*
 * Copyright (c) 2008 Reimar Döffinger
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

#include "avcodec.h"


static int text2movsub(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    if (buf_size > 0xffff) return 0;
    *poutbuf_size = buf_size + 2;
    *poutbuf = av_malloc(*poutbuf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    AV_WB16(*poutbuf, buf_size);
    memcpy(*poutbuf + 2, buf, buf_size);
    return 1;
}

AVBitStreamFilter text2movsub_bsf={
    "text2movsub",
    0,
    text2movsub,
};

static int mov2textsub(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    if (buf_size < 2) return 0;
    *poutbuf_size = FFMIN(buf_size - 2, AV_RB16(buf));
    *poutbuf = av_malloc(*poutbuf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(*poutbuf, buf + 2, *poutbuf_size);
    return 1;
}

AVBitStreamFilter mov2textsub_bsf={
    "mov2textsub",
    0,
    mov2textsub,
};
