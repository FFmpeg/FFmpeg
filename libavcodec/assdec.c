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
#include "ass_split.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

static av_cold int ass_decode_init(AVCodecContext *avctx)
{
    avctx->subtitle_header = av_malloc(avctx->extradata_size + 1);
    if (!avctx->subtitle_header)
        return AVERROR(ENOMEM);
    memcpy(avctx->subtitle_header, avctx->extradata, avctx->extradata_size);
    avctx->subtitle_header[avctx->extradata_size] = 0;
    avctx->subtitle_header_size = avctx->extradata_size;
    avctx->priv_data = ff_ass_split(avctx->extradata);
    if(!avctx->priv_data)
        return -1;
    return 0;
}

static int ass_decode_close(AVCodecContext *avctx)
{
    ff_ass_split_free(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

#if CONFIG_SSA_DECODER
static int ssa_decode_frame(AVCodecContext *avctx, void *data, int *got_sub_ptr,
                            AVPacket *avpkt)
{
    const char *ptr = avpkt->data;
    int len, size = avpkt->size;

    while (size > 0) {
        int duration;
        ASSDialog *dialog = ff_ass_split_dialog(avctx->priv_data, ptr, 0, NULL);
        if (!dialog)
            return AVERROR_INVALIDDATA;
        duration = dialog->end - dialog->start;
        len = ff_ass_add_rect(data, ptr, 0, duration, 1);
        if (len < 0)
            return len;
        ptr  += len;
        size -= len;
    }

    *got_sub_ptr = avpkt->size > 0;
    return avpkt->size;
}

AVCodec ff_ssa_decoder = {
    .name         = "ssa",
    .long_name    = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SSA,
    .init         = ass_decode_init,
    .decode       = ssa_decode_frame,
    .close        = ass_decode_close,
};
#endif

#if CONFIG_ASS_DECODER
static int ass_decode_frame(AVCodecContext *avctx, void *data, int *got_sub_ptr,
                            AVPacket *avpkt)
{
    int ret;
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    static const AVRational ass_tb = {1, 100};
    const int ts_start    = av_rescale_q(avpkt->pts,      avctx->time_base, ass_tb);
    const int ts_duration = av_rescale_q(avpkt->duration, avctx->time_base, ass_tb);

    if (avpkt->size <= 0)
        return avpkt->size;

    ret = ff_ass_add_rect(sub, ptr, ts_start, ts_duration, 2);
    if (ret < 0) {
        if (ret == AVERROR_INVALIDDATA)
            av_log(avctx, AV_LOG_ERROR, "Invalid ASS packet\n");
        return ret;
    }

    *got_sub_ptr = avpkt->size > 0;
    return avpkt->size;
}

AVCodec ff_ass_decoder = {
    .name         = "ass",
    .long_name    = NULL_IF_CONFIG_SMALL("ASS (Advanced SubStation Alpha) subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_ASS,
    .init         = ass_decode_init,
    .decode       = ass_decode_frame,
    .close        = ass_decode_close,
};
#endif
