/*
 * Copyright (c) 2002 The Libav Project
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

#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "mathops.h"
#include "msmpeg4.h"
#include "msmpeg4data.h"
#include "intrax8.h"
#include "wmv2.h"


static void parse_mb_skip(Wmv2Context * w){
    int mb_x, mb_y;
    MpegEncContext * const s= &w->s;
    uint32_t * const mb_type = s->current_picture_ptr->mb_type;

    w->skip_type= get_bits(&s->gb, 2);
    switch(w->skip_type){
    case SKIP_TYPE_NONE:
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                mb_type[mb_y*s->mb_stride + mb_x]= MB_TYPE_16x16 | MB_TYPE_L0;
            }
        }
        break;
    case SKIP_TYPE_MPEG:
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                mb_type[mb_y*s->mb_stride + mb_x]= (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
            }
        }
        break;
    case SKIP_TYPE_ROW:
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            if(get_bits1(&s->gb)){
                for(mb_x=0; mb_x<s->mb_width; mb_x++){
                    mb_type[mb_y*s->mb_stride + mb_x]=  MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                }
            }else{
                for(mb_x=0; mb_x<s->mb_width; mb_x++){
                    mb_type[mb_y*s->mb_stride + mb_x]= (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
                }
            }
        }
        break;
    case SKIP_TYPE_COL:
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            if(get_bits1(&s->gb)){
                for(mb_y=0; mb_y<s->mb_height; mb_y++){
                    mb_type[mb_y*s->mb_stride + mb_x]=  MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                }
            }else{
                for(mb_y=0; mb_y<s->mb_height; mb_y++){
                    mb_type[mb_y*s->mb_stride + mb_x]= (get_bits1(&s->gb) ? MB_TYPE_SKIP : 0) | MB_TYPE_16x16 | MB_TYPE_L0;
                }
            }
        }
        break;
    }
}

static int decode_ext_header(Wmv2Context *w){
    MpegEncContext * const s= &w->s;
    GetBitContext gb;
    int fps;
    int code;

    if(s->avctx->extradata_size<4) return -1;

    init_get_bits(&gb, s->avctx->extradata, 32);

    fps                = get_bits(&gb, 5);
    s->bit_rate        = get_bits(&gb, 11)*1024;
    w->mspel_bit       = get_bits1(&gb);
    s->loop_filter     = get_bits1(&gb);
    w->abt_flag        = get_bits1(&gb);
    w->j_type_bit      = get_bits1(&gb);
    w->top_left_mv_flag= get_bits1(&gb);
    w->per_mb_rl_bit   = get_bits1(&gb);
    code               = get_bits(&gb, 3);

    if(code==0) return -1;

    s->slice_height = s->mb_height / code;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(s->avctx, AV_LOG_DEBUG, "fps:%d, br:%d, qpbit:%d, abt_flag:%d, j_type_bit:%d, tl_mv_flag:%d, mbrl_bit:%d, code:%d, loop_filter:%d, slices:%d\n",
        fps, s->bit_rate, w->mspel_bit, w->abt_flag, w->j_type_bit, w->top_left_mv_flag, w->per_mb_rl_bit, code, s->loop_filter,
        code);
    }
    return 0;
}

int ff_wmv2_decode_picture_header(MpegEncContext * s)
{
    Wmv2Context * const w= (Wmv2Context*)s;
    int code;

    if(s->picture_number==0)
        decode_ext_header(w);

    s->pict_type = get_bits1(&s->gb) + 1;
    if(s->pict_type == AV_PICTURE_TYPE_I){
        code = get_bits(&s->gb, 7);
        av_log(s->avctx, AV_LOG_DEBUG, "I7:%X/\n", code);
    }
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    if(s->qscale <= 0)
       return -1;

    return 0;
}

int ff_wmv2_decode_secondary_picture_header(MpegEncContext * s)
{
    Wmv2Context * const w= (Wmv2Context*)s;

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        if(w->j_type_bit) w->j_type= get_bits1(&s->gb);
        else              w->j_type= 0; //FIXME check

        if(!w->j_type){
            if(w->per_mb_rl_bit) s->per_mb_rl_table= get_bits1(&s->gb);
            else                 s->per_mb_rl_table= 0;

            if(!s->per_mb_rl_table){
                s->rl_chroma_table_index = decode012(&s->gb);
                s->rl_table_index = decode012(&s->gb);
            }

            s->dc_table_index = get_bits1(&s->gb);
        }
        s->inter_intra_pred= 0;
        s->no_rounding = 1;
        if(s->avctx->debug&FF_DEBUG_PICT_INFO){
            av_log(s->avctx, AV_LOG_DEBUG, "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d j_type:%d \n",
                s->qscale,
                s->rl_chroma_table_index,
                s->rl_table_index,
                s->dc_table_index,
                s->per_mb_rl_table,
                w->j_type);
        }
    }else{
        int cbp_index;
        w->j_type=0;

        parse_mb_skip(w);
        cbp_index= decode012(&s->gb);
        if(s->qscale <= 10){
            int map[3]= {0,2,1};
            w->cbp_table_index= map[cbp_index];
        }else if(s->qscale <= 20){
            int map[3]= {1,0,2};
            w->cbp_table_index= map[cbp_index];
        }else{
            int map[3]= {2,1,0};
            w->cbp_table_index= map[cbp_index];
        }

        if(w->mspel_bit) s->mspel= get_bits1(&s->gb);
        else             s->mspel= 0; //FIXME check

        if(w->abt_flag){
            w->per_mb_abt= get_bits1(&s->gb)^1;
            if(!w->per_mb_abt){
                w->abt_type= decode012(&s->gb);
            }
        }

        if(w->per_mb_rl_bit) s->per_mb_rl_table= get_bits1(&s->gb);
        else                 s->per_mb_rl_table= 0;

        if(!s->per_mb_rl_table){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }

        s->dc_table_index = get_bits1(&s->gb);
        s->mv_table_index = get_bits1(&s->gb);

        s->inter_intra_pred= 0;//(s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE);
        s->no_rounding ^= 1;

        if(s->avctx->debug&FF_DEBUG_PICT_INFO){
            av_log(s->avctx, AV_LOG_DEBUG, "rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d mspel:%d per_mb_abt:%d abt_type:%d cbp:%d ii:%d\n",
                s->rl_table_index,
                s->rl_chroma_table_index,
                s->dc_table_index,
                s->mv_table_index,
                s->per_mb_rl_table,
                s->qscale,
                s->mspel,
                w->per_mb_abt,
                w->abt_type,
                w->cbp_table_index,
                s->inter_intra_pred);
        }
    }
    s->esc3_level_length= 0;
    s->esc3_run_length= 0;

s->picture_number++; //FIXME ?


    if(w->j_type){
        ff_intrax8_decode_picture(&w->x8, 2*s->qscale, (s->qscale-1)|1 );
        return 1;
    }

    return 0;
}

static inline int wmv2_decode_motion(Wmv2Context *w, int *mx_ptr, int *my_ptr){
    MpegEncContext * const s= &w->s;
    int ret;

    ret= ff_msmpeg4_decode_motion(s, mx_ptr, my_ptr);

    if(ret<0) return -1;

    if((((*mx_ptr)|(*my_ptr)) & 1) && s->mspel)
        w->hshift= get_bits1(&s->gb);
    else
        w->hshift= 0;

    return 0;
}

static int16_t *wmv2_pred_motion(Wmv2Context *w, int *px, int *py){
    MpegEncContext * const s= &w->s;
    int xy, wrap, diff, type;
    int16_t *A, *B, *C, *mot_val;

    wrap = s->b8_stride;
    xy = s->block_index[0];

    mot_val = s->current_picture.motion_val[0][xy];

    A = s->current_picture.motion_val[0][xy - 1];
    B = s->current_picture.motion_val[0][xy - wrap];
    C = s->current_picture.motion_val[0][xy + 2 - wrap];

    if(s->mb_x && !s->first_slice_line && !s->mspel && w->top_left_mv_flag)
        diff= FFMAX(FFABS(A[0] - B[0]), FFABS(A[1] - B[1]));
    else
        diff=0;

    if(diff >= 8)
        type= get_bits1(&s->gb);
    else
        type= 2;

    if(type == 0){
        *px= A[0];
        *py= A[1];
    }else if(type == 1){
        *px= B[0];
        *py= B[1];
    }else{
        /* special case for first (slice) line */
        if (s->first_slice_line) {
            *px = A[0];
            *py = A[1];
        } else {
            *px = mid_pred(A[0], B[0], C[0]);
            *py = mid_pred(A[1], B[1], C[1]);
        }
    }

    return mot_val;
}

static inline int wmv2_decode_inter_block(Wmv2Context *w, int16_t *block, int n, int cbp){
    MpegEncContext * const s= &w->s;
    static const int sub_cbp_table[3]= {2,3,1};
    int sub_cbp;

    if(!cbp){
        s->block_last_index[n] = -1;

        return 0;
    }

    if(w->per_block_abt)
        w->abt_type= decode012(&s->gb);
    w->abt_type_table[n]= w->abt_type;

    if(w->abt_type){
//        const uint8_t *scantable= w->abt_scantable[w->abt_type-1].permutated;
        const uint8_t *scantable= w->abt_scantable[w->abt_type-1].scantable;
//        const uint8_t *scantable= w->abt_type-1 ? w->abt_scantable[1].permutated : w->abt_scantable[0].scantable;

        sub_cbp= sub_cbp_table[ decode012(&s->gb) ];

        if(sub_cbp&1){
            if (ff_msmpeg4_decode_block(s, block, n, 1, scantable) < 0)
                return -1;
        }

        if(sub_cbp&2){
            if (ff_msmpeg4_decode_block(s, w->abt_block2[n], n, 1, scantable) < 0)
                return -1;
        }
        s->block_last_index[n] = 63;

        return 0;
    }else{
        return ff_msmpeg4_decode_block(s, block, n, 1, s->inter_scantable.permutated);
    }
}


int ff_wmv2_decode_mb(MpegEncContext *s, int16_t block[6][64])
{
    Wmv2Context * const w= (Wmv2Context*)s;
    int cbp, code, i;
    uint8_t *coded_val;

    if(w->j_type) return 0;

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        if (IS_SKIP(s->current_picture.mb_type[s->mb_y * s->mb_stride + s->mb_x])) {
            /* skip mb */
            s->mb_intra = 0;
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
            s->mb_skipped = 1;
            w->hshift=0;
            return 0;
        }

        code = get_vlc2(&s->gb, ff_mb_non_intra_vlc[w->cbp_table_index].table, MB_NON_INTRA_VLC_BITS, 3);
        if (code < 0)
            return -1;
        s->mb_intra = (~code & 0x40) >> 6;

        cbp = code & 0x3f;
    } else {
        s->mb_intra = 1;
        code = get_vlc2(&s->gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
        if (code < 0){
            av_log(s->avctx, AV_LOG_ERROR, "II-cbp illegal at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
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
        wmv2_pred_motion(w, &mx, &my);

        if(cbp){
            s->dsp.clear_blocks(s->block[0]);
            if(s->per_mb_rl_table){
                s->rl_table_index = decode012(&s->gb);
                s->rl_chroma_table_index = s->rl_table_index;
            }

            if(w->abt_flag && w->per_mb_abt){
                w->per_block_abt= get_bits1(&s->gb);
                if(!w->per_block_abt)
                    w->abt_type= decode012(&s->gb);
            }else
                w->per_block_abt=0;
        }

        if (wmv2_decode_motion(w, &mx, &my) < 0)
            return -1;

        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;

        for (i = 0; i < 6; i++) {
            if (wmv2_decode_inter_block(w, block[i], i, (cbp >> (5 - i)) & 1) < 0)
            {
                av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding inter block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
                return -1;
            }
        }
    } else {
        if (s->pict_type==AV_PICTURE_TYPE_P)
            av_dlog(s->avctx, "%d%d ", s->inter_intra_pred, cbp);
        av_dlog(s->avctx, "I at %d %d %d %06X\n", s->mb_x, s->mb_y,
                ((cbp & 3) ? 1 : 0) +((cbp & 0x3C)? 2 : 0),
                show_bits(&s->gb, 24));
        s->ac_pred = get_bits1(&s->gb);
        if(s->inter_intra_pred){
            s->h263_aic_dir= get_vlc2(&s->gb, ff_inter_intra_vlc.table, INTER_INTRA_VLC_BITS, 1);
            av_dlog(s->avctx, "%d%d %d %d/",
                    s->ac_pred, s->h263_aic_dir, s->mb_x, s->mb_y);
        }
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }

        s->dsp.clear_blocks(s->block[0]);
        for (i = 0; i < 6; i++) {
            if (ff_msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
            {
                av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding intra block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
                return -1;
            }
        }
    }

    return 0;
}

static av_cold int wmv2_decode_init(AVCodecContext *avctx){
    Wmv2Context * const w= avctx->priv_data;

    if(ff_msmpeg4_decode_init(avctx) < 0)
        return -1;

    ff_wmv2_common_init(w);

    ff_intrax8_common_init(&w->x8,&w->s);

    return 0;
}

static av_cold int wmv2_decode_end(AVCodecContext *avctx)
{
    Wmv2Context *w = avctx->priv_data;

    ff_intrax8_common_end(&w->x8);
    return ff_h263_decode_end(avctx);
}

AVCodec ff_wmv2_decoder = {
    .name           = "wmv2",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 8"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV2,
    .priv_data_size = sizeof(Wmv2Context),
    .init           = wmv2_decode_init,
    .close          = wmv2_decode_end,
    .decode         = ff_h263_decode_frame,
    .capabilities   = CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
    .pix_fmts       = ff_pixfmt_list_420,
};
