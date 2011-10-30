/*
 * Electronic Arts Madcow Video Decoder
 * Copyright (c) 2007-2009 Peter Ross
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 * Electronic Arts Madcow Video Decoder
 * @author Peter Ross <pross@xvid.org>
 *
 * @see technical details at
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_MAD
 */

#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "aandcttab.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "libavutil/imgutils.h"

#define EA_PREAMBLE_SIZE    8
#define MADk_TAG MKTAG('M', 'A', 'D', 'k')    /* MAD i-frame */
#define MADm_TAG MKTAG('M', 'A', 'D', 'm')    /* MAD p-frame */
#define MADe_TAG MKTAG('M', 'A', 'D', 'e')    /* MAD lqp-frame */

typedef struct MadContext {
    MpegEncContext s;
    AVFrame frame;
    AVFrame last_frame;
    void *bitstream_buf;
    unsigned int bitstream_buf_size;
    DECLARE_ALIGNED(16, DCTELEM, block)[64];
} MadContext;

static void bswap16_buf(uint16_t *dst, const uint16_t *src, int count)
{
    int i;
    for (i=0; i<count; i++)
        dst[i] = av_bswap16(src[i]);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MadContext *t = avctx->priv_data;
    MpegEncContext *s = &t->s;
    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    if (avctx->idct_algo == FF_IDCT_AUTO)
        avctx->idct_algo = FF_IDCT_EA;
    dsputil_init(&s->dsp, avctx);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable, ff_zigzag_direct);
    ff_mpeg12_init_vlcs();
    return 0;
}

static inline void comp(unsigned char *dst, int dst_stride,
                        unsigned char *src, int src_stride, int add)
{
    int j, i;
    for (j=0; j<8; j++)
        for (i=0; i<8; i++)
            dst[j*dst_stride + i] = av_clip_uint8(src[j*src_stride + i] + add);
}

static inline void comp_block(MadContext *t, int mb_x, int mb_y,
                              int j, int mv_x, int mv_y, int add)
{
    MpegEncContext *s = &t->s;
    if (j < 4) {
        comp(t->frame.data[0] + (mb_y*16 + ((j&2)<<2))*t->frame.linesize[0] + mb_x*16 + ((j&1)<<3),
             t->frame.linesize[0],
             t->last_frame.data[0] + (mb_y*16 + ((j&2)<<2) + mv_y)*t->last_frame.linesize[0] + mb_x*16 + ((j&1)<<3) + mv_x,
             t->last_frame.linesize[0], add);
    } else if (!(s->avctx->flags & CODEC_FLAG_GRAY)) {
        int index = j - 3;
        comp(t->frame.data[index] + (mb_y*8)*t->frame.linesize[index] + mb_x * 8,
             t->frame.linesize[index],
             t->last_frame.data[index] + (mb_y * 8 + (mv_y/2))*t->last_frame.linesize[index] + mb_x * 8 + (mv_x/2),
             t->last_frame.linesize[index], add);
    }
}

static inline void idct_put(MadContext *t, DCTELEM *block, int mb_x, int mb_y, int j)
{
    MpegEncContext *s = &t->s;
    if (j < 4) {
        s->dsp.idct_put(
            t->frame.data[0] + (mb_y*16 + ((j&2)<<2))*t->frame.linesize[0] + mb_x*16 + ((j&1)<<3),
            t->frame.linesize[0], block);
    } else if (!(s->avctx->flags & CODEC_FLAG_GRAY)) {
        int index = j - 3;
        s->dsp.idct_put(
            t->frame.data[index] + (mb_y*8)*t->frame.linesize[index] + mb_x*8,
            t->frame.linesize[index], block);
    }
}

static inline void decode_block_intra(MadContext * t, DCTELEM * block)
{
    MpegEncContext *s = &t->s;
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    const uint8_t *scantable = s->intra_scantable.permutated;
    int16_t *quant_matrix = s->intra_matrix;

    block[0] = (128 + get_sbits(&s->gb, 8)) * quant_matrix[0];

    /* The RL decoder is derived from mpeg1_decode_block_intra;
       Escaped level and run values a decoded differently */
    i = 0;
    {
        OPEN_READER(re, &s->gb);
        /* now quantify & encode AC coefficients */
        for (;;) {
            UPDATE_CACHE(re, &s->gb);
            GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);

            if (level == 127) {
                break;
            } else if (level != 0) {
                i += run;
                j = scantable[i];
                level = (level*quant_matrix[j]) >> 4;
                level = (level-1)|1;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            } else {
                /* escape */
                UPDATE_CACHE(re, &s->gb);
                level = SHOW_SBITS(re, &s->gb, 10); SKIP_BITS(re, &s->gb, 10);

                UPDATE_CACHE(re, &s->gb);
                run = SHOW_UBITS(re, &s->gb, 6)+1; LAST_SKIP_BITS(re, &s->gb, 6);

                i += run;
                j = scantable[i];
                if (level < 0) {
                    level = -level;
                    level = (level*quant_matrix[j]) >> 4;
                    level = (level-1)|1;
                    level = -level;
                } else {
                    level = (level*quant_matrix[j]) >> 4;
                    level = (level-1)|1;
                }
            }
            if (i > 63) {
                av_log(s->avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                return;
            }

            block[j] = level;
        }
        CLOSE_READER(re, &s->gb);
    }
}

static int decode_motion(GetBitContext *gb)
{
    int value = 0;
    if (get_bits1(gb)) {
        if (get_bits1(gb))
            value = -17;
        value += get_bits(gb, 4) + 1;
    }
    return value;
}

static void decode_mb(MadContext *t, int inter)
{
    MpegEncContext *s = &t->s;
    int mv_map = 0;
    int mv_x, mv_y;
    int j;

    if (inter) {
        int v = decode210(&s->gb);
        if (v < 2) {
            mv_map = v ? get_bits(&s->gb, 6) : 63;
            mv_x = decode_motion(&s->gb);
            mv_y = decode_motion(&s->gb);
        } else {
            mv_map = 0;
        }
    }

    for (j=0; j<6; j++) {
        if (mv_map & (1<<j)) {  // mv_x and mv_y are guarded by mv_map
            int add = 2*decode_motion(&s->gb);
            comp_block(t, s->mb_x, s->mb_y, j, mv_x, mv_y, add);
        } else {
            s->dsp.clear_block(t->block);
            decode_block_intra(t, t->block);
            idct_put(t, t->block, s->mb_x, s->mb_y, j);
        }
    }
}

static void calc_intra_matrix(MadContext *t, int qscale)
{
    MpegEncContext *s = &t->s;
    int i;

    if (s->avctx->idct_algo == FF_IDCT_EA) {
        s->intra_matrix[0] = (ff_inv_aanscales[0]*ff_mpeg1_default_intra_matrix[0]) >> 11;
        for (i=1; i<64; i++)
            s->intra_matrix[i] = (ff_inv_aanscales[i]*ff_mpeg1_default_intra_matrix[i]*qscale + 32) >> 10;
    } else {
        s->intra_matrix[0] = ff_mpeg1_default_intra_matrix[0];
        for (i=1; i<64; i++)
            s->intra_matrix[i] = (ff_mpeg1_default_intra_matrix[i]*qscale) << 1;
    }
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    MadContext *t     = avctx->priv_data;
    MpegEncContext *s = &t->s;
    int chunk_type;
    int inter;

    if (buf_size < 17) {
        av_log(avctx, AV_LOG_ERROR, "Input buffer too small\n");
        *data_size = 0;
        return -1;
    }

    chunk_type = AV_RL32(&buf[0]);
    inter = (chunk_type == MADm_TAG || chunk_type == MADe_TAG);
    buf += 8;

    av_reduce(&avctx->time_base.num, &avctx->time_base.den,
              AV_RL16(&buf[6]), 1000, 1<<30);

    s->width  = AV_RL16(&buf[8]);
    s->height = AV_RL16(&buf[10]);
    calc_intra_matrix(t, buf[13]);
    buf += 16;

    if (avctx->width != s->width || avctx->height != s->height) {
        if (av_image_check_size(s->width, s->height, 0, avctx) < 0)
            return -1;
        avcodec_set_dimensions(avctx, s->width, s->height);
        if (t->frame.data[0])
            avctx->release_buffer(avctx, &t->frame);
    }

    t->frame.reference = 1;
    if (!t->frame.data[0]) {
        if (avctx->get_buffer(avctx, &t->frame) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return -1;
        }
    }

    av_fast_malloc(&t->bitstream_buf, &t->bitstream_buf_size, (buf_end-buf) + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!t->bitstream_buf)
        return AVERROR(ENOMEM);
    bswap16_buf(t->bitstream_buf, (const uint16_t*)buf, (buf_end-buf)/2);
    init_get_bits(&s->gb, t->bitstream_buf, 8*(buf_end-buf));

    for (s->mb_y=0; s->mb_y < (avctx->height+15)/16; s->mb_y++)
        for (s->mb_x=0; s->mb_x < (avctx->width +15)/16; s->mb_x++)
            decode_mb(t, inter);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = t->frame;

    if (chunk_type != MADe_TAG)
        FFSWAP(AVFrame, t->frame, t->last_frame);

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    MadContext *t = avctx->priv_data;
    if (t->frame.data[0])
        avctx->release_buffer(avctx, &t->frame);
    if (t->last_frame.data[0])
        avctx->release_buffer(avctx, &t->last_frame);
    av_free(t->bitstream_buf);
    return 0;
}

AVCodec ff_eamad_decoder = {
    .name           = "eamad",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MAD,
    .priv_data_size = sizeof(MadContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Electronic Arts Madcow Video")
};
