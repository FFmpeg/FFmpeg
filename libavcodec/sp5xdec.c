/*
 * Sunplus JPEG decoder (SP5X)
 * Copyright (c) 2003 Alex Beregszaszi
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

/**
 * @file
 * Sunplus JPEG decoder (SP5X).
 */

#include "config_components.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "mjpeg.h"
#include "mjpegdec.h"
#include "sp5x.h"


static int sp5x_decode_frame(AVCodecContext *avctx,
                             AVFrame *frame, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    uint8_t *recoded;
    int i = 0, j = 0;
    int ret;

    if (!avctx->width || !avctx->height)
        return -1;

    recoded = av_mallocz(buf_size + 1024);
    if (!recoded)
        return -1;

    /* SOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD8;

    memcpy(recoded+j, &sp5x_data_dqt[0], sizeof(sp5x_data_dqt));
    memcpy(recoded + j + 5,  &sp5x_qscale_five_quant_table[0], 64);
    memcpy(recoded + j + 70, &sp5x_qscale_five_quant_table[1], 64);
    j += sizeof(sp5x_data_dqt);

    memcpy(recoded+j, &sp5x_data_dht[0], sizeof(sp5x_data_dht));
    j += sizeof(sp5x_data_dht);

    memcpy(recoded+j, &sp5x_data_sof[0], sizeof(sp5x_data_sof));
    AV_WB16(recoded+j+5, avctx->coded_height);
    AV_WB16(recoded+j+7, avctx->coded_width);
    j += sizeof(sp5x_data_sof);

    memcpy(recoded+j, &sp5x_data_sos[0], sizeof(sp5x_data_sos));
    j += sizeof(sp5x_data_sos);

    if(avctx->codec_id==AV_CODEC_ID_AMV)
        for (i = 2; i < buf_size-2 && j < buf_size+1024-2; i++)
            recoded[j++] = buf[i];
    else
    for (i = 14; i < buf_size && j < buf_size+1024-3; i++)
    {
        recoded[j++] = buf[i];
        if (buf[i] == 0xff)
            recoded[j++] = 0;
    }

    /* EOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD9;

    ret = ff_mjpeg_decode_frame_from_buf(avctx, frame, got_frame,
                                         avpkt, recoded, j);

    av_free(recoded);

    return ret < 0 ? ret : avpkt->size;
}

#if CONFIG_SP5X_DECODER
const FFCodec ff_sp5x_decoder = {
    .p.name         = "sp5x",
    CODEC_LONG_NAME("Sunplus JPEG (SP5X)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SP5X,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    FF_CODEC_DECODE_CB(sp5x_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.max_lowres   = 3,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
#if CONFIG_AMV_DECODER
const FFCodec ff_amv_decoder = {
    .p.name         = "amv",
    CODEC_LONG_NAME("AMV Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AMV,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    FF_CODEC_DECODE_CB(sp5x_decode_frame),
    .p.max_lowres   = 3,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
