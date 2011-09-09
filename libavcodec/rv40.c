/*
 * RV40 decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * RV40 decoder
 */

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "golomb.h"

#include "rv34.h"
#include "rv40vlc2.h"
#include "rv40data.h"

static VLC aic_top_vlc;
static VLC aic_mode1_vlc[AIC_MODE1_NUM], aic_mode2_vlc[AIC_MODE2_NUM];
static VLC ptype_vlc[NUM_PTYPE_VLCS], btype_vlc[NUM_BTYPE_VLCS];

static const int16_t mode2_offs[] = {
       0,  614, 1222, 1794, 2410,  3014,  3586,  4202,  4792, 5382, 5966, 6542,
    7138, 7716, 8292, 8864, 9444, 10030, 10642, 11212, 11814
};

/**
 * Initialize all tables.
 */
static av_cold void rv40_init_tables(void)
{
    int i;
    static VLC_TYPE aic_table[1 << AIC_TOP_BITS][2];
    static VLC_TYPE aic_mode1_table[AIC_MODE1_NUM << AIC_MODE1_BITS][2];
    static VLC_TYPE aic_mode2_table[11814][2];
    static VLC_TYPE ptype_table[NUM_PTYPE_VLCS << PTYPE_VLC_BITS][2];
    static VLC_TYPE btype_table[NUM_BTYPE_VLCS << BTYPE_VLC_BITS][2];

    aic_top_vlc.table = aic_table;
    aic_top_vlc.table_allocated = 1 << AIC_TOP_BITS;
    init_vlc(&aic_top_vlc, AIC_TOP_BITS, AIC_TOP_SIZE,
             rv40_aic_top_vlc_bits,  1, 1,
             rv40_aic_top_vlc_codes, 1, 1, INIT_VLC_USE_NEW_STATIC);
    for(i = 0; i < AIC_MODE1_NUM; i++){
        // Every tenth VLC table is empty
        if((i % 10) == 9) continue;
        aic_mode1_vlc[i].table = &aic_mode1_table[i << AIC_MODE1_BITS];
        aic_mode1_vlc[i].table_allocated = 1 << AIC_MODE1_BITS;
        init_vlc(&aic_mode1_vlc[i], AIC_MODE1_BITS, AIC_MODE1_SIZE,
                 aic_mode1_vlc_bits[i],  1, 1,
                 aic_mode1_vlc_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
    }
    for(i = 0; i < AIC_MODE2_NUM; i++){
        aic_mode2_vlc[i].table = &aic_mode2_table[mode2_offs[i]];
        aic_mode2_vlc[i].table_allocated = mode2_offs[i + 1] - mode2_offs[i];
        init_vlc(&aic_mode2_vlc[i], AIC_MODE2_BITS, AIC_MODE2_SIZE,
                 aic_mode2_vlc_bits[i],  1, 1,
                 aic_mode2_vlc_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
    }
    for(i = 0; i < NUM_PTYPE_VLCS; i++){
        ptype_vlc[i].table = &ptype_table[i << PTYPE_VLC_BITS];
        ptype_vlc[i].table_allocated = 1 << PTYPE_VLC_BITS;
        init_vlc_sparse(&ptype_vlc[i], PTYPE_VLC_BITS, PTYPE_VLC_SIZE,
                         ptype_vlc_bits[i],  1, 1,
                         ptype_vlc_codes[i], 1, 1,
                         ptype_vlc_syms,     1, 1, INIT_VLC_USE_NEW_STATIC);
    }
    for(i = 0; i < NUM_BTYPE_VLCS; i++){
        btype_vlc[i].table = &btype_table[i << BTYPE_VLC_BITS];
        btype_vlc[i].table_allocated = 1 << BTYPE_VLC_BITS;
        init_vlc_sparse(&btype_vlc[i], BTYPE_VLC_BITS, BTYPE_VLC_SIZE,
                         btype_vlc_bits[i],  1, 1,
                         btype_vlc_codes[i], 1, 1,
                         btype_vlc_syms,     1, 1, INIT_VLC_USE_NEW_STATIC);
    }
}

/**
 * Get stored dimension from bitstream.
 *
 * If the width/height is the standard one then it's coded as a 3-bit index.
 * Otherwise it is coded as escaped 8-bit portions.
 */
static int get_dimension(GetBitContext *gb, const int *dim)
{
    int t   = get_bits(gb, 3);
    int val = dim[t];
    if(val < 0)
        val = dim[get_bits1(gb) - val];
    if(!val){
        do{
            t = get_bits(gb, 8);
            val += t << 2;
        }while(t == 0xFF);
    }
    return val;
}

/**
 * Get encoded picture size - usually this is called from rv40_parse_slice_header.
 */
static void rv40_parse_picture_size(GetBitContext *gb, int *w, int *h)
{
    *w = get_dimension(gb, rv40_standard_widths);
    *h = get_dimension(gb, rv40_standard_heights);
}

static int rv40_parse_slice_header(RV34DecContext *r, GetBitContext *gb, SliceInfo *si)
{
    int mb_bits;
    int w = r->s.width, h = r->s.height;
    int mb_size;

    memset(si, 0, sizeof(SliceInfo));
    if(get_bits1(gb))
        return -1;
    si->type = get_bits(gb, 2);
    if(si->type == 1) si->type = 0;
    si->quant = get_bits(gb, 5);
    if(get_bits(gb, 2))
        return -1;
    si->vlc_set = get_bits(gb, 2);
    skip_bits1(gb);
    si->pts = get_bits(gb, 13);
    if(!si->type || !get_bits1(gb))
        rv40_parse_picture_size(gb, &w, &h);
    if(av_image_check_size(w, h, 0, r->s.avctx) < 0)
        return -1;
    si->width  = w;
    si->height = h;
    mb_size = ((w + 15) >> 4) * ((h + 15) >> 4);
    mb_bits = ff_rv34_get_start_offset(gb, mb_size);
    si->start = get_bits(gb, mb_bits);

    return 0;
}

/**
 * Decode 4x4 intra types array.
 */
static int rv40_decode_intra_types(RV34DecContext *r, GetBitContext *gb, int8_t *dst)
{
    MpegEncContext *s = &r->s;
    int i, j, k, v;
    int A, B, C;
    int pattern;
    int8_t *ptr;

    for(i = 0; i < 4; i++, dst += r->intra_types_stride){
        if(!i && s->first_slice_line){
            pattern = get_vlc2(gb, aic_top_vlc.table, AIC_TOP_BITS, 1);
            dst[0] = (pattern >> 2) & 2;
            dst[1] = (pattern >> 1) & 2;
            dst[2] =  pattern       & 2;
            dst[3] = (pattern << 1) & 2;
            continue;
        }
        ptr = dst;
        for(j = 0; j < 4; j++){
            /* Coefficients are read using VLC chosen by the prediction pattern
             * The first one (used for retrieving a pair of coefficients) is
             * constructed from the top, top right and left coefficients
             * The second one (used for retrieving only one coefficient) is
             * top + 10 * left.
             */
            A = ptr[-r->intra_types_stride + 1]; // it won't be used for the last coefficient in a row
            B = ptr[-r->intra_types_stride];
            C = ptr[-1];
            pattern = A + (B << 4) + (C << 8);
            for(k = 0; k < MODE2_PATTERNS_NUM; k++)
                if(pattern == rv40_aic_table_index[k])
                    break;
            if(j < 3 && k < MODE2_PATTERNS_NUM){ //pattern is found, decoding 2 coefficients
                v = get_vlc2(gb, aic_mode2_vlc[k].table, AIC_MODE2_BITS, 2);
                *ptr++ = v/9;
                *ptr++ = v%9;
                j++;
            }else{
                if(B != -1 && C != -1)
                    v = get_vlc2(gb, aic_mode1_vlc[B + C*10].table, AIC_MODE1_BITS, 1);
                else{ // tricky decoding
                    v = 0;
                    switch(C){
                    case -1: // code 0 -> 1, 1 -> 0
                        if(B < 2)
                            v = get_bits1(gb) ^ 1;
                        break;
                    case  0:
                    case  2: // code 0 -> 2, 1 -> 0
                        v = (get_bits1(gb) ^ 1) << 1;
                        break;
                    }
                }
                *ptr++ = v;
            }
        }
    }
    return 0;
}

/**
 * Decode macroblock information.
 */
static int rv40_decode_mb_info(RV34DecContext *r)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int q, i;
    int prev_type = 0;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int blocks[RV34_MB_TYPES] = {0};
    int count = 0;

    if(!r->s.mb_skip_run)
        r->s.mb_skip_run = svq3_get_ue_golomb(gb) + 1;

    if(--r->s.mb_skip_run)
         return RV34_MB_SKIP;

    if(r->avail_cache[6-1])
        blocks[r->mb_type[mb_pos - 1]]++;
    if(r->avail_cache[6-4]){
        blocks[r->mb_type[mb_pos - s->mb_stride]]++;
        if(r->avail_cache[6-2])
            blocks[r->mb_type[mb_pos - s->mb_stride + 1]]++;
        if(r->avail_cache[6-5])
            blocks[r->mb_type[mb_pos - s->mb_stride - 1]]++;
    }

    for(i = 0; i < RV34_MB_TYPES; i++){
        if(blocks[i] > count){
            count = blocks[i];
            prev_type = i;
        }
    }
    if(s->pict_type == AV_PICTURE_TYPE_P){
        prev_type = block_num_to_ptype_vlc_num[prev_type];
        q = get_vlc2(gb, ptype_vlc[prev_type].table, PTYPE_VLC_BITS, 1);
        if(q < PBTYPE_ESCAPE)
            return q;
        q = get_vlc2(gb, ptype_vlc[prev_type].table, PTYPE_VLC_BITS, 1);
        av_log(s->avctx, AV_LOG_ERROR, "Dquant for P-frame\n");
    }else{
        prev_type = block_num_to_btype_vlc_num[prev_type];
        q = get_vlc2(gb, btype_vlc[prev_type].table, BTYPE_VLC_BITS, 1);
        if(q < PBTYPE_ESCAPE)
            return q;
        q = get_vlc2(gb, btype_vlc[prev_type].table, BTYPE_VLC_BITS, 1);
        av_log(s->avctx, AV_LOG_ERROR, "Dquant for B-frame\n");
    }
    return 0;
}

enum RV40BlockPos{
    POS_CUR,
    POS_TOP,
    POS_LEFT,
    POS_BOTTOM,
};

#define MASK_CUR          0x0001
#define MASK_RIGHT        0x0008
#define MASK_BOTTOM       0x0010
#define MASK_TOP          0x1000
#define MASK_Y_TOP_ROW    0x000F
#define MASK_Y_LAST_ROW   0xF000
#define MASK_Y_LEFT_COL   0x1111
#define MASK_Y_RIGHT_COL  0x8888
#define MASK_C_TOP_ROW    0x0003
#define MASK_C_LAST_ROW   0x000C
#define MASK_C_LEFT_COL   0x0005
#define MASK_C_RIGHT_COL  0x000A

static const int neighbour_offs_x[4] = { 0,  0, -1, 0 };
static const int neighbour_offs_y[4] = { 0, -1,  0, 1 };

static void rv40_adaptive_loop_filter(RV34DSPContext *rdsp,
                                      uint8_t *src, int stride, int dmode,
                                      int lim_q1, int lim_p1,
                                      int alpha, int beta, int beta2,
                                      int chroma, int edge, int dir)
{
    int filter_p1, filter_q1;
    int strong;
    int lims;

    strong = rdsp->rv40_loop_filter_strength[dir](src, stride, beta, beta2,
                                                  edge, &filter_p1, &filter_q1);

    lims = filter_p1 + filter_q1 + ((lim_q1 + lim_p1) >> 1) + 1;

    if (strong) {
        rdsp->rv40_strong_loop_filter[dir](src, stride, alpha,
                                           lims, dmode, chroma);
    } else if (filter_p1 & filter_q1) {
        rdsp->rv40_weak_loop_filter[dir](src, stride, 1, 1, alpha, beta,
                                         lims, lim_q1, lim_p1);
    } else if (filter_p1 | filter_q1) {
        rdsp->rv40_weak_loop_filter[dir](src, stride, filter_p1, filter_q1,
                                         alpha, beta, lims >> 1, lim_q1 >> 1,
                                         lim_p1 >> 1);
    }
}

/**
 * RV40 loop filtering function
 */
static void rv40_loop_filter(RV34DecContext *r, int row)
{
    MpegEncContext *s = &r->s;
    int mb_pos, mb_x;
    int i, j, k;
    uint8_t *Y, *C;
    int alpha, beta, betaY, betaC;
    int q;
    int mbtype[4];   ///< current macroblock and its neighbours types
    /**
     * flags indicating that macroblock can be filtered with strong filter
     * it is set only for intra coded MB and MB with DCs coded separately
     */
    int mb_strong[4];
    int clip[4];     ///< MB filter clipping value calculated from filtering strength
    /**
     * coded block patterns for luma part of current macroblock and its neighbours
     * Format:
     * LSB corresponds to the top left block,
     * each nibble represents one row of subblocks.
     */
    int cbp[4];
    /**
     * coded block patterns for chroma part of current macroblock and its neighbours
     * Format is the same as for luma with two subblocks in a row.
     */
    int uvcbp[4][2];
    /**
     * This mask represents the pattern of luma subblocks that should be filtered
     * in addition to the coded ones because because they lie at the edge of
     * 8x8 block with different enough motion vectors
     */
    int mvmasks[4];

    mb_pos = row * s->mb_stride;
    for(mb_x = 0; mb_x < s->mb_width; mb_x++, mb_pos++){
        int mbtype = s->current_picture_ptr->f.mb_type[mb_pos];
        if(IS_INTRA(mbtype) || IS_SEPARATE_DC(mbtype))
            r->cbp_luma  [mb_pos] = r->deblock_coefs[mb_pos] = 0xFFFF;
        if(IS_INTRA(mbtype))
            r->cbp_chroma[mb_pos] = 0xFF;
    }
    mb_pos = row * s->mb_stride;
    for(mb_x = 0; mb_x < s->mb_width; mb_x++, mb_pos++){
        int y_h_deblock, y_v_deblock;
        int c_v_deblock[2], c_h_deblock[2];
        int clip_left;
        int avail[4];
        int y_to_deblock, c_to_deblock[2];

        q = s->current_picture_ptr->f.qscale_table[mb_pos];
        alpha = rv40_alpha_tab[q];
        beta  = rv40_beta_tab [q];
        betaY = betaC = beta * 3;
        if(s->width * s->height <= 176*144)
            betaY += beta;

        avail[0] = 1;
        avail[1] = row;
        avail[2] = mb_x;
        avail[3] = row < s->mb_height - 1;
        for(i = 0; i < 4; i++){
            if(avail[i]){
                int pos = mb_pos + neighbour_offs_x[i] + neighbour_offs_y[i]*s->mb_stride;
                mvmasks[i] = r->deblock_coefs[pos];
                mbtype [i] = s->current_picture_ptr->f.mb_type[pos];
                cbp    [i] = r->cbp_luma[pos];
                uvcbp[i][0] = r->cbp_chroma[pos] & 0xF;
                uvcbp[i][1] = r->cbp_chroma[pos] >> 4;
            }else{
                mvmasks[i] = 0;
                mbtype [i] = mbtype[0];
                cbp    [i] = 0;
                uvcbp[i][0] = uvcbp[i][1] = 0;
            }
            mb_strong[i] = IS_INTRA(mbtype[i]) || IS_SEPARATE_DC(mbtype[i]);
            clip[i] = rv40_filter_clip_tbl[mb_strong[i] + 1][q];
        }
        y_to_deblock =  mvmasks[POS_CUR]
                     | (mvmasks[POS_BOTTOM] << 16);
        /* This pattern contains bits signalling that horizontal edges of
         * the current block can be filtered.
         * That happens when either of adjacent subblocks is coded or lies on
         * the edge of 8x8 blocks with motion vectors differing by more than
         * 3/4 pel in any component (any edge orientation for some reason).
         */
        y_h_deblock =   y_to_deblock
                    | ((cbp[POS_CUR]                           <<  4) & ~MASK_Y_TOP_ROW)
                    | ((cbp[POS_TOP]        & MASK_Y_LAST_ROW) >> 12);
        /* This pattern contains bits signalling that vertical edges of
         * the current block can be filtered.
         * That happens when either of adjacent subblocks is coded or lies on
         * the edge of 8x8 blocks with motion vectors differing by more than
         * 3/4 pel in any component (any edge orientation for some reason).
         */
        y_v_deblock =   y_to_deblock
                    | ((cbp[POS_CUR]                      << 1) & ~MASK_Y_LEFT_COL)
                    | ((cbp[POS_LEFT] & MASK_Y_RIGHT_COL) >> 3);
        if(!mb_x)
            y_v_deblock &= ~MASK_Y_LEFT_COL;
        if(!row)
            y_h_deblock &= ~MASK_Y_TOP_ROW;
        if(row == s->mb_height - 1 || (mb_strong[POS_CUR] || mb_strong[POS_BOTTOM]))
            y_h_deblock &= ~(MASK_Y_TOP_ROW << 16);
        /* Calculating chroma patterns is similar and easier since there is
         * no motion vector pattern for them.
         */
        for(i = 0; i < 2; i++){
            c_to_deblock[i] = (uvcbp[POS_BOTTOM][i] << 4) | uvcbp[POS_CUR][i];
            c_v_deblock[i] =   c_to_deblock[i]
                           | ((uvcbp[POS_CUR] [i]                       << 1) & ~MASK_C_LEFT_COL)
                           | ((uvcbp[POS_LEFT][i]   & MASK_C_RIGHT_COL) >> 1);
            c_h_deblock[i] =   c_to_deblock[i]
                           | ((uvcbp[POS_TOP][i]    & MASK_C_LAST_ROW)  >> 2)
                           |  (uvcbp[POS_CUR][i]                        << 2);
            if(!mb_x)
                c_v_deblock[i] &= ~MASK_C_LEFT_COL;
            if(!row)
                c_h_deblock[i] &= ~MASK_C_TOP_ROW;
            if(row == s->mb_height - 1 || mb_strong[POS_CUR] || mb_strong[POS_BOTTOM])
                c_h_deblock[i] &= ~(MASK_C_TOP_ROW << 4);
        }

        for(j = 0; j < 16; j += 4){
            Y = s->current_picture_ptr->f.data[0] + mb_x*16 + (row*16 + j) * s->linesize;
            for(i = 0; i < 4; i++, Y += 4){
                int ij = i + j;
                int clip_cur = y_to_deblock & (MASK_CUR << ij) ? clip[POS_CUR] : 0;
                int dither = j ? ij : i*4;

                // if bottom block is coded then we can filter its top edge
                // (or bottom edge of this block, which is the same)
                if(y_h_deblock & (MASK_BOTTOM << ij)){
                    rv40_adaptive_loop_filter(&r->rdsp, Y+4*s->linesize,
                                              s->linesize, dither,
                                              y_to_deblock & (MASK_BOTTOM << ij) ? clip[POS_CUR] : 0,
                                              clip_cur, alpha, beta, betaY,
                                              0, 0, 0);
                }
                // filter left block edge in ordinary mode (with low filtering strength)
                if(y_v_deblock & (MASK_CUR << ij) && (i || !(mb_strong[POS_CUR] || mb_strong[POS_LEFT]))){
                    if(!i)
                        clip_left = mvmasks[POS_LEFT] & (MASK_RIGHT << j) ? clip[POS_LEFT] : 0;
                    else
                        clip_left = y_to_deblock & (MASK_CUR << (ij-1)) ? clip[POS_CUR] : 0;
                    rv40_adaptive_loop_filter(&r->rdsp, Y, s->linesize, dither,
                                              clip_cur,
                                              clip_left,
                                              alpha, beta, betaY, 0, 0, 1);
                }
                // filter top edge of the current macroblock when filtering strength is high
                if(!j && y_h_deblock & (MASK_CUR << i) && (mb_strong[POS_CUR] || mb_strong[POS_TOP])){
                    rv40_adaptive_loop_filter(&r->rdsp, Y, s->linesize, dither,
                                       clip_cur,
                                       mvmasks[POS_TOP] & (MASK_TOP << i) ? clip[POS_TOP] : 0,
                                       alpha, beta, betaY, 0, 1, 0);
                }
                // filter left block edge in edge mode (with high filtering strength)
                if(y_v_deblock & (MASK_CUR << ij) && !i && (mb_strong[POS_CUR] || mb_strong[POS_LEFT])){
                    clip_left = mvmasks[POS_LEFT] & (MASK_RIGHT << j) ? clip[POS_LEFT] : 0;
                    rv40_adaptive_loop_filter(&r->rdsp, Y, s->linesize, dither,
                                       clip_cur,
                                       clip_left,
                                       alpha, beta, betaY, 0, 1, 1);
                }
            }
        }
        for(k = 0; k < 2; k++){
            for(j = 0; j < 2; j++){
                C = s->current_picture_ptr->f.data[k + 1] + mb_x*8 + (row*8 + j*4) * s->uvlinesize;
                for(i = 0; i < 2; i++, C += 4){
                    int ij = i + j*2;
                    int clip_cur = c_to_deblock[k] & (MASK_CUR << ij) ? clip[POS_CUR] : 0;
                    if(c_h_deblock[k] & (MASK_CUR << (ij+2))){
                        int clip_bot = c_to_deblock[k] & (MASK_CUR << (ij+2)) ? clip[POS_CUR] : 0;
                        rv40_adaptive_loop_filter(&r->rdsp, C+4*s->uvlinesize, s->uvlinesize, i*8,
                                           clip_bot,
                                           clip_cur,
                                           alpha, beta, betaC, 1, 0, 0);
                    }
                    if((c_v_deblock[k] & (MASK_CUR << ij)) && (i || !(mb_strong[POS_CUR] || mb_strong[POS_LEFT]))){
                        if(!i)
                            clip_left = uvcbp[POS_LEFT][k] & (MASK_CUR << (2*j+1)) ? clip[POS_LEFT] : 0;
                        else
                            clip_left = c_to_deblock[k]    & (MASK_CUR << (ij-1))  ? clip[POS_CUR]  : 0;
                        rv40_adaptive_loop_filter(&r->rdsp, C, s->uvlinesize, j*8,
                                           clip_cur,
                                           clip_left,
                                           alpha, beta, betaC, 1, 0, 1);
                    }
                    if(!j && c_h_deblock[k] & (MASK_CUR << ij) && (mb_strong[POS_CUR] || mb_strong[POS_TOP])){
                        int clip_top = uvcbp[POS_TOP][k] & (MASK_CUR << (ij+2)) ? clip[POS_TOP] : 0;
                        rv40_adaptive_loop_filter(&r->rdsp, C, s->uvlinesize, i*8,
                                           clip_cur,
                                           clip_top,
                                           alpha, beta, betaC, 1, 1, 0);
                    }
                    if(c_v_deblock[k] & (MASK_CUR << ij) && !i && (mb_strong[POS_CUR] || mb_strong[POS_LEFT])){
                        clip_left = uvcbp[POS_LEFT][k] & (MASK_CUR << (2*j+1)) ? clip[POS_LEFT] : 0;
                        rv40_adaptive_loop_filter(&r->rdsp, C, s->uvlinesize, j*8,
                                           clip_cur,
                                           clip_left,
                                           alpha, beta, betaC, 1, 1, 1);
                    }
                }
            }
        }
    }
}

/**
 * Initialize decoder.
 */
static av_cold int rv40_decode_init(AVCodecContext *avctx)
{
    RV34DecContext *r = avctx->priv_data;

    r->rv30 = 0;
    ff_rv34_decode_init(avctx);
    if(!aic_top_vlc.bits)
        rv40_init_tables();
    r->parse_slice_header = rv40_parse_slice_header;
    r->decode_intra_types = rv40_decode_intra_types;
    r->decode_mb_info     = rv40_decode_mb_info;
    r->loop_filter        = rv40_loop_filter;
    r->luma_dc_quant_i = rv40_luma_dc_quant[0];
    r->luma_dc_quant_p = rv40_luma_dc_quant[1];
    return 0;
}

AVCodec ff_rv40_decoder = {
    .name           = "rv40",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_RV40,
    .priv_data_size = sizeof(RV34DecContext),
    .init           = rv40_decode_init,
    .close          = ff_rv34_decode_end,
    .decode         = ff_rv34_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_FRAME_THREADS,
    .flush          = ff_mpeg_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 4.0"),
    .pix_fmts       = ff_pixfmt_list_420,
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(ff_rv34_decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(ff_rv34_decode_update_thread_context),
};
