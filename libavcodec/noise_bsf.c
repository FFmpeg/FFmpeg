/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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


static int noise(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    unsigned int *state= bsfc->priv_data;
    int amount= args ? atoi(args) : (*state % 10001+1);
    int i;

    *poutbuf= av_malloc(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

    memcpy(*poutbuf, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    for(i=0; i<buf_size; i++){
        (*state) += (*poutbuf)[i] + 1;
        if(*state % amount == 0)
            (*poutbuf)[i] = *state;
    }
    return 1;
}

AVBitStreamFilter noise_bsf={
    "noise",
    sizeof(int),
    noise,
};
