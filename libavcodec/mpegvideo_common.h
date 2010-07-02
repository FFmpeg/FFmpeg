/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * 4MV & hq & B-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
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
 * The simplest mpeg encoder (well, it was the simplest!).
 */

#ifndef AVCODEC_MPEGVIDEO_COMMON_H
#define AVCODEC_MPEGVIDEO_COMMON_H

#include <string.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "mjpegenc.h"
#include "msmpeg4.h"
#include "faandct.h"
#include <limits.h>

int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
int dct_quantize_trellis_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
void  denoise_dct_c(MpegEncContext *s, DCTELEM *block);

/**
 * allocates a Picture
 * The pixels are allocated/set by calling get_buffer() if shared=0
 */
int alloc_picture(MpegEncContext *s, Picture *pic, int shared);

/**
 * sets the given MpegEncContext to common defaults (same for encoding and decoding).
 * the changed fields will not depend upon the prior state of the MpegEncContext.
 */
void MPV_common_defaults(MpegEncContext *s);

static inline void gmc1_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               uint8_t **ref_picture)
{
    uint8_t *ptr;
    int offset, src_x, src_y, linesize, uvlinesize;
    int motion_x, motion_y;
    int emu=0;

    motion_x= s->sprite_offset[0][0];
    motion_y= s->sprite_offset[0][1];
    src_x = s->mb_x * 16 + (motion_x >> (s->sprite_warping_accuracy+1));
    src_y = s->mb_y * 16 + (motion_y >> (s->sprite_warping_accuracy+1));
    motion_x<<=(3-s->sprite_warping_accuracy);
    motion_y<<=(3-s->sprite_warping_accuracy);
    src_x = av_clip(src_x, -16, s->width);
    if (src_x == s->width)
        motion_x =0;
    src_y = av_clip(src_y, -16, s->height);
    if (src_y == s->height)
        motion_y =0;

    linesize = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0] + (src_y * linesize) + src_x;

    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(   (unsigned)src_x >= s->h_edge_pos - 17
           || (unsigned)src_y >= s->v_edge_pos - 17){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, linesize, 17, 17, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
            ptr= s->edge_emu_buffer;
        }
    }

    if((motion_x|motion_y)&7){
        s->dsp.gmc1(dest_y  , ptr  , linesize, 16, motion_x&15, motion_y&15, 128 - s->no_rounding);
        s->dsp.gmc1(dest_y+8, ptr+8, linesize, 16, motion_x&15, motion_y&15, 128 - s->no_rounding);
    }else{
        int dxy;

        dxy= ((motion_x>>3)&1) | ((motion_y>>2)&2);
        if (s->no_rounding){
            s->dsp.put_no_rnd_pixels_tab[0][dxy](dest_y, ptr, linesize, 16);
        }else{
            s->dsp.put_pixels_tab       [0][dxy](dest_y, ptr, linesize, 16);
        }
    }

    if(CONFIG_GRAY && s->flags&CODEC_FLAG_GRAY) return;

    motion_x= s->sprite_offset[1][0];
    motion_y= s->sprite_offset[1][1];
    src_x = s->mb_x * 8 + (motion_x >> (s->sprite_warping_accuracy+1));
    src_y = s->mb_y * 8 + (motion_y >> (s->sprite_warping_accuracy+1));
    motion_x<<=(3-s->sprite_warping_accuracy);
    motion_y<<=(3-s->sprite_warping_accuracy);
    src_x = av_clip(src_x, -8, s->width>>1);
    if (src_x == s->width>>1)
        motion_x =0;
    src_y = av_clip(src_y, -8, s->height>>1);
    if (src_y == s->height>>1)
        motion_y =0;

    offset = (src_y * uvlinesize) + src_x;
    ptr = ref_picture[1] + offset;
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(   (unsigned)src_x >= (s->h_edge_pos>>1) - 9
           || (unsigned)src_y >= (s->v_edge_pos>>1) - 9){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
    }
    s->dsp.gmc1(dest_cb, ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);

    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    s->dsp.gmc1(dest_cr, ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);

    return;
}

static inline void gmc_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               uint8_t **ref_picture)
{
    uint8_t *ptr;
    int linesize, uvlinesize;
    const int a= s->sprite_warping_accuracy;
    int ox, oy;

    linesize = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0];

    ox= s->sprite_offset[0][0] + s->sprite_delta[0][0]*s->mb_x*16 + s->sprite_delta[0][1]*s->mb_y*16;
    oy= s->sprite_offset[0][1] + s->sprite_delta[1][0]*s->mb_x*16 + s->sprite_delta[1][1]*s->mb_y*16;

    s->dsp.gmc(dest_y, ptr, linesize, 16,
           ox,
           oy,
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1],
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos, s->v_edge_pos);
    s->dsp.gmc(dest_y+8, ptr, linesize, 16,
           ox + s->sprite_delta[0][0]*8,
           oy + s->sprite_delta[1][0]*8,
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1],
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos, s->v_edge_pos);

    if(CONFIG_GRAY && s->flags&CODEC_FLAG_GRAY) return;

    ox= s->sprite_offset[1][0] + s->sprite_delta[0][0]*s->mb_x*8 + s->sprite_delta[0][1]*s->mb_y*8;
    oy= s->sprite_offset[1][1] + s->sprite_delta[1][0]*s->mb_x*8 + s->sprite_delta[1][1]*s->mb_y*8;

    ptr = ref_picture[1];
    s->dsp.gmc(dest_cb, ptr, uvlinesize, 8,
           ox,
           oy,
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1],
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos>>1, s->v_edge_pos>>1);

    ptr = ref_picture[2];
    s->dsp.gmc(dest_cr, ptr, uvlinesize, 8,
           ox,
           oy,
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1],
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos>>1, s->v_edge_pos>>1);
}

static inline int hpel_motion(MpegEncContext *s,
                                  uint8_t *dest, uint8_t *src,
                                  int field_based, int field_select,
                                  int src_x, int src_y,
                                  int width, int height, int stride,
                                  int h_edge_pos, int v_edge_pos,
                                  int w, int h, op_pixels_func *pix_op,
                                  int motion_x, int motion_y)
{
    int dxy;
    int emu=0;

    dxy = ((motion_y & 1) << 1) | (motion_x & 1);
    src_x += motion_x >> 1;
    src_y += motion_y >> 1;

    /* WARNING: do no forget half pels */
    src_x = av_clip(src_x, -16, width); //FIXME unneeded for emu?
    if (src_x == width)
        dxy &= ~1;
    src_y = av_clip(src_y, -16, height);
    if (src_y == height)
        dxy &= ~2;
    src += src_y * stride + src_x;

    if(s->unrestricted_mv && (s->flags&CODEC_FLAG_EMU_EDGE)){
        if(   (unsigned)src_x > h_edge_pos - (motion_x&1) - w
           || (unsigned)src_y > v_edge_pos - (motion_y&1) - h){
            ff_emulated_edge_mc(s->edge_emu_buffer, src, s->linesize, w+1, (h+1)<<field_based,
                             src_x, src_y<<field_based, h_edge_pos, s->v_edge_pos);
            src= s->edge_emu_buffer;
            emu=1;
        }
    }
    if(field_select)
        src += s->linesize;
    pix_op[dxy](dest, src, stride, h);
    return emu;
}

static av_always_inline
void mpeg_motion_internal(MpegEncContext *s,
                 uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                 int field_based, int bottom_field, int field_select,
                 uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                 int motion_x, int motion_y, int h, int is_mpeg12, int mb_y)
{
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int dxy, uvdxy, mx, my, src_x, src_y,
        uvsrc_x, uvsrc_y, v_edge_pos, uvlinesize, linesize;

#if 0
if(s->quarter_sample)
{
    motion_x>>=1;
    motion_y>>=1;
}
#endif

    v_edge_pos = s->v_edge_pos >> field_based;
    linesize   = s->current_picture.linesize[0] << field_based;
    uvlinesize = s->current_picture.linesize[1] << field_based;

    dxy = ((motion_y & 1) << 1) | (motion_x & 1);
    src_x = s->mb_x* 16               + (motion_x >> 1);
    src_y =(   mb_y<<(4-field_based)) + (motion_y >> 1);

    if (!is_mpeg12 && s->out_format == FMT_H263) {
        if((s->workaround_bugs & FF_BUG_HPEL_CHROMA) && field_based){
            mx = (motion_x>>1)|(motion_x&1);
            my = motion_y >>1;
            uvdxy = ((my & 1) << 1) | (mx & 1);
            uvsrc_x = s->mb_x* 8               + (mx >> 1);
            uvsrc_y =(   mb_y<<(3-field_based))+ (my >> 1);
        }else{
            uvdxy = dxy | (motion_y & 2) | ((motion_x & 2) >> 1);
            uvsrc_x = src_x>>1;
            uvsrc_y = src_y>>1;
        }
    }else if(!is_mpeg12 && s->out_format == FMT_H261){//even chroma mv's are full pel in H261
        mx = motion_x / 4;
        my = motion_y / 4;
        uvdxy = 0;
        uvsrc_x = s->mb_x*8 + mx;
        uvsrc_y =    mb_y*8 + my;
    } else {
        if(s->chroma_y_shift){
            mx = motion_x / 2;
            my = motion_y / 2;
            uvdxy = ((my & 1) << 1) | (mx & 1);
            uvsrc_x = s->mb_x* 8               + (mx >> 1);
            uvsrc_y =(   mb_y<<(3-field_based))+ (my >> 1);
        } else {
            if(s->chroma_x_shift){
            //Chroma422
                mx = motion_x / 2;
                uvdxy = ((motion_y & 1) << 1) | (mx & 1);
                uvsrc_x = s->mb_x* 8           + (mx >> 1);
                uvsrc_y = src_y;
            } else {
            //Chroma444
                uvdxy = dxy;
                uvsrc_x = src_x;
                uvsrc_y = src_y;
            }
        }
    }

    ptr_y  = ref_picture[0] + src_y * linesize + src_x;
    ptr_cb = ref_picture[1] + uvsrc_y * uvlinesize + uvsrc_x;
    ptr_cr = ref_picture[2] + uvsrc_y * uvlinesize + uvsrc_x;

    if(   (unsigned)src_x > s->h_edge_pos - (motion_x&1) - 16
       || (unsigned)src_y >    v_edge_pos - (motion_y&1) - h){
            if(is_mpeg12 || s->codec_id == CODEC_ID_MPEG2VIDEO ||
               s->codec_id == CODEC_ID_MPEG1VIDEO){
                av_log(s->avctx,AV_LOG_DEBUG,
                        "MPEG motion vector out of boundary (%d %d)\n", src_x, src_y);
                return;
            }
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr_y, s->linesize,
                                17, 17+field_based,
                                src_x, src_y<<field_based,
                                s->h_edge_pos, s->v_edge_pos);
            ptr_y = s->edge_emu_buffer;
            if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                uint8_t *uvbuf= s->edge_emu_buffer+18*s->linesize;
                ff_emulated_edge_mc(uvbuf ,
                                    ptr_cb, s->uvlinesize,
                                    9, 9+field_based,
                                    uvsrc_x, uvsrc_y<<field_based,
                                    s->h_edge_pos>>1, s->v_edge_pos>>1);
                ff_emulated_edge_mc(uvbuf+16,
                                    ptr_cr, s->uvlinesize,
                                    9, 9+field_based,
                                    uvsrc_x, uvsrc_y<<field_based,
                                    s->h_edge_pos>>1, s->v_edge_pos>>1);
                ptr_cb= uvbuf;
                ptr_cr= uvbuf+16;
            }
    }

    if(bottom_field){ //FIXME use this for field pix too instead of the obnoxious hack which changes picture.data
        dest_y += s->linesize;
        dest_cb+= s->uvlinesize;
        dest_cr+= s->uvlinesize;
    }

    if(field_select){
        ptr_y += s->linesize;
        ptr_cb+= s->uvlinesize;
        ptr_cr+= s->uvlinesize;
    }

    pix_op[0][dxy](dest_y, ptr_y, linesize, h);

    if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
        pix_op[s->chroma_x_shift][uvdxy]
                (dest_cb, ptr_cb, uvlinesize, h >> s->chroma_y_shift);
        pix_op[s->chroma_x_shift][uvdxy]
                (dest_cr, ptr_cr, uvlinesize, h >> s->chroma_y_shift);
    }
    if(!is_mpeg12 && (CONFIG_H261_ENCODER || CONFIG_H261_DECODER) &&
         s->out_format == FMT_H261){
        ff_h261_loop_filter(s);
    }
}
/* apply one mpeg motion vector to the three components */
static av_always_inline
void mpeg_motion(MpegEncContext *s,
                 uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                 int field_based, int bottom_field, int field_select,
                 uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                 int motion_x, int motion_y, int h, int mb_y)
{
#if !CONFIG_SMALL
    if(s->out_format == FMT_MPEG1)
        mpeg_motion_internal(s, dest_y, dest_cb, dest_cr, field_based,
                    bottom_field, field_select, ref_picture, pix_op,
                    motion_x, motion_y, h, 1, mb_y);
    else
#endif
        mpeg_motion_internal(s, dest_y, dest_cb, dest_cr, field_based,
                    bottom_field, field_select, ref_picture, pix_op,
                    motion_x, motion_y, h, 0, mb_y);
}

//FIXME move to dsputil, avg variant, 16x16 version
static inline void put_obmc(uint8_t *dst, uint8_t *src[5], int stride){
    int x;
    uint8_t * const top   = src[1];
    uint8_t * const left  = src[2];
    uint8_t * const mid   = src[0];
    uint8_t * const right = src[3];
    uint8_t * const bottom= src[4];
#define OBMC_FILTER(x, t, l, m, r, b)\
    dst[x]= (t*top[x] + l*left[x] + m*mid[x] + r*right[x] + b*bottom[x] + 4)>>3
#define OBMC_FILTER4(x, t, l, m, r, b)\
    OBMC_FILTER(x         , t, l, m, r, b);\
    OBMC_FILTER(x+1       , t, l, m, r, b);\
    OBMC_FILTER(x  +stride, t, l, m, r, b);\
    OBMC_FILTER(x+1+stride, t, l, m, r, b);

    x=0;
    OBMC_FILTER (x  , 2, 2, 4, 0, 0);
    OBMC_FILTER (x+1, 2, 1, 5, 0, 0);
    OBMC_FILTER4(x+2, 2, 1, 5, 0, 0);
    OBMC_FILTER4(x+4, 2, 0, 5, 1, 0);
    OBMC_FILTER (x+6, 2, 0, 5, 1, 0);
    OBMC_FILTER (x+7, 2, 0, 4, 2, 0);
    x+= stride;
    OBMC_FILTER (x  , 1, 2, 5, 0, 0);
    OBMC_FILTER (x+1, 1, 2, 5, 0, 0);
    OBMC_FILTER (x+6, 1, 0, 5, 2, 0);
    OBMC_FILTER (x+7, 1, 0, 5, 2, 0);
    x+= stride;
    OBMC_FILTER4(x  , 1, 2, 5, 0, 0);
    OBMC_FILTER4(x+2, 1, 1, 6, 0, 0);
    OBMC_FILTER4(x+4, 1, 0, 6, 1, 0);
    OBMC_FILTER4(x+6, 1, 0, 5, 2, 0);
    x+= 2*stride;
    OBMC_FILTER4(x  , 0, 2, 5, 0, 1);
    OBMC_FILTER4(x+2, 0, 1, 6, 0, 1);
    OBMC_FILTER4(x+4, 0, 0, 6, 1, 1);
    OBMC_FILTER4(x+6, 0, 0, 5, 2, 1);
    x+= 2*stride;
    OBMC_FILTER (x  , 0, 2, 5, 0, 1);
    OBMC_FILTER (x+1, 0, 2, 5, 0, 1);
    OBMC_FILTER4(x+2, 0, 1, 5, 0, 2);
    OBMC_FILTER4(x+4, 0, 0, 5, 1, 2);
    OBMC_FILTER (x+6, 0, 0, 5, 2, 1);
    OBMC_FILTER (x+7, 0, 0, 5, 2, 1);
    x+= stride;
    OBMC_FILTER (x  , 0, 2, 4, 0, 2);
    OBMC_FILTER (x+1, 0, 1, 5, 0, 2);
    OBMC_FILTER (x+6, 0, 0, 5, 1, 2);
    OBMC_FILTER (x+7, 0, 0, 4, 2, 2);
}

/* obmc for 1 8x8 luma block */
static inline void obmc_motion(MpegEncContext *s,
                               uint8_t *dest, uint8_t *src,
                               int src_x, int src_y,
                               op_pixels_func *pix_op,
                               int16_t mv[5][2]/* mid top left right bottom*/)
#define MID    0
{
    int i;
    uint8_t *ptr[5];

    assert(s->quarter_sample==0);

    for(i=0; i<5; i++){
        if(i && mv[i][0]==mv[MID][0] && mv[i][1]==mv[MID][1]){
            ptr[i]= ptr[MID];
        }else{
            ptr[i]= s->obmc_scratchpad + 8*(i&1) + s->linesize*8*(i>>1);
            hpel_motion(s, ptr[i], src, 0, 0,
                        src_x, src_y,
                        s->width, s->height, s->linesize,
                        s->h_edge_pos, s->v_edge_pos,
                        8, 8, pix_op,
                        mv[i][0], mv[i][1]);
        }
    }

    put_obmc(dest, ptr, s->linesize);
}

static inline void qpel_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int field_based, int bottom_field, int field_select,
                               uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                               qpel_mc_func (*qpix_op)[16],
                               int motion_x, int motion_y, int h)
{
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int dxy, uvdxy, mx, my, src_x, src_y, uvsrc_x, uvsrc_y, v_edge_pos, linesize, uvlinesize;

    dxy = ((motion_y & 3) << 2) | (motion_x & 3);
    src_x = s->mb_x *  16                 + (motion_x >> 2);
    src_y = s->mb_y * (16 >> field_based) + (motion_y >> 2);

    v_edge_pos = s->v_edge_pos >> field_based;
    linesize = s->linesize << field_based;
    uvlinesize = s->uvlinesize << field_based;

    if(field_based){
        mx= motion_x/2;
        my= motion_y>>1;
    }else if(s->workaround_bugs&FF_BUG_QPEL_CHROMA2){
        static const int rtab[8]= {0,0,1,1,0,0,0,1};
        mx= (motion_x>>1) + rtab[motion_x&7];
        my= (motion_y>>1) + rtab[motion_y&7];
    }else if(s->workaround_bugs&FF_BUG_QPEL_CHROMA){
        mx= (motion_x>>1)|(motion_x&1);
        my= (motion_y>>1)|(motion_y&1);
    }else{
        mx= motion_x/2;
        my= motion_y/2;
    }
    mx= (mx>>1)|(mx&1);
    my= (my>>1)|(my&1);

    uvdxy= (mx&1) | ((my&1)<<1);
    mx>>=1;
    my>>=1;

    uvsrc_x = s->mb_x *  8                 + mx;
    uvsrc_y = s->mb_y * (8 >> field_based) + my;

    ptr_y  = ref_picture[0] +   src_y *   linesize +   src_x;
    ptr_cb = ref_picture[1] + uvsrc_y * uvlinesize + uvsrc_x;
    ptr_cr = ref_picture[2] + uvsrc_y * uvlinesize + uvsrc_x;

    if(   (unsigned)src_x > s->h_edge_pos - (motion_x&3) - 16
       || (unsigned)src_y >    v_edge_pos - (motion_y&3) - h  ){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr_y, s->linesize,
                            17, 17+field_based, src_x, src_y<<field_based,
                            s->h_edge_pos, s->v_edge_pos);
        ptr_y= s->edge_emu_buffer;
        if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
            uint8_t *uvbuf= s->edge_emu_buffer + 18*s->linesize;
            ff_emulated_edge_mc(uvbuf, ptr_cb, s->uvlinesize,
                                9, 9 + field_based,
                                uvsrc_x, uvsrc_y<<field_based,
                                s->h_edge_pos>>1, s->v_edge_pos>>1);
            ff_emulated_edge_mc(uvbuf + 16, ptr_cr, s->uvlinesize,
                                9, 9 + field_based,
                                uvsrc_x, uvsrc_y<<field_based,
                                s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr_cb= uvbuf;
            ptr_cr= uvbuf + 16;
        }
    }

    if(!field_based)
        qpix_op[0][dxy](dest_y, ptr_y, linesize);
    else{
        if(bottom_field){
            dest_y += s->linesize;
            dest_cb+= s->uvlinesize;
            dest_cr+= s->uvlinesize;
        }

        if(field_select){
            ptr_y  += s->linesize;
            ptr_cb += s->uvlinesize;
            ptr_cr += s->uvlinesize;
        }
        //damn interlaced mode
        //FIXME boundary mirroring is not exactly correct here
        qpix_op[1][dxy](dest_y  , ptr_y  , linesize);
        qpix_op[1][dxy](dest_y+8, ptr_y+8, linesize);
    }
    if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
        pix_op[1][uvdxy](dest_cr, ptr_cr, uvlinesize, h >> 1);
        pix_op[1][uvdxy](dest_cb, ptr_cb, uvlinesize, h >> 1);
    }
}

/**
 * h263 chroma 4mv motion compensation.
 */
static inline void chroma_4mv_motion(MpegEncContext *s,
                                     uint8_t *dest_cb, uint8_t *dest_cr,
                                     uint8_t **ref_picture,
                                     op_pixels_func *pix_op,
                                     int mx, int my){
    int dxy, emu=0, src_x, src_y, offset;
    uint8_t *ptr;

    /* In case of 8X8, we construct a single chroma motion vector
       with a special rounding */
    mx= ff_h263_round_chroma(mx);
    my= ff_h263_round_chroma(my);

    dxy = ((my & 1) << 1) | (mx & 1);
    mx >>= 1;
    my >>= 1;

    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * 8 + my;
    src_x = av_clip(src_x, -8, (s->width >> 1));
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = av_clip(src_y, -8, (s->height >> 1));
    if (src_y == (s->height >> 1))
        dxy &= ~2;

    offset = (src_y * (s->uvlinesize)) + src_x;
    ptr = ref_picture[1] + offset;
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(   (unsigned)src_x > (s->h_edge_pos>>1) - (dxy &1) - 8
           || (unsigned)src_y > (s->v_edge_pos>>1) - (dxy>>1) - 8){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize,
                                9, 9, src_x, src_y,
                                s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
    }
    pix_op[dxy](dest_cb, ptr, s->uvlinesize, 8);

    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize,
                            9, 9, src_x, src_y,
                            s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    pix_op[dxy](dest_cr, ptr, s->uvlinesize, 8);
}

static inline void prefetch_motion(MpegEncContext *s, uint8_t **pix, int dir){
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    const int shift = s->quarter_sample ? 2 : 1;
    const int mx= (s->mv[dir][0][0]>>shift) + 16*s->mb_x + 8;
    const int my= (s->mv[dir][0][1]>>shift) + 16*s->mb_y;
    int off= mx + (my + (s->mb_x&3)*4)*s->linesize + 64;
    s->dsp.prefetch(pix[0]+off, s->linesize, 4);
    off= (mx>>1) + ((my>>1) + (s->mb_x&7))*s->uvlinesize + 64;
    s->dsp.prefetch(pix[1]+off, pix[2]-pix[1], 2);
}

/**
 * motion compensation of a single macroblock
 * @param s context
 * @param dest_y luma destination pointer
 * @param dest_cb chroma cb/u destination pointer
 * @param dest_cr chroma cr/v destination pointer
 * @param dir direction (0->forward, 1->backward)
 * @param ref_picture array[3] of pointers to the 3 planes of the reference picture
 * @param pix_op halfpel motion compensation function (average or put normally)
 * @param qpix_op qpel motion compensation function (average or put normally)
 * the motion vectors are taken from s->mv and the MV type from s->mv_type
 */
static av_always_inline void MPV_motion_internal(MpegEncContext *s,
                              uint8_t *dest_y, uint8_t *dest_cb,
                              uint8_t *dest_cr, int dir,
                              uint8_t **ref_picture,
                              op_pixels_func (*pix_op)[4],
                              qpel_mc_func (*qpix_op)[16], int is_mpeg12)
{
    int dxy, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    uint8_t *ptr, *dest;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    prefetch_motion(s, ref_picture, dir);

    if(!is_mpeg12 && s->obmc && s->pict_type != FF_B_TYPE){
        int16_t mv_cache[4][4][2];
        const int xy= s->mb_x + s->mb_y*s->mb_stride;
        const int mot_stride= s->b8_stride;
        const int mot_xy= mb_x*2 + mb_y*2*mot_stride;

        assert(!s->mb_skipped);

        memcpy(mv_cache[1][1], s->current_picture.motion_val[0][mot_xy           ], sizeof(int16_t)*4);
        memcpy(mv_cache[2][1], s->current_picture.motion_val[0][mot_xy+mot_stride], sizeof(int16_t)*4);
        memcpy(mv_cache[3][1], s->current_picture.motion_val[0][mot_xy+mot_stride], sizeof(int16_t)*4);

        if(mb_y==0 || IS_INTRA(s->current_picture.mb_type[xy-s->mb_stride])){
            memcpy(mv_cache[0][1], mv_cache[1][1], sizeof(int16_t)*4);
        }else{
            memcpy(mv_cache[0][1], s->current_picture.motion_val[0][mot_xy-mot_stride], sizeof(int16_t)*4);
        }

        if(mb_x==0 || IS_INTRA(s->current_picture.mb_type[xy-1])){
            AV_COPY32(mv_cache[1][0], mv_cache[1][1]);
            AV_COPY32(mv_cache[2][0], mv_cache[2][1]);
        }else{
            AV_COPY32(mv_cache[1][0], s->current_picture.motion_val[0][mot_xy-1]);
            AV_COPY32(mv_cache[2][0], s->current_picture.motion_val[0][mot_xy-1+mot_stride]);
        }

        if(mb_x+1>=s->mb_width || IS_INTRA(s->current_picture.mb_type[xy+1])){
            AV_COPY32(mv_cache[1][3], mv_cache[1][2]);
            AV_COPY32(mv_cache[2][3], mv_cache[2][2]);
        }else{
            AV_COPY32(mv_cache[1][3], s->current_picture.motion_val[0][mot_xy+2]);
            AV_COPY32(mv_cache[2][3], s->current_picture.motion_val[0][mot_xy+2+mot_stride]);
        }

        mx = 0;
        my = 0;
        for(i=0;i<4;i++) {
            const int x= (i&1)+1;
            const int y= (i>>1)+1;
            int16_t mv[5][2]= {
                {mv_cache[y][x  ][0], mv_cache[y][x  ][1]},
                {mv_cache[y-1][x][0], mv_cache[y-1][x][1]},
                {mv_cache[y][x-1][0], mv_cache[y][x-1][1]},
                {mv_cache[y][x+1][0], mv_cache[y][x+1][1]},
                {mv_cache[y+1][x][0], mv_cache[y+1][x][1]}};
            //FIXME cleanup
            obmc_motion(s, dest_y + ((i & 1) * 8) + (i >> 1) * 8 * s->linesize,
                        ref_picture[0],
                        mb_x * 16 + (i & 1) * 8, mb_y * 16 + (i >>1) * 8,
                        pix_op[1],
                        mv);

            mx += mv[0][0];
            my += mv[0][1];
        }
        if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY))
            chroma_4mv_motion(s, dest_cb, dest_cr, ref_picture, pix_op[1], mx, my);

        return;
    }

    switch(s->mv_type) {
    case MV_TYPE_16X16:
        if(s->mcsel){
            if(s->real_sprite_warping_points==1){
                gmc1_motion(s, dest_y, dest_cb, dest_cr,
                            ref_picture);
            }else{
                gmc_motion(s, dest_y, dest_cb, dest_cr,
                            ref_picture);
            }
        }else if(!is_mpeg12 && s->quarter_sample){
            qpel_motion(s, dest_y, dest_cb, dest_cr,
                        0, 0, 0,
                        ref_picture, pix_op, qpix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else if(!is_mpeg12 && (CONFIG_WMV2_DECODER || CONFIG_WMV2_ENCODER) && s->mspel){
            ff_mspel_motion(s, dest_y, dest_cb, dest_cr,
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else
        {
            mpeg_motion(s, dest_y, dest_cb, dest_cr,
                        0, 0, 0,
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16, mb_y);
        }
        break;
    case MV_TYPE_8X8:
    if (!is_mpeg12) {
        mx = 0;
        my = 0;
        if(s->quarter_sample){
            for(i=0;i<4;i++) {
                motion_x = s->mv[dir][i][0];
                motion_y = s->mv[dir][i][1];

                dxy = ((motion_y & 3) << 2) | (motion_x & 3);
                src_x = mb_x * 16 + (motion_x >> 2) + (i & 1) * 8;
                src_y = mb_y * 16 + (motion_y >> 2) + (i >>1) * 8;

                /* WARNING: do no forget half pels */
                src_x = av_clip(src_x, -16, s->width);
                if (src_x == s->width)
                    dxy &= ~3;
                src_y = av_clip(src_y, -16, s->height);
                if (src_y == s->height)
                    dxy &= ~12;

                ptr = ref_picture[0] + (src_y * s->linesize) + (src_x);
                if(s->flags&CODEC_FLAG_EMU_EDGE){
                    if(   (unsigned)src_x > s->h_edge_pos - (motion_x&3) - 8
                       || (unsigned)src_y > s->v_edge_pos - (motion_y&3) - 8 ){
                        ff_emulated_edge_mc(s->edge_emu_buffer, ptr,
                                            s->linesize, 9, 9,
                                            src_x, src_y,
                                            s->h_edge_pos, s->v_edge_pos);
                        ptr= s->edge_emu_buffer;
                    }
                }
                dest = dest_y + ((i & 1) * 8) + (i >> 1) * 8 * s->linesize;
                qpix_op[1][dxy](dest, ptr, s->linesize);

                mx += s->mv[dir][i][0]/2;
                my += s->mv[dir][i][1]/2;
            }
        }else{
            for(i=0;i<4;i++) {
                hpel_motion(s, dest_y + ((i & 1) * 8) + (i >> 1) * 8 * s->linesize,
                            ref_picture[0], 0, 0,
                            mb_x * 16 + (i & 1) * 8, mb_y * 16 + (i >>1) * 8,
                            s->width, s->height, s->linesize,
                            s->h_edge_pos, s->v_edge_pos,
                            8, 8, pix_op[1],
                            s->mv[dir][i][0], s->mv[dir][i][1]);

                mx += s->mv[dir][i][0];
                my += s->mv[dir][i][1];
            }
        }

        if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY))
            chroma_4mv_motion(s, dest_cb, dest_cr, ref_picture, pix_op[1], mx, my);
    }
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            if(!is_mpeg12 && s->quarter_sample){
                for(i=0; i<2; i++){
                    qpel_motion(s, dest_y, dest_cb, dest_cr,
                                1, i, s->field_select[dir][i],
                                ref_picture, pix_op, qpix_op,
                                s->mv[dir][i][0], s->mv[dir][i][1], 8);
                }
            }else{
                /* top field */
                mpeg_motion(s, dest_y, dest_cb, dest_cr,
                            1, 0, s->field_select[dir][0],
                            ref_picture, pix_op,
                            s->mv[dir][0][0], s->mv[dir][0][1], 8, mb_y);
                /* bottom field */
                mpeg_motion(s, dest_y, dest_cb, dest_cr,
                            1, 1, s->field_select[dir][1],
                            ref_picture, pix_op,
                            s->mv[dir][1][0], s->mv[dir][1][1], 8, mb_y);
            }
        } else {
            if(s->picture_structure != s->field_select[dir][0] + 1 && s->pict_type != FF_B_TYPE && !s->first_field){
                ref_picture= s->current_picture_ptr->data;
            }

            mpeg_motion(s, dest_y, dest_cb, dest_cr,
                        0, 0, s->field_select[dir][0],
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16, mb_y>>1);
        }
        break;
    case MV_TYPE_16X8:
        for(i=0; i<2; i++){
            uint8_t ** ref2picture;

            if(s->picture_structure == s->field_select[dir][i] + 1
               || s->pict_type == FF_B_TYPE || s->first_field){
                ref2picture= ref_picture;
            }else{
                ref2picture= s->current_picture_ptr->data;
            }

            mpeg_motion(s, dest_y, dest_cb, dest_cr,
                        0, 0, s->field_select[dir][i],
                        ref2picture, pix_op,
                        s->mv[dir][i][0], s->mv[dir][i][1] + 16*i, 8, mb_y>>1);

            dest_y += 16*s->linesize;
            dest_cb+= (16>>s->chroma_y_shift)*s->uvlinesize;
            dest_cr+= (16>>s->chroma_y_shift)*s->uvlinesize;
        }
        break;
    case MV_TYPE_DMV:
        if(s->picture_structure == PICT_FRAME){
            for(i=0; i<2; i++){
                int j;
                for(j=0; j<2; j++){
                    mpeg_motion(s, dest_y, dest_cb, dest_cr,
                                1, j, j^i,
                                ref_picture, pix_op,
                                s->mv[dir][2*i + j][0], s->mv[dir][2*i + j][1], 8, mb_y);
                }
                pix_op = s->dsp.avg_pixels_tab;
            }
        }else{
            for(i=0; i<2; i++){
                mpeg_motion(s, dest_y, dest_cb, dest_cr,
                            0, 0, s->picture_structure != i+1,
                            ref_picture, pix_op,
                            s->mv[dir][2*i][0],s->mv[dir][2*i][1],16, mb_y>>1);

                // after put we make avg of the same block
                pix_op=s->dsp.avg_pixels_tab;

                //opposite parity is always in the same frame if this is second field
                if(!s->first_field){
                    ref_picture = s->current_picture_ptr->data;
                }
            }
        }
    break;
    default: assert(0);
    }
}

static inline void MPV_motion(MpegEncContext *s,
                              uint8_t *dest_y, uint8_t *dest_cb,
                              uint8_t *dest_cr, int dir,
                              uint8_t **ref_picture,
                              op_pixels_func (*pix_op)[4],
                              qpel_mc_func (*qpix_op)[16])
{
#if !CONFIG_SMALL
    if(s->out_format == FMT_MPEG1)
        MPV_motion_internal(s, dest_y, dest_cb, dest_cr, dir,
                            ref_picture, pix_op, qpix_op, 1);
    else
#endif
        MPV_motion_internal(s, dest_y, dest_cb, dest_cr, dir,
                            ref_picture, pix_op, qpix_op, 0);
}
#endif /* AVCODEC_MPEGVIDEO_COMMON_H */
