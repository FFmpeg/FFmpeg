/*
 * Electronic Arts TGQ Video Decoder
 * Copyright (c) 2007-2008 Peter Ross <pross@xvid.org>
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
 * Electronic Arts TGQ Video Decoder
 * @author Peter Ross <pross@xvid.org>
 *
 * Technical details here:
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_TGQ
 */

#include "avcodec.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "bytestream.h"
#include "dsputil.h"
#include "aandcttab.h"

typedef struct TgqContext {
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frame;
    int width,height;
    ScanTable scantable;
    int qtable[64];
    DECLARE_ALIGNED(16, DCTELEM, block)[6][64];
    GetByteContext gb;
} TgqContext;

static av_cold int tgq_decode_init(AVCodecContext *avctx){
    TgqContext *s = avctx->priv_data;
    s->avctx = avctx;
    if(avctx->idct_algo==FF_IDCT_AUTO)
        avctx->idct_algo=FF_IDCT_EA;
    dsputil_init(&s->dsp, avctx);
    ff_init_scantable(s->dsp.idct_permutation, &s->scantable, ff_zigzag_direct);
    avctx->time_base = (AVRational){1, 15};
    avctx->pix_fmt = PIX_FMT_YUV420P;
    return 0;
}

static void tgq_decode_block(TgqContext *s, DCTELEM block[64], GetBitContext *gb){
    uint8_t *perm = s->scantable.permutated;
    int i,j,value;
    block[0] = get_sbits(gb,8) * s->qtable[0];
    for(i=1; i<64; ) {
        switch(show_bits(gb,3)) {
        case 4:
            block[perm[i++]] = 0;
        case 0:
            block[perm[i++]] = 0;
            skip_bits(gb,3);
            break;
        case 5:
        case 1:
            skip_bits(gb,2);
            value = get_bits(gb,6);
            for(j=0; j<value; j++)
                block[perm[i++]] = 0;
            break;
        case 6:
            skip_bits(gb,3);
            block[perm[i]] = -s->qtable[perm[i]];
            i++;
            break;
        case 2:
            skip_bits(gb,3);
            block[perm[i]] = s->qtable[perm[i]];
            i++;
            break;
        case 7: // 111b
        case 3: // 011b
            skip_bits(gb,2);
            if (show_bits(gb,6)==0x3F) {
                skip_bits(gb, 6);
                block[perm[i]] = get_sbits(gb,8)*s->qtable[perm[i]];
            }else{
                block[perm[i]] = get_sbits(gb,6)*s->qtable[perm[i]];
            }
            i++;
            break;
        }
    }
    block[0] += 128<<4;
}

static void tgq_idct_put_mb(TgqContext *s, DCTELEM (*block)[64], int mb_x, int mb_y){
    int linesize= s->frame.linesize[0];
    uint8_t *dest_y  = s->frame.data[0] + (mb_y * 16* linesize            ) + mb_x * 16;
    uint8_t *dest_cb = s->frame.data[1] + (mb_y * 8 * s->frame.linesize[1]) + mb_x * 8;
    uint8_t *dest_cr = s->frame.data[2] + (mb_y * 8 * s->frame.linesize[2]) + mb_x * 8;

    s->dsp.idct_put(dest_y                 , linesize, block[0]);
    s->dsp.idct_put(dest_y              + 8, linesize, block[1]);
    s->dsp.idct_put(dest_y + 8*linesize    , linesize, block[2]);
    s->dsp.idct_put(dest_y + 8*linesize + 8, linesize, block[3]);
    if(!(s->avctx->flags&CODEC_FLAG_GRAY)){
         s->dsp.idct_put(dest_cb, s->frame.linesize[1], block[4]);
         s->dsp.idct_put(dest_cr, s->frame.linesize[2], block[5]);
    }
}

static inline void tgq_dconly(TgqContext *s, unsigned char *dst, int dst_stride, int dc){
    int level = av_clip_uint8((dc*s->qtable[0] + 2056)>>4);
    int j;
    for(j=0;j<8;j++)
        memset(dst+j*dst_stride, level, 8);
}

static void tgq_idct_put_mb_dconly(TgqContext *s, int mb_x, int mb_y, const int8_t *dc)
{
    int linesize= s->frame.linesize[0];
    uint8_t *dest_y  = s->frame.data[0] + (mb_y * 16* linesize            ) + mb_x * 16;
    uint8_t *dest_cb = s->frame.data[1] + (mb_y * 8 * s->frame.linesize[1]) + mb_x * 8;
    uint8_t *dest_cr = s->frame.data[2] + (mb_y * 8 * s->frame.linesize[2]) + mb_x * 8;
    tgq_dconly(s,dest_y                 , linesize, dc[0]);
    tgq_dconly(s,dest_y              + 8, linesize, dc[1]);
    tgq_dconly(s,dest_y + 8*linesize    , linesize, dc[2]);
    tgq_dconly(s,dest_y + 8*linesize + 8, linesize, dc[3]);
    if(!(s->avctx->flags&CODEC_FLAG_GRAY)) {
        tgq_dconly(s,dest_cb, s->frame.linesize[1], dc[4]);
        tgq_dconly(s,dest_cr, s->frame.linesize[2], dc[5]);
    }
}

static void tgq_decode_mb(TgqContext *s, int mb_y, int mb_x){
    int mode;
    int i;
    int8_t dc[6];

    mode = bytestream2_get_byte(&s->gb);
    if (mode>12) {
        GetBitContext gb;
        init_get_bits(&gb, s->gb.buffer, FFMIN(s->gb.buffer_end - s->gb.buffer, mode) * 8);
        for(i=0; i<6; i++)
            tgq_decode_block(s, s->block[i], &gb);
        tgq_idct_put_mb(s, s->block, mb_x, mb_y);
        bytestream2_skip(&s->gb, mode);
    }else{
        if (mode==3) {
            memset(dc, bytestream2_get_byte(&s->gb), 4);
            dc[4] = bytestream2_get_byte(&s->gb);
            dc[5] = bytestream2_get_byte(&s->gb);
        }else if (mode==6) {
            bytestream2_get_buffer(&s->gb, dc, 6);
        }else if (mode==12) {
            for (i = 0; i < 6; i++) {
                dc[i] = bytestream2_get_byte(&s->gb);
                bytestream2_skip(&s->gb, 1);
            }
        }else{
            av_log(s->avctx, AV_LOG_ERROR, "unsupported mb mode %i\n", mode);
        }
        tgq_idct_put_mb_dconly(s, mb_x, mb_y, dc);
    }
}

static void tgq_calculate_qtable(TgqContext *s, int quant){
    int i,j;
    const int a = (14*(100-quant))/100 + 1;
    const int b = (11*(100-quant))/100 + 4;
    for(j=0;j<8;j++)
    for(i=0;i<8;i++)
        if (s->avctx->idct_algo==FF_IDCT_EA)
            s->qtable[j*8+i] = ((a*(j+i)/(7+7) + b)*ff_inv_aanscales[j*8+i])>>(14-4);
        else
            s->qtable[j*8+i] = (a*(j+i)/(7+7) + b)<<3;
}

static int tgq_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt){
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TgqContext *s = avctx->priv_data;
    int x,y;
    int big_endian = AV_RL32(&buf[4]) > 0x000FFFFF;

    if (buf_size < 16) {
        av_log(avctx, AV_LOG_WARNING, "truncated header\n");
        return -1;
    }
    bytestream2_init(&s->gb, buf + 8, buf_size - 8);
    if (big_endian) {
        s->width  = bytestream2_get_be16u(&s->gb);
        s->height = bytestream2_get_be16u(&s->gb);
    } else {
        s->width  = bytestream2_get_le16u(&s->gb);
        s->height = bytestream2_get_le16u(&s->gb);
    }

    if (s->avctx->width!=s->width || s->avctx->height!=s->height) {
        avcodec_set_dimensions(s->avctx, s->width, s->height);
        if (s->frame.data[0])
            avctx->release_buffer(avctx, &s->frame);
    }
    tgq_calculate_qtable(s, bytestream2_get_byteu(&s->gb));
    bytestream2_skip(&s->gb, 3);

    if (!s->frame.data[0]) {
        s->frame.key_frame = 1;
        s->frame.pict_type = AV_PICTURE_TYPE_I;
        s->frame.buffer_hints = FF_BUFFER_HINTS_VALID;
        if (avctx->get_buffer(avctx, &s->frame)) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return -1;
        }
    }

    for (y = 0; y < FFALIGN(avctx->height, 16) >> 4; y++)
        for (x = 0; x < FFALIGN(avctx->width, 16) >> 4; x++)
            tgq_decode_mb(s, y, x);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return avpkt->size;
}

static av_cold int tgq_decode_end(AVCodecContext *avctx){
    TgqContext *s = avctx->priv_data;
    if (s->frame.data[0])
        s->avctx->release_buffer(avctx, &s->frame);
    return 0;
}

AVCodec ff_eatgq_decoder = {
    .name           = "eatgq",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TGQ,
    .priv_data_size = sizeof(TgqContext),
    .init           = tgq_decode_init,
    .close          = tgq_decode_end,
    .decode         = tgq_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Electronic Arts TGQ video"),
};
