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
 * @file
 * RV30 decoder
 */

#include "avcodec.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "golomb.h"

#include "rv34.h"
#include "rv30data.h"


static int rv30_parse_slice_header(RV34DecContext *r, GetBitContext *gb, SliceInfo *si)
{
    AVCodecContext *avctx = r->s.avctx;
    int mb_bits;
    int w = r->s.width, h = r->s.height;
    int mb_size;
    int rpr;

    memset(si, 0, sizeof(SliceInfo));
    if(get_bits(gb, 3))
        return -1;
    si->type = get_bits(gb, 2);
    if(si->type == 1) si->type = 0;
    if(get_bits1(gb))
        return -1;
    si->quant = get_bits(gb, 5);
    skip_bits1(gb);
    si->pts = get_bits(gb, 13);
    rpr = get_bits(gb, av_log2(r->max_rpr) + 1);
    if(rpr){
        if (rpr > r->max_rpr) {
            av_log(avctx, AV_LOG_ERROR, "rpr too large\n");
            return AVERROR_INVALIDDATA;
        }

        if (avctx->extradata_size < rpr * 2 + 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "Insufficient extradata - need at least %d bytes, got %d\n",
                   8 + rpr * 2, avctx->extradata_size);
            return AVERROR(EINVAL);
        }

        w = r->s.avctx->extradata[6 + rpr*2] << 2;
        h = r->s.avctx->extradata[7 + rpr*2] << 2;
    } else {
        w = r->orig_width;
        h = r->orig_height;
    }
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

    for(i = 0; i < 4; i++, dst += r->intra_types_stride - 4){
        for(j = 0; j < 4; j+= 2){
            unsigned code = svq3_get_ue_golomb(gb) << 1;
            if (code > 80U*2U) {
                av_log(r->s.avctx, AV_LOG_ERROR, "Incorrect intra prediction code\n");
                return -1;
            }
            for(k = 0; k < 2; k++){
                int A = dst[-r->intra_types_stride] + 1;
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
    unsigned code     = svq3_get_ue_golomb(gb);

    if (code > 11) {
        av_log(s->avctx, AV_LOG_ERROR, "Incorrect MB type code\n");
        return -1;
    }
    if(code > 5){
        av_log(s->avctx, AV_LOG_ERROR, "dquant needed\n");
        code -= 6;
    }
    if(s->pict_type != AV_PICTURE_TYPE_B)
        return rv30_p_types[code];
    else
        return rv30_b_types[code];
}

static inline void rv30_weak_loop_filter(uint8_t *src, const int step,
                                         const int stride, const int lim)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i, diff;

    for(i = 0; i < 4; i++){
        diff = ((src[-2*step] - src[1*step]) - (src[-1*step] - src[0*step])*4) >> 3;
        diff = av_clip(diff, -lim, lim);
        src[-1*step] = cm[src[-1*step] + diff];
        src[ 0*step] = cm[src[ 0*step] - diff];
        src += stride;
    }
}

static void rv30_loop_filter(RV34DecContext *r, int row)
{
    MpegEncContext *s = &r->s;
    int mb_pos, mb_x;
    int i, j, k;
    uint8_t *Y, *C;
    int loc_lim, cur_lim, left_lim = 0, top_lim = 0;

    mb_pos = row * s->mb_stride;
    for(mb_x = 0; mb_x < s->mb_width; mb_x++, mb_pos++){
        int mbtype = s->current_picture_ptr->mb_type[mb_pos];
        if(IS_INTRA(mbtype) || IS_SEPARATE_DC(mbtype))
            r->deblock_coefs[mb_pos] = 0xFFFF;
        if(IS_INTRA(mbtype))
            r->cbp_chroma[mb_pos] = 0xFF;
    }

    /* all vertical edges are filtered first
     * and horizontal edges are filtered on the next iteration
     */
    mb_pos = row * s->mb_stride;
    for(mb_x = 0; mb_x < s->mb_width; mb_x++, mb_pos++){
        cur_lim = rv30_loop_filt_lim[s->current_picture_ptr->qscale_table[mb_pos]];
        if(mb_x)
            left_lim = rv30_loop_filt_lim[s->current_picture_ptr->qscale_table[mb_pos - 1]];
        for(j = 0; j < 16; j += 4){
            Y = s->current_picture_ptr->f->data[0] + mb_x*16 + (row*16 + j) * s->linesize + 4 * !mb_x;
            for(i = !mb_x; i < 4; i++, Y += 4){
                int ij = i + j;
                loc_lim = 0;
                if(r->deblock_coefs[mb_pos] & (1 << ij))
                    loc_lim = cur_lim;
                else if(!i && r->deblock_coefs[mb_pos - 1] & (1 << (ij + 3)))
                    loc_lim = left_lim;
                else if( i && r->deblock_coefs[mb_pos]     & (1 << (ij - 1)))
                    loc_lim = cur_lim;
                if(loc_lim)
                    rv30_weak_loop_filter(Y, 1, s->linesize, loc_lim);
            }
        }
        for(k = 0; k < 2; k++){
            int cur_cbp, left_cbp = 0;
            cur_cbp = (r->cbp_chroma[mb_pos] >> (k*4)) & 0xF;
            if(mb_x)
                left_cbp = (r->cbp_chroma[mb_pos - 1] >> (k*4)) & 0xF;
            for(j = 0; j < 8; j += 4){
                C = s->current_picture_ptr->f->data[k + 1] + mb_x*8 + (row*8 + j) * s->uvlinesize + 4 * !mb_x;
                for(i = !mb_x; i < 2; i++, C += 4){
                    int ij = i + (j >> 1);
                    loc_lim = 0;
                    if (cur_cbp & (1 << ij))
                        loc_lim = cur_lim;
                    else if(!i && left_cbp & (1 << (ij + 1)))
                        loc_lim = left_lim;
                    else if( i && cur_cbp  & (1 << (ij - 1)))
                        loc_lim = cur_lim;
                    if(loc_lim)
                        rv30_weak_loop_filter(C, 1, s->uvlinesize, loc_lim);
                }
            }
        }
    }
    mb_pos = row * s->mb_stride;
    for(mb_x = 0; mb_x < s->mb_width; mb_x++, mb_pos++){
        cur_lim = rv30_loop_filt_lim[s->current_picture_ptr->qscale_table[mb_pos]];
        if(row)
            top_lim = rv30_loop_filt_lim[s->current_picture_ptr->qscale_table[mb_pos - s->mb_stride]];
        for(j = 4*!row; j < 16; j += 4){
            Y = s->current_picture_ptr->f->data[0] + mb_x*16 + (row*16 + j) * s->linesize;
            for(i = 0; i < 4; i++, Y += 4){
                int ij = i + j;
                loc_lim = 0;
                if(r->deblock_coefs[mb_pos] & (1 << ij))
                    loc_lim = cur_lim;
                else if(!j && r->deblock_coefs[mb_pos - s->mb_stride] & (1 << (ij + 12)))
                    loc_lim = top_lim;
                else if( j && r->deblock_coefs[mb_pos]                & (1 << (ij - 4)))
                    loc_lim = cur_lim;
                if(loc_lim)
                    rv30_weak_loop_filter(Y, s->linesize, 1, loc_lim);
            }
        }
        for(k = 0; k < 2; k++){
            int cur_cbp, top_cbp = 0;
            cur_cbp = (r->cbp_chroma[mb_pos] >> (k*4)) & 0xF;
            if(row)
                top_cbp = (r->cbp_chroma[mb_pos - s->mb_stride] >> (k*4)) & 0xF;
            for(j = 4*!row; j < 8; j += 4){
                C = s->current_picture_ptr->f->data[k+1] + mb_x*8 + (row*8 + j) * s->uvlinesize;
                for(i = 0; i < 2; i++, C += 4){
                    int ij = i + (j >> 1);
                    loc_lim = 0;
                    if (r->cbp_chroma[mb_pos] & (1 << ij))
                        loc_lim = cur_lim;
                    else if(!j && top_cbp & (1 << (ij + 2)))
                        loc_lim = top_lim;
                    else if( j && cur_cbp & (1 << (ij - 2)))
                        loc_lim = cur_lim;
                    if(loc_lim)
                        rv30_weak_loop_filter(C, s->uvlinesize, 1, loc_lim);
                }
            }
        }
    }
}

/**
 * Initialize decoder.
 */
static av_cold int rv30_decode_init(AVCodecContext *avctx)
{
    RV34DecContext *r = avctx->priv_data;
    int ret;

    r->orig_width  = avctx->coded_width;
    r->orig_height = avctx->coded_height;

    if (avctx->extradata_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "Extradata is too small.\n");
        return AVERROR(EINVAL);
    }
    r->rv30 = 1;
    if ((ret = ff_rv34_decode_init(avctx)) < 0)
        return ret;

    r->max_rpr = avctx->extradata[1] & 7;
    if(avctx->extradata_size < 2*r->max_rpr + 8){
        av_log(avctx, AV_LOG_WARNING, "Insufficient extradata - need at least %d bytes, got %d\n",
               2*r->max_rpr + 8, avctx->extradata_size);
    }

    r->parse_slice_header = rv30_parse_slice_header;
    r->decode_intra_types = rv30_decode_intra_types;
    r->decode_mb_info     = rv30_decode_mb_info;
    r->loop_filter        = rv30_loop_filter;
    r->luma_dc_quant_i = rv30_luma_dc_quant;
    r->luma_dc_quant_p = rv30_luma_dc_quant;
    return 0;
}

AVCodec ff_rv30_decoder = {
    .name                  = "rv30",
    .long_name             = NULL_IF_CONFIG_SMALL("RealVideo 3.0"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_RV30,
    .priv_data_size        = sizeof(RV34DecContext),
    .init                  = rv30_decode_init,
    .close                 = ff_rv34_decode_end,
    .decode                = ff_rv34_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                             AV_CODEC_CAP_FRAME_THREADS,
    .flush                 = ff_mpeg_flush,
    .pix_fmts              = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(ff_rv34_decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(ff_rv34_decode_update_thread_context),
};
