/*
 * H261 encoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * H.261 encoder.
 */

#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h261.h"
#include "h261data.h"

extern uint8_t ff_h261_rl_table_store[2][2*MAX_RUN + MAX_LEVEL + 3];

static void h261_encode_block(H261Context * h, DCTELEM * block,
                              int n);

int ff_h261_get_picture_format(int width, int height){
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

void ff_h261_encode_picture_header(MpegEncContext * s, int picture_number){
    H261Context * h = (H261Context *) s;
    int format, temp_ref;

    avpriv_align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = put_bits_ptr(&s->pb);

    put_bits(&s->pb, 20, 0x10); /* PSC */

    temp_ref= s->picture_number * (int64_t)30000 * s->avctx->time_base.num /
                         (1001 * (int64_t)s->avctx->time_base.den); //FIXME maybe this should use a timestamp
    put_sbits(&s->pb, 5, temp_ref); /* TemporalReference */

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
 * Encode a group of blocks header.
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
        assert(cbp>0);
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
        init_rl(&h261_rl_tcoeff, ff_h261_rl_table_store);
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
    int level, run, i, j, last_index, last_non_zero, sign, slevel, code;
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
                put_sbits(&s->pb, 8, slevel);
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

AVCodec ff_h261_encoder = {
    .name           = "h261",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H261,
    .priv_data_size = sizeof(H261Context),
    .init           = MPV_encode_init,
    .encode         = MPV_encode_picture,
    .close          = MPV_encode_end,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_NONE},
    .long_name= NULL_IF_CONFIG_SMALL("H.261"),
};

