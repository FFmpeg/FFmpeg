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
 * @file
 * Electronic Arts TQI Video Decoder
 * @author Peter Ross <pross@xvid.org>
 * @see http://wiki.multimedia.cx/index.php?title=Electronic_Arts_TQI
 */

#include "avcodec.h"
#include "blockdsp.h"
#include "bswapdsp.h"
#include "get_bits.h"
#include "aandcttab.h"
#include "eaidct.h"
#include "idctdsp.h"
#include "internal.h"
#include "mpeg12.h"
#include "mpegvideo.h"

typedef struct TqiContext {
    MpegEncContext s;
    BswapDSPContext bsdsp;
    void *bitstream_buf;
    unsigned int bitstream_buf_size;
    DECLARE_ALIGNED(16, int16_t, block)[6][64];
} TqiContext;

static av_cold int tqi_decode_init(AVCodecContext *avctx)
{
    TqiContext *t = avctx->priv_data;
    MpegEncContext *s = &t->s;
    s->avctx = avctx;
    ff_blockdsp_init(&s->bdsp, avctx);
    ff_bswapdsp_init(&t->bsdsp);
    ff_idctdsp_init(&s->idsp, avctx);
    ff_init_scantable_permutation(s->idsp.idct_permutation, FF_IDCT_PERM_NONE);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable, ff_zigzag_direct);
    s->qscale = 1;
    avctx->framerate = (AVRational){ 15, 1 };
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ff_mpeg12_init_vlcs();
    return 0;
}

static int tqi_decode_mb(MpegEncContext *s, int16_t (*block)[64])
{
    int n;
    s->bdsp.clear_blocks(block[0]);
    for (n=0; n<6; n++)
        if (ff_mpeg1_decode_block_intra(s, block[n], n) < 0)
            return -1;

    return 0;
}

static inline void tqi_idct_put(TqiContext *t, AVFrame *frame, int16_t (*block)[64])
{
    MpegEncContext *s = &t->s;
    int linesize = frame->linesize[0];
    uint8_t *dest_y  = frame->data[0] + (s->mb_y * 16* linesize            ) + s->mb_x * 16;
    uint8_t *dest_cb = frame->data[1] + (s->mb_y * 8 * frame->linesize[1]) + s->mb_x * 8;
    uint8_t *dest_cr = frame->data[2] + (s->mb_y * 8 * frame->linesize[2]) + s->mb_x * 8;

    ff_ea_idct_put_c(dest_y                 , linesize, block[0]);
    ff_ea_idct_put_c(dest_y              + 8, linesize, block[1]);
    ff_ea_idct_put_c(dest_y + 8*linesize    , linesize, block[2]);
    ff_ea_idct_put_c(dest_y + 8*linesize + 8, linesize, block[3]);
    if(!(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
        ff_ea_idct_put_c(dest_cb, frame->linesize[1], block[4]);
        ff_ea_idct_put_c(dest_cr, frame->linesize[2], block[5]);
    }
}

static void tqi_calculate_qtable(MpegEncContext *s, int quant)
{
    const int qscale = (215 - 2*quant)*5;
    int i;
    s->intra_matrix[0] = (ff_inv_aanscales[0]*ff_mpeg1_default_intra_matrix[0])>>11;
    for(i=1; i<64; i++)
        s->intra_matrix[i] = (ff_inv_aanscales[i]*ff_mpeg1_default_intra_matrix[i]*qscale + 32)>>14;
}

static int tqi_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    TqiContext *t = avctx->priv_data;
    MpegEncContext *s = &t->s;
    AVFrame *frame = data;
    int ret;

    s->width  = AV_RL16(&buf[0]);
    s->height = AV_RL16(&buf[2]);
    tqi_calculate_qtable(s, buf[4]);
    buf += 8;

    ret = ff_set_dimensions(s->avctx, s->width, s->height);
    if (ret < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    av_fast_padded_malloc(&t->bitstream_buf, &t->bitstream_buf_size,
                          buf_end - buf);
    if (!t->bitstream_buf)
        return AVERROR(ENOMEM);
    t->bsdsp.bswap_buf(t->bitstream_buf, (const uint32_t *) buf,
                       (buf_end - buf) / 4);
    init_get_bits(&s->gb, t->bitstream_buf, 8*(buf_end-buf));

    s->last_dc[0] = s->last_dc[1] = s->last_dc[2] = 0;
    for (s->mb_y=0; s->mb_y<(avctx->height+15)/16; s->mb_y++)
    for (s->mb_x=0; s->mb_x<(avctx->width+15)/16; s->mb_x++)
    {
        if (tqi_decode_mb(s, t->block) < 0)
            goto end;
        tqi_idct_put(t, frame, t->block);
    }
    end:

    *got_frame = 1;
    return buf_size;
}

static av_cold int tqi_decode_end(AVCodecContext *avctx)
{
    TqiContext *t = avctx->priv_data;
    av_freep(&t->bitstream_buf);
    return 0;
}

AVCodec ff_eatqi_decoder = {
    .name           = "eatqi",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts TQI Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TQI,
    .priv_data_size = sizeof(TqiContext),
    .init           = tqi_decode_init,
    .close          = tqi_decode_end,
    .decode         = tqi_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
