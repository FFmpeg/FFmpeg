/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2006 Robert Edele <yartrebo@earthlink.net>
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

#ifndef AVCODEC_SNOW_H
#define AVCODEC_SNOW_H

#include "libavutil/motion_vector.h"

#include "hpeldsp.h"
#include "me_cmp.h"
#include "qpeldsp.h"
#include "snow_dwt.h"

#include "rangecoder.h"
#include "mathops.h"

#include "mpegvideo.h"
#include "h264qpel.h"

#define SNOW_MAX_PLANES 4

#define FF_ME_ITER 3

#define MID_STATE 128

#define MAX_PLANES 4
#define QSHIFT 5
#define QROOT (1<<QSHIFT)
#define LOSSLESS_QLOG -128
#define FRAC_BITS 4
#define MAX_REF_FRAMES 8

#define LOG2_OBMC_MAX 8
#define OBMC_MAX (1<<(LOG2_OBMC_MAX))
typedef struct BlockNode{
    int16_t mx;                 ///< Motion vector component X, see mv_scale
    int16_t my;                 ///< Motion vector component Y, see mv_scale
    uint8_t ref;                ///< Reference frame index
    uint8_t color[3];           ///< Color for intra
    uint8_t type;               ///< Bitfield of BLOCK_*
//#define TYPE_SPLIT    1
#define BLOCK_INTRA   1         ///< Intra block, inter otherwise
#define BLOCK_OPT     2         ///< Block needs no checks in this round of iterative motion estiation
//#define TYPE_NOCOLOR  4
    uint8_t level; //FIXME merge into type?
}BlockNode;

static const BlockNode null_block= { //FIXME add border maybe
    .color= {128,128,128},
    .mx= 0,
    .my= 0,
    .ref= 0,
    .type= 0,
    .level= 0,
};

#define LOG2_MB_SIZE 4
#define MB_SIZE (1<<LOG2_MB_SIZE)
#define ENCODER_EXTRA_BITS 4
#define HTAPS_MAX 8

typedef struct x_and_coeff{
    int16_t x;
    uint16_t coeff;
} x_and_coeff;

typedef struct SubBand{
    int level;
    int stride;
    int width;
    int height;
    int qlog;        ///< log(qscale)/log[2^(1/6)]
    DWTELEM *buf;
    IDWTELEM *ibuf;
    int buf_x_offset;
    int buf_y_offset;
    int stride_line; ///< Stride measured in lines, not pixels.
    x_and_coeff * x_coeff;
    struct SubBand *parent;
    uint8_t state[/*7*2*/ 7 + 512][32];
}SubBand;

typedef struct Plane{
    int width;
    int height;
    SubBand band[MAX_DECOMPOSITIONS][4];

    int htaps;
    int8_t hcoeff[HTAPS_MAX/2];
    int diag_mc;
    int fast_mc;

    int last_htaps;
    int8_t last_hcoeff[HTAPS_MAX/2];
    int last_diag_mc;
}Plane;

typedef struct SnowContext{
    AVClass *class;
    AVCodecContext *avctx;
    RangeCoder c;
    MECmpContext mecc;
    HpelDSPContext hdsp;
    QpelDSPContext qdsp;
    VideoDSPContext vdsp;
    H264QpelContext h264qpel;
    MpegvideoEncDSPContext mpvencdsp;
    SnowDWTContext dwt;
    AVFrame *input_picture;              ///< new_picture with the internal linesizes
    AVFrame *current_picture;
    AVFrame *last_picture[MAX_REF_FRAMES];
    uint8_t *halfpel_plane[MAX_REF_FRAMES][4][4];
    AVFrame *mconly_picture;
//     uint8_t q_context[16];
    uint8_t header_state[32];
    uint8_t block_state[128 + 32*128];
    int keyframe;
    int always_reset;
    int version;
    int spatial_decomposition_type;
    int last_spatial_decomposition_type;
    int temporal_decomposition_type;
    int spatial_decomposition_count;
    int last_spatial_decomposition_count;
    int temporal_decomposition_count;
    int max_ref_frames;
    int ref_frames;
    int16_t (*ref_mvs[MAX_REF_FRAMES])[2];
    uint32_t *ref_scores[MAX_REF_FRAMES];
    DWTELEM *spatial_dwt_buffer;
    DWTELEM *temp_dwt_buffer;
    IDWTELEM *spatial_idwt_buffer;
    IDWTELEM *temp_idwt_buffer;
    int *run_buffer;
    int colorspace_type;
    int chroma_h_shift;
    int chroma_v_shift;
    int spatial_scalability;
    int qlog;
    int last_qlog;
    int lambda;
    int lambda2;
    int pass1_rc;
    int mv_scale;
    int last_mv_scale;
    int qbias;
    int last_qbias;
#define QBIAS_SHIFT 3
    int b_width;
    int b_height;
    int block_max_depth;
    int last_block_max_depth;
    int nb_planes;
    Plane plane[MAX_PLANES];
    BlockNode *block;
#define ME_CACHE_SIZE 1024
    unsigned me_cache[ME_CACHE_SIZE];
    unsigned me_cache_generation;
    slice_buffer sb;
    int memc_only;
    int no_bitstream;
    int intra_penalty;
    int motion_est;
    int iterative_dia_size;
    int scenechange_threshold;

    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to eventually make the motion estimation independent of MpegEncContext, so this will be removed then (FIXME/XXX)

    uint8_t *scratchbuf;
    uint8_t *emu_edge_buffer;

    AVMotionVector *avmv;
    unsigned avmv_size;
    int avmv_index;
    uint64_t encoding_error[SNOW_MAX_PLANES];

    int pred;
}SnowContext;

/* Tables */
extern const uint8_t * const ff_obmc_tab[4];
extern const uint8_t ff_qexp[QROOT];
extern int ff_scale_mv_ref[MAX_REF_FRAMES][MAX_REF_FRAMES];

/* C bits used by mmx/sse2/altivec */

static av_always_inline void snow_interleave_line_header(int * i, int width, IDWTELEM * low, IDWTELEM * high){
    (*i) = (width) - 2;

    if (width & 1){
        low[(*i)+1] = low[((*i)+1)>>1];
        (*i)--;
    }
}

static av_always_inline void snow_interleave_line_footer(int * i, IDWTELEM * low, IDWTELEM * high){
    for (; (*i)>=0; (*i)-=2){
        low[(*i)+1] = high[(*i)>>1];
        low[*i] = low[(*i)>>1];
    }
}

static av_always_inline void snow_horizontal_compose_lift_lead_out(int i, IDWTELEM * dst, IDWTELEM * src, IDWTELEM * ref, int width, int w, int lift_high, int mul, int add, int shift){
    for(; i<w; i++){
        dst[i] = src[i] - ((mul * (ref[i] + ref[i + 1]) + add) >> shift);
    }

    if((width^lift_high)&1){
        dst[w] = src[w] - ((mul * 2 * ref[w] + add) >> shift);
    }
}

static av_always_inline void snow_horizontal_compose_liftS_lead_out(int i, IDWTELEM * dst, IDWTELEM * src, IDWTELEM * ref, int width, int w){
        for(; i<w; i++){
            dst[i] = src[i] + ((ref[i] + ref[(i+1)]+W_BO + 4 * src[i]) >> W_BS);
        }

        if(width&1){
            dst[w] = src[w] + ((2 * ref[w] + W_BO + 4 * src[w]) >> W_BS);
        }
}

/* common code */

int ff_snow_common_init(AVCodecContext *avctx);
int ff_snow_common_init_after_header(AVCodecContext *avctx);
void ff_snow_common_end(SnowContext *s);
void ff_snow_release_buffer(AVCodecContext *avctx);
void ff_snow_reset_contexts(SnowContext *s);
int ff_snow_alloc_blocks(SnowContext *s);
int ff_snow_frame_start(SnowContext *s);
void ff_snow_pred_block(SnowContext *s, uint8_t *dst, uint8_t *tmp, ptrdiff_t stride,
                     int sx, int sy, int b_w, int b_h, const BlockNode *block,
                     int plane_index, int w, int h);
int ff_snow_get_buffer(SnowContext *s, AVFrame *frame);
/* common inline functions */
//XXX doublecheck all of them should stay inlined

static inline void pred_mv(SnowContext *s, int *mx, int *my, int ref,
                           const BlockNode *left, const BlockNode *top, const BlockNode *tr){
    if(s->ref_frames == 1){
        *mx = mid_pred(left->mx, top->mx, tr->mx);
        *my = mid_pred(left->my, top->my, tr->my);
    }else{
        const int *scale = ff_scale_mv_ref[ref];
        *mx = mid_pred((left->mx * scale[left->ref] + 128) >>8,
                       (top ->mx * scale[top ->ref] + 128) >>8,
                       (tr  ->mx * scale[tr  ->ref] + 128) >>8);
        *my = mid_pred((left->my * scale[left->ref] + 128) >>8,
                       (top ->my * scale[top ->ref] + 128) >>8,
                       (tr  ->my * scale[tr  ->ref] + 128) >>8);
    }
}

static av_always_inline int same_block(BlockNode *a, BlockNode *b){
    if((a->type&BLOCK_INTRA) && (b->type&BLOCK_INTRA)){
        return !((a->color[0] - b->color[0]) | (a->color[1] - b->color[1]) | (a->color[2] - b->color[2]));
    }else{
        return !((a->mx - b->mx) | (a->my - b->my) | (a->ref - b->ref) | ((a->type ^ b->type)&BLOCK_INTRA));
    }
}

//FIXME name cleanup (b_w, block_w, b_width stuff)
//XXX should we really inline it?
static av_always_inline void add_yblock(SnowContext *s, int sliced, slice_buffer *sb, IDWTELEM *dst, uint8_t *dst8, const uint8_t *obmc, int src_x, int src_y, int b_w, int b_h, int w, int h, int dst_stride, int src_stride, int obmc_stride, int b_x, int b_y, int add, int offset_dst, int plane_index){
    const int b_width = s->b_width  << s->block_max_depth;
    const int b_height= s->b_height << s->block_max_depth;
    const int b_stride= b_width;
    BlockNode *lt= &s->block[b_x + b_y*b_stride];
    BlockNode *rt= lt+1;
    BlockNode *lb= lt+b_stride;
    BlockNode *rb= lb+1;
    uint8_t *block[4];
    // When src_stride is large enough, it is possible to interleave the blocks.
    // Otherwise the blocks are written sequentially in the tmp buffer.
    int tmp_step= src_stride >= 7*MB_SIZE ? MB_SIZE : MB_SIZE*src_stride;
    uint8_t *tmp = s->scratchbuf;
    uint8_t *ptmp;
    int x,y;

    if(b_x<0){
        lt= rt;
        lb= rb;
    }else if(b_x + 1 >= b_width){
        rt= lt;
        rb= lb;
    }
    if(b_y<0){
        lt= lb;
        rt= rb;
    }else if(b_y + 1 >= b_height){
        lb= lt;
        rb= rt;
    }

    if(src_x<0){ //FIXME merge with prev & always round internal width up to *16
        obmc -= src_x;
        b_w += src_x;
        if(!sliced && !offset_dst)
            dst -= src_x;
        src_x=0;
    }
    if(src_x + b_w > w){
        b_w = w - src_x;
    }
    if(src_y<0){
        obmc -= src_y*obmc_stride;
        b_h += src_y;
        if(!sliced && !offset_dst)
            dst -= src_y*dst_stride;
        src_y=0;
    }
    if(src_y + b_h> h){
        b_h = h - src_y;
    }

    if(b_w<=0 || b_h<=0) return;

    if(!sliced && offset_dst)
        dst += src_x + src_y*dst_stride;
    dst8+= src_x + src_y*src_stride;
//    src += src_x + src_y*src_stride;

    ptmp= tmp + 3*tmp_step;
    block[0]= ptmp;
    ptmp+=tmp_step;
    ff_snow_pred_block(s, block[0], tmp, src_stride, src_x, src_y, b_w, b_h, lt, plane_index, w, h);

    if(same_block(lt, rt)){
        block[1]= block[0];
    }else{
        block[1]= ptmp;
        ptmp+=tmp_step;
        ff_snow_pred_block(s, block[1], tmp, src_stride, src_x, src_y, b_w, b_h, rt, plane_index, w, h);
    }

    if(same_block(lt, lb)){
        block[2]= block[0];
    }else if(same_block(rt, lb)){
        block[2]= block[1];
    }else{
        block[2]= ptmp;
        ptmp+=tmp_step;
        ff_snow_pred_block(s, block[2], tmp, src_stride, src_x, src_y, b_w, b_h, lb, plane_index, w, h);
    }

    if(same_block(lt, rb) ){
        block[3]= block[0];
    }else if(same_block(rt, rb)){
        block[3]= block[1];
    }else if(same_block(lb, rb)){
        block[3]= block[2];
    }else{
        block[3]= ptmp;
        ff_snow_pred_block(s, block[3], tmp, src_stride, src_x, src_y, b_w, b_h, rb, plane_index, w, h);
    }
    if(sliced){
        s->dwt.inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    }else{
        for(y=0; y<b_h; y++){
            //FIXME ugly misuse of obmc_stride
            const uint8_t *obmc1= obmc + y*obmc_stride;
            const uint8_t *obmc2= obmc1+ (obmc_stride>>1);
            const uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
            const uint8_t *obmc4= obmc3+ (obmc_stride>>1);
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
                    v += dst[x + y*dst_stride];
                    v = (v + (1<<(FRAC_BITS-1))) >> FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*src_stride] = v;
                }else{
                    dst[x + y*dst_stride] -= v;
                }
            }
        }
    }
}

static av_always_inline void predict_slice(SnowContext *s, IDWTELEM *buf, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size>>s->chroma_h_shift : block_size;
    int block_h    = plane_index ? block_size>>s->chroma_v_shift : block_size;
    const uint8_t *obmc  = plane_index ? ff_obmc_tab[s->block_max_depth+s->chroma_h_shift] : ff_obmc_tab[s->block_max_depth];
    const int obmc_stride= plane_index ? (2*block_size)>>s->chroma_h_shift : 2*block_size;
    int ref_stride= s->current_picture->linesize[plane_index];
    uint8_t *dst8= s->current_picture->data[plane_index];
    int w= p->width;
    int h= p->height;
    av_assert2(s->chroma_h_shift == s->chroma_v_shift); // obmc params assume squares
    if(s->keyframe || (s->avctx->debug&512)){
        if(mb_y==mb_h)
            return;

        if(add){
            for(y=block_h*mb_y; y<FFMIN(h,block_h*(mb_y+1)); y++){
                for(x=0; x<w; x++){
                    int v= buf[x + y*w] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    v >>= FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*ref_stride]= v;
                }
            }
        }else{
            for(y=block_h*mb_y; y<FFMIN(h,block_h*(mb_y+1)); y++){
                for(x=0; x<w; x++){
                    buf[x + y*w]-= 128<<FRAC_BITS;
                }
            }
        }

        return;
    }

    for(mb_x=0; mb_x<=mb_w; mb_x++){
        add_yblock(s, 0, NULL, buf, dst8, obmc,
                   block_w*mb_x - block_w/2,
                   block_h*mb_y - block_h/2,
                   block_w, block_h,
                   w, h,
                   w, ref_stride, obmc_stride,
                   mb_x - 1, mb_y - 1,
                   add, 1, plane_index);
    }
}

static av_always_inline void predict_plane(SnowContext *s, IDWTELEM *buf, int plane_index, int add){
    const int mb_h= s->b_height << s->block_max_depth;
    int mb_y;
    for(mb_y=0; mb_y<=mb_h; mb_y++)
        predict_slice(s, buf, plane_index, add, mb_y);
}

static inline void set_blocks(SnowContext *s, int level, int x, int y, int l, int cb, int cr, int mx, int my, int ref, int type){
    const int w= s->b_width << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    const int block_w= 1<<rem_depth;
    const int block_h= 1<<rem_depth; //FIXME "w!=h"
    BlockNode block;
    int i,j;

    block.color[0]= l;
    block.color[1]= cb;
    block.color[2]= cr;
    block.mx= mx;
    block.my= my;
    block.ref= ref;
    block.type= type;
    block.level= level;

    for(j=0; j<block_h; j++){
        for(i=0; i<block_w; i++){
            s->block[index + i + j*w]= block;
        }
    }
}

static inline void init_ref(MotionEstContext *c, uint8_t *src[3], uint8_t *ref[3], uint8_t *ref2[3], int x, int y, int ref_index){
    SnowContext *s = c->avctx->priv_data;
    const int offset[3]= {
          y*c->  stride + x,
        ((y*c->uvstride + x)>>s->chroma_h_shift),
        ((y*c->uvstride + x)>>s->chroma_h_shift),
    };
    int i;
    for(i=0; i<3; i++){
        c->src[0][i]= src [i];
        c->ref[0][i]= ref [i] + offset[i];
    }
    av_assert2(!ref_index);
}


/* bitstream functions */

extern const int8_t ff_quant3bA[256];

#define QEXPSHIFT (7-FRAC_BITS+8) //FIXME try to change this to 0

static inline void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed){
    int i;

    if(v){
        const int a= FFABS(v);
        const int e= av_log2(a);
        const int el= FFMIN(e, 10);
        put_rac(c, state+0, 0);

        for(i=0; i<el; i++){
            put_rac(c, state+1+i, 1);  //1..10
        }
        for(; i<e; i++){
            put_rac(c, state+1+9, 1);  //1..10
        }
        put_rac(c, state+1+FFMIN(i,9), 0);

        for(i=e-1; i>=el; i--){
            put_rac(c, state+22+9, (a>>i)&1); //22..31
        }
        for(; i>=0; i--){
            put_rac(c, state+22+i, (a>>i)&1); //22..31
        }

        if(is_signed)
            put_rac(c, state+11 + el, v < 0); //11..21
    }else{
        put_rac(c, state+0, 1);
    }
}

static inline int get_symbol(RangeCoder *c, uint8_t *state, int is_signed){
    if(get_rac(c, state+0))
        return 0;
    else{
        int i, e;
        unsigned a;
        e= 0;
        while(get_rac(c, state+1 + FFMIN(e,9))){ //1..10
            e++;
            if (e > 31)
                return AVERROR_INVALIDDATA;
        }

        a= 1;
        for(i=e-1; i>=0; i--){
            a += a + get_rac(c, state+22 + FFMIN(i,9)); //22..31
        }

        e= -(is_signed && get_rac(c, state+11 + FFMIN(e,10))); //11..21
        return (a^e)-e;
    }
}

static inline void put_symbol2(RangeCoder *c, uint8_t *state, int v, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;

    av_assert2(v>=0);
    av_assert2(log2>=-4);

    while(v >= r){
        put_rac(c, state+4+log2, 1);
        v -= r;
        log2++;
        if(log2>0) r+=r;
    }
    put_rac(c, state+4+log2, 0);

    for(i=log2-1; i>=0; i--){
        put_rac(c, state+31-i, (v>>i)&1);
    }
}

static inline int get_symbol2(RangeCoder *c, uint8_t *state, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;
    int v=0;

    av_assert2(log2>=-4);

    while(log2<28 && get_rac(c, state+4+log2)){
        v+= r;
        log2++;
        if(log2>0) r+=r;
    }

    for(i=log2-1; i>=0; i--){
        v+= get_rac(c, state+31-i)<<i;
    }

    return v;
}

static inline void unpack_coeffs(SnowContext *s, SubBand *b, SubBand * parent, int orientation){
    const int w= b->width;
    const int h= b->height;
    int x,y;

    int run, runs;
    x_and_coeff *xc= b->x_coeff;
    x_and_coeff *prev_xc= NULL;
    x_and_coeff *prev2_xc= xc;
    x_and_coeff *parent_xc= parent ? parent->x_coeff : NULL;
    x_and_coeff *prev_parent_xc= parent_xc;

    runs= get_symbol2(&s->c, b->state[30], 0);
    if(runs-- > 0) run= get_symbol2(&s->c, b->state[1], 3);
    else           run= INT_MAX;

    for(y=0; y<h; y++){
        int v=0;
        int lt=0, t=0, rt=0;

        if(y && prev_xc->x == 0){
            rt= prev_xc->coeff;
        }
        for(x=0; x<w; x++){
            int p=0;
            const int l= v;

            lt= t; t= rt;

            if(y){
                if(prev_xc->x <= x)
                    prev_xc++;
                if(prev_xc->x == x + 1)
                    rt= prev_xc->coeff;
                else
                    rt=0;
            }
            if(parent_xc){
                if(x>>1 > parent_xc->x){
                    parent_xc++;
                }
                if(x>>1 == parent_xc->x){
                    p= parent_xc->coeff;
                }
            }
            if(/*ll|*/l|lt|t|rt|p){
                int context= av_log2(/*FFABS(ll) + */3*(l>>1) + (lt>>1) + (t&~1) + (rt>>1) + (p>>1));

                v=get_rac(&s->c, &b->state[0][context]);
                if(v){
                    v= 2*(get_symbol2(&s->c, b->state[context + 2], context-4) + 1);
                    v+=get_rac(&s->c, &b->state[0][16 + 1 + 3 + ff_quant3bA[l&0xFF] + 3*ff_quant3bA[t&0xFF]]);
                    if ((uint16_t)v != v) {
                        av_log(s->avctx, AV_LOG_ERROR, "Coefficient damaged\n");
                        v = 1;
                    }
                    xc->x=x;
                    (xc++)->coeff= v;
                }
            }else{
                if(!run){
                    if(runs-- > 0) run= get_symbol2(&s->c, b->state[1], 3);
                    else           run= INT_MAX;
                    v= 2*(get_symbol2(&s->c, b->state[0 + 2], 0-4) + 1);
                    v+=get_rac(&s->c, &b->state[0][16 + 1 + 3]);
                    if ((uint16_t)v != v) {
                        av_log(s->avctx, AV_LOG_ERROR, "Coefficient damaged\n");
                        v = 1;
                    }

                    xc->x=x;
                    (xc++)->coeff= v;
                }else{
                    int max_run;
                    run--;
                    v=0;
                    av_assert2(run >= 0);
                    if(y) max_run= FFMIN(run, prev_xc->x - x - 2);
                    else  max_run= FFMIN(run, w-x-1);
                    if(parent_xc)
                        max_run= FFMIN(max_run, 2*parent_xc->x - x - 1);
                    av_assert2(max_run >= 0 && max_run <= run);

                    x+= max_run;
                    run-= max_run;
                }
            }
        }
        (xc++)->x= w+1; //end marker
        prev_xc= prev2_xc;
        prev2_xc= xc;

        if(parent_xc){
            if(y&1){
                while(parent_xc->x != parent->width+1)
                    parent_xc++;
                parent_xc++;
                prev_parent_xc= parent_xc;
            }else{
                parent_xc= prev_parent_xc;
            }
        }
    }

    (xc++)->x= w+1; //end marker
}

#endif /* AVCODEC_SNOW_H */
