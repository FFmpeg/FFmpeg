/*
 * ITU H.263 bitstream decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * H.263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * H.263 decoder.
 */

#define UNCHECKED_BITSTREAM_READER 1
#include <limits.h>

#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem_internal.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h263data.h"
#include "internal.h"
#include "mathops.h"
#include "mpegutils.h"
#include "unary.h"
#include "flv.h"
#include "rv10.h"
#include "mpeg4video.h"
#include "mpegvideodata.h"

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define H263_MBTYPE_B_VLC_BITS 6
#define CBPC_B_VLC_BITS 3

static const int h263_mb_type_b_map[15]= {
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1,
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1 | MB_TYPE_CBP,
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1 | MB_TYPE_CBP | MB_TYPE_QUANT,
                      MB_TYPE_L0                                 | MB_TYPE_16x16,
                      MB_TYPE_L0   | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L0   | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_L1                                 | MB_TYPE_16x16,
                      MB_TYPE_L1   | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L1   | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_L0L1                               | MB_TYPE_16x16,
                      MB_TYPE_L0L1 | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L0L1 | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
    0, //stuffing
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP,
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP | MB_TYPE_QUANT,
};

void ff_h263_show_pict_info(MpegEncContext *s){
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
    av_log(s->avctx, AV_LOG_DEBUG, "qp:%d %c size:%d rnd:%d%s%s%s%s%s%s%s%s%s %d/%d\n",
         s->qscale, av_get_picture_type_char(s->pict_type),
         s->gb.size_in_bits, 1-s->no_rounding,
         s->obmc ? " AP" : "",
         s->umvplus ? " UMV" : "",
         s->h263_long_vectors ? " LONG" : "",
         s->h263_plus ? " +" : "",
         s->h263_aic ? " AIC" : "",
         s->alt_inter_vlc ? " AIV" : "",
         s->modified_quant ? " MQ" : "",
         s->loop_filter ? " LOOP" : "",
         s->h263_slice_structured ? " SS" : "",
         s->avctx->framerate.num, s->avctx->framerate.den
    );
    }
}

/***********************************************/
/* decoding */

VLC ff_h263_intra_MCBPC_vlc;
VLC ff_h263_inter_MCBPC_vlc;
VLC ff_h263_cbpy_vlc;
VLC ff_h263_mv_vlc;
static VLC h263_mbtype_b_vlc;
static VLC cbpc_b_vlc;

/* init vlcs */

/* XXX: find a better solution to handle static init */
av_cold void ff_h263_decode_init_vlc(void)
{
    static volatile int done = 0;

    if (!done) {
        INIT_VLC_STATIC(&ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 9,
                 ff_h263_intra_MCBPC_bits, 1, 1,
                 ff_h263_intra_MCBPC_code, 1, 1, 72);
        INIT_VLC_STATIC(&ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 28,
                 ff_h263_inter_MCBPC_bits, 1, 1,
                 ff_h263_inter_MCBPC_code, 1, 1, 198);
        INIT_VLC_STATIC(&ff_h263_cbpy_vlc, CBPY_VLC_BITS, 16,
                 &ff_h263_cbpy_tab[0][1], 2, 1,
                 &ff_h263_cbpy_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&ff_h263_mv_vlc, H263_MV_VLC_BITS, 33,
                 &ff_mvtab[0][1], 2, 1,
                 &ff_mvtab[0][0], 2, 1, 538);
        ff_h263_init_rl_inter();
        INIT_VLC_RL(ff_h263_rl_inter, 554);
        INIT_FIRST_VLC_RL(ff_rl_intra_aic, 554);
        INIT_VLC_STATIC(&h263_mbtype_b_vlc, H263_MBTYPE_B_VLC_BITS, 15,
                 &ff_h263_mbtype_b_tab[0][1], 2, 1,
                 &ff_h263_mbtype_b_tab[0][0], 2, 1, 80);
        INIT_VLC_STATIC(&cbpc_b_vlc, CBPC_B_VLC_BITS, 4,
                 &ff_cbpc_b_tab[0][1], 2, 1,
                 &ff_cbpc_b_tab[0][0], 2, 1, 8);
        done = 1;
    }
}

int ff_h263_decode_mba(MpegEncContext *s)
{
    int i, mb_pos;

    for (i = 0; i < 6; i++)
        if (s->mb_num - 1 <= ff_mba_max[i])
            break;
    mb_pos  = get_bits(&s->gb, ff_mba_length[i]);
    s->mb_x = mb_pos % s->mb_width;
    s->mb_y = mb_pos / s->mb_width;

    return mb_pos;
}

/**
 * Decode the group of blocks header or slice header.
 * @return <0 if an error occurred
 */
static int h263_decode_gob_header(MpegEncContext *s)
{
    unsigned int val, gob_number;
    int left;

    /* Check for GOB Start Code */
    val = show_bits(&s->gb, 16);
    if(val)
        return -1;

        /* We have a GBSC probably with GSTUFF */
    skip_bits(&s->gb, 16); /* Drop the zeros */
    left= get_bits_left(&s->gb);
    left = FFMIN(left, 32);
    //MN: we must check the bits left or we might end in an infinite loop (or segfault)
    for(;left>13; left--){
        if(get_bits1(&s->gb)) break; /* Seek the '1' bit */
    }
    if(left<=13)
        return -1;

    if(s->h263_slice_structured){
        if(check_marker(s->avctx, &s->gb, "before MBA")==0)
            return -1;

        ff_h263_decode_mba(s);

        if(s->mb_num > 1583)
            if(check_marker(s->avctx, &s->gb, "after MBA")==0)
                return -1;

        s->qscale = get_bits(&s->gb, 5); /* SQUANT */
        if(check_marker(s->avctx, &s->gb, "after SQUANT")==0)
            return -1;
        skip_bits(&s->gb, 2); /* GFID */
    }else{
        gob_number = get_bits(&s->gb, 5); /* GN */
        s->mb_x= 0;
        s->mb_y= s->gob_index* gob_number;
        skip_bits(&s->gb, 2); /* GFID */
        s->qscale = get_bits(&s->gb, 5); /* GQUANT */
    }

    if(s->mb_y >= s->mb_height)
        return -1;

    if(s->qscale==0)
        return -1;

    return 0;
}

/**
 * Decode the group of blocks / video packet header / slice header (MPEG-4 Studio).
 * @return bit position of the resync_marker, or <0 if none was found
 */
int ff_h263_resync(MpegEncContext *s){
    int left, pos, ret;

    /* In MPEG-4 studio mode look for a new slice startcode
     * and decode slice header */
    if(s->codec_id==AV_CODEC_ID_MPEG4 && s->studio_profile) {
        align_get_bits(&s->gb);

        while (get_bits_left(&s->gb) >= 32 && show_bits_long(&s->gb, 32) != SLICE_STARTCODE) {
            get_bits(&s->gb, 8);
        }

        if (get_bits_left(&s->gb) >= 32 && show_bits_long(&s->gb, 32) == SLICE_STARTCODE)
            return get_bits_count(&s->gb);
        else
            return -1;
    }

    if(s->codec_id==AV_CODEC_ID_MPEG4){
        skip_bits1(&s->gb);
        align_get_bits(&s->gb);
    }

    if(show_bits(&s->gb, 16)==0){
        pos= get_bits_count(&s->gb);
        if(CONFIG_MPEG4_DECODER && s->codec_id==AV_CODEC_ID_MPEG4)
            ret= ff_mpeg4_decode_video_packet_header(s->avctx->priv_data);
        else
            ret= h263_decode_gob_header(s);
        if(ret>=0)
            return pos;
    }
    //OK, it's not where it is supposed to be ...
    s->gb= s->last_resync_gb;
    align_get_bits(&s->gb);
    left= get_bits_left(&s->gb);

    for(;left>16+1+5+5; left-=8){
        if(show_bits(&s->gb, 16)==0){
            GetBitContext bak= s->gb;

            pos= get_bits_count(&s->gb);
            if(CONFIG_MPEG4_DECODER && s->codec_id==AV_CODEC_ID_MPEG4)
                ret= ff_mpeg4_decode_video_packet_header(s->avctx->priv_data);
            else
                ret= h263_decode_gob_header(s);
            if(ret>=0)
                return pos;

            s->gb= bak;
        }
        skip_bits(&s->gb, 8);
    }

    return -1;
}

int ff_h263_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift;
    code = get_vlc2(&s->gb, ff_h263_mv_vlc.table, H263_MV_VLC_BITS, 2);

    if (code == 0)
        return pred;
    if (code < 0)
        return 0xffff;

    sign = get_bits1(&s->gb);
    shift = f_code - 1;
    val = code;
    if (shift) {
        val = (val - 1) << shift;
        val |= get_bits(&s->gb, shift);
        val++;
    }
    if (sign)
        val = -val;
    val += pred;

    /* modulo decoding */
    if (!s->h263_long_vectors) {
        val = sign_extend(val, 5 + f_code);
    } else {
        /* horrible H.263 long vector mode */
        if (pred < -31 && val < -63)
            val += 64;
        if (pred > 32 && val > 63)
            val -= 64;

    }
    return val;
}


/* Decode RVLC of H.263+ UMV */
static int h263p_decode_umotion(MpegEncContext * s, int pred)
{
   int code = 0, sign;

   if (get_bits1(&s->gb)) /* Motion difference = 0 */
      return pred;

   code = 2 + get_bits1(&s->gb);

   while (get_bits1(&s->gb))
   {
      code <<= 1;
      code += get_bits1(&s->gb);
      if (code >= 32768) {
          avpriv_request_sample(s->avctx, "Huge DMV");
          return 0xffff;
      }
   }
   sign = code & 1;
   code >>= 1;

   code = (sign) ? (pred - code) : (pred + code);
   ff_tlog(s->avctx,"H.263+ UMV Motion = %d\n", code);
   return code;

}

/**
 * read the next MVs for OBMC. yes this is an ugly hack, feel free to send a patch :)
 */
static void preview_obmc(MpegEncContext *s){
    GetBitContext gb= s->gb;

    int cbpc, i, pred_x, pred_y, mx, my;
    int16_t *mot_val;
    const int xy= s->mb_x + 1 + s->mb_y * s->mb_stride;
    const int stride= s->b8_stride*2;

    for(i=0; i<4; i++)
        s->block_index[i]+= 2;
    for(i=4; i<6; i++)
        s->block_index[i]+= 1;
    s->mb_x++;

    av_assert2(s->pict_type == AV_PICTURE_TYPE_P);

    do{
        if (get_bits1(&s->gb)) {
            /* skip mb */
            mot_val = s->current_picture.motion_val[0][s->block_index[0]];
            mot_val[0       ]= mot_val[2       ]=
            mot_val[0+stride]= mot_val[2+stride]= 0;
            mot_val[1       ]= mot_val[3       ]=
            mot_val[1+stride]= mot_val[3+stride]= 0;

            s->current_picture.mb_type[xy] = MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
            goto end;
        }
        cbpc = get_vlc2(&s->gb, ff_h263_inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
    }while(cbpc == 20);

    if(cbpc & 4){
        s->current_picture.mb_type[xy] = MB_TYPE_INTRA;
    }else{
        get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if (cbpc & 8) {
            if(s->modified_quant){
                if(get_bits1(&s->gb)) skip_bits(&s->gb, 1);
                else                  skip_bits(&s->gb, 5);
            }else
                skip_bits(&s->gb, 2);
        }

        if ((cbpc & 16) == 0) {
                s->current_picture.mb_type[xy] = MB_TYPE_16x16 | MB_TYPE_L0;
                /* 16x16 motion prediction */
                mot_val= ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                if (s->umvplus)
                    mx = h263p_decode_umotion(s, pred_x);
                else
                    mx = ff_h263_decode_motion(s, pred_x, 1);

                if (s->umvplus)
                    my = h263p_decode_umotion(s, pred_y);
                else
                    my = ff_h263_decode_motion(s, pred_y, 1);

                mot_val[0       ]= mot_val[2       ]=
                mot_val[0+stride]= mot_val[2+stride]= mx;
                mot_val[1       ]= mot_val[3       ]=
                mot_val[1+stride]= mot_val[3+stride]= my;
        } else {
            s->current_picture.mb_type[xy] = MB_TYPE_8x8 | MB_TYPE_L0;
            for(i=0;i<4;i++) {
                mot_val = ff_h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                if (s->umvplus)
                    mx = h263p_decode_umotion(s, pred_x);
                else
                    mx = ff_h263_decode_motion(s, pred_x, 1);

                if (s->umvplus)
                    my = h263p_decode_umotion(s, pred_y);
                else
                    my = ff_h263_decode_motion(s, pred_y, 1);
                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    }
end:

    for(i=0; i<4; i++)
        s->block_index[i]-= 2;
    for(i=4; i<6; i++)
        s->block_index[i]-= 1;
    s->mb_x--;

    s->gb= gb;
}

static void h263_decode_dquant(MpegEncContext *s){
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    if(s->modified_quant){
        if(get_bits1(&s->gb))
            s->qscale= ff_modified_quant_tab[get_bits1(&s->gb)][ s->qscale ];
        else
            s->qscale= get_bits(&s->gb, 5);
    }else
        s->qscale += quant_tab[get_bits(&s->gb, 2)];
    ff_set_qscale(s, s->qscale);
}

static int h263_decode_block(MpegEncContext * s, int16_t * block,
                             int n, int coded)
{
    int level, i, j, run;
    RLTable *rl = &ff_h263_rl_inter;
    const uint8_t *scan_table;
    GetBitContext gb= s->gb;

    scan_table = s->intra_scantable.permutated;
    if (s->h263_aic && s->mb_intra) {
        rl = &ff_rl_intra_aic;
        i = 0;
        if (s->ac_pred) {
            if (s->h263_aic_dir)
                scan_table = s->intra_v_scantable.permutated; /* left */
            else
                scan_table = s->intra_h_scantable.permutated; /* top */
        }
    } else if (s->mb_intra) {
        /* DC coef */
        if (CONFIG_RV10_DECODER && s->codec_id == AV_CODEC_ID_RV10) {
          if (s->rv10_version == 3 && s->pict_type == AV_PICTURE_TYPE_I) {
            int component, diff;
            component = (n <= 3 ? 0 : n - 4 + 1);
            level = s->last_dc[component];
            if (s->rv10_first_dc_coded[component]) {
                diff = ff_rv_decode_dc(s, n);
                if (diff < 0)
                    return -1;
                level += diff;
                level = level & 0xff; /* handle wrap round */
                s->last_dc[component] = level;
            } else {
                s->rv10_first_dc_coded[component] = 1;
            }
          } else {
                level = get_bits(&s->gb, 8);
                if (level == 255)
                    level = 128;
          }
        }else{
            level = get_bits(&s->gb, 8);
            if((level&0x7F) == 0){
                av_log(s->avctx, AV_LOG_ERROR, "illegal dc %d at %d %d\n", level, s->mb_x, s->mb_y);
                if (s->avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT))
                    return -1;
            }
            if (level == 255)
                level = 128;
        }
        block[0] = level;
        i = 1;
    } else {
        i = 0;
    }
    if (!coded) {
        if (s->mb_intra && s->h263_aic)
            goto not_coded;
        s->block_last_index[n] = i - 1;
        return 0;
    }
retry:
    {
    OPEN_READER(re, &s->gb);
    i--; // offset by -1 to allow direct indexing of scan_table
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_RL_VLC(level, run, re, &s->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);
        if (run == 66) {
            if (level){
                CLOSE_READER(re, &s->gb);
                av_log(s->avctx, AV_LOG_ERROR, "illegal ac vlc code at %dx%d\n", s->mb_x, s->mb_y);
                return -1;
            }
            /* escape */
            if (CONFIG_FLV_DECODER && s->h263_flv > 1) {
                int is11 = SHOW_UBITS(re, &s->gb, 1);
                SKIP_CACHE(re, &s->gb, 1);
                run = SHOW_UBITS(re, &s->gb, 7) + 1;
                if (is11) {
                    SKIP_COUNTER(re, &s->gb, 1 + 7);
                    UPDATE_CACHE(re, &s->gb);
                    level = SHOW_SBITS(re, &s->gb, 11);
                    SKIP_COUNTER(re, &s->gb, 11);
                } else {
                    SKIP_CACHE(re, &s->gb, 7);
                    level = SHOW_SBITS(re, &s->gb, 7);
                    SKIP_COUNTER(re, &s->gb, 1 + 7 + 7);
                }
            } else {
                run = SHOW_UBITS(re, &s->gb, 7) + 1;
                SKIP_CACHE(re, &s->gb, 7);
                level = (int8_t)SHOW_UBITS(re, &s->gb, 8);
                SKIP_COUNTER(re, &s->gb, 7 + 8);
                if(level == -128){
                    UPDATE_CACHE(re, &s->gb);
                    if (s->codec_id == AV_CODEC_ID_RV10) {
                        /* XXX: should patch encoder too */
                        level = SHOW_SBITS(re, &s->gb, 12);
                        SKIP_COUNTER(re, &s->gb, 12);
                    }else{
                        level = SHOW_UBITS(re, &s->gb, 5);
                        SKIP_CACHE(re, &s->gb, 5);
                        level |= SHOW_SBITS(re, &s->gb, 6) * (1<<5);
                        SKIP_COUNTER(re, &s->gb, 5 + 6);
                    }
                }
            }
        } else {
            if (SHOW_UBITS(re, &s->gb, 1))
                level = -level;
            SKIP_COUNTER(re, &s->gb, 1);
        }
        i += run;
        if (i >= 64){
            CLOSE_READER(re, &s->gb);
            // redo update without last flag, revert -1 offset
            i = i - run + ((run-1)&63) + 1;
            if (i < 64) {
                // only last marker, no overrun
                block[scan_table[i]] = level;
                break;
            }
            if(s->alt_inter_vlc && rl == &ff_h263_rl_inter && !s->mb_intra){
                //Looks like a hack but no, it's the way it is supposed to work ...
                rl = &ff_rl_intra_aic;
                i = 0;
                s->gb= gb;
                s->bdsp.clear_block(block);
                goto retry;
            }
            av_log(s->avctx, AV_LOG_ERROR, "run overflow at %dx%d i:%d\n", s->mb_x, s->mb_y, s->mb_intra);
            return -1;
        }
        j = scan_table[i];
        block[j] = level;
    }
    }
not_coded:
    if (s->mb_intra && s->h263_aic) {
        ff_h263_pred_acdc(s, block, n);
        i = 63;
    }
    s->block_last_index[n] = i;
    return 0;
}

static int h263_skip_b_part(MpegEncContext *s, int cbp)
{
    LOCAL_ALIGNED_32(int16_t, dblock, [64]);
    int i, mbi;
    int bli[6];

    /* we have to set s->mb_intra to zero to decode B-part of PB-frame correctly
     * but real value should be restored in order to be used later (in OBMC condition)
     */
    mbi = s->mb_intra;
    memcpy(bli, s->block_last_index, sizeof(bli));
    s->mb_intra = 0;
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(s, dblock, i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }
    s->mb_intra = mbi;
    memcpy(s->block_last_index, bli, sizeof(bli));
    return 0;
}

static int h263_get_modb(GetBitContext *gb, int pb_frame, int *cbpb)
{
    int c, mv = 1;

    if (pb_frame < 3) { // h.263 Annex G and i263 PB-frame
        c = get_bits1(gb);
        if (pb_frame == 2 && c)
            mv = !get_bits1(gb);
    } else { // h.263 Annex M improved PB-frame
        mv = get_unary(gb, 0, 4) + 1;
        c = mv & 1;
        mv = !!(mv & 2);
    }
    if(c)
        *cbpb = get_bits(gb, 6);
    return mv;
}

#define tab_size ((signed)FF_ARRAY_ELEMS(s->direct_scale_mv[0]))
#define tab_bias (tab_size / 2)
static inline void set_one_direct_mv(MpegEncContext *s, Picture *p, int i)
{
    int xy           = s->block_index[i];
    uint16_t time_pp = s->pp_time;
    uint16_t time_pb = s->pb_time;
    int p_mx, p_my;

    p_mx = p->motion_val[0][xy][0];
    if ((unsigned)(p_mx + tab_bias) < tab_size) {
        s->mv[0][i][0] = s->direct_scale_mv[0][p_mx + tab_bias];
        s->mv[1][i][0] = s->direct_scale_mv[1][p_mx + tab_bias];
    } else {
        s->mv[0][i][0] = p_mx * time_pb / time_pp;
        s->mv[1][i][0] = p_mx * (time_pb - time_pp) / time_pp;
    }
    p_my = p->motion_val[0][xy][1];
    if ((unsigned)(p_my + tab_bias) < tab_size) {
        s->mv[0][i][1] = s->direct_scale_mv[0][p_my + tab_bias];
        s->mv[1][i][1] = s->direct_scale_mv[1][p_my + tab_bias];
    } else {
        s->mv[0][i][1] = p_my * time_pb / time_pp;
        s->mv[1][i][1] = p_my * (time_pb - time_pp) / time_pp;
    }
}

/**
 * @return the mb_type
 */
static int set_direct_mv(MpegEncContext *s)
{
    const int mb_index = s->mb_x + s->mb_y * s->mb_stride;
    Picture *p = &s->next_picture;
    int colocated_mb_type = p->mb_type[mb_index];
    int i;

    if (s->codec_tag == AV_RL32("U263") && p->f->pict_type == AV_PICTURE_TYPE_I) {
        p = &s->last_picture;
        colocated_mb_type = p->mb_type[mb_index];
    }

    if (IS_8X8(colocated_mb_type)) {
        s->mv_type = MV_TYPE_8X8;
        for (i = 0; i < 4; i++)
            set_one_direct_mv(s, p, i);
        return MB_TYPE_DIRECT2 | MB_TYPE_8x8 | MB_TYPE_L0L1;
    } else {
        set_one_direct_mv(s, p, 0);
        s->mv[0][1][0] =
        s->mv[0][2][0] =
        s->mv[0][3][0] = s->mv[0][0][0];
        s->mv[0][1][1] =
        s->mv[0][2][1] =
        s->mv[0][3][1] = s->mv[0][0][1];
        s->mv[1][1][0] =
        s->mv[1][2][0] =
        s->mv[1][3][0] = s->mv[1][0][0];
        s->mv[1][1][1] =
        s->mv[1][2][1] =
        s->mv[1][3][1] = s->mv[1][0][1];
        s->mv_type = MV_TYPE_8X8;
        // Note see prev line
        return MB_TYPE_DIRECT2 | MB_TYPE_16x16 | MB_TYPE_L0L1;
    }
}

int ff_h263_decode_mb(MpegEncContext *s,
                      int16_t block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    int16_t *mot_val;
    const int xy= s->mb_x + s->mb_y * s->mb_stride;
    int cbpb = 0, pb_mv_count = 0;

    av_assert2(!s->h263_pred);

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        do{
            if (get_bits1(&s->gb)) {
                /* skip mb */
                s->mb_intra = 0;
                for(i=0;i<6;i++)
                    s->block_last_index[i] = -1;
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                s->current_picture.mb_type[xy] = MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skipped = !(s->obmc | s->loop_filter);
                goto end;
            }
            cbpc = get_vlc2(&s->gb, ff_h263_inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(s->avctx, AV_LOG_ERROR, "cbpc damaged at %d %d\n", s->mb_x, s->mb_y);
                return SLICE_ERROR;
            }
        }while(cbpc == 20);

        s->bdsp.clear_blocks(s->block[0]);

        dquant = cbpc & 8;
        s->mb_intra = ((cbpc & 4) != 0);
        if (s->mb_intra) goto intra;

        if(s->pb_frame && get_bits1(&s->gb))
            pb_mv_count = h263_get_modb(&s->gb, s->pb_frame, &cbpb);
        cbpy = get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);

        if (cbpy < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
            return SLICE_ERROR;
        }

        if(s->alt_inter_vlc==0 || (cbpc & 3)!=3)
            cbpy ^= 0xF;

        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(s);
        }

        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            s->current_picture.mb_type[xy] = MB_TYPE_16x16 | MB_TYPE_L0;
            /* 16x16 motion prediction */
            s->mv_type = MV_TYPE_16X16;
            ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            if (s->umvplus)
               mx = h263p_decode_umotion(s, pred_x);
            else
               mx = ff_h263_decode_motion(s, pred_x, 1);

            if (mx >= 0xffff)
                return SLICE_ERROR;

            if (s->umvplus)
               my = h263p_decode_umotion(s, pred_y);
            else
               my = ff_h263_decode_motion(s, pred_y, 1);

            if (my >= 0xffff)
                return SLICE_ERROR;
            s->mv[0][0][0] = mx;
            s->mv[0][0][1] = my;

            if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
               skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
        } else {
            s->current_picture.mb_type[xy] = MB_TYPE_8x8 | MB_TYPE_L0;
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = ff_h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                if (s->umvplus)
                    mx = h263p_decode_umotion(s, pred_x);
                else
                    mx = ff_h263_decode_motion(s, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (s->umvplus)
                    my = h263p_decode_umotion(s, pred_y);
                else
                    my = ff_h263_decode_motion(s, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                  skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else if(s->pict_type==AV_PICTURE_TYPE_B) {
        int mb_type;
        const int stride= s->b8_stride;
        int16_t *mot_val0 = s->current_picture.motion_val[0][2 * (s->mb_x + s->mb_y * stride)];
        int16_t *mot_val1 = s->current_picture.motion_val[1][2 * (s->mb_x + s->mb_y * stride)];
//        const int mv_xy= s->mb_x + 1 + s->mb_y * s->mb_stride;

        //FIXME ugly
        mot_val0[0       ]= mot_val0[2       ]= mot_val0[0+2*stride]= mot_val0[2+2*stride]=
        mot_val0[1       ]= mot_val0[3       ]= mot_val0[1+2*stride]= mot_val0[3+2*stride]=
        mot_val1[0       ]= mot_val1[2       ]= mot_val1[0+2*stride]= mot_val1[2+2*stride]=
        mot_val1[1       ]= mot_val1[3       ]= mot_val1[1+2*stride]= mot_val1[3+2*stride]= 0;

        do{
            mb_type= get_vlc2(&s->gb, h263_mbtype_b_vlc.table, H263_MBTYPE_B_VLC_BITS, 2);
            if (mb_type < 0){
                av_log(s->avctx, AV_LOG_ERROR, "b mb_type damaged at %d %d\n", s->mb_x, s->mb_y);
                return SLICE_ERROR;
            }

            mb_type= h263_mb_type_b_map[ mb_type ];
        }while(!mb_type);

        s->mb_intra = IS_INTRA(mb_type);
        if(HAS_CBP(mb_type)){
            s->bdsp.clear_blocks(s->block[0]);
            cbpc = get_vlc2(&s->gb, cbpc_b_vlc.table, CBPC_B_VLC_BITS, 1);
            if(s->mb_intra){
                dquant = IS_QUANT(mb_type);
                goto intra;
            }

            cbpy = get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);

            if (cbpy < 0){
                av_log(s->avctx, AV_LOG_ERROR, "b cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
                return SLICE_ERROR;
            }

            if(s->alt_inter_vlc==0 || (cbpc & 3)!=3)
                cbpy ^= 0xF;

            cbp = (cbpc & 3) | (cbpy << 2);
        }else
            cbp=0;

        av_assert2(!s->mb_intra);

        if(IS_QUANT(mb_type)){
            h263_decode_dquant(s);
        }

        if(IS_DIRECT(mb_type)){
            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            mb_type |= set_direct_mv(s);
        }else{
            s->mv_dir = 0;
            s->mv_type= MV_TYPE_16X16;
//FIXME UMV

            if(USES_LIST(mb_type, 0)){
                int16_t *mot_val= ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                s->mv_dir = MV_DIR_FORWARD;

                if (s->umvplus)
                    mx = h263p_decode_umotion(s, pred_x);
                else
                    mx = ff_h263_decode_motion(s, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (s->umvplus)
                    my = h263p_decode_umotion(s, pred_y);
                else
                    my = ff_h263_decode_motion(s, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;

                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */

                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }

            if(USES_LIST(mb_type, 1)){
                int16_t *mot_val= ff_h263_pred_motion(s, 0, 1, &pred_x, &pred_y);
                s->mv_dir |= MV_DIR_BACKWARD;

                if (s->umvplus)
                    mx = h263p_decode_umotion(s, pred_x);
                else
                    mx = ff_h263_decode_motion(s, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (s->umvplus)
                    my = h263p_decode_umotion(s, pred_y);
                else
                    my = ff_h263_decode_motion(s, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;

                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */

                s->mv[1][0][0] = mx;
                s->mv[1][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }
        }

        s->current_picture.mb_type[xy] = mb_type;
    } else { /* I-Frame */
        do{
            cbpc = get_vlc2(&s->gb, ff_h263_intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(s->avctx, AV_LOG_ERROR, "I cbpc damaged at %d %d\n", s->mb_x, s->mb_y);
                return SLICE_ERROR;
            }
        }while(cbpc == 8);

        s->bdsp.clear_blocks(s->block[0]);

        dquant = cbpc & 4;
        s->mb_intra = 1;
intra:
        s->current_picture.mb_type[xy] = MB_TYPE_INTRA;
        if (s->h263_aic) {
            s->ac_pred = get_bits1(&s->gb);
            if(s->ac_pred){
                s->current_picture.mb_type[xy] = MB_TYPE_INTRA | MB_TYPE_ACPRED;

                s->h263_aic_dir = get_bits1(&s->gb);
            }
        }else
            s->ac_pred = 0;

        if(s->pb_frame && get_bits1(&s->gb))
            pb_mv_count = h263_get_modb(&s->gb, s->pb_frame, &cbpb);
        cbpy = get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(s->avctx, AV_LOG_ERROR, "I cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
            return SLICE_ERROR;
        }
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(s);
        }

        pb_mv_count += !!s->pb_frame;
    }

    while(pb_mv_count--){
        ff_h263_decode_motion(s, 0, 1);
        ff_h263_decode_motion(s, 0, 1);
    }

    /* decode each block */
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(s, block[i], i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }

    if(s->pb_frame && h263_skip_b_part(s, cbpb) < 0)
        return -1;
    if(s->obmc && !s->mb_intra){
        if(s->pict_type == AV_PICTURE_TYPE_P && s->mb_x+1<s->mb_width && s->mb_num_left != 1)
            preview_obmc(s);
    }
end:

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

        /* per-MB end of slice check */
    {
        int v= show_bits(&s->gb, 16);

        if (get_bits_left(&s->gb) < 16) {
            v >>= 16 - get_bits_left(&s->gb);
        }

        if(v==0)
            return SLICE_END;
    }

    return SLICE_OK;
}

/* Most is hardcoded; should extend to handle all H.263 streams. */
int ff_h263_decode_picture_header(MpegEncContext *s)
{
    int format, width, height, i, ret;
    uint32_t startcode;

    align_get_bits(&s->gb);

    if (show_bits(&s->gb, 2) == 2 && s->avctx->frame_number == 0) {
         av_log(s->avctx, AV_LOG_WARNING, "Header looks like RTP instead of H.263\n");
    }

    startcode= get_bits(&s->gb, 22-8);

    for(i= get_bits_left(&s->gb); i>24; i-=8) {
        startcode = ((startcode << 8) | get_bits(&s->gb, 8)) & 0x003FFFFF;

        if(startcode == 0x20)
            break;
    }

    if (startcode != 0x20) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    /* temporal reference */
    i = get_bits(&s->gb, 8); /* picture timestamp */

    i -= (i - (s->picture_number & 0xFF) + 128) & ~0xFF;

    s->picture_number= (s->picture_number&~0xFF) + i;

    /* PTYPE starts here */
    if (check_marker(s->avctx, &s->gb, "in PTYPE") != 1) {
        return -1;
    }
    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad H.263 id\n");
        return -1;      /* H.263 id */
    }
    skip_bits1(&s->gb);         /* split screen off */
    skip_bits1(&s->gb);         /* camera  off */
    skip_bits1(&s->gb);         /* freeze picture release off */

    format = get_bits(&s->gb, 3);
    /*
        0    forbidden
        1    sub-QCIF
        10   QCIF
        7       extended PTYPE (PLUSPTYPE)
    */

    if (format != 7 && format != 6) {
        s->h263_plus = 0;
        /* H.263v1 */
        width = ff_h263_format[format][0];
        height = ff_h263_format[format][1];
        if (!width)
            return -1;

        s->pict_type = AV_PICTURE_TYPE_I + get_bits1(&s->gb);

        s->h263_long_vectors = get_bits1(&s->gb);

        if (get_bits1(&s->gb) != 0) {
            av_log(s->avctx, AV_LOG_ERROR, "H.263 SAC not supported\n");
            return -1; /* SAC: off */
        }
        s->obmc= get_bits1(&s->gb); /* Advanced prediction mode */
        s->unrestricted_mv = s->h263_long_vectors || s->obmc;

        s->pb_frame = get_bits1(&s->gb);
        s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
        skip_bits1(&s->gb); /* Continuous Presence Multipoint mode: off */

        s->width = width;
        s->height = height;
        s->avctx->sample_aspect_ratio= (AVRational){12,11};
        s->avctx->framerate = (AVRational){ 30000, 1001 };
    } else {
        int ufep;

        /* H.263v2 */
        s->h263_plus = 1;
        ufep = get_bits(&s->gb, 3); /* Update Full Extended PTYPE */

        /* ufep other than 0 and 1 are reserved */
        if (ufep == 1) {
            /* OPPTYPE */
            format = get_bits(&s->gb, 3);
            ff_dlog(s->avctx, "ufep=1, format: %d\n", format);
            s->custom_pcf= get_bits1(&s->gb);
            s->umvplus = get_bits1(&s->gb); /* Unrestricted Motion Vector */
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Syntax-based Arithmetic Coding (SAC) not supported\n");
            }
            s->obmc= get_bits1(&s->gb); /* Advanced prediction mode */
            s->h263_aic = get_bits1(&s->gb); /* Advanced Intra Coding (AIC) */
            s->loop_filter= get_bits1(&s->gb);
            s->unrestricted_mv = s->umvplus || s->obmc || s->loop_filter;
            if(s->avctx->lowres)
                s->loop_filter = 0;

            s->h263_slice_structured= get_bits1(&s->gb);
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Reference Picture Selection not supported\n");
            }
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Independent Segment Decoding not supported\n");
            }
            s->alt_inter_vlc= get_bits1(&s->gb);
            s->modified_quant= get_bits1(&s->gb);
            if(s->modified_quant)
                s->chroma_qscale_table= ff_h263_chroma_qscale_table;

            skip_bits(&s->gb, 1); /* Prevent start code emulation */

            skip_bits(&s->gb, 3); /* Reserved */
        } else if (ufep != 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Bad UFEP type (%d)\n", ufep);
            return -1;
        }

        /* MPPTYPE */
        s->pict_type = get_bits(&s->gb, 3);
        switch(s->pict_type){
        case 0: s->pict_type= AV_PICTURE_TYPE_I;break;
        case 1: s->pict_type= AV_PICTURE_TYPE_P;break;
        case 2: s->pict_type= AV_PICTURE_TYPE_P;s->pb_frame = 3;break;
        case 3: s->pict_type= AV_PICTURE_TYPE_B;break;
        case 7: s->pict_type= AV_PICTURE_TYPE_I;break; //ZYGO
        default:
            return -1;
        }
        skip_bits(&s->gb, 2);
        s->no_rounding = get_bits1(&s->gb);
        skip_bits(&s->gb, 4);

        /* Get the picture dimensions */
        if (ufep) {
            if (format == 6) {
                /* Custom Picture Format (CPFMT) */
                s->aspect_ratio_info = get_bits(&s->gb, 4);
                ff_dlog(s->avctx, "aspect: %d\n", s->aspect_ratio_info);
                /* aspect ratios:
                0 - forbidden
                1 - 1:1
                2 - 12:11 (CIF 4:3)
                3 - 10:11 (525-type 4:3)
                4 - 16:11 (CIF 16:9)
                5 - 40:33 (525-type 16:9)
                6-14 - reserved
                */
                width = (get_bits(&s->gb, 9) + 1) * 4;
                check_marker(s->avctx, &s->gb, "in dimensions");
                height = get_bits(&s->gb, 9) * 4;
                ff_dlog(s->avctx, "\nH.263+ Custom picture: %dx%d\n",width,height);
                if (s->aspect_ratio_info == FF_ASPECT_EXTENDED) {
                    /* expected dimensions */
                    s->avctx->sample_aspect_ratio.num= get_bits(&s->gb, 8);
                    s->avctx->sample_aspect_ratio.den= get_bits(&s->gb, 8);
                }else{
                    s->avctx->sample_aspect_ratio= ff_h263_pixel_aspect[s->aspect_ratio_info];
                }
            } else {
                width = ff_h263_format[format][0];
                height = ff_h263_format[format][1];
                s->avctx->sample_aspect_ratio= (AVRational){12,11};
            }
            s->avctx->sample_aspect_ratio.den <<= s->ehc_mode;
            if ((width == 0) || (height == 0))
                return -1;
            s->width = width;
            s->height = height;

            if(s->custom_pcf){
                int gcd;
                s->avctx->framerate.num  = 1800000;
                s->avctx->framerate.den  = 1000 + get_bits1(&s->gb);
                s->avctx->framerate.den *= get_bits(&s->gb, 7);
                if(s->avctx->framerate.den == 0){
                    av_log(s, AV_LOG_ERROR, "zero framerate\n");
                    return -1;
                }
                gcd= av_gcd(s->avctx->framerate.den, s->avctx->framerate.num);
                s->avctx->framerate.den /= gcd;
                s->avctx->framerate.num /= gcd;
            }else{
                s->avctx->framerate = (AVRational){ 30000, 1001 };
            }
        }

        if(s->custom_pcf){
            skip_bits(&s->gb, 2); //extended Temporal reference
        }

        if (ufep) {
            if (s->umvplus) {
                if(get_bits1(&s->gb)==0) /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
                    skip_bits1(&s->gb);
            }
            if(s->h263_slice_structured){
                if (get_bits1(&s->gb) != 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "rectangular slices not supported\n");
                }
                if (get_bits1(&s->gb) != 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "unordered slices not supported\n");
                }
            }
            if (s->pict_type == AV_PICTURE_TYPE_B) {
                skip_bits(&s->gb, 4); //ELNUM
                if (ufep == 1) {
                    skip_bits(&s->gb, 4); // RLNUM
                }
            }
        }

        s->qscale = get_bits(&s->gb, 5);
    }

    if ((ret = av_image_check_size(s->width, s->height, 0, s)) < 0)
        return ret;

    if (!(s->avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)) {
        if ((s->width * s->height / 256 / 8) > get_bits_left(&s->gb))
            return AVERROR_INVALIDDATA;
    }

    s->mb_width = (s->width  + 15) / 16;
    s->mb_height = (s->height  + 15) / 16;
    s->mb_num = s->mb_width * s->mb_height;

    if (s->pb_frame) {
        skip_bits(&s->gb, 3); /* Temporal reference for B-pictures */
        if (s->custom_pcf)
            skip_bits(&s->gb, 2); //extended Temporal reference
        skip_bits(&s->gb, 2); /* Quantization information for B-pictures */
    }

    if (s->pict_type!=AV_PICTURE_TYPE_B) {
        s->time            = s->picture_number;
        s->pp_time         = s->time - s->last_non_b_time;
        s->last_non_b_time = s->time;
    }else{
        s->time    = s->picture_number;
        s->pb_time = s->pp_time - (s->last_non_b_time - s->time);
        if (s->pp_time <=s->pb_time ||
            s->pp_time <= s->pp_time - s->pb_time ||
            s->pp_time <= 0){
            s->pp_time = 2;
            s->pb_time = 1;
        }
        ff_mpeg4_init_direct_mv(s);
    }

    /* PEI */
    if (skip_1stop_8data_bits(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    if(s->h263_slice_structured){
        if (check_marker(s->avctx, &s->gb, "SEPB1") != 1) {
            return -1;
        }

        ff_h263_decode_mba(s);

        if (check_marker(s->avctx, &s->gb, "SEPB2") != 1) {
            return -1;
        }
    }
    s->f_code = 1;

    if (s->pict_type == AV_PICTURE_TYPE_B)
        s->low_delay = 0;

    if(s->h263_aic){
         s->y_dc_scale_table=
         s->c_dc_scale_table= ff_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }

        ff_h263_show_pict_info(s);
    if (s->pict_type == AV_PICTURE_TYPE_I && s->codec_tag == AV_RL32("ZYGO") && get_bits_left(&s->gb) >= 85 + 13*3*16 + 50){
        int i,j;
        for(i=0; i<85; i++) av_log(s->avctx, AV_LOG_DEBUG, "%d", get_bits1(&s->gb));
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
        for(i=0; i<13; i++){
            for(j=0; j<3; j++){
                int v= get_bits(&s->gb, 8);
                v |= get_sbits(&s->gb, 8) * (1 << 8);
                av_log(s->avctx, AV_LOG_DEBUG, " %5d", v);
            }
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
        for(i=0; i<50; i++) av_log(s->avctx, AV_LOG_DEBUG, "%d", get_bits1(&s->gb));
    }

    return 0;
}
