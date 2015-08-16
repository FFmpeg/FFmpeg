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
#include "internal.h"
#include "libavutil/opt.h"

typedef struct {
    AVClass *av_class;
    int change_field_order;
} FRWUContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "frwu needs even width\n");
        return AVERROR(EINVAL);
    }
    avctx->pix_fmt = AV_PIX_FMT_UYVY422;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    FRWUContext *s = avctx->priv_data;
    int field, ret;
    AVFrame *pic = data;
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = buf + avpkt->size;

    if (avpkt->size < avctx->width * 2 * avctx->height + 4 + 2*8) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small.\n");
        return AVERROR_INVALIDDATA;
    }
    if (bytestream_get_le32(&buf) != MKTAG('F', 'R', 'W', '1')) {
        av_log(avctx, AV_LOG_ERROR, "incorrect marker\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    for (field = 0; field < 2; field++) {
        int i;
        int field_h = (avctx->height + !field) >> 1;
        int field_size, min_field_size = avctx->width * 2 * field_h;
        uint8_t *dst = pic->data[0];
        if (buf_end - buf < 8)
            return AVERROR_INVALIDDATA;
        buf += 4; // flags? 0x80 == bottom field maybe?
        field_size = bytestream_get_le32(&buf);
        if (field_size < min_field_size) {
            av_log(avctx, AV_LOG_ERROR, "Field size %i is too small (required %i)\n", field_size, min_field_size);
            return AVERROR_INVALIDDATA;
        }
        if (buf_end - buf < field_size) {
            av_log(avctx, AV_LOG_ERROR, "Packet is too small, need %i, have %i\n", field_size, (int)(buf_end - buf));
            return AVERROR_INVALIDDATA;
        }
        if (field ^ s->change_field_order) {
            dst += pic->linesize[0];
        } else if (s->change_field_order) {
            dst += 2 * pic->linesize[0];
        }
        for (i = 0; i < field_h; i++) {
            if (s->change_field_order && field && i == field_h - 1)
                dst = pic->data[0];
            memcpy(dst, buf, avctx->width * 2);
            buf += avctx->width * 2;
            dst += pic->linesize[0] << 1;
        }
        buf += field_size - min_field_size;
    }

    *got_frame = 1;

    return avpkt->size;
}

static const AVOption frwu_options[] = {
    {"change_field_order", "Change field order", offsetof(FRWUContext, change_field_order), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM},
    {NULL}
};

static const AVClass frwu_class = {
    .class_name = "frwu Decoder",
    .item_name  = av_default_item_name,
    .option     = frwu_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_frwu_decoder = {
    .name           = "frwu",
    .long_name      = NULL_IF_CONFIG_SMALL("Forward Uncompressed"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FRWU,
    .priv_data_size = sizeof(FRWUContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .priv_class     = &frwu_class,
};
