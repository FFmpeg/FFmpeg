/*
 * Error resilience / concealment
 *
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
 * @file error_resilience.c
 * Error resilience / concealment.
 */

#include <limits.h>

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

static void decode_mb(MpegEncContext *s){
    s->dest[0] = s->current_picture.data[0] + (s->mb_y * 16* s->linesize  ) + s->mb_x * 16;
    s->dest[1] = s->current_picture.data[1] + (s->mb_y * 8 * s->uvlinesize) + s->mb_x * 8;
    s->dest[2] = s->current_picture.data[2] + (s->mb_y * 8 * s->uvlinesize) + s->mb_x * 8;

    MPV_decode_mb(s, s->block);
}

/**
 * replaces the current MB with a flat dc only version.
 */
static void put_dc(MpegEncContext *s, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr, int mb_x, int mb_y)
{
    int dc, dcu, dcv, y, i;
    for(i=0; i<4; i++){
        dc= s->dc_val[0][mb_x*2 + (i&1) + (mb_y*2 + (i>>1))*s->b8_stride];
        if(dc<0) dc=0;
        else if(dc>2040) dc=2040;
        for(y=0; y<8; y++){
            int x;
            for(x=0; x<8; x++){
                dest_y[x + (i&1)*8 + (y + (i>>1)*8)*s->linesize]= dc/8;
            }
        }
    }
    dcu = s->dc_val[1][mb_x + mb_y*s->mb_stride];
    dcv = s->dc_val[2][mb_x + mb_y*s->mb_stride];
    if     (dcu<0   ) dcu=0;
    else if(dcu>2040) dcu=2040;
    if     (dcv<0   ) dcv=0;
    else if(dcv>2040) dcv=2040;
    for(y=0; y<8; y++){
        int x;
        for(x=0; x<8; x++){
            dest_cb[x + y*(s->uvlinesize)]= dcu/8;
            dest_cr[x + y*(s->uvlinesize)]= dcv/8;
        }
    }
}

static void filter181(int16_t *data, int width, int height, int stride){
    int x,y;

    /* horizontal filter */
    for(y=1; y<height-1; y++){
        int prev_dc= data[0 + y*stride];

        for(x=1; x<width-1; x++){
            int dc;

            dc= - prev_dc
                + data[x     + y*stride]*8
                - data[x + 1 + y*stride];
            dc= (dc*10923 + 32768)>>16;
            prev_dc= data[x + y*stride];
            data[x + y*stride]= dc;
        }
    }

    /* vertical filter */
    for(x=1; x<width-1; x++){
        int prev_dc= data[x];

        for(y=1; y<height-1; y++){
            int dc;

            dc= - prev_dc
                + data[x +  y   *stride]*8
                - data[x + (y+1)*stride];
            dc= (dc*10923 + 32768)>>16;
            prev_dc= data[x + y*stride];
            data[x + y*stride]= dc;
        }
    }
}

/**
 * guess the dc of blocks which do not have an undamaged dc
 * @param w     width in 8 pixel blocks
 * @param h     height in 8 pixel blocks
 */
static void guess_dc(MpegEncContext *s, int16_t *dc, int w, int h, int stride, int is_luma){
    int b_x, b_y;

    for(b_y=0; b_y<h; b_y++){
        for(b_x=0; b_x<w; b_x++){
            int color[4]={1024,1024,1024,1024};
            int distance[4]={9999,9999,9999,9999};
            int mb_index, error, j;
            int64_t guess, weight_sum;

            mb_index= (b_x>>is_luma) + (b_y>>is_luma)*s->mb_stride;

            error= s->error_status_table[mb_index];

            if(IS_INTER(s->current_picture.mb_type[mb_index])) continue; //inter
            if(!(error&DC_ERROR)) continue;           //dc-ok

            /* right block */
            for(j=b_x+1; j<w; j++){
                int mb_index_j= (j>>is_luma) + (b_y>>is_luma)*s->mb_stride;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= IS_INTRA(s->current_picture.mb_type[mb_index_j]);
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[0]= dc[j + b_y*stride];
                    distance[0]= j-b_x;
                    break;
                }
            }

            /* left block */
            for(j=b_x-1; j>=0; j--){
                int mb_index_j= (j>>is_luma) + (b_y>>is_luma)*s->mb_stride;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= IS_INTRA(s->current_picture.mb_type[mb_index_j]);
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[1]= dc[j + b_y*stride];
                    distance[1]= b_x-j;
                    break;
                }
            }

            /* bottom block */
            for(j=b_y+1; j<h; j++){
                int mb_index_j= (b_x>>is_luma) + (j>>is_luma)*s->mb_stride;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= IS_INTRA(s->current_picture.mb_type[mb_index_j]);
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[2]= dc[b_x + j*stride];
                    distance[2]= j-b_y;
                    break;
                }
            }

            /* top block */
            for(j=b_y-1; j>=0; j--){
                int mb_index_j= (b_x>>is_luma) + (j>>is_luma)*s->mb_stride;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= IS_INTRA(s->current_picture.mb_type[mb_index_j]);
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[3]= dc[b_x + j*stride];
                    distance[3]= b_y-j;
                    break;
                }
            }

            weight_sum=0;
            guess=0;
            for(j=0; j<4; j++){
                int64_t weight= 256*256*256*16/distance[j];
                guess+= weight*(int64_t)color[j];
                weight_sum+= weight;
            }
            guess= (guess + weight_sum/2) / weight_sum;

            dc[b_x + b_y*stride]= guess;
        }
    }
}

/**
 * simple horizontal deblocking filter used for error resilience
 * @param w     width in 8 pixel blocks
 * @param h     height in 8 pixel blocks
 */
static void h_block_filter(MpegEncContext *s, uint8_t *dst, int w, int h, int stride, int is_luma){
    int b_x, b_y;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    for(b_y=0; b_y<h; b_y++){
        for(b_x=0; b_x<w-1; b_x++){
            int y;
            int left_status = s->error_status_table[( b_x   >>is_luma) + (b_y>>is_luma)*s->mb_stride];
            int right_status= s->error_status_table[((b_x+1)>>is_luma) + (b_y>>is_luma)*s->mb_stride];
            int left_intra=   IS_INTRA(s->current_picture.mb_type      [( b_x   >>is_luma) + (b_y>>is_luma)*s->mb_stride]);
            int right_intra=  IS_INTRA(s->current_picture.mb_type      [((b_x+1)>>is_luma) + (b_y>>is_luma)*s->mb_stride]);
            int left_damage =  left_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int right_damage= right_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int offset= b_x*8 + b_y*stride*8;
            int16_t *left_mv=  s->current_picture.motion_val[0][s->b8_stride*(b_y<<(1-is_luma)) + ( b_x   <<(1-is_luma))];
            int16_t *right_mv= s->current_picture.motion_val[0][s->b8_stride*(b_y<<(1-is_luma)) + ((b_x+1)<<(1-is_luma))];

            if(!(left_damage||right_damage)) continue; // both undamaged

            if(   (!left_intra) && (!right_intra)
               && FFABS(left_mv[0]-right_mv[0]) + FFABS(left_mv[1]+right_mv[1]) < 2) continue;

            for(y=0; y<8; y++){
                int a,b,c,d;

                a= dst[offset + 7 + y*stride] - dst[offset + 6 + y*stride];
                b= dst[offset + 8 + y*stride] - dst[offset + 7 + y*stride];
                c= dst[offset + 9 + y*stride] - dst[offset + 8 + y*stride];

                d= FFABS(b) - ((FFABS(a) + FFABS(c) + 1)>>1);
                d= FFMAX(d, 0);
                if(b<0) d= -d;

                if(d==0) continue;

                if(!(left_damage && right_damage))
                    d= d*16/9;

                if(left_damage){
                    dst[offset + 7 + y*stride] = cm[dst[offset + 7 + y*stride] + ((d*7)>>4)];
                    dst[offset + 6 + y*stride] = cm[dst[offset + 6 + y*stride] + ((d*5)>>4)];
                    dst[offset + 5 + y*stride] = cm[dst[offset + 5 + y*stride] + ((d*3)>>4)];
                    dst[offset + 4 + y*stride] = cm[dst[offset + 4 + y*stride] + ((d*1)>>4)];
                }
                if(right_damage){
                    dst[offset + 8 + y*stride] = cm[dst[offset + 8 + y*stride] - ((d*7)>>4)];
                    dst[offset + 9 + y*stride] = cm[dst[offset + 9 + y*stride] - ((d*5)>>4)];
                    dst[offset + 10+ y*stride] = cm[dst[offset +10 + y*stride] - ((d*3)>>4)];
                    dst[offset + 11+ y*stride] = cm[dst[offset +11 + y*stride] - ((d*1)>>4)];
                }
            }
        }
    }
}

/**
 * simple vertical deblocking filter used for error resilience
 * @param w     width in 8 pixel blocks
 * @param h     height in 8 pixel blocks
 */
static void v_block_filter(MpegEncContext *s, uint8_t *dst, int w, int h, int stride, int is_luma){
    int b_x, b_y;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    for(b_y=0; b_y<h-1; b_y++){
        for(b_x=0; b_x<w; b_x++){
            int x;
            int top_status   = s->error_status_table[(b_x>>is_luma) + ( b_y   >>is_luma)*s->mb_stride];
            int bottom_status= s->error_status_table[(b_x>>is_luma) + ((b_y+1)>>is_luma)*s->mb_stride];
            int top_intra=     IS_INTRA(s->current_picture.mb_type      [(b_x>>is_luma) + ( b_y   >>is_luma)*s->mb_stride]);
            int bottom_intra=  IS_INTRA(s->current_picture.mb_type      [(b_x>>is_luma) + ((b_y+1)>>is_luma)*s->mb_stride]);
            int top_damage =      top_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int bottom_damage= bottom_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int offset= b_x*8 + b_y*stride*8;
            int16_t *top_mv=    s->current_picture.motion_val[0][s->b8_stride*( b_y   <<(1-is_luma)) + (b_x<<(1-is_luma))];
            int16_t *bottom_mv= s->current_picture.motion_val[0][s->b8_stride*((b_y+1)<<(1-is_luma)) + (b_x<<(1-is_luma))];

            if(!(top_damage||bottom_damage)) continue; // both undamaged

            if(   (!top_intra) && (!bottom_intra)
               && FFABS(top_mv[0]-bottom_mv[0]) + FFABS(top_mv[1]+bottom_mv[1]) < 2) continue;

            for(x=0; x<8; x++){
                int a,b,c,d;

                a= dst[offset + x + 7*stride] - dst[offset + x + 6*stride];
                b= dst[offset + x + 8*stride] - dst[offset + x + 7*stride];
                c= dst[offset + x + 9*stride] - dst[offset + x + 8*stride];

                d= FFABS(b) - ((FFABS(a) + FFABS(c)+1)>>1);
                d= FFMAX(d, 0);
                if(b<0) d= -d;

                if(d==0) continue;

                if(!(top_damage && bottom_damage))
                    d= d*16/9;

                if(top_damage){
                    dst[offset + x +  7*stride] = cm[dst[offset + x +  7*stride] + ((d*7)>>4)];
                    dst[offset + x +  6*stride] = cm[dst[offset + x +  6*stride] + ((d*5)>>4)];
                    dst[offset + x +  5*stride] = cm[dst[offset + x +  5*stride] + ((d*3)>>4)];
                    dst[offset + x +  4*stride] = cm[dst[offset + x +  4*stride] + ((d*1)>>4)];
                }
                if(bottom_damage){
                    dst[offset + x +  8*stride] = cm[dst[offset + x +  8*stride] - ((d*7)>>4)];
                    dst[offset + x +  9*stride] = cm[dst[offset + x +  9*stride] - ((d*5)>>4)];
                    dst[offset + x + 10*stride] = cm[dst[offset + x + 10*stride] - ((d*3)>>4)];
                    dst[offset + x + 11*stride] = cm[dst[offset + x + 11*stride] - ((d*1)>>4)];
                }
            }
        }
    }
}

static void guess_mv(MpegEncContext *s){
    uint8_t fixed[s->mb_stride * s->mb_height];
#define MV_FROZEN    3
#define MV_CHANGED   2
#define MV_UNCHANGED 1
    const int mb_stride = s->mb_stride;
    const int mb_width = s->mb_width;
    const int mb_height= s->mb_height;
    int i, depth, num_avail;
    int mb_x, mb_y;

    num_avail=0;
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[ i ];
        int f=0;
        int error= s->error_status_table[mb_xy];

        if(IS_INTRA(s->current_picture.mb_type[mb_xy])) f=MV_FROZEN; //intra //FIXME check
        if(!(error&MV_ERROR)) f=MV_FROZEN;           //inter with undamaged MV

        fixed[mb_xy]= f;
        if(f==MV_FROZEN)
            num_avail++;
    }

    if((!(s->avctx->error_concealment&FF_EC_GUESS_MVS)) || num_avail <= mb_width/2){
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                const int mb_xy= mb_x + mb_y*s->mb_stride;

                if(IS_INTRA(s->current_picture.mb_type[mb_xy]))  continue;
                if(!(s->error_status_table[mb_xy]&MV_ERROR)) continue;

                s->mv_dir = MV_DIR_FORWARD;
                s->mb_intra=0;
                s->mv_type = MV_TYPE_16X16;
                s->mb_skipped=0;

                s->dsp.clear_blocks(s->block[0]);

                s->mb_x= mb_x;
                s->mb_y= mb_y;
                s->mv[0][0][0]= 0;
                s->mv[0][0][1]= 0;
                decode_mb(s);
            }
        }
        return;
    }

    for(depth=0;; depth++){
        int changed, pass, none_left;

        none_left=1;
        changed=1;
        for(pass=0; (changed || pass<2) && pass<10; pass++){
            int mb_x, mb_y;
int score_sum=0;

            changed=0;
            for(mb_y=0; mb_y<s->mb_height; mb_y++){
                for(mb_x=0; mb_x<s->mb_width; mb_x++){
                    const int mb_xy= mb_x + mb_y*s->mb_stride;
                    int mv_predictor[8][2]={{0}};
                    int pred_count=0;
                    int j;
                    int best_score=256*256*256*64;
                    int best_pred=0;
                    const int mot_stride= s->b8_stride;
                    const int mot_index= mb_x*2 + mb_y*2*mot_stride;
                    int prev_x= s->current_picture.motion_val[0][mot_index][0];
                    int prev_y= s->current_picture.motion_val[0][mot_index][1];

                    if((mb_x^mb_y^pass)&1) continue;

                    if(fixed[mb_xy]==MV_FROZEN) continue;
                    assert(!IS_INTRA(s->current_picture.mb_type[mb_xy]));
                    assert(s->last_picture_ptr && s->last_picture_ptr->data[0]);

                    j=0;
                    if(mb_x>0           && fixed[mb_xy-1        ]==MV_FROZEN) j=1;
                    if(mb_x+1<mb_width  && fixed[mb_xy+1        ]==MV_FROZEN) j=1;
                    if(mb_y>0           && fixed[mb_xy-mb_stride]==MV_FROZEN) j=1;
                    if(mb_y+1<mb_height && fixed[mb_xy+mb_stride]==MV_FROZEN) j=1;
                    if(j==0) continue;

                    j=0;
                    if(mb_x>0           && fixed[mb_xy-1        ]==MV_CHANGED) j=1;
                    if(mb_x+1<mb_width  && fixed[mb_xy+1        ]==MV_CHANGED) j=1;
                    if(mb_y>0           && fixed[mb_xy-mb_stride]==MV_CHANGED) j=1;
                    if(mb_y+1<mb_height && fixed[mb_xy+mb_stride]==MV_CHANGED) j=1;
                    if(j==0 && pass>1) continue;

                    none_left=0;

                    if(mb_x>0 && fixed[mb_xy-1]){
                        mv_predictor[pred_count][0]= s->current_picture.motion_val[0][mot_index - 2][0];
                        mv_predictor[pred_count][1]= s->current_picture.motion_val[0][mot_index - 2][1];
                        pred_count++;
                    }
                    if(mb_x+1<mb_width && fixed[mb_xy+1]){
                        mv_predictor[pred_count][0]= s->current_picture.motion_val[0][mot_index + 2][0];
                        mv_predictor[pred_count][1]= s->current_picture.motion_val[0][mot_index + 2][1];
                        pred_count++;
                    }
                    if(mb_y>0 && fixed[mb_xy-mb_stride]){
                        mv_predictor[pred_count][0]= s->current_picture.motion_val[0][mot_index - mot_stride*2][0];
                        mv_predictor[pred_count][1]= s->current_picture.motion_val[0][mot_index - mot_stride*2][1];
                        pred_count++;
                    }
                    if(mb_y+1<mb_height && fixed[mb_xy+mb_stride]){
                        mv_predictor[pred_count][0]= s->current_picture.motion_val[0][mot_index + mot_stride*2][0];
                        mv_predictor[pred_count][1]= s->current_picture.motion_val[0][mot_index + mot_stride*2][1];
                        pred_count++;
                    }
                    if(pred_count==0) continue;

                    if(pred_count>1){
                        int sum_x=0, sum_y=0;
                        int max_x, max_y, min_x, min_y;

                        for(j=0; j<pred_count; j++){
                            sum_x+= mv_predictor[j][0];
                            sum_y+= mv_predictor[j][1];
                        }

                        /* mean */
                        mv_predictor[pred_count][0] = sum_x/j;
                        mv_predictor[pred_count][1] = sum_y/j;

                        /* median */
                        if(pred_count>=3){
                            min_y= min_x= 99999;
                            max_y= max_x=-99999;
                        }else{
                            min_x=min_y=max_x=max_y=0;
                        }
                        for(j=0; j<pred_count; j++){
                            max_x= FFMAX(max_x, mv_predictor[j][0]);
                            max_y= FFMAX(max_y, mv_predictor[j][1]);
                            min_x= FFMIN(min_x, mv_predictor[j][0]);
                            min_y= FFMIN(min_y, mv_predictor[j][1]);
                        }
                        mv_predictor[pred_count+1][0] = sum_x - max_x - min_x;
                        mv_predictor[pred_count+1][1] = sum_y - max_y - min_y;

                        if(pred_count==4){
                            mv_predictor[pred_count+1][0] /= 2;
                            mv_predictor[pred_count+1][1] /= 2;
                        }
                        pred_count+=2;
                    }

                    /* zero MV */
                    pred_count++;

                    /* last MV */
                    mv_predictor[pred_count][0]= s->current_picture.motion_val[0][mot_index][0];
                    mv_predictor[pred_count][1]= s->current_picture.motion_val[0][mot_index][1];
                    pred_count++;

                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra=0;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_skipped=0;

                    s->dsp.clear_blocks(s->block[0]);

                    s->mb_x= mb_x;
                    s->mb_y= mb_y;

                    for(j=0; j<pred_count; j++){
                        int score=0;
                        uint8_t *src= s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;

                        s->current_picture.motion_val[0][mot_index][0]= s->mv[0][0][0]= mv_predictor[j][0];
                        s->current_picture.motion_val[0][mot_index][1]= s->mv[0][0][1]= mv_predictor[j][1];

                        decode_mb(s);

                        if(mb_x>0 && fixed[mb_xy-1]){
                            int k;
                            for(k=0; k<16; k++)
                                score += FFABS(src[k*s->linesize-1 ]-src[k*s->linesize   ]);
                        }
                        if(mb_x+1<mb_width && fixed[mb_xy+1]){
                            int k;
                            for(k=0; k<16; k++)
                                score += FFABS(src[k*s->linesize+15]-src[k*s->linesize+16]);
                        }
                        if(mb_y>0 && fixed[mb_xy-mb_stride]){
                            int k;
                            for(k=0; k<16; k++)
                                score += FFABS(src[k-s->linesize   ]-src[k               ]);
                        }
                        if(mb_y+1<mb_height && fixed[mb_xy+mb_stride]){
                            int k;
                            for(k=0; k<16; k++)
                                score += FFABS(src[k+s->linesize*15]-src[k+s->linesize*16]);
                        }

                        if(score <= best_score){ // <= will favor the last MV
                            best_score= score;
                            best_pred= j;
                        }
                    }
score_sum+= best_score;
//FIXME no need to set s->current_picture.motion_val[0][mot_index][0] explicit
                    s->current_picture.motion_val[0][mot_index][0]= s->mv[0][0][0]= mv_predictor[best_pred][0];
                    s->current_picture.motion_val[0][mot_index][1]= s->mv[0][0][1]= mv_predictor[best_pred][1];

                    decode_mb(s);


                    if(s->mv[0][0][0] != prev_x || s->mv[0][0][1] != prev_y){
                        fixed[mb_xy]=MV_CHANGED;
                        changed++;
                    }else
                        fixed[mb_xy]=MV_UNCHANGED;
                }
            }

//            printf(".%d/%d", changed, score_sum); fflush(stdout);
        }

        if(none_left)
            return;

        for(i=0; i<s->mb_num; i++){
            int mb_xy= s->mb_index2xy[i];
            if(fixed[mb_xy])
                fixed[mb_xy]=MV_FROZEN;
        }
//        printf(":"); fflush(stdout);
    }
}

static int is_intra_more_likely(MpegEncContext *s){
    int is_intra_likely, i, j, undamaged_count, skip_amount, mb_x, mb_y;

    if(s->last_picture_ptr==NULL) return 1; //no previous frame available -> use spatial prediction

    undamaged_count=0;
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[i];
        const int error= s->error_status_table[mb_xy];
        if(!((error&DC_ERROR) && (error&MV_ERROR)))
            undamaged_count++;
    }

    if(undamaged_count < 5) return 0; //almost all MBs damaged -> use temporal prediction

#ifdef HAVE_XVMC
    //prevent dsp.sad() check, that requires access to the image
    if(s->avctx->xvmc_acceleration && s->pict_type==FF_I_TYPE) return 1;
#endif

    skip_amount= FFMAX(undamaged_count/50, 1); //check only upto 50 MBs
    is_intra_likely=0;

    j=0;
    for(mb_y= 0; mb_y<s->mb_height-1; mb_y++){
        for(mb_x= 0; mb_x<s->mb_width; mb_x++){
            int error;
            const int mb_xy= mb_x + mb_y*s->mb_stride;

            error= s->error_status_table[mb_xy];
            if((error&DC_ERROR) && (error&MV_ERROR))
                continue; //skip damaged

            j++;
            if((j%skip_amount) != 0) continue; //skip a few to speed things up

            if(s->pict_type==FF_I_TYPE){
                uint8_t *mb_ptr     = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
                uint8_t *last_mb_ptr= s->last_picture.data   [0] + mb_x*16 + mb_y*16*s->linesize;

                is_intra_likely += s->dsp.sad[0](NULL, last_mb_ptr, mb_ptr                    , s->linesize, 16);
                is_intra_likely -= s->dsp.sad[0](NULL, last_mb_ptr, last_mb_ptr+s->linesize*16, s->linesize, 16);
            }else{
                if(IS_INTRA(s->current_picture.mb_type[mb_xy]))
                   is_intra_likely++;
                else
                   is_intra_likely--;
            }
        }
    }
//printf("is_intra_likely: %d type:%d\n", is_intra_likely, s->pict_type);
    return is_intra_likely > 0;
}

void ff_er_frame_start(MpegEncContext *s){
    if(!s->error_resilience) return;

    memset(s->error_status_table, MV_ERROR|AC_ERROR|DC_ERROR|VP_START|AC_END|DC_END|MV_END, s->mb_stride*s->mb_height*sizeof(uint8_t));
    s->error_count= 3*s->mb_num;
}

/**
 * adds a slice.
 * @param endx x component of the last macroblock, can be -1 for the last of the previous line
 * @param status the status at the end (MV_END, AC_ERROR, ...), it is assumed that no earlier end or
 *               error of the same type occurred
 */
void ff_er_add_slice(MpegEncContext *s, int startx, int starty, int endx, int endy, int status){
    const int start_i= av_clip(startx + starty * s->mb_width    , 0, s->mb_num-1);
    const int end_i  = av_clip(endx   + endy   * s->mb_width    , 0, s->mb_num);
    const int start_xy= s->mb_index2xy[start_i];
    const int end_xy  = s->mb_index2xy[end_i];
    int mask= -1;

    if(start_i > end_i || start_xy > end_xy){
        av_log(s->avctx, AV_LOG_ERROR, "internal error, slice end before start\n");
        return;
    }

    if(!s->error_resilience) return;

    mask &= ~VP_START;
    if(status & (AC_ERROR|AC_END)){
        mask &= ~(AC_ERROR|AC_END);
        s->error_count -= end_i - start_i + 1;
    }
    if(status & (DC_ERROR|DC_END)){
        mask &= ~(DC_ERROR|DC_END);
        s->error_count -= end_i - start_i + 1;
    }
    if(status & (MV_ERROR|MV_END)){
        mask &= ~(MV_ERROR|MV_END);
        s->error_count -= end_i - start_i + 1;
    }

    if(status & (AC_ERROR|DC_ERROR|MV_ERROR)) s->error_count= INT_MAX;

    if(mask == ~0x7F){
        memset(&s->error_status_table[start_xy], 0, (end_xy - start_xy) * sizeof(uint8_t));
    }else{
        int i;
        for(i=start_xy; i<end_xy; i++){
            s->error_status_table[ i ] &= mask;
        }
    }

    if(end_i == s->mb_num)
        s->error_count= INT_MAX;
    else{
        s->error_status_table[end_xy] &= mask;
        s->error_status_table[end_xy] |= status;
    }

    s->error_status_table[start_xy] |= VP_START;

    if(start_xy > 0 && s->avctx->thread_count <= 1 && s->avctx->skip_top*s->mb_width < start_i){
        int prev_status= s->error_status_table[ s->mb_index2xy[start_i - 1] ];

        prev_status &= ~ VP_START;
        if(prev_status != (MV_END|DC_END|AC_END)) s->error_count= INT_MAX;
    }
}

void ff_er_frame_end(MpegEncContext *s){
    int i, mb_x, mb_y, error, error_type, dc_error, mv_error, ac_error;
    int distance;
    int threshold_part[4]= {100,100,100};
    int threshold= 50;
    int is_intra_likely;
    int size = s->b8_stride * 2 * s->mb_height;
    Picture *pic= s->current_picture_ptr;

    if(!s->error_resilience || s->error_count==0 ||
       s->error_count==3*s->mb_width*(s->avctx->skip_top + s->avctx->skip_bottom)) return;

    if(s->current_picture.motion_val[0] == NULL){
        av_log(s->avctx, AV_LOG_ERROR, "Warning MVs not available\n");

        for(i=0; i<2; i++){
            pic->ref_index[i]= av_mallocz(size * sizeof(uint8_t));
            pic->motion_val_base[i]= av_mallocz((size+4) * 2 * sizeof(uint16_t));
            pic->motion_val[i]= pic->motion_val_base[i]+4;
        }
        pic->motion_subsample_log2= 3;
        s->current_picture= *s->current_picture_ptr;
    }

    for(i=0; i<2; i++){
        if(pic->ref_index[i])
            memset(pic->ref_index[i], 0, size * sizeof(uint8_t));
    }

    if(s->avctx->debug&FF_DEBUG_ER){
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                int status= s->error_status_table[mb_x + mb_y*s->mb_stride];

                av_log(s->avctx, AV_LOG_DEBUG, "%2X ", status);
            }
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
    }

#if 1
    /* handle overlapping slices */
    for(error_type=1; error_type<=3; error_type++){
        int end_ok=0;

        for(i=s->mb_num-1; i>=0; i--){
            const int mb_xy= s->mb_index2xy[i];
            int error= s->error_status_table[mb_xy];

            if(error&(1<<error_type))
                end_ok=1;
            if(error&(8<<error_type))
                end_ok=1;

            if(!end_ok)
                s->error_status_table[mb_xy]|= 1<<error_type;

            if(error&VP_START)
                end_ok=0;
        }
    }
#endif
#if 1
    /* handle slices with partitions of different length */
    if(s->partitioned_frame){
        int end_ok=0;

        for(i=s->mb_num-1; i>=0; i--){
            const int mb_xy= s->mb_index2xy[i];
            int error= s->error_status_table[mb_xy];

            if(error&AC_END)
                end_ok=0;
            if((error&MV_END) || (error&DC_END) || (error&AC_ERROR))
                end_ok=1;

            if(!end_ok)
                s->error_status_table[mb_xy]|= AC_ERROR;

            if(error&VP_START)
                end_ok=0;
        }
    }
#endif
    /* handle missing slices */
    if(s->error_resilience>=4){
        int end_ok=1;

        for(i=s->mb_num-2; i>=s->mb_width+100; i--){ //FIXME +100 hack
            const int mb_xy= s->mb_index2xy[i];
            int error1= s->error_status_table[mb_xy  ];
            int error2= s->error_status_table[s->mb_index2xy[i+1]];

            if(error1&VP_START)
                end_ok=1;

            if(   error2==(VP_START|DC_ERROR|AC_ERROR|MV_ERROR|AC_END|DC_END|MV_END)
               && error1!=(VP_START|DC_ERROR|AC_ERROR|MV_ERROR|AC_END|DC_END|MV_END)
               && ((error1&AC_END) || (error1&DC_END) || (error1&MV_END))){ //end & uninit
                end_ok=0;
            }

            if(!end_ok)
                s->error_status_table[mb_xy]|= DC_ERROR|AC_ERROR|MV_ERROR;
        }
    }

#if 1
    /* backward mark errors */
    distance=9999999;
    for(error_type=1; error_type<=3; error_type++){
        for(i=s->mb_num-1; i>=0; i--){
            const int mb_xy= s->mb_index2xy[i];
            int error= s->error_status_table[mb_xy];

            if(!s->mbskip_table[mb_xy]) //FIXME partition specific
                distance++;
            if(error&(1<<error_type))
                distance= 0;

            if(s->partitioned_frame){
                if(distance < threshold_part[error_type-1])
                    s->error_status_table[mb_xy]|= 1<<error_type;
            }else{
                if(distance < threshold)
                    s->error_status_table[mb_xy]|= 1<<error_type;
            }

            if(error&VP_START)
                distance= 9999999;
        }
    }
#endif

    /* forward mark errors */
    error=0;
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[i];
        int old_error= s->error_status_table[mb_xy];

        if(old_error&VP_START)
            error= old_error& (DC_ERROR|AC_ERROR|MV_ERROR);
        else{
            error|= old_error& (DC_ERROR|AC_ERROR|MV_ERROR);
            s->error_status_table[mb_xy]|= error;
        }
    }
#if 1
    /* handle not partitioned case */
    if(!s->partitioned_frame){
        for(i=0; i<s->mb_num; i++){
            const int mb_xy= s->mb_index2xy[i];
            error= s->error_status_table[mb_xy];
            if(error&(AC_ERROR|DC_ERROR|MV_ERROR))
                error|= AC_ERROR|DC_ERROR|MV_ERROR;
            s->error_status_table[mb_xy]= error;
        }
    }
#endif

    dc_error= ac_error= mv_error=0;
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[i];
        error= s->error_status_table[mb_xy];
        if(error&DC_ERROR) dc_error ++;
        if(error&AC_ERROR) ac_error ++;
        if(error&MV_ERROR) mv_error ++;
    }
    av_log(s->avctx, AV_LOG_INFO, "concealing %d DC, %d AC, %d MV errors\n", dc_error, ac_error, mv_error);

    is_intra_likely= is_intra_more_likely(s);

    /* set unknown mb-type to most likely */
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[i];
        error= s->error_status_table[mb_xy];
        if(!((error&DC_ERROR) && (error&MV_ERROR)))
            continue;

        if(is_intra_likely)
            s->current_picture.mb_type[mb_xy]= MB_TYPE_INTRA4x4;
        else
            s->current_picture.mb_type[mb_xy]= MB_TYPE_16x16 | MB_TYPE_L0;
    }

    /* handle inter blocks with damaged AC */
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            const int mb_xy= mb_x + mb_y * s->mb_stride;
            const int mb_type= s->current_picture.mb_type[mb_xy];
            error= s->error_status_table[mb_xy];

            if(IS_INTRA(mb_type)) continue; //intra
            if(error&MV_ERROR) continue;              //inter with damaged MV
            if(!(error&AC_ERROR)) continue;           //undamaged inter

            s->mv_dir = MV_DIR_FORWARD;
            s->mb_intra=0;
            s->mb_skipped=0;
            if(IS_8X8(mb_type)){
                int mb_index= mb_x*2 + mb_y*2*s->b8_stride;
                int j;
                s->mv_type = MV_TYPE_8X8;
                for(j=0; j<4; j++){
                    s->mv[0][j][0] = s->current_picture.motion_val[0][ mb_index + (j&1) + (j>>1)*s->b8_stride ][0];
                    s->mv[0][j][1] = s->current_picture.motion_val[0][ mb_index + (j&1) + (j>>1)*s->b8_stride ][1];
                }
            }else{
                s->mv_type = MV_TYPE_16X16;
                s->mv[0][0][0] = s->current_picture.motion_val[0][ mb_x*2 + mb_y*2*s->b8_stride ][0];
                s->mv[0][0][1] = s->current_picture.motion_val[0][ mb_x*2 + mb_y*2*s->b8_stride ][1];
            }

            s->dsp.clear_blocks(s->block[0]);

            s->mb_x= mb_x;
            s->mb_y= mb_y;
            decode_mb(s);
        }
    }

    /* guess MVs */
    if(s->pict_type==FF_B_TYPE){
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                int xy= mb_x*2 + mb_y*2*s->b8_stride;
                const int mb_xy= mb_x + mb_y * s->mb_stride;
                const int mb_type= s->current_picture.mb_type[mb_xy];
                error= s->error_status_table[mb_xy];

                if(IS_INTRA(mb_type)) continue;
                if(!(error&MV_ERROR)) continue;           //inter with undamaged MV
                if(!(error&AC_ERROR)) continue;           //undamaged inter

                s->mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD;
                s->mb_intra=0;
                s->mv_type = MV_TYPE_16X16;
                s->mb_skipped=0;

                if(s->pp_time){
                    int time_pp= s->pp_time;
                    int time_pb= s->pb_time;

                    s->mv[0][0][0] = s->next_picture.motion_val[0][xy][0]*time_pb/time_pp;
                    s->mv[0][0][1] = s->next_picture.motion_val[0][xy][1]*time_pb/time_pp;
                    s->mv[1][0][0] = s->next_picture.motion_val[0][xy][0]*(time_pb - time_pp)/time_pp;
                    s->mv[1][0][1] = s->next_picture.motion_val[0][xy][1]*(time_pb - time_pp)/time_pp;
                }else{
                    s->mv[0][0][0]= 0;
                    s->mv[0][0][1]= 0;
                    s->mv[1][0][0]= 0;
                    s->mv[1][0][1]= 0;
                }

                s->dsp.clear_blocks(s->block[0]);
                s->mb_x= mb_x;
                s->mb_y= mb_y;
                decode_mb(s);
            }
        }
    }else
        guess_mv(s);

#ifdef HAVE_XVMC
    /* the filters below are not XvMC compatible, skip them */
    if(s->avctx->xvmc_acceleration) goto ec_clean;
#endif
    /* fill DC for inter blocks */
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            int dc, dcu, dcv, y, n;
            int16_t *dc_ptr;
            uint8_t *dest_y, *dest_cb, *dest_cr;
            const int mb_xy= mb_x + mb_y * s->mb_stride;
            const int mb_type= s->current_picture.mb_type[mb_xy];

            error= s->error_status_table[mb_xy];

            if(IS_INTRA(mb_type) && s->partitioned_frame) continue;
//            if(error&MV_ERROR) continue; //inter data damaged FIXME is this good?

            dest_y = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
            dest_cb= s->current_picture.data[1] + mb_x*8  + mb_y*8 *s->uvlinesize;
            dest_cr= s->current_picture.data[2] + mb_x*8  + mb_y*8 *s->uvlinesize;

            dc_ptr= &s->dc_val[0][mb_x*2 + mb_y*2*s->b8_stride];
            for(n=0; n<4; n++){
                dc=0;
                for(y=0; y<8; y++){
                    int x;
                    for(x=0; x<8; x++){
                       dc+= dest_y[x + (n&1)*8 + (y + (n>>1)*8)*s->linesize];
                    }
                }
                dc_ptr[(n&1) + (n>>1)*s->b8_stride]= (dc+4)>>3;
            }

            dcu=dcv=0;
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    dcu+=dest_cb[x + y*(s->uvlinesize)];
                    dcv+=dest_cr[x + y*(s->uvlinesize)];
                }
            }
            s->dc_val[1][mb_x + mb_y*s->mb_stride]= (dcu+4)>>3;
            s->dc_val[2][mb_x + mb_y*s->mb_stride]= (dcv+4)>>3;
        }
    }
#if 1
    /* guess DC for damaged blocks */
    guess_dc(s, s->dc_val[0], s->mb_width*2, s->mb_height*2, s->b8_stride, 1);
    guess_dc(s, s->dc_val[1], s->mb_width  , s->mb_height  , s->mb_stride, 0);
    guess_dc(s, s->dc_val[2], s->mb_width  , s->mb_height  , s->mb_stride, 0);
#endif
    /* filter luma DC */
    filter181(s->dc_val[0], s->mb_width*2, s->mb_height*2, s->b8_stride);

#if 1
    /* render DC only intra */
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            uint8_t *dest_y, *dest_cb, *dest_cr;
            const int mb_xy= mb_x + mb_y * s->mb_stride;
            const int mb_type= s->current_picture.mb_type[mb_xy];

            error= s->error_status_table[mb_xy];

            if(IS_INTER(mb_type)) continue;
            if(!(error&AC_ERROR)) continue;              //undamaged

            dest_y = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
            dest_cb= s->current_picture.data[1] + mb_x*8  + mb_y*8 *s->uvlinesize;
            dest_cr= s->current_picture.data[2] + mb_x*8  + mb_y*8 *s->uvlinesize;

            put_dc(s, dest_y, dest_cb, dest_cr, mb_x, mb_y);
        }
    }
#endif

    if(s->avctx->error_concealment&FF_EC_DEBLOCK){
        /* filter horizontal block boundaries */
        h_block_filter(s, s->current_picture.data[0], s->mb_width*2, s->mb_height*2, s->linesize  , 1);
        h_block_filter(s, s->current_picture.data[1], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
        h_block_filter(s, s->current_picture.data[2], s->mb_width  , s->mb_height  , s->uvlinesize, 0);

        /* filter vertical block boundaries */
        v_block_filter(s, s->current_picture.data[0], s->mb_width*2, s->mb_height*2, s->linesize  , 1);
        v_block_filter(s, s->current_picture.data[1], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
        v_block_filter(s, s->current_picture.data[2], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
    }

#ifdef HAVE_XVMC
ec_clean:
#endif
    /* clean a few tables */
    for(i=0; i<s->mb_num; i++){
        const int mb_xy= s->mb_index2xy[i];
        int error= s->error_status_table[mb_xy];

        if(s->pict_type!=FF_B_TYPE && (error&(DC_ERROR|MV_ERROR|AC_ERROR))){
            s->mbskip_table[mb_xy]=0;
        }
        s->mbintra_table[mb_xy]=1;
    }
}
