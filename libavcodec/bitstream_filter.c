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

#include <string.h>

#include "avcodec.h"
#include "libavutil/atomic.h"
#include "libavutil/mem.h"

static AVBitStreamFilter *first_bitstream_filter = NULL;

AVBitStreamFilter *av_bitstream_filter_next(const AVBitStreamFilter *f)
{
    if (f)
        return f->next;
    else
        return first_bitstream_filter;
}

void av_register_bitstream_filter(AVBitStreamFilter *bsf)
{
    do {
        bsf->next = first_bitstream_filter;
    } while(bsf->next != avpriv_atomic_ptr_cas((void * volatile *)&first_bitstream_filter, bsf->next, bsf));
}

AVBitStreamFilterContext *av_bitstream_filter_init(const char *name)
{
    AVBitStreamFilter *bsf = NULL;

    while (bsf = av_bitstream_filter_next(bsf)) {
        if (!strcmp(name, bsf->name)) {
            AVBitStreamFilterContext *bsfc =
                av_mallocz(sizeof(AVBitStreamFilterContext));
            bsfc->filter    = bsf;
            bsfc->priv_data =
                bsf->priv_data_size ? av_mallocz(bsf->priv_data_size) : NULL;
            return bsfc;
        }
    }
    return NULL;
}

void av_bitstream_filter_close(AVBitStreamFilterContext *bsfc)
{
    if (!bsfc)
        return;
    if (bsfc->filter->close)
        bsfc->filter->close(bsfc);
    av_freep(&bsfc->priv_data);
    av_parser_close(bsfc->parser);
    av_free(bsfc);
}

int av_bitstream_filter_filter(AVBitStreamFilterContext *bsfc,
                               AVCodecContext *avctx, const char *args,
                               uint8_t **poutbuf, int *poutbuf_size,
                               const uint8_t *buf, int buf_size, int keyframe)
{
    *poutbuf      = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return bsfc->filter->filter(bsfc, avctx, args, poutbuf, poutbuf_size,
                                buf, buf_size, keyframe);
}
