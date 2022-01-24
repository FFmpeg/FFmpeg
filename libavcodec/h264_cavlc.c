/*
 * H.26L/H.264/AVC/JVT/14496-10/... cavlc bitstream decoding
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG-4 part10 cavlc bitstream decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#define CABAC(h) 0
#define UNCHECKED_BITSTREAM_READER 1

#include "h264dec.h"
#include "h264_mvpred.h"
#include "h264data.h"
#include "golomb.h"
#include "mpegutils.h"
#include "libavutil/avassert.h"


static const uint8_t golomb_to_inter_cbp_gray[16]={
 0, 1, 2, 4, 8, 3, 5,10,12,15, 7,11,13,14, 6, 9,
};

static const uint8_t golomb_to_intra4x4_cbp_gray[16]={
15, 0, 7,11,13,14, 3, 5,10,12, 1, 2, 4, 8, 6, 9,
};

static const uint8_t chroma_dc_coeff_token_len[4*5]={
 2, 0, 0, 0,
 6, 1, 0, 0,
 6, 6, 3, 0,
 6, 7, 7, 6,
 6, 8, 8, 7,
};

static const uint8_t chroma_dc_coeff_token_bits[4*5]={
 1, 0, 0, 0,
 7, 1, 0, 0,
 4, 6, 1, 0,
 3, 3, 2, 5,
 2, 3, 2, 0,
};

static const uint8_t chroma422_dc_coeff_token_len[4*9]={
  1,  0,  0,  0,
  7,  2,  0,  0,
  7,  7,  3,  0,
  9,  7,  7,  5,
  9,  9,  7,  6,
 10, 10,  9,  7,
 11, 11, 10,  7,
 12, 12, 11, 10,
 13, 12, 12, 11,
};

static const uint8_t chroma422_dc_coeff_token_bits[4*9]={
  1,   0,  0, 0,
 15,   1,  0, 0,
 14,  13,  1, 0,
  7,  12, 11, 1,
  6,   5, 10, 1,
  7,   6,  4, 9,
  7,   6,  5, 8,
  7,   6,  5, 4,
  7,   5,  4, 4,
};

static const uint8_t coeff_token_len[4][4*17]={
{
     1, 0, 0, 0,
     6, 2, 0, 0,     8, 6, 3, 0,     9, 8, 7, 5,    10, 9, 8, 6,
    11,10, 9, 7,    13,11,10, 8,    13,13,11, 9,    13,13,13,10,
    14,14,13,11,    14,14,14,13,    15,15,14,14,    15,15,15,14,
    16,15,15,15,    16,16,16,15,    16,16,16,16,    16,16,16,16,
},
{
     2, 0, 0, 0,
     6, 2, 0, 0,     6, 5, 3, 0,     7, 6, 6, 4,     8, 6, 6, 4,
     8, 7, 7, 5,     9, 8, 8, 6,    11, 9, 9, 6,    11,11,11, 7,
    12,11,11, 9,    12,12,12,11,    12,12,12,11,    13,13,13,12,
    13,13,13,13,    13,14,13,13,    14,14,14,13,    14,14,14,14,
},
{
     4, 0, 0, 0,
     6, 4, 0, 0,     6, 5, 4, 0,     6, 5, 5, 4,     7, 5, 5, 4,
     7, 5, 5, 4,     7, 6, 6, 4,     7, 6, 6, 4,     8, 7, 7, 5,
     8, 8, 7, 6,     9, 8, 8, 7,     9, 9, 8, 8,     9, 9, 9, 8,
    10, 9, 9, 9,    10,10,10,10,    10,10,10,10,    10,10,10,10,
},
{
     6, 0, 0, 0,
     6, 6, 0, 0,     6, 6, 6, 0,     6, 6, 6, 6,     6, 6, 6, 6,
     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,
     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,
     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,     6, 6, 6, 6,
}
};

static const uint8_t coeff_token_bits[4][4*17]={
{
     1, 0, 0, 0,
     5, 1, 0, 0,     7, 4, 1, 0,     7, 6, 5, 3,     7, 6, 5, 3,
     7, 6, 5, 4,    15, 6, 5, 4,    11,14, 5, 4,     8,10,13, 4,
    15,14, 9, 4,    11,10,13,12,    15,14, 9,12,    11,10,13, 8,
    15, 1, 9,12,    11,14,13, 8,     7,10, 9,12,     4, 6, 5, 8,
},
{
     3, 0, 0, 0,
    11, 2, 0, 0,     7, 7, 3, 0,     7,10, 9, 5,     7, 6, 5, 4,
     4, 6, 5, 6,     7, 6, 5, 8,    15, 6, 5, 4,    11,14,13, 4,
    15,10, 9, 4,    11,14,13,12,     8,10, 9, 8,    15,14,13,12,
    11,10, 9,12,     7,11, 6, 8,     9, 8,10, 1,     7, 6, 5, 4,
},
{
    15, 0, 0, 0,
    15,14, 0, 0,    11,15,13, 0,     8,12,14,12,    15,10,11,11,
    11, 8, 9,10,     9,14,13, 9,     8,10, 9, 8,    15,14,13,13,
    11,14,10,12,    15,10,13,12,    11,14, 9,12,     8,10,13, 8,
    13, 7, 9,12,     9,12,11,10,     5, 8, 7, 6,     1, 4, 3, 2,
},
{
     3, 0, 0, 0,
     0, 1, 0, 0,     4, 5, 6, 0,     8, 9,10,11,    12,13,14,15,
    16,17,18,19,    20,21,22,23,    24,25,26,27,    28,29,30,31,
    32,33,34,35,    36,37,38,39,    40,41,42,43,    44,45,46,47,
    48,49,50,51,    52,53,54,55,    56,57,58,59,    60,61,62,63,
}
};

static const uint8_t total_zeros_len[16][16]= {
    {1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
    {3,3,3,3,3,4,4,4,4,5,5,6,6,6,6},
    {4,3,3,3,4,4,3,3,4,5,5,6,5,6},
    {5,3,4,4,3,3,3,4,3,4,5,5,5},
    {4,4,4,3,3,3,3,3,4,5,4,5},
    {6,5,3,3,3,3,3,3,4,3,6},
    {6,5,3,3,3,2,3,4,3,6},
    {6,4,5,3,2,2,3,3,6},
    {6,6,4,2,2,3,2,5},
    {5,5,3,2,2,2,4},
    {4,4,3,3,1,3},
    {4,4,2,1,3},
    {3,3,1,2},
    {2,2,1},
    {1,1},
};

static const uint8_t total_zeros_bits[16][16]= {
    {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
    {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0},
    {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
    {3,7,5,4,6,5,4,3,3,2,2,1,0},
    {5,4,3,7,6,5,4,3,2,1,1,0},
    {1,1,7,6,5,4,3,2,1,1,0},
    {1,1,5,4,3,3,2,1,1,0},
    {1,1,1,3,3,2,2,1,0},
    {1,0,1,3,2,1,1,1},
    {1,0,1,3,2,1,1},
    {0,1,1,2,1,3},
    {0,1,1,1,1},
    {0,1,1,1},
    {0,1,1},
    {0,1},
};

static const uint8_t chroma_dc_total_zeros_len[3][4]= {
    { 1, 2, 3, 3,},
    { 1, 2, 2, 0,},
    { 1, 1, 0, 0,},
};

static const uint8_t chroma_dc_total_zeros_bits[3][4]= {
    { 1, 1, 1, 0,},
    { 1, 1, 0, 0,},
    { 1, 0, 0, 0,},
};

static const uint8_t chroma422_dc_total_zeros_len[7][8]= {
    { 1, 3, 3, 4, 4, 4, 5, 5 },
    { 3, 2, 3, 3, 3, 3, 3 },
    { 3, 3, 2, 2, 3, 3 },
    { 3, 2, 2, 2, 3 },
    { 2, 2, 2, 2 },
    { 2, 2, 1 },
    { 1, 1 },
};

static const uint8_t chroma422_dc_total_zeros_bits[7][8]= {
    { 1, 2, 3, 2, 3, 1, 1, 0 },
    { 0, 1, 1, 4, 5, 6, 7 },
    { 0, 1, 1, 2, 6, 7 },
    { 6, 0, 1, 2, 7 },
    { 0, 1, 2, 3 },
    { 0, 1, 1 },
    { 0, 1 },
};

static const uint8_t run_len[7][16]={
    {1,1},
    {1,2,2},
    {2,2,2,2},
    {2,2,2,3,3},
    {2,2,3,3,3,3},
    {2,3,3,3,3,3,3},
    {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11},
};

static const uint8_t run_bits[7][16]={
    {1,0},
    {1,1,0},
    {3,2,1,0},
    {3,2,1,1,0},
    {3,2,3,2,1,0},
    {3,0,1,3,2,5,4},
    {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1},
};

static VLC coeff_token_vlc[4];
static VLC_TYPE coeff_token_vlc_tables[520+332+280+256][2];
static const int coeff_token_vlc_tables_size[4]={520,332,280,256};

static VLC chroma_dc_coeff_token_vlc;
static VLC_TYPE chroma_dc_coeff_token_vlc_table[256][2];
static const int chroma_dc_coeff_token_vlc_table_size = 256;

static VLC chroma422_dc_coeff_token_vlc;
static VLC_TYPE chroma422_dc_coeff_token_vlc_table[8192][2];
static const int chroma422_dc_coeff_token_vlc_table_size = 8192;

static VLC total_zeros_vlc[15+1];
static VLC_TYPE total_zeros_vlc_tables[15][512][2];
static const int total_zeros_vlc_tables_size = 512;

static VLC chroma_dc_total_zeros_vlc[3+1];
static VLC_TYPE chroma_dc_total_zeros_vlc_tables[3][8][2];
static const int chroma_dc_total_zeros_vlc_tables_size = 8;

static VLC chroma422_dc_total_zeros_vlc[7+1];
static VLC_TYPE chroma422_dc_total_zeros_vlc_tables[7][32][2];
static const int chroma422_dc_total_zeros_vlc_tables_size = 32;

static VLC run_vlc[6+1];
static VLC_TYPE run_vlc_tables[6][8][2];
static const int run_vlc_tables_size = 8;

static VLC run7_vlc;
static VLC_TYPE run7_vlc_table[96][2];
static const int run7_vlc_table_size = 96;

#define LEVEL_TAB_BITS 8
static int8_t cavlc_level_tab[7][1<<LEVEL_TAB_BITS][2];

#define CHROMA_DC_COEFF_TOKEN_VLC_BITS 8
#define CHROMA422_DC_COEFF_TOKEN_VLC_BITS 13
#define COEFF_TOKEN_VLC_BITS           8
#define TOTAL_ZEROS_VLC_BITS           9
#define CHROMA_DC_TOTAL_ZEROS_VLC_BITS 3
#define CHROMA422_DC_TOTAL_ZEROS_VLC_BITS 5
#define RUN_VLC_BITS                   3
#define RUN7_VLC_BITS                  6

/**
 * Get the predicted number of non-zero coefficients.
 * @param n block index
 */
static inline int pred_non_zero_count(const H264Context *h, H264SliceContext *sl, int n)
{
    const int index8= scan8[n];
    const int left = sl->non_zero_count_cache[index8 - 1];
    const int top  = sl->non_zero_count_cache[index8 - 8];
    int i= left + top;

    if(i<64) i= (i+1)>>1;

    ff_tlog(h->avctx, "pred_nnz L%X T%X n%d s%d P%X\n", left, top, n, scan8[n], i&31);

    return i&31;
}

static av_cold void init_cavlc_level_tab(void){
    int suffix_length;
    unsigned int i;

    for(suffix_length=0; suffix_length<7; suffix_length++){
        for(i=0; i<(1<<LEVEL_TAB_BITS); i++){
            int prefix= LEVEL_TAB_BITS - av_log2(2*i);

            if(prefix + 1 + suffix_length <= LEVEL_TAB_BITS){
                int level_code = (prefix << suffix_length) +
                    (i >> (av_log2(i) - suffix_length)) - (1 << suffix_length);
                int mask = -(level_code&1);
                level_code = (((2 + level_code) >> 1) ^ mask) - mask;
                cavlc_level_tab[suffix_length][i][0]= level_code;
                cavlc_level_tab[suffix_length][i][1]= prefix + 1 + suffix_length;
            }else if(prefix + 1 <= LEVEL_TAB_BITS){
                cavlc_level_tab[suffix_length][i][0]= prefix+100;
                cavlc_level_tab[suffix_length][i][1]= prefix + 1;
            }else{
                cavlc_level_tab[suffix_length][i][0]= LEVEL_TAB_BITS+100;
                cavlc_level_tab[suffix_length][i][1]= LEVEL_TAB_BITS;
            }
        }
    }
}

av_cold void ff_h264_decode_init_vlc(void)
{
    int offset;

    chroma_dc_coeff_token_vlc.table = chroma_dc_coeff_token_vlc_table;
    chroma_dc_coeff_token_vlc.table_allocated = chroma_dc_coeff_token_vlc_table_size;
    init_vlc(&chroma_dc_coeff_token_vlc, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 4*5,
             &chroma_dc_coeff_token_len [0], 1, 1,
             &chroma_dc_coeff_token_bits[0], 1, 1,
             INIT_VLC_USE_NEW_STATIC);

    chroma422_dc_coeff_token_vlc.table = chroma422_dc_coeff_token_vlc_table;
    chroma422_dc_coeff_token_vlc.table_allocated = chroma422_dc_coeff_token_vlc_table_size;
    init_vlc(&chroma422_dc_coeff_token_vlc, CHROMA422_DC_COEFF_TOKEN_VLC_BITS, 4*9,
             &chroma422_dc_coeff_token_len [0], 1, 1,
             &chroma422_dc_coeff_token_bits[0], 1, 1,
             INIT_VLC_USE_NEW_STATIC);

    offset = 0;
    for (int i = 0; i < 4; i++) {
        coeff_token_vlc[i].table = coeff_token_vlc_tables + offset;
        coeff_token_vlc[i].table_allocated = coeff_token_vlc_tables_size[i];
        init_vlc(&coeff_token_vlc[i], COEFF_TOKEN_VLC_BITS, 4*17,
                 &coeff_token_len [i][0], 1, 1,
                 &coeff_token_bits[i][0], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
        offset += coeff_token_vlc_tables_size[i];
    }
    /*
     * This is a one time safety check to make sure that
     * the packed static coeff_token_vlc table sizes
     * were initialized correctly.
     */
    av_assert0(offset == FF_ARRAY_ELEMS(coeff_token_vlc_tables));

    for (int i = 0; i < 3; i++) {
        chroma_dc_total_zeros_vlc[i + 1].table = chroma_dc_total_zeros_vlc_tables[i];
        chroma_dc_total_zeros_vlc[i + 1].table_allocated = chroma_dc_total_zeros_vlc_tables_size;
        init_vlc(&chroma_dc_total_zeros_vlc[i + 1],
                 CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 4,
                 &chroma_dc_total_zeros_len [i][0], 1, 1,
                 &chroma_dc_total_zeros_bits[i][0], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
    }

    for (int i = 0; i < 7; i++) {
        chroma422_dc_total_zeros_vlc[i + 1].table = chroma422_dc_total_zeros_vlc_tables[i];
        chroma422_dc_total_zeros_vlc[i + 1].table_allocated = chroma422_dc_total_zeros_vlc_tables_size;
        init_vlc(&chroma422_dc_total_zeros_vlc[i + 1],
                 CHROMA422_DC_TOTAL_ZEROS_VLC_BITS, 8,
                 &chroma422_dc_total_zeros_len [i][0], 1, 1,
                 &chroma422_dc_total_zeros_bits[i][0], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
    }

    for (int i = 0; i < 15; i++) {
        total_zeros_vlc[i + 1].table = total_zeros_vlc_tables[i];
        total_zeros_vlc[i + 1].table_allocated = total_zeros_vlc_tables_size;
        init_vlc(&total_zeros_vlc[i + 1],
                 TOTAL_ZEROS_VLC_BITS, 16,
                 &total_zeros_len [i][0], 1, 1,
                 &total_zeros_bits[i][0], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
    }

    for (int i = 0; i < 6; i++) {
        run_vlc[i + 1].table = run_vlc_tables[i];
        run_vlc[i + 1].table_allocated = run_vlc_tables_size;
        init_vlc(&run_vlc[i + 1],
                 RUN_VLC_BITS, 7,
                 &run_len [i][0], 1, 1,
                 &run_bits[i][0], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
    }
    run7_vlc.table = run7_vlc_table;
    run7_vlc.table_allocated = run7_vlc_table_size;
    init_vlc(&run7_vlc, RUN7_VLC_BITS, 16,
             &run_len [6][0], 1, 1,
             &run_bits[6][0], 1, 1,
             INIT_VLC_USE_NEW_STATIC);

    init_cavlc_level_tab();
}

static inline int get_level_prefix(GetBitContext *gb){
    unsigned int buf;
    int log;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);

    log= 32 - av_log2(buf);

    LAST_SKIP_BITS(re, gb, log);
    CLOSE_READER(re, gb);

    return log-1;
}

/**
 * Decode a residual block.
 * @param n block index
 * @param scantable scantable
 * @param max_coeff number of coefficients in the block
 * @return <0 if an error occurred
 */
static int decode_residual(const H264Context *h, H264SliceContext *sl,
                           GetBitContext *gb, int16_t *block, int n,
                           const uint8_t *scantable, const uint32_t *qmul,
                           int max_coeff)
{
    static const int coeff_token_table_index[17]= {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    int level[16];
    int zeros_left, coeff_token, total_coeff, i, trailing_ones, run_before;

    //FIXME put trailing_onex into the context

    if(max_coeff <= 8){
        if (max_coeff == 4)
            coeff_token = get_vlc2(gb, chroma_dc_coeff_token_vlc.table, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 1);
        else
            coeff_token = get_vlc2(gb, chroma422_dc_coeff_token_vlc.table, CHROMA422_DC_COEFF_TOKEN_VLC_BITS, 1);
        total_coeff= coeff_token>>2;
    }else{
        if(n >= LUMA_DC_BLOCK_INDEX){
            total_coeff= pred_non_zero_count(h, sl, (n - LUMA_DC_BLOCK_INDEX)*16);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
        }else{
            total_coeff= pred_non_zero_count(h, sl, n);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
        }
    }
    sl->non_zero_count_cache[scan8[n]] = total_coeff;

    //FIXME set last_non_zero?

    if(total_coeff==0)
        return 0;
    if(total_coeff > (unsigned)max_coeff) {
        av_log(h->avctx, AV_LOG_ERROR, "corrupted macroblock %d %d (total_coeff=%d)\n", sl->mb_x, sl->mb_y, total_coeff);
        return -1;
    }

    trailing_ones= coeff_token&3;
    ff_tlog(h->avctx, "trailing:%d, total:%d\n", trailing_ones, total_coeff);
    av_assert2(total_coeff<=16);

    i = show_bits(gb, 3);
    skip_bits(gb, trailing_ones);
    level[0] = 1-((i&4)>>1);
    level[1] = 1-((i&2)   );
    level[2] = 1-((i&1)<<1);

    if(trailing_ones<total_coeff) {
        int mask, prefix;
        int suffix_length = total_coeff > 10 & trailing_ones < 3;
        int bitsi= show_bits(gb, LEVEL_TAB_BITS);
        int level_code= cavlc_level_tab[suffix_length][bitsi][0];

        skip_bits(gb, cavlc_level_tab[suffix_length][bitsi][1]);
        if(level_code >= 100){
            prefix= level_code - 100;
            if(prefix == LEVEL_TAB_BITS)
                prefix += get_level_prefix(gb);

            //first coefficient has suffix_length equal to 0 or 1
            if(prefix<14){ //FIXME try to build a large unified VLC table for all this
                if(suffix_length)
                    level_code= (prefix<<1) + get_bits1(gb); //part
                else
                    level_code= prefix; //part
            }else if(prefix==14){
                if(suffix_length)
                    level_code= (prefix<<1) + get_bits1(gb); //part
                else
                    level_code= prefix + get_bits(gb, 4); //part
            }else{
                level_code= 30;
                if(prefix>=16){
                    if(prefix > 25+3){
                        av_log(h->avctx, AV_LOG_ERROR, "Invalid level prefix\n");
                        return -1;
                    }
                    level_code += (1<<(prefix-3))-4096;
                }
                level_code += get_bits(gb, prefix-3); //part
            }

            if(trailing_ones < 3) level_code += 2;

            suffix_length = 2;
            mask= -(level_code&1);
            level[trailing_ones]= (((2+level_code)>>1) ^ mask) - mask;
        }else{
            level_code += ((level_code>>31)|1) & -(trailing_ones < 3);

            suffix_length = 1 + (level_code + 3U > 6U);
            level[trailing_ones]= level_code;
        }

        //remaining coefficients have suffix_length > 0
        for(i=trailing_ones+1;i<total_coeff;i++) {
            static const unsigned int suffix_limit[7] = {0,3,6,12,24,48,INT_MAX };
            int bitsi= show_bits(gb, LEVEL_TAB_BITS);
            level_code= cavlc_level_tab[suffix_length][bitsi][0];

            skip_bits(gb, cavlc_level_tab[suffix_length][bitsi][1]);
            if(level_code >= 100){
                prefix= level_code - 100;
                if(prefix == LEVEL_TAB_BITS){
                    prefix += get_level_prefix(gb);
                }
                if(prefix<15){
                    level_code = (prefix<<suffix_length) + get_bits(gb, suffix_length);
                }else{
                    level_code = 15<<suffix_length;
                    if (prefix>=16) {
                        if(prefix > 25+3){
                            av_log(h->avctx, AV_LOG_ERROR, "Invalid level prefix\n");
                            return AVERROR_INVALIDDATA;
                        }
                        level_code += (1<<(prefix-3))-4096;
                    }
                    level_code += get_bits(gb, prefix-3);
                }
                mask= -(level_code&1);
                level_code= (((2+level_code)>>1) ^ mask) - mask;
            }
            level[i]= level_code;
            suffix_length+= suffix_limit[suffix_length] + level_code > 2U*suffix_limit[suffix_length];
        }
    }

    if(total_coeff == max_coeff)
        zeros_left=0;
    else{
        if (max_coeff <= 8) {
            if (max_coeff == 4)
                zeros_left = get_vlc2(gb, chroma_dc_total_zeros_vlc[total_coeff].table,
                                      CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 1);
            else
                zeros_left = get_vlc2(gb, chroma422_dc_total_zeros_vlc[total_coeff].table,
                                      CHROMA422_DC_TOTAL_ZEROS_VLC_BITS, 1);
        } else {
            zeros_left= get_vlc2(gb, total_zeros_vlc[ total_coeff ].table, TOTAL_ZEROS_VLC_BITS, 1);
        }
    }

#define STORE_BLOCK(type) \
    scantable += zeros_left + total_coeff - 1; \
    if(n >= LUMA_DC_BLOCK_INDEX){ \
        ((type*)block)[*scantable] = level[0]; \
        for(i=1;i<total_coeff && zeros_left > 0;i++) { \
            if(zeros_left < 7) \
                run_before= get_vlc2(gb, run_vlc[zeros_left].table, RUN_VLC_BITS, 1); \
            else \
                run_before= get_vlc2(gb, run7_vlc.table, RUN7_VLC_BITS, 2); \
            zeros_left -= run_before; \
            scantable -= 1 + run_before; \
            ((type*)block)[*scantable]= level[i]; \
        } \
        for(;i<total_coeff;i++) { \
            scantable--; \
            ((type*)block)[*scantable]= level[i]; \
        } \
    }else{ \
        ((type*)block)[*scantable] = ((int)(level[0] * qmul[*scantable] + 32))>>6; \
        for(i=1;i<total_coeff && zeros_left > 0;i++) { \
            if(zeros_left < 7) \
                run_before= get_vlc2(gb, run_vlc[zeros_left].table, RUN_VLC_BITS, 1); \
            else \
                run_before= get_vlc2(gb, run7_vlc.table, RUN7_VLC_BITS, 2); \
            zeros_left -= run_before; \
            scantable -= 1 + run_before; \
            ((type*)block)[*scantable]= ((int)(level[i] * qmul[*scantable] + 32))>>6; \
        } \
        for(;i<total_coeff;i++) { \
            scantable--; \
            ((type*)block)[*scantable]= ((int)(level[i] * qmul[*scantable] + 32))>>6; \
        } \
    }

    if (h->pixel_shift) {
        STORE_BLOCK(int32_t)
    } else {
        STORE_BLOCK(int16_t)
    }

    if(zeros_left<0){
        av_log(h->avctx, AV_LOG_ERROR, "negative number of zero coeffs at %d %d\n", sl->mb_x, sl->mb_y);
        return -1;
    }

    return 0;
}

static av_always_inline
int decode_luma_residual(const H264Context *h, H264SliceContext *sl,
                         GetBitContext *gb, const uint8_t *scan,
                         const uint8_t *scan8x8, int pixel_shift,
                         int mb_type, int cbp, int p)
{
    int i4x4, i8x8;
    int qscale = p == 0 ? sl->qscale : sl->chroma_qp[p - 1];
    if(IS_INTRA16x16(mb_type)){
        AV_ZERO128(sl->mb_luma_dc[p]+0);
        AV_ZERO128(sl->mb_luma_dc[p]+8);
        AV_ZERO128(sl->mb_luma_dc[p]+16);
        AV_ZERO128(sl->mb_luma_dc[p]+24);
        if (decode_residual(h, sl, gb, sl->mb_luma_dc[p], LUMA_DC_BLOCK_INDEX + p, scan, NULL, 16) < 0) {
            return -1; //FIXME continue if partitioned and other return -1 too
        }

        av_assert2((cbp&15) == 0 || (cbp&15) == 15);

        if(cbp&15){
            for(i8x8=0; i8x8<4; i8x8++){
                for(i4x4=0; i4x4<4; i4x4++){
                    const int index= i4x4 + 4*i8x8 + p*16;
                    if( decode_residual(h, sl, gb, sl->mb + (16*index << pixel_shift),
                        index, scan + 1, h->ps.pps->dequant4_coeff[p][qscale], 15) < 0 ){
                        return -1;
                    }
                }
            }
            return 0xf;
        }else{
            fill_rectangle(&sl->non_zero_count_cache[scan8[p*16]], 4, 4, 8, 0, 1);
            return 0;
        }
    }else{
        int cqm = (IS_INTRA( mb_type ) ? 0:3)+p;
        /* For CAVLC 4:4:4, we need to keep track of the luma 8x8 CBP for deblocking nnz purposes. */
        int new_cbp = 0;
        for(i8x8=0; i8x8<4; i8x8++){
            if(cbp & (1<<i8x8)){
                if(IS_8x8DCT(mb_type)){
                    int16_t *buf = &sl->mb[64*i8x8+256*p << pixel_shift];
                    uint8_t *nnz;
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8 + p*16;
                        if( decode_residual(h, sl, gb, buf, index, scan8x8+16*i4x4,
                                            h->ps.pps->dequant8_coeff[cqm][qscale], 16) < 0 )
                            return -1;
                    }
                    nnz = &sl->non_zero_count_cache[scan8[4 * i8x8 + p * 16]];
                    nnz[0] += nnz[1] + nnz[8] + nnz[9];
                    new_cbp |= !!nnz[0] << i8x8;
                }else{
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8 + p*16;
                        if( decode_residual(h, sl, gb, sl->mb + (16*index << pixel_shift), index,
                                            scan, h->ps.pps->dequant4_coeff[cqm][qscale], 16) < 0 ){
                            return -1;
                        }
                        new_cbp |= sl->non_zero_count_cache[scan8[index]] << i8x8;
                    }
                }
            }else{
                uint8_t * const nnz = &sl->non_zero_count_cache[scan8[4 * i8x8 + p * 16]];
                nnz[0] = nnz[1] = nnz[8] = nnz[9] = 0;
            }
        }
        return new_cbp;
    }
}

int ff_h264_decode_mb_cavlc(const H264Context *h, H264SliceContext *sl)
{
    int mb_xy;
    int partition_count;
    unsigned int mb_type, cbp;
    int dct8x8_allowed = h->ps.pps->transform_8x8_mode;
    const int decode_chroma = h->ps.sps->chroma_format_idc == 1 || h->ps.sps->chroma_format_idc == 2;
    const int pixel_shift = h->pixel_shift;

    mb_xy = sl->mb_xy = sl->mb_x + sl->mb_y*h->mb_stride;

    ff_tlog(h->avctx, "pic:%d mb:%d/%d\n", h->poc.frame_num, sl->mb_x, sl->mb_y);
    cbp = 0; /* avoid warning. FIXME: find a solution without slowing
                down the code */
    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        if (sl->mb_skip_run == -1) {
            unsigned mb_skip_run = get_ue_golomb_long(&sl->gb);
            if (mb_skip_run > h->mb_num) {
                av_log(h->avctx, AV_LOG_ERROR, "mb_skip_run %d is invalid\n", mb_skip_run);
                return AVERROR_INVALIDDATA;
            }
            sl->mb_skip_run = mb_skip_run;
        }

        if (sl->mb_skip_run--) {
            if (FRAME_MBAFF(h) && (sl->mb_y & 1) == 0) {
                if (sl->mb_skip_run == 0)
                    sl->mb_mbaff = sl->mb_field_decoding_flag = get_bits1(&sl->gb);
            }
            decode_mb_skip(h, sl);
            return 0;
        }
    }
    if (FRAME_MBAFF(h)) {
        if ((sl->mb_y & 1) == 0)
            sl->mb_mbaff = sl->mb_field_decoding_flag = get_bits1(&sl->gb);
    }

    sl->prev_mb_skipped = 0;

    mb_type= get_ue_golomb(&sl->gb);
    if (sl->slice_type_nos == AV_PICTURE_TYPE_B) {
        if(mb_type < 23){
            partition_count = ff_h264_b_mb_type_info[mb_type].partition_count;
            mb_type         = ff_h264_b_mb_type_info[mb_type].type;
        }else{
            mb_type -= 23;
            goto decode_intra_mb;
        }
    } else if (sl->slice_type_nos == AV_PICTURE_TYPE_P) {
        if(mb_type < 5){
            partition_count = ff_h264_p_mb_type_info[mb_type].partition_count;
            mb_type         = ff_h264_p_mb_type_info[mb_type].type;
        }else{
            mb_type -= 5;
            goto decode_intra_mb;
        }
    }else{
       av_assert2(sl->slice_type_nos == AV_PICTURE_TYPE_I);
        if (sl->slice_type == AV_PICTURE_TYPE_SI && mb_type)
            mb_type--;
decode_intra_mb:
        if(mb_type > 25){
            av_log(h->avctx, AV_LOG_ERROR, "mb_type %d in %c slice too large at %d %d\n", mb_type, av_get_picture_type_char(sl->slice_type), sl->mb_x, sl->mb_y);
            return -1;
        }
        partition_count=0;
        cbp                      = ff_h264_i_mb_type_info[mb_type].cbp;
        sl->intra16x16_pred_mode = ff_h264_i_mb_type_info[mb_type].pred_mode;
        mb_type                  = ff_h264_i_mb_type_info[mb_type].type;
    }

    if (MB_FIELD(sl))
        mb_type |= MB_TYPE_INTERLACED;

    h->slice_table[mb_xy] = sl->slice_num;

    if(IS_INTRA_PCM(mb_type)){
        const int mb_size = ff_h264_mb_sizes[h->ps.sps->chroma_format_idc] *
                            h->ps.sps->bit_depth_luma;

        // We assume these blocks are very rare so we do not optimize it.
        sl->intra_pcm_ptr = align_get_bits(&sl->gb);
        if (get_bits_left(&sl->gb) < mb_size) {
            av_log(h->avctx, AV_LOG_ERROR, "Not enough data for an intra PCM block.\n");
            return AVERROR_INVALIDDATA;
        }
        skip_bits_long(&sl->gb, mb_size);

        // In deblocking, the quantizer is 0
        h->cur_pic.qscale_table[mb_xy] = 0;
        // All coeffs are present
        memset(h->non_zero_count[mb_xy], 16, 48);

        h->cur_pic.mb_type[mb_xy] = mb_type;
        return 0;
    }

    fill_decode_neighbors(h, sl, mb_type);
    fill_decode_caches(h, sl, mb_type);

    //mb_pred
    if(IS_INTRA(mb_type)){
        int pred_mode;
//            init_top_left_availability(h);
        if(IS_INTRA4x4(mb_type)){
            int i;
            int di = 1;
            if(dct8x8_allowed && get_bits1(&sl->gb)){
                mb_type |= MB_TYPE_8x8DCT;
                di = 4;
            }

//                fill_intra4x4_pred_table(h);
            for(i=0; i<16; i+=di){
                int mode = pred_intra_mode(h, sl, i);

                if(!get_bits1(&sl->gb)){
                    const int rem_mode= get_bits(&sl->gb, 3);
                    mode = rem_mode + (rem_mode >= mode);
                }

                if(di==4)
                    fill_rectangle(&sl->intra4x4_pred_mode_cache[ scan8[i] ], 2, 2, 8, mode, 1);
                else
                    sl->intra4x4_pred_mode_cache[scan8[i]] = mode;
            }
            write_back_intra_pred_mode(h, sl);
            if (ff_h264_check_intra4x4_pred_mode(sl->intra4x4_pred_mode_cache, h->avctx,
                                                 sl->top_samples_available, sl->left_samples_available) < 0)
                return -1;
        }else{
            sl->intra16x16_pred_mode = ff_h264_check_intra_pred_mode(h->avctx, sl->top_samples_available,
                                                                     sl->left_samples_available, sl->intra16x16_pred_mode, 0);
            if (sl->intra16x16_pred_mode < 0)
                return -1;
        }
        if(decode_chroma){
            pred_mode= ff_h264_check_intra_pred_mode(h->avctx, sl->top_samples_available,
                                                     sl->left_samples_available, get_ue_golomb_31(&sl->gb), 1);
            if(pred_mode < 0)
                return -1;
            sl->chroma_pred_mode = pred_mode;
        } else {
            sl->chroma_pred_mode = DC_128_PRED8x8;
        }
    }else if(partition_count==4){
        int i, j, sub_partition_count[4], list, ref[2][4];

        if (sl->slice_type_nos == AV_PICTURE_TYPE_B) {
            for(i=0; i<4; i++){
                sl->sub_mb_type[i]= get_ue_golomb_31(&sl->gb);
                if(sl->sub_mb_type[i] >=13){
                    av_log(h->avctx, AV_LOG_ERROR, "B sub_mb_type %u out of range at %d %d\n", sl->sub_mb_type[i], sl->mb_x, sl->mb_y);
                    return -1;
                }
                sub_partition_count[i] = ff_h264_b_sub_mb_type_info[sl->sub_mb_type[i]].partition_count;
                sl->sub_mb_type[i]     = ff_h264_b_sub_mb_type_info[sl->sub_mb_type[i]].type;
            }
            if( IS_DIRECT(sl->sub_mb_type[0]|sl->sub_mb_type[1]|sl->sub_mb_type[2]|sl->sub_mb_type[3])) {
                ff_h264_pred_direct_motion(h, sl, &mb_type);
                sl->ref_cache[0][scan8[4]] =
                sl->ref_cache[1][scan8[4]] =
                sl->ref_cache[0][scan8[12]] =
                sl->ref_cache[1][scan8[12]] = PART_NOT_AVAILABLE;
            }
        }else{
            av_assert2(sl->slice_type_nos == AV_PICTURE_TYPE_P); //FIXME SP correct ?
            for(i=0; i<4; i++){
                sl->sub_mb_type[i]= get_ue_golomb_31(&sl->gb);
                if(sl->sub_mb_type[i] >=4){
                    av_log(h->avctx, AV_LOG_ERROR, "P sub_mb_type %u out of range at %d %d\n", sl->sub_mb_type[i], sl->mb_x, sl->mb_y);
                    return -1;
                }
                sub_partition_count[i] = ff_h264_p_sub_mb_type_info[sl->sub_mb_type[i]].partition_count;
                sl->sub_mb_type[i]     = ff_h264_p_sub_mb_type_info[sl->sub_mb_type[i]].type;
            }
        }

        for (list = 0; list < sl->list_count; list++) {
            int ref_count = IS_REF0(mb_type) ? 1 : sl->ref_count[list] << MB_MBAFF(sl);
            for(i=0; i<4; i++){
                if(IS_DIRECT(sl->sub_mb_type[i])) continue;
                if(IS_DIR(sl->sub_mb_type[i], 0, list)){
                    unsigned int tmp;
                    if(ref_count == 1){
                        tmp= 0;
                    }else if(ref_count == 2){
                        tmp= get_bits1(&sl->gb)^1;
                    }else{
                        tmp= get_ue_golomb_31(&sl->gb);
                        if(tmp>=ref_count){
                            av_log(h->avctx, AV_LOG_ERROR, "ref %u overflow\n", tmp);
                            return -1;
                        }
                    }
                    ref[list][i]= tmp;
                }else{
                 //FIXME
                    ref[list][i] = -1;
                }
            }
        }

        if(dct8x8_allowed)
            dct8x8_allowed = get_dct8x8_allowed(h, sl);

        for (list = 0; list < sl->list_count; list++) {
            for(i=0; i<4; i++){
                if(IS_DIRECT(sl->sub_mb_type[i])) {
                    sl->ref_cache[list][ scan8[4*i] ] = sl->ref_cache[list][ scan8[4*i]+1 ];
                    continue;
                }
                sl->ref_cache[list][ scan8[4*i]   ]=sl->ref_cache[list][ scan8[4*i]+1 ]=
                sl->ref_cache[list][ scan8[4*i]+8 ]=sl->ref_cache[list][ scan8[4*i]+9 ]= ref[list][i];

                if(IS_DIR(sl->sub_mb_type[i], 0, list)){
                    const int sub_mb_type= sl->sub_mb_type[i];
                    const int block_width= (sub_mb_type & (MB_TYPE_16x16|MB_TYPE_16x8)) ? 2 : 1;
                    for(j=0; j<sub_partition_count[i]; j++){
                        int mx, my;
                        const int index= 4*i + block_width*j;
                        int16_t (* mv_cache)[2]= &sl->mv_cache[list][ scan8[index] ];
                        pred_motion(h, sl, index, block_width, list, sl->ref_cache[list][ scan8[index] ], &mx, &my);
                        mx += (unsigned)get_se_golomb(&sl->gb);
                        my += (unsigned)get_se_golomb(&sl->gb);
                        ff_tlog(h->avctx, "final mv:%d %d\n", mx, my);

                        if(IS_SUB_8X8(sub_mb_type)){
                            mv_cache[ 1 ][0]=
                            mv_cache[ 8 ][0]= mv_cache[ 9 ][0]= mx;
                            mv_cache[ 1 ][1]=
                            mv_cache[ 8 ][1]= mv_cache[ 9 ][1]= my;
                        }else if(IS_SUB_8X4(sub_mb_type)){
                            mv_cache[ 1 ][0]= mx;
                            mv_cache[ 1 ][1]= my;
                        }else if(IS_SUB_4X8(sub_mb_type)){
                            mv_cache[ 8 ][0]= mx;
                            mv_cache[ 8 ][1]= my;
                        }
                        mv_cache[ 0 ][0]= mx;
                        mv_cache[ 0 ][1]= my;
                    }
                }else{
                    uint32_t *p= (uint32_t *)&sl->mv_cache[list][ scan8[4*i] ][0];
                    p[0] = p[1]=
                    p[8] = p[9]= 0;
                }
            }
        }
    }else if(IS_DIRECT(mb_type)){
        ff_h264_pred_direct_motion(h, sl, &mb_type);
        dct8x8_allowed &= h->ps.sps->direct_8x8_inference_flag;
    }else{
        int list, mx, my, i;
         //FIXME we should set ref_idx_l? to 0 if we use that later ...
        if(IS_16X16(mb_type)){
            for (list = 0; list < sl->list_count; list++) {
                    unsigned int val;
                    if(IS_DIR(mb_type, 0, list)){
                        unsigned rc = sl->ref_count[list] << MB_MBAFF(sl);
                        if (rc == 1) {
                            val= 0;
                        } else if (rc == 2) {
                            val= get_bits1(&sl->gb)^1;
                        }else{
                            val= get_ue_golomb_31(&sl->gb);
                            if (val >= rc) {
                                av_log(h->avctx, AV_LOG_ERROR, "ref %u overflow\n", val);
                                return -1;
                            }
                        }
                    fill_rectangle(&sl->ref_cache[list][ scan8[0] ], 4, 4, 8, val, 1);
                    }
            }
            for (list = 0; list < sl->list_count; list++) {
                if(IS_DIR(mb_type, 0, list)){
                    pred_motion(h, sl, 0, 4, list, sl->ref_cache[list][ scan8[0] ], &mx, &my);
                    mx += (unsigned)get_se_golomb(&sl->gb);
                    my += (unsigned)get_se_golomb(&sl->gb);
                    ff_tlog(h->avctx, "final mv:%d %d\n", mx, my);

                    fill_rectangle(sl->mv_cache[list][ scan8[0] ], 4, 4, 8, pack16to32(mx,my), 4);
                }
            }
        }
        else if(IS_16X8(mb_type)){
            for (list = 0; list < sl->list_count; list++) {
                    for(i=0; i<2; i++){
                        unsigned int val;
                        if(IS_DIR(mb_type, i, list)){
                            unsigned rc = sl->ref_count[list] << MB_MBAFF(sl);
                            if (rc == 1) {
                                val= 0;
                            } else if (rc == 2) {
                                val= get_bits1(&sl->gb)^1;
                            }else{
                                val= get_ue_golomb_31(&sl->gb);
                                if (val >= rc) {
                                    av_log(h->avctx, AV_LOG_ERROR, "ref %u overflow\n", val);
                                    return -1;
                                }
                            }
                        }else
                            val= LIST_NOT_USED&0xFF;
                        fill_rectangle(&sl->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, val, 1);
                    }
            }
            for (list = 0; list < sl->list_count; list++) {
                for(i=0; i<2; i++){
                    unsigned int val;
                    if(IS_DIR(mb_type, i, list)){
                        pred_16x8_motion(h, sl, 8*i, list, sl->ref_cache[list][scan8[0] + 16*i], &mx, &my);
                        mx += (unsigned)get_se_golomb(&sl->gb);
                        my += (unsigned)get_se_golomb(&sl->gb);
                        ff_tlog(h->avctx, "final mv:%d %d\n", mx, my);

                        val= pack16to32(mx,my);
                    }else
                        val=0;
                    fill_rectangle(sl->mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, val, 4);
                }
            }
        }else{
            av_assert2(IS_8X16(mb_type));
            for (list = 0; list < sl->list_count; list++) {
                    for(i=0; i<2; i++){
                        unsigned int val;
                        if(IS_DIR(mb_type, i, list)){ //FIXME optimize
                            unsigned rc = sl->ref_count[list] << MB_MBAFF(sl);
                            if (rc == 1) {
                                val= 0;
                            } else if (rc == 2) {
                                val= get_bits1(&sl->gb)^1;
                            }else{
                                val= get_ue_golomb_31(&sl->gb);
                                if (val >= rc) {
                                    av_log(h->avctx, AV_LOG_ERROR, "ref %u overflow\n", val);
                                    return -1;
                                }
                            }
                        }else
                            val= LIST_NOT_USED&0xFF;
                        fill_rectangle(&sl->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, val, 1);
                    }
            }
            for (list = 0; list < sl->list_count; list++) {
                for(i=0; i<2; i++){
                    unsigned int val;
                    if(IS_DIR(mb_type, i, list)){
                        pred_8x16_motion(h, sl, i*4, list, sl->ref_cache[list][ scan8[0] + 2*i ], &mx, &my);
                        mx += (unsigned)get_se_golomb(&sl->gb);
                        my += (unsigned)get_se_golomb(&sl->gb);
                        ff_tlog(h->avctx, "final mv:%d %d\n", mx, my);

                        val= pack16to32(mx,my);
                    }else
                        val=0;
                    fill_rectangle(sl->mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, val, 4);
                }
            }
        }
    }

    if(IS_INTER(mb_type))
        write_back_motion(h, sl, mb_type);

    if(!IS_INTRA16x16(mb_type)){
        cbp= get_ue_golomb(&sl->gb);

        if(decode_chroma){
            if(cbp > 47){
                av_log(h->avctx, AV_LOG_ERROR, "cbp too large (%u) at %d %d\n", cbp, sl->mb_x, sl->mb_y);
                return -1;
            }
            if (IS_INTRA4x4(mb_type))
                cbp = ff_h264_golomb_to_intra4x4_cbp[cbp];
            else
                cbp = ff_h264_golomb_to_inter_cbp[cbp];
        }else{
            if(cbp > 15){
                av_log(h->avctx, AV_LOG_ERROR, "cbp too large (%u) at %d %d\n", cbp, sl->mb_x, sl->mb_y);
                return -1;
            }
            if(IS_INTRA4x4(mb_type)) cbp= golomb_to_intra4x4_cbp_gray[cbp];
            else                     cbp= golomb_to_inter_cbp_gray[cbp];
        }
    } else {
        if (!decode_chroma && cbp>15) {
            av_log(h->avctx, AV_LOG_ERROR, "gray chroma\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if(dct8x8_allowed && (cbp&15) && !IS_INTRA(mb_type)){
        mb_type |= MB_TYPE_8x8DCT*get_bits1(&sl->gb);
    }
    sl->cbp=
    h->cbp_table[mb_xy]= cbp;
    h->cur_pic.mb_type[mb_xy] = mb_type;

    if(cbp || IS_INTRA16x16(mb_type)){
        int i4x4, i8x8, chroma_idx;
        int dquant;
        int ret;
        GetBitContext *gb = &sl->gb;
        const uint8_t *scan, *scan8x8;
        const int max_qp = 51 + 6 * (h->ps.sps->bit_depth_luma - 8);

        dquant= get_se_golomb(&sl->gb);

        sl->qscale += (unsigned)dquant;

        if (((unsigned)sl->qscale) > max_qp){
            if (sl->qscale < 0) sl->qscale += max_qp + 1;
            else                sl->qscale -= max_qp+1;
            if (((unsigned)sl->qscale) > max_qp){
                av_log(h->avctx, AV_LOG_ERROR, "dquant out of range (%d) at %d %d\n", dquant, sl->mb_x, sl->mb_y);
                sl->qscale = max_qp;
                return -1;
            }
        }

        sl->chroma_qp[0] = get_chroma_qp(h->ps.pps, 0, sl->qscale);
        sl->chroma_qp[1] = get_chroma_qp(h->ps.pps, 1, sl->qscale);

        if(IS_INTERLACED(mb_type)){
            scan8x8 = sl->qscale ? h->field_scan8x8_cavlc : h->field_scan8x8_cavlc_q0;
            scan    = sl->qscale ? h->field_scan : h->field_scan_q0;
        }else{
            scan8x8 = sl->qscale ? h->zigzag_scan8x8_cavlc : h->zigzag_scan8x8_cavlc_q0;
            scan    = sl->qscale ? h->zigzag_scan : h->zigzag_scan_q0;
        }

        if ((ret = decode_luma_residual(h, sl, gb, scan, scan8x8, pixel_shift, mb_type, cbp, 0)) < 0 ) {
            return -1;
        }
        h->cbp_table[mb_xy] |= ret << 12;
        if (CHROMA444(h)) {
            if (decode_luma_residual(h, sl, gb, scan, scan8x8, pixel_shift, mb_type, cbp, 1) < 0 ) {
                return -1;
            }
            if (decode_luma_residual(h, sl, gb, scan, scan8x8, pixel_shift, mb_type, cbp, 2) < 0 ) {
                return -1;
            }
        } else {
            const int num_c8x8 = h->ps.sps->chroma_format_idc;

            if(cbp&0x30){
                for(chroma_idx=0; chroma_idx<2; chroma_idx++)
                    if (decode_residual(h, sl, gb, sl->mb + ((256 + 16*16*chroma_idx) << pixel_shift),
                                        CHROMA_DC_BLOCK_INDEX + chroma_idx,
                                        CHROMA422(h) ? ff_h264_chroma422_dc_scan : ff_h264_chroma_dc_scan,
                                        NULL, 4 * num_c8x8) < 0) {
                        return -1;
                    }
            }

            if(cbp&0x20){
                for(chroma_idx=0; chroma_idx<2; chroma_idx++){
                    const uint32_t *qmul = h->ps.pps->dequant4_coeff[chroma_idx+1+(IS_INTRA( mb_type ) ? 0:3)][sl->chroma_qp[chroma_idx]];
                    int16_t *mb = sl->mb + (16*(16 + 16*chroma_idx) << pixel_shift);
                    for (i8x8 = 0; i8x8<num_c8x8; i8x8++) {
                        for (i4x4 = 0; i4x4 < 4; i4x4++) {
                            const int index = 16 + 16*chroma_idx + 8*i8x8 + i4x4;
                            if (decode_residual(h, sl, gb, mb, index, scan + 1, qmul, 15) < 0)
                                return -1;
                            mb += 16 << pixel_shift;
                        }
                    }
                }
            }else{
                fill_rectangle(&sl->non_zero_count_cache[scan8[16]], 4, 4, 8, 0, 1);
                fill_rectangle(&sl->non_zero_count_cache[scan8[32]], 4, 4, 8, 0, 1);
            }
        }
    }else{
        fill_rectangle(&sl->non_zero_count_cache[scan8[ 0]], 4, 4, 8, 0, 1);
        fill_rectangle(&sl->non_zero_count_cache[scan8[16]], 4, 4, 8, 0, 1);
        fill_rectangle(&sl->non_zero_count_cache[scan8[32]], 4, 4, 8, 0, 1);
    }
    h->cur_pic.qscale_table[mb_xy] = sl->qscale;
    write_back_non_zero_count(h, sl);

    return 0;
}
