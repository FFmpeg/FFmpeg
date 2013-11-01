/*
 * Electronic Arts Madcow Video Decoder
 * Copyright (c) 2007-2009 Peter Ross
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
 * Electronic Arts Madcow Video Decoder
 * @author Peter Ross <pross@xvid.org>
 *
 * @see technical details at
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_MAD
 */

#include "avcodec.h"
#include "get_bits.h"
#include "aandcttab.h"
#include "eaidct.h"
#include "internal.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "libavutil/imgutils.h"

#define EA_PREAMBLE_SIZE    8
#define MADk_TAG MKTAG('M', 'A', 'D', 'k')    /* MAD i-frame */
#define MADm_TAG MKTAG('M', 'A', 'D', 'm')    /* MAD p-frame */
#define MADe_TAG MKTAG('M', 'A', 'D', 'e')    /* MAD lqp-frame */

typedef struct MadContext {
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame last_frame;
    GetBitContext gb;
    void *bitstream_buf;
    unsigned int bitstream_buf_size;
    DECLARE_ALIGNED(16, int16_t, block)[64];
    ScanTable scantable;
    uint16_t quant_matrix[64];
    int mb_x;
    int mb_y;
} MadContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    MadContext *s = avctx->priv_data;
    s->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ff_dsputil_init(&s->dsp, avctx);
    ff_init_scantable_permutation(s->dsp.idct_permutation, FF_NO_IDCT_PERM);
    ff_init_scantable(s->dsp.idct_permutation, &s->scantable, ff_zigzag_direct);
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

static inline void comp_block(MadContext *t, AVFrame *frame,
                              int mb_x, int mb_y,
                              int j, int mv_x, int mv_y, int add)
{
    if (j < 4) {
        unsigned offset = (mb_y*16 + ((j&2)<<2) + mv_y)*t->last_frame.linesize[0] + mb_x*16 + ((j&1)<<3) + mv_x;
        if (offset >= (t->avctx->height - 7) * t->last_frame.linesize[0] - 7)
            return;
        comp(frame->data[0] + (mb_y*16 + ((j&2)<<2))*frame->linesize[0] + mb_x*16 + ((j&1)<<3),
             frame->linesize[0],
             t->last_frame.data[0] + offset,
             t->last_frame.linesize[0], add);
    } else if (!(t->avctx->flags & CODEC_FLAG_GRAY)) {
        int index = j - 3;
        unsigned offset = (mb_y * 8 + (mv_y/2))*t->last_frame.linesize[index] + mb_x * 8 + (mv_x/2);
        if (offset >= (t->avctx->height/2 - 7) * t->last_frame.linesize[index] - 7)
            return;
        comp(frame->data[index] + (mb_y*8)*frame->linesize[index] + mb_x * 8,
             frame->linesize[index],
             t->last_frame.data[index] + offset,
             t->last_frame.linesize[index], add);
    }
}

static inline void idct_put(MadContext *t, AVFrame *frame, int16_t *block,
                            int mb_x, int mb_y, int j)
{
    if (j < 4) {
        ff_ea_idct_put_c(
            frame->data[0] + (mb_y*16 + ((j&2)<<2))*frame->linesize[0] + mb_x*16 + ((j&1)<<3),
            frame->linesize[0], block);
    } else if (!(t->avctx->flags & CODEC_FLAG_GRAY)) {
        int index = j - 3;
        ff_ea_idct_put_c(
            frame->data[index] + (mb_y*8)*frame->linesize[index] + mb_x*8,
            frame->linesize[index], block);
    }
}

static inline int decode_block_intra(MadContext *s, int16_t * block)
{
    int level, i, j, run;
    RLTable *rl = &ff_rl_mpeg1;
    const uint8_t *scantable = s->scantable.permutated;
    int16_t *quant_matrix = s->quant_matrix;

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
                return -1;
            }

            block[j] = level;
        }
        CLOSE_READER(re, &s->gb);
    }
    return 0;
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

static int decode_mb(MadContext *s, AVFrame *frame, int inter)
{
    int mv_map = 0;
    int mv_x, mv_y;
    int j;

    if (inter) {
        int v = decode210(&s->gb);
        if (v < 2) {
            mv_map = v ? get_bits(&s->gb, 6) : 63;
            mv_x = decode_motion(&s->gb);
            mv_y = decode_motion(&s->gb);
        }
    }

    for (j=0; j<6; j++) {
        if (mv_map & (1<<j)) {  // mv_x and mv_y are guarded by mv_map
            int add = 2*decode_motion(&s->gb);
            if (s->last_frame.data[0])
                comp_block(s, frame, s->mb_x, s->mb_y, j, mv_x, mv_y, add);
        } else {
            s->dsp.clear_block(s->block);
            if(decode_block_intra(s, s->block) < 0)
                return -1;
            idct_put(s, frame, s->block, s->mb_x, s->mb_y, j);
        }
    }
    return 0;
}

static void calc_quant_matrix(MadContext *s, int qscale)
{
    int i;

    s->quant_matrix[0] = (ff_inv_aanscales[0]*ff_mpeg1_default_intra_matrix[0]) >> 11;
    for (i=1; i<64; i++)
        s->quant_matrix[i] = (ff_inv_aanscales[i]*ff_mpeg1_default_intra_matrix[i]*qscale + 32) >> 10;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;
    MadContext *s     = avctx->priv_data;
    AVFrame *frame    = data;
    int width, height;
    int chunk_type;
    int inter, ret;

    if (buf_size < 26) {
        av_log(avctx, AV_LOG_ERROR, "Input buffer too small\n");
        *got_frame = 0;
        return AVERROR_INVALIDDATA;
    }

    chunk_type = AV_RL32(&buf[0]);
    inter = (chunk_type == MADm_TAG || chunk_type == MADe_TAG);
    buf += 8;

    av_reduce(&avctx->time_base.num, &avctx->time_base.den,
              AV_RL16(&buf[6]), 1000, 1<<30);

    width  = AV_RL16(&buf[8]);
    height = AV_RL16(&buf[10]);
    calc_quant_matrix(s, buf[13]);
    buf += 16;

    if (width < 16 || height < 16) {
        av_log(avctx, AV_LOG_ERROR, "Dimensions too small\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->width != width || avctx->height != height) {
        av_frame_unref(&s->last_frame);
        if((width * height)/2048*7 > buf_end-buf)
            return AVERROR_INVALIDDATA;
        if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
            return ret;
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (inter && !s->last_frame.data[0]) {
        av_log(avctx, AV_LOG_WARNING, "Missing reference frame.\n");
        ret = ff_get_buffer(avctx, &s->last_frame, AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            return ret;
        memset(s->last_frame.data[0], 0, s->last_frame.height *
               s->last_frame.linesize[0]);
        memset(s->last_frame.data[1], 0x80, s->last_frame.height / 2 *
               s->last_frame.linesize[1]);
        memset(s->last_frame.data[2], 0x80, s->last_frame.height / 2 *
               s->last_frame.linesize[2]);
    }

    av_fast_padded_malloc(&s->bitstream_buf, &s->bitstream_buf_size,
                          buf_end - buf);
    if (!s->bitstream_buf)
        return AVERROR(ENOMEM);
    s->dsp.bswap16_buf(s->bitstream_buf, (const uint16_t*)buf, (buf_end-buf)/2);
    memset((uint8_t*)s->bitstream_buf + (buf_end-buf), 0, FF_INPUT_BUFFER_PADDING_SIZE);
    init_get_bits(&s->gb, s->bitstream_buf, 8*(buf_end-buf));

    for (s->mb_y=0; s->mb_y < (avctx->height+15)/16; s->mb_y++)
        for (s->mb_x=0; s->mb_x < (avctx->width +15)/16; s->mb_x++)
            if(decode_mb(s, frame, inter) < 0)
                return AVERROR_INVALIDDATA;

    *got_frame = 1;

    if (chunk_type != MADe_TAG) {
        av_frame_unref(&s->last_frame);
        if ((ret = av_frame_ref(&s->last_frame, frame)) < 0)
            return ret;
    }

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    MadContext *t = avctx->priv_data;
    av_frame_unref(&t->last_frame);
    av_free(t->bitstream_buf);
    return 0;
}

AVCodec ff_eamad_decoder = {
    .name           = "eamad",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts Madcow Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MAD,
    .priv_data_size = sizeof(MadContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
