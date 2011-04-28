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
#include "mjpeg.h"
#include "mjpegdec.h"
#include "sp5x.h"


static int sp5x_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVPacket avpkt_recoded;
#if 0
    MJpegDecodeContext *s = avctx->priv_data;
#endif
    const int qscale = 5;
    const uint8_t *buf_ptr;
    uint8_t *recoded;
    int i = 0, j = 0;

    if (!avctx->width || !avctx->height)
        return -1;

    buf_ptr = buf;

#if 1
    recoded = av_mallocz(buf_size + 1024);
    if (!recoded)
        return -1;

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

    if(avctx->codec_id==CODEC_ID_AMV)
        for (i = 2; i < buf_size-2 && j < buf_size+1024-2; i++)
            recoded[j++] = buf[i];
    else
    for (i = 14; i < buf_size && j < buf_size+1024-2; i++)
    {
        recoded[j++] = buf[i];
        if (buf[i] == 0xff)
            recoded[j++] = 0;
    }

    /* EOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD9;

    av_init_packet(&avpkt_recoded);
    avpkt_recoded.data = recoded;
    avpkt_recoded.size = j;
    i = ff_mjpeg_decode_frame(avctx, data, data_size, &avpkt_recoded);

    av_free(recoded);

#else
    /* SOF */
    s->bits = 8;
    s->width  = avctx->coded_width;
    s->height = avctx->coded_height;
    s->nb_components = 3;
    s->component_id[0] = 0;
    s->h_count[0] = 2;
    s->v_count[0] = 2;
    s->quant_index[0] = 0;
    s->component_id[1] = 1;
    s->h_count[1] = 1;
    s->v_count[1] = 1;
    s->quant_index[1] = 1;
    s->component_id[2] = 2;
    s->h_count[2] = 1;
    s->v_count[2] = 1;
    s->quant_index[2] = 1;
    s->h_max = 2;
    s->v_max = 2;

    s->qscale_table = av_mallocz((s->width+15)/16);
    avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV420P : PIX_FMT_YUVJ420;
    s->interlaced = 0;

    s->picture.reference = 0;
    if (avctx->get_buffer(avctx, &s->picture) < 0)
    {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    s->picture.pict_type = FF_I_TYPE;
    s->picture.key_frame = 1;

    for (i = 0; i < 3; i++)
        s->linesize[i] = s->picture.linesize[i] << s->interlaced;

    /* DQT */
    for (i = 0; i < 64; i++)
    {
        j = s->scantable.permutated[i];
        s->quant_matrixes[0][j] = sp5x_quant_table[(qscale * 2) + i];
    }
    s->qscale[0] = FFMAX(
        s->quant_matrixes[0][s->scantable.permutated[1]],
        s->quant_matrixes[0][s->scantable.permutated[8]]) >> 1;

    for (i = 0; i < 64; i++)
    {
        j = s->scantable.permutated[i];
        s->quant_matrixes[1][j] = sp5x_quant_table[(qscale * 2) + 1 + i];
    }
    s->qscale[1] = FFMAX(
        s->quant_matrixes[1][s->scantable.permutated[1]],
        s->quant_matrixes[1][s->scantable.permutated[8]]) >> 1;

    /* DHT */

    /* SOS */
    s->comp_index[0] = 0;
    s->nb_blocks[0] = s->h_count[0] * s->v_count[0];
    s->h_scount[0] = s->h_count[0];
    s->v_scount[0] = s->v_count[0];
    s->dc_index[0] = 0;
    s->ac_index[0] = 0;

    s->comp_index[1] = 1;
    s->nb_blocks[1] = s->h_count[1] * s->v_count[1];
    s->h_scount[1] = s->h_count[1];
    s->v_scount[1] = s->v_count[1];
    s->dc_index[1] = 1;
    s->ac_index[1] = 1;

    s->comp_index[2] = 2;
    s->nb_blocks[2] = s->h_count[2] * s->v_count[2];
    s->h_scount[2] = s->h_count[2];
    s->v_scount[2] = s->v_count[2];
    s->dc_index[2] = 1;
    s->ac_index[2] = 1;

    for (i = 0; i < 3; i++)
        s->last_dc[i] = 1024;

    s->mb_width = (s->width * s->h_max * 8 -1) / (s->h_max * 8);
    s->mb_height = (s->height * s->v_max * 8 -1) / (s->v_max * 8);

    init_get_bits(&s->gb, buf+14, (buf_size-14)*8);

    return mjpeg_decode_scan(s);
#endif

    return i;
}

AVCodec sp5x_decoder = {
    "sp5x",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_SP5X,
    sizeof(MJpegDecodeContext),
    ff_mjpeg_decode_init,
    NULL,
    ff_mjpeg_decode_end,
    sp5x_decode_frame,
    CODEC_CAP_DR1,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Sunplus JPEG (SP5X)"),
};

AVCodec amv_decoder = {
    "amv",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_AMV,
    sizeof(MJpegDecodeContext),
    ff_mjpeg_decode_init,
    NULL,
    ff_mjpeg_decode_end,
    sp5x_decode_frame,
    0,
    .long_name = NULL_IF_CONFIG_SMALL("AMV Video"),
};
