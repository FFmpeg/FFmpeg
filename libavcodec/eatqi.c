/*
 * Electronic Arts TQI Video Decoder
 * Copyright (c) 2007-2009 Peter Ross <pross@xvid.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file eatqi.c
 * Electronic Arts TQI Video Decoder
 * by Peter Ross <pross@xvid.org>
 *
 * Technical details here:
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_TQI
 */

#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "aandcttab.h"
#include "mpeg12.h"
#include "mpegvideo.h"

typedef struct TqiContext {
    MpegEncContext s;
    AVFrame frame;
    void *bitstream_buf;
    unsigned int bitstream_buf_size;
} TqiContext;

static av_cold int tqi_decode_init(AVCodecContext *avctx)
{
    TqiContext *t = avctx->priv_data;
    MpegEncContext *s = &t->s;
    s->avctx = avctx;
    if(avctx->idct_algo==FF_IDCT_AUTO)
        avctx->idct_algo=FF_IDCT_EA;
    dsputil_init(&s->dsp, avctx);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable, ff_zigzag_direct);
    s->qscale = 1;
    avctx->time_base = (AVRational){1, 15};
    avctx->pix_fmt = PIX_FMT_YUV420P;
    ff_mpeg12_init_vlcs();
    return 0;
}

static void tqi_decode_mb(MpegEncContext *s, DCTELEM (*block)[64])
{
    int n;
    s->dsp.clear_blocks(block[0]);
    for (n=0; n<6; n++)
        ff_mpeg1_decode_block_intra(s, block[n], n);
}

static inline void tqi_idct_put(TqiContext *t, DCTELEM (*block)[64])
{
    MpegEncContext *s = &t->s;
    int linesize= t->frame.linesize[0];
    uint8_t *dest_y  = t->frame.data[0] + (s->mb_y * 16* linesize            ) + s->mb_x * 16;
    uint8_t *dest_cb = t->frame.data[1] + (s->mb_y * 8 * t->frame.linesize[1]) + s->mb_x * 8;
    uint8_t *dest_cr = t->frame.data[2] + (s->mb_y * 8 * t->frame.linesize[2]) + s->mb_x * 8;

    s->dsp.idct_put(dest_y                 , linesize, block[0]);
    s->dsp.idct_put(dest_y              + 8, linesize, block[1]);
    s->dsp.idct_put(dest_y + 8*linesize    , linesize, block[2]);
    s->dsp.idct_put(dest_y + 8*linesize + 8, linesize, block[3]);
    if(!(s->avctx->flags&CODEC_FLAG_GRAY)) {
        s->dsp.idct_put(dest_cb, t->frame.linesize[1], block[4]);
        s->dsp.idct_put(dest_cr, t->frame.linesize[2], block[5]);
    }
}

static void tqi_calculate_qtable(MpegEncContext *s, int quant)
{
    const int qscale = (215 - 2*quant)*5;
    int i;
    if (s->avctx->idct_algo==FF_IDCT_EA) {
        s->intra_matrix[0] = (ff_inv_aanscales[0]*ff_mpeg1_default_intra_matrix[0])>>11;
        for(i=1; i<64; i++)
            s->intra_matrix[i] = (ff_inv_aanscales[i]*ff_mpeg1_default_intra_matrix[i]*qscale + 32)>>14;
    }else{
        s->intra_matrix[0] = ff_mpeg1_default_intra_matrix[0];
        for(i=1; i<64; i++)
            s->intra_matrix[i] = (ff_mpeg1_default_intra_matrix[i]*qscale + 32)>>3;
    }
}

static int tqi_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    TqiContext *t = avctx->priv_data;
    MpegEncContext *s = &t->s;
    DECLARE_ALIGNED_16(DCTELEM, block[6][64]);

    s->width  = AV_RL16(&buf[0]);
    s->height = AV_RL16(&buf[2]);
    tqi_calculate_qtable(s, buf[4]);
    buf += 8;

    if (t->frame.data[0])
        avctx->release_buffer(avctx, &t->frame);

    if (s->avctx->width!=s->width || s->avctx->height!=s->height)
        avcodec_set_dimensions(s->avctx, s->width, s->height);

    if(avctx->get_buffer(avctx, &t->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    av_fast_malloc(&t->bitstream_buf, &t->bitstream_buf_size, (buf_end-buf) + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!t->bitstream_buf)
        return AVERROR(ENOMEM);
    s->dsp.bswap_buf(t->bitstream_buf, (const uint32_t*)buf, (buf_end-buf)/4);
    init_get_bits(&s->gb, t->bitstream_buf, 8*(buf_end-buf));

    s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 0;
    for (s->mb_y=0; s->mb_y<(avctx->height+15)/16; s->mb_y++)
    for (s->mb_x=0; s->mb_x<(avctx->width+15)/16; s->mb_x++)
    {
        tqi_decode_mb(s, block);
        tqi_idct_put(t, block);
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = t->frame;
    return buf_size;
}

static av_cold int tqi_decode_end(AVCodecContext *avctx)
{
    TqiContext *t = avctx->priv_data;
    if(t->frame.data[0])
        avctx->release_buffer(avctx, &t->frame);
    av_free(t->bitstream_buf);
    return 0;
}

AVCodec eatqi_decoder = {
    "eatqi",
    CODEC_TYPE_VIDEO,
    CODEC_ID_TQI,
    sizeof(TqiContext),
    tqi_decode_init,
    NULL,
    tqi_decode_end,
    tqi_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Electronic Arts TQI Video"),
};
