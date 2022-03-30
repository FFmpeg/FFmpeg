/*
 * MSMPEG4 encoder backend
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
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
 * MSMPEG4 encoder backend
 */

#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avutil.h"
#include "libavutil/thread.h"
#include "codec_internal.h"
#include "mpegvideo.h"
#include "mpegvideoenc.h"
#include "h263.h"
#include "h263data.h"
#include "mpeg4video.h"
#include "msmpeg4.h"
#include "msmpeg4data.h"
#include "msmpeg4enc.h"
#include "put_bits.h"
#include "rl.h"

static uint8_t rl_length[NB_RL_TABLES][MAX_LEVEL+1][MAX_RUN+1][2];

/* build the table which associate a (x,y) motion vector to a vlc */
static av_cold void init_mv_table(MVTable *tab, uint16_t table_mv_index[4096])
{
    int i, x, y;

    tab->table_mv_index = table_mv_index;

    /* mark all entries as not used */
    for(i=0;i<4096;i++)
        tab->table_mv_index[i] = MSMPEG4_MV_TABLES_NB_ELEMS;

    for (i = 0; i < MSMPEG4_MV_TABLES_NB_ELEMS; i++) {
        x = tab->table_mvx[i];
        y = tab->table_mvy[i];
        tab->table_mv_index[(x << 6) | y] = i;
    }
}

void ff_msmpeg4_code012(PutBitContext *pb, int n)
{
    if (n == 0) {
        put_bits(pb, 1, 0);
    } else {
        put_bits(pb, 1, 1);
        put_bits(pb, 1, (n >= 2));
    }
}

static int get_size_of_code(const RLTable *rl, int last, int run,
                            int level, int intra)
{
    int size=0;
    int code;
    int run_diff= intra ? 0 : 1;

    code = get_rl_index(rl, last, run, level);
    size+= rl->table_vlc[code][1];
    if (code == rl->n) {
        int level1, run1;

        level1 = level - rl->max_level[last][run];
        if (level1 < 1)
            goto esc2;
        code = get_rl_index(rl, last, run, level1);
        if (code == rl->n) {
            esc2:
            size++;
            if (level > MAX_LEVEL)
                goto esc3;
            run1 = run - rl->max_run[last][level] - run_diff;
            if (run1 < 0)
                goto esc3;
            code = get_rl_index(rl, last, run1, level);
            if (code == rl->n) {
            esc3:
                /* third escape */
                size+=1+1+6+8;
            } else {
                /* second escape */
                size+= 1+1+ rl->table_vlc[code][1];
            }
        } else {
            /* first escape */
            size+= 1+1+ rl->table_vlc[code][1];
        }
    } else {
        size++;
    }
    return size;
}

static av_cold void msmpeg4_encode_init_static(void)
{
    static uint16_t mv_index_tables[2][4096];
    init_mv_table(&ff_mv_tables[0], mv_index_tables[0]);
    init_mv_table(&ff_mv_tables[1], mv_index_tables[1]);

    for (int i = 0; i < NB_RL_TABLES; i++) {
        for (int level = 1; level <= MAX_LEVEL; level++) {
            for (int run = 0; run <= MAX_RUN; run++) {
                for (int last = 0; last < 2; last++) {
                    rl_length[i][level][run][last] = get_size_of_code(&ff_rl_table[i], last, run, level, 0);
                }
            }
        }
    }
}

av_cold void ff_msmpeg4_encode_init(MpegEncContext *s)
{
    static AVOnce init_static_once = AV_ONCE_INIT;

    ff_msmpeg4_common_init(s);
    if (s->msmpeg4_version >= 4) {
        s->min_qcoeff = -255;
        s->max_qcoeff =  255;
    }

    /* init various encoding tables */
    ff_thread_once(&init_static_once, msmpeg4_encode_init_static);
}

static void find_best_tables(MSMPEG4EncContext *ms)
{
    MpegEncContext *const s = &ms->s;
    int i;
    int best        = 0, best_size        = INT_MAX;
    int chroma_best = 0, best_chroma_size = INT_MAX;

    for(i=0; i<3; i++){
        int level;
        int chroma_size=0;
        int size=0;

        if(i>0){// ;)
            size++;
            chroma_size++;
        }
        for(level=0; level<=MAX_LEVEL; level++){
            int run;
            for(run=0; run<=MAX_RUN; run++){
                int last;
                const int last_size= size + chroma_size;
                for(last=0; last<2; last++){
                    int inter_count       = ms->ac_stats[0][0][level][run][last] + ms->ac_stats[0][1][level][run][last];
                    int intra_luma_count  = ms->ac_stats[1][0][level][run][last];
                    int intra_chroma_count= ms->ac_stats[1][1][level][run][last];

                    if(s->pict_type==AV_PICTURE_TYPE_I){
                        size       += intra_luma_count  *rl_length[i  ][level][run][last];
                        chroma_size+= intra_chroma_count*rl_length[i+3][level][run][last];
                    }else{
                        size+=        intra_luma_count  *rl_length[i  ][level][run][last]
                                     +intra_chroma_count*rl_length[i+3][level][run][last]
                                     +inter_count       *rl_length[i+3][level][run][last];
                    }
                }
                if(last_size == size+chroma_size) break;
            }
        }
        if(size<best_size){
            best_size= size;
            best= i;
        }
        if(chroma_size<best_chroma_size){
            best_chroma_size= chroma_size;
            chroma_best= i;
        }
    }

    if(s->pict_type==AV_PICTURE_TYPE_P) chroma_best= best;

    memset(ms->ac_stats, 0, sizeof(ms->ac_stats));

    s->rl_table_index       =        best;
    s->rl_chroma_table_index= chroma_best;

    if(s->pict_type != s->last_non_b_pict_type){
        s->rl_table_index= 2;
        if(s->pict_type==AV_PICTURE_TYPE_I)
            s->rl_chroma_table_index= 1;
        else
            s->rl_chroma_table_index= 2;
    }

}

/* write MSMPEG4 compatible frame header */
void ff_msmpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    MSMPEG4EncContext *const ms = (MSMPEG4EncContext*)s;

    find_best_tables(ms);

    align_put_bits(&s->pb);
    put_bits(&s->pb, 2, s->pict_type - 1);

    put_bits(&s->pb, 5, s->qscale);
    if(s->msmpeg4_version<=2){
        s->rl_table_index = 2;
        s->rl_chroma_table_index = 2;
    }

    s->dc_table_index = 1;
    s->mv_table_index = 1; /* only if P-frame */
    s->use_skip_mb_code = 1; /* only if P-frame */
    s->per_mb_rl_table = 0;
    if(s->msmpeg4_version==4)
        s->inter_intra_pred= (s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE && s->pict_type==AV_PICTURE_TYPE_P);
    ff_dlog(s, "%d %"PRId64" %d %d %d\n", s->pict_type, s->bit_rate,
            s->inter_intra_pred, s->width, s->height);

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        s->slice_height= s->mb_height/1;
        put_bits(&s->pb, 5, 0x16 + s->mb_height/s->slice_height);

        if(s->msmpeg4_version==4){
            ff_msmpeg4_encode_ext_header(s);
            if(s->bit_rate>MBAC_BITRATE)
                put_bits(&s->pb, 1, s->per_mb_rl_table);
        }

        if(s->msmpeg4_version>2){
            if(!s->per_mb_rl_table){
                ff_msmpeg4_code012(&s->pb, s->rl_chroma_table_index);
                ff_msmpeg4_code012(&s->pb, s->rl_table_index);
            }

            put_bits(&s->pb, 1, s->dc_table_index);
        }
    } else {
        put_bits(&s->pb, 1, s->use_skip_mb_code);

        if(s->msmpeg4_version==4 && s->bit_rate>MBAC_BITRATE)
            put_bits(&s->pb, 1, s->per_mb_rl_table);

        if(s->msmpeg4_version>2){
            if(!s->per_mb_rl_table)
                ff_msmpeg4_code012(&s->pb, s->rl_table_index);

            put_bits(&s->pb, 1, s->dc_table_index);

            put_bits(&s->pb, 1, s->mv_table_index);
        }
    }

    s->esc3_level_length= 0;
    s->esc3_run_length= 0;
}

void ff_msmpeg4_encode_ext_header(MpegEncContext * s)
{
        unsigned fps = s->avctx->time_base.den / s->avctx->time_base.num / FFMAX(s->avctx->ticks_per_frame, 1);
        put_bits(&s->pb, 5, FFMIN(fps, 31)); //yes 29.97 -> 29

        put_bits(&s->pb, 11, FFMIN(s->bit_rate/1024, 2047));

        if(s->msmpeg4_version>=3)
            put_bits(&s->pb, 1, s->flipflop_rounding);
        else
            av_assert0(s->flipflop_rounding==0);
}

void ff_msmpeg4_encode_motion(MpegEncContext * s,
                                  int mx, int my)
{
    int code;
    MVTable *mv;

    /* modulo encoding */
    /* WARNING : you cannot reach all the MVs even with the modulo
       encoding. This is a somewhat strange compromise they took !!!  */
    if (mx <= -64)
        mx += 64;
    else if (mx >= 64)
        mx -= 64;
    if (my <= -64)
        my += 64;
    else if (my >= 64)
        my -= 64;

    mx += 32;
    my += 32;
    mv = &ff_mv_tables[s->mv_table_index];

    code = mv->table_mv_index[(mx << 6) | my];
    put_bits(&s->pb,
             mv->table_mv_bits[code],
             mv->table_mv_code[code]);
    if (code == MSMPEG4_MV_TABLES_NB_ELEMS) {
        /* escape : code literally */
        put_bits(&s->pb, 6, mx);
        put_bits(&s->pb, 6, my);
    }
}

void ff_msmpeg4_handle_slices(MpegEncContext *s){
    if (s->mb_x == 0) {
        if (s->slice_height && (s->mb_y % s->slice_height) == 0) {
            if(s->msmpeg4_version < 4){
                ff_mpeg4_clean_buffers(s);
            }
            s->first_slice_line = 1;
        } else {
            s->first_slice_line = 0;
        }
    }
}

static void msmpeg4v2_encode_motion(MpegEncContext * s, int val)
{
    int range, bit_size, sign, code, bits;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb, ff_mvtab[code][1], ff_mvtab[code][0]);
    } else {
        bit_size = s->f_code - 1;
        range = 1 << bit_size;
        if (val <= -64)
            val += 64;
        else if (val >= 64)
            val -= 64;

        if (val >= 0) {
            sign = 0;
        } else {
            val = -val;
            sign = 1;
        }
        val--;
        code = (val >> bit_size) + 1;
        bits = val & (range - 1);

        put_bits(&s->pb, ff_mvtab[code][1] + 1, (ff_mvtab[code][0] << 1) | sign);
        if (bit_size > 0) {
            put_bits(&s->pb, bit_size, bits);
        }
    }
}

void ff_msmpeg4_encode_mb(MpegEncContext * s,
                          int16_t block[6][64],
                          int motion_x, int motion_y)
{
    int cbp, coded_cbp, i;
    int pred_x, pred_y;
    uint8_t *coded_block;

    ff_msmpeg4_handle_slices(s);

    if (!s->mb_intra) {
        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
        if (s->use_skip_mb_code && (cbp | motion_x | motion_y) == 0) {
            /* skip macroblock */
            put_bits(&s->pb, 1, 1);
            s->last_bits++;
            s->misc_bits++;
            s->skip_count++;

            return;
        }
        if (s->use_skip_mb_code)
            put_bits(&s->pb, 1, 0);     /* mb coded */

        if(s->msmpeg4_version<=2){
            put_bits(&s->pb,
                     ff_v2_mb_type[cbp&3][1],
                     ff_v2_mb_type[cbp&3][0]);
            if((cbp&3) != 3) coded_cbp= cbp ^ 0x3C;
            else             coded_cbp= cbp;

            put_bits(&s->pb,
                     ff_h263_cbpy_tab[coded_cbp>>2][1],
                     ff_h263_cbpy_tab[coded_cbp>>2][0]);

            s->misc_bits += get_bits_diff(s);

            ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            msmpeg4v2_encode_motion(s, motion_x - pred_x);
            msmpeg4v2_encode_motion(s, motion_y - pred_y);
        }else{
            put_bits(&s->pb,
                     ff_table_mb_non_intra[cbp + 64][1],
                     ff_table_mb_non_intra[cbp + 64][0]);

            s->misc_bits += get_bits_diff(s);

            /* motion vector */
            ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            ff_msmpeg4_encode_motion(s, motion_x - pred_x,
                                  motion_y - pred_y);
        }

        s->mv_bits += get_bits_diff(s);

        for (i = 0; i < 6; i++) {
            ff_msmpeg4_encode_block(s, block[i], i);
        }
        s->p_tex_bits += get_bits_diff(s);
    } else {
        /* compute cbp */
        cbp = 0;
        coded_cbp = 0;
        for (i = 0; i < 6; i++) {
            int val, pred;
            val = (s->block_last_index[i] >= 1);
            cbp |= val << (5 - i);
            if (i < 4) {
                /* predict value for close blocks only for luma */
                pred = ff_msmpeg4_coded_block_pred(s, i, &coded_block);
                *coded_block = val;
                val = val ^ pred;
            }
            coded_cbp |= val << (5 - i);
        }

        if(s->msmpeg4_version<=2){
            if (s->pict_type == AV_PICTURE_TYPE_I) {
                put_bits(&s->pb,
                         ff_v2_intra_cbpc[cbp&3][1], ff_v2_intra_cbpc[cbp&3][0]);
            } else {
                if (s->use_skip_mb_code)
                    put_bits(&s->pb, 1, 0);     /* mb coded */
                put_bits(&s->pb,
                         ff_v2_mb_type[(cbp&3) + 4][1],
                         ff_v2_mb_type[(cbp&3) + 4][0]);
            }
            put_bits(&s->pb, 1, 0);             /* no AC prediction yet */
            put_bits(&s->pb,
                     ff_h263_cbpy_tab[cbp>>2][1],
                     ff_h263_cbpy_tab[cbp>>2][0]);
        }else{
            if (s->pict_type == AV_PICTURE_TYPE_I) {
                put_bits(&s->pb,
                         ff_msmp4_mb_i_table[coded_cbp][1], ff_msmp4_mb_i_table[coded_cbp][0]);
            } else {
                if (s->use_skip_mb_code)
                    put_bits(&s->pb, 1, 0);     /* mb coded */
                put_bits(&s->pb,
                         ff_table_mb_non_intra[cbp][1],
                         ff_table_mb_non_intra[cbp][0]);
            }
            put_bits(&s->pb, 1, 0);             /* no AC prediction yet */
            if(s->inter_intra_pred){
                s->h263_aic_dir=0;
                put_bits(&s->pb, ff_table_inter_intra[s->h263_aic_dir][1], ff_table_inter_intra[s->h263_aic_dir][0]);
            }
        }
        s->misc_bits += get_bits_diff(s);

        for (i = 0; i < 6; i++) {
            ff_msmpeg4_encode_block(s, block[i], i);
        }
        s->i_tex_bits += get_bits_diff(s);
        s->i_count++;
    }
}

static void msmpeg4_encode_dc(MpegEncContext * s, int level, int n, int *dir_ptr)
{
    int sign, code;
    int pred;

    int16_t *dc_val;
    pred = ff_msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);

    /* update predictor */
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }

    /* do the prediction */
    level -= pred;

    if(s->msmpeg4_version<=2){
        if (n < 4) {
            put_bits(&s->pb,
                     ff_v2_dc_lum_table[level + 256][1],
                     ff_v2_dc_lum_table[level + 256][0]);
        }else{
            put_bits(&s->pb,
                     ff_v2_dc_chroma_table[level + 256][1],
                     ff_v2_dc_chroma_table[level + 256][0]);
        }
    }else{
        sign = 0;
        if (level < 0) {
            level = -level;
            sign = 1;
        }
        code = level;
        if (code > DC_MAX)
            code = DC_MAX;

        if (s->dc_table_index == 0) {
            if (n < 4) {
                put_bits(&s->pb, ff_table0_dc_lum[code][1], ff_table0_dc_lum[code][0]);
            } else {
                put_bits(&s->pb, ff_table0_dc_chroma[code][1], ff_table0_dc_chroma[code][0]);
            }
        } else {
            if (n < 4) {
                put_bits(&s->pb, ff_table1_dc_lum[code][1], ff_table1_dc_lum[code][0]);
            } else {
                put_bits(&s->pb, ff_table1_dc_chroma[code][1], ff_table1_dc_chroma[code][0]);
            }
        }

        if (code == DC_MAX)
            put_bits(&s->pb, 8, level);

        if (level != 0) {
            put_bits(&s->pb, 1, sign);
        }
    }
}

/* Encoding of a block; very similar to MPEG-4 except for a different
 * escape coding (same as H.263) and more VLC tables. */
void ff_msmpeg4_encode_block(MpegEncContext * s, int16_t * block, int n)
{
    MSMPEG4EncContext *const ms = (MSMPEG4EncContext*)s;
    int level, run, last, i, j, last_index;
    int last_non_zero, sign, slevel;
    int code, run_diff, dc_pred_dir;
    const RLTable *rl;
    const uint8_t *scantable;

    if (s->mb_intra) {
        msmpeg4_encode_dc(s, block[0], n, &dc_pred_dir);
        i = 1;
        if (n < 4) {
            rl = &ff_rl_table[s->rl_table_index];
        } else {
            rl = &ff_rl_table[3 + s->rl_chroma_table_index];
        }
        run_diff = s->msmpeg4_version>=4;
        scantable= s->intra_scantable.permutated;
    } else {
        i = 0;
        rl = &ff_rl_table[3 + s->rl_table_index];
        if(s->msmpeg4_version<=2)
            run_diff = 0;
        else
            run_diff = 1;
        scantable= s->inter_scantable.permutated;
    }

    /* recalculate block_last_index for M$ wmv1 */
    if (s->msmpeg4_version >= 4 && s->block_last_index[n] > 0) {
        for(last_index=63; last_index>=0; last_index--){
            if(block[scantable[last_index]]) break;
        }
        s->block_last_index[n]= last_index;
    }else
        last_index = s->block_last_index[n];
    /* AC coefs */
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
        j = scantable[i];
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

            if(level<=MAX_LEVEL && run<=MAX_RUN){
                ms->ac_stats[s->mb_intra][n>3][level][run][last]++;
            }

            ms->ac_stats[s->mb_intra][n > 3][40][63][0]++; //esc3 like

            code = get_rl_index(rl, last, run, level);
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
                int level1, run1;

                level1 = level - rl->max_level[last][run];
                if (level1 < 1)
                    goto esc2;
                code = get_rl_index(rl, last, run, level1);
                if (code == rl->n) {
                esc2:
                    put_bits(&s->pb, 1, 0);
                    if (level > MAX_LEVEL)
                        goto esc3;
                    run1 = run - rl->max_run[last][level] - run_diff;
                    if (run1 < 0)
                        goto esc3;
                    code = get_rl_index(rl, last, run1+1, level);
                    if (s->msmpeg4_version == 4 && code == rl->n)
                        goto esc3;
                    code = get_rl_index(rl, last, run1, level);
                    if (code == rl->n) {
                    esc3:
                        /* third escape */
                        put_bits(&s->pb, 1, 0);
                        put_bits(&s->pb, 1, last);
                        if(s->msmpeg4_version>=4){
                            if(s->esc3_level_length==0){
                                s->esc3_level_length=8;
                                s->esc3_run_length= 6;
                                //ESCLVLSZ + ESCRUNSZ
                                if(s->qscale<8)
                                    put_bits(&s->pb, 6, 3);
                                else
                                    put_bits(&s->pb, 8, 3);
                            }
                            put_bits(&s->pb, s->esc3_run_length, run);
                            put_bits(&s->pb, 1, sign);
                            put_bits(&s->pb, s->esc3_level_length, level);
                        }else{
                            put_bits(&s->pb, 6, run);
                            put_sbits(&s->pb, 8, slevel);
                        }
                    } else {
                        /* second escape */
                        put_bits(&s->pb, 1, 1);
                        put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                        put_bits(&s->pb, 1, sign);
                    }
                } else {
                    /* first escape */
                    put_bits(&s->pb, 1, 1);
                    put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                    put_bits(&s->pb, 1, sign);
                }
            } else {
                put_bits(&s->pb, 1, sign);
            }
            last_non_zero = i;
        }
    }
}

const FFCodec ff_msmpeg4v2_encoder = {
    .p.name         = "msmpeg4v2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V2,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .p.priv_class   = &ff_mpv_enc_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(MSMPEG4EncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
};

const FFCodec ff_msmpeg4v3_encoder = {
    .p.name         = "msmpeg4",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 3"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V3,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .p.priv_class   = &ff_mpv_enc_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(MSMPEG4EncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
};

const FFCodec ff_wmv1_encoder = {
    .p.name         = "wmv1",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Windows Media Video 7"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV1,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .p.priv_class   = &ff_mpv_enc_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(MSMPEG4EncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
};
