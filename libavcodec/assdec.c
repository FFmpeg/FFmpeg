/*
 * SSA/ASS decoder
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "avcodec.h"
#include "ass.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

static av_cold int ass_decode_init(AVCodecContext *avctx)
{
    avctx->subtitle_header = av_malloc(avctx->extradata_size);
    if (!avctx->subtitle_header)
        return AVERROR(ENOMEM);
    memcpy(avctx->subtitle_header, avctx->extradata, avctx->extradata_size);
    avctx->subtitle_header_size = avctx->extradata_size;
    return 0;
}

static int ass_decode_frame(AVCodecContext *avctx, void *data, int *got_sub_ptr,
                            AVPacket *avpkt)
{
    const char *ptr = avpkt->data;
    int len, size = avpkt->size;

    ff_ass_init(data);

    while (size > 0) {
        len = ff_ass_add_rect(data, ptr, 0, 0/* FIXME: duration */, 1);
        if (len < 0)
            return len;
        ptr  += len;
        size -= len;
    }

    *got_sub_ptr = avpkt->size > 0;
    return avpkt->size;
}

AVCodec ff_ass_decoder = {
    .name         = "ass",
    .long_name    = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SSA,
    .init         = ass_decode_init,
    .decode       = ass_decode_frame,
};
