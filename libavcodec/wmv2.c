/*
 * Copyright (c) 2002 The FFmpeg Project.
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
 *
 */

/**
 * @file wmv2.c
 * wmv2 codec.
 */

#include "simple_idct.h"

#define SKIP_TYPE_NONE 0
#define SKIP_TYPE_MPEG 1
#define SKIP_TYPE_ROW  2
#define SKIP_TYPE_COL  3


typedef struct Wmv2Context{
    MpegEncContext s;
    int j_type_bit;
    int j_type;
    int flag3;
    int flag63;
    int abt_flag;
    int abt_type;
    int abt_type_table[6];
    int per_mb_abt;
    int per_block_abt;
    int mspel_bit;
    int cbp_table_index;
    int top_left_mv_flag;
    int per_mb_rl_bit;
    int skip_type;
    int hshift;

    ScanTable abt_scantable[2];
    DECLARE_ALIGNED_8(DCTELEM, abt_block2[6][64]);
}Wmv2Context;

static void wmv2_common_init(Wmv2Context * w){
    MpegEncContext * const s= &w->s;

    ff_init_scantable(s->dsp.idct_permutation, &w->abt_scantable[0], wmv2_scantableA);
    ff_init_scantable(s->dsp.idct_permutation, &w->abt_scantable[1], wmv2_scantableB);
}

#ifdef CONFIG_ENCODERS

static int encode_ext_header(Wmv2Context *w){
    MpegEncContext * const s= &w->s;
    PutBitContext pb;
    int code;

    init_put_bits(&pb, s->avctx->extradata, s->avctx->extradata_size);

    put_bits(&pb, 5, s->avctx->time_base.den / s->avctx->time_base.num); //yes 29.97 -> 29
    put_bits(&pb, 11, FFMIN(s->bit_rate/1024, 2047));

    put_bits(&pb, 1, w->mspel_bit=1);
    put_bits(&pb, 1, w->flag3=1);
    put_bits(&pb, 1, w->abt_flag=1);
    put_bits(&pb, 1, w->j_type_bit=1);
    put_bits(&pb, 1, w->top_left_mv_flag=0);
    put_bits(&pb, 1, w->per_mb_rl_bit=1);
    put_bits(&pb, 3, code=1);

    flush_put_bits(&pb);

    s->slice_height = s->mb_height / code;

    return 0;
}

static int wmv2_encode_init(AVCodecContext *avctx){
    Wmv2Context * const w= avctx->priv_data;

    if(MPV_encode_init(avctx) < 0)
        return -1;

    wmv2_common_init(w);

    avctx->extradata_size= 4;
    avctx->extradata= av_mallocz(avctx->extradata_size + 10);
    encode_ext_header(w);

    return 0;
}

#if 0 /* unused, remove? */
static int wmv2_encode_end(AVCodecContext *avctx){

    if(MPV_encode_end(avctx) < 0)
        return -1;

    avctx->extradata_size= 0;
    av_freep(&avctx->extradata);

    return 0;
}
#endif

int ff_wmv2_encode_picture_header(MpegEncContext * s, int picture_number)
{
    Wmv2Context * const w= (Wmv2Context*)s;

    put_bits(&s->pb, 1, s->pict_type - 1);
    if(s->pict_type == I_TYPE){
        put_bits(&s->pb, 7, 0);
    }
    put_bits(&s->pb, 5, s->qscale);

    s->dc_table_index = 1;
    s->mv_table_index = 1; /* only if P frame */
//    s->use_skip_mb_code = 1; /* only if P frame */
    s->per_mb_rl_table = 0;
    s->mspel= 0;
    w->per_mb_abt=0;
    w->abt_type=0;
    w->j_type=0;

    assert(s->flipflop_rounding);

    if (s->pict_type == I_TYPE) {
        assert(s->no_rounding==1);
        if(w->j_type_bit) put_bits(&s->pb, 1, w->j_type);

        if(w->per_mb_rl_bit) put_bits(&s->pb, 1, s->per_mb_rl_table);

        if(!s->per_mb_rl_table){
            code012(&s->pb, s->rl_chroma_table_index);
            code012(&s->pb, s->rl_table_index);
        }

        put_bits(&s->pb, 1, s->dc_table_index);

        s->inter_intra_pred= 0;
    }else{
        int cbp_index;

        put_bits(&s->pb, 2, SKIP_TYPE_NONE);

        code012(&s->pb, cbp_index=0);
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

        if(w->mspel_bit) put_bits(&s->pb, 1, s->mspel);

        if(w->abt_flag){
            put_bits(&s->pb, 1, w->per_mb_abt^1);
            if(!w->per_mb_abt){
                code012(&s->pb, w->abt_type);
            }
        }

        if(w->per_mb_rl_bit) put_bits(&s->pb, 1, s->per_mb_rl_table);

        if(!s->per_mb_rl_table){
            code012(&s->pb, s->rl_table_index);
            s->rl_chroma_table_index = s->rl_table_index;
        }
        put_bits(&s->pb, 1, s->dc_table_index);
        put_bits(&s->pb, 1, s->mv_table_index);

        s->inter_intra_pred= 0;//(s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE);
    }
    s->esc3_level_length= 0;
    s->esc3_run_length= 0;

    return 0;
}

// nearly idential to wmv1 but thats just because we dont use the useless M$ crap features
// its duplicated here in case someone wants to add support for these carp features
void ff_wmv2_encode_mb(MpegEncContext * s,
                       DCTELEM block[6][64],
                       int motion_x, int motion_y)
{
    Wmv2Context * const w= (Wmv2Context*)s;
    int cbp, coded_cbp, i;
    int pred_x, pred_y;
    uint8_t *coded_block;

    handle_slices(s);

    if (!s->mb_intra) {
        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }

        put_bits(&s->pb,
                 wmv2_inter_table[w->cbp_table_index][cbp + 64][1],
                 wmv2_inter_table[w->cbp_table_index][cbp + 64][0]);

        /* motion vector */
        h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
        msmpeg4_encode_motion(s, motion_x - pred_x,
                              motion_y - pred_y);
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
                pred = coded_block_pred(s, i, &coded_block);
                *coded_block = val;
                val = val ^ pred;
            }
            coded_cbp |= val << (5 - i);
        }
#if 0
        if (coded_cbp)
            printf("cbp=%x %x\n", cbp, coded_cbp);
#endif

        if (s->pict_type == I_TYPE) {
            put_bits(&s->pb,
                     ff_msmp4_mb_i_table[coded_cbp][1], ff_msmp4_mb_i_table[coded_cbp][0]);
        } else {
            put_bits(&s->pb,
                     wmv2_inter_table[w->cbp_table_index][cbp][1],
                     wmv2_inter_table[w->cbp_table_index][cbp][0]);
        }
        put_bits(&s->pb, 1, 0);         /* no AC prediction yet */
        if(s->inter_intra_pred){
            s->h263_aic_dir=0;
            put_bits(&s->pb, table_inter_intra[s->h263_aic_dir][1], table_inter_intra[s->h263_aic_dir][0]);
        }
    }

    for (i = 0; i < 6; i++) {
        msmpeg4_encode_block(s, block[i], i);
    }
}
#endif //CONFIG_ENCODERS

static void parse_mb_skip(Wmv2Context * w){
    int mb_x, mb_y;
    MpegEncContext * const s= &w->s;
    uint32_t * const mb_type= s->current_picture_ptr->mb_type;

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

    init_get_bits(&gb, s->avctx->extradata, s->avctx->extradata_size*8);

    fps                = get_bits(&gb, 5);
    s->bit_rate        = get_bits(&gb, 11)*1024;
    w->mspel_bit       = get_bits1(&gb);
    w->flag3           = get_bits1(&gb);
    w->abt_flag        = get_bits1(&gb);
    w->j_type_bit      = get_bits1(&gb);
    w->top_left_mv_flag= get_bits1(&gb);
    w->per_mb_rl_bit   = get_bits1(&gb);
    code               = get_bits(&gb, 3);

    if(code==0) return -1;

    s->slice_height = s->mb_height / code;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(s->avctx, AV_LOG_DEBUG, "fps:%d, br:%d, qpbit:%d, abt_flag:%d, j_type_bit:%d, tl_mv_flag:%d, mbrl_bit:%d, code:%d, flag3:%d, slices:%d\n",
        fps, s->bit_rate, w->mspel_bit, w->abt_flag, w->j_type_bit, w->top_left_mv_flag, w->per_mb_rl_bit, code, w->flag3,
        code);
    }
    return 0;
}

int ff_wmv2_decode_picture_header(MpegEncContext * s)
{
    Wmv2Context * const w= (Wmv2Context*)s;
    int code;

#if 0
{
int i;
for(i=0; i<s->gb.size*8; i++)
    printf("%d", get_bits1(&s->gb));
//    get_bits1(&s->gb);
printf("END\n");
return -1;
}
#endif
    if(s->picture_number==0)
        decode_ext_header(w);

    s->pict_type = get_bits(&s->gb, 1) + 1;
    if(s->pict_type == I_TYPE){
        code = get_bits(&s->gb, 7);
        av_log(s->avctx, AV_LOG_DEBUG, "I7:%X/\n", code);
    }
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    if(s->qscale < 0)
       return -1;

    return 0;
}

int ff_wmv2_decode_secondary_picture_header(MpegEncContext * s)
{
    Wmv2Context * const w= (Wmv2Context*)s;

    if (s->pict_type == I_TYPE) {
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


//    if(w->j_type)
//        return wmv2_decode_j_picture(w); //FIXME

    if(w->j_type){
        av_log(s->avctx, AV_LOG_ERROR, "J-type picture is not supported\n");
        return -1;
    }

    return 0;
}

static inline int wmv2_decode_motion(Wmv2Context *w, int *mx_ptr, int *my_ptr){
    MpegEncContext * const s= &w->s;
    int ret;

    ret= msmpeg4_decode_motion(s, mx_ptr, my_ptr);

    if(ret<0) return -1;

    if((((*mx_ptr)|(*my_ptr)) & 1) && s->mspel)
        w->hshift= get_bits1(&s->gb);
    else
        w->hshift= 0;

//printf("%d %d  ", *mx_ptr, *my_ptr);

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

static inline int wmv2_decode_inter_block(Wmv2Context *w, DCTELEM *block, int n, int cbp){
    MpegEncContext * const s= &w->s;
    static const int sub_cbp_table[3]= {2,3,1};
    int sub_cbp;

    if(!cbp){
        s->block_last_index[n] = -1;

        return 0;
    }

    if(w->per_block_abt)
        w->abt_type= decode012(&s->gb);
#if 0
    if(w->per_block_abt)
        printf("B%d", w->abt_type);
#endif
    w->abt_type_table[n]= w->abt_type;

    if(w->abt_type){
//        const uint8_t *scantable= w->abt_scantable[w->abt_type-1].permutated;
        const uint8_t *scantable= w->abt_scantable[w->abt_type-1].scantable;
//        const uint8_t *scantable= w->abt_type-1 ? w->abt_scantable[1].permutated : w->abt_scantable[0].scantable;

        sub_cbp= sub_cbp_table[ decode012(&s->gb) ];
//        printf("S%d", sub_cbp);

        if(sub_cbp&1){
            if (msmpeg4_decode_block(s, block, n, 1, scantable) < 0)
                return -1;
        }

        if(sub_cbp&2){
            if (msmpeg4_decode_block(s, w->abt_block2[n], n, 1, scantable) < 0)
                return -1;
        }
        s->block_last_index[n] = 63;

        return 0;
    }else{
        return msmpeg4_decode_block(s, block, n, 1, s->inter_scantable.permutated);
    }
}

static void wmv2_add_block(Wmv2Context *w, DCTELEM *block1, uint8_t *dst, int stride, int n){
    MpegEncContext * const s= &w->s;

  if (s->block_last_index[n] >= 0) {
    switch(w->abt_type_table[n]){
    case 0:
        s->dsp.idct_add (dst, stride, block1);
        break;
    case 1:
        simple_idct84_add(dst           , stride, block1);
        simple_idct84_add(dst + 4*stride, stride, w->abt_block2[n]);
        memset(w->abt_block2[n], 0, 64*sizeof(DCTELEM));
        break;
    case 2:
        simple_idct48_add(dst           , stride, block1);
        simple_idct48_add(dst + 4       , stride, w->abt_block2[n]);
        memset(w->abt_block2[n], 0, 64*sizeof(DCTELEM));
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "internal error in WMV2 abt\n");
    }
  }
}

void ff_wmv2_add_mb(MpegEncContext *s, DCTELEM block1[6][64], uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr){
    Wmv2Context * const w= (Wmv2Context*)s;

    wmv2_add_block(w, block1[0], dest_y                    , s->linesize, 0);
    wmv2_add_block(w, block1[1], dest_y + 8                , s->linesize, 1);
    wmv2_add_block(w, block1[2], dest_y +     8*s->linesize, s->linesize, 2);
    wmv2_add_block(w, block1[3], dest_y + 8 + 8*s->linesize, s->linesize, 3);

    if(s->flags&CODEC_FLAG_GRAY) return;

    wmv2_add_block(w, block1[4], dest_cb                   , s->uvlinesize, 4);
    wmv2_add_block(w, block1[5], dest_cr                   , s->uvlinesize, 5);
}

void ff_mspel_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                               int motion_x, int motion_y, int h)
{
    Wmv2Context * const w= (Wmv2Context*)s;
    uint8_t *ptr;
    int dxy, offset, mx, my, src_x, src_y, v_edge_pos, linesize, uvlinesize;
    int emu=0;

    dxy = ((motion_y & 1) << 1) | (motion_x & 1);
    dxy = 2*dxy + w->hshift;
    src_x = s->mb_x * 16 + (motion_x >> 1);
    src_y = s->mb_y * 16 + (motion_y >> 1);

    /* WARNING: do no forget half pels */
    v_edge_pos = s->v_edge_pos;
    src_x = av_clip(src_x, -16, s->width);
    src_y = av_clip(src_y, -16, s->height);

    if(src_x<=-16 || src_x >= s->width)
        dxy &= ~3;
    if(src_y<=-16 || src_y >= s->height)
        dxy &= ~4;

    linesize   = s->linesize;
    uvlinesize = s->uvlinesize;
    ptr = ref_picture[0] + (src_y * linesize) + src_x;

    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<1 || src_y<1 || src_x + 17  >= s->h_edge_pos
                              || src_y + h+1 >= v_edge_pos){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr - 1 - s->linesize, s->linesize, 19, 19,
                             src_x-1, src_y-1, s->h_edge_pos, s->v_edge_pos);
            ptr= s->edge_emu_buffer + 1 + s->linesize;
            emu=1;
        }
    }

    s->dsp.put_mspel_pixels_tab[dxy](dest_y             , ptr             , linesize);
    s->dsp.put_mspel_pixels_tab[dxy](dest_y+8           , ptr+8           , linesize);
    s->dsp.put_mspel_pixels_tab[dxy](dest_y  +8*linesize, ptr  +8*linesize, linesize);
    s->dsp.put_mspel_pixels_tab[dxy](dest_y+8+8*linesize, ptr+8+8*linesize, linesize);

    if(s->flags&CODEC_FLAG_GRAY) return;

    if (s->out_format == FMT_H263) {
        dxy = 0;
        if ((motion_x & 3) != 0)
            dxy |= 1;
        if ((motion_y & 3) != 0)
            dxy |= 2;
        mx = motion_x >> 2;
        my = motion_y >> 2;
    } else {
        mx = motion_x / 2;
        my = motion_y / 2;
        dxy = ((my & 1) << 1) | (mx & 1);
        mx >>= 1;
        my >>= 1;
    }

    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * 8 + my;
    src_x = av_clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = av_clip(src_y, -8, s->height >> 1);
    if (src_y == (s->height >> 1))
        dxy &= ~2;
    offset = (src_y * uvlinesize) + src_x;
    ptr = ref_picture[1] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9,
                         src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    pix_op[1][dxy](dest_cb, ptr, uvlinesize, h >> 1);

    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9,
                         src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    pix_op[1][dxy](dest_cr, ptr, uvlinesize, h >> 1);
}


static int wmv2_decode_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    Wmv2Context * const w= (Wmv2Context*)s;
    int cbp, code, i;
    uint8_t *coded_val;

    if(w->j_type) return 0;

    if (s->pict_type == P_TYPE) {
        if(IS_SKIP(s->current_picture.mb_type[s->mb_y * s->mb_stride + s->mb_x])){
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

        code = get_vlc2(&s->gb, mb_non_intra_vlc[w->cbp_table_index].table, MB_NON_INTRA_VLC_BITS, 3);
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
                int pred = coded_block_pred(s, i, &coded_val);
                val = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!s->mb_intra) {
        int mx, my;
//printf("P at %d %d\n", s->mb_x, s->mb_y);
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
//if(s->pict_type==P_TYPE)
//   printf("%d%d ", s->inter_intra_pred, cbp);
//printf("I at %d %d %d %06X\n", s->mb_x, s->mb_y, ((cbp&3)? 1 : 0) +((cbp&0x3C)? 2 : 0), show_bits(&s->gb, 24));
        s->ac_pred = get_bits1(&s->gb);
        if(s->inter_intra_pred){
            s->h263_aic_dir= get_vlc2(&s->gb, inter_intra_vlc.table, INTER_INTRA_VLC_BITS, 1);
//            printf("%d%d %d %d/", s->ac_pred, s->h263_aic_dir, s->mb_x, s->mb_y);
        }
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }

        s->dsp.clear_blocks(s->block[0]);
        for (i = 0; i < 6; i++) {
            if (msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
            {
                av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding intra block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
                return -1;
            }
        }
    }

    return 0;
}

static int wmv2_decode_init(AVCodecContext *avctx){
    Wmv2Context * const w= avctx->priv_data;

    if(ff_h263_decode_init(avctx) < 0)
        return -1;

    wmv2_common_init(w);

    return 0;
}

AVCodec wmv2_decoder = {
    "wmv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV2,
    sizeof(Wmv2Context),
    wmv2_decode_init,
    NULL,
    ff_h263_decode_end,
    ff_h263_decode_frame,
    CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_DR1,
};

#ifdef CONFIG_ENCODERS
AVCodec wmv2_encoder = {
    "wmv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV2,
    sizeof(Wmv2Context),
    wmv2_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_YUV420P, -1},
};
#endif
