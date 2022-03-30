/*
 * SSA/ASS decoder
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

#include <string.h>

#include "avcodec.h"
#include "ass.h"
#include "codec_internal.h"
#include "config_components.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

static av_cold int ass_decode_init(AVCodecContext *avctx)
{
    avctx->subtitle_header = av_malloc(avctx->extradata_size + 1);
    if (!avctx->subtitle_header)
        return AVERROR(ENOMEM);
    if (avctx->extradata_size)
        memcpy(avctx->subtitle_header, avctx->extradata, avctx->extradata_size);
    avctx->subtitle_header[avctx->extradata_size] = 0;
    avctx->subtitle_header_size = avctx->extradata_size;
    return 0;
}

static int ass_decode_frame(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr, const AVPacket *avpkt)
{
    if (avpkt->size <= 0)
        return avpkt->size;

    sub->rects = av_malloc(sizeof(*sub->rects));
    if (!sub->rects)
        return AVERROR(ENOMEM);
    sub->rects[0] = av_mallocz(sizeof(*sub->rects[0]));
    if (!sub->rects[0])
        return AVERROR(ENOMEM);
    sub->num_rects = 1;
    sub->rects[0]->type = SUBTITLE_ASS;
    sub->rects[0]->ass  = av_strdup(avpkt->data);
    if (!sub->rects[0]->ass)
        return AVERROR(ENOMEM);
    *got_sub_ptr = 1;
    return avpkt->size;
}

#if CONFIG_SSA_DECODER
const FFCodec ff_ssa_decoder = {
    .p.name       = "ssa",
    .p.long_name  = NULL_IF_CONFIG_SMALL("ASS (Advanced SubStation Alpha) subtitle"),
    .p.type       = AVMEDIA_TYPE_SUBTITLE,
    .p.id         = AV_CODEC_ID_ASS,
    .init         = ass_decode_init,
    FF_CODEC_DECODE_SUB_CB(ass_decode_frame),
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_ASS_DECODER
const FFCodec ff_ass_decoder = {
    .p.name       = "ass",
    .p.long_name  = NULL_IF_CONFIG_SMALL("ASS (Advanced SubStation Alpha) subtitle"),
    .p.type       = AVMEDIA_TYPE_SUBTITLE,
    .p.id         = AV_CODEC_ID_ASS,
    .init         = ass_decode_init,
    FF_CODEC_DECODE_SUB_CB(ass_decode_frame),
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
