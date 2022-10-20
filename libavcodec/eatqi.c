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

#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "bswapdsp.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "aandcttab.h"
#include "eaidct.h"
#include "mpeg12data.h"
#include "mpeg12dec.h"

typedef struct TqiContext {
    AVCodecContext *avctx;
    GetBitContext gb;
    BlockDSPContext bdsp;
    BswapDSPContext bsdsp;

    void *bitstream_buf;
    unsigned int bitstream_buf_size;

    int mb_x, mb_y;
    uint16_t intra_matrix[64];
    int last_dc[3];

    DECLARE_ALIGNED(32, int16_t, block)[6][64];
} TqiContext;

static av_cold int tqi_decode_init(AVCodecContext *avctx)
{
    TqiContext *t = avctx->priv_data;

    ff_blockdsp_init(&t->bdsp);
    ff_bswapdsp_init(&t->bsdsp);

    avctx->framerate = (AVRational){ 15, 1 };
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ff_mpeg12_init_vlcs();
    return 0;
}

static int tqi_decode_mb(TqiContext *t, int16_t (*block)[64])
{
    int n;

    t->bdsp.clear_blocks(block[0]);
    for (n = 0; n < 6; n++) {
        int ret = ff_mpeg1_decode_block_intra(&t->gb,
                                              t->intra_matrix,
                                              ff_zigzag_direct,
                                              t->last_dc, block[n], n, 1);
        if (ret < 0) {
            av_log(t->avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n",
                   t->mb_x, t->mb_y);
            return ret;
        }
    }

    return 0;
}

static inline void tqi_idct_put(AVCodecContext *avctx, AVFrame *frame,
                                int16_t (*block)[64])
{
    TqiContext *t = avctx->priv_data;
    ptrdiff_t linesize = frame->linesize[0];
    uint8_t *dest_y  = frame->data[0] + t->mb_y * 16 * linesize           + t->mb_x * 16;
    uint8_t *dest_cb = frame->data[1] + t->mb_y *  8 * frame->linesize[1] + t->mb_x *  8;
    uint8_t *dest_cr = frame->data[2] + t->mb_y *  8 * frame->linesize[2] + t->mb_x *  8;

    ff_ea_idct_put_c(dest_y                 , linesize, block[0]);
    ff_ea_idct_put_c(dest_y              + 8, linesize, block[1]);
    ff_ea_idct_put_c(dest_y + 8*linesize    , linesize, block[2]);
    ff_ea_idct_put_c(dest_y + 8*linesize + 8, linesize, block[3]);

    if (!(avctx->flags & AV_CODEC_FLAG_GRAY)) {
        ff_ea_idct_put_c(dest_cb, frame->linesize[1], block[4]);
        ff_ea_idct_put_c(dest_cr, frame->linesize[2], block[5]);
    }
}

static void tqi_calculate_qtable(TqiContext *t, int quant)
{
    const int64_t qscale = (215 - 2*quant)*5;
    int i;

    t->intra_matrix[0] = (ff_inv_aanscales[0] * ff_mpeg1_default_intra_matrix[0]) >> 11;
    for(i=1; i<64; i++)
        t->intra_matrix[i] = (ff_inv_aanscales[i] * ff_mpeg1_default_intra_matrix[i] * qscale + 32) >> 14;
}

static int tqi_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    TqiContext *t = avctx->priv_data;
    int ret, w, h;

    if (buf_size < 12)
        return AVERROR_INVALIDDATA;

    t->avctx = avctx;

    w = AV_RL16(&buf[0]);
    h = AV_RL16(&buf[2]);
    tqi_calculate_qtable(t, buf[4]);
    buf += 8;

    ret = ff_set_dimensions(avctx, w, h);
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
    init_get_bits(&t->gb, t->bitstream_buf, 8 * (buf_end - buf));

    t->last_dc[0] =
    t->last_dc[1] =
    t->last_dc[2] = 0;
    for (t->mb_y = 0; t->mb_y < (h + 15) / 16; t->mb_y++) {
        for (t->mb_x = 0; t->mb_x < (w + 15) / 16; t->mb_x++) {
            if (tqi_decode_mb(t, t->block) < 0)
                goto end;
            tqi_idct_put(avctx, frame, t->block);
        }
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

const FFCodec ff_eatqi_decoder = {
    .p.name         = "eatqi",
    CODEC_LONG_NAME("Electronic Arts TQI Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_TQI,
    .priv_data_size = sizeof(TqiContext),
    .init           = tqi_decode_init,
    .close          = tqi_decode_end,
    FF_CODEC_DECODE_CB(tqi_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
