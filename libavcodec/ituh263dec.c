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

#include "config_components.h"

#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h263data.h"
#include "h263dec.h"
#include "mathops.h"
#include "mpegutils.h"
#include "unary.h"
#include "rv10dec.h"
#include "mpeg4video.h"
#include "mpegvideodata.h"
#include "mpegvideodec.h"
#include "mpeg4videodec.h"
#include "mpeg4videodefs.h"

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define H263_MBTYPE_B_VLC_BITS 6
#define CBPC_B_VLC_BITS 3

static const int16_t h263_mb_type_b_map[15]= {
    MB_TYPE_DIRECT2 | MB_TYPE_BIDIR_MV,
    MB_TYPE_DIRECT2 | MB_TYPE_BIDIR_MV    | MB_TYPE_CBP,
    MB_TYPE_DIRECT2 | MB_TYPE_BIDIR_MV    | MB_TYPE_CBP | MB_TYPE_QUANT,
                      MB_TYPE_FORWARD_MV                                | MB_TYPE_16x16,
                      MB_TYPE_FORWARD_MV  | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_FORWARD_MV  | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_BACKWARD_MV                               | MB_TYPE_16x16,
                      MB_TYPE_BACKWARD_MV | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_BACKWARD_MV | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_BIDIR_MV                                  | MB_TYPE_16x16,
                      MB_TYPE_BIDIR_MV    | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_BIDIR_MV    | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
    0, //stuffing
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP,
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP | MB_TYPE_QUANT,
};

void ff_h263_show_pict_info(H263DecContext *const h, int h263_plus)
{
    if (h->c.avctx->debug&FF_DEBUG_PICT_INFO) {
        av_log(h->c.avctx, AV_LOG_DEBUG, "qp:%d %c size:%d rnd:%d%s%s%s%s%s%s%s%s%s %d/%d\n",
               h->c.qscale, av_get_picture_type_char(h->c.pict_type),
               h->gb.size_in_bits, 1-h->c.no_rounding,
               h->c.obmc ? " AP" : "",
               h->umvplus ? " UMV" : "",
               h->h263_long_vectors ? " LONG" : "",
               h263_plus ? " +" : "",
               h->c.h263_aic ? " AIC" : "",
               h->alt_inter_vlc ? " AIV" : "",
               h->modified_quant ? " MQ" : "",
               h->loop_filter ? " LOOP" : "",
               h->h263_slice_structured ? " SS" : "",
               h->c.avctx->framerate.num, h->c.avctx->framerate.den);
    }
}

/***********************************************/
/* decoding */

VLCElem ff_h263_intra_MCBPC_vlc[72];
VLCElem ff_h263_inter_MCBPC_vlc[198];
VLCElem ff_h263_cbpy_vlc[64];
VLCElem ff_h263_mv_vlc[538];
static VLCElem h263_mbtype_b_vlc[80];
static VLCElem cbpc_b_vlc[8];

/* init vlcs */

static av_cold void h263_decode_init_vlc(void)
{
    VLC_INIT_STATIC_TABLE(ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 9,
                          ff_h263_intra_MCBPC_bits, 1, 1,
                          ff_h263_intra_MCBPC_code, 1, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 28,
                          ff_h263_inter_MCBPC_bits, 1, 1,
                          ff_h263_inter_MCBPC_code, 1, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_h263_cbpy_vlc, CBPY_VLC_BITS, 16,
                          &ff_h263_cbpy_tab[0][1], 2, 1,
                          &ff_h263_cbpy_tab[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_h263_mv_vlc, H263_MV_VLC_BITS, 33,
                          &ff_mvtab[0][1], 2, 1,
                          &ff_mvtab[0][0], 2, 1, 0);
    VLC_INIT_RL(ff_h263_rl_inter, 554);
    INIT_FIRST_VLC_RL(ff_rl_intra_aic, 554);
    VLC_INIT_STATIC_SPARSE_TABLE(h263_mbtype_b_vlc, H263_MBTYPE_B_VLC_BITS, 15,
                                 &ff_h263_mbtype_b_tab[0][1], 2, 1,
                                 &ff_h263_mbtype_b_tab[0][0], 2, 1,
                                 h263_mb_type_b_map, 2, 2, 0);
    VLC_INIT_STATIC_TABLE(cbpc_b_vlc, CBPC_B_VLC_BITS, 4,
                          &ff_cbpc_b_tab[0][1], 2, 1,
                          &ff_cbpc_b_tab[0][0], 2, 1, 0);
}

av_cold void ff_h263_decode_init_vlc(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, h263_decode_init_vlc);
}

int ff_h263_decode_mba(H263DecContext *const h)
{
    int i, mb_pos;

    for (i = 0; i < 6; i++)
        if (h->c.mb_num - 1 <= ff_mba_max[i])
            break;
    mb_pos    = get_bits(&h->gb, ff_mba_length[i]);
    h->c.mb_x = mb_pos % h->c.mb_width;
    h->c.mb_y = mb_pos / h->c.mb_width;

    return mb_pos;
}

/**
 * Decode the group of blocks header or slice header.
 * @return <0 if an error occurred
 */
static int h263_decode_gob_header(H263DecContext *const h)
{
    unsigned int val, gob_number;
    int left;

    /* Check for GOB Start Code */
    val = show_bits(&h->gb, 16);
    if(val)
        return -1;

        /* We have a GBSC probably with GSTUFF */
    skip_bits(&h->gb, 16); /* Drop the zeros */
    left = get_bits_left(&h->gb);
    left = FFMIN(left, 32);
    //MN: we must check the bits left or we might end in an infinite loop (or segfault)
    for(;left>13; left--){
        if (get_bits1(&h->gb))
            break; /* Seek the '1' bit */
    }
    if(left<=13)
        return -1;

    if (h->h263_slice_structured) {
        if (check_marker(h->c.avctx, &h->gb, "before MBA")==0)
            return -1;

        ff_h263_decode_mba(h);

        if (h->c.mb_num > 1583)
            if (check_marker(h->c.avctx, &h->gb, "after MBA")==0)
                return -1;

        h->c.qscale = get_bits(&h->gb, 5); /* SQUANT */
        if (check_marker(h->c.avctx, &h->gb, "after SQUANT")==0)
            return -1;
        skip_bits(&h->gb, 2); /* GFID */
    }else{
        gob_number = get_bits(&h->gb, 5); /* GN */
        h->c.mb_x = 0;
        h->c.mb_y = h->gob_index* gob_number;
        skip_bits(&h->gb, 2); /* GFID */
        h->c.qscale = get_bits(&h->gb, 5); /* GQUANT */
    }

    if (h->c.mb_y >= h->c.mb_height)
        return -1;

    if (h->c.qscale==0)
        return -1;

    return 0;
}

/**
 * Decode the group of blocks / video packet header / slice header (MPEG-4 Studio).
 * @return bit position of the resync_marker, or <0 if none was found
 */
int ff_h263_resync(H263DecContext *const h)
{
    int left, pos, ret;

    /* In MPEG-4 studio mode look for a new slice startcode
     * and decode slice header */
    if (h->c.codec_id==AV_CODEC_ID_MPEG4 && h->c.studio_profile) {
        align_get_bits(&h->gb);

        while (get_bits_left(&h->gb) >= 32 && show_bits_long(&h->gb, 32) != SLICE_STARTCODE) {
            get_bits(&h->gb, 8);
        }

        if (get_bits_left(&h->gb) >= 32 && show_bits_long(&h->gb, 32) == SLICE_STARTCODE)
            return get_bits_count(&h->gb);
        else
            return -1;
    }

    if (h->c.codec_id==AV_CODEC_ID_MPEG4){
        skip_bits1(&h->gb);
        align_get_bits(&h->gb);
    }

    if (show_bits(&h->gb, 16) ==0) {
        pos = get_bits_count(&h->gb);
        if(CONFIG_MPEG4_DECODER && h->c.codec_id==AV_CODEC_ID_MPEG4)
            ret = ff_mpeg4_decode_video_packet_header(h);
        else
            ret = h263_decode_gob_header(h);
        if(ret>=0)
            return pos;
    }
    //OK, it's not where it is supposed to be ...
    h->gb = h->last_resync_gb;
    align_get_bits(&h->gb);
    left = get_bits_left(&h->gb);

    for(;left>16+1+5+5; left-=8){
        if (show_bits(&h->gb, 16) == 0){
            GetBitContext bak = h->gb;

            pos = get_bits_count(&h->gb);
            if(CONFIG_MPEG4_DECODER && h->c.codec_id==AV_CODEC_ID_MPEG4)
                ret = ff_mpeg4_decode_video_packet_header(h);
            else
                ret = h263_decode_gob_header(h);
            if(ret>=0)
                return pos;

            h->gb = bak;
        }
        skip_bits(&h->gb, 8);
    }

    return -1;
}

int ff_h263_decode_motion(H263DecContext *const h, int pred, int f_code)
{
    int code, val, sign, shift;
    code = get_vlc2(&h->gb, ff_h263_mv_vlc, H263_MV_VLC_BITS, 2);

    if (code == 0)
        return pred;
    if (code < 0)
        return 0xffff;

    sign = get_bits1(&h->gb);
    shift = f_code - 1;
    val = code;
    if (shift) {
        val = (val - 1) << shift;
        val |= get_bits(&h->gb, shift);
        val++;
    }
    if (sign)
        val = -val;
    val += pred;

    /* modulo decoding */
    if (!h->h263_long_vectors) {
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
static int h263p_decode_umotion(H263DecContext *const h, int pred)
{
   int code = 0, sign;

   if (get_bits1(&h->gb)) /* Motion difference = 0 */
      return pred;

   code = 2 + get_bits1(&h->gb);

   while (get_bits1(&h->gb))
   {
      code <<= 1;
      code += get_bits1(&h->gb);
      if (code >= 32768) {
          avpriv_request_sample(h->c.avctx, "Huge DMV");
          return 0xffff;
      }
   }
   sign = code & 1;
   code >>= 1;

   code = (sign) ? (pred - code) : (pred + code);
   ff_tlog(h->c.avctx,"H.263+ UMV Motion = %d\n", code);
   return code;

}

/**
 * read the next MVs for OBMC. yes this is an ugly hack, feel free to send a patch :)
 */
static void preview_obmc(H263DecContext *const h)
{
    GetBitContext gb = h->gb;

    int cbpc, i, pred_x, pred_y, mx, my;
    int16_t *mot_val;
    const int xy     = h->c.mb_x + 1 + h->c.mb_y * h->c.mb_stride;
    const int stride = h->c.b8_stride * 2;

    for(i=0; i<4; i++)
        h->c.block_index[i] += 2;
    for(i=4; i<6; i++)
        h->c.block_index[i] += 1;
    h->c.mb_x++;

    av_assert2(h->c.pict_type == AV_PICTURE_TYPE_P);

    do{
        if (get_bits1(&h->gb)) {
            /* skip mb */
            mot_val = h->c.cur_pic.motion_val[0][h->c.block_index[0]];
            mot_val[0       ]= mot_val[2       ]=
            mot_val[0+stride]= mot_val[2+stride]= 0;
            mot_val[1       ]= mot_val[3       ]=
            mot_val[1+stride]= mot_val[3+stride]= 0;

            h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            goto end;
        }
        cbpc = get_vlc2(&h->gb, ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 2);
    }while(cbpc == 20);

    if(cbpc & 4){
        h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA;
    }else{
        get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
        if (cbpc & 8) {
            skip_bits(&h->gb, h->modified_quant ? (get_bits1(&h->gb) ? 1 : 5) : 2);
        }

        if ((cbpc & 16) == 0) {
                h->c.cur_pic.mb_type[xy] = MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
                /* 16x16 motion prediction */
                mot_val= ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);
                if (h->umvplus)
                    mx = h263p_decode_umotion(h, pred_x);
                else
                    mx = ff_h263_decode_motion(h, pred_x, 1);

                if (h->umvplus)
                    my = h263p_decode_umotion(h, pred_y);
                else
                    my = ff_h263_decode_motion(h, pred_y, 1);

                mot_val[0       ]= mot_val[2       ]=
                mot_val[0+stride]= mot_val[2+stride]= mx;
                mot_val[1       ]= mot_val[3       ]=
                mot_val[1+stride]= mot_val[3+stride]= my;
        } else {
            h->c.cur_pic.mb_type[xy] = MB_TYPE_8x8 | MB_TYPE_FORWARD_MV;
            for(i=0;i<4;i++) {
                mot_val = ff_h263_pred_motion(&h->c, i, 0, &pred_x, &pred_y);
                if (h->umvplus)
                    mx = h263p_decode_umotion(h, pred_x);
                else
                    mx = ff_h263_decode_motion(h, pred_x, 1);

                if (h->umvplus)
                    my = h263p_decode_umotion(h, pred_y);
                else
                    my = ff_h263_decode_motion(h, pred_y, 1);
                if (h->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&h->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    }
end:

    for(i=0; i<4; i++)
        h->c.block_index[i] -= 2;
    for(i=4; i<6; i++)
        h->c.block_index[i] -= 1;
    h->c.mb_x--;

    h->gb = gb;
}

static void h263_decode_dquant(H263DecContext *const h)
{
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };
    int qscale;

    if (h->modified_quant) {
        if (get_bits1(&h->gb))
            qscale = ff_modified_quant_tab[get_bits1(&h->gb)][h->c.qscale];
        else
            qscale = get_bits(&h->gb, 5);
    }else
        qscale = h->c.qscale + quant_tab[get_bits(&h->gb, 2)];
    ff_set_qscale(&h->c, qscale);
}

static void h263_pred_acdc(MpegEncContext * s, int16_t *block, int n)
{
    int wrap, a, c, pred_dc, scale;
    const int xy = s->block_index[n];
    int16_t *const dc_val =  s->dc_val + xy;
    int16_t *const ac_val = (s->ac_val + xy)[0];

    /* find prediction */
    if (n < 4) {
        wrap = s->b8_stride;
        scale = s->y_dc_scale;
    } else {
        wrap = s->mb_stride;
        scale = s->c_dc_scale;
    }

    /* B C
     * A X
     */
    a = dc_val[-1];
    c = dc_val[-wrap];

    /* No prediction outside GOB boundary */
    if (s->first_slice_line && n != 3) {
        if (n != 2) c= 1024;
        if (n != 1 && s->mb_x == s->resync_mb_x) a= 1024;
    }

    if (s->ac_pred) {
        pred_dc = 1024;
        if (s->h263_aic_dir) {
            /* left prediction */
            if (a != 1024) {
                int16_t *const ac_val2 = ac_val - 16;
                for (int i = 1; i < 8; i++) {
                    block[s->idsp.idct_permutation[i << 3]] += ac_val2[i];
                }
                pred_dc = a;
            }
        } else {
            /* top prediction */
            if (c != 1024) {
                int16_t *const ac_val2 = ac_val - 16 * wrap;
                for (int i = 1; i < 8; i++) {
                    block[s->idsp.idct_permutation[i]] += ac_val2[i + 8];
                }
                pred_dc = c;
            }
        }
    } else {
        /* just DC prediction */
        if (a != 1024 && c != 1024)
            pred_dc = (a + c) >> 1;
        else if (a != 1024)
            pred_dc = a;
        else
            pred_dc = c;
    }

    /* we assume pred is positive */
    block[0] = block[0] * scale + pred_dc;

    if (block[0] < 0)
        block[0] = 0;
    else
        block[0] |= 1;

    /* Update AC/DC tables */
    *dc_val = block[0];

    /* left copy */
    for (int i = 1; i < 8; i++)
        ac_val[i]     = block[s->idsp.idct_permutation[i << 3]];
    /* top copy */
    for (int i = 1; i < 8; i++)
        ac_val[8 + i] = block[s->idsp.idct_permutation[i]];
}

static int h263_decode_block(H263DecContext *const h, int16_t block[64],
                             int n, int coded)
{
    int level, i, j, run;
    const RLTable *rl = &ff_h263_rl_inter;
    const uint8_t *scan_table;
    GetBitContext gb = h->gb;

    scan_table = h->c.intra_scantable.permutated;
    if (h->c.h263_aic && h->c.mb_intra) {
        i = 0;
        if (!coded)
            goto not_coded;
        rl = &ff_rl_intra_aic;
        if (h->c.ac_pred) {
            if (h->c.h263_aic_dir)
                scan_table = h->c.permutated_intra_v_scantable; /* left */
            else
                scan_table = h->c.permutated_intra_h_scantable; /* top */
        }
    } else if (h->c.mb_intra) {
        /* DC coef */
        if (CONFIG_RV10_DECODER && h->c.codec_id == AV_CODEC_ID_RV10) {
            if (h->rv10_version == 3 && h->c.pict_type == AV_PICTURE_TYPE_I) {
                int component = (n <= 3 ? 0 : n - 4 + 1);
                level = h->c.last_dc[component];
                if (h->rv10_first_dc_coded[component]) {
                    int diff = ff_rv_decode_dc(h, n);
                    if (diff < 0)
                        return -1;
                    level += diff;
                    level = level & 0xff; /* handle wrap round */
                    h->c.last_dc[component] = level;
                } else {
                    h->rv10_first_dc_coded[component] = 1;
                }
            } else {
                level = get_bits(&h->gb, 8);
                if (level == 255)
                    level = 128;
            }
        }else{
            level = get_bits(&h->gb, 8);
            if((level&0x7F) == 0){
                av_log(h->c.avctx, AV_LOG_ERROR, "illegal dc %d at %d %d\n",
                       level, h->c.mb_x, h->c.mb_y);
                if (h->c.avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT))
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
        h->c.block_last_index[n] = i - 1;
        return 0;
    }
retry:
    {
    OPEN_READER(re, &h->gb);
    i--; // offset by -1 to allow direct indexing of scan_table
    for(;;) {
        UPDATE_CACHE(re, &h->gb);
        GET_RL_VLC(level, run, re, &h->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2, 0);
        if (run == 66) {
            if (level){
                CLOSE_READER(re, &h->gb);
                av_log(h->c.avctx, AV_LOG_ERROR, "illegal ac vlc code at %dx%d\n",
                       h->c.mb_x, h->c.mb_y);
                return -1;
            }
            /* escape */
            if (CONFIG_FLV_DECODER && h->flv) {
                int is11 = SHOW_UBITS(re, &h->gb, 1);
                SKIP_CACHE(re, &h->gb, 1);
                run = SHOW_UBITS(re, &h->gb, 7) + 1;
                if (is11) {
                    SKIP_COUNTER(re, &h->gb, 1 + 7);
                    UPDATE_CACHE(re, &h->gb);
                    level = SHOW_SBITS(re, &h->gb, 11);
                    SKIP_COUNTER(re, &h->gb, 11);
                } else {
                    SKIP_CACHE(re, &h->gb, 7);
                    level = SHOW_SBITS(re, &h->gb, 7);
                    SKIP_COUNTER(re, &h->gb, 1 + 7 + 7);
                }
            } else {
                run = SHOW_UBITS(re, &h->gb, 7) + 1;
                SKIP_CACHE(re, &h->gb, 7);
                level = (int8_t)SHOW_UBITS(re, &h->gb, 8);
                SKIP_COUNTER(re, &h->gb, 7 + 8);
                if(level == -128){
                    UPDATE_CACHE(re, &h->gb);
                    if (h->c.codec_id == AV_CODEC_ID_RV10) {
                        /* XXX: should patch encoder too */
                        level = SHOW_SBITS(re, &h->gb, 12);
                        SKIP_COUNTER(re, &h->gb, 12);
                    }else{
                        level = SHOW_UBITS(re, &h->gb, 5);
                        SKIP_CACHE(re, &h->gb, 5);
                        level |= SHOW_SBITS(re, &h->gb, 6) * (1<<5);
                        SKIP_COUNTER(re, &h->gb, 5 + 6);
                    }
                }
            }
        } else {
            if (SHOW_UBITS(re, &h->gb, 1))
                level = -level;
            SKIP_COUNTER(re, &h->gb, 1);
        }
        i += run;
        if (i >= 64){
            CLOSE_READER(re, &h->gb);
            // redo update without last flag, revert -1 offset
            i = i - run + ((run-1)&63) + 1;
            if (i < 64) {
                // only last marker, no overrun
                block[scan_table[i]] = level;
                break;
            }
            if(h->alt_inter_vlc && rl == &ff_h263_rl_inter && !h->c.mb_intra){
                //Looks like a hack but no, it's the way it is supposed to work ...
                rl = &ff_rl_intra_aic;
                i = 0;
                h->gb = gb;
                h->c.bdsp.clear_block(block);
                goto retry;
            }
            av_log(h->c.avctx, AV_LOG_ERROR, "run overflow at %dx%d i:%d\n",
                   h->c.mb_x, h->c.mb_y, h->c.mb_intra);
            return -1;
        }
        j = scan_table[i];
        block[j] = level;
    }
    }
    if (h->c.mb_intra && h->c.h263_aic) {
not_coded:
        h263_pred_acdc(&h->c, block, n);
    }
    h->c.block_last_index[n] = i;
    return 0;
}

static int h263_skip_b_part(H263DecContext *const h, int cbp)
{
    LOCAL_ALIGNED_32(int16_t, dblock, [64]);
    int i, mbi;
    int bli[6];

    /* we have to set h->c.mb_intra to zero to decode B-part of PB-frame correctly
     * but real value should be restored in order to be used later (in OBMC condition)
     */
    mbi = h->c.mb_intra;
    memcpy(bli, h->c.block_last_index, sizeof(bli));
    h->c.mb_intra = 0;
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(h, dblock, i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }
    h->c.mb_intra = mbi;
    memcpy(h->c.block_last_index, bli, sizeof(bli));
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
static inline void set_one_direct_mv(MpegEncContext *s, const MPVPicture *p, int i)
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
    const MPVPicture *p = s->next_pic.ptr;
    int colocated_mb_type = p->mb_type[mb_index];
    int i;

    if (s->codec_tag == AV_RL32("U263") && p->f->pict_type == AV_PICTURE_TYPE_I) {
        p = s->last_pic.ptr;
        colocated_mb_type = p->mb_type[mb_index];
    }

    if (IS_8X8(colocated_mb_type)) {
        s->mv_type = MV_TYPE_8X8;
        for (i = 0; i < 4; i++)
            set_one_direct_mv(s, p, i);
        return MB_TYPE_DIRECT2 | MB_TYPE_8x8 | MB_TYPE_BIDIR_MV;
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
        return MB_TYPE_DIRECT2 | MB_TYPE_16x16 | MB_TYPE_BIDIR_MV;
    }
}

int ff_h263_decode_mb(H263DecContext *const h)
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    int16_t *mot_val;
    const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;
    int cbpb = 0, pb_mv_count = 0;

    av_assert2(!h->c.h263_pred);

    if (h->c.pict_type == AV_PICTURE_TYPE_P) {
        do{
            if (get_bits1(&h->gb)) {
                /* skip mb */
                h->c.mb_intra = 0;
                for(i=0;i<6;i++)
                    h->c.block_last_index[i] = -1;
                h->c.mv_dir  = MV_DIR_FORWARD;
                h->c.mv_type = MV_TYPE_16X16;
                h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
                h->c.mv[0][0][0] = 0;
                h->c.mv[0][0][1] = 0;
                h->c.mb_skipped = !(h->c.obmc | h->loop_filter);
                goto end;
            }
            cbpc = get_vlc2(&h->gb, ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(h->c.avctx, AV_LOG_ERROR, "cbpc damaged at %d %d\n",
                       h->c.mb_x, h->c.mb_y);
                return SLICE_ERROR;
            }
        }while(cbpc == 20);

        h->c.bdsp.clear_blocks(h->block[0]);

        dquant = cbpc & 8;
        h->c.mb_intra = ((cbpc & 4) != 0);
        if (h->c.mb_intra)
            goto intra;

        if (h->pb_frame && get_bits1(&h->gb))
            pb_mv_count = h263_get_modb(&h->gb, h->pb_frame, &cbpb);
        cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);

        if (cbpy < 0) {
            av_log(h->c.avctx, AV_LOG_ERROR, "cbpy damaged at %d %d\n",
                   h->c.mb_x, h->c.mb_y);
            return SLICE_ERROR;
        }

        if (!h->alt_inter_vlc|| (cbpc & 3)!=3)
            cbpy ^= 0xF;

        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(h);
        }

        h->c.mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            h->c.cur_pic.mb_type[xy] = MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
            /* 16x16 motion prediction */
            h->c.mv_type = MV_TYPE_16X16;
            ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);
            if (h->umvplus)
               mx = h263p_decode_umotion(h, pred_x);
            else
               mx = ff_h263_decode_motion(h, pred_x, 1);

            if (mx >= 0xffff)
                return SLICE_ERROR;

            if (h->umvplus)
               my = h263p_decode_umotion(h, pred_y);
            else
               my = ff_h263_decode_motion(h, pred_y, 1);

            if (my >= 0xffff)
                return SLICE_ERROR;
            h->c.mv[0][0][0] = mx;
            h->c.mv[0][0][1] = my;

            if (h->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                skip_bits1(&h->gb); /* Bit stuffing to prevent PSC */
        } else {
            h->c.cur_pic.mb_type[xy] = MB_TYPE_8x8 | MB_TYPE_FORWARD_MV;
            h->c.mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = ff_h263_pred_motion(&h->c, i, 0, &pred_x, &pred_y);
                if (h->umvplus)
                    mx = h263p_decode_umotion(h, pred_x);
                else
                    mx = ff_h263_decode_motion(h, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (h->umvplus)
                    my = h263p_decode_umotion(h, pred_y);
                else
                    my = ff_h263_decode_motion(h, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;
                h->c.mv[0][i][0] = mx;
                h->c.mv[0][i][1] = my;
                if (h->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&h->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else if (h->c.pict_type==AV_PICTURE_TYPE_B) {
        int mb_type;
        const int stride  = h->c.b8_stride;
        int16_t *mot_val0 = h->c.cur_pic.motion_val[0][2 * (h->c.mb_x + h->c.mb_y * stride)];
        int16_t *mot_val1 = h->c.cur_pic.motion_val[1][2 * (h->c.mb_x + h->c.mb_y * stride)];
//        const int mv_xy = h->c.mb_x + 1 + h->c.mb_y * h->c.mb_stride;

        //FIXME ugly
        mot_val0[0       ]= mot_val0[2       ]= mot_val0[0+2*stride]= mot_val0[2+2*stride]=
        mot_val0[1       ]= mot_val0[3       ]= mot_val0[1+2*stride]= mot_val0[3+2*stride]=
        mot_val1[0       ]= mot_val1[2       ]= mot_val1[0+2*stride]= mot_val1[2+2*stride]=
        mot_val1[1       ]= mot_val1[3       ]= mot_val1[1+2*stride]= mot_val1[3+2*stride]= 0;

        do{
            mb_type = get_vlc2(&h->gb, h263_mbtype_b_vlc,
                               H263_MBTYPE_B_VLC_BITS, 2);
            if (mb_type < 0){
                av_log(h->c.avctx, AV_LOG_ERROR, "b mb_type damaged at %d %d\n",
                       h->c.mb_x, h->c.mb_y);
                return SLICE_ERROR;
            }
        }while(!mb_type);

        h->c.mb_intra = IS_INTRA(mb_type);
        if(HAS_CBP(mb_type)){
            h->c.bdsp.clear_blocks(h->block[0]);
            cbpc = get_vlc2(&h->gb, cbpc_b_vlc, CBPC_B_VLC_BITS, 1);
            if (h->c.mb_intra) {
                dquant = IS_QUANT(mb_type);
                goto intra;
            }

            cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);

            if (cbpy < 0){
                av_log(h->c.avctx, AV_LOG_ERROR, "b cbpy damaged at %d %d\n",
                       h->c.mb_x, h->c.mb_y);
                return SLICE_ERROR;
            }

            if (!h->alt_inter_vlc || (cbpc & 3)!=3)
                cbpy ^= 0xF;

            cbp = (cbpc & 3) | (cbpy << 2);
        }else
            cbp=0;

        av_assert2(!h->c.mb_intra);

        if(IS_QUANT(mb_type)){
            h263_decode_dquant(h);
        }

        if(IS_DIRECT(mb_type)){
            h->c.mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            mb_type |= set_direct_mv(&h->c);
        }else{
            h->c.mv_dir  = 0;
            h->c.mv_type = MV_TYPE_16X16;
//FIXME UMV

            if (HAS_FORWARD_MV(mb_type)) {
                int16_t *mot_val = ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);
                h->c.mv_dir = MV_DIR_FORWARD;

                if (h->umvplus)
                    mx = h263p_decode_umotion(h, pred_x);
                else
                    mx = ff_h263_decode_motion(h, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (h->umvplus)
                    my = h263p_decode_umotion(h, pred_y);
                else
                    my = ff_h263_decode_motion(h, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;

                if (h->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&h->gb); /* Bit stuffing to prevent PSC */

                h->c.mv[0][0][0] = mx;
                h->c.mv[0][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }

            if (HAS_BACKWARD_MV(mb_type)) {
                int16_t *mot_val = ff_h263_pred_motion(&h->c, 0, 1, &pred_x, &pred_y);
                h->c.mv_dir |= MV_DIR_BACKWARD;

                if (h->umvplus)
                    mx = h263p_decode_umotion(h, pred_x);
                else
                    mx = ff_h263_decode_motion(h, pred_x, 1);
                if (mx >= 0xffff)
                    return SLICE_ERROR;

                if (h->umvplus)
                    my = h263p_decode_umotion(h, pred_y);
                else
                    my = ff_h263_decode_motion(h, pred_y, 1);
                if (my >= 0xffff)
                    return SLICE_ERROR;

                if (h->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                    skip_bits1(&h->gb); /* Bit stuffing to prevent PSC */

                h->c.mv[1][0][0] = mx;
                h->c.mv[1][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }
        }

        h->c.cur_pic.mb_type[xy] = mb_type;
    } else { /* I-Frame */
        do{
            cbpc = get_vlc2(&h->gb, ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(h->c.avctx, AV_LOG_ERROR, "I cbpc damaged at %d %d\n",
                       h->c.mb_x, h->c.mb_y);
                return SLICE_ERROR;
            }
        }while(cbpc == 8);

        h->c.bdsp.clear_blocks(h->block[0]);

        dquant = cbpc & 4;
        h->c.mb_intra = 1;
intra:
        h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA;
        if (h->c.h263_aic) {
            h->c.ac_pred = get_bits1(&h->gb);
            if (h->c.ac_pred) {
                h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA | MB_TYPE_ACPRED;

                h->c.h263_aic_dir = get_bits1(&h->gb);
            }
        }else
            h->c.ac_pred = 0;

        if (h->pb_frame && get_bits1(&h->gb))
            pb_mv_count = h263_get_modb(&h->gb, h->pb_frame, &cbpb);
        cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(h->c.avctx, AV_LOG_ERROR, "I cbpy damaged at %d %d\n",
                   h->c.mb_x, h->c.mb_y);
            return SLICE_ERROR;
        }
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(h);
        }

        pb_mv_count += !!h->pb_frame;
    }

    while(pb_mv_count--){
        ff_h263_decode_motion(h, 0, 1);
        ff_h263_decode_motion(h, 0, 1);
    }

    /* decode each block */
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(h, h->block[i], i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }

    if (h->pb_frame && h263_skip_b_part(h, cbpb) < 0)
        return -1;
    if (h->c.obmc && !h->c.mb_intra) {
        if (h->c.pict_type == AV_PICTURE_TYPE_P &&
            h->c.mb_x + 1 < h->c.mb_width && h->mb_num_left != 1)
            preview_obmc(h);
    }
end:

    if (get_bits_left(&h->gb) < 0)
        return AVERROR_INVALIDDATA;

        /* per-MB end of slice check */
    {
        int v = show_bits(&h->gb, 16);

        if (get_bits_left(&h->gb) < 16) {
            v >>= 16 - get_bits_left(&h->gb);
        }

        if(v==0)
            return SLICE_END;
    }

    return SLICE_OK;
}

/* Most is hardcoded; should extend to handle all H.263 streams. */
int ff_h263_decode_picture_header(H263DecContext *const h)
{
    int width, height, i, ret;
    int h263_plus;

    align_get_bits(&h->gb);

    if (show_bits(&h->gb, 2) == 2 && h->c.avctx->frame_num == 0) {
         av_log(h->c.avctx, AV_LOG_WARNING, "Header looks like RTP instead of H.263\n");
    }

    uint32_t startcode = get_bits(&h->gb, 22-8);

    for (i = get_bits_left(&h->gb); i>24; i -= 8) {
        startcode = ((startcode << 8) | get_bits(&h->gb, 8)) & 0x003FFFFF;

        if(startcode == 0x20)
            break;
    }

    if (startcode != 0x20) {
        av_log(h->c.avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    /* temporal reference */
    i = get_bits(&h->gb, 8); /* picture timestamp */

    i -= (i - (h->picture_number & 0xFF) + 128) & ~0xFF;

    h->picture_number = (h->picture_number&~0xFF) + i;

    /* PTYPE starts here */
    if (check_marker(h->c.avctx, &h->gb, "in PTYPE") != 1) {
        return -1;
    }
    if (get_bits1(&h->gb) != 0) {
        av_log(h->c.avctx, AV_LOG_ERROR, "Bad H.263 id\n");
        return -1;      /* H.263 id */
    }
    skip_bits1(&h->gb);         /* split screen off */
    skip_bits1(&h->gb);         /* camera  off */
    skip_bits1(&h->gb);         /* freeze picture release off */

    int format = get_bits(&h->gb, 3);
    /*
        0    forbidden
        1    sub-QCIF
        10   QCIF
        7       extended PTYPE (PLUSPTYPE)
    */

    if (format != 7 && format != 6) {
        h263_plus = 0;
        /* H.263v1 */
        width = ff_h263_format[format][0];
        height = ff_h263_format[format][1];
        if (!width)
            return -1;

        h->c.pict_type = AV_PICTURE_TYPE_I + get_bits1(&h->gb);

        h->h263_long_vectors = get_bits1(&h->gb);

        if (get_bits1(&h->gb) != 0) {
            av_log(h->c.avctx, AV_LOG_ERROR, "H.263 SAC not supported\n");
            return -1; /* SAC: off */
        }
        h->c.obmc = get_bits1(&h->gb); /* Advanced prediction mode */

        h->pb_frame = get_bits1(&h->gb);
        h->c.chroma_qscale = h->c.qscale = get_bits(&h->gb, 5);
        skip_bits1(&h->gb); /* Continuous Presence Multipoint mode: off */

        h->c.width = width;
        h->c.height = height;
        h->c.avctx->sample_aspect_ratio= (AVRational){12,11};
        h->c.avctx->framerate = (AVRational){ 30000, 1001 };
    } else {
        int ufep;

        /* H.263v2 */
        h263_plus = 1;
        ufep = get_bits(&h->gb, 3); /* Update Full Extended PTYPE */

        /* ufep other than 0 and 1 are reserved */
        if (ufep == 1) {
            /* OPPTYPE */
            format = get_bits(&h->gb, 3);
            ff_dlog(h->c.avctx, "ufep=1, format: %d\n", format);
            h->custom_pcf = get_bits1(&h->gb);
            h->umvplus    = get_bits1(&h->gb); /* Unrestricted Motion Vector */
            if (get_bits1(&h->gb) != 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "Syntax-based Arithmetic Coding (SAC) not supported\n");
            }
            h->c.obmc        = get_bits1(&h->gb); /* Advanced prediction mode */
            h->c.h263_aic    = get_bits1(&h->gb); /* Advanced Intra Coding (AIC) */
            h->loop_filter = get_bits1(&h->gb);
            if (h->c.avctx->lowres)
                h->loop_filter = 0;

            h->h263_slice_structured = get_bits1(&h->gb);
            if (get_bits1(&h->gb) != 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "Reference Picture Selection not supported\n");
            }
            if (get_bits1(&h->gb) != 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "Independent Segment Decoding not supported\n");
            }
            h->alt_inter_vlc  = get_bits1(&h->gb);
            h->modified_quant = get_bits1(&h->gb);
            if (h->modified_quant)
                h->c.chroma_qscale_table= ff_h263_chroma_qscale_table;

            skip_bits(&h->gb, 1); /* Prevent start code emulation */

            skip_bits(&h->gb, 3); /* Reserved */
        } else if (ufep != 0) {
            av_log(h->c.avctx, AV_LOG_ERROR, "Bad UFEP type (%d)\n", ufep);
            return -1;
        }

        /* MPPTYPE */
        h->c.pict_type = get_bits(&h->gb, 3);
        switch (h->c.pict_type) {
        case 0: h->c.pict_type = AV_PICTURE_TYPE_I; break;
        case 1: h->c.pict_type = AV_PICTURE_TYPE_P; break;
        case 2: h->c.pict_type = AV_PICTURE_TYPE_P; h->pb_frame = 3; break;
        case 3: h->c.pict_type = AV_PICTURE_TYPE_B; break;
        case 7: h->c.pict_type = AV_PICTURE_TYPE_I; break; //ZYGO
        default:
            return -1;
        }
        skip_bits(&h->gb, 2);
        h->c.no_rounding = get_bits1(&h->gb);
        skip_bits(&h->gb, 4);

        /* Get the picture dimensions */
        if (ufep) {
            if (format == 6) {
                /* Custom Picture Format (CPFMT) */
                int aspect_ratio_info = get_bits(&h->gb, 4);
                ff_dlog(h->c.avctx, "aspect: %d\n", aspect_ratio_info);
                /* aspect ratios:
                0 - forbidden
                1 - 1:1
                2 - 12:11 (CIF 4:3)
                3 - 10:11 (525-type 4:3)
                4 - 16:11 (CIF 16:9)
                5 - 40:33 (525-type 16:9)
                6-14 - reserved
                */
                width = (get_bits(&h->gb, 9) + 1) * 4;
                check_marker(h->c.avctx, &h->gb, "in dimensions");
                height = get_bits(&h->gb, 9) * 4;
                ff_dlog(h->c.avctx, "\nH.263+ Custom picture: %dx%d\n",width,height);
                if (aspect_ratio_info == FF_ASPECT_EXTENDED) {
                    /* expected dimensions */
                    h->c.avctx->sample_aspect_ratio.num = get_bits(&h->gb, 8);
                    h->c.avctx->sample_aspect_ratio.den = get_bits(&h->gb, 8);
                }else{
                    h->c.avctx->sample_aspect_ratio= ff_h263_pixel_aspect[aspect_ratio_info];
                }
            } else {
                width = ff_h263_format[format][0];
                height = ff_h263_format[format][1];
                h->c.avctx->sample_aspect_ratio = (AVRational){12,11};
            }
            h->c.avctx->sample_aspect_ratio.den <<= h->ehc_mode;
            if ((width == 0) || (height == 0))
                return -1;
            h->c.width  = width;
            h->c.height = height;

            if (h->custom_pcf) {
                h->c.avctx->framerate.num  = 1800000;
                h->c.avctx->framerate.den  = 1000 + get_bits1(&h->gb);
                h->c.avctx->framerate.den *= get_bits(&h->gb, 7);
                if (h->c.avctx->framerate.den == 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "zero framerate\n");
                    return -1;
                }
                int gcd = av_gcd(h->c.avctx->framerate.den, h->c.avctx->framerate.num);
                h->c.avctx->framerate.den /= gcd;
                h->c.avctx->framerate.num /= gcd;
            }else{
                h->c.avctx->framerate = (AVRational){ 30000, 1001 };
            }
        }

        if (h->custom_pcf)
            skip_bits(&h->gb, 2); //extended Temporal reference

        if (ufep) {
            if (h->umvplus) {
                if (get_bits1(&h->gb)==0) /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
                    skip_bits1(&h->gb);
            }
            if (h->h263_slice_structured) {
                if (get_bits1(&h->gb) != 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "rectangular slices not supported\n");
                }
                if (get_bits1(&h->gb) != 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "unordered slices not supported\n");
                }
            }
            if (h->c.pict_type == AV_PICTURE_TYPE_B) {
                skip_bits(&h->gb, 4); //ELNUM
                if (ufep == 1) {
                    skip_bits(&h->gb, 4); // RLNUM
                }
            }
        }

        h->c.qscale = get_bits(&h->gb, 5);
    }

    ret = av_image_check_size(h->c.width, h->c.height, 0, h->c.avctx);
    if (ret < 0)
        return ret;

    if (!(h->c.avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)) {
        if ((h->c.width * h->c.height / 256 / 8) > get_bits_left(&h->gb))
            return AVERROR_INVALIDDATA;
    }

    h->c.mb_width  = (h->c.width  + 15U) / 16;
    h->c.mb_height = (h->c.height + 15U) / 16;
    h->c.mb_num    = h->c.mb_width * h->c.mb_height;

    h->gob_index = H263_GOB_HEIGHT(h->c.height);

    if (h->pb_frame) {
        skip_bits(&h->gb, 3); /* Temporal reference for B-pictures */
        if (h->custom_pcf)
            skip_bits(&h->gb, 2); //extended Temporal reference
        skip_bits(&h->gb, 2); /* Quantization information for B-pictures */
    }

    if (h->c.pict_type!=AV_PICTURE_TYPE_B) {
        h->c.time            = h->picture_number;
        h->c.pp_time         = h->c.time - h->c.last_non_b_time;
        h->c.last_non_b_time = h->c.time;
    }else{
        h->c.time    = h->picture_number;
        h->c.pb_time = h->c.pp_time - (h->c.last_non_b_time - h->c.time);
        if (h->c.pp_time <= h->c.pb_time ||
            h->c.pp_time <= h->c.pp_time - h->c.pb_time ||
            h->c.pp_time <= 0) {
            h->c.pp_time = 2;
            h->c.pb_time = 1;
        }
        ff_mpeg4_init_direct_mv(&h->c);
    }

    /* PEI */
    if (skip_1stop_8data_bits(&h->gb) < 0)
        return AVERROR_INVALIDDATA;

    if (h->h263_slice_structured) {
        if (check_marker(h->c.avctx, &h->gb, "SEPB1") != 1) {
            return -1;
        }

        ff_h263_decode_mba(h);

        if (check_marker(h->c.avctx, &h->gb, "SEPB2") != 1) {
            return -1;
        }
    }

    if (h->c.pict_type == AV_PICTURE_TYPE_B)
        h->c.low_delay = 0;

    if (h->c.h263_aic) {
         h->c.y_dc_scale_table =
         h->c.c_dc_scale_table = ff_aic_dc_scale_table;
    }else{
        h->c.y_dc_scale_table =
        h->c.c_dc_scale_table = ff_mpeg1_dc_scale_table;
    }

    ff_h263_show_pict_info(h, h263_plus);

    if (h->c.pict_type == AV_PICTURE_TYPE_I && h->c.codec_tag == AV_RL32("ZYGO") && get_bits_left(&h->gb) >= 85 + 13*3*16 + 50){
        int i,j;
        for(i=0; i<85; i++) av_log(h->c.avctx, AV_LOG_DEBUG, "%d", get_bits1(&h->gb));
        av_log(h->c.avctx, AV_LOG_DEBUG, "\n");
        for(i=0; i<13; i++){
            for(j=0; j<3; j++){
                int v= get_bits(&h->gb, 8);
                v |= get_sbits(&h->gb, 8) * (1 << 8);
                av_log(h->c.avctx, AV_LOG_DEBUG, " %5d", v);
            }
            av_log(h->c.avctx, AV_LOG_DEBUG, "\n");
        }
        for(i=0; i<50; i++) av_log(h->c.avctx, AV_LOG_DEBUG, "%d", get_bits1(&h->gb));
    }

    return 0;
}
