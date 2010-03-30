/*
 * Forward Uncompressed
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
#include "bytestream.h"
#include "libavutil/intreadwrite.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "FRWU needs even width\n");
        return -1;
    }
    avctx->pix_fmt = PIX_FMT_UYVY422;

    avctx->coded_frame = avcodec_alloc_frame();

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    int field;
    AVFrame *pic = avctx->coded_frame;
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = buf + avpkt->size;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < avctx->width * 2 * avctx->height + 4 + 2*8) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small.\n");
        return -1;
    }
    if (bytestream_get_le32(&buf) != AV_RL32("FRW1")) {
        av_log(avctx, AV_LOG_ERROR, "incorrect marker\n");
        return -1;
    }

    pic->reference = 0;
    if (avctx->get_buffer(avctx, pic) < 0)
        return -1;

    pic->pict_type = FF_I_TYPE;
    pic->key_frame = 1;
    pic->interlaced_frame = 1;
    pic->top_field_first = 1;

    for (field = 0; field < 2; field++) {
        int i;
        int field_h = (avctx->height + !field) >> 1;
        int field_size, min_field_size = avctx->width * 2 * field_h;
        uint8_t *dst = pic->data[0];
        if (buf_end - buf < 8)
            return -1;
        buf += 4; // flags? 0x80 == bottom field maybe?
        field_size = bytestream_get_le32(&buf);
        if (field_size < min_field_size) {
            av_log(avctx, AV_LOG_ERROR, "Field size %i is too small (required %i)\n", field_size, min_field_size);
            return -1;
        }
        if (buf_end - buf < field_size) {
            av_log(avctx, AV_LOG_ERROR, "Packet is too small, need %i, have %i\n", field_size, (int)(buf_end - buf));
            return -1;
        }
        if (field)
            dst += pic->linesize[0];
        for (i = 0; i < field_h; i++) {
            memcpy(dst, buf, avctx->width * 2);
            buf += avctx->width * 2;
            dst += pic->linesize[0] << 1;
        }
        buf += field_size - min_field_size;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *pic;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AVFrame *pic = avctx->coded_frame;
    if (pic->data[0])
        avctx->release_buffer(avctx, pic);
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec frwu_decoder = {
    "FRWU",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_FRWU,
    0,
    decode_init,
    NULL,
    decode_close,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Forward Uncompressed"),
};
