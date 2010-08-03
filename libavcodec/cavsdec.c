/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder
 * @author Stefan Gehrer <stefan.gehrer@gmx.de>
 */

#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "cavs.h"

static const uint8_t mv_scan[4] = {
    MV_FWD_X0,MV_FWD_X1,
    MV_FWD_X2,MV_FWD_X3
};

static const uint8_t cbp_tab[64][2] = {
  {63, 0},{15,15},{31,63},{47,31},{ 0,16},{14,32},{13,47},{11,13},
  { 7,14},{ 5,11},{10,12},{ 8, 5},{12,10},{61, 7},{ 4,48},{55, 3},
  { 1, 2},{ 2, 8},{59, 4},{ 3, 1},{62,61},{ 9,55},{ 6,59},{29,62},
  {45,29},{51,27},{23,23},{39,19},{27,30},{46,28},{53, 9},{30, 6},
  {43,60},{37,21},{60,44},{16,26},{21,51},{28,35},{19,18},{35,20},
  {42,24},{26,53},{44,17},{32,37},{58,39},{24,45},{20,58},{17,43},
  {18,42},{48,46},{22,36},{33,33},{25,34},{49,40},{40,52},{36,49},
  {34,50},{50,56},{52,25},{54,22},{41,54},{56,57},{38,41},{57,38}
};

/*****************************************************************************
 *
 * motion vector prediction
 *
 ****************************************************************************/

static inline void store_mvs(AVSContext *h) {
    h->col_mv[h->mbidx*4 + 0] = h->mv[MV_FWD_X0];
    h->col_mv[h->mbidx*4 + 1] = h->mv[MV_FWD_X1];
    h->col_mv[h->mbidx*4 + 2] = h->mv[MV_FWD_X2];
    h->col_mv[h->mbidx*4 + 3] = h->mv[MV_FWD_X3];
}

static inline void mv_pred_direct(AVSContext *h, cavs_vector *pmv_fw,
                                  cavs_vector *col_mv) {
    cavs_vector *pmv_bw = pmv_fw + MV_BWD_OFFS;
    int den = h->direct_den[col_mv->ref];
    int m = col_mv->x >> 31;

    pmv_fw->dist = h->dist[1];
    pmv_bw->dist = h->dist[0];
    pmv_fw->ref = 1;
    pmv_bw->ref = 0;
    /* scale the co-located motion vector according to its temporal span */
    pmv_fw->x = (((den+(den*col_mv->x*pmv_fw->dist^m)-m-1)>>14)^m)-m;
    pmv_bw->x = m-(((den+(den*col_mv->x*pmv_bw->dist^m)-m-1)>>14)^m);
    m = col_mv->y >> 31;
    pmv_fw->y = (((den+(den*col_mv->y*pmv_fw->dist^m)-m-1)>>14)^m)-m;
    pmv_bw->y = m-(((den+(den*col_mv->y*pmv_bw->dist^m)-m-1)>>14)^m);
}

static inline void mv_pred_sym(AVSContext *h, cavs_vector *src, enum cavs_block size) {
    cavs_vector *dst = src + MV_BWD_OFFS;

    /* backward mv is the scaled and negated forward mv */
    dst->x = -((src->x * h->sym_factor + 256) >> 9);
    dst->y = -((src->y * h->sym_factor + 256) >> 9);
    dst->ref = 0;
    dst->dist = h->dist[0];
    set_mvs(dst, size);
}

/*****************************************************************************
 *
 * residual data decoding
 *
 ****************************************************************************/

/** kth-order exponential golomb code */
static inline int get_ue_code(GetBitContext *gb, int order) {
    if(order) {
        int ret = get_ue_golomb(gb) << order;
        return ret + get_bits(gb,order);
    }
    return get_ue_golomb(gb);
}

/**
 * decode coefficients from one 8x8 block, dequantize, inverse transform
 *  and add them to sample block
 * @param r pointer to 2D VLC table
 * @param esc_golomb_order escape codes are k-golomb with this order k
 * @param qp quantizer
 * @param dst location of sample block
 * @param stride line stride in frame buffer
 */
static int decode_residual_block(AVSContext *h, GetBitContext *gb,
                                 const struct dec_2dvlc *r, int esc_golomb_order,
                                 int qp, uint8_t *dst, int stride) {
    int i, level_code, esc_code, level, run, mask;
    DCTELEM level_buf[65];
    uint8_t run_buf[65];
    DCTELEM *block = h->block;

    for(i=0;i<65;i++) {
        level_code = get_ue_code(gb,r->golomb_order);
        if(level_code >= ESCAPE_CODE) {
            run = ((level_code - ESCAPE_CODE) >> 1) + 1;
            esc_code = get_ue_code(gb,esc_golomb_order);
            level = esc_code + (run > r->max_run ? 1 : r->level_add[run]);
            while(level > r->inc_limit)
                r++;
            mask = -(level_code & 1);
            level = (level^mask) - mask;
        } else {
            level = r->rltab[level_code][0];
            if(!level) //end of block signal
                break;
            run   = r->rltab[level_code][1];
            r += r->rltab[level_code][2];
        }
        level_buf[i] = level;
        run_buf[i] = run;
    }
    if(dequant(h,level_buf, run_buf, block, ff_cavs_dequant_mul[qp],
               ff_cavs_dequant_shift[qp], i))
        return -1;
    h->cdsp.cavs_idct8_add(dst,block,stride);
    h->s.dsp.clear_block(block);
    return 0;
}


static inline void decode_residual_chroma(AVSContext *h) {
    if(h->cbp & (1<<4))
        decode_residual_block(h,&h->s.gb,ff_cavs_chroma_dec,0,
                              ff_cavs_chroma_qp[h->qp],h->cu,h->c_stride);
    if(h->cbp & (1<<5))
        decode_residual_block(h,&h->s.gb,ff_cavs_chroma_dec,0,
                              ff_cavs_chroma_qp[h->qp],h->cv,h->c_stride);
}

static inline int decode_residual_inter(AVSContext *h) {
    int block;

    /* get coded block pattern */
    int cbp= get_ue_golomb(&h->s.gb);
    if(cbp > 63){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal inter cbp\n");
        return -1;
    }
    h->cbp = cbp_tab[cbp][1];

    /* get quantizer */
    if(h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(&h->s.gb)) & 63;
    for(block=0;block<4;block++)
        if(h->cbp & (1<<block))
            decode_residual_block(h,&h->s.gb,ff_cavs_inter_dec,0,h->qp,
                                  h->cy + h->luma_scan[block], h->l_stride);
    decode_residual_chroma(h);

    return 0;
}

/*****************************************************************************
 *
 * macroblock level
 *
 ****************************************************************************/

static int decode_mb_i(AVSContext *h, int cbp_code) {
    GetBitContext *gb = &h->s.gb;
    int block, pred_mode_uv;
    uint8_t top[18];
    uint8_t *left = NULL;
    uint8_t *d;

    ff_cavs_init_mb(h);

    /* get intra prediction modes from stream */
    for(block=0;block<4;block++) {
        int nA,nB,predpred;
        int pos = ff_cavs_scan3x3[block];

        nA = h->pred_mode_Y[pos-1];
        nB = h->pred_mode_Y[pos-3];
        predpred = FFMIN(nA,nB);
        if(predpred == NOT_AVAIL) // if either is not available
            predpred = INTRA_L_LP;
        if(!get_bits1(gb)){
            int rem_mode= get_bits(gb, 2);
            predpred = rem_mode + (rem_mode >= predpred);
        }
        h->pred_mode_Y[pos] = predpred;
    }
    pred_mode_uv = get_ue_golomb(gb);
    if(pred_mode_uv > 6) {
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal intra chroma pred mode\n");
        return -1;
    }
    ff_cavs_modify_mb_i(h, &pred_mode_uv);

    /* get coded block pattern */
    if(h->pic_type == FF_I_TYPE)
        cbp_code = get_ue_golomb(gb);
    if(cbp_code > 63){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal intra cbp\n");
        return -1;
    }
    h->cbp = cbp_tab[cbp_code][0];
    if(h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(gb)) & 63; //qp_delta

    /* luma intra prediction interleaved with residual decode/transform/add */
    for(block=0;block<4;block++) {
        d = h->cy + h->luma_scan[block];
        ff_cavs_load_intra_pred_luma(h, top, &left, block);
        h->intra_pred_l[h->pred_mode_Y[ff_cavs_scan3x3[block]]]
            (d, top, left, h->l_stride);
        if(h->cbp & (1<<block))
            decode_residual_block(h,gb,ff_cavs_intra_dec,1,h->qp,d,h->l_stride);
    }

    /* chroma intra prediction */
    ff_cavs_load_intra_pred_chroma(h);
    h->intra_pred_c[pred_mode_uv](h->cu, &h->top_border_u[h->mbx*10],
                                  h->left_border_u, h->c_stride);
    h->intra_pred_c[pred_mode_uv](h->cv, &h->top_border_v[h->mbx*10],
                                  h->left_border_v, h->c_stride);

    decode_residual_chroma(h);
    ff_cavs_filter(h,I_8X8);
    set_mv_intra(h);
    return 0;
}

static void decode_mb_p(AVSContext *h, enum cavs_mb mb_type) {
    GetBitContext *gb = &h->s.gb;
    int ref[4];

    ff_cavs_init_mb(h);
    switch(mb_type) {
    case P_SKIP:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_PSKIP,  BLK_16X16, 0);
        break;
    case P_16X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16,ref[0]);
        break;
    case P_16X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,    BLK_16X8, ref[0]);
        ff_cavs_mv(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT,   BLK_16X8, ref[2]);
        break;
    case P_8X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT,   BLK_8X16, ref[0]);
        ff_cavs_mv(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_TOPRIGHT,BLK_8X16, ref[1]);
        break;
    case P_8X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        ref[3] = h->ref_flag ? 0 : get_bits1(gb);
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_MEDIAN,   BLK_8X8, ref[0]);
        ff_cavs_mv(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_MEDIAN,   BLK_8X8, ref[1]);
        ff_cavs_mv(h, MV_FWD_X2, MV_FWD_X1, MV_PRED_MEDIAN,   BLK_8X8, ref[2]);
        ff_cavs_mv(h, MV_FWD_X3, MV_FWD_X0, MV_PRED_MEDIAN,   BLK_8X8, ref[3]);
    }
    ff_cavs_inter(h, mb_type);
    set_intra_mode_default(h);
    store_mvs(h);
    if(mb_type != P_SKIP)
        decode_residual_inter(h);
    ff_cavs_filter(h,mb_type);
    h->col_type_base[h->mbidx] = mb_type;
}

static void decode_mb_b(AVSContext *h, enum cavs_mb mb_type) {
    int block;
    enum cavs_sub_mb sub_type[4];
    int flags;

    ff_cavs_init_mb(h);

    /* reset all MVs */
    h->mv[MV_FWD_X0] = ff_cavs_dir_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = ff_cavs_dir_mv;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    switch(mb_type) {
    case B_SKIP:
    case B_DIRECT:
        if(!h->col_type_base[h->mbidx]) {
            /* intra MB at co-location, do in-plane prediction */
            ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_BSKIP, BLK_16X16, 1);
            ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_BSKIP, BLK_16X16, 0);
        } else
            /* direct prediction from co-located P MB, block-wise */
            for(block=0;block<4;block++)
                mv_pred_direct(h,&h->mv[mv_scan[block]],
                                 &h->col_mv[h->mbidx*4 + block]);
        break;
    case B_FWD_16X16:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        break;
    case B_SYM_16X16:
        ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X16);
        break;
    case B_BWD_16X16:
        ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_MEDIAN, BLK_16X16, 0);
        break;
    case B_8X8:
        for(block=0;block<4;block++)
            sub_type[block] = get_bits(&h->s.gb,2);
        for(block=0;block<4;block++) {
            switch(sub_type[block]) {
            case B_SUB_DIRECT:
                if(!h->col_type_base[h->mbidx]) {
                    /* intra MB at co-location, do in-plane prediction */
                    ff_cavs_mv(h, mv_scan[block], mv_scan[block]-3,
                            MV_PRED_BSKIP, BLK_8X8, 1);
                    ff_cavs_mv(h, mv_scan[block]+MV_BWD_OFFS,
                            mv_scan[block]-3+MV_BWD_OFFS,
                            MV_PRED_BSKIP, BLK_8X8, 0);
                } else
                    mv_pred_direct(h,&h->mv[mv_scan[block]],
                                   &h->col_mv[h->mbidx*4 + block]);
                break;
            case B_SUB_FWD:
                ff_cavs_mv(h, mv_scan[block], mv_scan[block]-3,
                        MV_PRED_MEDIAN, BLK_8X8, 1);
                break;
            case B_SUB_SYM:
                ff_cavs_mv(h, mv_scan[block], mv_scan[block]-3,
                        MV_PRED_MEDIAN, BLK_8X8, 1);
                mv_pred_sym(h, &h->mv[mv_scan[block]], BLK_8X8);
                break;
            }
        }
        for(block=0;block<4;block++) {
            if(sub_type[block] == B_SUB_BWD)
                ff_cavs_mv(h, mv_scan[block]+MV_BWD_OFFS,
                        mv_scan[block]+MV_BWD_OFFS-3,
                        MV_PRED_MEDIAN, BLK_8X8, 0);
        }
        break;
    default:
        assert((mb_type > B_SYM_16X16) && (mb_type < B_8X8));
        flags = ff_cavs_partition_flags[mb_type];
        if(mb_type & 1) { /* 16x8 macroblock types */
            if(flags & FWD0)
                ff_cavs_mv(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,  BLK_16X8, 1);
            if(flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X8);
            if(flags & FWD1)
                ff_cavs_mv(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT, BLK_16X8, 1);
            if(flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X2], BLK_16X8);
            if(flags & BWD0)
                ff_cavs_mv(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_TOP,  BLK_16X8, 0);
            if(flags & BWD1)
                ff_cavs_mv(h, MV_BWD_X2, MV_BWD_A1, MV_PRED_LEFT, BLK_16X8, 0);
        } else {          /* 8x16 macroblock types */
            if(flags & FWD0)
                ff_cavs_mv(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT, BLK_8X16, 1);
            if(flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_8X16);
            if(flags & FWD1)
                ff_cavs_mv(h,MV_FWD_X1,MV_FWD_C2,MV_PRED_TOPRIGHT,BLK_8X16,1);
            if(flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X1], BLK_8X16);
            if(flags & BWD0)
                ff_cavs_mv(h, MV_BWD_X0, MV_BWD_B3, MV_PRED_LEFT, BLK_8X16, 0);
            if(flags & BWD1)
                ff_cavs_mv(h,MV_BWD_X1,MV_BWD_C2,MV_PRED_TOPRIGHT,BLK_8X16,0);
        }
    }
    ff_cavs_inter(h, mb_type);
    set_intra_mode_default(h);
    if(mb_type != B_SKIP)
        decode_residual_inter(h);
    ff_cavs_filter(h,mb_type);
}

/*****************************************************************************
 *
 * slice level
 *
 ****************************************************************************/

static inline int decode_slice_header(AVSContext *h, GetBitContext *gb) {
    if(h->stc > 0xAF)
        av_log(h->s.avctx, AV_LOG_ERROR, "unexpected start code 0x%02x\n", h->stc);
    h->mby = h->stc;
    h->mbidx = h->mby*h->mb_width;

    /* mark top macroblocks as unavailable */
    h->flags &= ~(B_AVAIL|C_AVAIL);
    if((h->mby == 0) && (!h->qp_fixed)){
        h->qp_fixed = get_bits1(gb);
        h->qp = get_bits(gb,6);
    }
    /* inter frame or second slice can have weighting params */
    if((h->pic_type != FF_I_TYPE) || (!h->pic_structure && h->mby >= h->mb_width/2))
        if(get_bits1(gb)) { //slice_weighting_flag
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "weighted prediction not yet supported\n");
        }
    return 0;
}

static inline int check_for_slice(AVSContext *h) {
    GetBitContext *gb = &h->s.gb;
    int align;

    if(h->mbx)
        return 0;
    align = (-get_bits_count(gb)) & 7;
    /* check for stuffing byte */
    if(!align && (show_bits(gb,8) == 0x80))
        align = 8;
    if((show_bits_long(gb,24+align) & 0xFFFFFF) == 0x000001) {
        skip_bits_long(gb,24+align);
        h->stc = get_bits(gb,8);
        decode_slice_header(h,gb);
        return 1;
    }
    return 0;
}

/*****************************************************************************
 *
 * frame level
 *
 ****************************************************************************/

static int decode_pic(AVSContext *h) {
    MpegEncContext *s = &h->s;
    int skip_count = -1;
    enum cavs_mb mb_type;

    if (!s->context_initialized) {
        s->avctx->idct_algo = FF_IDCT_CAVS;
        if (MPV_common_init(s) < 0)
            return -1;
        ff_init_scantable(s->dsp.idct_permutation,&h->scantable,ff_zigzag_direct);
    }
    skip_bits(&s->gb,16);//bbv_dwlay
    if(h->stc == PIC_PB_START_CODE) {
        h->pic_type = get_bits(&s->gb,2) + FF_I_TYPE;
        if(h->pic_type > FF_B_TYPE) {
            av_log(s->avctx, AV_LOG_ERROR, "illegal picture type\n");
            return -1;
        }
        /* make sure we have the reference frames we need */
        if(!h->DPB[0].data[0] ||
          (!h->DPB[1].data[0] && h->pic_type == FF_B_TYPE))
            return -1;
    } else {
        h->pic_type = FF_I_TYPE;
        if(get_bits1(&s->gb))
            skip_bits(&s->gb,24);//time_code
        /* old sample clips were all progressive and no low_delay,
           bump stream revision if detected otherwise */
        if((s->low_delay) || !(show_bits(&s->gb,9) & 1))
            h->stream_revision = 1;
        /* similarly test top_field_first and repeat_first_field */
        else if(show_bits(&s->gb,11) & 3)
            h->stream_revision = 1;
        if(h->stream_revision > 0)
            skip_bits(&s->gb,1); //marker_bit
    }
    /* release last B frame */
    if(h->picture.data[0])
        s->avctx->release_buffer(s->avctx, (AVFrame *)&h->picture);

    s->avctx->get_buffer(s->avctx, (AVFrame *)&h->picture);
    ff_cavs_init_pic(h);
    h->picture.poc = get_bits(&s->gb,8)*2;

    /* get temporal distances and MV scaling factors */
    if(h->pic_type != FF_B_TYPE) {
        h->dist[0] = (h->picture.poc - h->DPB[0].poc  + 512) % 512;
    } else {
        h->dist[0] = (h->DPB[0].poc  - h->picture.poc + 512) % 512;
    }
    h->dist[1] = (h->picture.poc - h->DPB[1].poc  + 512) % 512;
    h->scale_den[0] = h->dist[0] ? 512/h->dist[0] : 0;
    h->scale_den[1] = h->dist[1] ? 512/h->dist[1] : 0;
    if(h->pic_type == FF_B_TYPE) {
        h->sym_factor = h->dist[0]*h->scale_den[1];
    } else {
        h->direct_den[0] = h->dist[0] ? 16384/h->dist[0] : 0;
        h->direct_den[1] = h->dist[1] ? 16384/h->dist[1] : 0;
    }

    if(s->low_delay)
        get_ue_golomb(&s->gb); //bbv_check_times
    h->progressive             = get_bits1(&s->gb);
    h->pic_structure = 1;
    if(!h->progressive)
        h->pic_structure = get_bits1(&s->gb);
    if(!h->pic_structure && h->stc == PIC_PB_START_CODE)
        skip_bits1(&s->gb);     //advanced_pred_mode_disable
    skip_bits1(&s->gb);        //top_field_first
    skip_bits1(&s->gb);        //repeat_first_field
    h->qp_fixed                = get_bits1(&s->gb);
    h->qp                      = get_bits(&s->gb,6);
    if(h->pic_type == FF_I_TYPE) {
        if(!h->progressive && !h->pic_structure)
            skip_bits1(&s->gb);//what is this?
        skip_bits(&s->gb,4);   //reserved bits
    } else {
        if(!(h->pic_type == FF_B_TYPE && h->pic_structure == 1))
            h->ref_flag        = get_bits1(&s->gb);
        skip_bits(&s->gb,4);   //reserved bits
        h->skip_mode_flag      = get_bits1(&s->gb);
    }
    h->loop_filter_disable     = get_bits1(&s->gb);
    if(!h->loop_filter_disable && get_bits1(&s->gb)) {
        h->alpha_offset        = get_se_golomb(&s->gb);
        h->beta_offset         = get_se_golomb(&s->gb);
    } else {
        h->alpha_offset = h->beta_offset  = 0;
    }
    if(h->pic_type == FF_I_TYPE) {
        do {
            check_for_slice(h);
            decode_mb_i(h, 0);
        } while(ff_cavs_next_mb(h));
    } else if(h->pic_type == FF_P_TYPE) {
        do {
            if(check_for_slice(h))
                skip_count = -1;
            if(h->skip_mode_flag && (skip_count < 0))
                skip_count = get_ue_golomb(&s->gb);
            if(h->skip_mode_flag && skip_count--) {
                decode_mb_p(h,P_SKIP);
            } else {
                mb_type = get_ue_golomb(&s->gb) + P_SKIP + h->skip_mode_flag;
                if(mb_type > P_8X8)
                    decode_mb_i(h, mb_type - P_8X8 - 1);
                else
                    decode_mb_p(h,mb_type);
            }
        } while(ff_cavs_next_mb(h));
    } else { /* FF_B_TYPE */
        do {
            if(check_for_slice(h))
                skip_count = -1;
            if(h->skip_mode_flag && (skip_count < 0))
                skip_count = get_ue_golomb(&s->gb);
            if(h->skip_mode_flag && skip_count--) {
                decode_mb_b(h,B_SKIP);
            } else {
                mb_type = get_ue_golomb(&s->gb) + B_SKIP + h->skip_mode_flag;
                if(mb_type > B_8X8)
                    decode_mb_i(h, mb_type - B_8X8 - 1);
                else
                    decode_mb_b(h,mb_type);
            }
        } while(ff_cavs_next_mb(h));
    }
    if(h->pic_type != FF_B_TYPE) {
        if(h->DPB[1].data[0])
            s->avctx->release_buffer(s->avctx, (AVFrame *)&h->DPB[1]);
        h->DPB[1] = h->DPB[0];
        h->DPB[0] = h->picture;
        memset(&h->picture,0,sizeof(Picture));
    }
    return 0;
}

/*****************************************************************************
 *
 * headers and interface
 *
 ****************************************************************************/

static int decode_seq_header(AVSContext *h) {
    MpegEncContext *s = &h->s;
    int frame_rate_code;

    h->profile =         get_bits(&s->gb,8);
    h->level =           get_bits(&s->gb,8);
    skip_bits1(&s->gb); //progressive sequence
    s->width =           get_bits(&s->gb,14);
    s->height =          get_bits(&s->gb,14);
    skip_bits(&s->gb,2); //chroma format
    skip_bits(&s->gb,3); //sample_precision
    h->aspect_ratio =    get_bits(&s->gb,4);
    frame_rate_code =    get_bits(&s->gb,4);
    skip_bits(&s->gb,18);//bit_rate_lower
    skip_bits1(&s->gb);  //marker_bit
    skip_bits(&s->gb,12);//bit_rate_upper
    s->low_delay =       get_bits1(&s->gb);
    h->mb_width  = (s->width  + 15) >> 4;
    h->mb_height = (s->height + 15) >> 4;
    h->s.avctx->time_base.den = ff_frame_rate_tab[frame_rate_code].num;
    h->s.avctx->time_base.num = ff_frame_rate_tab[frame_rate_code].den;
    h->s.avctx->width  = s->width;
    h->s.avctx->height = s->height;
    if(!h->top_qp)
        ff_cavs_init_top_lines(h);
    return 0;
}

static void cavs_flush(AVCodecContext * avctx) {
    AVSContext *h = avctx->priv_data;
    h->got_keyframe = 0;
}

static int cavs_decode_frame(AVCodecContext * avctx,void *data, int *data_size,
                             AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVSContext *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    int input_size;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;
    AVFrame *picture = data;
    uint32_t stc = -1;

    s->avctx = avctx;

    if (buf_size == 0) {
        if(!s->low_delay && h->DPB[0].data[0]) {
            *data_size = sizeof(AVPicture);
            *picture = *(AVFrame *) &h->DPB[0];
        }
        return 0;
    }

    buf_ptr = buf;
    buf_end = buf + buf_size;
    for(;;) {
        buf_ptr = ff_find_start_code(buf_ptr,buf_end, &stc);
        if(stc & 0xFFFFFE00)
            return FFMAX(0, buf_ptr - buf - s->parse_context.last_index);
        input_size = (buf_end - buf_ptr)*8;
        switch(stc) {
        case CAVS_START_CODE:
            init_get_bits(&s->gb, buf_ptr, input_size);
            decode_seq_header(h);
            break;
        case PIC_I_START_CODE:
            if(!h->got_keyframe) {
                if(h->DPB[0].data[0])
                    avctx->release_buffer(avctx, (AVFrame *)&h->DPB[0]);
                if(h->DPB[1].data[0])
                    avctx->release_buffer(avctx, (AVFrame *)&h->DPB[1]);
                h->got_keyframe = 1;
            }
        case PIC_PB_START_CODE:
            *data_size = 0;
            if(!h->got_keyframe)
                break;
            init_get_bits(&s->gb, buf_ptr, input_size);
            h->stc = stc;
            if(decode_pic(h))
                break;
            *data_size = sizeof(AVPicture);
            if(h->pic_type != FF_B_TYPE) {
                if(h->DPB[1].data[0]) {
                    *picture = *(AVFrame *) &h->DPB[1];
                } else {
                    *data_size = 0;
                }
            } else
                *picture = *(AVFrame *) &h->picture;
            break;
        case EXT_START_CODE:
            //mpeg_decode_extension(avctx,buf_ptr, input_size);
            break;
        case USER_START_CODE:
            //mpeg_decode_user_data(avctx,buf_ptr, input_size);
            break;
        default:
            if (stc <= SLICE_MAX_START_CODE) {
                init_get_bits(&s->gb, buf_ptr, input_size);
                decode_slice_header(h, &s->gb);
            }
            break;
        }
    }
}

AVCodec cavs_decoder = {
    "cavs",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_CAVS,
    sizeof(AVSContext),
    ff_cavs_init,
    NULL,
    ff_cavs_end,
    cavs_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .flush= cavs_flush,
    .long_name= NULL_IF_CONFIG_SMALL("Chinese AVS video (AVS1-P2, JiZhun profile)"),
};
