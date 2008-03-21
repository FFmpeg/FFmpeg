/*
 * RV30 decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * @file rv30.c
 * RV30 decoder
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "golomb.h"

#include "rv34.h"
#include "rv30data.h"


static int rv30_parse_slice_header(RV34DecContext *r, GetBitContext *gb, SliceInfo *si)
{
    int mb_bits;
    int w = r->s.width, h = r->s.height;
    int mb_size;

    memset(si, 0, sizeof(SliceInfo));
    skip_bits(gb, 3);
    si->type = get_bits(gb, 2);
    if(si->type == 1) si->type = 0;
    if(get_bits1(gb))
        return -1;
    si->quant = get_bits(gb, 5);
    skip_bits1(gb);
    skip_bits(gb, 13); // timestamp
    skip_bits(gb, r->rpr);
    si->width  = w;
    si->height = h;
    mb_size = ((w + 15) >> 4) * ((h + 15) >> 4);
    mb_bits = ff_rv34_get_start_offset(gb, mb_size);
    si->start = get_bits(gb, mb_bits);
    skip_bits1(gb);
    return 0;
}

/**
 * Decode 4x4 intra types array.
 */
static int rv30_decode_intra_types(RV34DecContext *r, GetBitContext *gb, int8_t *dst)
{
    int i, j, k;

    for(i = 0; i < 4; i++, dst += r->s.b4_stride - 4){
        for(j = 0; j < 4; j+= 2){
            int code = svq3_get_ue_golomb(gb) << 1;
            if(code >= 81*2){
                av_log(r->s.avctx, AV_LOG_ERROR, "Incorrect intra prediction code\n");
                return -1;
            }
            for(k = 0; k < 2; k++){
                int A = dst[-r->s.b4_stride] + 1;
                int B = dst[-1] + 1;
                *dst++ = rv30_itype_from_context[A * 90 + B * 9 + rv30_itype_code[code + k]];
                if(dst[-1] == 9){
                    av_log(r->s.avctx, AV_LOG_ERROR, "Incorrect intra prediction mode\n");
                    return -1;
                }
            }
        }
    }
    return 0;
}

/**
 * Decode macroblock information.
 */
static int rv30_decode_mb_info(RV34DecContext *r)
{
    static const int rv30_p_types[6] = { RV34_MB_SKIP, RV34_MB_P_16x16, RV34_MB_P_8x8, -1, RV34_MB_TYPE_INTRA, RV34_MB_TYPE_INTRA16x16 };
    static const int rv30_b_types[6] = { RV34_MB_SKIP, RV34_MB_B_DIRECT, RV34_MB_B_FORWARD, RV34_MB_B_BACKWARD, RV34_MB_TYPE_INTRA, RV34_MB_TYPE_INTRA16x16 };
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int code = svq3_get_ue_golomb(gb);

    if(code > 11){
        av_log(s->avctx, AV_LOG_ERROR, "Incorrect MB type code\n");
        return -1;
    }
    if(code > 5){
        av_log(s->avctx, AV_LOG_ERROR, "dquant needed\n");
        code -= 6;
    }
    if(s->pict_type != FF_B_TYPE)
        return rv30_p_types[code];
    else
        return rv30_b_types[code];
}

/**
 * Initialize decoder.
 */
static av_cold int rv30_decode_init(AVCodecContext *avctx)
{
    RV34DecContext *r = avctx->priv_data;

    r->rv30 = 1;
    ff_rv34_decode_init(avctx);
    if(avctx->extradata_size < 2){
        av_log(avctx, AV_LOG_ERROR, "Extradata is too small.\n");
        return -1;
    }
    r->rpr = (avctx->extradata[1] & 7) >> 1;
    r->rpr = FFMIN(r->rpr + 1, 3);
    r->parse_slice_header = rv30_parse_slice_header;
    r->decode_intra_types = rv30_decode_intra_types;
    r->decode_mb_info     = rv30_decode_mb_info;
    r->luma_dc_quant_i = rv30_luma_dc_quant;
    r->luma_dc_quant_p = rv30_luma_dc_quant;
    return 0;
}

AVCodec rv30_decoder = {
    "rv30",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RV30,
    sizeof(RV34DecContext),
    rv30_decode_init,
    NULL,
    ff_rv34_decode_end,
    ff_rv34_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
};
