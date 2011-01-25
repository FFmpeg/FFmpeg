/*
 * SSA/ASS encoder
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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
#include "libavutil/avstring.h"

static av_cold int ass_encode_init(AVCodecContext *avctx)
{
    avctx->extradata = av_malloc(avctx->subtitle_header_size);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    memcpy(avctx->extradata, avctx->subtitle_header, avctx->subtitle_header_size);
    avctx->extradata_size = avctx->subtitle_header_size;
    return 0;
}

static int ass_encode_frame(AVCodecContext *avctx,
                            unsigned char *buf, int bufsize, void *data)
{
    AVSubtitle *sub = data;
    int i, len, total_len = 0;

    for (i=0; i<sub->num_rects; i++) {
        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return -1;
        }

        len = av_strlcpy(buf+total_len, sub->rects[i]->ass, bufsize-total_len);

        if (len > bufsize-total_len-1) {
            av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
            return -1;
        }

        total_len += len;
    }

    return total_len;
}

AVCodec ff_ass_encoder = {
    .name         = "ass",
    .long_name    = NULL_IF_CONFIG_SMALL("Advanced SubStation Alpha subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = CODEC_ID_SSA,
    .init         = ass_encode_init,
    .encode       = ass_encode_frame,
};
