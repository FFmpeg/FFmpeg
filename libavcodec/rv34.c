/*
 * RV30/40 decoder common data
 * Copyright (c) 2007 Mike Melanson, Konstantin Shishkov
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
 * RV30/40 decoder common data
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"
#include "libavutil/video_enc_params.h"

#include "avcodec.h"
#include "error_resilience.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "golomb.h"
#include "internal.h"
#include "mathops.h"
#include "mpeg_er.h"
#include "qpeldsp.h"
#include "rectangle.h"
#include "thread.h"

#include "rv34vlc.h"
#include "rv34data.h"
#include "rv34.h"

static inline void ZERO8x2(void* dst, int stride)
{
    fill_rectangle(dst,                 1, 2, stride, 0, 4);
    fill_rectangle(((uint8_t*)(dst))+4, 1, 2, stride, 0, 4);
}

/** translation of RV30/40 macroblock types to lavc ones */
static const int rv34_mb_type_to_lavc[12] = {
    MB_TYPE_INTRA,
    MB_TYPE_INTRA16x16              | MB_TYPE_SEPARATE_DC,
    MB_TYPE_16x16   | MB_TYPE_L0,
    MB_TYPE_8x8     | MB_TYPE_L0,
    MB_TYPE_16x16   | MB_TYPE_L0,
    MB_TYPE_16x16   | MB_TYPE_L1,
    MB_TYPE_SKIP,
    MB_TYPE_DIRECT2 | MB_TYPE_16x16,
    MB_TYPE_16x8    | MB_TYPE_L0,
    MB_TYPE_8x16    | MB_TYPE_L0,
    MB_TYPE_16x16   | MB_TYPE_L0L1,
    MB_TYPE_16x16   | MB_TYPE_L0    | MB_TYPE_SEPARATE_DC
};


static RV34VLC intra_vlcs[NUM_INTRA_TABLES], inter_vlcs[NUM_INTER_TABLES];

static int rv34_decode_mv(RV34DecContext *r, int block_type);

/**
 * @name RV30/40 VLC generating functions
 * @{
 */

static VLC_TYPE table_data[117592][2];

/**
 * Generate VLC from codeword lengths.
 * @param bits   codeword lengths (zeroes are accepted)
 * @param size   length of input data
 * @param vlc    output VLC
 * @param insyms symbols for input codes (NULL for default ones)
 * @param num    VLC table number (for static initialization)
 */
static void rv34_gen_vlc(const uint8_t *bits, int size, VLC *vlc, const uint8_t *syms,
                         int *offset)
{
    int counts[17] = {0}, codes[17];
    uint16_t cw[MAX_VLC_SIZE];
    int maxbits;

    for (int i = 0; i < size; i++)
        counts[bits[i]]++;

    /* bits[0] is zero for some tables, i.e. syms actually starts at 1.
     * So we reset it here. The code assigned to this element is 0x00. */
    codes[0] = counts[0] = 0;
    for (int i = 0; i < 16; i++) {
        codes[i+1] = (codes[i] + counts[i]) << 1;
        if (counts[i])
            maxbits = i;
    }
    for (int i = 0; i < size; i++)
        cw[i] = codes[bits[i]]++;

    vlc->table           = &table_data[*offset];
    vlc->table_allocated = FF_ARRAY_ELEMS(table_data) - *offset;
    ff_init_vlc_sparse(vlc, FFMIN(maxbits, 9), size,
                       bits, 1, 1,
                       cw,    2, 2,
                       syms, !!syms, !!syms, INIT_VLC_STATIC_OVERLONG);
    *offset += vlc->table_size;
}

/**
 * Initialize all tables.
 */
static av_cold void rv34_init_tables(void)
{
    int i, j, k, offset = 0;

    for(i = 0; i < NUM_INTRA_TABLES; i++){
        for(j = 0; j < 2; j++){
            rv34_gen_vlc(rv34_table_intra_cbppat   [i][j], CBPPAT_VLC_SIZE,
                         &intra_vlcs[i].cbppattern[j],     NULL, &offset);
            rv34_gen_vlc(rv34_table_intra_secondpat[i][j], OTHERBLK_VLC_SIZE,
                         &intra_vlcs[i].second_pattern[j], NULL, &offset);
            rv34_gen_vlc(rv34_table_intra_thirdpat [i][j], OTHERBLK_VLC_SIZE,
                         &intra_vlcs[i].third_pattern[j],  NULL, &offset);
            for(k = 0; k < 4; k++){
                rv34_gen_vlc(rv34_table_intra_cbp[i][j+k*2],  CBP_VLC_SIZE,
                             &intra_vlcs[i].cbp[j][k], rv34_cbp_code, &offset);
            }
        }
        for(j = 0; j < 4; j++){
            rv34_gen_vlc(rv34_table_intra_firstpat[i][j], FIRSTBLK_VLC_SIZE,
                         &intra_vlcs[i].first_pattern[j], NULL, &offset);
        }
        rv34_gen_vlc(rv34_intra_coeff[i], COEFF_VLC_SIZE,
                     &intra_vlcs[i].coefficient, NULL, &offset);
    }

    for(i = 0; i < NUM_INTER_TABLES; i++){
        rv34_gen_vlc(rv34_inter_cbppat[i], CBPPAT_VLC_SIZE,
                     &inter_vlcs[i].cbppattern[0], NULL, &offset);
        for(j = 0; j < 4; j++){
            rv34_gen_vlc(rv34_inter_cbp[i][j], CBP_VLC_SIZE,
                         &inter_vlcs[i].cbp[0][j], rv34_cbp_code, &offset);
        }
        for(j = 0; j < 2; j++){
            rv34_gen_vlc(rv34_table_inter_firstpat [i][j], FIRSTBLK_VLC_SIZE,
                         &inter_vlcs[i].first_pattern[j],  NULL, &offset);
            rv34_gen_vlc(rv34_table_inter_secondpat[i][j], OTHERBLK_VLC_SIZE,
                         &inter_vlcs[i].second_pattern[j], NULL, &offset);
            rv34_gen_vlc(rv34_table_inter_thirdpat [i][j], OTHERBLK_VLC_SIZE,
                         &inter_vlcs[i].third_pattern[j],  NULL, &offset);
        }
        rv34_gen_vlc(rv34_inter_coeff[i], COEFF_VLC_SIZE,
                     &inter_vlcs[i].coefficient, NULL, &offset);
    }
}

/** @} */ // vlc group

/**
 * @name RV30/40 4x4 block decoding functions
 * @{
 */

/**
 * Decode coded block pattern.
 */
static int rv34_decode_cbp(GetBitContext *gb, RV34VLC *vlc, int table)
{
    int pattern, code, cbp=0;
    int ones;
    static const int cbp_masks[3] = {0x100000, 0x010000, 0x110000};
    static const int shifts[4] = { 0, 2, 8, 10 };
    const int *curshift = shifts;
    int i, t, mask;

    code = get_vlc2(gb, vlc->cbppattern[table].table, 9, 2);
    pattern = code & 0xF;
    code >>= 4;

    ones = rv34_count_ones[pattern];

    for(mask = 8; mask; mask >>= 1, curshift++){
        if(pattern & mask)
            cbp |= get_vlc2(gb, vlc->cbp[table][ones].table, vlc->cbp[table][ones].bits, 1) << curshift[0];
    }

    for(i = 0; i < 4; i++){
        t = (modulo_three_table[code] >> (6 - 2*i)) & 3;
        if(t == 1)
            cbp |= cbp_masks[get_bits1(gb)] << i;
        if(t == 2)
            cbp |= cbp_masks[2] << i;
    }
    return cbp;
}

/**
 * Get one coefficient value from the bitstream and store it.
 */
static inline void decode_coeff(int16_t *dst, int coef, int esc, GetBitContext *gb, VLC* vlc, int q)
{
    if(coef){
        if(coef == esc){
            coef = get_vlc2(gb, vlc->table, 9, 2);
            if(coef > 23){
                coef -= 23;
                coef = 22 + ((1 << coef) | get_bits(gb, coef));
            }
            coef += esc;
        }
        if(get_bits1(gb))
            coef = -coef;
        *dst = (coef*q + 8) >> 4;
    }
}

/**
 * Decode 2x2 subblock of coefficients.
 */
static inline void decode_subblock(int16_t *dst, int code, const int is_block2, GetBitContext *gb, VLC *vlc, int q)
{
    int flags = modulo_three_table[code];

    decode_coeff(    dst+0*4+0, (flags >> 6)    , 3, gb, vlc, q);
    if(is_block2){
        decode_coeff(dst+1*4+0, (flags >> 4) & 3, 2, gb, vlc, q);
        decode_coeff(dst+0*4+1, (flags >> 2) & 3, 2, gb, vlc, q);
    }else{
        decode_coeff(dst+0*4+1, (flags >> 4) & 3, 2, gb, vlc, q);
        decode_coeff(dst+1*4+0, (flags >> 2) & 3, 2, gb, vlc, q);
    }
    decode_coeff(    dst+1*4+1, (flags >> 0) & 3, 2, gb, vlc, q);
}

/**
 * Decode a single coefficient.
 */
static inline void decode_subblock1(int16_t *dst, int code, GetBitContext *gb, VLC *vlc, int q)
{
    int coeff = modulo_three_table[code] >> 6;
    decode_coeff(dst, coeff, 3, gb, vlc, q);
}

static inline void decode_subblock3(int16_t *dst, int code, GetBitContext *gb, VLC *vlc,
                                    int q_dc, int q_ac1, int q_ac2)
{
    int flags = modulo_three_table[code];

    decode_coeff(dst+0*4+0, (flags >> 6)    , 3, gb, vlc, q_dc);
    decode_coeff(dst+0*4+1, (flags >> 4) & 3, 2, gb, vlc, q_ac1);
    decode_coeff(dst+1*4+0, (flags >> 2) & 3, 2, gb, vlc, q_ac1);
    decode_coeff(dst+1*4+1, (flags >> 0) & 3, 2, gb, vlc, q_ac2);
}

/**
 * Decode coefficients for 4x4 block.
 *
 * This is done by filling 2x2 subblocks with decoded coefficients
 * in this order (the same for subblocks and subblock coefficients):
 *  o--o
 *    /
 *   /
 *  o--o
 */

static int rv34_decode_block(int16_t *dst, GetBitContext *gb, RV34VLC *rvlc, int fc, int sc, int q_dc, int q_ac1, int q_ac2)
{
    int code, pattern, has_ac = 1;

    code = get_vlc2(gb, rvlc->first_pattern[fc].table, 9, 2);

    pattern = code & 0x7;

    code >>= 3;

    if (modulo_three_table[code] & 0x3F) {
        decode_subblock3(dst, code, gb, &rvlc->coefficient, q_dc, q_ac1, q_ac2);
    } else {
        decode_subblock1(dst, code, gb, &rvlc->coefficient, q_dc);
        if (!pattern)
            return 0;
        has_ac = 0;
    }

    if(pattern & 4){
        code = get_vlc2(gb, rvlc->second_pattern[sc].table, 9, 2);
        decode_subblock(dst + 4*0+2, code, 0, gb, &rvlc->coefficient, q_ac2);
    }
    if(pattern & 2){ // Looks like coefficients 1 and 2 are swapped for this block
        code = get_vlc2(gb, rvlc->second_pattern[sc].table, 9, 2);
        decode_subblock(dst + 4*2+0, code, 1, gb, &rvlc->coefficient, q_ac2);
    }
    if(pattern & 1){
        code = get_vlc2(gb, rvlc->third_pattern[sc].table, 9, 2);
        decode_subblock(dst + 4*2+2, code, 0, gb, &rvlc->coefficient, q_ac2);
    }
    return has_ac | pattern;
}

/**
 * @name RV30/40 bitstream parsing
 * @{
 */

/**
 * Decode starting slice position.
 * @todo Maybe replace with ff_h263_decode_mba() ?
 */
int ff_rv34_get_start_offset(GetBitContext *gb, int mb_size)
{
    int i;
    for(i = 0; i < 5; i++)
        if(rv34_mb_max_sizes[i] >= mb_size - 1)
            break;
    return rv34_mb_bits_sizes[i];
}

/**
 * Select VLC set for decoding from current quantizer, modifier and frame type.
 */
static inline RV34VLC* choose_vlc_set(int quant, int mod, int type)
{
    if(mod == 2 && quant < 19) quant += 10;
    else if(mod && quant < 26) quant += 5;
    av_assert2(quant >= 0 && quant < 32);
    return type ? &inter_vlcs[rv34_quant_to_vlc_set[1][quant]]
                : &intra_vlcs[rv34_quant_to_vlc_set[0][quant]];
}

/**
 * Decode intra macroblock header and return CBP in case of success, -1 otherwise.
 */
static int rv34_decode_intra_mb_header(RV34DecContext *r, int8_t *intra_types)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int t;

    r->is16 = get_bits1(gb);
    if(r->is16){
        s->current_picture_ptr->mb_type[mb_pos] = MB_TYPE_INTRA16x16;
        r->block_type = RV34_MB_TYPE_INTRA16x16;
        t = get_bits(gb, 2);
        fill_rectangle(intra_types, 4, 4, r->intra_types_stride, t, sizeof(intra_types[0]));
        r->luma_vlc   = 2;
    }else{
        if(!r->rv30){
            if(!get_bits1(gb))
                av_log(s->avctx, AV_LOG_ERROR, "Need DQUANT\n");
        }
        s->current_picture_ptr->mb_type[mb_pos] = MB_TYPE_INTRA;
        r->block_type = RV34_MB_TYPE_INTRA;
        if(r->decode_intra_types(r, gb, intra_types) < 0)
            return -1;
        r->luma_vlc   = 1;
    }

    r->chroma_vlc = 0;
    r->cur_vlcs   = choose_vlc_set(r->si.quant, r->si.vlc_set, 0);

    return rv34_decode_cbp(gb, r->cur_vlcs, r->is16);
}

/**
 * Decode inter macroblock header and return CBP in case of success, -1 otherwise.
 */
static int rv34_decode_inter_mb_header(RV34DecContext *r, int8_t *intra_types)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int i, t;

    r->block_type = r->decode_mb_info(r);
    if(r->block_type == -1)
        return -1;
    s->current_picture_ptr->mb_type[mb_pos] = rv34_mb_type_to_lavc[r->block_type];
    r->mb_type[mb_pos] = r->block_type;
    if(r->block_type == RV34_MB_SKIP){
        if(s->pict_type == AV_PICTURE_TYPE_P)
            r->mb_type[mb_pos] = RV34_MB_P_16x16;
        if(s->pict_type == AV_PICTURE_TYPE_B)
            r->mb_type[mb_pos] = RV34_MB_B_DIRECT;
    }
    r->is16 = !!IS_INTRA16x16(s->current_picture_ptr->mb_type[mb_pos]);
    if (rv34_decode_mv(r, r->block_type) < 0)
        return -1;
    if(r->block_type == RV34_MB_SKIP){
        fill_rectangle(intra_types, 4, 4, r->intra_types_stride, 0, sizeof(intra_types[0]));
        return 0;
    }
    r->chroma_vlc = 1;
    r->luma_vlc   = 0;

    if(IS_INTRA(s->current_picture_ptr->mb_type[mb_pos])){
        if(r->is16){
            t = get_bits(gb, 2);
            fill_rectangle(intra_types, 4, 4, r->intra_types_stride, t, sizeof(intra_types[0]));
            r->luma_vlc   = 2;
        }else{
            if(r->decode_intra_types(r, gb, intra_types) < 0)
                return -1;
            r->luma_vlc   = 1;
        }
        r->chroma_vlc = 0;
        r->cur_vlcs = choose_vlc_set(r->si.quant, r->si.vlc_set, 0);
    }else{
        for(i = 0; i < 16; i++)
            intra_types[(i & 3) + (i>>2) * r->intra_types_stride] = 0;
        r->cur_vlcs = choose_vlc_set(r->si.quant, r->si.vlc_set, 1);
        if(r->mb_type[mb_pos] == RV34_MB_P_MIX16x16){
            r->is16 = 1;
            r->chroma_vlc = 1;
            r->luma_vlc   = 2;
            r->cur_vlcs = choose_vlc_set(r->si.quant, r->si.vlc_set, 0);
        }
    }

    return rv34_decode_cbp(gb, r->cur_vlcs, r->is16);
}

/** @} */ //bitstream functions

/**
 * @name motion vector related code (prediction, reconstruction, motion compensation)
 * @{
 */

/** macroblock partition width in 8x8 blocks */
static const uint8_t part_sizes_w[RV34_MB_TYPES] = { 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 2, 2 };

/** macroblock partition height in 8x8 blocks */
static const uint8_t part_sizes_h[RV34_MB_TYPES] = { 2, 2, 2, 1, 2, 2, 2, 2, 1, 2, 2, 2 };

/** availability index for subblocks */
static const uint8_t avail_indexes[4] = { 6, 7, 10, 11 };

/**
 * motion vector prediction
 *
 * Motion prediction performed for the block by using median prediction of
 * motion vectors from the left, top and right top blocks but in corner cases
 * some other vectors may be used instead.
 */
static void rv34_pred_mv(RV34DecContext *r, int block_type, int subblock_no, int dmv_no)
{
    MpegEncContext *s = &r->s;
    int mv_pos = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride;
    int A[2] = {0}, B[2], C[2];
    int i, j;
    int mx, my;
    int* avail = r->avail_cache + avail_indexes[subblock_no];
    int c_off = part_sizes_w[block_type];

    mv_pos += (subblock_no & 1) + (subblock_no >> 1)*s->b8_stride;
    if(subblock_no == 3)
        c_off = -1;

    if(avail[-1]){
        A[0] = s->current_picture_ptr->motion_val[0][mv_pos-1][0];
        A[1] = s->current_picture_ptr->motion_val[0][mv_pos-1][1];
    }
    if(avail[-4]){
        B[0] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride][0];
        B[1] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride][1];
    }else{
        B[0] = A[0];
        B[1] = A[1];
    }
    if(!avail[c_off-4]){
        if(avail[-4] && (avail[-1] || r->rv30)){
            C[0] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride-1][0];
            C[1] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride-1][1];
        }else{
            C[0] = A[0];
            C[1] = A[1];
        }
    }else{
        C[0] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride+c_off][0];
        C[1] = s->current_picture_ptr->motion_val[0][mv_pos-s->b8_stride+c_off][1];
    }
    mx = mid_pred(A[0], B[0], C[0]);
    my = mid_pred(A[1], B[1], C[1]);
    mx += r->dmv[dmv_no][0];
    my += r->dmv[dmv_no][1];
    for(j = 0; j < part_sizes_h[block_type]; j++){
        for(i = 0; i < part_sizes_w[block_type]; i++){
            s->current_picture_ptr->motion_val[0][mv_pos + i + j*s->b8_stride][0] = mx;
            s->current_picture_ptr->motion_val[0][mv_pos + i + j*s->b8_stride][1] = my;
        }
    }
}

#define GET_PTS_DIFF(a, b) (((a) - (b) + 8192) & 0x1FFF)

/**
 * Calculate motion vector component that should be added for direct blocks.
 */
static int calc_add_mv(RV34DecContext *r, int dir, int val)
{
    int mul = dir ? -r->mv_weight2 : r->mv_weight1;

    return (int)(val * (SUINT)mul + 0x2000) >> 14;
}

/**
 * Predict motion vector for B-frame macroblock.
 */
static inline void rv34_pred_b_vector(int A[2], int B[2], int C[2],
                                      int A_avail, int B_avail, int C_avail,
                                      int *mx, int *my)
{
    if(A_avail + B_avail + C_avail != 3){
        *mx = A[0] + B[0] + C[0];
        *my = A[1] + B[1] + C[1];
        if(A_avail + B_avail + C_avail == 2){
            *mx /= 2;
            *my /= 2;
        }
    }else{
        *mx = mid_pred(A[0], B[0], C[0]);
        *my = mid_pred(A[1], B[1], C[1]);
    }
}

/**
 * motion vector prediction for B-frames
 */
static void rv34_pred_mv_b(RV34DecContext *r, int block_type, int dir)
{
    MpegEncContext *s = &r->s;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int mv_pos = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride;
    int A[2] = { 0 }, B[2] = { 0 }, C[2] = { 0 };
    int has_A = 0, has_B = 0, has_C = 0;
    int mx, my;
    int i, j;
    Picture *cur_pic = s->current_picture_ptr;
    const int mask = dir ? MB_TYPE_L1 : MB_TYPE_L0;
    int type = cur_pic->mb_type[mb_pos];

    if((r->avail_cache[6-1] & type) & mask){
        A[0] = cur_pic->motion_val[dir][mv_pos - 1][0];
        A[1] = cur_pic->motion_val[dir][mv_pos - 1][1];
        has_A = 1;
    }
    if((r->avail_cache[6-4] & type) & mask){
        B[0] = cur_pic->motion_val[dir][mv_pos - s->b8_stride][0];
        B[1] = cur_pic->motion_val[dir][mv_pos - s->b8_stride][1];
        has_B = 1;
    }
    if(r->avail_cache[6-4] && (r->avail_cache[6-2] & type) & mask){
        C[0] = cur_pic->motion_val[dir][mv_pos - s->b8_stride + 2][0];
        C[1] = cur_pic->motion_val[dir][mv_pos - s->b8_stride + 2][1];
        has_C = 1;
    }else if((s->mb_x+1) == s->mb_width && (r->avail_cache[6-5] & type) & mask){
        C[0] = cur_pic->motion_val[dir][mv_pos - s->b8_stride - 1][0];
        C[1] = cur_pic->motion_val[dir][mv_pos - s->b8_stride - 1][1];
        has_C = 1;
    }

    rv34_pred_b_vector(A, B, C, has_A, has_B, has_C, &mx, &my);

    mx += r->dmv[dir][0];
    my += r->dmv[dir][1];

    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            cur_pic->motion_val[dir][mv_pos + i + j*s->b8_stride][0] = mx;
            cur_pic->motion_val[dir][mv_pos + i + j*s->b8_stride][1] = my;
        }
    }
    if(block_type == RV34_MB_B_BACKWARD || block_type == RV34_MB_B_FORWARD){
        ZERO8x2(cur_pic->motion_val[!dir][mv_pos], s->b8_stride);
    }
}

/**
 * motion vector prediction - RV3 version
 */
static void rv34_pred_mv_rv3(RV34DecContext *r, int block_type, int dir)
{
    MpegEncContext *s = &r->s;
    int mv_pos = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride;
    int A[2] = {0}, B[2], C[2];
    int i, j, k;
    int mx, my;
    int* avail = r->avail_cache + avail_indexes[0];

    if(avail[-1]){
        A[0] = s->current_picture_ptr->motion_val[0][mv_pos - 1][0];
        A[1] = s->current_picture_ptr->motion_val[0][mv_pos - 1][1];
    }
    if(avail[-4]){
        B[0] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride][0];
        B[1] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride][1];
    }else{
        B[0] = A[0];
        B[1] = A[1];
    }
    if(!avail[-4 + 2]){
        if(avail[-4] && (avail[-1])){
            C[0] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride - 1][0];
            C[1] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride - 1][1];
        }else{
            C[0] = A[0];
            C[1] = A[1];
        }
    }else{
        C[0] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride + 2][0];
        C[1] = s->current_picture_ptr->motion_val[0][mv_pos - s->b8_stride + 2][1];
    }
    mx = mid_pred(A[0], B[0], C[0]);
    my = mid_pred(A[1], B[1], C[1]);
    mx += r->dmv[0][0];
    my += r->dmv[0][1];
    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            for(k = 0; k < 2; k++){
                s->current_picture_ptr->motion_val[k][mv_pos + i + j*s->b8_stride][0] = mx;
                s->current_picture_ptr->motion_val[k][mv_pos + i + j*s->b8_stride][1] = my;
            }
        }
    }
}

static const int chroma_coeffs[3] = { 0, 3, 5 };

/**
 * generic motion compensation function
 *
 * @param r decoder context
 * @param block_type type of the current block
 * @param xoff horizontal offset from the start of the current block
 * @param yoff vertical offset from the start of the current block
 * @param mv_off offset to the motion vector information
 * @param width width of the current partition in 8x8 blocks
 * @param height height of the current partition in 8x8 blocks
 * @param dir motion compensation direction (i.e. from the last or the next reference frame)
 * @param thirdpel motion vectors are specified in 1/3 of pixel
 * @param qpel_mc a set of functions used to perform luma motion compensation
 * @param chroma_mc a set of functions used to perform chroma motion compensation
 */
static inline void rv34_mc(RV34DecContext *r, const int block_type,
                          const int xoff, const int yoff, int mv_off,
                          const int width, const int height, int dir,
                          const int thirdpel, int weighted,
                          qpel_mc_func (*qpel_mc)[16],
                          h264_chroma_mc_func (*chroma_mc))
{
    MpegEncContext *s = &r->s;
    uint8_t *Y, *U, *V, *srcY, *srcU, *srcV;
    int dxy, mx, my, umx, umy, lx, ly, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;
    int mv_pos = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride + mv_off;
    int is16x16 = 1;
    int emu = 0;

    if(thirdpel){
        int chroma_mx, chroma_my;
        mx = (s->current_picture_ptr->motion_val[dir][mv_pos][0] + (3 << 24)) / 3 - (1 << 24);
        my = (s->current_picture_ptr->motion_val[dir][mv_pos][1] + (3 << 24)) / 3 - (1 << 24);
        lx = (s->current_picture_ptr->motion_val[dir][mv_pos][0] + (3 << 24)) % 3;
        ly = (s->current_picture_ptr->motion_val[dir][mv_pos][1] + (3 << 24)) % 3;
        chroma_mx = s->current_picture_ptr->motion_val[dir][mv_pos][0] / 2;
        chroma_my = s->current_picture_ptr->motion_val[dir][mv_pos][1] / 2;
        umx = (chroma_mx + (3 << 24)) / 3 - (1 << 24);
        umy = (chroma_my + (3 << 24)) / 3 - (1 << 24);
        uvmx = chroma_coeffs[(chroma_mx + (3 << 24)) % 3];
        uvmy = chroma_coeffs[(chroma_my + (3 << 24)) % 3];
    }else{
        int cx, cy;
        mx = s->current_picture_ptr->motion_val[dir][mv_pos][0] >> 2;
        my = s->current_picture_ptr->motion_val[dir][mv_pos][1] >> 2;
        lx = s->current_picture_ptr->motion_val[dir][mv_pos][0] & 3;
        ly = s->current_picture_ptr->motion_val[dir][mv_pos][1] & 3;
        cx = s->current_picture_ptr->motion_val[dir][mv_pos][0] / 2;
        cy = s->current_picture_ptr->motion_val[dir][mv_pos][1] / 2;
        umx = cx >> 2;
        umy = cy >> 2;
        uvmx = (cx & 3) << 1;
        uvmy = (cy & 3) << 1;
        //due to some flaw RV40 uses the same MC compensation routine for H2V2 and H3V3
        if(uvmx == 6 && uvmy == 6)
            uvmx = uvmy = 4;
    }

    if (HAVE_THREADS && (s->avctx->active_thread_type & FF_THREAD_FRAME)) {
        /* wait for the referenced mb row to be finished */
        int mb_row = s->mb_y + ((yoff + my + 5 + 8 * height) >> 4);
        ThreadFrame *f = dir ? &s->next_picture_ptr->tf : &s->last_picture_ptr->tf;
        ff_thread_await_progress(f, mb_row, 0);
    }

    dxy = ly*4 + lx;
    srcY = dir ? s->next_picture_ptr->f->data[0] : s->last_picture_ptr->f->data[0];
    srcU = dir ? s->next_picture_ptr->f->data[1] : s->last_picture_ptr->f->data[1];
    srcV = dir ? s->next_picture_ptr->f->data[2] : s->last_picture_ptr->f->data[2];
    src_x = s->mb_x * 16 + xoff + mx;
    src_y = s->mb_y * 16 + yoff + my;
    uvsrc_x = s->mb_x * 8 + (xoff >> 1) + umx;
    uvsrc_y = s->mb_y * 8 + (yoff >> 1) + umy;
    srcY += src_y * s->linesize + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;
    if(s->h_edge_pos - (width << 3) < 6 || s->v_edge_pos - (height << 3) < 6 ||
       (unsigned)(src_x - !!lx*2) > s->h_edge_pos - !!lx*2 - (width <<3) - 4 ||
       (unsigned)(src_y - !!ly*2) > s->v_edge_pos - !!ly*2 - (height<<3) - 4) {
        srcY -= 2 + 2*s->linesize;
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, srcY,
                                 s->linesize, s->linesize,
                                 (width << 3) + 6, (height << 3) + 6,
                                 src_x - 2, src_y - 2,
                                 s->h_edge_pos, s->v_edge_pos);
        srcY = s->sc.edge_emu_buffer + 2 + 2*s->linesize;
        emu = 1;
    }
    if(!weighted){
        Y = s->dest[0] + xoff      + yoff     *s->linesize;
        U = s->dest[1] + (xoff>>1) + (yoff>>1)*s->uvlinesize;
        V = s->dest[2] + (xoff>>1) + (yoff>>1)*s->uvlinesize;
    }else{
        Y = r->tmp_b_block_y [dir]     +  xoff     +  yoff    *s->linesize;
        U = r->tmp_b_block_uv[dir*2]   + (xoff>>1) + (yoff>>1)*s->uvlinesize;
        V = r->tmp_b_block_uv[dir*2+1] + (xoff>>1) + (yoff>>1)*s->uvlinesize;
    }

    if(block_type == RV34_MB_P_16x8){
        qpel_mc[1][dxy](Y, srcY, s->linesize);
        Y    += 8;
        srcY += 8;
    }else if(block_type == RV34_MB_P_8x16){
        qpel_mc[1][dxy](Y, srcY, s->linesize);
        Y    += 8 * s->linesize;
        srcY += 8 * s->linesize;
    }
    is16x16 = (block_type != RV34_MB_P_8x8) && (block_type != RV34_MB_P_16x8) && (block_type != RV34_MB_P_8x16);
    qpel_mc[!is16x16][dxy](Y, srcY, s->linesize);
    if (emu) {
        uint8_t *uvbuf = s->sc.edge_emu_buffer;

        s->vdsp.emulated_edge_mc(uvbuf, srcU,
                                 s->uvlinesize, s->uvlinesize,
                                 (width << 2) + 1, (height << 2) + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        srcU = uvbuf;
        uvbuf += 9*s->uvlinesize;

        s->vdsp.emulated_edge_mc(uvbuf, srcV,
                                 s->uvlinesize, s->uvlinesize,
                                 (width << 2) + 1, (height << 2) + 1,
                                 uvsrc_x, uvsrc_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        srcV = uvbuf;
    }
    chroma_mc[2-width]   (U, srcU, s->uvlinesize, height*4, uvmx, uvmy);
    chroma_mc[2-width]   (V, srcV, s->uvlinesize, height*4, uvmx, uvmy);
}

static void rv34_mc_1mv(RV34DecContext *r, const int block_type,
                        const int xoff, const int yoff, int mv_off,
                        const int width, const int height, int dir)
{
    rv34_mc(r, block_type, xoff, yoff, mv_off, width, height, dir, r->rv30, 0,
            r->rdsp.put_pixels_tab,
            r->rdsp.put_chroma_pixels_tab);
}

static void rv4_weight(RV34DecContext *r)
{
    r->rdsp.rv40_weight_pixels_tab[r->scaled_weight][0](r->s.dest[0],
                                                        r->tmp_b_block_y[0],
                                                        r->tmp_b_block_y[1],
                                                        r->weight1,
                                                        r->weight2,
                                                        r->s.linesize);
    r->rdsp.rv40_weight_pixels_tab[r->scaled_weight][1](r->s.dest[1],
                                                        r->tmp_b_block_uv[0],
                                                        r->tmp_b_block_uv[2],
                                                        r->weight1,
                                                        r->weight2,
                                                        r->s.uvlinesize);
    r->rdsp.rv40_weight_pixels_tab[r->scaled_weight][1](r->s.dest[2],
                                                        r->tmp_b_block_uv[1],
                                                        r->tmp_b_block_uv[3],
                                                        r->weight1,
                                                        r->weight2,
                                                        r->s.uvlinesize);
}

static void rv34_mc_2mv(RV34DecContext *r, const int block_type)
{
    int weighted = !r->rv30 && block_type != RV34_MB_B_BIDIR && r->weight1 != 8192;

    rv34_mc(r, block_type, 0, 0, 0, 2, 2, 0, r->rv30, weighted,
            r->rdsp.put_pixels_tab,
            r->rdsp.put_chroma_pixels_tab);
    if(!weighted){
        rv34_mc(r, block_type, 0, 0, 0, 2, 2, 1, r->rv30, 0,
                r->rdsp.avg_pixels_tab,
                r->rdsp.avg_chroma_pixels_tab);
    }else{
        rv34_mc(r, block_type, 0, 0, 0, 2, 2, 1, r->rv30, 1,
                r->rdsp.put_pixels_tab,
                r->rdsp.put_chroma_pixels_tab);
        rv4_weight(r);
    }
}

static void rv34_mc_2mv_skip(RV34DecContext *r)
{
    int i, j;
    int weighted = !r->rv30 && r->weight1 != 8192;

    for(j = 0; j < 2; j++)
        for(i = 0; i < 2; i++){
             rv34_mc(r, RV34_MB_P_8x8, i*8, j*8, i+j*r->s.b8_stride, 1, 1, 0, r->rv30,
                     weighted,
                     r->rdsp.put_pixels_tab,
                     r->rdsp.put_chroma_pixels_tab);
             rv34_mc(r, RV34_MB_P_8x8, i*8, j*8, i+j*r->s.b8_stride, 1, 1, 1, r->rv30,
                     weighted,
                     weighted ? r->rdsp.put_pixels_tab : r->rdsp.avg_pixels_tab,
                     weighted ? r->rdsp.put_chroma_pixels_tab : r->rdsp.avg_chroma_pixels_tab);
        }
    if(weighted)
        rv4_weight(r);
}

/** number of motion vectors in each macroblock type */
static const int num_mvs[RV34_MB_TYPES] = { 0, 0, 1, 4, 1, 1, 0, 0, 2, 2, 2, 1 };

/**
 * Decode motion vector differences
 * and perform motion vector reconstruction and motion compensation.
 */
static int rv34_decode_mv(RV34DecContext *r, int block_type)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int i, j, k, l;
    int mv_pos = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride;
    int next_bt;

    memset(r->dmv, 0, sizeof(r->dmv));
    for(i = 0; i < num_mvs[block_type]; i++){
        r->dmv[i][0] = get_interleaved_se_golomb(gb);
        r->dmv[i][1] = get_interleaved_se_golomb(gb);
        if (r->dmv[i][0] == INVALID_VLC ||
            r->dmv[i][1] == INVALID_VLC) {
            r->dmv[i][0] = r->dmv[i][1] = 0;
            return AVERROR_INVALIDDATA;
        }
    }
    switch(block_type){
    case RV34_MB_TYPE_INTRA:
    case RV34_MB_TYPE_INTRA16x16:
        ZERO8x2(s->current_picture_ptr->motion_val[0][s->mb_x * 2 + s->mb_y * 2 * s->b8_stride], s->b8_stride);
        return 0;
    case RV34_MB_SKIP:
        if(s->pict_type == AV_PICTURE_TYPE_P){
            ZERO8x2(s->current_picture_ptr->motion_val[0][s->mb_x * 2 + s->mb_y * 2 * s->b8_stride], s->b8_stride);
            rv34_mc_1mv (r, block_type, 0, 0, 0, 2, 2, 0);
            break;
        }
    case RV34_MB_B_DIRECT:
        //surprisingly, it uses motion scheme from next reference frame
        /* wait for the current mb row to be finished */
        if (HAVE_THREADS && (s->avctx->active_thread_type & FF_THREAD_FRAME))
            ff_thread_await_progress(&s->next_picture_ptr->tf, FFMAX(0, s->mb_y-1), 0);

        next_bt = s->next_picture_ptr->mb_type[s->mb_x + s->mb_y * s->mb_stride];
        if(IS_INTRA(next_bt) || IS_SKIP(next_bt)){
            ZERO8x2(s->current_picture_ptr->motion_val[0][s->mb_x * 2 + s->mb_y * 2 * s->b8_stride], s->b8_stride);
            ZERO8x2(s->current_picture_ptr->motion_val[1][s->mb_x * 2 + s->mb_y * 2 * s->b8_stride], s->b8_stride);
        }else
            for(j = 0; j < 2; j++)
                for(i = 0; i < 2; i++)
                    for(k = 0; k < 2; k++)
                        for(l = 0; l < 2; l++)
                            s->current_picture_ptr->motion_val[l][mv_pos + i + j*s->b8_stride][k] = calc_add_mv(r, l, s->next_picture_ptr->motion_val[0][mv_pos + i + j*s->b8_stride][k]);
        if(!(IS_16X8(next_bt) || IS_8X16(next_bt) || IS_8X8(next_bt))) //we can use whole macroblock MC
            rv34_mc_2mv(r, block_type);
        else
            rv34_mc_2mv_skip(r);
        ZERO8x2(s->current_picture_ptr->motion_val[0][s->mb_x * 2 + s->mb_y * 2 * s->b8_stride], s->b8_stride);
        break;
    case RV34_MB_P_16x16:
    case RV34_MB_P_MIX16x16:
        rv34_pred_mv(r, block_type, 0, 0);
        rv34_mc_1mv (r, block_type, 0, 0, 0, 2, 2, 0);
        break;
    case RV34_MB_B_FORWARD:
    case RV34_MB_B_BACKWARD:
        r->dmv[1][0] = r->dmv[0][0];
        r->dmv[1][1] = r->dmv[0][1];
        if(r->rv30)
            rv34_pred_mv_rv3(r, block_type, block_type == RV34_MB_B_BACKWARD);
        else
            rv34_pred_mv_b  (r, block_type, block_type == RV34_MB_B_BACKWARD);
        rv34_mc_1mv     (r, block_type, 0, 0, 0, 2, 2, block_type == RV34_MB_B_BACKWARD);
        break;
    case RV34_MB_P_16x8:
    case RV34_MB_P_8x16:
        rv34_pred_mv(r, block_type, 0, 0);
        rv34_pred_mv(r, block_type, 1 + (block_type == RV34_MB_P_16x8), 1);
        if(block_type == RV34_MB_P_16x8){
            rv34_mc_1mv(r, block_type, 0, 0, 0,            2, 1, 0);
            rv34_mc_1mv(r, block_type, 0, 8, s->b8_stride, 2, 1, 0);
        }
        if(block_type == RV34_MB_P_8x16){
            rv34_mc_1mv(r, block_type, 0, 0, 0, 1, 2, 0);
            rv34_mc_1mv(r, block_type, 8, 0, 1, 1, 2, 0);
        }
        break;
    case RV34_MB_B_BIDIR:
        rv34_pred_mv_b  (r, block_type, 0);
        rv34_pred_mv_b  (r, block_type, 1);
        rv34_mc_2mv     (r, block_type);
        break;
    case RV34_MB_P_8x8:
        for(i=0;i< 4;i++){
            rv34_pred_mv(r, block_type, i, i);
            rv34_mc_1mv (r, block_type, (i&1)<<3, (i&2)<<2, (i&1)+(i>>1)*s->b8_stride, 1, 1, 0);
        }
        break;
    }

    return 0;
}
/** @} */ // mv group

/**
 * @name Macroblock reconstruction functions
 * @{
 */
/** mapping of RV30/40 intra prediction types to standard H.264 types */
static const int ittrans[9] = {
 DC_PRED, VERT_PRED, HOR_PRED, DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_LEFT_PRED,
 VERT_RIGHT_PRED, VERT_LEFT_PRED, HOR_UP_PRED, HOR_DOWN_PRED,
};

/** mapping of RV30/40 intra 16x16 prediction types to standard H.264 types */
static const int ittrans16[4] = {
 DC_PRED8x8, VERT_PRED8x8, HOR_PRED8x8, PLANE_PRED8x8,
};

/**
 * Perform 4x4 intra prediction.
 */
static void rv34_pred_4x4_block(RV34DecContext *r, uint8_t *dst, int stride, int itype, int up, int left, int down, int right)
{
    uint8_t *prev = dst - stride + 4;
    uint32_t topleft;

    if(!up && !left)
        itype = DC_128_PRED;
    else if(!up){
        if(itype == VERT_PRED) itype = HOR_PRED;
        if(itype == DC_PRED)   itype = LEFT_DC_PRED;
    }else if(!left){
        if(itype == HOR_PRED)  itype = VERT_PRED;
        if(itype == DC_PRED)   itype = TOP_DC_PRED;
        if(itype == DIAG_DOWN_LEFT_PRED) itype = DIAG_DOWN_LEFT_PRED_RV40_NODOWN;
    }
    if(!down){
        if(itype == DIAG_DOWN_LEFT_PRED) itype = DIAG_DOWN_LEFT_PRED_RV40_NODOWN;
        if(itype == HOR_UP_PRED) itype = HOR_UP_PRED_RV40_NODOWN;
        if(itype == VERT_LEFT_PRED) itype = VERT_LEFT_PRED_RV40_NODOWN;
    }
    if(!right && up){
        topleft = dst[-stride + 3] * 0x01010101u;
        prev = (uint8_t*)&topleft;
    }
    r->h.pred4x4[itype](dst, prev, stride);
}

static inline int adjust_pred16(int itype, int up, int left)
{
    if(!up && !left)
        itype = DC_128_PRED8x8;
    else if(!up){
        if(itype == PLANE_PRED8x8)itype = HOR_PRED8x8;
        if(itype == VERT_PRED8x8) itype = HOR_PRED8x8;
        if(itype == DC_PRED8x8)   itype = LEFT_DC_PRED8x8;
    }else if(!left){
        if(itype == PLANE_PRED8x8)itype = VERT_PRED8x8;
        if(itype == HOR_PRED8x8)  itype = VERT_PRED8x8;
        if(itype == DC_PRED8x8)   itype = TOP_DC_PRED8x8;
    }
    return itype;
}

static inline void rv34_process_block(RV34DecContext *r,
                                      uint8_t *pdst, int stride,
                                      int fc, int sc, int q_dc, int q_ac)
{
    MpegEncContext *s = &r->s;
    int16_t *ptr = s->block[0];
    int has_ac = rv34_decode_block(ptr, &s->gb, r->cur_vlcs,
                                   fc, sc, q_dc, q_ac, q_ac);
    if(has_ac){
        r->rdsp.rv34_idct_add(pdst, stride, ptr);
    }else{
        r->rdsp.rv34_idct_dc_add(pdst, stride, ptr[0]);
        ptr[0] = 0;
    }
}

static void rv34_output_i16x16(RV34DecContext *r, int8_t *intra_types, int cbp)
{
    LOCAL_ALIGNED_16(int16_t, block16, [16]);
    MpegEncContext *s    = &r->s;
    GetBitContext  *gb   = &s->gb;
    int             q_dc = rv34_qscale_tab[ r->luma_dc_quant_i[s->qscale] ],
                    q_ac = rv34_qscale_tab[s->qscale];
    uint8_t        *dst  = s->dest[0];
    int16_t        *ptr  = s->block[0];
    int i, j, itype, has_ac;

    memset(block16, 0, 16 * sizeof(*block16));

    has_ac = rv34_decode_block(block16, gb, r->cur_vlcs, 3, 0, q_dc, q_dc, q_ac);
    if(has_ac)
        r->rdsp.rv34_inv_transform(block16);
    else
        r->rdsp.rv34_inv_transform_dc(block16);

    itype = ittrans16[intra_types[0]];
    itype = adjust_pred16(itype, r->avail_cache[6-4], r->avail_cache[6-1]);
    r->h.pred16x16[itype](dst, s->linesize);

    for(j = 0; j < 4; j++){
        for(i = 0; i < 4; i++, cbp >>= 1){
            int dc = block16[i + j*4];

            if(cbp & 1){
                has_ac = rv34_decode_block(ptr, gb, r->cur_vlcs, r->luma_vlc, 0, q_ac, q_ac, q_ac);
            }else
                has_ac = 0;

            if(has_ac){
                ptr[0] = dc;
                r->rdsp.rv34_idct_add(dst+4*i, s->linesize, ptr);
            }else
                r->rdsp.rv34_idct_dc_add(dst+4*i, s->linesize, dc);
        }

        dst += 4*s->linesize;
    }

    itype = ittrans16[intra_types[0]];
    if(itype == PLANE_PRED8x8) itype = DC_PRED8x8;
    itype = adjust_pred16(itype, r->avail_cache[6-4], r->avail_cache[6-1]);

    q_dc = rv34_qscale_tab[rv34_chroma_quant[1][s->qscale]];
    q_ac = rv34_qscale_tab[rv34_chroma_quant[0][s->qscale]];

    for(j = 1; j < 3; j++){
        dst = s->dest[j];
        r->h.pred8x8[itype](dst, s->uvlinesize);
        for(i = 0; i < 4; i++, cbp >>= 1){
            uint8_t *pdst;
            if(!(cbp & 1)) continue;
            pdst   = dst + (i&1)*4 + (i&2)*2*s->uvlinesize;

            rv34_process_block(r, pdst, s->uvlinesize,
                               r->chroma_vlc, 1, q_dc, q_ac);
        }
    }
}

static void rv34_output_intra(RV34DecContext *r, int8_t *intra_types, int cbp)
{
    MpegEncContext *s   = &r->s;
    uint8_t        *dst = s->dest[0];
    int      avail[6*8] = {0};
    int i, j, k;
    int idx, q_ac, q_dc;

    // Set neighbour information.
    if(r->avail_cache[1])
        avail[0] = 1;
    if(r->avail_cache[2])
        avail[1] = avail[2] = 1;
    if(r->avail_cache[3])
        avail[3] = avail[4] = 1;
    if(r->avail_cache[4])
        avail[5] = 1;
    if(r->avail_cache[5])
        avail[8] = avail[16] = 1;
    if(r->avail_cache[9])
        avail[24] = avail[32] = 1;

    q_ac = rv34_qscale_tab[s->qscale];
    for(j = 0; j < 4; j++){
        idx = 9 + j*8;
        for(i = 0; i < 4; i++, cbp >>= 1, dst += 4, idx++){
            rv34_pred_4x4_block(r, dst, s->linesize, ittrans[intra_types[i]], avail[idx-8], avail[idx-1], avail[idx+7], avail[idx-7]);
            avail[idx] = 1;
            if(!(cbp & 1)) continue;

            rv34_process_block(r, dst, s->linesize,
                               r->luma_vlc, 0, q_ac, q_ac);
        }
        dst += s->linesize * 4 - 4*4;
        intra_types += r->intra_types_stride;
    }

    intra_types -= r->intra_types_stride * 4;

    q_dc = rv34_qscale_tab[rv34_chroma_quant[1][s->qscale]];
    q_ac = rv34_qscale_tab[rv34_chroma_quant[0][s->qscale]];

    for(k = 0; k < 2; k++){
        dst = s->dest[1+k];
        fill_rectangle(r->avail_cache + 6, 2, 2, 4, 0, 4);

        for(j = 0; j < 2; j++){
            int* acache = r->avail_cache + 6 + j*4;
            for(i = 0; i < 2; i++, cbp >>= 1, acache++){
                int itype = ittrans[intra_types[i*2+j*2*r->intra_types_stride]];
                rv34_pred_4x4_block(r, dst+4*i, s->uvlinesize, itype, acache[-4], acache[-1], !i && !j, acache[-3]);
                acache[0] = 1;

                if(!(cbp&1)) continue;

                rv34_process_block(r, dst + 4*i, s->uvlinesize,
                                   r->chroma_vlc, 1, q_dc, q_ac);
            }

            dst += 4*s->uvlinesize;
        }
    }
}

static int is_mv_diff_gt_3(int16_t (*motion_val)[2], int step)
{
    int d;
    d = motion_val[0][0] - motion_val[-step][0];
    if(d < -3 || d > 3)
        return 1;
    d = motion_val[0][1] - motion_val[-step][1];
    if(d < -3 || d > 3)
        return 1;
    return 0;
}

static int rv34_set_deblock_coef(RV34DecContext *r)
{
    MpegEncContext *s = &r->s;
    int hmvmask = 0, vmvmask = 0, i, j;
    int midx = s->mb_x * 2 + s->mb_y * 2 * s->b8_stride;
    int16_t (*motion_val)[2] = &s->current_picture_ptr->motion_val[0][midx];
    for(j = 0; j < 16; j += 8){
        for(i = 0; i < 2; i++){
            if(is_mv_diff_gt_3(motion_val + i, 1))
                vmvmask |= 0x11 << (j + i*2);
            if((j || s->mb_y) && is_mv_diff_gt_3(motion_val + i, s->b8_stride))
                hmvmask |= 0x03 << (j + i*2);
        }
        motion_val += s->b8_stride;
    }
    if(s->first_slice_line)
        hmvmask &= ~0x000F;
    if(!s->mb_x)
        vmvmask &= ~0x1111;
    if(r->rv30){ //RV30 marks both subblocks on the edge for filtering
        vmvmask |= (vmvmask & 0x4444) >> 1;
        hmvmask |= (hmvmask & 0x0F00) >> 4;
        if(s->mb_x)
            r->deblock_coefs[s->mb_x - 1 + s->mb_y*s->mb_stride] |= (vmvmask & 0x1111) << 3;
        if(!s->first_slice_line)
            r->deblock_coefs[s->mb_x + (s->mb_y - 1)*s->mb_stride] |= (hmvmask & 0xF) << 12;
    }
    return hmvmask | vmvmask;
}

static int rv34_decode_inter_macroblock(RV34DecContext *r, int8_t *intra_types)
{
    MpegEncContext *s   = &r->s;
    GetBitContext  *gb  = &s->gb;
    uint8_t        *dst = s->dest[0];
    int16_t        *ptr = s->block[0];
    int          mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp, cbp2;
    int q_dc, q_ac, has_ac;
    int i, j;
    int dist;

    // Calculate which neighbours are available. Maybe it's worth optimizing too.
    memset(r->avail_cache, 0, sizeof(r->avail_cache));
    fill_rectangle(r->avail_cache + 6, 2, 2, 4, 1, 4);
    dist = (s->mb_x - s->resync_mb_x) + (s->mb_y - s->resync_mb_y) * s->mb_width;
    if(s->mb_x && dist)
        r->avail_cache[5] =
        r->avail_cache[9] = s->current_picture_ptr->mb_type[mb_pos - 1];
    if(dist >= s->mb_width)
        r->avail_cache[2] =
        r->avail_cache[3] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride];
    if(((s->mb_x+1) < s->mb_width) && dist >= s->mb_width - 1)
        r->avail_cache[4] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride + 1];
    if(s->mb_x && dist > s->mb_width)
        r->avail_cache[1] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride - 1];

    s->qscale = r->si.quant;
    cbp = cbp2 = rv34_decode_inter_mb_header(r, intra_types);
    r->cbp_luma  [mb_pos] = cbp;
    r->cbp_chroma[mb_pos] = cbp >> 16;
    r->deblock_coefs[mb_pos] = rv34_set_deblock_coef(r) | r->cbp_luma[mb_pos];
    s->current_picture_ptr->qscale_table[mb_pos] = s->qscale;

    if(cbp == -1)
        return -1;

    if (IS_INTRA(s->current_picture_ptr->mb_type[mb_pos])){
        if(r->is16) rv34_output_i16x16(r, intra_types, cbp);
        else        rv34_output_intra(r, intra_types, cbp);
        return 0;
    }

    if(r->is16){
        // Only for RV34_MB_P_MIX16x16
        LOCAL_ALIGNED_16(int16_t, block16, [16]);
        memset(block16, 0, 16 * sizeof(*block16));
        q_dc = rv34_qscale_tab[ r->luma_dc_quant_p[s->qscale] ];
        q_ac = rv34_qscale_tab[s->qscale];
        if (rv34_decode_block(block16, gb, r->cur_vlcs, 3, 0, q_dc, q_dc, q_ac))
            r->rdsp.rv34_inv_transform(block16);
        else
            r->rdsp.rv34_inv_transform_dc(block16);

        q_ac = rv34_qscale_tab[s->qscale];

        for(j = 0; j < 4; j++){
            for(i = 0; i < 4; i++, cbp >>= 1){
                int      dc   = block16[i + j*4];

                if(cbp & 1){
                    has_ac = rv34_decode_block(ptr, gb, r->cur_vlcs, r->luma_vlc, 0, q_ac, q_ac, q_ac);
                }else
                    has_ac = 0;

                if(has_ac){
                    ptr[0] = dc;
                    r->rdsp.rv34_idct_add(dst+4*i, s->linesize, ptr);
                }else
                    r->rdsp.rv34_idct_dc_add(dst+4*i, s->linesize, dc);
            }

            dst += 4*s->linesize;
        }

        r->cur_vlcs = choose_vlc_set(r->si.quant, r->si.vlc_set, 1);
    }else{
        q_ac = rv34_qscale_tab[s->qscale];

        for(j = 0; j < 4; j++){
            for(i = 0; i < 4; i++, cbp >>= 1){
                if(!(cbp & 1)) continue;

                rv34_process_block(r, dst + 4*i, s->linesize,
                                   r->luma_vlc, 0, q_ac, q_ac);
            }
            dst += 4*s->linesize;
        }
    }

    q_dc = rv34_qscale_tab[rv34_chroma_quant[1][s->qscale]];
    q_ac = rv34_qscale_tab[rv34_chroma_quant[0][s->qscale]];

    for(j = 1; j < 3; j++){
        dst = s->dest[j];
        for(i = 0; i < 4; i++, cbp >>= 1){
            uint8_t *pdst;
            if(!(cbp & 1)) continue;
            pdst = dst + (i&1)*4 + (i&2)*2*s->uvlinesize;

            rv34_process_block(r, pdst, s->uvlinesize,
                               r->chroma_vlc, 1, q_dc, q_ac);
        }
    }

    return 0;
}

static int rv34_decode_intra_macroblock(RV34DecContext *r, int8_t *intra_types)
{
    MpegEncContext *s = &r->s;
    int cbp, dist;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    // Calculate which neighbours are available. Maybe it's worth optimizing too.
    memset(r->avail_cache, 0, sizeof(r->avail_cache));
    fill_rectangle(r->avail_cache + 6, 2, 2, 4, 1, 4);
    dist = (s->mb_x - s->resync_mb_x) + (s->mb_y - s->resync_mb_y) * s->mb_width;
    if(s->mb_x && dist)
        r->avail_cache[5] =
        r->avail_cache[9] = s->current_picture_ptr->mb_type[mb_pos - 1];
    if(dist >= s->mb_width)
        r->avail_cache[2] =
        r->avail_cache[3] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride];
    if(((s->mb_x+1) < s->mb_width) && dist >= s->mb_width - 1)
        r->avail_cache[4] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride + 1];
    if(s->mb_x && dist > s->mb_width)
        r->avail_cache[1] = s->current_picture_ptr->mb_type[mb_pos - s->mb_stride - 1];

    s->qscale = r->si.quant;
    cbp = rv34_decode_intra_mb_header(r, intra_types);
    r->cbp_luma  [mb_pos] = cbp;
    r->cbp_chroma[mb_pos] = cbp >> 16;
    r->deblock_coefs[mb_pos] = 0xFFFF;
    s->current_picture_ptr->qscale_table[mb_pos] = s->qscale;

    if(cbp == -1)
        return -1;

    if(r->is16){
        rv34_output_i16x16(r, intra_types, cbp);
        return 0;
    }

    rv34_output_intra(r, intra_types, cbp);
    return 0;
}

static int check_slice_end(RV34DecContext *r, MpegEncContext *s)
{
    int bits;
    if(s->mb_y >= s->mb_height)
        return 1;
    if(!s->mb_num_left)
        return 1;
    if(r->s.mb_skip_run > 1)
        return 0;
    bits = get_bits_left(&s->gb);
    if(bits <= 0 || (bits < 8 && !show_bits(&s->gb, bits)))
        return 1;
    return 0;
}


static void rv34_decoder_free(RV34DecContext *r)
{
    av_freep(&r->intra_types_hist);
    r->intra_types = NULL;
    av_freep(&r->tmp_b_block_base);
    av_freep(&r->mb_type);
    av_freep(&r->cbp_luma);
    av_freep(&r->cbp_chroma);
    av_freep(&r->deblock_coefs);
}


static int rv34_decoder_alloc(RV34DecContext *r)
{
    r->intra_types_stride = r->s.mb_width * 4 + 4;

    r->cbp_chroma       = av_mallocz(r->s.mb_stride * r->s.mb_height *
                                    sizeof(*r->cbp_chroma));
    r->cbp_luma         = av_mallocz(r->s.mb_stride * r->s.mb_height *
                                    sizeof(*r->cbp_luma));
    r->deblock_coefs    = av_mallocz(r->s.mb_stride * r->s.mb_height *
                                    sizeof(*r->deblock_coefs));
    r->intra_types_hist = av_malloc(r->intra_types_stride * 4 * 2 *
                                    sizeof(*r->intra_types_hist));
    r->mb_type          = av_mallocz(r->s.mb_stride * r->s.mb_height *
                                     sizeof(*r->mb_type));

    if (!(r->cbp_chroma       && r->cbp_luma && r->deblock_coefs &&
          r->intra_types_hist && r->mb_type)) {
        r->s.context_reinit = 1;
        rv34_decoder_free(r);
        return AVERROR(ENOMEM);
    }

    r->intra_types = r->intra_types_hist + r->intra_types_stride * 4;

    return 0;
}


static int rv34_decoder_realloc(RV34DecContext *r)
{
    rv34_decoder_free(r);
    return rv34_decoder_alloc(r);
}


static int rv34_decode_slice(RV34DecContext *r, int end, const uint8_t* buf, int buf_size)
{
    MpegEncContext *s = &r->s;
    GetBitContext *gb = &s->gb;
    int mb_pos, slice_type;
    int res;

    init_get_bits(&r->s.gb, buf, buf_size*8);
    res = r->parse_slice_header(r, gb, &r->si);
    if(res < 0){
        av_log(s->avctx, AV_LOG_ERROR, "Incorrect or unknown slice header\n");
        return -1;
    }

    slice_type = r->si.type ? r->si.type : AV_PICTURE_TYPE_I;
    if (slice_type != s->pict_type) {
        av_log(s->avctx, AV_LOG_ERROR, "Slice type mismatch\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->width != r->si.width || s->height != r->si.height) {
        av_log(s->avctx, AV_LOG_ERROR, "Size mismatch\n");
        return AVERROR_INVALIDDATA;
    }

    r->si.end = end;
    s->qscale = r->si.quant;
    s->mb_num_left = r->si.end - r->si.start;
    r->s.mb_skip_run = 0;

    mb_pos = s->mb_x + s->mb_y * s->mb_width;
    if(r->si.start != mb_pos){
        av_log(s->avctx, AV_LOG_ERROR, "Slice indicates MB offset %d, got %d\n", r->si.start, mb_pos);
        s->mb_x = r->si.start % s->mb_width;
        s->mb_y = r->si.start / s->mb_width;
    }
    memset(r->intra_types_hist, -1, r->intra_types_stride * 4 * 2 * sizeof(*r->intra_types_hist));
    s->first_slice_line = 1;
    s->resync_mb_x = s->mb_x;
    s->resync_mb_y = s->mb_y;

    ff_init_block_index(s);
    while(!check_slice_end(r, s)) {
        ff_update_block_index(s);

        if(r->si.type)
            res = rv34_decode_inter_macroblock(r, r->intra_types + s->mb_x * 4 + 4);
        else
            res = rv34_decode_intra_macroblock(r, r->intra_types + s->mb_x * 4 + 4);
        if(res < 0){
            ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, ER_MB_ERROR);
            return -1;
        }
        if (++s->mb_x == s->mb_width) {
            s->mb_x = 0;
            s->mb_y++;
            ff_init_block_index(s);

            memmove(r->intra_types_hist, r->intra_types, r->intra_types_stride * 4 * sizeof(*r->intra_types_hist));
            memset(r->intra_types, -1, r->intra_types_stride * 4 * sizeof(*r->intra_types_hist));

            if(r->loop_filter && s->mb_y >= 2)
                r->loop_filter(r, s->mb_y - 2);

            if (HAVE_THREADS && (s->avctx->active_thread_type & FF_THREAD_FRAME))
                ff_thread_report_progress(&s->current_picture_ptr->tf,
                                          s->mb_y - 2, 0);

        }
        if(s->mb_x == s->resync_mb_x)
            s->first_slice_line=0;
        s->mb_num_left--;
    }
    ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, ER_MB_END);

    return s->mb_y == s->mb_height;
}

/** @} */ // reconstruction group end

/**
 * Initialize decoder.
 */
av_cold int ff_rv34_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    RV34DecContext *r = avctx->priv_data;
    MpegEncContext *s = &r->s;
    int ret;

    ff_mpv_decode_init(s, avctx);
    s->out_format = FMT_H263;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->has_b_frames = 1;
    s->low_delay = 0;

    ff_mpv_idct_init(s);
    if ((ret = ff_mpv_common_init(s)) < 0)
        return ret;

    ff_h264_pred_init(&r->h, AV_CODEC_ID_RV40, 8, 1);

#if CONFIG_RV30_DECODER
    if (avctx->codec_id == AV_CODEC_ID_RV30)
        ff_rv30dsp_init(&r->rdsp);
#endif
#if CONFIG_RV40_DECODER
    if (avctx->codec_id == AV_CODEC_ID_RV40)
        ff_rv40dsp_init(&r->rdsp);
#endif

    if ((ret = rv34_decoder_alloc(r)) < 0) {
        ff_mpv_common_end(&r->s);
        return ret;
    }

    ff_thread_once(&init_static_once, rv34_init_tables);

    return 0;
}

int ff_rv34_decode_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    RV34DecContext *r = dst->priv_data, *r1 = src->priv_data;
    MpegEncContext * const s = &r->s, * const s1 = &r1->s;
    int err;

    if (dst == src || !s1->context_initialized)
        return 0;

    if (s->height != s1->height || s->width != s1->width || s->context_reinit) {
        s->height = s1->height;
        s->width  = s1->width;
        if ((err = ff_mpv_common_frame_size_change(s)) < 0)
            return err;
        if ((err = rv34_decoder_realloc(r)) < 0)
            return err;
    }

    r->cur_pts  = r1->cur_pts;
    r->last_pts = r1->last_pts;
    r->next_pts = r1->next_pts;

    memset(&r->si, 0, sizeof(r->si));

    // Do no call ff_mpeg_update_thread_context on a partially initialized
    // decoder context.
    if (!s1->context_initialized)
        return 0;

    return ff_mpeg_update_thread_context(dst, src);
}

static int get_slice_offset(AVCodecContext *avctx, const uint8_t *buf, int n, int slice_count, int buf_size)
{
    if (n < slice_count) {
        if(avctx->slice_count) return avctx->slice_offset[n];
        else                   return AV_RL32(buf + n*8 - 4) == 1 ? AV_RL32(buf + n*8) :  AV_RB32(buf + n*8);
    } else
        return buf_size;
}

static int finish_frame(AVCodecContext *avctx, AVFrame *pict)
{
    RV34DecContext *r = avctx->priv_data;
    MpegEncContext *s = &r->s;
    int got_picture = 0, ret;

    ff_er_frame_end(&s->er);
    ff_mpv_frame_end(s);
    s->mb_num_left = 0;

    if (HAVE_THREADS && (s->avctx->active_thread_type & FF_THREAD_FRAME))
        ff_thread_report_progress(&s->current_picture_ptr->tf, INT_MAX, 0);

    if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
        if ((ret = av_frame_ref(pict, s->current_picture_ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, s->current_picture_ptr, pict);
        ff_mpv_export_qp_table(s, pict, s->current_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        got_picture = 1;
    } else if (s->last_picture_ptr) {
        if ((ret = av_frame_ref(pict, s->last_picture_ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, s->last_picture_ptr, pict);
        ff_mpv_export_qp_table(s, pict, s->last_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        got_picture = 1;
    }

    return got_picture;
}

static AVRational update_sar(int old_w, int old_h, AVRational sar, int new_w, int new_h)
{
    // attempt to keep aspect during typical resolution switches
    if (!sar.num)
        sar = (AVRational){1, 1};

    sar = av_mul_q(sar, av_mul_q((AVRational){new_h, new_w}, (AVRational){old_w, old_h}));
    return sar;
}

int ff_rv34_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_picture_ptr,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    RV34DecContext *r = avctx->priv_data;
    MpegEncContext *s = &r->s;
    AVFrame *pict = data;
    SliceInfo si;
    int i, ret;
    int slice_count;
    const uint8_t *slices_hdr = NULL;
    int last = 0;
    int faulty_b = 0;
    int offset;

    /* no supplementary picture */
    if (buf_size == 0) {
        /* special case for last picture */
        if (s->low_delay==0 && s->next_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->next_picture_ptr->f)) < 0)
                return ret;
            s->next_picture_ptr = NULL;

            *got_picture_ptr = 1;
        }
        return 0;
    }

    if(!avctx->slice_count){
        slice_count = (*buf++) + 1;
        slices_hdr = buf + 4;
        buf += 8 * slice_count;
        buf_size -= 1 + 8 * slice_count;
    }else
        slice_count = avctx->slice_count;

    offset = get_slice_offset(avctx, slices_hdr, 0, slice_count, buf_size);
    //parse first slice header to check whether this frame can be decoded
    if(offset < 0 || offset > buf_size){
        av_log(avctx, AV_LOG_ERROR, "Slice offset is invalid\n");
        return AVERROR_INVALIDDATA;
    }
    init_get_bits(&s->gb, buf+offset, (buf_size-offset)*8);
    if(r->parse_slice_header(r, &r->s.gb, &si) < 0 || si.start){
        av_log(avctx, AV_LOG_ERROR, "First slice header is incorrect\n");
        return AVERROR_INVALIDDATA;
    }
    if ((!s->last_picture_ptr || !s->last_picture_ptr->f->data[0]) &&
        si.type == AV_PICTURE_TYPE_B) {
        av_log(avctx, AV_LOG_ERROR, "Invalid decoder state: B-frame without "
               "reference data.\n");
        faulty_b = 1;
    }
    if(   (avctx->skip_frame >= AVDISCARD_NONREF && si.type==AV_PICTURE_TYPE_B)
       || (avctx->skip_frame >= AVDISCARD_NONKEY && si.type!=AV_PICTURE_TYPE_I)
       ||  avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    /* first slice */
    if (si.start == 0) {
        if (s->mb_num_left > 0 && s->current_picture_ptr) {
            av_log(avctx, AV_LOG_ERROR, "New frame but still %d MB left.\n",
                   s->mb_num_left);
            if (!s->context_reinit)
                ff_er_frame_end(&s->er);
            ff_mpv_frame_end(s);
        }

        if (s->width != si.width || s->height != si.height || s->context_reinit) {
            int err;

            av_log(s->avctx, AV_LOG_WARNING, "Changing dimensions to %dx%d\n",
                   si.width, si.height);

            if (av_image_check_size(si.width, si.height, 0, s->avctx))
                return AVERROR_INVALIDDATA;

            s->avctx->sample_aspect_ratio = update_sar(
                s->width, s->height, s->avctx->sample_aspect_ratio,
                si.width, si.height);
            s->width  = si.width;
            s->height = si.height;

            err = ff_set_dimensions(s->avctx, s->width, s->height);
            if (err < 0)
                return err;
            if ((err = ff_mpv_common_frame_size_change(s)) < 0)
                return err;
            if ((err = rv34_decoder_realloc(r)) < 0)
                return err;
        }
        if (faulty_b)
            return AVERROR_INVALIDDATA;
        s->pict_type = si.type ? si.type : AV_PICTURE_TYPE_I;
        if (ff_mpv_frame_start(s, s->avctx) < 0)
            return -1;
        ff_mpeg_er_frame_start(s);
        if (!r->tmp_b_block_base) {
            int i;

            r->tmp_b_block_base = av_malloc(s->linesize * 48);
            for (i = 0; i < 2; i++)
                r->tmp_b_block_y[i] = r->tmp_b_block_base
                                      + i * 16 * s->linesize;
            for (i = 0; i < 4; i++)
                r->tmp_b_block_uv[i] = r->tmp_b_block_base + 32 * s->linesize
                                       + (i >> 1) * 8 * s->uvlinesize
                                       + (i &  1) * 16;
        }
        r->cur_pts = si.pts;
        if (s->pict_type != AV_PICTURE_TYPE_B) {
            r->last_pts = r->next_pts;
            r->next_pts = r->cur_pts;
        } else {
            int refdist = GET_PTS_DIFF(r->next_pts, r->last_pts);
            int dist0   = GET_PTS_DIFF(r->cur_pts,  r->last_pts);
            int dist1   = GET_PTS_DIFF(r->next_pts, r->cur_pts);

            if(!refdist){
                r->mv_weight1 = r->mv_weight2 = r->weight1 = r->weight2 = 8192;
                r->scaled_weight = 0;
            }else{
                if (FFMAX(dist0, dist1) > refdist)
                    av_log(avctx, AV_LOG_TRACE, "distance overflow\n");

                r->mv_weight1 = (dist0 << 14) / refdist;
                r->mv_weight2 = (dist1 << 14) / refdist;
                if((r->mv_weight1|r->mv_weight2) & 511){
                    r->weight1 = r->mv_weight1;
                    r->weight2 = r->mv_weight2;
                    r->scaled_weight = 0;
                }else{
                    r->weight1 = r->mv_weight1 >> 9;
                    r->weight2 = r->mv_weight2 >> 9;
                    r->scaled_weight = 1;
                }
            }
        }
        s->mb_x = s->mb_y = 0;
        ff_thread_finish_setup(s->avctx);
    } else if (s->context_reinit) {
        av_log(s->avctx, AV_LOG_ERROR, "Decoder needs full frames to "
               "reinitialize (start MB is %d).\n", si.start);
        return AVERROR_INVALIDDATA;
    } else if (HAVE_THREADS &&
               (s->avctx->active_thread_type & FF_THREAD_FRAME)) {
        av_log(s->avctx, AV_LOG_ERROR, "Decoder needs full frames in frame "
               "multithreading mode (start MB is %d).\n", si.start);
        return AVERROR_INVALIDDATA;
    }

    for(i = 0; i < slice_count; i++){
        int offset  = get_slice_offset(avctx, slices_hdr, i  , slice_count, buf_size);
        int offset1 = get_slice_offset(avctx, slices_hdr, i+1, slice_count, buf_size);
        int size;

        if(offset < 0 || offset > offset1 || offset1 > buf_size){
            av_log(avctx, AV_LOG_ERROR, "Slice offset is invalid\n");
            break;
        }
        size = offset1 - offset;

        r->si.end = s->mb_width * s->mb_height;
        s->mb_num_left = r->s.mb_x + r->s.mb_y*r->s.mb_width - r->si.start;

        if(i+1 < slice_count){
            int offset2 = get_slice_offset(avctx, slices_hdr, i+2, slice_count, buf_size);
            if (offset2 < offset1 || offset2 > buf_size) {
                av_log(avctx, AV_LOG_ERROR, "Slice offset is invalid\n");
                break;
            }
            init_get_bits(&s->gb, buf+offset1, (buf_size-offset1)*8);
            if(r->parse_slice_header(r, &r->s.gb, &si) < 0){
                size = offset2 - offset;
            }else
                r->si.end = si.start;
        }
        av_assert0 (size >= 0 && size <= buf_size - offset);
        last = rv34_decode_slice(r, r->si.end, buf + offset, size);
        if(last)
            break;
    }

    if (s->current_picture_ptr) {
        if (last) {
            if(r->loop_filter)
                r->loop_filter(r, s->mb_height - 1);

            ret = finish_frame(avctx, pict);
            if (ret < 0)
                return ret;
            *got_picture_ptr = ret;
        } else if (HAVE_THREADS &&
                   (s->avctx->active_thread_type & FF_THREAD_FRAME)) {
            av_log(avctx, AV_LOG_INFO, "marking unfished frame as finished\n");
            /* always mark the current frame as finished, frame-mt supports
             * only complete frames */
            ff_er_frame_end(&s->er);
            ff_mpv_frame_end(s);
            s->mb_num_left = 0;
            ff_thread_report_progress(&s->current_picture_ptr->tf, INT_MAX, 0);
            return AVERROR_INVALIDDATA;
        }
    }

    return avpkt->size;
}

av_cold int ff_rv34_decode_end(AVCodecContext *avctx)
{
    RV34DecContext *r = avctx->priv_data;

    ff_mpv_common_end(&r->s);
    rv34_decoder_free(r);

    return 0;
}
