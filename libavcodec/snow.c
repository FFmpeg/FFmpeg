/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "snow_dwt.h"
#include "snow.h"
#include "snowdata.h"


void ff_snow_inner_add_yblock(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                              int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8){
    int y, x;
    IDWTELEM * dst;
    for(y=0; y<b_h; y++){
        //FIXME ugly misuse of obmc_stride
        const uint8_t *obmc1= obmc + y*obmc_stride;
        const uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        const uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        const uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        dst = slice_buffer_get_line(sb, src_y + y);
        for(x=0; x<b_w; x++){
            int v=   obmc1[x] * block[3][x + y*src_stride]
                    +obmc2[x] * block[2][x + y*src_stride]
                    +obmc3[x] * block[1][x + y*src_stride]
                    +obmc4[x] * block[0][x + y*src_stride];

            v <<= 8 - LOG2_OBMC_MAX;
            if(FRAC_BITS != 8){
                v >>= 8 - FRAC_BITS;
            }
            if(add){
                v += dst[x + src_x];
                v = (v + (1<<(FRAC_BITS-1))) >> FRAC_BITS;
                if(v&(~255)) v= ~(v>>31);
                dst8[x + y*src_stride] = v;
            }else{
                dst[x + src_x] -= v;
            }
        }
    }
}

void ff_snow_reset_contexts(SnowContext *s){ //FIXME better initial contexts
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<3; plane_index++){
        for(level=0; level<MAX_DECOMPOSITIONS; level++){
            for(orientation=level ? 1:0; orientation<4; orientation++){
                memset(s->plane[plane_index].band[level][orientation].state, MID_STATE, sizeof(s->plane[plane_index].band[level][orientation].state));
            }
        }
    }
    memset(s->header_state, MID_STATE, sizeof(s->header_state));
    memset(s->block_state, MID_STATE, sizeof(s->block_state));
}

int ff_snow_alloc_blocks(SnowContext *s){
    int w= AV_CEIL_RSHIFT(s->avctx->width,  LOG2_MB_SIZE);
    int h= AV_CEIL_RSHIFT(s->avctx->height, LOG2_MB_SIZE);

    s->b_width = w;
    s->b_height= h;

    av_free(s->block);
    s->block = av_calloc(w * h,  sizeof(*s->block) << (s->block_max_depth*2));
    if (!s->block)
        return AVERROR(ENOMEM);

    return 0;
}

static void mc_block(Plane *p, uint8_t *dst, const uint8_t *src, int stride, int b_w, int b_h, int dx, int dy){
    static const uint8_t weight[64]={
    8,7,6,5,4,3,2,1,
    7,7,0,0,0,0,0,1,
    6,0,6,0,0,0,2,0,
    5,0,0,5,0,3,0,0,
    4,0,0,0,4,0,0,0,
    3,0,0,5,0,3,0,0,
    2,0,6,0,0,0,2,0,
    1,7,0,0,0,0,0,1,
    };

    static const uint8_t brane[256]={
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x11,0x12,0x12,0x12,0x12,0x12,0x12,0x12,
    0x04,0x05,0xcc,0xcc,0xcc,0xcc,0xcc,0x41,0x15,0x16,0xcc,0xcc,0xcc,0xcc,0xcc,0x52,
    0x04,0xcc,0x05,0xcc,0xcc,0xcc,0x41,0xcc,0x15,0xcc,0x16,0xcc,0xcc,0xcc,0x52,0xcc,
    0x04,0xcc,0xcc,0x05,0xcc,0x41,0xcc,0xcc,0x15,0xcc,0xcc,0x16,0xcc,0x52,0xcc,0xcc,
    0x04,0xcc,0xcc,0xcc,0x41,0xcc,0xcc,0xcc,0x15,0xcc,0xcc,0xcc,0x16,0xcc,0xcc,0xcc,
    0x04,0xcc,0xcc,0x41,0xcc,0x05,0xcc,0xcc,0x15,0xcc,0xcc,0x52,0xcc,0x16,0xcc,0xcc,
    0x04,0xcc,0x41,0xcc,0xcc,0xcc,0x05,0xcc,0x15,0xcc,0x52,0xcc,0xcc,0xcc,0x16,0xcc,
    0x04,0x41,0xcc,0xcc,0xcc,0xcc,0xcc,0x05,0x15,0x52,0xcc,0xcc,0xcc,0xcc,0xcc,0x16,
    0x44,0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x55,0x56,0x56,0x56,0x56,0x56,0x56,0x56,
    0x48,0x49,0xcc,0xcc,0xcc,0xcc,0xcc,0x85,0x59,0x5A,0xcc,0xcc,0xcc,0xcc,0xcc,0x96,
    0x48,0xcc,0x49,0xcc,0xcc,0xcc,0x85,0xcc,0x59,0xcc,0x5A,0xcc,0xcc,0xcc,0x96,0xcc,
    0x48,0xcc,0xcc,0x49,0xcc,0x85,0xcc,0xcc,0x59,0xcc,0xcc,0x5A,0xcc,0x96,0xcc,0xcc,
    0x48,0xcc,0xcc,0xcc,0x49,0xcc,0xcc,0xcc,0x59,0xcc,0xcc,0xcc,0x96,0xcc,0xcc,0xcc,
    0x48,0xcc,0xcc,0x85,0xcc,0x49,0xcc,0xcc,0x59,0xcc,0xcc,0x96,0xcc,0x5A,0xcc,0xcc,
    0x48,0xcc,0x85,0xcc,0xcc,0xcc,0x49,0xcc,0x59,0xcc,0x96,0xcc,0xcc,0xcc,0x5A,0xcc,
    0x48,0x85,0xcc,0xcc,0xcc,0xcc,0xcc,0x49,0x59,0x96,0xcc,0xcc,0xcc,0xcc,0xcc,0x5A,
    };

    static const uint8_t needs[16]={
    0,1,0,0,
    2,4,2,0,
    0,1,0,0,
    15
    };

    int x, y, b, r, l;
    int16_t tmpIt   [64*(32+HTAPS_MAX)];
    uint8_t tmp2t[3][64*(32+HTAPS_MAX)];
    int16_t *tmpI= tmpIt;
    uint8_t *tmp2= tmp2t[0];
    const uint8_t *hpel[11];
    av_assert2(dx<16 && dy<16);
    r= brane[dx + 16*dy]&15;
    l= brane[dx + 16*dy]>>4;

    b= needs[l] | needs[r];
    if(p && !p->diag_mc)
        b= 15;

    if(b&5){
        for(y=0; y < b_h+HTAPS_MAX-1; y++){
            for(x=0; x < b_w; x++){
                int a_1=src[x + HTAPS_MAX/2-4];
                int a0= src[x + HTAPS_MAX/2-3];
                int a1= src[x + HTAPS_MAX/2-2];
                int a2= src[x + HTAPS_MAX/2-1];
                int a3= src[x + HTAPS_MAX/2+0];
                int a4= src[x + HTAPS_MAX/2+1];
                int a5= src[x + HTAPS_MAX/2+2];
                int a6= src[x + HTAPS_MAX/2+3];
                int am=0;
                if(!p || p->fast_mc){
                    am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
                    tmpI[x]= am;
                    am= (am+16)>>5;
                }else{
                    am= p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6);
                    tmpI[x]= am;
                    am= (am+32)>>6;
                }

                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            tmpI+= 64;
            tmp2+= 64;
            src += stride;
        }
        src -= stride*y;
    }
    src += HTAPS_MAX/2 - 1;
    tmp2= tmp2t[1];

    if(b&2){
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w+1; x++){
                int a_1=src[x + (HTAPS_MAX/2-4)*stride];
                int a0= src[x + (HTAPS_MAX/2-3)*stride];
                int a1= src[x + (HTAPS_MAX/2-2)*stride];
                int a2= src[x + (HTAPS_MAX/2-1)*stride];
                int a3= src[x + (HTAPS_MAX/2+0)*stride];
                int a4= src[x + (HTAPS_MAX/2+1)*stride];
                int a5= src[x + (HTAPS_MAX/2+2)*stride];
                int a6= src[x + (HTAPS_MAX/2+3)*stride];
                int am=0;
                if(!p || p->fast_mc)
                    am= (20*(a2+a3) - 5*(a1+a4) + (a0+a5) + 16)>>5;
                else
                    am= (p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6) + 32)>>6;

                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            src += stride;
            tmp2+= 64;
        }
        src -= stride*y;
    }
    src += stride*(HTAPS_MAX/2 - 1);
    tmp2= tmp2t[2];
    tmpI= tmpIt;
    if(b&4){
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                int a_1=tmpI[x + (HTAPS_MAX/2-4)*64];
                int a0= tmpI[x + (HTAPS_MAX/2-3)*64];
                int a1= tmpI[x + (HTAPS_MAX/2-2)*64];
                int a2= tmpI[x + (HTAPS_MAX/2-1)*64];
                int a3= tmpI[x + (HTAPS_MAX/2+0)*64];
                int a4= tmpI[x + (HTAPS_MAX/2+1)*64];
                int a5= tmpI[x + (HTAPS_MAX/2+2)*64];
                int a6= tmpI[x + (HTAPS_MAX/2+3)*64];
                int am=0;
                if(!p || p->fast_mc)
                    am= (20*(a2+a3) - 5*(a1+a4) + (a0+a5) + 512)>>10;
                else
                    am= (p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6) + 2048)>>12;
                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            tmpI+= 64;
            tmp2+= 64;
        }
    }

    hpel[ 0]= src;
    hpel[ 1]= tmp2t[0] + 64*(HTAPS_MAX/2-1);
    hpel[ 2]= src + 1;

    hpel[ 4]= tmp2t[1];
    hpel[ 5]= tmp2t[2];
    hpel[ 6]= tmp2t[1] + 1;

    hpel[ 8]= src + stride;
    hpel[ 9]= hpel[1] + 64;
    hpel[10]= hpel[8] + 1;

#define MC_STRIDE(x) (needs[x] ? 64 : stride)

    if(b==15){
        int dxy = dx / 8 + dy / 8 * 4;
        const uint8_t *src1 = hpel[dxy    ];
        const uint8_t *src2 = hpel[dxy + 1];
        const uint8_t *src3 = hpel[dxy + 4];
        const uint8_t *src4 = hpel[dxy + 5];
        int stride1 = MC_STRIDE(dxy);
        int stride2 = MC_STRIDE(dxy + 1);
        int stride3 = MC_STRIDE(dxy + 4);
        int stride4 = MC_STRIDE(dxy + 5);
        dx&=7;
        dy&=7;
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                dst[x]= ((8-dx)*(8-dy)*src1[x] + dx*(8-dy)*src2[x]+
                         (8-dx)*   dy *src3[x] + dx*   dy *src4[x]+32)>>6;
            }
            src1+=stride1;
            src2+=stride2;
            src3+=stride3;
            src4+=stride4;
            dst +=stride;
        }
    }else{
        const uint8_t *src1= hpel[l];
        const uint8_t *src2= hpel[r];
        int stride1 = MC_STRIDE(l);
        int stride2 = MC_STRIDE(r);
        int a= weight[((dx&7) + (8*(dy&7)))];
        int b= 8-a;
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                dst[x]= (a*src1[x] + b*src2[x] + 4)>>3;
            }
            src1+=stride1;
            src2+=stride2;
            dst +=stride;
        }
    }
}

void ff_snow_pred_block(SnowContext *s, uint8_t *dst, uint8_t *tmp, ptrdiff_t stride, int sx, int sy, int b_w, int b_h, const BlockNode *block, int plane_index, int w, int h){
    if(block->type & BLOCK_INTRA){
        int x, y;
        const unsigned color  = block->color[plane_index];
        const unsigned color4 = color*0x01010101;
        if(b_w==32){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
                *(uint32_t*)&dst[8 + y*stride]= color4;
                *(uint32_t*)&dst[12+ y*stride]= color4;
                *(uint32_t*)&dst[16+ y*stride]= color4;
                *(uint32_t*)&dst[20+ y*stride]= color4;
                *(uint32_t*)&dst[24+ y*stride]= color4;
                *(uint32_t*)&dst[28+ y*stride]= color4;
            }
        }else if(b_w==16){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
                *(uint32_t*)&dst[8 + y*stride]= color4;
                *(uint32_t*)&dst[12+ y*stride]= color4;
            }
        }else if(b_w==8){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
            }
        }else if(b_w==4){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
            }
        }else{
            for(y=0; y < b_h; y++){
                for(x=0; x < b_w; x++){
                    dst[x + y*stride]= color;
                }
            }
        }
    }else{
        const uint8_t *src = s->last_picture[block->ref]->data[plane_index];
        const int scale= plane_index ?  (2*s->mv_scale)>>s->chroma_h_shift : 2*s->mv_scale;
        int mx= block->mx*scale;
        int my= block->my*scale;
        const int dx= mx&15;
        const int dy= my&15;
        const int tab_index= 3 - (b_w>>2) + (b_w>>4);
        sx += (mx>>4) - (HTAPS_MAX/2-1);
        sy += (my>>4) - (HTAPS_MAX/2-1);
        src += sx + sy*stride;
        if(   (unsigned)sx >= FFMAX(w - b_w - (HTAPS_MAX-2), 0)
           || (unsigned)sy >= FFMAX(h - b_h - (HTAPS_MAX-2), 0)){
            s->vdsp.emulated_edge_mc(tmp + MB_SIZE, src,
                                     stride, stride,
                                     b_w+HTAPS_MAX-1, b_h+HTAPS_MAX-1,
                                     sx, sy, w, h);
            src= tmp + MB_SIZE;
        }

        av_assert2(s->chroma_h_shift == s->chroma_v_shift); // only one mv_scale

        av_assert2((tab_index>=0 && tab_index<4) || b_w==32);
        if(    (dx&3) || (dy&3)
            || !(b_w == b_h || 2*b_w == b_h || b_w == 2*b_h)
            || (b_w&(b_w-1))
            || b_w == 1
            || b_h == 1
            || !s->plane[plane_index].fast_mc )
            mc_block(&s->plane[plane_index], dst, src, stride, b_w, b_h, dx, dy);
        else if(b_w==32){
            int y;
            for(y=0; y<b_h; y+=16){
                s->h264qpel.put_h264_qpel_pixels_tab[0][dy+(dx>>2)](dst + y*stride, src + 3 + (y+3)*stride,stride);
                s->h264qpel.put_h264_qpel_pixels_tab[0][dy+(dx>>2)](dst + 16 + y*stride, src + 19 + (y+3)*stride,stride);
            }
        }else if(b_w==b_h)
            s->h264qpel.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst,src + 3 + 3*stride,stride);
        else if(b_w==2*b_h){
            s->h264qpel.put_h264_qpel_pixels_tab[tab_index+1][dy+(dx>>2)](dst    ,src + 3       + 3*stride,stride);
            s->h264qpel.put_h264_qpel_pixels_tab[tab_index+1][dy+(dx>>2)](dst+b_h,src + 3 + b_h + 3*stride,stride);
        }else{
            av_assert2(2*b_w==b_h);
            s->h264qpel.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst           ,src + 3 + 3*stride           ,stride);
            s->h264qpel.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst+b_w*stride,src + 3 + 3*stride+b_w*stride,stride);
        }
    }
}

#define mca(dx,dy,b_w)\
static void mc_block_hpel ## dx ## dy ## b_w(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h){\
    av_assert2(h==b_w);\
    mc_block(NULL, dst, src-(HTAPS_MAX/2-1)-(HTAPS_MAX/2-1)*stride, stride, b_w, b_w, dx, dy);\
}

mca( 0, 0,16)
mca( 8, 0,16)
mca( 0, 8,16)
mca( 8, 8,16)
mca( 0, 0,8)
mca( 8, 0,8)
mca( 0, 8,8)
mca( 8, 8,8)

static av_cold void snow_static_init(void)
{
    for (int i = 0; i < MAX_REF_FRAMES; i++)
        for (int j = 0; j < MAX_REF_FRAMES; j++)
            ff_scale_mv_ref[i][j] = 256 * (i + 1) / (j + 1);
}

av_cold int ff_snow_common_init(AVCodecContext *avctx){
    static AVOnce init_static_once = AV_ONCE_INIT;
    SnowContext *s = avctx->priv_data;
    int width, height;
    int i;

    s->avctx= avctx;
    s->max_ref_frames=1; //just make sure it's not an invalid value in case of no initial keyframe
    s->spatial_decomposition_count = 1;

    ff_videodsp_init(&s->vdsp, 8);
    ff_dwt_init(&s->dwt);
    ff_h264qpel_init(&s->h264qpel, 8);

#define mcfh(dx,dy)\
    s->hdsp.put_pixels_tab       [0][dy/4+dx/8]=\
    s->hdsp.put_no_rnd_pixels_tab[0][dy/4+dx/8]=\
        mc_block_hpel ## dx ## dy ## 16;\
    s->hdsp.put_pixels_tab       [1][dy/4+dx/8]=\
    s->hdsp.put_no_rnd_pixels_tab[1][dy/4+dx/8]=\
        mc_block_hpel ## dx ## dy ## 8;

    mcfh(0, 0)
    mcfh(8, 0)
    mcfh(0, 8)
    mcfh(8, 8)

//    dec += FFMAX(s->chroma_h_shift, s->chroma_v_shift);

    width= s->avctx->width;
    height= s->avctx->height;

    if (!FF_ALLOCZ_TYPED_ARRAY(s->spatial_idwt_buffer, width * height) ||
        !FF_ALLOCZ_TYPED_ARRAY(s->spatial_dwt_buffer,  width * height) ||  //FIXME this does not belong here
        !FF_ALLOCZ_TYPED_ARRAY(s->temp_dwt_buffer,     width)          ||
        !FF_ALLOCZ_TYPED_ARRAY(s->temp_idwt_buffer,    width)          ||
        !FF_ALLOCZ_TYPED_ARRAY(s->run_buffer, ((width + 1) >> 1) * ((height + 1) >> 1) + 1))
        return AVERROR(ENOMEM);

    for(i=0; i<MAX_REF_FRAMES; i++) {
        s->last_picture[i] = av_frame_alloc();
        if (!s->last_picture[i])
            return AVERROR(ENOMEM);
    }

    s->mconly_picture = av_frame_alloc();
    s->current_picture = av_frame_alloc();
    if (!s->mconly_picture || !s->current_picture)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_static_once, snow_static_init);

    return 0;
}

int ff_snow_common_init_after_header(AVCodecContext *avctx) {
    SnowContext *s = avctx->priv_data;
    int plane_index, level, orientation;

    if(!s->scratchbuf) {
        int emu_buf_size;
        emu_buf_size = FFMAX(s->mconly_picture->linesize[0], 2*avctx->width+256) * (2 * MB_SIZE + HTAPS_MAX - 1);
        if (!FF_ALLOCZ_TYPED_ARRAY(s->scratchbuf,      FFMAX(s->mconly_picture->linesize[0], 2*avctx->width+256) * 7 * MB_SIZE) ||
            !FF_ALLOCZ_TYPED_ARRAY(s->emu_edge_buffer, emu_buf_size))
            return AVERROR(ENOMEM);
    }

    for(plane_index=0; plane_index < s->nb_planes; plane_index++){
        int w= s->avctx->width;
        int h= s->avctx->height;

        if(plane_index){
            w = AV_CEIL_RSHIFT(w, s->chroma_h_shift);
            h = AV_CEIL_RSHIFT(h, s->chroma_v_shift);
        }
        s->plane[plane_index].width = w;
        s->plane[plane_index].height= h;

        for(level=s->spatial_decomposition_count-1; level>=0; level--){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &s->plane[plane_index].band[level][orientation];

                b->buf= s->spatial_dwt_buffer;
                b->level= level;
                b->stride= s->plane[plane_index].width << (s->spatial_decomposition_count - level);
                b->width = (w + !(orientation&1))>>1;
                b->height= (h + !(orientation>1))>>1;

                b->stride_line = 1 << (s->spatial_decomposition_count - level);
                b->buf_x_offset = 0;
                b->buf_y_offset = 0;

                if(orientation&1){
                    b->buf += (w+1)>>1;
                    b->buf_x_offset = (w+1)>>1;
                }
                if(orientation>1){
                    b->buf += b->stride>>1;
                    b->buf_y_offset = b->stride_line >> 1;
                }
                b->ibuf= s->spatial_idwt_buffer + (b->buf - s->spatial_dwt_buffer);

                if(level)
                    b->parent= &s->plane[plane_index].band[level-1][orientation];
                //FIXME avoid this realloc
                av_freep(&b->x_coeff);
                b->x_coeff = av_calloc((b->width + 1) * b->height + 1,
                                       sizeof(*b->x_coeff));
                if (!b->x_coeff)
                    return AVERROR(ENOMEM);
            }
            w= (w+1)>>1;
            h= (h+1)>>1;
        }
    }

    return 0;
}

void ff_snow_release_buffer(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    if(s->last_picture[s->max_ref_frames-1]->data[0]){
        av_frame_unref(s->last_picture[s->max_ref_frames-1]);
    }
}

int ff_snow_frames_prepare(SnowContext *s)
{
   AVFrame *tmp;

    ff_snow_release_buffer(s->avctx);

    tmp= s->last_picture[s->max_ref_frames-1];
    for (int i = s->max_ref_frames - 1; i > 0; i--)
        s->last_picture[i] = s->last_picture[i-1];
    s->last_picture[0] = s->current_picture;
    s->current_picture = tmp;

    if(s->keyframe){
        s->ref_frames= 0;
        s->current_picture->flags |= AV_FRAME_FLAG_KEY;
    }else{
        int i;
        for(i=0; i<s->max_ref_frames && s->last_picture[i]->data[0]; i++)
            if(i && (s->last_picture[i-1]->flags & AV_FRAME_FLAG_KEY))
                break;
        s->ref_frames= i;
        if(s->ref_frames==0){
            av_log(s->avctx,AV_LOG_ERROR, "No reference frames\n");
            return AVERROR_INVALIDDATA;
        }
        s->current_picture->flags &= ~AV_FRAME_FLAG_KEY;
    }

    return 0;
}

av_cold void ff_snow_common_end(SnowContext *s)
{
    int plane_index, level, orientation, i;

    av_freep(&s->spatial_dwt_buffer);
    av_freep(&s->temp_dwt_buffer);
    av_freep(&s->spatial_idwt_buffer);
    av_freep(&s->temp_idwt_buffer);
    av_freep(&s->run_buffer);

    av_freep(&s->block);
    av_freep(&s->scratchbuf);
    av_freep(&s->emu_edge_buffer);

    for(i=0; i<MAX_REF_FRAMES; i++){
        if(s->last_picture[i] && s->last_picture[i]->data[0]) {
            av_assert0(s->last_picture[i]->data[0] != s->current_picture->data[0]);
        }
        av_frame_free(&s->last_picture[i]);
    }

    for(plane_index=0; plane_index < MAX_PLANES; plane_index++){
        for(level=MAX_DECOMPOSITIONS-1; level>=0; level--){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &s->plane[plane_index].band[level][orientation];

                av_freep(&b->x_coeff);
            }
        }
    }
    av_frame_free(&s->mconly_picture);
    av_frame_free(&s->current_picture);
}
