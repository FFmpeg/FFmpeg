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

#include "avcodec.h"
#include "internal.h"
#include "mjpeg.h"
#include "mjpegdec.h"
#include "sp5x.h"

int ff_sp5x_process_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVBufferRef *buf_recoded;
    const int qscale = 5;
    uint8_t *recoded;
    int i = 0, j = 0;

    if (!avctx->width || !avctx->height)
        return -1;

    buf_recoded = av_buffer_allocz(buf_size + 1024);
    if (!buf_recoded)
        return -1;
    recoded = buf_recoded->data;

    /* SOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD8;

    memcpy(recoded+j, &sp5x_data_dqt[0], sizeof(sp5x_data_dqt));
    memcpy(recoded+j+5, &sp5x_quant_table[qscale * 2], 64);
    memcpy(recoded+j+70, &sp5x_quant_table[(qscale * 2) + 1], 64);
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

    av_buffer_unref(&avpkt->buf);
    avpkt->buf  = buf_recoded;
    avpkt->data = recoded;
    avpkt->size = j;

    return 0;
}

#if CONFIG_SP5X_DECODER
AVCodec ff_sp5x_decoder = {
    .name           = "sp5x",
    .long_name      = NULL_IF_CONFIG_SMALL("Sunplus JPEG (SP5X)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SP5X,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    .receive_frame  = ff_mjpeg_receive_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .max_lowres     = 3,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif
#if CONFIG_AMV_DECODER
AVCodec ff_amv_decoder = {
    .name           = "amv",
    .long_name      = NULL_IF_CONFIG_SMALL("AMV Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AMV,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    .receive_frame  = ff_mjpeg_receive_frame,
    .max_lowres     = 3,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif
