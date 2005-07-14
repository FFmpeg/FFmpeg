/*
 * H261 decoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file h261.c
 * h261codec.
 */

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h261data.h"


#define H261_MBA_VLC_BITS 9
#define H261_MTYPE_VLC_BITS 6
#define H261_MV_VLC_BITS 7
#define H261_CBP_VLC_BITS 9
#define TCOEFF_VLC_BITS 9

#define MBA_STUFFING 33
#define MBA_STARTCODE 34
#define IS_FIL(a)    ((a)&MB_TYPE_H261_FIL)

/**
 * H261Context
 */
typedef struct H261Context{
    MpegEncContext s;

    int current_mba;
    int previous_mba;
    int mba_diff;
    int mtype;
    int current_mv_x;
    int current_mv_y;
    int gob_number;
    int gob_start_code_skipped; // 1 if gob start code is already read before gob header is read
}H261Context;

void ff_h261_loop_filter(MpegEncContext *s){
    H261Context * h= (H261Context*)s;
    const int linesize  = s->linesize;
    const int uvlinesize= s->uvlinesize;
    uint8_t *dest_y = s->dest[0];
    uint8_t *dest_cb= s->dest[1];
    uint8_t *dest_cr= s->dest[2];

    if(!(IS_FIL (h->mtype)))
        return;

    s->dsp.h261_loop_filter(dest_y                   , linesize);
    s->dsp.h261_loop_filter(dest_y                + 8, linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize    , linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize + 8, linesize);
    s->dsp.h261_loop_filter(dest_cb, uvlinesize);
    s->dsp.h261_loop_filter(dest_cr, uvlinesize);
}

static int ff_h261_get_picture_format(int width, int height){
    // QCIF
    if (width == 176 && height == 144)
        return 0;
    // CIF
    else if (width == 352 && height == 288)
        return 1;
    // ERROR
    else
        return -1;
}

static void h261_encode_block(H261Context * h, DCTELEM * block,
                              int n);
static int h261_decode_block(H261Context *h, DCTELEM *block,
                             int n, int coded);

void ff_h261_encode_picture_header(MpegEncContext * s, int picture_number){
    H261Context * h = (H261Context *) s;
    int format, temp_ref;

    align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = pbBufPtr(&s->pb);

    put_bits(&s->pb, 20, 0x10); /* PSC */

    temp_ref= s->picture_number * (int64_t)30000 * s->avctx->time_base.num / 
                         (1001 * (int64_t)s->avctx->time_base.den); //FIXME maybe this should use a timestamp
    put_bits(&s->pb, 5, temp_ref & 0x1f); /* TemporalReference */

    put_bits(&s->pb, 1, 0); /* split screen off */
    put_bits(&s->pb, 1, 0); /* camera  off */
    put_bits(&s->pb, 1, 0); /* freeze picture release off */
    
    format = ff_h261_get_picture_format(s->width, s->height);
    
    put_bits(&s->pb, 1, format); /* 0 == QCIF, 1 == CIF */

    put_bits(&s->pb, 1, 0); /* still image mode */
    put_bits(&s->pb, 1, 0); /* reserved */

    put_bits(&s->pb, 1, 0); /* no PEI */    
    if(format == 0)
        h->gob_number = -1;
    else
        h->gob_number = 0;
    h->current_mba = 0;
}

/**
 * Encodes a group of blocks header.
 */
static void h261_encode_gob_header(MpegEncContext * s, int mb_line){
    H261Context * h = (H261Context *)s;
    if(ff_h261_get_picture_format(s->width, s->height) == 0){
        h->gob_number+=2; // QCIF
    }
    else{
        h->gob_number++; // CIF
    }
    put_bits(&s->pb, 16, 1); /* GBSC */
    put_bits(&s->pb, 4, h->gob_number); /* GN */
    put_bits(&s->pb, 5, s->qscale); /* GQUANT */
    put_bits(&s->pb, 1, 0); /* no GEI */
    h->current_mba = 0;
    h->previous_mba = 0;
    h->current_mv_x=0;
    h->current_mv_y=0;
}

void ff_h261_reorder_mb_index(MpegEncContext* s){
    int index= s->mb_x + s->mb_y*s->mb_width;

    if(index % 33 == 0)
        h261_encode_gob_header(s,0);

    /* for CIF the GOB's are fragmented in the middle of a scanline
       that's why we need to adjust the x and y index of the macroblocks */
    if(ff_h261_get_picture_format(s->width,s->height) == 1){ // CIF
        s->mb_x =     index % 11 ; index /= 11;
        s->mb_y =     index %  3 ; index /=  3;
        s->mb_x+= 11*(index %  2); index /=  2;
        s->mb_y+=  3*index;
        
        ff_init_block_index(s);
        ff_update_block_index(s);
    }
}

static void h261_encode_motion(H261Context * h, int val){
    MpegEncContext * const s = &h->s;
    int sign, code;
    if(val==0){
        code = 0;
        put_bits(&s->pb,h261_mv_tab[code][1],h261_mv_tab[code][0]);
    } 
    else{
        if(val > 15)
            val -=32;
        if(val < -16)
            val+=32;
        sign = val < 0;
        code = sign ? -val : val; 
        put_bits(&s->pb,h261_mv_tab[code][1],h261_mv_tab[code][0]);
        put_bits(&s->pb,1,sign);
    }
}

static inline int get_cbp(MpegEncContext * s,
                      DCTELEM block[6][64])
{
    int i, cbp;
    cbp= 0;
    for (i = 0; i < 6; i++) {
        if (s->block_last_index[i] >= 0)
            cbp |= 1 << (5 - i);
    }
    return cbp;
}
void ff_h261_encode_mb(MpegEncContext * s,
         DCTELEM block[6][64],
         int motion_x, int motion_y)
{
    H261Context * h = (H261Context *)s;
    int mvd, mv_diff_x, mv_diff_y, i, cbp;
    cbp = 63; // avoid warning
    mvd = 0;
 
    h->current_mba++;
    h->mtype = 0;
 
    if (!s->mb_intra){
        /* compute cbp */
        cbp= get_cbp(s, block);
   
        /* mvd indicates if this block is motion compensated */
        mvd = motion_x | motion_y;

        if((cbp | mvd | s->dquant ) == 0) {
            /* skip macroblock */
            s->skip_count++;
            h->current_mv_x=0;
            h->current_mv_y=0;
            return;
        }
    }

    /* MB is not skipped, encode MBA */
    put_bits(&s->pb, h261_mba_bits[(h->current_mba-h->previous_mba)-1], h261_mba_code[(h->current_mba-h->previous_mba)-1]);
 
    /* calculate MTYPE */
    if(!s->mb_intra){
        h->mtype++;
        
        if(mvd || s->loop_filter)
            h->mtype+=3;
        if(s->loop_filter)
            h->mtype+=3;
        if(cbp || s->dquant)
            h->mtype++;
        assert(h->mtype > 1);
    }

    if(s->dquant) 
        h->mtype++;

    put_bits(&s->pb, h261_mtype_bits[h->mtype], h261_mtype_code[h->mtype]);
 
    h->mtype = h261_mtype_map[h->mtype];
 
    if(IS_QUANT(h->mtype)){
        ff_set_qscale(s,s->qscale+s->dquant);
        put_bits(&s->pb, 5, s->qscale);
    }
 
    if(IS_16X16(h->mtype)){
        mv_diff_x = (motion_x >> 1) - h->current_mv_x;
        mv_diff_y = (motion_y >> 1) - h->current_mv_y;
        h->current_mv_x = (motion_x >> 1);
        h->current_mv_y = (motion_y >> 1);
        h261_encode_motion(h,mv_diff_x);
        h261_encode_motion(h,mv_diff_y);
    }
 
    h->previous_mba = h->current_mba;
 
    if(HAS_CBP(h->mtype)){
        put_bits(&s->pb,h261_cbp_tab[cbp-1][1],h261_cbp_tab[cbp-1][0]); 
    }
    for(i=0; i<6; i++) {
        /* encode each block */
        h261_encode_block(h, block[i], i);
    }

    if ( ( h->current_mba == 11 ) || ( h->current_mba == 22 ) || ( h->current_mba == 33 ) || ( !IS_16X16 ( h->mtype ) )){
        h->current_mv_x=0;
        h->current_mv_y=0;
    }
}

void ff_h261_encode_init(MpegEncContext *s){
    static int done = 0;
    
    if (!done) {
        done = 1;
        init_rl(&h261_rl_tcoeff, 1);
    }

    s->min_qcoeff= -127;
    s->max_qcoeff=  127;
    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
}


/**
 * encodes a 8x8 block.
 * @param block the 8x8 block
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static void h261_encode_block(H261Context * h, DCTELEM * block, int n){
    MpegEncContext * const s = &h->s;
    int level, run, last, i, j, last_index, last_non_zero, sign, slevel, code;
    RLTable *rl;

    rl = &h261_rl_tcoeff;
    if (s->mb_intra) {
        /* DC coef */
        level = block[0];
        /* 255 cannot be represented, so we clamp */
        if (level > 254) {
            level = 254;
            block[0] = 254;
        }
        /* 0 cannot be represented also */
        else if (level < 1) {
            level = 1;
            block[0] = 1;
        }
        if (level == 128)
            put_bits(&s->pb, 8, 0xff);
        else
            put_bits(&s->pb, 8, level);
        i = 1;
    } else if((block[0]==1 || block[0] == -1) && (s->block_last_index[n] > -1)){
        //special case
        put_bits(&s->pb,2,block[0]>0 ? 2 : 3 );
        i = 1;
    } else {
        i = 0;
    }
   
    /* AC coefs */
    last_index = s->block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
        j = s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            run = i - last_non_zero - 1;
            last = (i == last_index);
            sign = 0;
            slevel = level;
            if (level < 0) {
                sign = 1;
                level = -level;
            }
            code = get_rl_index(rl, 0 /*no last in H.261, EOB is used*/, run, level);
            if(run==0 && level < 16)
            code+=1;
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
                put_bits(&s->pb, 6, run);
                assert(slevel != 0);
                assert(level <= 127);
                put_bits(&s->pb, 8, slevel & 0xff);
            } else {
                put_bits(&s->pb, 1, sign);
            }
            last_non_zero = i;
        }
    }
    if(last_index > -1){
        put_bits(&s->pb, rl->table_vlc[0][1], rl->table_vlc[0][0]);// END OF BLOCK
    }
}

/***********************************************/
/* decoding */

static VLC h261_mba_vlc;
static VLC h261_mtype_vlc;
static VLC h261_mv_vlc;
static VLC h261_cbp_vlc;

void init_vlc_rl(RLTable *rl, int use_static);

static void h261_decode_init_vlc(H261Context *h){
    static int done = 0;

    if(!done){
        done = 1;
        init_vlc(&h261_mba_vlc, H261_MBA_VLC_BITS, 35,
                 h261_mba_bits, 1, 1,
                 h261_mba_code, 1, 1, 1);
        init_vlc(&h261_mtype_vlc, H261_MTYPE_VLC_BITS, 10,
                 h261_mtype_bits, 1, 1,
                 h261_mtype_code, 1, 1, 1);
        init_vlc(&h261_mv_vlc, H261_MV_VLC_BITS, 17,
                 &h261_mv_tab[0][1], 2, 1,
                 &h261_mv_tab[0][0], 2, 1, 1);
        init_vlc(&h261_cbp_vlc, H261_CBP_VLC_BITS, 63,
                 &h261_cbp_tab[0][1], 2, 1,
                 &h261_cbp_tab[0][0], 2, 1, 1);
        init_rl(&h261_rl_tcoeff, 1);
        init_vlc_rl(&h261_rl_tcoeff, 1);
    }
}

static int h261_decode_init(AVCodecContext *avctx){
    H261Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    // set defaults
    MPV_decode_defaults(s);
    s->avctx = avctx;

    s->width  = s->avctx->coded_width;
    s->height = s->avctx->coded_height;
    s->codec_id = s->avctx->codec->id;

    s->out_format = FMT_H261;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    s->codec_id= avctx->codec->id;

    h261_decode_init_vlc(h);

    h->gob_start_code_skipped = 0;
    
    return 0;
}

/**
 * decodes the group of blocks header or slice header.
 * @return <0 if an error occured
 */
static int h261_decode_gob_header(H261Context *h){
    unsigned int val;
    MpegEncContext * const s = &h->s;
    
    if ( !h->gob_start_code_skipped ){
        /* Check for GOB Start Code */
        val = show_bits(&s->gb, 15);
        if(val)
            return -1;

        /* We have a GBSC */
        skip_bits(&s->gb, 16);
    }

    h->gob_start_code_skipped = 0;

    h->gob_number = get_bits(&s->gb, 4); /* GN */
    s->qscale = get_bits(&s->gb, 5); /* GQUANT */

    /* Check if gob_number is valid */
    if (s->mb_height==18){ //cif
        if ((h->gob_number<=0) || (h->gob_number>12))
            return -1;
    }
    else{ //qcif
        if ((h->gob_number!=1) && (h->gob_number!=3) && (h->gob_number!=5))
            return -1;
    }

    /* GEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }

    if(s->qscale==0)
        return -1;

    // For the first transmitted macroblock in a GOB, MBA is the absolute address. For
    // subsequent macroblocks, MBA is the difference between the absolute addresses of
    // the macroblock and the last transmitted macroblock.
    h->current_mba = 0;
    h->mba_diff = 0;

    return 0;
}

/**
 * decodes the group of blocks / video packet header.
 * @return <0 if no resync found
 */
static int ff_h261_resync(H261Context *h){
    MpegEncContext * const s = &h->s;
    int left, ret;

    if ( h->gob_start_code_skipped ){
        ret= h261_decode_gob_header(h);
        if(ret>=0)
            return 0;
    }
    else{
        if(show_bits(&s->gb, 15)==0){
            ret= h261_decode_gob_header(h);
            if(ret>=0)
                return 0;
        }
        //ok, its not where its supposed to be ...
        s->gb= s->last_resync_gb;
        align_get_bits(&s->gb);
        left= s->gb.size_in_bits - get_bits_count(&s->gb);

        for(;left>15+1+4+5; left-=8){
            if(show_bits(&s->gb, 15)==0){
                GetBitContext bak= s->gb;

                ret= h261_decode_gob_header(h);
                if(ret>=0)
                    return 0;

                s->gb= bak;
            }
            skip_bits(&s->gb, 8);
        }
    }

    return -1;
}

/**
 * decodes skipped macroblocks
 * @return 0
 */
static int h261_decode_mb_skipped(H261Context *h, int mba1, int mba2 )
{
    MpegEncContext * const s = &h->s;
    int i;
    
    s->mb_intra = 0;

    for(i=mba1; i<mba2; i++){
        int j, xy;

        s->mb_x= ((h->gob_number-1) % 2) * 11 + i % 11;
        s->mb_y= ((h->gob_number-1) / 2) * 3 + i / 11;
        xy = s->mb_x + s->mb_y * s->mb_stride;
        ff_init_block_index(s);
        ff_update_block_index(s);

        for(j=0;j<6;j++)
            s->block_last_index[j] = -1;

        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
        s->mv[0][0][0] = 0;
        s->mv[0][0][1] = 0;
        s->mb_skipped = 1;
        h->mtype &= ~MB_TYPE_H261_FIL;

        MPV_decode_mb(s, s->block);
    }

    return 0;
}

static int decode_mv_component(GetBitContext *gb, int v){
    int mv_diff = get_vlc2(gb, h261_mv_vlc.table, H261_MV_VLC_BITS, 2);

    /* check if mv_diff is valid */
    if ( mv_diff < 0 )
        return v;

    mv_diff = mvmap[mv_diff];

    if(mv_diff && !get_bits1(gb))
        mv_diff= -mv_diff;
    
    v += mv_diff;
    if     (v <=-16) v+= 32;
    else if(v >= 16) v-= 32;

    return v;
}

static int h261_decode_mb(H261Context *h){
    MpegEncContext * const s = &h->s;
    int i, cbp, xy;

    cbp = 63;
    // Read mba
    do{
        h->mba_diff = get_vlc2(&s->gb, h261_mba_vlc.table, H261_MBA_VLC_BITS, 2);

        /* Check for slice end */
        /* NOTE: GOB can be empty (no MB data) or exist only of MBA_stuffing */
        if (h->mba_diff == MBA_STARTCODE){ // start code
            h->gob_start_code_skipped = 1;
            return SLICE_END;
        }
    }
    while( h->mba_diff == MBA_STUFFING ); // stuffing

    if ( h->mba_diff < 0 ){
        if ( get_bits_count(&s->gb) + 7 >= s->gb.size_in_bits )
            return SLICE_END;

        av_log(s->avctx, AV_LOG_ERROR, "illegal mba at %d %d\n", s->mb_x, s->mb_y);
        return SLICE_ERROR;
    }

    h->mba_diff += 1;
    h->current_mba += h->mba_diff;

    if ( h->current_mba > MBA_STUFFING )
        return SLICE_ERROR;
    
    s->mb_x= ((h->gob_number-1) % 2) * 11 + ((h->current_mba-1) % 11);
    s->mb_y= ((h->gob_number-1) / 2) * 3 + ((h->current_mba-1) / 11);
    xy = s->mb_x + s->mb_y * s->mb_stride;
    ff_init_block_index(s);
    ff_update_block_index(s);

    // Read mtype
    h->mtype = get_vlc2(&s->gb, h261_mtype_vlc.table, H261_MTYPE_VLC_BITS, 2);
    h->mtype = h261_mtype_map[h->mtype];

    // Read mquant
    if ( IS_QUANT ( h->mtype ) ){
        ff_set_qscale(s, get_bits(&s->gb, 5));
    }

    s->mb_intra = IS_INTRA4x4(h->mtype);

    // Read mv
    if ( IS_16X16 ( h->mtype ) ){
        // Motion vector data is included for all MC macroblocks. MVD is obtained from the macroblock vector by subtracting the
        // vector of the preceding macroblock. For this calculation the vector of the preceding macroblock is regarded as zero in the
        // following three situations:
        // 1) evaluating MVD for macroblocks 1, 12 and 23;
        // 2) evaluating MVD for macroblocks in which MBA does not represent a difference of 1;
        // 3) MTYPE of the previous macroblock was not MC.
        if ( ( h->current_mba == 1 ) || ( h->current_mba == 12 ) || ( h->current_mba == 23 ) ||
             ( h->mba_diff != 1))
        {
            h->current_mv_x = 0;
            h->current_mv_y = 0;
        }

        h->current_mv_x= decode_mv_component(&s->gb, h->current_mv_x);
        h->current_mv_y= decode_mv_component(&s->gb, h->current_mv_y);
    }else{
        h->current_mv_x = 0;
        h->current_mv_y = 0;
    }

    // Read cbp
    if ( HAS_CBP( h->mtype ) ){
        cbp = get_vlc2(&s->gb, h261_cbp_vlc.table, H261_CBP_VLC_BITS, 2) + 1;
    }

    if(s->mb_intra){
        s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
        goto intra;
    }

    //set motion vectors
    s->mv_dir = MV_DIR_FORWARD;
    s->mv_type = MV_TYPE_16X16;
    s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
    s->mv[0][0][0] = h->current_mv_x * 2;//gets divided by 2 in motion compensation
    s->mv[0][0][1] = h->current_mv_y * 2;

intra:
    /* decode each block */
    if(s->mb_intra || HAS_CBP(h->mtype)){
        s->dsp.clear_blocks(s->block[0]);
        for (i = 0; i < 6; i++) {
            if (h261_decode_block(h, s->block[i], i, cbp&32) < 0){
                return SLICE_ERROR;
            }
            cbp+=cbp;
        }
    }else{
        for (i = 0; i < 6; i++)
            s->block_last_index[i]= -1;
    }

    MPV_decode_mb(s, s->block);

    return SLICE_OK;
}

/**
 * decodes a macroblock
 * @return <0 if an error occured
 */
static int h261_decode_block(H261Context * h, DCTELEM * block,
                             int n, int coded)
{
    MpegEncContext * const s = &h->s;
    int code, level, i, j, run;
    RLTable *rl = &h261_rl_tcoeff;
    const uint8_t *scan_table;
    
    // For the variable length encoding there are two code tables, one being used for
    // the first transmitted LEVEL in INTER, INTER+MC and INTER+MC+FIL blocks, the second
    // for all other LEVELs except the first one in INTRA blocks which is fixed length
    // coded with 8 bits.
    // NOTE: the two code tables only differ in one VLC so we handle that manually.
    scan_table = s->intra_scantable.permutated;
    if (s->mb_intra){
        /* DC coef */
        level = get_bits(&s->gb, 8);
        // 0 (00000000b) and -128 (10000000b) are FORBIDDEN
        if((level&0x7F) == 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal dc %d at %d %d\n", level, s->mb_x, s->mb_y);
            return -1;
        }
        // The code 1000 0000 is not used, the reconstruction level of 1024 being coded as 1111 1111.
        if (level == 255)
            level = 128;
        block[0] = level;
        i = 1;
    }else if(coded){
        // Run  Level   Code
        // EOB                  Not possible for first level when cbp is available (that's why the table is different)
        // 0    1               1s
        // *    *               0*
        int check = show_bits(&s->gb, 2);
        i = 0;
        if ( check & 0x2 ){
            skip_bits(&s->gb, 2);
            block[0] = ( check & 0x1 ) ? -1 : 1;
            i = 1;
        }
    }else{
        i = 0;
    }
    if(!coded){
        s->block_last_index[n] = i - 1;
        return 0;
    }
    for(;;){
        code = get_vlc2(&s->gb, rl->vlc.table, TCOEFF_VLC_BITS, 2);
        if (code < 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal ac vlc code at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        if (code == rl->n) {
            /* escape */
            // The remaining combinations of (run, level) are encoded with a 20-bit word consisting of 6 bits escape, 6 bits run and 8 bits level.
            run = get_bits(&s->gb, 6);
            level = get_sbits(&s->gb, 8);
        }else if(code == 0){
            break;
        }else{
            run = rl->table_run[code];
            level = rl->table_level[code];
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64){
            av_log(s->avctx, AV_LOG_ERROR, "run overflow at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        j = scan_table[i];
        block[j] = level;
        i++;
    }
    s->block_last_index[n] = i-1;
    return 0;
}

/**
 * decodes the H261 picture header.
 * @return <0 if no startcode found
 */
int h261_decode_picture_header(H261Context *h){
    MpegEncContext * const s = &h->s;
    int format, i;
    uint32_t startcode= 0;

    for(i= s->gb.size_in_bits - get_bits_count(&s->gb); i>24; i-=1){
        startcode = ((startcode << 1) | get_bits(&s->gb, 1)) & 0x000FFFFF;

        if(startcode == 0x10)
            break;
    }

    if (startcode != 0x10){
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }

    /* temporal reference */
    s->picture_number = get_bits(&s->gb, 5); /* picture timestamp */

    /* PTYPE starts here */
    skip_bits1(&s->gb); /* split screen off */
    skip_bits1(&s->gb); /* camera  off */
    skip_bits1(&s->gb); /* freeze picture release off */

    format = get_bits1(&s->gb);

    //only 2 formats possible
    if (format == 0){//QCIF
        s->width = 176;
        s->height = 144;
        s->mb_width = 11;
        s->mb_height = 9;
    }else{//CIF
        s->width = 352;
        s->height = 288;
        s->mb_width = 22;
        s->mb_height = 18;
    }

    s->mb_num = s->mb_width * s->mb_height;

    skip_bits1(&s->gb); /* still image mode off */
    skip_bits1(&s->gb); /* Reserved */

    /* PEI */
    while (get_bits1(&s->gb) != 0){
        skip_bits(&s->gb, 8);
    }

    // h261 has no I-FRAMES, but if we pass I_TYPE for the first frame, the codec crashes if it does 
    // not contain all I-blocks (e.g. when a packet is lost)
    s->pict_type = P_TYPE;

    h->gob_number = 0;
    return 0;
}

static int h261_decode_gob(H261Context *h){
    MpegEncContext * const s = &h->s;
    
    ff_set_qscale(s, s->qscale);

    /* decode mb's */
    while(h->current_mba <= MBA_STUFFING)
    {
        int ret;
        /* DCT & quantize */
        ret= h261_decode_mb(h);
        if(ret<0){
            if(ret==SLICE_END){
                h261_decode_mb_skipped(h, h->current_mba, 33);                
                return 0;
            }
            av_log(s->avctx, AV_LOG_ERROR, "Error at MB: %d\n", s->mb_x + s->mb_y*s->mb_stride);
            return -1;
        }
        
        h261_decode_mb_skipped(h, h->current_mba-h->mba_diff, h->current_mba-1);
    }
    
    return -1;
}

static int h261_find_frame_end(ParseContext *pc, AVCodecContext* avctx, const uint8_t *buf, int buf_size){
    int vop_found, i, j;
    uint32_t state;

    vop_found= pc->frame_start_found;
    state= pc->state;
   
    for(i=0; i<buf_size && !vop_found; i++){
        state= (state<<8) | buf[i];
        for(j=0; j<8; j++){
            if(((state>>j)&0xFFFFF) == 0x00010){
                i++;
                vop_found=1;
                break;
            }
        }
    }
    if(vop_found){
        for(; i<buf_size; i++){
            state= (state<<8) | buf[i];
            for(j=0; j<8; j++){
                if(((state>>j)&0xFFFFF) == 0x00010){
                    pc->frame_start_found=0;
                    pc->state= state>>(2*8);
                    return i-1;
                }
            }
        }
    }

    pc->frame_start_found= vop_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int h261_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      uint8_t **poutbuf, int *poutbuf_size, 
                      const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;
    
    next= h261_find_frame_end(pc,avctx, buf, buf_size);
    if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

/**
 * returns the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size){
    int pos= get_bits_count(&s->gb)>>3;
    if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
    if(pos+10>buf_size) pos=buf_size; // oops ;)

    return pos;
}

static int h261_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    H261Context *h= avctx->priv_data;
    MpegEncContext *s = &h->s;
    int ret;
    AVFrame *pict = data;

#ifdef DEBUG
    printf("*****frame %d size=%d\n", avctx->frame_number, buf_size);
    printf("bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif
    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

    h->gob_start_code_skipped=0;

retry:

    init_get_bits(&s->gb, buf, buf_size*8);

    if(!s->context_initialized){
        if (MPV_common_init(s) < 0) //we need the idct permutaton for reading a custom matrix
            return -1;
    }

    //we need to set current_picture_ptr before reading the header, otherwise we cant store anyting im there
    if(s->current_picture_ptr==NULL || s->current_picture_ptr->data[0]){
        int i= ff_find_unused_picture(s, 0);
        s->current_picture_ptr= &s->picture[i];
    }

    ret = h261_decode_picture_header(h);

    /* skip if the header was thrashed */
    if (ret < 0){
        av_log(s->avctx, AV_LOG_ERROR, "header damaged\n");
        return -1;
    }

    if (s->width != avctx->coded_width || s->height != avctx->coded_height){
        ParseContext pc= s->parse_context; //FIXME move these demuxng hack to avformat
        s->parse_context.buffer=0;
        MPV_common_end(s);
        s->parse_context= pc;
    }
    if (!s->context_initialized) {
        avcodec_set_dimensions(avctx, s->width, s->height);

        goto retry;
    }

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == I_TYPE;

    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) return get_consumed_bytes(s, buf_size);
    if(  (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type==B_TYPE)
       ||(avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type!=I_TYPE)
       || avctx->skip_frame >= AVDISCARD_ALL)
        return get_consumed_bytes(s, buf_size);

    if(MPV_frame_start(s, avctx) < 0)
        return -1;

    ff_er_frame_start(s);

    /* decode each macroblock */
    s->mb_x=0;
    s->mb_y=0;

    while(h->gob_number < (s->mb_height==18 ? 12 : 5)){
        if(ff_h261_resync(h)<0)
            break;
        h261_decode_gob(h);
    }
    MPV_frame_end(s);

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    *pict= *(AVFrame*)s->current_picture_ptr;
    ff_print_debug_info(s, pict);

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    *data_size = sizeof(AVFrame);

    return get_consumed_bytes(s, buf_size);
}

static int h261_decode_end(AVCodecContext *avctx)
{
    H261Context *h= avctx->priv_data;
    MpegEncContext *s = &h->s;

    MPV_common_end(s);
    return 0;
}

#ifdef CONFIG_ENCODERS
AVCodec h261_encoder = {
    "h261",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H261,
    sizeof(H261Context),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};
#endif

AVCodec h261_decoder = {
    "h261",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H261,
    sizeof(H261Context),
    h261_decode_init,
    NULL,
    h261_decode_end,
    h261_decode_frame,
    CODEC_CAP_DR1,
};

AVCodecParser h261_parser = {
    { CODEC_ID_H261 },
    sizeof(ParseContext),
    NULL,
    h261_parse,
    ff_parse_close,
};
