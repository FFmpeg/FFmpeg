/*
 * MSMPEG4 backend for encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2013 Michael Niedermayer <michaelni@gmx.at>
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

#include "config_components.h"

#include "libavutil/thread.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "msmpeg4.h"
#include "msmpeg4dec.h"
#include "libavutil/imgutils.h"
#include "h263.h"
#include "h263data.h"
#include "h263dec.h"
#include "mpeg4videodec.h"
#include "msmpeg4data.h"
#include "wmv2dec.h"

#define DC_VLC_BITS 9
#define V2_INTRA_CBPC_VLC_BITS 3
#define V2_MB_TYPE_VLC_BITS 7
#define MV_VLC_BITS 9
#define TEX_VLC_BITS 9

#define DEFAULT_INTER_INDEX 3

static inline int msmpeg4v1_pred_dc(MpegEncContext * s, int n,
                                    int32_t **dc_val_ptr)
{
    int i;

    if (n < 4) {
        i= 0;
    } else {
        i= n-3;
    }

    *dc_val_ptr= &s->last_dc[i];
    return s->last_dc[i];
}

/****************************************/
/* decoding stuff */

VLC ff_mb_non_intra_vlc[4];
static VLC v2_dc_lum_vlc;
static VLC v2_dc_chroma_vlc;
static VLC v2_intra_cbpc_vlc;
static VLC v2_mb_type_vlc;
VLC ff_inter_intra_vlc;

/* This is identical to H.263 except that its range is multiplied by 2. */
static int msmpeg4v2_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift;

    code = get_vlc2(&s->gb, ff_h263_mv_vlc.table, H263_MV_VLC_BITS, 2);
    ff_dlog(s, "MV code %d at %d %d pred: %d\n", code, s->mb_x,s->mb_y, pred);
    if (code < 0)
        return 0xffff;

    if (code == 0)
        return pred;
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
    if (val <= -64)
        val += 64;
    else if (val >= 64)
        val -= 64;

    return val;
}

static int msmpeg4v12_decode_mb(MpegEncContext *s, int16_t block[6][64])
{
    int cbp, code, i;
    uint32_t * const mb_type_ptr = &s->current_picture.mb_type[s->mb_x + s->mb_y*s->mb_stride];

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        if (s->use_skip_mb_code) {
            if (get_bits1(&s->gb)) {
                /* skip mb */
                s->mb_intra = 0;
                for(i=0;i<6;i++)
                    s->block_last_index[i] = -1;
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skipped = 1;
                *mb_type_ptr = MB_TYPE_SKIP | MB_TYPE_L0 | MB_TYPE_16x16;
                return 0;
            }
        }

        if(s->msmpeg4_version==2)
            code = get_vlc2(&s->gb, v2_mb_type_vlc.table, V2_MB_TYPE_VLC_BITS, 1);
        else
            code = get_vlc2(&s->gb, ff_h263_inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
        if(code<0 || code>7){
            av_log(s->avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n", code, s->mb_x, s->mb_y);
            return -1;
        }

        s->mb_intra = code >>2;

        cbp = code & 0x3;
    } else {
        s->mb_intra = 1;
        if(s->msmpeg4_version==2)
            cbp= get_vlc2(&s->gb, v2_intra_cbpc_vlc.table, V2_INTRA_CBPC_VLC_BITS, 1);
        else
            cbp= get_vlc2(&s->gb, ff_h263_intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 2);
        if(cbp<0 || cbp>3){
            av_log(s->avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }
    }

    if (!s->mb_intra) {
        int mx, my, cbpy;

        cbpy= get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(s->avctx, AV_LOG_ERROR, "cbpy %d invalid at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }

        cbp|= cbpy<<2;
        if(s->msmpeg4_version==1 || (cbp&3) != 3) cbp^= 0x3C;

        ff_h263_pred_motion(s, 0, 0, &mx, &my);
        mx= msmpeg4v2_decode_motion(s, mx, 1);
        my= msmpeg4v2_decode_motion(s, my, 1);

        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;
        *mb_type_ptr = MB_TYPE_L0 | MB_TYPE_16x16;
    } else {
        int v;
        if(s->msmpeg4_version==2){
            s->ac_pred = get_bits1(&s->gb);
            v = get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);
            if (v < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "cbpy vlc invalid\n");
                return -1;
            }
            cbp|= v<<2;
        } else{
            s->ac_pred = 0;
            v = get_vlc2(&s->gb, ff_h263_cbpy_vlc.table, CBPY_VLC_BITS, 1);
            if (v < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "cbpy vlc invalid\n");
                return -1;
            }
            cbp|= v<<2;
            if(s->pict_type==AV_PICTURE_TYPE_P) cbp^=0x3C;
        }
        *mb_type_ptr = MB_TYPE_INTRA;
    }

    s->bdsp.clear_blocks(s->block[0]);
    for (i = 0; i < 6; i++) {
        if (ff_msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
        {
             av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
             return -1;
        }
    }
    return 0;
}

static int msmpeg4v34_decode_mb(MpegEncContext *s, int16_t block[6][64])
{
    int cbp, code, i;
    uint8_t *coded_val;
    uint32_t * const mb_type_ptr = &s->current_picture.mb_type[s->mb_x + s->mb_y*s->mb_stride];

    if (get_bits_left(&s->gb) <= 0)
        return AVERROR_INVALIDDATA;

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        if (s->use_skip_mb_code) {
            if (get_bits1(&s->gb)) {
                /* skip mb */
                s->mb_intra = 0;
                for(i=0;i<6;i++)
                    s->block_last_index[i] = -1;
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skipped = 1;
                *mb_type_ptr = MB_TYPE_SKIP | MB_TYPE_L0 | MB_TYPE_16x16;

                return 0;
            }
        }

        code = get_vlc2(&s->gb, ff_mb_non_intra_vlc[DEFAULT_INTER_INDEX].table, MB_NON_INTRA_VLC_BITS, 3);
        //s->mb_intra = (code & 0x40) ? 0 : 1;
        s->mb_intra = (~code & 0x40) >> 6;

        cbp = code & 0x3f;
    } else {
        s->mb_intra = 1;
        code = get_vlc2(&s->gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
        /* predict coded block pattern */
        cbp = 0;
        for(i=0;i<6;i++) {
            int val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                int pred = ff_msmpeg4_coded_block_pred(s, i, &coded_val);
                val = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!s->mb_intra) {
        int mx, my;
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }
        ff_h263_pred_motion(s, 0, 0, &mx, &my);
        ff_msmpeg4_decode_motion(s, &mx, &my);
        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;
        *mb_type_ptr = MB_TYPE_L0 | MB_TYPE_16x16;
    } else {
        ff_dlog(s, "I at %d %d %d %06X\n", s->mb_x, s->mb_y,
                ((cbp & 3) ? 1 : 0) +((cbp & 0x3C)? 2 : 0),
                show_bits(&s->gb, 24));
        s->ac_pred = get_bits1(&s->gb);
        *mb_type_ptr = MB_TYPE_INTRA;
        if(s->inter_intra_pred){
            s->h263_aic_dir= get_vlc2(&s->gb, ff_inter_intra_vlc.table, INTER_INTRA_VLC_BITS, 1);
            ff_dlog(s, "%d%d %d %d/",
                    s->ac_pred, s->h263_aic_dir, s->mb_x, s->mb_y);
        }
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }
    }

    s->bdsp.clear_blocks(s->block[0]);
    for (i = 0; i < 6; i++) {
        if (ff_msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
        {
            av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
            return -1;
        }
    }

    return 0;
}

/* init all vlc decoding tables */
static av_cold void msmpeg4_decode_init_static(void)
{
    MVTable *mv;

    INIT_FIRST_VLC_RL(ff_rl_table[0], 642);
    INIT_FIRST_VLC_RL(ff_rl_table[1], 1104);
    INIT_FIRST_VLC_RL(ff_rl_table[2], 554);
    INIT_VLC_RL(ff_rl_table[3], 940);
    INIT_VLC_RL(ff_rl_table[4], 962);
    /* ff_rl_table[5] coincides with ff_h263_rl_inter which has just been
     * initialized in ff_h263_decode_init() earlier. So just copy the VLCs. */
    av_assert1(ff_h263_rl_inter.rl_vlc[0]);
    memcpy(ff_rl_table[5].rl_vlc, ff_h263_rl_inter.rl_vlc, sizeof(ff_rl_table[5].rl_vlc));

    mv = &ff_mv_tables[0];
    INIT_VLC_STATIC(&mv->vlc, MV_VLC_BITS, MSMPEG4_MV_TABLES_NB_ELEMS + 1,
                    mv->table_mv_bits, 1, 1,
                    mv->table_mv_code, 2, 2, 3714);
    mv = &ff_mv_tables[1];
    INIT_VLC_STATIC(&mv->vlc, MV_VLC_BITS, MSMPEG4_MV_TABLES_NB_ELEMS + 1,
                    mv->table_mv_bits, 1, 1,
                    mv->table_mv_code, 2, 2, 2694);

    INIT_VLC_STATIC(&ff_msmp4_dc_luma_vlc[0], DC_VLC_BITS, 120,
                    &ff_table0_dc_lum[0][1], 8, 4,
                    &ff_table0_dc_lum[0][0], 8, 4, 1158);
    INIT_VLC_STATIC(&ff_msmp4_dc_chroma_vlc[0], DC_VLC_BITS, 120,
                    &ff_table0_dc_chroma[0][1], 8, 4,
                    &ff_table0_dc_chroma[0][0], 8, 4, 1118);
    INIT_VLC_STATIC(&ff_msmp4_dc_luma_vlc[1], DC_VLC_BITS, 120,
                    &ff_table1_dc_lum[0][1], 8, 4,
                    &ff_table1_dc_lum[0][0], 8, 4, 1476);
    INIT_VLC_STATIC(&ff_msmp4_dc_chroma_vlc[1], DC_VLC_BITS, 120,
                    &ff_table1_dc_chroma[0][1], 8, 4,
                    &ff_table1_dc_chroma[0][0], 8, 4, 1216);

    INIT_VLC_STATIC(&v2_dc_lum_vlc, DC_VLC_BITS, 512,
                    &ff_v2_dc_lum_table[0][1], 8, 4,
                    &ff_v2_dc_lum_table[0][0], 8, 4, 1472);
    INIT_VLC_STATIC(&v2_dc_chroma_vlc, DC_VLC_BITS, 512,
                    &ff_v2_dc_chroma_table[0][1], 8, 4,
                    &ff_v2_dc_chroma_table[0][0], 8, 4, 1506);

    INIT_VLC_STATIC(&v2_intra_cbpc_vlc, V2_INTRA_CBPC_VLC_BITS, 4,
                    &ff_v2_intra_cbpc[0][1], 2, 1,
                    &ff_v2_intra_cbpc[0][0], 2, 1, 8);
    INIT_VLC_STATIC(&v2_mb_type_vlc, V2_MB_TYPE_VLC_BITS, 8,
                    &ff_v2_mb_type[0][1], 2, 1,
                    &ff_v2_mb_type[0][0], 2, 1, 128);

    for (unsigned i = 0, offset = 0; i < 4; i++) {
        static VLCElem vlc_buf[1636 + 2648 + 1532 + 2488];
        ff_mb_non_intra_vlc[i].table           = &vlc_buf[offset];
        ff_mb_non_intra_vlc[i].table_allocated = FF_ARRAY_ELEMS(vlc_buf) - offset;
        init_vlc(&ff_mb_non_intra_vlc[i], MB_NON_INTRA_VLC_BITS, 128,
                 &ff_wmv2_inter_table[i][0][1], 8, 4,
                 &ff_wmv2_inter_table[i][0][0], 8, 4,
                 INIT_VLC_STATIC_OVERLONG);
        offset += ff_mb_non_intra_vlc[i].table_size;
    }

    INIT_VLC_STATIC(&ff_msmp4_mb_i_vlc, MB_INTRA_VLC_BITS, 64,
                    &ff_msmp4_mb_i_table[0][1], 4, 2,
                    &ff_msmp4_mb_i_table[0][0], 4, 2, 536);

    INIT_VLC_STATIC(&ff_inter_intra_vlc, INTER_INTRA_VLC_BITS, 4,
                    &ff_table_inter_intra[0][1], 2, 1,
                    &ff_table_inter_intra[0][0], 2, 1, 8);
}

av_cold int ff_msmpeg4_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MpegEncContext *s = avctx->priv_data;
    int ret;

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return ret;

    if (ff_h263_decode_init(avctx) < 0)
        return -1;

    ff_msmpeg4_common_init(s);

    switch(s->msmpeg4_version){
    case 1:
    case 2:
        s->decode_mb= msmpeg4v12_decode_mb;
        break;
    case 3:
    case 4:
        s->decode_mb= msmpeg4v34_decode_mb;
        break;
    case 5:
        if (CONFIG_WMV2_DECODER)
            s->decode_mb= ff_wmv2_decode_mb;
    case 6:
        //FIXME + TODO VC1 decode mb
        break;
    }

    s->slice_height= s->mb_height; //to avoid 1/0 if the first frame is not a keyframe

    ff_thread_once(&init_static_once, msmpeg4_decode_init_static);

    return 0;
}

int ff_msmpeg4_decode_picture_header(MpegEncContext * s)
{
    int code;

    // at minimum one bit per macroblock is required at least in a valid frame,
    // we discard frames much smaller than this. Frames smaller than 1/8 of the
    // smallest "black/skip" frame generally contain not much recoverable content
    // while at the same time they have the highest computational requirements
    // per byte
    if (get_bits_left(&s->gb) * 8LL < (s->width+15)/16 * ((s->height+15)/16))
        return AVERROR_INVALIDDATA;

    if(s->msmpeg4_version==1){
        int start_code = get_bits_long(&s->gb, 32);
        if(start_code!=0x00000100){
            av_log(s->avctx, AV_LOG_ERROR, "invalid startcode\n");
            return -1;
        }

        skip_bits(&s->gb, 5); // frame number */
    }

    s->pict_type = get_bits(&s->gb, 2) + 1;
    if (s->pict_type != AV_PICTURE_TYPE_I &&
        s->pict_type != AV_PICTURE_TYPE_P){
        av_log(s->avctx, AV_LOG_ERROR, "invalid picture type\n");
        return -1;
    }
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    if(s->qscale==0){
        av_log(s->avctx, AV_LOG_ERROR, "invalid qscale\n");
        return -1;
    }

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        code = get_bits(&s->gb, 5);
        if(s->msmpeg4_version==1){
            if(code==0 || code>s->mb_height){
                av_log(s->avctx, AV_LOG_ERROR, "invalid slice height %d\n", code);
                return -1;
            }

            s->slice_height = code;
        }else{
            /* 0x17: one slice, 0x18: two slices, ... */
            if (code < 0x17){
                av_log(s->avctx, AV_LOG_ERROR, "error, slice code was %X\n", code);
                return -1;
            }

            s->slice_height = s->mb_height / (code - 0x16);
        }

        switch(s->msmpeg4_version){
        case 1:
        case 2:
            s->rl_chroma_table_index = 2;
            s->rl_table_index = 2;

            s->dc_table_index = 0; //not used
            break;
        case 3:
            s->rl_chroma_table_index = decode012(&s->gb);
            s->rl_table_index = decode012(&s->gb);

            s->dc_table_index = get_bits1(&s->gb);
            break;
        case 4:
            ff_msmpeg4_decode_ext_header(s, (2+5+5+17+7)/8);

            if(s->bit_rate > MBAC_BITRATE) s->per_mb_rl_table= get_bits1(&s->gb);
            else                           s->per_mb_rl_table= 0;

            if(!s->per_mb_rl_table){
                s->rl_chroma_table_index = decode012(&s->gb);
                s->rl_table_index = decode012(&s->gb);
            }

            s->dc_table_index = get_bits1(&s->gb);
            s->inter_intra_pred= 0;
            break;
        }
        s->no_rounding = 1;
        if(s->avctx->debug&FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_DEBUG, "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d slice:%d   \n",
                s->qscale,
                s->rl_chroma_table_index,
                s->rl_table_index,
                s->dc_table_index,
                s->per_mb_rl_table,
                s->slice_height);
    } else {
        switch(s->msmpeg4_version){
        case 1:
        case 2:
            if(s->msmpeg4_version==1)
                s->use_skip_mb_code = 1;
            else
                s->use_skip_mb_code = get_bits1(&s->gb);
            s->rl_table_index = 2;
            s->rl_chroma_table_index = s->rl_table_index;
            s->dc_table_index = 0; //not used
            s->mv_table_index = 0;
            break;
        case 3:
            s->use_skip_mb_code = get_bits1(&s->gb);
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;

            s->dc_table_index = get_bits1(&s->gb);

            s->mv_table_index = get_bits1(&s->gb);
            break;
        case 4:
            s->use_skip_mb_code = get_bits1(&s->gb);

            if(s->bit_rate > MBAC_BITRATE) s->per_mb_rl_table= get_bits1(&s->gb);
            else                           s->per_mb_rl_table= 0;

            if(!s->per_mb_rl_table){
                s->rl_table_index = decode012(&s->gb);
                s->rl_chroma_table_index = s->rl_table_index;
            }

            s->dc_table_index = get_bits1(&s->gb);

            s->mv_table_index = get_bits1(&s->gb);
            s->inter_intra_pred= (s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE);
            break;
        }

        if(s->avctx->debug&FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_DEBUG, "skip:%d rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d   \n",
                s->use_skip_mb_code,
                s->rl_table_index,
                s->rl_chroma_table_index,
                s->dc_table_index,
                s->mv_table_index,
                s->per_mb_rl_table,
                s->qscale);

        if(s->flipflop_rounding){
            s->no_rounding ^= 1;
        }else{
            s->no_rounding = 0;
        }
    }
    ff_dlog(s->avctx, "%d %"PRId64" %d %d %d\n", s->pict_type, s->bit_rate,
            s->inter_intra_pred, s->width, s->height);

    s->esc3_level_length= 0;
    s->esc3_run_length= 0;

    return 0;
}

int ff_msmpeg4_decode_ext_header(MpegEncContext * s, int buf_size)
{
    int left= buf_size*8 - get_bits_count(&s->gb);
    int length= s->msmpeg4_version>=3 ? 17 : 16;
    /* the alt_bitstream reader could read over the end so we need to check it */
    if(left>=length && left<length+8)
    {
        skip_bits(&s->gb, 5); /* fps */
        s->bit_rate= get_bits(&s->gb, 11)*1024;
        if(s->msmpeg4_version>=3)
            s->flipflop_rounding= get_bits1(&s->gb);
        else
            s->flipflop_rounding= 0;
    }
    else if(left<length+8)
    {
        s->flipflop_rounding= 0;
        if(s->msmpeg4_version != 2)
            av_log(s->avctx, AV_LOG_ERROR, "ext header missing, %d left\n", left);
    }
    else
    {
        av_log(s->avctx, AV_LOG_ERROR, "I-frame too long, ignoring ext header\n");
    }

    return 0;
}

static int msmpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr)
{
    int level, pred;

    if(s->msmpeg4_version<=2){
        if (n < 4) {
            level = get_vlc2(&s->gb, v2_dc_lum_vlc.table, DC_VLC_BITS, 3);
        } else {
            level = get_vlc2(&s->gb, v2_dc_chroma_vlc.table, DC_VLC_BITS, 3);
        }
        if (level < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "illegal dc vlc\n");
            *dir_ptr = 0;
            return -1;
        }
        level-=256;
    }else{  //FIXME optimize use unified tables & index
        if (n < 4) {
            level = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        } else {
            level = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        }

        if (level == DC_MAX) {
            level = get_bits(&s->gb, 8);
            if (get_bits1(&s->gb))
                level = -level;
        } else if (level != 0) {
            if (get_bits1(&s->gb))
                level = -level;
        }
    }

    if(s->msmpeg4_version==1){
        int32_t *dc_val;
        pred = msmpeg4v1_pred_dc(s, n, &dc_val);
        level += pred;

        /* update predictor */
        *dc_val= level;
    }else{
        int16_t *dc_val;
        pred   = ff_msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);
        level += pred;

        /* update predictor */
        if (n < 4) {
            *dc_val = level * s->y_dc_scale;
        } else {
            *dc_val = level * s->c_dc_scale;
        }
    }

    return level;
}

int ff_msmpeg4_decode_block(MpegEncContext * s, int16_t * block,
                              int n, int coded, const uint8_t *scan_table)
{
    int level, i, last, run, run_diff;
    int av_uninit(dc_pred_dir);
    RLTable *rl;
    RL_VLC_ELEM *rl_vlc;
    int qmul, qadd;

    if (s->mb_intra) {
        qmul=1;
        qadd=0;

        /* DC coef */
        level = msmpeg4_decode_dc(s, n, &dc_pred_dir);

        if (level < 0){
            av_log(s->avctx, AV_LOG_ERROR, "dc overflow- block: %d qscale: %d//\n", n, s->qscale);
            if(s->inter_intra_pred) level=0;
        }
        if (n < 4) {
            rl = &ff_rl_table[s->rl_table_index];
            if(level > 256*s->y_dc_scale){
                av_log(s->avctx, AV_LOG_ERROR, "dc overflow+ L qscale: %d//\n", s->qscale);
                if(!s->inter_intra_pred) return -1;
            }
        } else {
            rl = &ff_rl_table[3 + s->rl_chroma_table_index];
            if(level > 256*s->c_dc_scale){
                av_log(s->avctx, AV_LOG_ERROR, "dc overflow+ C qscale: %d//\n", s->qscale);
                if(!s->inter_intra_pred) return -1;
            }
        }
        block[0] = level;

        run_diff = s->msmpeg4_version >= 4;
        i = 0;
        if (!coded) {
            goto not_coded;
        }
        if (s->ac_pred) {
            if (dc_pred_dir == 0)
                scan_table = s->intra_v_scantable.permutated; /* left */
            else
                scan_table = s->intra_h_scantable.permutated; /* top */
        } else {
            scan_table = s->intra_scantable.permutated;
        }
        rl_vlc= rl->rl_vlc[0];
    } else {
        qmul = s->qscale << 1;
        qadd = (s->qscale - 1) | 1;
        i = -1;
        rl = &ff_rl_table[3 + s->rl_table_index];

        if(s->msmpeg4_version==2)
            run_diff = 0;
        else
            run_diff = 1;

        if (!coded) {
            s->block_last_index[n] = i;
            return 0;
        }
        if(!scan_table)
            scan_table = s->inter_scantable.permutated;
        rl_vlc= rl->rl_vlc[s->qscale];
    }
  {
    OPEN_READER(re, &s->gb);
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 0);
        if (level==0) {
            int cache;
            cache= GET_CACHE(re, &s->gb);
            /* escape */
            if (s->msmpeg4_version==1 || (cache&0x80000000)==0) {
                if (s->msmpeg4_version==1 || (cache&0x40000000)==0) {
                    /* third escape */
                    if(s->msmpeg4_version!=1) LAST_SKIP_BITS(re, &s->gb, 2);
                    UPDATE_CACHE(re, &s->gb);
                    if(s->msmpeg4_version<=3){
                        last=  SHOW_UBITS(re, &s->gb, 1); SKIP_CACHE(re, &s->gb, 1);
                        run=   SHOW_UBITS(re, &s->gb, 6); SKIP_CACHE(re, &s->gb, 6);
                        level= SHOW_SBITS(re, &s->gb, 8);
                        SKIP_COUNTER(re, &s->gb, 1+6+8);
                    }else{
                        int sign;
                        last=  SHOW_UBITS(re, &s->gb, 1); SKIP_BITS(re, &s->gb, 1);
                        if(!s->esc3_level_length){
                            int ll;
                            ff_dlog(s->avctx, "ESC-3 %X at %d %d\n",
                                    show_bits(&s->gb, 24), s->mb_x, s->mb_y);
                            if(s->qscale<8){
                                ll= SHOW_UBITS(re, &s->gb, 3); SKIP_BITS(re, &s->gb, 3);
                                if(ll==0){
                                    ll= 8+SHOW_UBITS(re, &s->gb, 1); SKIP_BITS(re, &s->gb, 1);
                                }
                            }else{
                                ll=2;
                                while(ll<8 && SHOW_UBITS(re, &s->gb, 1)==0){
                                    ll++;
                                    SKIP_BITS(re, &s->gb, 1);
                                }
                                if(ll<8) SKIP_BITS(re, &s->gb, 1);
                            }

                            s->esc3_level_length= ll;
                            s->esc3_run_length= SHOW_UBITS(re, &s->gb, 2) + 3; SKIP_BITS(re, &s->gb, 2);
                            UPDATE_CACHE(re, &s->gb);
                        }
                        run=   SHOW_UBITS(re, &s->gb, s->esc3_run_length);
                        SKIP_BITS(re, &s->gb, s->esc3_run_length);

                        sign=  SHOW_UBITS(re, &s->gb, 1);
                        SKIP_BITS(re, &s->gb, 1);

                        level= SHOW_UBITS(re, &s->gb, s->esc3_level_length);
                        SKIP_BITS(re, &s->gb, s->esc3_level_length);
                        if(sign) level= -level;
                    }

                    //level = level * qmul + (level>0) * qadd - (level<=0) * qadd ;
                    if (level>0) level= level * qmul + qadd;
                    else         level= level * qmul - qadd;
                    i+= run + 1;
                    if(last) i+=192;
                } else {
                    /* second escape */
                    SKIP_BITS(re, &s->gb, 2);
                    GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                    i+= run + rl->max_run[run>>7][level/qmul] + run_diff; //FIXME opt indexing
                    level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                    LAST_SKIP_BITS(re, &s->gb, 1);
                }
            } else {
                /* first escape */
                SKIP_BITS(re, &s->gb, 1);
                GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                i+= run;
                level = level + rl->max_level[run>>7][(run-1)&63] * qmul;//FIXME opt indexing
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
        } else {
            i+= run;
            level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
            LAST_SKIP_BITS(re, &s->gb, 1);
        }
        if (i > 62){
            i-= 192;
            if(i&(~63)){
                const int left= get_bits_left(&s->gb);
                if (((i + 192 == 64 && level / qmul == -1) ||
                     !(s->avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT))) &&
                    left >= 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "ignoring overflow at %d %d\n", s->mb_x, s->mb_y);
                    i = 63;
                    break;
                }else{
                    av_log(s->avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
            }

            block[scan_table[i]] = level;
            break;
        }

        block[scan_table[i]] = level;
    }
    CLOSE_READER(re, &s->gb);
  }
 not_coded:
    if (s->mb_intra) {
        ff_mpeg4_pred_ac(s, block, n, dc_pred_dir);
        if (s->ac_pred) {
            i = 63; /* XXX: not optimal */
        }
    }
    if(s->msmpeg4_version>=4 && i>0) i=63; //FIXME/XXX optimize
    s->block_last_index[n] = i;

    return 0;
}

void ff_msmpeg4_decode_motion(MpegEncContext *s, int *mx_ptr, int *my_ptr)
{
    MVTable *mv;
    int code, mx, my;

    mv = &ff_mv_tables[s->mv_table_index];

    code = get_vlc2(&s->gb, mv->vlc.table, MV_VLC_BITS, 2);
    if (code == MSMPEG4_MV_TABLES_NB_ELEMS) {
        mx = get_bits(&s->gb, 6);
        my = get_bits(&s->gb, 6);
    } else {
        mx = mv->table_mvx[code];
        my = mv->table_mvy[code];
    }

    mx += *mx_ptr - 32;
    my += *my_ptr - 32;
    /* WARNING : they do not do exactly modulo encoding */
    if (mx <= -64)
        mx += 64;
    else if (mx >= 64)
        mx -= 64;

    if (my <= -64)
        my += 64;
    else if (my >= 64)
        my -= 64;
    *mx_ptr = mx;
    *my_ptr = my;
}

const FFCodec ff_msmpeg4v1_decoder = {
    .p.name         = "msmpeg4v1",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V1,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_msmpeg4_decode_init,
    .close          = ff_h263_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};

const FFCodec ff_msmpeg4v2_decoder = {
    .p.name         = "msmpeg4v2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V2,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_msmpeg4_decode_init,
    .close          = ff_h263_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};

const FFCodec ff_msmpeg4v3_decoder = {
    .p.name         = "msmpeg4",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 3"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSMPEG4V3,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_msmpeg4_decode_init,
    .close          = ff_h263_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};

const FFCodec ff_wmv1_decoder = {
    .p.name         = "wmv1",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Windows Media Video 7"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV1,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_msmpeg4_decode_init,
    .close          = ff_h263_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.max_lowres   = 3,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
