/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * 4MV & hq & b-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
 */
 
/**
 * @file mpegvideo.c
 * The simplest mpeg encoder (well, it was the simplest!).
 */ 
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "faandct.h"
#include <limits.h>

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

//#undef NDEBUG
//#include <assert.h>

#ifdef CONFIG_ENCODERS
static void encode_picture(MpegEncContext *s, int picture_number);
#endif //CONFIG_ENCODERS
static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg1_inter_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg2_intra_c(MpegEncContext *s,
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg2_inter_c(MpegEncContext *s,
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_intra_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_inter_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void dct_unquantize_h261_intra_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void dct_unquantize_h261_inter_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void draw_edges_c(uint8_t *buf, int wrap, int width, int height, int w);
#ifdef CONFIG_ENCODERS
static int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
static int dct_quantize_trellis_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
static int dct_quantize_refine(MpegEncContext *s, DCTELEM *block, int16_t *weight, DCTELEM *orig, int n, int qscale);
static int sse_mb(MpegEncContext *s);
static void  denoise_dct_c(MpegEncContext *s, DCTELEM *block);
#endif //CONFIG_ENCODERS

#ifdef HAVE_XVMC
extern int  XVMC_field_start(MpegEncContext*s, AVCodecContext *avctx);
extern void XVMC_field_end(MpegEncContext *s);
extern void XVMC_decode_mb(MpegEncContext *s);
#endif

void (*draw_edges)(uint8_t *buf, int wrap, int width, int height, int w)= draw_edges_c;


/* enable all paranoid tests for rounding, overflows, etc... */
//#define PARANOID

//#define DEBUG


/* for jpeg fast DCT */
#define CONST_BITS 14

static const uint16_t aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867 , 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520 ,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

static const uint8_t h263_chroma_roundtab[16] = {
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
};

static const uint8_t ff_default_chroma_qscale_table[32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
};

#ifdef CONFIG_ENCODERS
static uint8_t (*default_mv_penalty)[MAX_MV*2+1]=NULL;
static uint8_t default_fcode_tab[MAX_MV*2+1];

enum PixelFormat ff_yuv420p_list[2]= {PIX_FMT_YUV420P, -1};

static void convert_matrix(DSPContext *dsp, int (*qmat)[64], uint16_t (*qmat16)[2][64],
                           const uint16_t *quant_matrix, int bias, int qmin, int qmax)
{
    int qscale;

    for(qscale=qmin; qscale<=qmax; qscale++){
        int i;
        if (dsp->fdct == ff_jpeg_fdct_islow 
#ifdef FAAN_POSTSCALE
            || dsp->fdct == ff_faandct
#endif
            ) {
            for(i=0;i<64;i++) {
                const int j= dsp->idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((uint64_t_C(1) << QMAT_SHIFT) / 
                                (qscale * quant_matrix[j]));
            }
        } else if (dsp->fdct == fdct_ifast
#ifndef FAAN_POSTSCALE
                   || dsp->fdct == ff_faandct
#endif
                   ) {
            for(i=0;i<64;i++) {
                const int j= dsp->idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((uint64_t_C(1) << (QMAT_SHIFT + 14)) / 
                                (aanscales[i] * qscale * quant_matrix[j]));
            }
        } else {
            for(i=0;i<64;i++) {
                const int j= dsp->idct_permutation[i];
                /* We can safely suppose that 16 <= quant_matrix[i] <= 255
                   So 16           <= qscale * quant_matrix[i]             <= 7905
                   so (1<<19) / 16 >= (1<<19) / (qscale * quant_matrix[i]) >= (1<<19) / 7905
                   so 32768        >= (1<<19) / (qscale * quant_matrix[i]) >= 67
                */
                qmat[qscale][i] = (int)((uint64_t_C(1) << QMAT_SHIFT) / (qscale * quant_matrix[j]));
//                qmat  [qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
                qmat16[qscale][0][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[j]);

                if(qmat16[qscale][0][i]==0 || qmat16[qscale][0][i]==128*256) qmat16[qscale][0][i]=128*256-1;
                qmat16[qscale][1][i]= ROUNDED_DIV(bias<<(16-QUANT_BIAS_SHIFT), qmat16[qscale][0][i]);
            }
        }
    }
}

static inline void update_qscale(MpegEncContext *s){
    s->qscale= (s->lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
    s->qscale= clip(s->qscale, s->avctx->qmin, s->avctx->qmax);
    
    s->lambda2= (s->lambda*s->lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;
}
#endif //CONFIG_ENCODERS

void ff_init_scantable(uint8_t *permutation, ScanTable *st, const uint8_t *src_scantable){
    int i;
    int end;
    
    st->scantable= src_scantable;

    for(i=0; i<64; i++){
        int j;
        j = src_scantable[i];
        st->permutated[i] = permutation[j];
#ifdef ARCH_POWERPC
        st->inverse[j] = i;
#endif
    }
    
    end=-1;
    for(i=0; i<64; i++){
        int j;
        j = st->permutated[i];
        if(j>end) end=j;
        st->raster_end[i]= end;
    }
}

#ifdef CONFIG_ENCODERS
void ff_write_quant_matrix(PutBitContext *pb, int16_t *matrix){
    int i;

    if(matrix){
        put_bits(pb, 1, 1);
        for(i=0;i<64;i++) {
            put_bits(pb, 8, matrix[ ff_zigzag_direct[i] ]);
        }
    }else
        put_bits(pb, 1, 0);
}
#endif //CONFIG_ENCODERS

/* init common dct for both encoder and decoder */
int DCT_common_init(MpegEncContext *s)
{
    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_c;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_c;
    s->dct_unquantize_h261_intra = dct_unquantize_h261_intra_c;
    s->dct_unquantize_h261_inter = dct_unquantize_h261_inter_c;
    s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_c;
    s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_c;
    s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_c;
    s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_c;

#ifdef CONFIG_ENCODERS
    s->dct_quantize= dct_quantize_c;
    s->denoise_dct= denoise_dct_c;
#endif
        
#ifdef HAVE_MMX
    MPV_common_init_mmx(s);
#endif
#ifdef ARCH_ALPHA
    MPV_common_init_axp(s);
#endif
#ifdef HAVE_MLIB
    MPV_common_init_mlib(s);
#endif
#ifdef HAVE_MMI
    MPV_common_init_mmi(s);
#endif
#ifdef ARCH_ARMV4L
    MPV_common_init_armv4l(s);
#endif
#ifdef ARCH_POWERPC
    MPV_common_init_ppc(s);
#endif

#ifdef CONFIG_ENCODERS
    s->fast_dct_quantize= s->dct_quantize;

    if(s->flags&CODEC_FLAG_TRELLIS_QUANT){
        s->dct_quantize= dct_quantize_trellis_c; //move before MPV_common_init_*
    }

#endif //CONFIG_ENCODERS

    /* load & permutate scantables
       note: only wmv uses differnt ones 
    */
    if(s->alternate_scan){
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_alternate_vertical_scan);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_alternate_vertical_scan);
    }else{
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_zigzag_direct);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_zigzag_direct);
    }
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_alternate_horizontal_scan);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);

    return 0;
}

static void copy_picture(Picture *dst, Picture *src){
    *dst = *src;
    dst->type= FF_BUFFER_TYPE_COPY;
}

static void copy_picture_attributes(MpegEncContext *s, AVFrame *dst, AVFrame *src){
    int i;

    dst->pict_type              = src->pict_type;
    dst->quality                = src->quality;
    dst->coded_picture_number   = src->coded_picture_number;
    dst->display_picture_number = src->display_picture_number;
//    dst->reference              = src->reference;
    dst->pts                    = src->pts;
    dst->interlaced_frame       = src->interlaced_frame;
    dst->top_field_first        = src->top_field_first;

    if(s->avctx->me_threshold){
        if(!src->motion_val[0])
            av_log(s->avctx, AV_LOG_ERROR, "AVFrame.motion_val not set!\n");
        if(!src->mb_type)
            av_log(s->avctx, AV_LOG_ERROR, "AVFrame.mb_type not set!\n");
        if(!src->ref_index[0])
            av_log(s->avctx, AV_LOG_ERROR, "AVFrame.ref_index not set!\n");
        if(src->motion_subsample_log2 != dst->motion_subsample_log2)
            av_log(s->avctx, AV_LOG_ERROR, "AVFrame.motion_subsample_log2 doesnt match! (%d!=%d)\n",
            src->motion_subsample_log2, dst->motion_subsample_log2);

        memcpy(dst->mb_type, src->mb_type, s->mb_stride * s->mb_height * sizeof(dst->mb_type[0]));
        
        for(i=0; i<2; i++){
            int stride= ((16*s->mb_width )>>src->motion_subsample_log2) + 1;
            int height= ((16*s->mb_height)>>src->motion_subsample_log2);

            if(src->motion_val[i] && src->motion_val[i] != dst->motion_val[i]){
                memcpy(dst->motion_val[i], src->motion_val[i], 2*stride*height*sizeof(int16_t));
            }
            if(src->ref_index[i] && src->ref_index[i] != dst->ref_index[i]){
                memcpy(dst->ref_index[i], src->ref_index[i], s->b8_stride*2*s->mb_height*sizeof(int8_t));
            }
        }
    }
}

/**
 * allocates a Picture
 * The pixels are allocated/set by calling get_buffer() if shared=0
 */
static int alloc_picture(MpegEncContext *s, Picture *pic, int shared){
    const int big_mb_num= s->mb_stride*(s->mb_height+1) + 1; //the +1 is needed so memset(,,stride*height) doesnt sig11
    const int mb_array_size= s->mb_stride*s->mb_height;
    const int b8_array_size= s->b8_stride*s->mb_height*2;
    const int b4_array_size= s->b4_stride*s->mb_height*4;
    int i;
    
    if(shared){
        assert(pic->data[0]);
        assert(pic->type == 0 || pic->type == FF_BUFFER_TYPE_SHARED);
        pic->type= FF_BUFFER_TYPE_SHARED;
    }else{
        int r;
        
        assert(!pic->data[0]);
        
        r= s->avctx->get_buffer(s->avctx, (AVFrame*)pic);
        
        if(r<0 || !pic->age || !pic->type || !pic->data[0]){
	    av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed (%d %d %d %p)\n", r, pic->age, pic->type, pic->data[0]);
            return -1;
        }

        if(s->linesize && (s->linesize != pic->linesize[0] || s->uvlinesize != pic->linesize[1])){
            av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed (stride changed)\n");
            return -1;
        }

        if(pic->linesize[1] != pic->linesize[2]){
            av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed (uv stride missmatch)\n");
            return -1;
        }

        s->linesize  = pic->linesize[0];
        s->uvlinesize= pic->linesize[1];
    }
    
    if(pic->qscale_table==NULL){
        if (s->encoding) {        
            CHECKED_ALLOCZ(pic->mb_var   , mb_array_size * sizeof(int16_t))
            CHECKED_ALLOCZ(pic->mc_mb_var, mb_array_size * sizeof(int16_t))
            CHECKED_ALLOCZ(pic->mb_mean  , mb_array_size * sizeof(int8_t))
        }

        CHECKED_ALLOCZ(pic->mbskip_table , mb_array_size * sizeof(uint8_t)+2) //the +2 is for the slice end check
        CHECKED_ALLOCZ(pic->qscale_table , mb_array_size * sizeof(uint8_t))
        CHECKED_ALLOCZ(pic->mb_type_base , big_mb_num    * sizeof(uint32_t))
        pic->mb_type= pic->mb_type_base + s->mb_stride+1;
        if(s->out_format == FMT_H264){
            for(i=0; i<2; i++){
                CHECKED_ALLOCZ(pic->motion_val_base[i], 2 * (b4_array_size+2)  * sizeof(int16_t))
                pic->motion_val[i]= pic->motion_val_base[i]+2;
                CHECKED_ALLOCZ(pic->ref_index[i], b8_array_size * sizeof(uint8_t))
            }
            pic->motion_subsample_log2= 2;
        }else if(s->out_format == FMT_H263 || s->encoding || (s->avctx->debug&FF_DEBUG_MV) || (s->avctx->debug_mv)){
            for(i=0; i<2; i++){
                CHECKED_ALLOCZ(pic->motion_val_base[i], 2 * (b8_array_size+2) * sizeof(int16_t))
                pic->motion_val[i]= pic->motion_val_base[i]+2;
                CHECKED_ALLOCZ(pic->ref_index[i], b8_array_size * sizeof(uint8_t))
            }
            pic->motion_subsample_log2= 3;
        }
        if(s->avctx->debug&FF_DEBUG_DCT_COEFF) {
            CHECKED_ALLOCZ(pic->dct_coeff, 64 * mb_array_size * sizeof(DCTELEM)*6)
        }
        pic->qstride= s->mb_stride;
        CHECKED_ALLOCZ(pic->pan_scan , 1 * sizeof(AVPanScan))
    }

    //it might be nicer if the application would keep track of these but it would require a API change
    memmove(s->prev_pict_types+1, s->prev_pict_types, PREV_PICT_TYPES_BUFFER_SIZE-1);
    s->prev_pict_types[0]= s->pict_type;
    if(pic->age < PREV_PICT_TYPES_BUFFER_SIZE && s->prev_pict_types[pic->age] == B_TYPE)
        pic->age= INT_MAX; // skiped MBs in b frames are quite rare in mpeg1/2 and its a bit tricky to skip them anyway
    
    return 0;
fail: //for the CHECKED_ALLOCZ macro
    return -1;
}

/**
 * deallocates a picture
 */
static void free_picture(MpegEncContext *s, Picture *pic){
    int i;

    if(pic->data[0] && pic->type!=FF_BUFFER_TYPE_SHARED){
        s->avctx->release_buffer(s->avctx, (AVFrame*)pic);
    }

    av_freep(&pic->mb_var);
    av_freep(&pic->mc_mb_var);
    av_freep(&pic->mb_mean);
    av_freep(&pic->mbskip_table);
    av_freep(&pic->qscale_table);
    av_freep(&pic->mb_type_base);
    av_freep(&pic->dct_coeff);
    av_freep(&pic->pan_scan);
    pic->mb_type= NULL;
    for(i=0; i<2; i++){
        av_freep(&pic->motion_val_base[i]);
        av_freep(&pic->ref_index[i]);
    }
    
    if(pic->type == FF_BUFFER_TYPE_SHARED){
        for(i=0; i<4; i++){
            pic->base[i]=
            pic->data[i]= NULL;
        }
        pic->type= 0;        
    }
}

static int init_duplicate_context(MpegEncContext *s, MpegEncContext *base){
    int i;

    // edge emu needs blocksize + filter length - 1 (=17x17 for halfpel / 21x21 for h264) 
    CHECKED_ALLOCZ(s->allocated_edge_emu_buffer, (s->width+64)*2*17*2); //(width + edge + align)*interlaced*MBsize*tolerance
    s->edge_emu_buffer= s->allocated_edge_emu_buffer + (s->width+64)*2*17;

     //FIXME should be linesize instead of s->width*2 but that isnt known before get_buffer()
    CHECKED_ALLOCZ(s->me.scratchpad,  (s->width+64)*4*16*2*sizeof(uint8_t)) 
    s->rd_scratchpad=   s->me.scratchpad;
    s->b_scratchpad=    s->me.scratchpad;
    s->obmc_scratchpad= s->me.scratchpad + 16;
    if (s->encoding) {
        CHECKED_ALLOCZ(s->me.map      , ME_MAP_SIZE*sizeof(uint32_t))
        CHECKED_ALLOCZ(s->me.score_map, ME_MAP_SIZE*sizeof(uint32_t))
        if(s->avctx->noise_reduction){
            CHECKED_ALLOCZ(s->dct_error_sum, 2 * 64 * sizeof(int))
        }
    }   
    CHECKED_ALLOCZ(s->blocks, 64*12*2 * sizeof(DCTELEM))
    s->block= s->blocks[0];

    for(i=0;i<12;i++){
        s->pblocks[i] = (short *)(&s->block[i]);
    }
    return 0;
fail:
    return -1; //free() through MPV_common_end()
}

static void free_duplicate_context(MpegEncContext *s){
    if(s==NULL) return;

    av_freep(&s->allocated_edge_emu_buffer); s->edge_emu_buffer= NULL;
    av_freep(&s->me.scratchpad);
    s->rd_scratchpad=   
    s->b_scratchpad=    
    s->obmc_scratchpad= NULL;
    
    av_freep(&s->dct_error_sum);
    av_freep(&s->me.map);
    av_freep(&s->me.score_map);
    av_freep(&s->blocks);
    s->block= NULL;
}

static void backup_duplicate_context(MpegEncContext *bak, MpegEncContext *src){
#define COPY(a) bak->a= src->a
    COPY(allocated_edge_emu_buffer);
    COPY(edge_emu_buffer);
    COPY(me.scratchpad);
    COPY(rd_scratchpad);
    COPY(b_scratchpad);
    COPY(obmc_scratchpad);
    COPY(me.map);
    COPY(me.score_map);
    COPY(blocks);
    COPY(block);
    COPY(start_mb_y);
    COPY(end_mb_y);
    COPY(me.map_generation);
    COPY(pb);
    COPY(dct_error_sum);
    COPY(dct_count[0]);
    COPY(dct_count[1]);
#undef COPY
}

void ff_update_duplicate_context(MpegEncContext *dst, MpegEncContext *src){
    MpegEncContext bak;
    int i;
    //FIXME copy only needed parts
//START_TIMER
    backup_duplicate_context(&bak, dst);
    memcpy(dst, src, sizeof(MpegEncContext));
    backup_duplicate_context(dst, &bak);
    for(i=0;i<12;i++){
        dst->pblocks[i] = (short *)(&dst->block[i]);
    }
//STOP_TIMER("update_duplicate_context") //about 10k cycles / 0.01 sec for 1000frames on 1ghz with 2 threads
}

static void update_duplicate_context_after_me(MpegEncContext *dst, MpegEncContext *src){
#define COPY(a) dst->a= src->a
    COPY(pict_type);
    COPY(current_picture);
    COPY(f_code);
    COPY(b_code);
    COPY(qscale);
    COPY(lambda);
    COPY(lambda2);
    COPY(picture_in_gop_number);
    COPY(gop_picture_number);
    COPY(frame_pred_frame_dct); //FIXME dont set in encode_header
    COPY(progressive_frame); //FIXME dont set in encode_header
    COPY(partitioned_frame); //FIXME dont set in encode_header
#undef COPY
}

/**
 * sets the given MpegEncContext to common defaults (same for encoding and decoding).
 * the changed fields will not depend upon the prior state of the MpegEncContext.
 */
static void MPV_common_defaults(MpegEncContext *s){
    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    s->chroma_qscale_table= ff_default_chroma_qscale_table;
    s->progressive_frame= 1;
    s->progressive_sequence= 1;
    s->picture_structure= PICT_FRAME;

    s->coded_picture_number = 0;
    s->picture_number = 0;
    s->input_picture_number = 0;

    s->picture_in_gop_number = 0;

    s->f_code = 1;
    s->b_code = 1;
}

/**
 * sets the given MpegEncContext to defaults for decoding.
 * the changed fields will not depend upon the prior state of the MpegEncContext.
 */
void MPV_decode_defaults(MpegEncContext *s){
    MPV_common_defaults(s);
}

/**
 * sets the given MpegEncContext to defaults for encoding.
 * the changed fields will not depend upon the prior state of the MpegEncContext.
 */

#ifdef CONFIG_ENCODERS
static void MPV_encode_defaults(MpegEncContext *s){
    static int done=0;
    
    MPV_common_defaults(s);
    
    if(!done){
        int i;
        done=1;

        default_mv_penalty= av_mallocz( sizeof(uint8_t)*(MAX_FCODE+1)*(2*MAX_MV+1) );
        memset(default_mv_penalty, 0, sizeof(uint8_t)*(MAX_FCODE+1)*(2*MAX_MV+1));
        memset(default_fcode_tab , 0, sizeof(uint8_t)*(2*MAX_MV+1));

        for(i=-16; i<16; i++){
            default_fcode_tab[i + MAX_MV]= 1;
        }
    }
    s->me.mv_penalty= default_mv_penalty;
    s->fcode_tab= default_fcode_tab;
}
#endif //CONFIG_ENCODERS

/** 
 * init common structure for both encoder and decoder.
 * this assumes that some variables like width/height are already set
 */
int MPV_common_init(MpegEncContext *s)
{
    int y_size, c_size, yc_size, i, mb_array_size, mv_table_size, x, y;

    if(s->avctx->thread_count > MAX_THREADS || (16*s->avctx->thread_count > s->height && s->height)){
        av_log(s->avctx, AV_LOG_ERROR, "too many threads\n");
        return -1;
    }

    dsputil_init(&s->dsp, s->avctx);
    DCT_common_init(s);

    s->flags= s->avctx->flags;
    s->flags2= s->avctx->flags2;

    s->mb_width  = (s->width  + 15) / 16;
    s->mb_height = (s->height + 15) / 16;
    s->mb_stride = s->mb_width + 1;
    s->b8_stride = s->mb_width*2 + 1;
    s->b4_stride = s->mb_width*4 + 1;
    mb_array_size= s->mb_height * s->mb_stride;
    mv_table_size= (s->mb_height+2) * s->mb_stride + 1;

    /* set chroma shifts */
    avcodec_get_chroma_sub_sample(s->avctx->pix_fmt,&(s->chroma_x_shift),
                                                    &(s->chroma_y_shift) );

    /* set default edge pos, will be overriden in decode_header if needed */
    s->h_edge_pos= s->mb_width*16;
    s->v_edge_pos= s->mb_height*16;

    s->mb_num = s->mb_width * s->mb_height;
    
    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->b8_stride;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_stride;
 
    y_size = s->b8_stride * (2 * s->mb_height + 1);
    c_size = s->mb_stride * (s->mb_height + 1);
    yc_size = y_size + 2 * c_size;
    
    /* convert fourcc to upper case */
    s->avctx->codec_tag=   toupper( s->avctx->codec_tag     &0xFF)          
                        + (toupper((s->avctx->codec_tag>>8 )&0xFF)<<8 )
                        + (toupper((s->avctx->codec_tag>>16)&0xFF)<<16) 
                        + (toupper((s->avctx->codec_tag>>24)&0xFF)<<24);

    s->avctx->stream_codec_tag=   toupper( s->avctx->stream_codec_tag     &0xFF)          
                               + (toupper((s->avctx->stream_codec_tag>>8 )&0xFF)<<8 )
                               + (toupper((s->avctx->stream_codec_tag>>16)&0xFF)<<16) 
                               + (toupper((s->avctx->stream_codec_tag>>24)&0xFF)<<24);

    s->avctx->coded_frame= (AVFrame*)&s->current_picture;

    CHECKED_ALLOCZ(s->mb_index2xy, (s->mb_num+1)*sizeof(int)) //error ressilience code looks cleaner with this
    for(y=0; y<s->mb_height; y++){
        for(x=0; x<s->mb_width; x++){
            s->mb_index2xy[ x + y*s->mb_width ] = x + y*s->mb_stride;
        }
    }
    s->mb_index2xy[ s->mb_height*s->mb_width ] = (s->mb_height-1)*s->mb_stride + s->mb_width; //FIXME really needed?
    
    if (s->encoding) {
        /* Allocate MV tables */
        CHECKED_ALLOCZ(s->p_mv_table_base            , mv_table_size * 2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->b_forw_mv_table_base       , mv_table_size * 2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->b_back_mv_table_base       , mv_table_size * 2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->b_bidir_forw_mv_table_base , mv_table_size * 2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->b_bidir_back_mv_table_base , mv_table_size * 2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->b_direct_mv_table_base     , mv_table_size * 2 * sizeof(int16_t))
        s->p_mv_table           = s->p_mv_table_base            + s->mb_stride + 1;
        s->b_forw_mv_table      = s->b_forw_mv_table_base       + s->mb_stride + 1;
        s->b_back_mv_table      = s->b_back_mv_table_base       + s->mb_stride + 1;
        s->b_bidir_forw_mv_table= s->b_bidir_forw_mv_table_base + s->mb_stride + 1;
        s->b_bidir_back_mv_table= s->b_bidir_back_mv_table_base + s->mb_stride + 1;
        s->b_direct_mv_table    = s->b_direct_mv_table_base     + s->mb_stride + 1;

        if(s->msmpeg4_version){
            CHECKED_ALLOCZ(s->ac_stats, 2*2*(MAX_LEVEL+1)*(MAX_RUN+1)*2*sizeof(int));
        }
        CHECKED_ALLOCZ(s->avctx->stats_out, 256);

        /* Allocate MB type table */
        CHECKED_ALLOCZ(s->mb_type  , mb_array_size * sizeof(uint16_t)) //needed for encoding
        
        CHECKED_ALLOCZ(s->lambda_table, mb_array_size * sizeof(int))
        
        CHECKED_ALLOCZ(s->q_intra_matrix, 64*32 * sizeof(int))
        CHECKED_ALLOCZ(s->q_inter_matrix, 64*32 * sizeof(int))
        CHECKED_ALLOCZ(s->q_intra_matrix16, 64*32*2 * sizeof(uint16_t))
        CHECKED_ALLOCZ(s->q_inter_matrix16, 64*32*2 * sizeof(uint16_t))
        CHECKED_ALLOCZ(s->input_picture, MAX_PICTURE_COUNT * sizeof(Picture*))
        CHECKED_ALLOCZ(s->reordered_input_picture, MAX_PICTURE_COUNT * sizeof(Picture*))
        
        if(s->avctx->noise_reduction){
            CHECKED_ALLOCZ(s->dct_offset, 2 * 64 * sizeof(uint16_t))
        }
    }
    CHECKED_ALLOCZ(s->picture, MAX_PICTURE_COUNT * sizeof(Picture))

    CHECKED_ALLOCZ(s->error_status_table, mb_array_size*sizeof(uint8_t))
    
    if(s->codec_id==CODEC_ID_MPEG4 || (s->flags & CODEC_FLAG_INTERLACED_ME)){
        /* interlaced direct mode decoding tables */
            for(i=0; i<2; i++){
                int j, k;
                for(j=0; j<2; j++){
                    for(k=0; k<2; k++){
                        CHECKED_ALLOCZ(s->b_field_mv_table_base[i][j][k]     , mv_table_size * 2 * sizeof(int16_t))
                        s->b_field_mv_table[i][j][k]    = s->b_field_mv_table_base[i][j][k]     + s->mb_stride + 1;
                    }
                    CHECKED_ALLOCZ(s->b_field_select_table[i][j]     , mb_array_size * 2 * sizeof(uint8_t))
                    CHECKED_ALLOCZ(s->p_field_mv_table_base[i][j]     , mv_table_size * 2 * sizeof(int16_t))
                    s->p_field_mv_table[i][j]    = s->p_field_mv_table_base[i][j]     + s->mb_stride + 1;
                }
                CHECKED_ALLOCZ(s->p_field_select_table[i]      , mb_array_size * 2 * sizeof(uint8_t))
            }
    }
    if (s->out_format == FMT_H263) {
        /* ac values */
        CHECKED_ALLOCZ(s->ac_val_base, yc_size * sizeof(int16_t) * 16);
        s->ac_val[0] = s->ac_val_base + s->b8_stride + 1;
        s->ac_val[1] = s->ac_val_base + y_size + s->mb_stride + 1;
        s->ac_val[2] = s->ac_val[1] + c_size;
        
        /* cbp values */
        CHECKED_ALLOCZ(s->coded_block_base, y_size);
        s->coded_block= s->coded_block_base + s->b8_stride + 1;
        
        /* divx501 bitstream reorder buffer */
        CHECKED_ALLOCZ(s->bitstream_buffer, BITSTREAM_BUFFER_SIZE);

        /* cbp, ac_pred, pred_dir */
        CHECKED_ALLOCZ(s->cbp_table  , mb_array_size * sizeof(uint8_t))
        CHECKED_ALLOCZ(s->pred_dir_table, mb_array_size * sizeof(uint8_t))
    }
    
    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        //MN: we need these for error resilience of intra-frames
        CHECKED_ALLOCZ(s->dc_val_base, yc_size * sizeof(int16_t));
        s->dc_val[0] = s->dc_val_base + s->b8_stride + 1;
        s->dc_val[1] = s->dc_val_base + y_size + s->mb_stride + 1;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for(i=0;i<yc_size;i++)
            s->dc_val_base[i] = 1024;
    }

    /* which mb is a intra block */
    CHECKED_ALLOCZ(s->mbintra_table, mb_array_size);
    memset(s->mbintra_table, 1, mb_array_size);
    
    /* init macroblock skip table */
    CHECKED_ALLOCZ(s->mbskip_table, mb_array_size+2);
    //Note the +1 is for a quicker mpeg4 slice_end detection
    CHECKED_ALLOCZ(s->prev_pict_types, PREV_PICT_TYPES_BUFFER_SIZE);
    
    s->parse_context.state= -1;
    if((s->avctx->debug&(FF_DEBUG_VIS_QP|FF_DEBUG_VIS_MB_TYPE)) || (s->avctx->debug_mv)){
       s->visualization_buffer[0] = av_malloc((s->mb_width*16 + 2*EDGE_WIDTH) * s->mb_height*16 + 2*EDGE_WIDTH);
       s->visualization_buffer[1] = av_malloc((s->mb_width*8 + EDGE_WIDTH) * s->mb_height*8 + EDGE_WIDTH);
       s->visualization_buffer[2] = av_malloc((s->mb_width*8 + EDGE_WIDTH) * s->mb_height*8 + EDGE_WIDTH);
    }

    s->context_initialized = 1;

    s->thread_context[0]= s;
    for(i=1; i<s->avctx->thread_count; i++){
        s->thread_context[i]= av_malloc(sizeof(MpegEncContext));
        memcpy(s->thread_context[i], s, sizeof(MpegEncContext));
    }

    for(i=0; i<s->avctx->thread_count; i++){
        if(init_duplicate_context(s->thread_context[i], s) < 0)
           goto fail;
        s->thread_context[i]->start_mb_y= (s->mb_height*(i  ) + s->avctx->thread_count/2) / s->avctx->thread_count;
        s->thread_context[i]->end_mb_y  = (s->mb_height*(i+1) + s->avctx->thread_count/2) / s->avctx->thread_count;
    }

    return 0;
 fail:
    MPV_common_end(s);
    return -1;
}

/* init common structure for both encoder and decoder */
void MPV_common_end(MpegEncContext *s)
{
    int i, j, k;

    for(i=0; i<s->avctx->thread_count; i++){
        free_duplicate_context(s->thread_context[i]);
    }
    for(i=1; i<s->avctx->thread_count; i++){
        av_freep(&s->thread_context[i]);
    }

    av_freep(&s->parse_context.buffer);
    s->parse_context.buffer_size=0;

    av_freep(&s->mb_type);
    av_freep(&s->p_mv_table_base);
    av_freep(&s->b_forw_mv_table_base);
    av_freep(&s->b_back_mv_table_base);
    av_freep(&s->b_bidir_forw_mv_table_base);
    av_freep(&s->b_bidir_back_mv_table_base);
    av_freep(&s->b_direct_mv_table_base);
    s->p_mv_table= NULL;
    s->b_forw_mv_table= NULL;
    s->b_back_mv_table= NULL;
    s->b_bidir_forw_mv_table= NULL;
    s->b_bidir_back_mv_table= NULL;
    s->b_direct_mv_table= NULL;
    for(i=0; i<2; i++){
        for(j=0; j<2; j++){
            for(k=0; k<2; k++){
                av_freep(&s->b_field_mv_table_base[i][j][k]);
                s->b_field_mv_table[i][j][k]=NULL;
            }
            av_freep(&s->b_field_select_table[i][j]);
            av_freep(&s->p_field_mv_table_base[i][j]);
            s->p_field_mv_table[i][j]=NULL;
        }
        av_freep(&s->p_field_select_table[i]);
    }
    
    av_freep(&s->dc_val_base);
    av_freep(&s->ac_val_base);
    av_freep(&s->coded_block_base);
    av_freep(&s->mbintra_table);
    av_freep(&s->cbp_table);
    av_freep(&s->pred_dir_table);
    
    av_freep(&s->mbskip_table);
    av_freep(&s->prev_pict_types);
    av_freep(&s->bitstream_buffer);
    av_freep(&s->avctx->stats_out);
    av_freep(&s->ac_stats);
    av_freep(&s->error_status_table);
    av_freep(&s->mb_index2xy);
    av_freep(&s->lambda_table);
    av_freep(&s->q_intra_matrix);
    av_freep(&s->q_inter_matrix);
    av_freep(&s->q_intra_matrix16);
    av_freep(&s->q_inter_matrix16);
    av_freep(&s->input_picture);
    av_freep(&s->reordered_input_picture);
    av_freep(&s->dct_offset);

    if(s->picture){
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            free_picture(s, &s->picture[i]);
        }
    }
    av_freep(&s->picture);
    s->context_initialized = 0;
    s->last_picture_ptr=
    s->next_picture_ptr=
    s->current_picture_ptr= NULL;

    for(i=0; i<3; i++)
        av_freep(&s->visualization_buffer[i]);
}

#ifdef CONFIG_ENCODERS

/* init video encoder */
int MPV_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i, dummy;
    int chroma_h_shift, chroma_v_shift;
    
    MPV_encode_defaults(s);

    avctx->pix_fmt = PIX_FMT_YUV420P; // FIXME

    s->bit_rate = avctx->bit_rate;
    s->width = avctx->width;
    s->height = avctx->height;
    if(avctx->gop_size > 600){
	av_log(avctx, AV_LOG_ERROR, "Warning keyframe interval too large! reducing it ...\n");
        avctx->gop_size=600;
    }
    s->gop_size = avctx->gop_size;
    s->avctx = avctx;
    s->flags= avctx->flags;
    s->flags2= avctx->flags2;
    s->max_b_frames= avctx->max_b_frames;
    s->codec_id= avctx->codec->id;
    s->luma_elim_threshold  = avctx->luma_elim_threshold;
    s->chroma_elim_threshold= avctx->chroma_elim_threshold;
    s->strict_std_compliance= avctx->strict_std_compliance;
    s->data_partitioning= avctx->flags & CODEC_FLAG_PART;
    s->quarter_sample= (avctx->flags & CODEC_FLAG_QPEL)!=0;
    s->mpeg_quant= avctx->mpeg_quant;
    s->rtp_mode= !!avctx->rtp_payload_size;
    s->intra_dc_precision= avctx->intra_dc_precision;

    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }

    s->me_method = avctx->me_method;

    /* Fixed QSCALE */
    s->fixed_qscale = !!(avctx->flags & CODEC_FLAG_QSCALE);
    
    s->adaptive_quant= (   s->avctx->lumi_masking
                        || s->avctx->dark_masking
                        || s->avctx->temporal_cplx_masking 
                        || s->avctx->spatial_cplx_masking
                        || s->avctx->p_masking
                        || (s->flags&CODEC_FLAG_QP_RD))
                       && !s->fixed_qscale;
    
    s->obmc= !!(s->flags & CODEC_FLAG_OBMC);
    s->loop_filter= !!(s->flags & CODEC_FLAG_LOOP_FILTER);
    s->alternate_scan= !!(s->flags & CODEC_FLAG_ALT_SCAN);

    if(avctx->rc_max_rate && !avctx->rc_buffer_size){
        av_log(avctx, AV_LOG_ERROR, "a vbv buffer size is needed, for encoding with a maximum bitrate\n");
        return -1;
    }    

    if(avctx->rc_min_rate && avctx->rc_max_rate != avctx->rc_min_rate){
        av_log(avctx, AV_LOG_INFO, "Warning min_rate > 0 but min_rate != max_rate isnt recommanded!\n");
    }
    
    if(avctx->rc_min_rate && avctx->rc_min_rate > avctx->bit_rate){
        av_log(avctx, AV_LOG_INFO, "bitrate below min bitrate\n");
        return -1;
    }
    
    if(avctx->rc_max_rate && avctx->rc_max_rate < avctx->bit_rate){
        av_log(avctx, AV_LOG_INFO, "bitrate above max bitrate\n");
        return -1;
    }
        
    if(   s->avctx->rc_max_rate && s->avctx->rc_min_rate == s->avctx->rc_max_rate 
       && (s->codec_id == CODEC_ID_MPEG1VIDEO || s->codec_id == CODEC_ID_MPEG2VIDEO)
       && 90000LL * (avctx->rc_buffer_size-1) > s->avctx->rc_max_rate*0xFFFFLL){
        
        av_log(avctx, AV_LOG_INFO, "Warning vbv_delay will be set to 0xFFFF (=VBR) as the specified vbv buffer is too large for the given bitrate!\n");
    }
       
    if((s->flags & CODEC_FLAG_4MV) && s->codec_id != CODEC_ID_MPEG4 
       && s->codec_id != CODEC_ID_H263 && s->codec_id != CODEC_ID_H263P && s->codec_id != CODEC_ID_FLV1){
        av_log(avctx, AV_LOG_ERROR, "4MV not supported by codec\n");
        return -1;
    }
        
    if(s->obmc && s->avctx->mb_decision != FF_MB_DECISION_SIMPLE){
        av_log(avctx, AV_LOG_ERROR, "OBMC is only supported with simple mb decission\n");
        return -1;
    }
    
    if(s->obmc && s->codec_id != CODEC_ID_H263 && s->codec_id != CODEC_ID_H263P){
        av_log(avctx, AV_LOG_ERROR, "OBMC is only supported with H263(+)\n");
        return -1;
    }
    
    if(s->quarter_sample && s->codec_id != CODEC_ID_MPEG4){
        av_log(avctx, AV_LOG_ERROR, "qpel not supported by codec\n");
        return -1;
    }

    if(s->data_partitioning && s->codec_id != CODEC_ID_MPEG4){
        av_log(avctx, AV_LOG_ERROR, "data partitioning not supported by codec\n");
        return -1;
    }
    
    if(s->max_b_frames && s->codec_id != CODEC_ID_MPEG4 && s->codec_id != CODEC_ID_MPEG1VIDEO && s->codec_id != CODEC_ID_MPEG2VIDEO){
        av_log(avctx, AV_LOG_ERROR, "b frames not supported by codec\n");
        return -1;
    }

    if((s->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME|CODEC_FLAG_ALT_SCAN)) 
       && s->codec_id != CODEC_ID_MPEG4 && s->codec_id != CODEC_ID_MPEG2VIDEO){
        av_log(avctx, AV_LOG_ERROR, "interlacing not supported by codec\n");
        return -1;
    }
        
    if(s->mpeg_quant && s->codec_id != CODEC_ID_MPEG4){ //FIXME mpeg2 uses that too
        av_log(avctx, AV_LOG_ERROR, "mpeg2 style quantization not supporetd by codec\n");
        return -1;
    }
        
    if((s->flags & CODEC_FLAG_CBP_RD) && !(s->flags & CODEC_FLAG_TRELLIS_QUANT)){
        av_log(avctx, AV_LOG_ERROR, "CBP RD needs trellis quant\n");
        return -1;
    }

    if((s->flags & CODEC_FLAG_QP_RD) && s->avctx->mb_decision != FF_MB_DECISION_RD){
        av_log(avctx, AV_LOG_ERROR, "QP RD needs mbd=2\n");
        return -1;
    }
    
    if(s->avctx->scenechange_threshold < 1000000000 && (s->flags & CODEC_FLAG_CLOSED_GOP)){
        av_log(avctx, AV_LOG_ERROR, "closed gop with scene change detection arent supported yet\n");
        return -1;
    }
    
    if(s->avctx->thread_count > 1 && s->codec_id != CODEC_ID_MPEG4 
       && s->codec_id != CODEC_ID_MPEG1VIDEO && s->codec_id != CODEC_ID_MPEG2VIDEO 
       && (s->codec_id != CODEC_ID_H263P || !(s->flags & CODEC_FLAG_H263P_SLICE_STRUCT))){
        av_log(avctx, AV_LOG_ERROR, "multi threaded encoding not supported by codec\n");
        return -1;
    }
    
    if(s->avctx->thread_count > 1)
        s->rtp_mode= 1;

    i= ff_gcd(avctx->frame_rate, avctx->frame_rate_base);
    if(i > 1){
        av_log(avctx, AV_LOG_INFO, "removing common factors from framerate\n");
        avctx->frame_rate /= i;
        avctx->frame_rate_base /= i;
//        return -1;
    }
    
    if(s->codec_id==CODEC_ID_MJPEG){
        s->intra_quant_bias= 1<<(QUANT_BIAS_SHIFT-1); //(a + x/2)/x
        s->inter_quant_bias= 0;
    }else if(s->mpeg_quant || s->codec_id==CODEC_ID_MPEG1VIDEO || s->codec_id==CODEC_ID_MPEG2VIDEO){
        s->intra_quant_bias= 3<<(QUANT_BIAS_SHIFT-3); //(a + x*3/8)/x
        s->inter_quant_bias= 0;
    }else{
        s->intra_quant_bias=0;
        s->inter_quant_bias=-(1<<(QUANT_BIAS_SHIFT-2)); //(a - x/4)/x
    }
    
    if(avctx->intra_quant_bias != FF_DEFAULT_QUANT_BIAS)
        s->intra_quant_bias= avctx->intra_quant_bias;
    if(avctx->inter_quant_bias != FF_DEFAULT_QUANT_BIAS)
        s->inter_quant_bias= avctx->inter_quant_bias;
        
    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &chroma_h_shift, &chroma_v_shift);

    av_reduce(&s->time_increment_resolution, &dummy, s->avctx->frame_rate, s->avctx->frame_rate_base, (1<<16)-1);
    s->time_increment_bits = av_log2(s->time_increment_resolution - 1) + 1;

    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        s->low_delay= 0; //s->max_b_frames ? 0 : 1;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1);
        break;
    case CODEC_ID_MPEG2VIDEO:
        s->out_format = FMT_MPEG1;
        s->low_delay= 0; //s->max_b_frames ? 0 : 1;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1);
        s->rtp_mode= 1;
        break;
    case CODEC_ID_LJPEG:
    case CODEC_ID_MJPEG:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        s->mjpeg_write_tables = 1; /* write all tables */
	s->mjpeg_data_only_frames = 0; /* write all the needed headers */
        s->mjpeg_vsample[0] = 1<<chroma_v_shift;
        s->mjpeg_vsample[1] = 1;
        s->mjpeg_vsample[2] = 1; 
        s->mjpeg_hsample[0] = 1<<chroma_h_shift;
        s->mjpeg_hsample[1] = 1; 
        s->mjpeg_hsample[2] = 1; 
        if (mjpeg_init(s) < 0)
            return -1;
        avctx->delay=0;
        s->low_delay=1;
        break;
#ifdef CONFIG_RISKY
    case CODEC_ID_H263:
        if (h263_get_picture_format(s->width, s->height) == 7) {
            av_log(avctx, AV_LOG_INFO, "Input picture size isn't suitable for h263 codec! try h263+\n");
            return -1;
        }
        s->out_format = FMT_H263;
	s->obmc= (avctx->flags & CODEC_FLAG_OBMC) ? 1:0;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->h263_plus = 1;
	/* Fx */
        s->umvplus = (avctx->flags & CODEC_FLAG_H263P_UMV) ? 1:0;
	s->h263_aic= (avctx->flags & CODEC_FLAG_H263P_AIC) ? 1:0;
	s->modified_quant= s->h263_aic;
	s->alt_inter_vlc= (avctx->flags & CODEC_FLAG_H263P_AIV) ? 1:0;
	s->obmc= (avctx->flags & CODEC_FLAG_OBMC) ? 1:0;
	s->loop_filter= (avctx->flags & CODEC_FLAG_LOOP_FILTER) ? 1:0;
	s->unrestricted_mv= s->obmc || s->loop_filter || s->umvplus;
        s->h263_slice_structured= (s->flags & CODEC_FLAG_H263P_SLICE_STRUCT) ? 1:0;

	/* /Fx */
        /* These are just to be sure */
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_FLV1:
        s->out_format = FMT_H263;
        s->h263_flv = 2; /* format = 1; 11-bit codes */
        s->unrestricted_mv = 1;
        s->rtp_mode=0; /* don't allow GOB */
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_MPEG4:
        s->out_format = FMT_H263;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->low_delay= s->max_b_frames ? 0 : 1;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1);
        break;
    case CODEC_ID_MSMPEG4V1:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 1;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_MSMPEG4V2:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 2;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_MSMPEG4V3:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 3;
        s->flipflop_rounding=1;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_WMV1:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 4;
        s->flipflop_rounding=1;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_WMV2:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 5;
        s->flipflop_rounding=1;
        avctx->delay=0;
        s->low_delay=1;
        break;
#endif
    default:
        return -1;
    }
    
    avctx->has_b_frames= !s->low_delay;

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;

    if(s->modified_quant)
        s->chroma_qscale_table= ff_h263_chroma_qscale_table;
    s->progressive_frame= 
    s->progressive_sequence= !(avctx->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME));
    s->quant_precision=5;
    
    ff_set_cmp(&s->dsp, s->dsp.ildct_cmp, s->avctx->ildct_cmp);
    
#ifdef CONFIG_ENCODERS
#ifdef CONFIG_RISKY
    if (s->out_format == FMT_H263)
        h263_encode_init(s);
    if(s->msmpeg4_version)
        ff_msmpeg4_encode_init(s);
#endif
    if (s->out_format == FMT_MPEG1)
        ff_mpeg1_encode_init(s);
#endif

    /* init q matrix */
    for(i=0;i<64;i++) {
        int j= s->dsp.idct_permutation[i];
#ifdef CONFIG_RISKY
        if(s->codec_id==CODEC_ID_MPEG4 && s->mpeg_quant){
            s->intra_matrix[j] = ff_mpeg4_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg4_default_non_intra_matrix[i];
        }else if(s->out_format == FMT_H263){
            s->intra_matrix[j] =
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        }else
#endif
        { /* mpeg1/2 */
            s->intra_matrix[j] = ff_mpeg1_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        }
        if(s->avctx->intra_matrix)
            s->intra_matrix[j] = s->avctx->intra_matrix[i];
        if(s->avctx->inter_matrix)
            s->inter_matrix[j] = s->avctx->inter_matrix[i];
    }

    /* precompute matrix */
    /* for mjpeg, we do include qscale in the matrix */
    if (s->out_format != FMT_MJPEG) {
        convert_matrix(&s->dsp, s->q_intra_matrix, s->q_intra_matrix16, 
                       s->intra_matrix, s->intra_quant_bias, 1, 31);
        convert_matrix(&s->dsp, s->q_inter_matrix, s->q_inter_matrix16, 
                       s->inter_matrix, s->inter_quant_bias, 1, 31);
    }

    if(ff_rate_control_init(s) < 0)
        return -1;
    
    return 0;
}

int MPV_encode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

#ifdef STATS
    print_stats();
#endif

    ff_rate_control_uninit(s);

    MPV_common_end(s);
    if (s->out_format == FMT_MJPEG)
        mjpeg_close(s);

    av_freep(&avctx->extradata);
      
    return 0;
}

#endif //CONFIG_ENCODERS

void init_rl(RLTable *rl)
{
    int8_t max_level[MAX_RUN+1], max_run[MAX_LEVEL+1];
    uint8_t index_run[MAX_RUN+1];
    int last, run, level, start, end, i;

    /* compute max_level[], max_run[] and index_run[] */
    for(last=0;last<2;last++) {
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(max_level, 0, MAX_RUN + 1);
        memset(max_run, 0, MAX_LEVEL + 1);
        memset(index_run, rl->n, MAX_RUN + 1);
        for(i=start;i<end;i++) {
            run = rl->table_run[i];
            level = rl->table_level[i];
            if (index_run[run] == rl->n)
                index_run[run] = i;
            if (level > max_level[run])
                max_level[run] = level;
            if (run > max_run[level])
                max_run[level] = run;
        }
        rl->max_level[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->max_level[last], max_level, MAX_RUN + 1);
        rl->max_run[last] = av_malloc(MAX_LEVEL + 1);
        memcpy(rl->max_run[last], max_run, MAX_LEVEL + 1);
        rl->index_run[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->index_run[last], index_run, MAX_RUN + 1);
    }
}

/* draw the edges of width 'w' of an image of size width, height */
//FIXME check that this is ok for mpeg4 interlaced
static void draw_edges_c(uint8_t *buf, int wrap, int width, int height, int w)
{
    uint8_t *ptr, *last_line;
    int i;

    last_line = buf + (height - 1) * wrap;
    for(i=0;i<w;i++) {
        /* top and bottom */
        memcpy(buf - (i + 1) * wrap, buf, width);
        memcpy(last_line + (i + 1) * wrap, last_line, width);
    }
    /* left and right */
    ptr = buf;
    for(i=0;i<height;i++) {
        memset(ptr - w, ptr[0], w);
        memset(ptr + width, ptr[width-1], w);
        ptr += wrap;
    }
    /* corners */
    for(i=0;i<w;i++) {
        memset(buf - (i + 1) * wrap - w, buf[0], w); /* top left */
        memset(buf - (i + 1) * wrap + width, buf[width-1], w); /* top right */
        memset(last_line + (i + 1) * wrap - w, last_line[0], w); /* top left */
        memset(last_line + (i + 1) * wrap + width, last_line[width-1], w); /* top right */
    }
}

int ff_find_unused_picture(MpegEncContext *s, int shared){
    int i;
    
    if(shared){
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL && s->picture[i].type==0) return i;
        }
    }else{
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL && s->picture[i].type!=0) return i; //FIXME
        }
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL) return i;
        }
    }

    assert(0);
    return -1;
}

static void update_noise_reduction(MpegEncContext *s){
    int intra, i;

    for(intra=0; intra<2; intra++){
        if(s->dct_count[intra] > (1<<16)){
            for(i=0; i<64; i++){
                s->dct_error_sum[intra][i] >>=1;
            }
            s->dct_count[intra] >>= 1;
        }
        
        for(i=0; i<64; i++){
            s->dct_offset[intra][i]= (s->avctx->noise_reduction * s->dct_count[intra] + s->dct_error_sum[intra][i]/2) / (s->dct_error_sum[intra][i]+1);
        }
    }
}

/**
 * generic function for encode/decode called after coding/decoding the header and before a frame is coded/decoded
 */
int MPV_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int i;
    AVFrame *pic;
    s->mb_skiped = 0;

    assert(s->last_picture_ptr==NULL || s->out_format != FMT_H264 || s->codec_id == CODEC_ID_SVQ3);

    /* mark&release old frames */
    if (s->pict_type != B_TYPE && s->last_picture_ptr && s->last_picture_ptr != s->next_picture_ptr && s->last_picture_ptr->data[0]) {
        avctx->release_buffer(avctx, (AVFrame*)s->last_picture_ptr);

        /* release forgotten pictures */
        /* if(mpeg124/h263) */
        if(!s->encoding){
            for(i=0; i<MAX_PICTURE_COUNT; i++){
                if(s->picture[i].data[0] && &s->picture[i] != s->next_picture_ptr && s->picture[i].reference){
                    av_log(avctx, AV_LOG_ERROR, "releasing zombie picture\n");
                    avctx->release_buffer(avctx, (AVFrame*)&s->picture[i]);                
                }
            }
        }
    }
alloc:
    if(!s->encoding){
        /* release non refernce frames */
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0] && !s->picture[i].reference /*&& s->picture[i].type!=FF_BUFFER_TYPE_SHARED*/){
                s->avctx->release_buffer(s->avctx, (AVFrame*)&s->picture[i]);
            }
        }

        if(s->current_picture_ptr && s->current_picture_ptr->data[0]==NULL)
            pic= (AVFrame*)s->current_picture_ptr; //we allready have a unused image (maybe it was set before reading the header)
        else{
            i= ff_find_unused_picture(s, 0);
            pic= (AVFrame*)&s->picture[i];
        }

        pic->reference= s->pict_type != B_TYPE && !s->dropable ? 3 : 0;

        pic->coded_picture_number= s->coded_picture_number++;
        
        if( alloc_picture(s, (Picture*)pic, 0) < 0)
            return -1;

        s->current_picture_ptr= (Picture*)pic;
        s->current_picture_ptr->top_field_first= s->top_field_first; //FIXME use only the vars from current_pic
        s->current_picture_ptr->interlaced_frame= !s->progressive_frame && !s->progressive_sequence;
    }

    s->current_picture_ptr->pict_type= s->pict_type;
//    if(s->flags && CODEC_FLAG_QSCALE) 
  //      s->current_picture_ptr->quality= s->new_picture_ptr->quality;
    s->current_picture_ptr->key_frame= s->pict_type == I_TYPE;

    copy_picture(&s->current_picture, s->current_picture_ptr);
  
  if(s->out_format != FMT_H264 || s->codec_id == CODEC_ID_SVQ3){
    if (s->pict_type != B_TYPE) {
        s->last_picture_ptr= s->next_picture_ptr;
        if(!s->dropable)
            s->next_picture_ptr= s->current_picture_ptr;
    }
/*    av_log(s->avctx, AV_LOG_DEBUG, "L%p N%p C%p L%p N%p C%p type:%d drop:%d\n", s->last_picture_ptr, s->next_picture_ptr,s->current_picture_ptr,
        s->last_picture_ptr    ? s->last_picture_ptr->data[0] : NULL, 
        s->next_picture_ptr    ? s->next_picture_ptr->data[0] : NULL, 
        s->current_picture_ptr ? s->current_picture_ptr->data[0] : NULL,
        s->pict_type, s->dropable);*/
    
    if(s->last_picture_ptr) copy_picture(&s->last_picture, s->last_picture_ptr);
    if(s->next_picture_ptr) copy_picture(&s->next_picture, s->next_picture_ptr);
    
    if(s->pict_type != I_TYPE && (s->last_picture_ptr==NULL || s->last_picture_ptr->data[0]==NULL)){
        av_log(avctx, AV_LOG_ERROR, "warning: first frame is no keyframe\n");
        assert(s->pict_type != B_TYPE); //these should have been dropped if we dont have a reference
        goto alloc;
    }

    assert(s->pict_type == I_TYPE || (s->last_picture_ptr && s->last_picture_ptr->data[0]));

    if(s->picture_structure!=PICT_FRAME){
        int i;
        for(i=0; i<4; i++){
            if(s->picture_structure == PICT_BOTTOM_FIELD){
                 s->current_picture.data[i] += s->current_picture.linesize[i];
            } 
            s->current_picture.linesize[i] *= 2;
            s->last_picture.linesize[i] *=2;
            s->next_picture.linesize[i] *=2;
        }
    }
  }
   
    s->hurry_up= s->avctx->hurry_up;
    s->error_resilience= avctx->error_resilience;

    /* set dequantizer, we cant do it during init as it might change for mpeg4
       and we cant do it in the header decode as init isnt called for mpeg4 there yet */
    if(s->mpeg_quant || s->codec_id == CODEC_ID_MPEG2VIDEO){
        s->dct_unquantize_intra = s->dct_unquantize_mpeg2_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg2_inter;
    }else if(s->out_format == FMT_H263){
        s->dct_unquantize_intra = s->dct_unquantize_h263_intra;
        s->dct_unquantize_inter = s->dct_unquantize_h263_inter;
    }else if(s->out_format == FMT_H261){
        s->dct_unquantize_intra = s->dct_unquantize_h261_intra;
        s->dct_unquantize_inter = s->dct_unquantize_h261_inter;
    }else{
        s->dct_unquantize_intra = s->dct_unquantize_mpeg1_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg1_inter;
    }

    if(s->dct_error_sum){
        assert(s->avctx->noise_reduction && s->encoding);

        update_noise_reduction(s);
    }
        
#ifdef HAVE_XVMC
    if(s->avctx->xvmc_acceleration)
        return XVMC_field_start(s, avctx);
#endif
    return 0;
}

/* generic function for encode/decode called after a frame has been coded/decoded */
void MPV_frame_end(MpegEncContext *s)
{
    int i;
    /* draw edge for correct motion prediction if outside */
#ifdef HAVE_XVMC
//just to make sure that all data is rendered.
    if(s->avctx->xvmc_acceleration){
        XVMC_field_end(s);
    }else
#endif
    if(s->unrestricted_mv && s->pict_type != B_TYPE && !s->intra_only && !(s->flags&CODEC_FLAG_EMU_EDGE)) {
            draw_edges(s->current_picture.data[0], s->linesize  , s->h_edge_pos   , s->v_edge_pos   , EDGE_WIDTH  );
            draw_edges(s->current_picture.data[1], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
            draw_edges(s->current_picture.data[2], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
    }
    emms_c();
    
    s->last_pict_type    = s->pict_type;
    if(s->pict_type!=B_TYPE){
        s->last_non_b_pict_type= s->pict_type;
    }
#if 0
        /* copy back current_picture variables */
    for(i=0; i<MAX_PICTURE_COUNT; i++){
        if(s->picture[i].data[0] == s->current_picture.data[0]){
            s->picture[i]= s->current_picture;
            break;
        }    
    }
    assert(i<MAX_PICTURE_COUNT);
#endif    

    if(s->encoding){
        /* release non refernce frames */
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0] && !s->picture[i].reference /*&& s->picture[i].type!=FF_BUFFER_TYPE_SHARED*/){
                s->avctx->release_buffer(s->avctx, (AVFrame*)&s->picture[i]);
            }
        }
    }
    // clear copies, to avoid confusion
#if 0
    memset(&s->last_picture, 0, sizeof(Picture));
    memset(&s->next_picture, 0, sizeof(Picture));
    memset(&s->current_picture, 0, sizeof(Picture));
#endif
}

/**
 * draws an line from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_line(uint8_t *buf, int sx, int sy, int ex, int ey, int w, int h, int stride, int color){
    int t, x, y, fr, f;
    
    sx= clip(sx, 0, w-1);
    sy= clip(sy, 0, h-1);
    ex= clip(ex, 0, w-1);
    ey= clip(ey, 0, h-1);
    
    buf[sy*stride + sx]+= color;
    
    if(ABS(ex - sx) > ABS(ey - sy)){
        if(sx > ex){
            t=sx; sx=ex; ex=t;
            t=sy; sy=ey; ey=t;
        }
        buf+= sx + sy*stride;
        ex-= sx;
        f= ((ey-sy)<<16)/ex;
        for(x= 0; x <= ex; x++){
            y = (x*f)>>16;
            fr= (x*f)&0xFFFF;
            buf[ y   *stride + x]+= (color*(0x10000-fr))>>16;
            buf[(y+1)*stride + x]+= (color*         fr )>>16;
        }
    }else{
        if(sy > ey){
            t=sx; sx=ex; ex=t;
            t=sy; sy=ey; ey=t;
        }
        buf+= sx + sy*stride;
        ey-= sy;
        if(ey) f= ((ex-sx)<<16)/ey;
        else   f= 0;
        for(y= 0; y <= ey; y++){
            x = (y*f)>>16;
            fr= (y*f)&0xFFFF;
            buf[y*stride + x  ]+= (color*(0x10000-fr))>>16;;
            buf[y*stride + x+1]+= (color*         fr )>>16;;
        }
    }
}

/**
 * draws an arrow from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_arrow(uint8_t *buf, int sx, int sy, int ex, int ey, int w, int h, int stride, int color){ 
    int dx,dy;

    sx= clip(sx, -100, w+100);
    sy= clip(sy, -100, h+100);
    ex= clip(ex, -100, w+100);
    ey= clip(ey, -100, h+100);
    
    dx= ex - sx;
    dy= ey - sy;
    
    if(dx*dx + dy*dy > 3*3){
        int rx=  dx + dy;
        int ry= -dx + dy;
        int length= ff_sqrt((rx*rx + ry*ry)<<8);
        
        //FIXME subpixel accuracy
        rx= ROUNDED_DIV(rx*3<<4, length);
        ry= ROUNDED_DIV(ry*3<<4, length);
        
        draw_line(buf, sx, sy, sx + rx, sy + ry, w, h, stride, color);
        draw_line(buf, sx, sy, sx - ry, sy + rx, w, h, stride, color);
    }
    draw_line(buf, sx, sy, ex, ey, w, h, stride, color);
}

/**
 * prints debuging info for the given picture.
 */
void ff_print_debug_info(MpegEncContext *s, AVFrame *pict){

    if(!pict || !pict->mb_type) return;

    if(s->avctx->debug&(FF_DEBUG_SKIP | FF_DEBUG_QP | FF_DEBUG_MB_TYPE)){
        int x,y;
        
        av_log(s->avctx,AV_LOG_DEBUG,"New frame, type: ");
        switch (pict->pict_type) {
            case FF_I_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"I\n"); break;
            case FF_P_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"P\n"); break;
            case FF_B_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"B\n"); break;
            case FF_S_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"S\n"); break;
            case FF_SI_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"SI\n"); break;
            case FF_SP_TYPE: av_log(s->avctx,AV_LOG_DEBUG,"SP\n"); break;            
        }
        for(y=0; y<s->mb_height; y++){
            for(x=0; x<s->mb_width; x++){
                if(s->avctx->debug&FF_DEBUG_SKIP){
                    int count= s->mbskip_table[x + y*s->mb_stride];
                    if(count>9) count=9;
                    av_log(s->avctx, AV_LOG_DEBUG, "%1d", count);
                }
                if(s->avctx->debug&FF_DEBUG_QP){
                    av_log(s->avctx, AV_LOG_DEBUG, "%2d", pict->qscale_table[x + y*s->mb_stride]);
                }
                if(s->avctx->debug&FF_DEBUG_MB_TYPE){
                    int mb_type= pict->mb_type[x + y*s->mb_stride];
                    //Type & MV direction
                    if(IS_PCM(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "P");
                    else if(IS_INTRA(mb_type) && IS_ACPRED(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "A");
                    else if(IS_INTRA4x4(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "i");
                    else if(IS_INTRA16x16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "I");
                    else if(IS_DIRECT(mb_type) && IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "d");
                    else if(IS_DIRECT(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "D");
                    else if(IS_GMC(mb_type) && IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "g");
                    else if(IS_GMC(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "G");
                    else if(IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "S");
                    else if(!USES_LIST(mb_type, 1))
                        av_log(s->avctx, AV_LOG_DEBUG, ">");
                    else if(!USES_LIST(mb_type, 0))
                        av_log(s->avctx, AV_LOG_DEBUG, "<");
                    else{
                        assert(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        av_log(s->avctx, AV_LOG_DEBUG, "X");
                    }
                    
                    //segmentation
                    if(IS_8X8(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "+");
                    else if(IS_16X8(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "-");
                    else if(IS_8X16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "¦");
                    else if(IS_INTRA(mb_type) || IS_16X16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, " ");
                    else
                        av_log(s->avctx, AV_LOG_DEBUG, "?");
                    
                        
                    if(IS_INTERLACED(mb_type) && s->codec_id == CODEC_ID_H264)
                        av_log(s->avctx, AV_LOG_DEBUG, "=");
                    else
                        av_log(s->avctx, AV_LOG_DEBUG, " ");
                }
//                av_log(s->avctx, AV_LOG_DEBUG, " ");
            }
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
    }

    if((s->avctx->debug&(FF_DEBUG_VIS_QP|FF_DEBUG_VIS_MB_TYPE)) || (s->avctx->debug_mv)){
        const int shift= 1 + s->quarter_sample;
        int mb_y;
        uint8_t *ptr;
        int i;
        int h_chroma_shift, v_chroma_shift;
        s->low_delay=0; //needed to see the vectors without trashing the buffers

        avcodec_get_chroma_sub_sample(s->avctx->pix_fmt, &h_chroma_shift, &v_chroma_shift);
        for(i=0; i<3; i++){
            memcpy(s->visualization_buffer[i], pict->data[i], (i==0) ? pict->linesize[i]*s->height:pict->linesize[i]*s->height >> v_chroma_shift);
            pict->data[i]= s->visualization_buffer[i];
        }
        pict->type= FF_BUFFER_TYPE_COPY;
        ptr= pict->data[0];

        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            int mb_x;
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                const int mb_index= mb_x + mb_y*s->mb_stride;
                if((s->avctx->debug_mv) && pict->motion_val){
                  int type;
                  for(type=0; type<3; type++){
                    int direction = 0;
                    switch (type) {
                      case 0: if ((!(s->avctx->debug_mv&FF_DEBUG_VIS_MV_P_FOR)) || (pict->pict_type!=FF_P_TYPE))
                                continue;
                              direction = 0;
                              break;
                      case 1: if ((!(s->avctx->debug_mv&FF_DEBUG_VIS_MV_B_FOR)) || (pict->pict_type!=FF_B_TYPE))
                                continue;
                              direction = 0;
                              break;
                      case 2: if ((!(s->avctx->debug_mv&FF_DEBUG_VIS_MV_B_BACK)) || (pict->pict_type!=FF_B_TYPE))
                                continue;
                              direction = 1;
                              break;
                    }
                    if(!USES_LIST(pict->mb_type[mb_index], direction))
                        continue;

                    //FIXME for h264
                    if(IS_8X8(pict->mb_type[mb_index])){
                      int i;
                      for(i=0; i<4; i++){
                        int sx= mb_x*16 + 4 + 8*(i&1);
                        int sy= mb_y*16 + 4 + 8*(i>>1);
                        int xy= mb_x*2 + (i&1) + (mb_y*2 + (i>>1))*s->b8_stride;
                        int mx= (pict->motion_val[direction][xy][0]>>shift) + sx;
                        int my= (pict->motion_val[direction][xy][1]>>shift) + sy;
                        draw_arrow(ptr, sx, sy, mx, my, s->width, s->height, s->linesize, 100);
                      }
                    }else if(IS_16X8(pict->mb_type[mb_index])){
                      int i;
                      for(i=0; i<2; i++){
                        int sx=mb_x*16 + 8;
                        int sy=mb_y*16 + 4 + 8*i;
                        int xy= mb_x*2 + (mb_y*2 + i)*s->b8_stride;
                        int mx=(pict->motion_val[direction][xy][0]>>shift);
                        int my=(pict->motion_val[direction][xy][1]>>shift);
                        
                        if(IS_INTERLACED(pict->mb_type[mb_index]))
                            my*=2;
                        
                        draw_arrow(ptr, sx, sy, mx+sx, my+sy, s->width, s->height, s->linesize, 100);
                      }
                    }else{
                      int sx= mb_x*16 + 8;
                      int sy= mb_y*16 + 8;
                      int xy= mb_x*2 + mb_y*2*s->b8_stride;
                      int mx= (pict->motion_val[direction][xy][0]>>shift) + sx;
                      int my= (pict->motion_val[direction][xy][1]>>shift) + sy;
                      draw_arrow(ptr, sx, sy, mx, my, s->width, s->height, s->linesize, 100);
                    }
                  }                  
                }
                if((s->avctx->debug&FF_DEBUG_VIS_QP) && pict->motion_val){
                    uint64_t c= (pict->qscale_table[mb_index]*128/31) * 0x0101010101010101ULL;
                    int y;
                    for(y=0; y<8; y++){
                        *(uint64_t*)(pict->data[1] + 8*mb_x + (8*mb_y + y)*pict->linesize[1])= c;
                        *(uint64_t*)(pict->data[2] + 8*mb_x + (8*mb_y + y)*pict->linesize[2])= c;
                    }
                }
                if((s->avctx->debug&FF_DEBUG_VIS_MB_TYPE) && pict->motion_val){
                    int mb_type= pict->mb_type[mb_index];
                    uint64_t u,v;
                    int y;
#define COLOR(theta, r)\
u= (int)(128 + r*cos(theta*3.141592/180));\
v= (int)(128 + r*sin(theta*3.141592/180));

                    
                    u=v=128;
                    if(IS_PCM(mb_type)){
                        COLOR(120,48)
                    }else if((IS_INTRA(mb_type) && IS_ACPRED(mb_type)) || IS_INTRA16x16(mb_type)){
                        COLOR(30,48)
                    }else if(IS_INTRA4x4(mb_type)){
                        COLOR(90,48)
                    }else if(IS_DIRECT(mb_type) && IS_SKIP(mb_type)){
//                        COLOR(120,48)
                    }else if(IS_DIRECT(mb_type)){
                        COLOR(150,48)
                    }else if(IS_GMC(mb_type) && IS_SKIP(mb_type)){
                        COLOR(170,48)
                    }else if(IS_GMC(mb_type)){
                        COLOR(190,48)
                    }else if(IS_SKIP(mb_type)){
//                        COLOR(180,48)
                    }else if(!USES_LIST(mb_type, 1)){
                        COLOR(240,48)
                    }else if(!USES_LIST(mb_type, 0)){
                        COLOR(0,48)
                    }else{
                        assert(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        COLOR(300,48)
                    }

                    u*= 0x0101010101010101ULL;
                    v*= 0x0101010101010101ULL;
                    for(y=0; y<8; y++){
                        *(uint64_t*)(pict->data[1] + 8*mb_x + (8*mb_y + y)*pict->linesize[1])= u;
                        *(uint64_t*)(pict->data[2] + 8*mb_x + (8*mb_y + y)*pict->linesize[2])= v;
                    }

                    //segmentation
                    if(IS_8X8(mb_type) || IS_16X8(mb_type)){
                        *(uint64_t*)(pict->data[0] + 16*mb_x + 0 + (16*mb_y + 8)*pict->linesize[0])^= 0x8080808080808080ULL;
                        *(uint64_t*)(pict->data[0] + 16*mb_x + 8 + (16*mb_y + 8)*pict->linesize[0])^= 0x8080808080808080ULL;
                    }
                    if(IS_8X8(mb_type) || IS_8X16(mb_type)){
                        for(y=0; y<16; y++)
                            pict->data[0][16*mb_x + 8 + (16*mb_y + y)*pict->linesize[0]]^= 0x80;
                    }
                        
                    if(IS_INTERLACED(mb_type) && s->codec_id == CODEC_ID_H264){
                        // hmm
                    }
                }
                s->mbskip_table[mb_index]=0;
            }
        }
    }
}

#ifdef CONFIG_ENCODERS

static int get_sae(uint8_t *src, int ref, int stride){
    int x,y;
    int acc=0;
    
    for(y=0; y<16; y++){
        for(x=0; x<16; x++){
            acc+= ABS(src[x+y*stride] - ref);
        }
    }
    
    return acc;
}

static int get_intra_count(MpegEncContext *s, uint8_t *src, uint8_t *ref, int stride){
    int x, y, w, h;
    int acc=0;
    
    w= s->width &~15;
    h= s->height&~15;
    
    for(y=0; y<h; y+=16){
        for(x=0; x<w; x+=16){
            int offset= x + y*stride;
            int sad = s->dsp.sad[0](NULL, src + offset, ref + offset, stride, 16);
            int mean= (s->dsp.pix_sum(src + offset, stride) + 128)>>8;
            int sae = get_sae(src + offset, mean, stride);
            
            acc+= sae + 500 < sad;
        }
    }
    return acc;
}


static int load_input_picture(MpegEncContext *s, AVFrame *pic_arg){
    AVFrame *pic=NULL;
    int i;
    const int encoding_delay= s->max_b_frames;
    int direct=1;
    
  if(pic_arg){
    if(encoding_delay && !(s->flags&CODEC_FLAG_INPUT_PRESERVED)) direct=0;
    if(pic_arg->linesize[0] != s->linesize) direct=0;
    if(pic_arg->linesize[1] != s->uvlinesize) direct=0;
    if(pic_arg->linesize[2] != s->uvlinesize) direct=0;
  
//    av_log(AV_LOG_DEBUG, "%d %d %d %d\n",pic_arg->linesize[0], pic_arg->linesize[1], s->linesize, s->uvlinesize);
    
    if(direct){
        i= ff_find_unused_picture(s, 1);

        pic= (AVFrame*)&s->picture[i];
        pic->reference= 3;
    
        for(i=0; i<4; i++){
            pic->data[i]= pic_arg->data[i];
            pic->linesize[i]= pic_arg->linesize[i];
        }
        alloc_picture(s, (Picture*)pic, 1);
    }else{
        int offset= 16;
        i= ff_find_unused_picture(s, 0);

        pic= (AVFrame*)&s->picture[i];
        pic->reference= 3;

        alloc_picture(s, (Picture*)pic, 0);

        if(   pic->data[0] + offset == pic_arg->data[0] 
           && pic->data[1] + offset == pic_arg->data[1]
           && pic->data[2] + offset == pic_arg->data[2]){
       // empty
        }else{
            int h_chroma_shift, v_chroma_shift;
            avcodec_get_chroma_sub_sample(s->avctx->pix_fmt, &h_chroma_shift, &v_chroma_shift);
        
            for(i=0; i<3; i++){
                int src_stride= pic_arg->linesize[i];
                int dst_stride= i ? s->uvlinesize : s->linesize;
                int h_shift= i ? h_chroma_shift : 0;
                int v_shift= i ? v_chroma_shift : 0;
                int w= s->width >>h_shift;
                int h= s->height>>v_shift;
                uint8_t *src= pic_arg->data[i];
                uint8_t *dst= pic->data[i] + offset;
            
                if(src_stride==dst_stride)
                    memcpy(dst, src, src_stride*h);
                else{
                    while(h--){
                        memcpy(dst, src, w);
                        dst += dst_stride;
                        src += src_stride;
                    }
                }
            }
        }
    }
    copy_picture_attributes(s, pic, pic_arg);
    
    pic->display_picture_number= s->input_picture_number++;
    if(pic->pts != AV_NOPTS_VALUE){ 
        s->user_specified_pts= pic->pts;
    }else{
        if(s->user_specified_pts){
            pic->pts= s->user_specified_pts + AV_TIME_BASE*(int64_t)s->avctx->frame_rate_base / s->avctx->frame_rate;
            av_log(s->avctx, AV_LOG_INFO, "Warning: AVFrame.pts=? trying to guess (%Ld)\n", pic->pts);
        }else{
            pic->pts= av_rescale(pic->display_picture_number*(int64_t)s->avctx->frame_rate_base, AV_TIME_BASE, s->avctx->frame_rate);
        }
    }
  }
  
    /* shift buffer entries */
    for(i=1; i<MAX_PICTURE_COUNT /*s->encoding_delay+1*/; i++)
        s->input_picture[i-1]= s->input_picture[i];
        
    s->input_picture[encoding_delay]= (Picture*)pic;

    return 0;
}

static void select_input_picture(MpegEncContext *s){
    int i;

    for(i=1; i<MAX_PICTURE_COUNT; i++)
        s->reordered_input_picture[i-1]= s->reordered_input_picture[i];
    s->reordered_input_picture[MAX_PICTURE_COUNT-1]= NULL;

    /* set next picture types & ordering */
    if(s->reordered_input_picture[0]==NULL && s->input_picture[0]){
        if(/*s->picture_in_gop_number >= s->gop_size ||*/ s->next_picture_ptr==NULL || s->intra_only){
            s->reordered_input_picture[0]= s->input_picture[0];
            s->reordered_input_picture[0]->pict_type= I_TYPE;
            s->reordered_input_picture[0]->coded_picture_number= s->coded_picture_number++;
        }else{
            int b_frames;
            
            if(s->flags&CODEC_FLAG_PASS2){
                for(i=0; i<s->max_b_frames+1; i++){
                    int pict_num= s->input_picture[0]->display_picture_number + i;
                    int pict_type= s->rc_context.entry[pict_num].new_pict_type;
                    s->input_picture[i]->pict_type= pict_type;
                    
                    if(i + 1 >= s->rc_context.num_entries) break;
                }
            }

            if(s->input_picture[0]->pict_type){
                /* user selected pict_type */
                for(b_frames=0; b_frames<s->max_b_frames+1; b_frames++){
                    if(s->input_picture[b_frames]->pict_type!=B_TYPE) break;
                }
            
                if(b_frames > s->max_b_frames){
                    av_log(s->avctx, AV_LOG_ERROR, "warning, too many bframes in a row\n");
                    b_frames = s->max_b_frames;
                }
            }else if(s->avctx->b_frame_strategy==0){
                b_frames= s->max_b_frames;
                while(b_frames && !s->input_picture[b_frames]) b_frames--;
            }else if(s->avctx->b_frame_strategy==1){
                for(i=1; i<s->max_b_frames+1; i++){
                    if(s->input_picture[i] && s->input_picture[i]->b_frame_score==0){
                        s->input_picture[i]->b_frame_score= 
                            get_intra_count(s, s->input_picture[i  ]->data[0], 
                                               s->input_picture[i-1]->data[0], s->linesize) + 1;
                    }
                }
                for(i=0; i<s->max_b_frames; i++){
                    if(s->input_picture[i]==NULL || s->input_picture[i]->b_frame_score - 1 > s->mb_num/40) break;
                }
                                
                b_frames= FFMAX(0, i-1);
                
                /* reset scores */
                for(i=0; i<b_frames+1; i++){
                    s->input_picture[i]->b_frame_score=0;
                }
            }else{
                av_log(s->avctx, AV_LOG_ERROR, "illegal b frame strategy\n");
                b_frames=0;
            }

            emms_c();
//static int b_count=0;
//b_count+= b_frames;
//av_log(s->avctx, AV_LOG_DEBUG, "b_frames: %d\n", b_count);
            if(s->picture_in_gop_number + b_frames >= s->gop_size){
                if(s->flags & CODEC_FLAG_CLOSED_GOP)
                    b_frames=0;
                s->input_picture[b_frames]->pict_type= I_TYPE;
            }
            
            if(   (s->flags & CODEC_FLAG_CLOSED_GOP)
               && b_frames
               && s->input_picture[b_frames]->pict_type== I_TYPE)
                b_frames--;

            s->reordered_input_picture[0]= s->input_picture[b_frames];
            if(s->reordered_input_picture[0]->pict_type != I_TYPE)
                s->reordered_input_picture[0]->pict_type= P_TYPE;
            s->reordered_input_picture[0]->coded_picture_number= s->coded_picture_number++;
            for(i=0; i<b_frames; i++){
                s->reordered_input_picture[i+1]= s->input_picture[i];
                s->reordered_input_picture[i+1]->pict_type= B_TYPE;
                s->reordered_input_picture[i+1]->coded_picture_number= s->coded_picture_number++;
            }
        }
    }
    
    if(s->reordered_input_picture[0]){
        s->reordered_input_picture[0]->reference= s->reordered_input_picture[0]->pict_type!=B_TYPE ? 3 : 0;

        copy_picture(&s->new_picture, s->reordered_input_picture[0]);

        if(s->reordered_input_picture[0]->type == FF_BUFFER_TYPE_SHARED){
            // input is a shared pix, so we cant modifiy it -> alloc a new one & ensure that the shared one is reuseable
        
            int i= ff_find_unused_picture(s, 0);
            Picture *pic= &s->picture[i];

            /* mark us unused / free shared pic */
            for(i=0; i<4; i++)
                s->reordered_input_picture[0]->data[i]= NULL;
            s->reordered_input_picture[0]->type= 0;
            
            pic->reference              = s->reordered_input_picture[0]->reference;
            
            alloc_picture(s, pic, 0);

            copy_picture_attributes(s, (AVFrame*)pic, (AVFrame*)s->reordered_input_picture[0]);

            s->current_picture_ptr= pic;
        }else{
            // input is not a shared pix -> reuse buffer for current_pix

            assert(   s->reordered_input_picture[0]->type==FF_BUFFER_TYPE_USER 
                   || s->reordered_input_picture[0]->type==FF_BUFFER_TYPE_INTERNAL);
            
            s->current_picture_ptr= s->reordered_input_picture[0];
            for(i=0; i<4; i++){
                s->new_picture.data[i]+=16;
            }
        }
        copy_picture(&s->current_picture, s->current_picture_ptr);
    
        s->picture_number= s->new_picture.display_picture_number;
//printf("dpn:%d\n", s->picture_number);
    }else{
       memset(&s->new_picture, 0, sizeof(Picture));
    }
}

int MPV_encode_picture(AVCodecContext *avctx,
                       unsigned char *buf, int buf_size, void *data)
{
    MpegEncContext *s = avctx->priv_data;
    AVFrame *pic_arg = data;
    int i, stuffing_count;

    if(avctx->pix_fmt != PIX_FMT_YUV420P){
        av_log(avctx, AV_LOG_ERROR, "this codec supports only YUV420P\n");
        return -1;
    }
    
    for(i=0; i<avctx->thread_count; i++){
        int start_y= s->thread_context[i]->start_mb_y;
        int   end_y= s->thread_context[i]->  end_mb_y;
        int h= s->mb_height;
        uint8_t *start= buf + buf_size*start_y/h;
        uint8_t *end  = buf + buf_size*  end_y/h;

        init_put_bits(&s->thread_context[i]->pb, start, end - start);
    }

    s->picture_in_gop_number++;

    load_input_picture(s, pic_arg);
    
    select_input_picture(s);
    
    /* output? */
    if(s->new_picture.data[0]){
        s->pict_type= s->new_picture.pict_type;
//emms_c();
//printf("qs:%f %f %d\n", s->new_picture.quality, s->current_picture.quality, s->qscale);
        MPV_frame_start(s, avctx);

        encode_picture(s, s->picture_number);
        
        avctx->real_pict_num  = s->picture_number;
        avctx->header_bits = s->header_bits;
        avctx->mv_bits     = s->mv_bits;
        avctx->misc_bits   = s->misc_bits;
        avctx->i_tex_bits  = s->i_tex_bits;
        avctx->p_tex_bits  = s->p_tex_bits;
        avctx->i_count     = s->i_count;
        avctx->p_count     = s->mb_num - s->i_count - s->skip_count; //FIXME f/b_count in avctx
        avctx->skip_count  = s->skip_count;

        MPV_frame_end(s);

        if (s->out_format == FMT_MJPEG)
            mjpeg_picture_trailer(s);
        
        if(s->flags&CODEC_FLAG_PASS1)
            ff_write_pass1_stats(s);

        for(i=0; i<4; i++){
            avctx->error[i] += s->current_picture_ptr->error[i];
        }

        flush_put_bits(&s->pb);
        s->frame_bits  = put_bits_count(&s->pb);

        stuffing_count= ff_vbv_update(s, s->frame_bits);
        if(stuffing_count){
            switch(s->codec_id){
            case CODEC_ID_MPEG1VIDEO:
            case CODEC_ID_MPEG2VIDEO:
                while(stuffing_count--){
                    put_bits(&s->pb, 8, 0);
                }
            break;
            case CODEC_ID_MPEG4:
                put_bits(&s->pb, 16, 0);
                put_bits(&s->pb, 16, 0x1C3);
                stuffing_count -= 4;
                while(stuffing_count--){
                    put_bits(&s->pb, 8, 0xFF);
                }
            break;
            default:
                av_log(s->avctx, AV_LOG_ERROR, "vbv buffer overflow\n");
            }
            flush_put_bits(&s->pb);
            s->frame_bits  = put_bits_count(&s->pb);
        }

        /* update mpeg1/2 vbv_delay for CBR */    
        if(s->avctx->rc_max_rate && s->avctx->rc_min_rate == s->avctx->rc_max_rate && s->out_format == FMT_MPEG1
           && 90000LL * (avctx->rc_buffer_size-1) <= s->avctx->rc_max_rate*0xFFFFLL){
            int vbv_delay;

            assert(s->repeat_first_field==0);
            
            vbv_delay= lrintf(90000 * s->rc_context.buffer_index / s->avctx->rc_max_rate);
            assert(vbv_delay < 0xFFFF);

            s->vbv_delay_ptr[0] &= 0xF8;
            s->vbv_delay_ptr[0] |= vbv_delay>>13;
            s->vbv_delay_ptr[1]  = vbv_delay>>5;
            s->vbv_delay_ptr[2] &= 0x07;
            s->vbv_delay_ptr[2] |= vbv_delay<<3;
        }
        s->total_bits += s->frame_bits;
        avctx->frame_bits  = s->frame_bits;
    }else{
        assert((pbBufPtr(&s->pb) == s->pb.buf));
        s->frame_bits=0;
    }
    assert((s->frame_bits&7)==0);
    
    return s->frame_bits/8;
}

#endif //CONFIG_ENCODERS

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
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        motion_x =0;
    src_y = clip(src_y, -16, s->height);
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
    
    if(s->flags&CODEC_FLAG_GRAY) return;

    motion_x= s->sprite_offset[1][0];
    motion_y= s->sprite_offset[1][1];
    src_x = s->mb_x * 8 + (motion_x >> (s->sprite_warping_accuracy+1));
    src_y = s->mb_y * 8 + (motion_y >> (s->sprite_warping_accuracy+1));
    motion_x<<=(3-s->sprite_warping_accuracy);
    motion_y<<=(3-s->sprite_warping_accuracy);
    src_x = clip(src_x, -8, s->width>>1);
    if (src_x == s->width>>1)
        motion_x =0;
    src_y = clip(src_y, -8, s->height>>1);
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

    if(s->flags&CODEC_FLAG_GRAY) return;

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

/**
 * Copies a rectangular area of samples to a temporary buffer and replicates the boarder samples.
 * @param buf destination buffer
 * @param src source buffer
 * @param linesize number of bytes between 2 vertically adjacent samples in both the source and destination buffers
 * @param block_w width of block
 * @param block_h height of block
 * @param src_x x coordinate of the top left sample of the block in the source buffer
 * @param src_y y coordinate of the top left sample of the block in the source buffer
 * @param w width of the source buffer
 * @param h height of the source buffer
 */
void ff_emulated_edge_mc(uint8_t *buf, uint8_t *src, int linesize, int block_w, int block_h, 
                                    int src_x, int src_y, int w, int h){
    int x, y;
    int start_y, start_x, end_y, end_x;

    if(src_y>= h){
        src+= (h-1-src_y)*linesize;
        src_y=h-1;
    }else if(src_y<=-block_h){
        src+= (1-block_h-src_y)*linesize;
        src_y=1-block_h;
    }
    if(src_x>= w){
        src+= (w-1-src_x);
        src_x=w-1;
    }else if(src_x<=-block_w){
        src+= (1-block_w-src_x);
        src_x=1-block_w;
    }

    start_y= FFMAX(0, -src_y);
    start_x= FFMAX(0, -src_x);
    end_y= FFMIN(block_h, h-src_y);
    end_x= FFMIN(block_w, w-src_x);

    // copy existing part
    for(y=start_y; y<end_y; y++){
        for(x=start_x; x<end_x; x++){
            buf[x + y*linesize]= src[x + y*linesize];
        }
    }

    //top
    for(y=0; y<start_y; y++){
        for(x=start_x; x<end_x; x++){
            buf[x + y*linesize]= buf[x + start_y*linesize];
        }
    }

    //bottom
    for(y=end_y; y<block_h; y++){
        for(x=start_x; x<end_x; x++){
            buf[x + y*linesize]= buf[x + (end_y-1)*linesize];
        }
    }
                                    
    for(y=0; y<block_h; y++){
       //left
        for(x=0; x<start_x; x++){
            buf[x + y*linesize]= buf[start_x + y*linesize];
        }
       
       //right
        for(x=end_x; x<block_w; x++){
            buf[x + y*linesize]= buf[end_x - 1 + y*linesize];
        }
    }
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
    src_x = clip(src_x, -16, width); //FIXME unneeded for emu?
    if (src_x == width)
        dxy &= ~1;
    src_y = clip(src_y, -16, height);
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

/* apply one mpeg motion vector to the three components */
static always_inline void mpeg_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int field_based, int bottom_field, int field_select,
                               uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                               int motion_x, int motion_y, int h)
{
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int dxy, uvdxy, mx, my, src_x, src_y, uvsrc_x, uvsrc_y, v_edge_pos, uvlinesize, linesize;
    
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
    src_y =(s->mb_y<<(4-field_based)) + (motion_y >> 1);

    if (s->out_format == FMT_H263) {
        if((s->workaround_bugs & FF_BUG_HPEL_CHROMA) && field_based){
            mx = (motion_x>>1)|(motion_x&1);
            my = motion_y >>1;
            uvdxy = ((my & 1) << 1) | (mx & 1);
            uvsrc_x = s->mb_x* 8               + (mx >> 1);
            uvsrc_y = (s->mb_y<<(3-field_based)) + (my >> 1);
        }else{
            uvdxy = dxy | (motion_y & 2) | ((motion_x & 2) >> 1);
            uvsrc_x = src_x>>1;
            uvsrc_y = src_y>>1;
        }
    }else if(s->out_format == FMT_H261){//even chroma mv's are full pel in H261
        mx = motion_x / 4;
        my = motion_y / 4;
        uvdxy = 0;
        uvsrc_x = s->mb_x*8 + mx;
        uvsrc_y = s->mb_y*8 + my;
    } else {
        if(s->chroma_y_shift){
            mx = motion_x / 2;
            my = motion_y / 2;
            uvdxy = ((my & 1) << 1) | (mx & 1);
            uvsrc_x = s->mb_x* 8               + (mx >> 1);
            uvsrc_y = (s->mb_y<<(3-field_based)) + (my >> 1);
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
            if(s->codec_id == CODEC_ID_MPEG2VIDEO ||
               s->codec_id == CODEC_ID_MPEG1VIDEO){
                av_log(s->avctx,AV_LOG_DEBUG,"MPEG motion vector out of boundary\n");
                return ;
            }
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr_y, s->linesize, 17, 17+field_based,
                             src_x, src_y<<field_based, s->h_edge_pos, s->v_edge_pos);
            ptr_y = s->edge_emu_buffer;
            if(!(s->flags&CODEC_FLAG_GRAY)){
                uint8_t *uvbuf= s->edge_emu_buffer+18*s->linesize;
                ff_emulated_edge_mc(uvbuf  , ptr_cb, s->uvlinesize, 9, 9+field_based, 
                                 uvsrc_x, uvsrc_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
                ff_emulated_edge_mc(uvbuf+16, ptr_cr, s->uvlinesize, 9, 9+field_based, 
                                 uvsrc_x, uvsrc_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
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
    
    if(!(s->flags&CODEC_FLAG_GRAY)){
        pix_op[s->chroma_x_shift][uvdxy](dest_cb, ptr_cb, uvlinesize, h >> s->chroma_y_shift);
        pix_op[s->chroma_x_shift][uvdxy](dest_cr, ptr_cr, uvlinesize, h >> s->chroma_y_shift);
    }
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
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr_y, s->linesize, 17, 17+field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos, s->v_edge_pos);
        ptr_y= s->edge_emu_buffer;
        if(!(s->flags&CODEC_FLAG_GRAY)){
            uint8_t *uvbuf= s->edge_emu_buffer + 18*s->linesize;
            ff_emulated_edge_mc(uvbuf, ptr_cb, s->uvlinesize, 9, 9 + field_based, 
                             uvsrc_x, uvsrc_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ff_emulated_edge_mc(uvbuf + 16, ptr_cr, s->uvlinesize, 9, 9 + field_based, 
                             uvsrc_x, uvsrc_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
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
    if(!(s->flags&CODEC_FLAG_GRAY)){
        pix_op[1][uvdxy](dest_cr, ptr_cr, uvlinesize, h >> 1);
        pix_op[1][uvdxy](dest_cb, ptr_cb, uvlinesize, h >> 1);
    }
}

inline int ff_h263_round_chroma(int x){
    if (x >= 0)
        return  (h263_chroma_roundtab[x & 0xf] + ((x >> 3) & ~1));
    else {
        x = -x;
        return -(h263_chroma_roundtab[x & 0xf] + ((x >> 3) & ~1));
    }
}

/**
 * h263 chorma 4mv motion compensation.
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
    src_x = clip(src_x, -8, s->width/2);
    if (src_x == s->width/2)
        dxy &= ~1;
    src_y = clip(src_y, -8, s->height/2);
    if (src_y == s->height/2)
        dxy &= ~2;
    
    offset = (src_y * (s->uvlinesize)) + src_x;
    ptr = ref_picture[1] + offset;
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(   (unsigned)src_x > (s->h_edge_pos>>1) - (dxy &1) - 8
           || (unsigned)src_y > (s->v_edge_pos>>1) - (dxy>>1) - 8){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
    }
    pix_op[dxy](dest_cb, ptr, s->uvlinesize, 8);

    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    pix_op[dxy](dest_cr, ptr, s->uvlinesize, 8);
}

/**
 * motion compesation of a single macroblock
 * @param s context
 * @param dest_y luma destination pointer
 * @param dest_cb chroma cb/u destination pointer
 * @param dest_cr chroma cr/v destination pointer
 * @param dir direction (0->forward, 1->backward)
 * @param ref_picture array[3] of pointers to the 3 planes of the reference picture
 * @param pic_op halfpel motion compensation function (average or put normally)
 * @param pic_op qpel motion compensation function (average or put normally)
 * the motion vectors are taken from s->mv and the MV type from s->mv_type
 */
static inline void MPV_motion(MpegEncContext *s, 
                              uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                              int dir, uint8_t **ref_picture, 
                              op_pixels_func (*pix_op)[4], qpel_mc_func (*qpix_op)[16])
{
    int dxy, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    uint8_t *ptr, *dest;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    if(s->obmc && s->pict_type != B_TYPE){
        int16_t mv_cache[4][4][2];
        const int xy= s->mb_x + s->mb_y*s->mb_stride;
        const int mot_stride= s->b8_stride;
        const int mot_xy= mb_x*2 + mb_y*2*mot_stride;

        assert(!s->mb_skiped);
                
        memcpy(mv_cache[1][1], s->current_picture.motion_val[0][mot_xy           ], sizeof(int16_t)*4);
        memcpy(mv_cache[2][1], s->current_picture.motion_val[0][mot_xy+mot_stride], sizeof(int16_t)*4);
        memcpy(mv_cache[3][1], s->current_picture.motion_val[0][mot_xy+mot_stride], sizeof(int16_t)*4);

        if(mb_y==0 || IS_INTRA(s->current_picture.mb_type[xy-s->mb_stride])){
            memcpy(mv_cache[0][1], mv_cache[1][1], sizeof(int16_t)*4);
        }else{
            memcpy(mv_cache[0][1], s->current_picture.motion_val[0][mot_xy-mot_stride], sizeof(int16_t)*4);
        }

        if(mb_x==0 || IS_INTRA(s->current_picture.mb_type[xy-1])){
            *(int32_t*)mv_cache[1][0]= *(int32_t*)mv_cache[1][1];
            *(int32_t*)mv_cache[2][0]= *(int32_t*)mv_cache[2][1];
        }else{
            *(int32_t*)mv_cache[1][0]= *(int32_t*)s->current_picture.motion_val[0][mot_xy-1];
            *(int32_t*)mv_cache[2][0]= *(int32_t*)s->current_picture.motion_val[0][mot_xy-1+mot_stride];
        }

        if(mb_x+1>=s->mb_width || IS_INTRA(s->current_picture.mb_type[xy+1])){
            *(int32_t*)mv_cache[1][3]= *(int32_t*)mv_cache[1][2];
            *(int32_t*)mv_cache[2][3]= *(int32_t*)mv_cache[2][2];
        }else{
            *(int32_t*)mv_cache[1][3]= *(int32_t*)s->current_picture.motion_val[0][mot_xy+2];
            *(int32_t*)mv_cache[2][3]= *(int32_t*)s->current_picture.motion_val[0][mot_xy+2+mot_stride];
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
        if(!(s->flags&CODEC_FLAG_GRAY))
            chroma_4mv_motion(s, dest_cb, dest_cr, ref_picture, pix_op[1], mx, my);

        return;
    }
   
    switch(s->mv_type) {
    case MV_TYPE_16X16:
#ifdef CONFIG_RISKY
        if(s->mcsel){
            if(s->real_sprite_warping_points==1){
                gmc1_motion(s, dest_y, dest_cb, dest_cr,
                            ref_picture);
            }else{
                gmc_motion(s, dest_y, dest_cb, dest_cr,
                            ref_picture);
            }
        }else if(s->quarter_sample){
            qpel_motion(s, dest_y, dest_cb, dest_cr, 
                        0, 0, 0,
                        ref_picture, pix_op, qpix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else if(s->mspel){
            ff_mspel_motion(s, dest_y, dest_cb, dest_cr,
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else
#endif
        {
            mpeg_motion(s, dest_y, dest_cb, dest_cr, 
                        0, 0, 0,
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }           
        break;
    case MV_TYPE_8X8:
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
                src_x = clip(src_x, -16, s->width);
                if (src_x == s->width)
                    dxy &= ~3;
                src_y = clip(src_y, -16, s->height);
                if (src_y == s->height)
                    dxy &= ~12;
                    
                ptr = ref_picture[0] + (src_y * s->linesize) + (src_x);
                if(s->flags&CODEC_FLAG_EMU_EDGE){
                    if(   (unsigned)src_x > s->h_edge_pos - (motion_x&3) - 8 
                       || (unsigned)src_y > s->v_edge_pos - (motion_y&3) - 8 ){
                        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->linesize, 9, 9, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
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

        if(!(s->flags&CODEC_FLAG_GRAY))
            chroma_4mv_motion(s, dest_cb, dest_cr, ref_picture, pix_op[1], mx, my);
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            if(s->quarter_sample){
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
                            s->mv[dir][0][0], s->mv[dir][0][1], 8);
                /* bottom field */
                mpeg_motion(s, dest_y, dest_cb, dest_cr,
                            1, 1, s->field_select[dir][1],
                            ref_picture, pix_op,
                            s->mv[dir][1][0], s->mv[dir][1][1], 8);
            }
        } else {
            if(s->picture_structure != s->field_select[dir][0] + 1 && s->pict_type != B_TYPE && !s->first_field){
                ref_picture= s->current_picture_ptr->data;
            } 

            mpeg_motion(s, dest_y, dest_cb, dest_cr,
                        0, 0, s->field_select[dir][0],
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }
        break;
    case MV_TYPE_16X8:
        for(i=0; i<2; i++){
            uint8_t ** ref2picture;

            if(s->picture_structure == s->field_select[dir][i] + 1 || s->pict_type == B_TYPE || s->first_field){
                ref2picture= ref_picture;
            }else{
                ref2picture= s->current_picture_ptr->data;
            } 

            mpeg_motion(s, dest_y, dest_cb, dest_cr, 
                        0, 0, s->field_select[dir][i],
                        ref2picture, pix_op,
                        s->mv[dir][i][0], s->mv[dir][i][1] + 16*i, 8);
                
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
                                s->mv[dir][2*i + j][0], s->mv[dir][2*i + j][1], 8);
                }
                pix_op = s->dsp.avg_pixels_tab; 
            }
        }else{
            for(i=0; i<2; i++){
                mpeg_motion(s, dest_y, dest_cb, dest_cr, 
                            0, 0, s->picture_structure != i+1,
                            ref_picture, pix_op,
                            s->mv[dir][2*i][0],s->mv[dir][2*i][1],16);

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


/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, uint8_t *dest, int line_size, int qscale)
{
    s->dct_unquantize_intra(s, block, i, qscale);
    s->dsp.idct_put (dest, line_size, block);
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, uint8_t *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->dsp.idct_add (dest, line_size, block);
    }
}

static inline void add_dequant_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize_inter(s, block, i, qscale);

        s->dsp.idct_add (dest, line_size, block);
    }
}

/**
 * cleans dc, ac, coded_block for the current non intra MB
 */
void ff_clean_intra_table_entries(MpegEncContext *s)
{
    int wrap = s->b8_stride;
    int xy = s->block_index[0];
    
    s->dc_val[0][xy           ] = 
    s->dc_val[0][xy + 1       ] = 
    s->dc_val[0][xy     + wrap] =
    s->dc_val[0][xy + 1 + wrap] = 1024;
    /* ac pred */
    memset(s->ac_val[0][xy       ], 0, 32 * sizeof(int16_t));
    memset(s->ac_val[0][xy + wrap], 0, 32 * sizeof(int16_t));
    if (s->msmpeg4_version>=3) {
        s->coded_block[xy           ] =
        s->coded_block[xy + 1       ] =
        s->coded_block[xy     + wrap] =
        s->coded_block[xy + 1 + wrap] = 0;
    }
    /* chroma */
    wrap = s->mb_stride;
    xy = s->mb_x + s->mb_y * wrap;
    s->dc_val[1][xy] =
    s->dc_val[2][xy] = 1024;
    /* ac pred */
    memset(s->ac_val[1][xy], 0, 16 * sizeof(int16_t));
    memset(s->ac_val[2][xy], 0, 16 * sizeof(int16_t));
    
    s->mbintra_table[xy]= 0;
}

/* generic function called after a macroblock has been parsed by the
   decoder or after it has been encoded by the encoder.

   Important variables used:
   s->mb_intra : true if intra macroblock
   s->mv_dir   : motion vector direction
   s->mv_type  : motion vector type
   s->mv       : motion vector
   s->interlaced_dct : true if interlaced dct used (mpeg2)
 */
void MPV_decode_mb(MpegEncContext *s, DCTELEM block[12][64])
{
    int mb_x, mb_y;
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;
#ifdef HAVE_XVMC
    if(s->avctx->xvmc_acceleration){
        XVMC_decode_mb(s);//xvmc uses pblocks
        return;
    }
#endif

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    if(s->avctx->debug&FF_DEBUG_DCT_COEFF) {
       /* save DCT coefficients */
       int i,j;
       DCTELEM *dct = &s->current_picture.dct_coeff[mb_xy*64*6];
       for(i=0; i<6; i++)
           for(j=0; j<64; j++)
               *dct++ = block[i][s->dsp.idct_permutation[j]];
    }

    s->current_picture.qscale_table[mb_xy]= s->qscale;

    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        if (s->h263_pred || s->h263_aic) {
            if(s->mbintra_table[mb_xy])
                ff_clean_intra_table_entries(s);
        } else {
            s->last_dc[0] =
            s->last_dc[1] =
            s->last_dc[2] = 128 << s->intra_dc_precision;
        }
    }
    else if (s->h263_pred || s->h263_aic)
        s->mbintra_table[mb_xy]=1;

    if ((s->flags&CODEC_FLAG_PSNR) || !(s->encoding && (s->intra_only || s->pict_type==B_TYPE))) { //FIXME precalc
        uint8_t *dest_y, *dest_cb, *dest_cr;
        int dct_linesize, dct_offset;
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        const int linesize= s->current_picture.linesize[0]; //not s->linesize as this woulnd be wrong for field pics
        const int uvlinesize= s->current_picture.linesize[1];
        const int readable= s->pict_type != B_TYPE || s->encoding || s->avctx->draw_horiz_band;

        /* avoid copy if macroblock skipped in last frame too */
        /* skip only during decoding as we might trash the buffers during encoding a bit */
        if(!s->encoding){
            uint8_t *mbskip_ptr = &s->mbskip_table[mb_xy];
            const int age= s->current_picture.age;

            assert(age);

            if (s->mb_skiped) {
                s->mb_skiped= 0;
                assert(s->pict_type!=I_TYPE);
 
                (*mbskip_ptr) ++; /* indicate that this time we skiped it */
                if(*mbskip_ptr >99) *mbskip_ptr= 99;

                /* if previous was skipped too, then nothing to do !  */
                if (*mbskip_ptr >= age && s->current_picture.reference){
                    return;
                }
            } else if(!s->current_picture.reference){
                (*mbskip_ptr) ++; /* increase counter so the age can be compared cleanly */
                if(*mbskip_ptr >99) *mbskip_ptr= 99;
            } else{
                *mbskip_ptr = 0; /* not skipped */
            }
        }

        dct_linesize = linesize << s->interlaced_dct;
        dct_offset =(s->interlaced_dct)? linesize : linesize*8;

        if(readable){
            dest_y=  s->dest[0];
            dest_cb= s->dest[1];
            dest_cr= s->dest[2];
        }else{
            dest_y = s->b_scratchpad;
            dest_cb= s->b_scratchpad+16*linesize;
            dest_cr= s->b_scratchpad+32*linesize;
        }
        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was allready done otherwise) */
            if(!s->encoding){
                if ((!s->no_rounding) || s->pict_type==B_TYPE){                
		    op_pix = s->dsp.put_pixels_tab;
                    op_qpix= s->dsp.put_qpel_pixels_tab;
                }else{
                    op_pix = s->dsp.put_no_rnd_pixels_tab;
                    op_qpix= s->dsp.put_no_rnd_qpel_pixels_tab;
                }

                if (s->mv_dir & MV_DIR_FORWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture.data, op_pix, op_qpix);
		    op_pix = s->dsp.avg_pixels_tab;
                    op_qpix= s->dsp.avg_qpel_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture.data, op_pix, op_qpix);
                }
            }

            /* skip dequant / idct if we are really late ;) */
            if(s->hurry_up>1) return;

            /* add dct residue */
            if(s->encoding || !(   s->h263_msmpeg4 || s->codec_id==CODEC_ID_MPEG1VIDEO || s->codec_id==CODEC_ID_MPEG2VIDEO
                                || (s->codec_id==CODEC_ID_MPEG4 && !s->mpeg_quant))){
                add_dequant_dct(s, block[0], 0, dest_y, dct_linesize, s->qscale);
                add_dequant_dct(s, block[1], 1, dest_y + 8, dct_linesize, s->qscale);
                add_dequant_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize, s->qscale);
                add_dequant_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize, s->qscale);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                    add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                }
            } else if(s->codec_id != CODEC_ID_WMV2){
                add_dct(s, block[0], 0, dest_y, dct_linesize);
                add_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                add_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                add_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    if(s->chroma_y_shift){//Chroma420
                        add_dct(s, block[4], 4, dest_cb, uvlinesize);
                        add_dct(s, block[5], 5, dest_cr, uvlinesize);
                    }else{
                        //chroma422
                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset =(s->interlaced_dct)? uvlinesize : uvlinesize*8;

                        add_dct(s, block[4], 4, dest_cb, dct_linesize);
                        add_dct(s, block[5], 5, dest_cr, dct_linesize);
                        add_dct(s, block[6], 6, dest_cb+dct_offset, dct_linesize);
                        add_dct(s, block[7], 7, dest_cr+dct_offset, dct_linesize);
                        if(!s->chroma_x_shift){//Chroma444
                            add_dct(s, block[8], 8, dest_cb+8, dct_linesize);
                            add_dct(s, block[9], 9, dest_cr+8, dct_linesize);
                            add_dct(s, block[10], 10, dest_cb+8+dct_offset, dct_linesize);
                            add_dct(s, block[11], 11, dest_cr+8+dct_offset, dct_linesize);
                        }
                    }
                }//fi gray
            }
#ifdef CONFIG_RISKY
            else{
                ff_wmv2_add_mb(s, block, dest_y, dest_cb, dest_cr);
            }
#endif
        } else {
            /* dct only in intra block */
            if(s->encoding || !(s->codec_id==CODEC_ID_MPEG1VIDEO || s->codec_id==CODEC_ID_MPEG2VIDEO)){
                put_dct(s, block[0], 0, dest_y, dct_linesize, s->qscale);
                put_dct(s, block[1], 1, dest_y + 8, dct_linesize, s->qscale);
                put_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize, s->qscale);
                put_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize, s->qscale);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    put_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                    put_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                }
            }else{
                s->dsp.idct_put(dest_y                 , dct_linesize, block[0]);
                s->dsp.idct_put(dest_y              + 8, dct_linesize, block[1]);
                s->dsp.idct_put(dest_y + dct_offset    , dct_linesize, block[2]);
                s->dsp.idct_put(dest_y + dct_offset + 8, dct_linesize, block[3]);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    if(s->chroma_y_shift){
                        s->dsp.idct_put(dest_cb, uvlinesize, block[4]);
                        s->dsp.idct_put(dest_cr, uvlinesize, block[5]);
                    }else{

                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset =(s->interlaced_dct)? uvlinesize : uvlinesize*8;

                        s->dsp.idct_put(dest_cb,              dct_linesize, block[4]);
                        s->dsp.idct_put(dest_cr,              dct_linesize, block[5]);
                        s->dsp.idct_put(dest_cb + dct_offset, dct_linesize, block[6]);
                        s->dsp.idct_put(dest_cr + dct_offset, dct_linesize, block[7]);
                        if(!s->chroma_x_shift){//Chroma444
                            s->dsp.idct_put(dest_cb + 8,              dct_linesize, block[8]);
                            s->dsp.idct_put(dest_cr + 8,              dct_linesize, block[9]);
                            s->dsp.idct_put(dest_cb + 8 + dct_offset, dct_linesize, block[10]);
                            s->dsp.idct_put(dest_cr + 8 + dct_offset, dct_linesize, block[11]);
                        }
                    }
                }//gray
            }
        }
        if(!readable){
            s->dsp.put_pixels_tab[0][0](s->dest[0], dest_y ,   linesize,16);
            s->dsp.put_pixels_tab[s->chroma_x_shift][0](s->dest[1], dest_cb, uvlinesize,16 >> s->chroma_y_shift);
            s->dsp.put_pixels_tab[s->chroma_x_shift][0](s->dest[2], dest_cr, uvlinesize,16 >> s->chroma_y_shift);
        }
    }
}

#ifdef CONFIG_ENCODERS

static inline void dct_single_coeff_elimination(MpegEncContext *s, int n, int threshold)
{
    static const char tab[64]=
        {3,2,2,1,1,1,1,1,
         1,1,1,1,1,1,1,1,
         1,1,1,1,1,1,1,1,
         0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0};
    int score=0;
    int run=0;
    int i;
    DCTELEM *block= s->block[n];
    const int last_index= s->block_last_index[n];
    int skip_dc;

    if(threshold<0){
        skip_dc=0;
        threshold= -threshold;
    }else
        skip_dc=1;

    /* are all which we could set to zero are allready zero? */
    if(last_index<=skip_dc - 1) return;

    for(i=0; i<=last_index; i++){
        const int j = s->intra_scantable.permutated[i];
        const int level = ABS(block[j]);
        if(level==1){
            if(skip_dc && i==0) continue;
            score+= tab[run];
            run=0;
        }else if(level>1){
            return;
        }else{
            run++;
        }
    }
    if(score >= threshold) return;
    for(i=skip_dc; i<=last_index; i++){
        const int j = s->intra_scantable.permutated[i];
        block[j]=0;
    }
    if(block[0]) s->block_last_index[n]= 0;
    else         s->block_last_index[n]= -1;
}

static inline void clip_coeffs(MpegEncContext *s, DCTELEM *block, int last_index)
{
    int i;
    const int maxlevel= s->max_qcoeff;
    const int minlevel= s->min_qcoeff;
    int overflow=0;
    
    if(s->mb_intra){
        i=1; //skip clipping of intra dc
    }else
        i=0;
    
    for(;i<=last_index; i++){
        const int j= s->intra_scantable.permutated[i];
        int level = block[j];
       
        if     (level>maxlevel){
            level=maxlevel;
            overflow++;
        }else if(level<minlevel){
            level=minlevel;
            overflow++;
        }
        
        block[j]= level;
    }
    
    if(overflow && s->avctx->mb_decision == FF_MB_DECISION_SIMPLE)
        av_log(s->avctx, AV_LOG_INFO, "warning, cliping %d dct coefficents to %d..%d\n", overflow, minlevel, maxlevel);
}

#endif //CONFIG_ENCODERS

/**
 *
 * @param h is the normal height, this will be reduced automatically if needed for the last row
 */
void ff_draw_horiz_band(MpegEncContext *s, int y, int h){
    if (s->avctx->draw_horiz_band) {
        AVFrame *src;
        int offset[4];
        
        if(s->picture_structure != PICT_FRAME){
            h <<= 1;
            y <<= 1;
            if(s->first_field  && !(s->avctx->slice_flags&SLICE_FLAG_ALLOW_FIELD)) return;
        }

        h= FFMIN(h, s->height - y);

        if(s->pict_type==B_TYPE || s->low_delay || (s->avctx->slice_flags&SLICE_FLAG_CODED_ORDER)) 
            src= (AVFrame*)s->current_picture_ptr;
        else if(s->last_picture_ptr)
            src= (AVFrame*)s->last_picture_ptr;
        else
            return;
            
        if(s->pict_type==B_TYPE && s->picture_structure == PICT_FRAME && s->out_format != FMT_H264){
            offset[0]=
            offset[1]=
            offset[2]=
            offset[3]= 0;
        }else{
            offset[0]= y * s->linesize;;
            offset[1]= 
            offset[2]= (y >> s->chroma_y_shift) * s->uvlinesize;
            offset[3]= 0;
        }

        emms_c();

        s->avctx->draw_horiz_band(s->avctx, src, offset,
                                  y, s->picture_structure, h);
    }
}

void ff_init_block_index(MpegEncContext *s){ //FIXME maybe rename
    const int linesize= s->current_picture.linesize[0]; //not s->linesize as this woulnd be wrong for field pics
    const int uvlinesize= s->current_picture.linesize[1];
        
    s->block_index[0]= s->b8_stride*(s->mb_y*2    ) - 2 + s->mb_x*2;
    s->block_index[1]= s->b8_stride*(s->mb_y*2    ) - 1 + s->mb_x*2;
    s->block_index[2]= s->b8_stride*(s->mb_y*2 + 1) - 2 + s->mb_x*2;
    s->block_index[3]= s->b8_stride*(s->mb_y*2 + 1) - 1 + s->mb_x*2;
    s->block_index[4]= s->mb_stride*(s->mb_y + 1)                + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    s->block_index[5]= s->mb_stride*(s->mb_y + s->mb_height + 2) + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    //block_index is not used by mpeg2, so it is not affected by chroma_format

    s->dest[0] = s->current_picture.data[0] + (s->mb_x - 1)*16;
    s->dest[1] = s->current_picture.data[1] + (s->mb_x - 1)*(16 >> s->chroma_x_shift);
    s->dest[2] = s->current_picture.data[2] + (s->mb_x - 1)*(16 >> s->chroma_x_shift);
    
    if(!(s->pict_type==B_TYPE && s->avctx->draw_horiz_band && s->picture_structure==PICT_FRAME))
    {
        s->dest[0] += s->mb_y *   linesize * 16;
        s->dest[1] += s->mb_y * uvlinesize * (16 >> s->chroma_y_shift);
        s->dest[2] += s->mb_y * uvlinesize * (16 >> s->chroma_y_shift);
    }
}

#ifdef CONFIG_ENCODERS

static void get_vissual_weight(int16_t *weight, uint8_t *ptr, int stride){
    int x, y;
//FIXME optimize
    for(y=0; y<8; y++){
        for(x=0; x<8; x++){
            int x2, y2;
            int sum=0;
            int sqr=0;
            int count=0;

            for(y2= FFMAX(y-1, 0); y2 < FFMIN(8, y+2); y2++){
                for(x2= FFMAX(x-1, 0); x2 < FFMIN(8, x+2); x2++){
                    int v= ptr[x2 + y2*stride];
                    sum += v;
                    sqr += v*v;
                    count++;
                }
            }
            weight[x + 8*y]= (36*ff_sqrt(count*sqr - sum*sum)) / count;
        }
    }
}

static void encode_mb(MpegEncContext *s, int motion_x, int motion_y)
{
    int16_t weight[6][64];
    DCTELEM orig[6][64];
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    int i;
    int skip_dct[6];
    int dct_offset   = s->linesize*8; //default for progressive frames
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int wrap_y, wrap_c;
    
    for(i=0; i<6; i++) skip_dct[i]=0;
    
    if(s->adaptive_quant){
        const int last_qp= s->qscale;
        const int mb_xy= mb_x + mb_y*s->mb_stride;

        s->lambda= s->lambda_table[mb_xy];
        update_qscale(s);
    
        if(!(s->flags&CODEC_FLAG_QP_RD)){
            s->dquant= s->qscale - last_qp;

            if(s->out_format==FMT_H263){
                s->dquant= clip(s->dquant, -2, 2); //FIXME RD
            
                if(s->codec_id==CODEC_ID_MPEG4){        
                    if(!s->mb_intra){
                        if(s->pict_type == B_TYPE){
                            if(s->dquant&1) 
                                s->dquant= (s->dquant/2)*2;
                            if(s->mv_dir&MV_DIRECT)
                                s->dquant= 0;
                        }
                        if(s->mv_type==MV_TYPE_8X8)
                            s->dquant=0;
                    }
                }
            }
        }
        ff_set_qscale(s, last_qp + s->dquant);
    }else if(s->flags&CODEC_FLAG_QP_RD)
        ff_set_qscale(s, s->qscale + s->dquant);

    wrap_y = s->linesize;
    wrap_c = s->uvlinesize;
    ptr_y = s->new_picture.data[0] + (mb_y * 16 * wrap_y) + mb_x * 16;
    ptr_cb = s->new_picture.data[1] + (mb_y * 8 * wrap_c) + mb_x * 8;
    ptr_cr = s->new_picture.data[2] + (mb_y * 8 * wrap_c) + mb_x * 8;

    if(mb_x*16+16 > s->width || mb_y*16+16 > s->height){
        ff_emulated_edge_mc(s->edge_emu_buffer            , ptr_y , wrap_y,16,16,mb_x*16,mb_y*16, s->width   , s->height);
        ptr_y= s->edge_emu_buffer;
        ff_emulated_edge_mc(s->edge_emu_buffer+18*wrap_y  , ptr_cb, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
        ptr_cb= s->edge_emu_buffer+18*wrap_y;
        ff_emulated_edge_mc(s->edge_emu_buffer+18*wrap_y+9, ptr_cr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
        ptr_cr= s->edge_emu_buffer+18*wrap_y+9;
    }

    if (s->mb_intra) {
        if(s->flags&CODEC_FLAG_INTERLACED_DCT){
            int progressive_score, interlaced_score;

            s->interlaced_dct=0;
            progressive_score= s->dsp.ildct_cmp[4](s, ptr_y           , NULL, wrap_y, 8) 
                              +s->dsp.ildct_cmp[4](s, ptr_y + wrap_y*8, NULL, wrap_y, 8) - 400;

            if(progressive_score > 0){
                interlaced_score = s->dsp.ildct_cmp[4](s, ptr_y           , NULL, wrap_y*2, 8) 
                                  +s->dsp.ildct_cmp[4](s, ptr_y + wrap_y  , NULL, wrap_y*2, 8);
                if(progressive_score > interlaced_score){
                    s->interlaced_dct=1;
            
                    dct_offset= wrap_y;
                    wrap_y<<=1;
                }
            }
        }
        
	s->dsp.get_pixels(s->block[0], ptr_y                 , wrap_y);
        s->dsp.get_pixels(s->block[1], ptr_y              + 8, wrap_y);
        s->dsp.get_pixels(s->block[2], ptr_y + dct_offset    , wrap_y);
        s->dsp.get_pixels(s->block[3], ptr_y + dct_offset + 8, wrap_y);

        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
	    s->dsp.get_pixels(s->block[4], ptr_cb, wrap_c);
            s->dsp.get_pixels(s->block[5], ptr_cr, wrap_c);
        }
    }else{
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        uint8_t *dest_y, *dest_cb, *dest_cr;

        dest_y  = s->dest[0];
        dest_cb = s->dest[1];
        dest_cr = s->dest[2];

        if ((!s->no_rounding) || s->pict_type==B_TYPE){
	    op_pix = s->dsp.put_pixels_tab;
            op_qpix= s->dsp.put_qpel_pixels_tab;
        }else{
            op_pix = s->dsp.put_no_rnd_pixels_tab;
            op_qpix= s->dsp.put_no_rnd_qpel_pixels_tab;
        }

        if (s->mv_dir & MV_DIR_FORWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture.data, op_pix, op_qpix);
            op_pix = s->dsp.avg_pixels_tab;
            op_qpix= s->dsp.avg_qpel_pixels_tab;
        }
        if (s->mv_dir & MV_DIR_BACKWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture.data, op_pix, op_qpix);
        }

        if(s->flags&CODEC_FLAG_INTERLACED_DCT){
            int progressive_score, interlaced_score;

            s->interlaced_dct=0;
            progressive_score= s->dsp.ildct_cmp[0](s, dest_y           , ptr_y           , wrap_y, 8) 
                              +s->dsp.ildct_cmp[0](s, dest_y + wrap_y*8, ptr_y + wrap_y*8, wrap_y, 8) - 400;
            
            if(s->avctx->ildct_cmp == FF_CMP_VSSE) progressive_score -= 400;

            if(progressive_score>0){
                interlaced_score = s->dsp.ildct_cmp[0](s, dest_y           , ptr_y           , wrap_y*2, 8) 
                                  +s->dsp.ildct_cmp[0](s, dest_y + wrap_y  , ptr_y + wrap_y  , wrap_y*2, 8);
            
                if(progressive_score > interlaced_score){
                    s->interlaced_dct=1;
            
                    dct_offset= wrap_y;
                    wrap_y<<=1;
                }
            }
        }
        
	s->dsp.diff_pixels(s->block[0], ptr_y                 , dest_y                 , wrap_y);
        s->dsp.diff_pixels(s->block[1], ptr_y              + 8, dest_y              + 8, wrap_y);
        s->dsp.diff_pixels(s->block[2], ptr_y + dct_offset    , dest_y + dct_offset    , wrap_y);
        s->dsp.diff_pixels(s->block[3], ptr_y + dct_offset + 8, dest_y + dct_offset + 8, wrap_y);
        
        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
            s->dsp.diff_pixels(s->block[4], ptr_cb, dest_cb, wrap_c);
            s->dsp.diff_pixels(s->block[5], ptr_cr, dest_cr, wrap_c);
        }
        /* pre quantization */         
        if(s->current_picture.mc_mb_var[s->mb_stride*mb_y+ mb_x]<2*s->qscale*s->qscale){
            //FIXME optimize
	    if(s->dsp.sad[1](NULL, ptr_y               , dest_y               , wrap_y, 8) < 20*s->qscale) skip_dct[0]= 1;
            if(s->dsp.sad[1](NULL, ptr_y            + 8, dest_y            + 8, wrap_y, 8) < 20*s->qscale) skip_dct[1]= 1;
            if(s->dsp.sad[1](NULL, ptr_y +dct_offset   , dest_y +dct_offset   , wrap_y, 8) < 20*s->qscale) skip_dct[2]= 1;
            if(s->dsp.sad[1](NULL, ptr_y +dct_offset+ 8, dest_y +dct_offset+ 8, wrap_y, 8) < 20*s->qscale) skip_dct[3]= 1;
            if(s->dsp.sad[1](NULL, ptr_cb              , dest_cb              , wrap_c, 8) < 20*s->qscale) skip_dct[4]= 1;
            if(s->dsp.sad[1](NULL, ptr_cr              , dest_cr              , wrap_c, 8) < 20*s->qscale) skip_dct[5]= 1;
        }
    }

    if(s->avctx->quantizer_noise_shaping){
        if(!skip_dct[0]) get_vissual_weight(weight[0], ptr_y                 , wrap_y);
        if(!skip_dct[1]) get_vissual_weight(weight[1], ptr_y              + 8, wrap_y);
        if(!skip_dct[2]) get_vissual_weight(weight[2], ptr_y + dct_offset    , wrap_y);
        if(!skip_dct[3]) get_vissual_weight(weight[3], ptr_y + dct_offset + 8, wrap_y);
        if(!skip_dct[4]) get_vissual_weight(weight[4], ptr_cb                , wrap_c);
        if(!skip_dct[5]) get_vissual_weight(weight[5], ptr_cr                , wrap_c);
        memcpy(orig[0], s->block[0], sizeof(DCTELEM)*64*6);
    }
            
    /* DCT & quantize */
    assert(s->out_format!=FMT_MJPEG || s->qscale==8);
    {
        for(i=0;i<6;i++) {
            if(!skip_dct[i]){
                int overflow;
                s->block_last_index[i] = s->dct_quantize(s, s->block[i], i, s->qscale, &overflow);
            // FIXME we could decide to change to quantizer instead of clipping
            // JS: I don't think that would be a good idea it could lower quality instead
            //     of improve it. Just INTRADC clipping deserves changes in quantizer
                if (overflow) clip_coeffs(s, s->block[i], s->block_last_index[i]);
            }else
                s->block_last_index[i]= -1;
        }
        if(s->avctx->quantizer_noise_shaping){
            for(i=0;i<6;i++) {
                if(!skip_dct[i]){
                    s->block_last_index[i] = dct_quantize_refine(s, s->block[i], weight[i], orig[i], i, s->qscale);
                }
            }
        }
        
        if(s->luma_elim_threshold && !s->mb_intra)
            for(i=0; i<4; i++)
                dct_single_coeff_elimination(s, i, s->luma_elim_threshold);
        if(s->chroma_elim_threshold && !s->mb_intra)
            for(i=4; i<6; i++)
                dct_single_coeff_elimination(s, i, s->chroma_elim_threshold);

        if(s->flags & CODEC_FLAG_CBP_RD){
            for(i=0;i<6;i++) {
                if(s->block_last_index[i] == -1)
                    s->coded_score[i]= INT_MAX/256;
            }
        }
    }

    if((s->flags&CODEC_FLAG_GRAY) && s->mb_intra){
        s->block_last_index[4]=
        s->block_last_index[5]= 0;
        s->block[4][0]=
        s->block[5][0]= (1024 + s->c_dc_scale/2)/ s->c_dc_scale;
    }

    //non c quantize code returns incorrect block_last_index FIXME
    if(s->alternate_scan && s->dct_quantize != dct_quantize_c){
        for(i=0; i<6; i++){
            int j;
            if(s->block_last_index[i]>0){
                for(j=63; j>0; j--){
                    if(s->block[i][ s->intra_scantable.permutated[j] ]) break;
                }
                s->block_last_index[i]= j;
            }
        }
    }

    /* huffman encode */
    switch(s->codec_id){ //FIXME funct ptr could be slightly faster
    case CODEC_ID_MPEG1VIDEO:
    case CODEC_ID_MPEG2VIDEO:
        mpeg1_encode_mb(s, s->block, motion_x, motion_y); break;
#ifdef CONFIG_RISKY
    case CODEC_ID_MPEG4:
        mpeg4_encode_mb(s, s->block, motion_x, motion_y); break;
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
    case CODEC_ID_WMV1:
        msmpeg4_encode_mb(s, s->block, motion_x, motion_y); break;
    case CODEC_ID_WMV2:
         ff_wmv2_encode_mb(s, s->block, motion_x, motion_y); break;
    case CODEC_ID_H263:
    case CODEC_ID_H263P:
    case CODEC_ID_FLV1:
    case CODEC_ID_RV10:
        h263_encode_mb(s, s->block, motion_x, motion_y); break;
#endif
    case CODEC_ID_MJPEG:
        mjpeg_encode_mb(s, s->block); break;
    default:
        assert(0);
    }
}

#endif //CONFIG_ENCODERS

void ff_mpeg_flush(AVCodecContext *avctx){
    int i;
    MpegEncContext *s = avctx->priv_data;
    
    if(s==NULL || s->picture==NULL) 
        return;
    
    for(i=0; i<MAX_PICTURE_COUNT; i++){
       if(s->picture[i].data[0] && (   s->picture[i].type == FF_BUFFER_TYPE_INTERNAL
                                    || s->picture[i].type == FF_BUFFER_TYPE_USER))
        avctx->release_buffer(avctx, (AVFrame*)&s->picture[i]);
    }
    s->current_picture_ptr = s->last_picture_ptr = s->next_picture_ptr = NULL;
    
    s->parse_context.state= -1;
    s->parse_context.frame_start_found= 0;
    s->parse_context.overread= 0;
    s->parse_context.overread_index= 0;
    s->parse_context.index= 0;
    s->parse_context.last_index= 0;
    s->bitstream_buffer_size=0;
}

#ifdef CONFIG_ENCODERS
void ff_copy_bits(PutBitContext *pb, uint8_t *src, int length)
{
    const uint16_t *srcw= (uint16_t*)src;
    int words= length>>4;
    int bits= length&15;
    int i;

    if(length==0) return;
    
    if(words < 16){
        for(i=0; i<words; i++) put_bits(pb, 16, be2me_16(srcw[i]));
    }else if(put_bits_count(pb)&7){
        for(i=0; i<words; i++) put_bits(pb, 16, be2me_16(srcw[i]));
    }else{
        for(i=0; put_bits_count(pb)&31; i++)
            put_bits(pb, 8, src[i]);
        flush_put_bits(pb);
        memcpy(pbBufPtr(pb), src+i, 2*words-i);
        skip_put_bytes(pb, 2*words-i);
    }
        
    put_bits(pb, bits, be2me_16(srcw[words])>>(16-bits));
}

static inline void copy_context_before_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster then a loop?

    /* mpeg1 */
    d->mb_skip_run= s->mb_skip_run;
    for(i=0; i<3; i++)
        d->last_dc[i]= s->last_dc[i];
    
    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->f_count= s->f_count;
    d->b_count= s->b_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;
    d->last_bits= 0;

    d->mb_skiped= 0;
    d->qscale= s->qscale;
    d->dquant= s->dquant;
}

static inline void copy_context_after_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->mv, s->mv, 2*4*2*sizeof(int)); 
    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster then a loop?
    
    /* mpeg1 */
    d->mb_skip_run= s->mb_skip_run;
    for(i=0; i<3; i++)
        d->last_dc[i]= s->last_dc[i];
    
    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->f_count= s->f_count;
    d->b_count= s->b_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;

    d->mb_intra= s->mb_intra;
    d->mb_skiped= s->mb_skiped;
    d->mv_type= s->mv_type;
    d->mv_dir= s->mv_dir;
    d->pb= s->pb;
    if(s->data_partitioning){
        d->pb2= s->pb2;
        d->tex_pb= s->tex_pb;
    }
    d->block= s->block;
    for(i=0; i<6; i++)
        d->block_last_index[i]= s->block_last_index[i];
    d->interlaced_dct= s->interlaced_dct;
    d->qscale= s->qscale;
}

static inline void encode_mb_hq(MpegEncContext *s, MpegEncContext *backup, MpegEncContext *best, int type, 
                           PutBitContext pb[2], PutBitContext pb2[2], PutBitContext tex_pb[2],
                           int *dmin, int *next_block, int motion_x, int motion_y)
{
    int score;
    uint8_t *dest_backup[3];
    
    copy_context_before_encode(s, backup, type);

    s->block= s->blocks[*next_block];
    s->pb= pb[*next_block];
    if(s->data_partitioning){
        s->pb2   = pb2   [*next_block];
        s->tex_pb= tex_pb[*next_block];
    }
    
    if(*next_block){
        memcpy(dest_backup, s->dest, sizeof(s->dest));
        s->dest[0] = s->rd_scratchpad;
        s->dest[1] = s->rd_scratchpad + 16*s->linesize;
        s->dest[2] = s->rd_scratchpad + 16*s->linesize + 8;
        assert(s->linesize >= 32); //FIXME
    }

    encode_mb(s, motion_x, motion_y);
    
    score= put_bits_count(&s->pb);
    if(s->data_partitioning){
        score+= put_bits_count(&s->pb2);
        score+= put_bits_count(&s->tex_pb);
    }
   
    if(s->avctx->mb_decision == FF_MB_DECISION_RD){
        MPV_decode_mb(s, s->block);

        score *= s->lambda2;
        score += sse_mb(s) << FF_LAMBDA_SHIFT;
    }
    
    if(*next_block){
        memcpy(s->dest, dest_backup, sizeof(s->dest));
    }

    if(score<*dmin){
        *dmin= score;
        *next_block^=1;

        copy_context_after_encode(best, s, type);
    }
}
                
static int sse(MpegEncContext *s, uint8_t *src1, uint8_t *src2, int w, int h, int stride){
    uint32_t *sq = squareTbl + 256;
    int acc=0;
    int x,y;
    
    if(w==16 && h==16) 
        return s->dsp.sse[0](NULL, src1, src2, stride, 16);
    else if(w==8 && h==8)
        return s->dsp.sse[1](NULL, src1, src2, stride, 8);
    
    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            acc+= sq[src1[x + y*stride] - src2[x + y*stride]];
        } 
    }
    
    assert(acc>=0);
    
    return acc;
}

static int sse_mb(MpegEncContext *s){
    int w= 16;
    int h= 16;

    if(s->mb_x*16 + 16 > s->width ) w= s->width - s->mb_x*16;
    if(s->mb_y*16 + 16 > s->height) h= s->height- s->mb_y*16;

    if(w==16 && h==16)
      if(s->avctx->mb_cmp == FF_CMP_NSSE){
        return  s->dsp.nsse[0](s, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*s->linesize*16, s->dest[0], s->linesize, 16)
               +s->dsp.nsse[1](s, s->new_picture.data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[1], s->uvlinesize, 8)
               +s->dsp.nsse[1](s, s->new_picture.data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[2], s->uvlinesize, 8);
      }else{
        return  s->dsp.sse[0](NULL, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*s->linesize*16, s->dest[0], s->linesize, 16)
               +s->dsp.sse[1](NULL, s->new_picture.data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[1], s->uvlinesize, 8)
               +s->dsp.sse[1](NULL, s->new_picture.data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[2], s->uvlinesize, 8);
      }
    else
        return  sse(s, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*s->linesize*16, s->dest[0], w, h, s->linesize)
               +sse(s, s->new_picture.data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[1], w>>1, h>>1, s->uvlinesize)
               +sse(s, s->new_picture.data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[2], w>>1, h>>1, s->uvlinesize);
}

static int pre_estimate_motion_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= arg;

    
    s->me.pre_pass=1;
    s->me.dia_size= s->avctx->pre_dia_size;
    s->first_slice_line=1;
    for(s->mb_y= s->end_mb_y-1; s->mb_y >= s->start_mb_y; s->mb_y--) {
        for(s->mb_x=s->mb_width-1; s->mb_x >=0 ;s->mb_x--) {
            ff_pre_estimate_p_frame_motion(s, s->mb_x, s->mb_y);
        }
        s->first_slice_line=0;
    }
    
    s->me.pre_pass=0;
    
    return 0;
}

static int estimate_motion_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= arg;

    s->me.dia_size= s->avctx->dia_size;
    s->first_slice_line=1;
    for(s->mb_y= s->start_mb_y; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x=0; //for block init below
        ff_init_block_index(s);
        for(s->mb_x=0; s->mb_x < s->mb_width; s->mb_x++) {
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;
            
            /* compute motion vector & mb_type and store in context */
            if(s->pict_type==B_TYPE)
                ff_estimate_b_frame_motion(s, s->mb_x, s->mb_y);
            else
                ff_estimate_p_frame_motion(s, s->mb_x, s->mb_y);
        }
        s->first_slice_line=0;
    }
    return 0;
}

static int mb_var_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= arg;
    int mb_x, mb_y;

    for(mb_y=s->start_mb_y; mb_y < s->end_mb_y; mb_y++) {
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            int xx = mb_x * 16;
            int yy = mb_y * 16;
            uint8_t *pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
            int varc;
            int sum = s->dsp.pix_sum(pix, s->linesize);
    
            varc = (s->dsp.pix_norm1(pix, s->linesize) - (((unsigned)(sum*sum))>>8) + 500 + 128)>>8;

            s->current_picture.mb_var [s->mb_stride * mb_y + mb_x] = varc;
            s->current_picture.mb_mean[s->mb_stride * mb_y + mb_x] = (sum+128)>>8;
            s->me.mb_var_sum_temp    += varc;
        }
    }
    return 0;
}

static void write_slice_end(MpegEncContext *s){
    if(s->codec_id==CODEC_ID_MPEG4){
        if(s->partitioned_frame){
            ff_mpeg4_merge_partitions(s);
        }
    
        ff_mpeg4_stuffing(&s->pb);
    }else if(s->out_format == FMT_MJPEG){
        ff_mjpeg_stuffing(&s->pb);
    }

    align_put_bits(&s->pb);
    flush_put_bits(&s->pb);
}

static int encode_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= arg;
    int mb_x, mb_y, pdif = 0;
    int i, j;
    MpegEncContext best_s, backup_s;
    uint8_t bit_buf[2][3000];
    uint8_t bit_buf2[2][3000];
    uint8_t bit_buf_tex[2][3000];
    PutBitContext pb[2], pb2[2], tex_pb[2];
//printf("%d->%d\n", s->resync_mb_y, s->end_mb_y);

    for(i=0; i<2; i++){
        init_put_bits(&pb    [i], bit_buf    [i], 3000);
        init_put_bits(&pb2   [i], bit_buf2   [i], 3000);
        init_put_bits(&tex_pb[i], bit_buf_tex[i], 3000);
    }

    s->last_bits= put_bits_count(&s->pb);
    s->mv_bits=0;
    s->misc_bits=0;
    s->i_tex_bits=0;
    s->p_tex_bits=0;
    s->i_count=0;
    s->f_count=0;
    s->b_count=0;
    s->skip_count=0;

    for(i=0; i<3; i++){
        /* init last dc values */
        /* note: quant matrix value (8) is implied here */
        s->last_dc[i] = 128 << s->intra_dc_precision;
        
        s->current_picture_ptr->error[i] = 0;
    }
    s->mb_skip_run = 0;
    memset(s->last_mv, 0, sizeof(s->last_mv));
     
    s->last_mv_dir = 0;

#ifdef CONFIG_RISKY
    switch(s->codec_id){
    case CODEC_ID_H263:
    case CODEC_ID_H263P:
    case CODEC_ID_FLV1:
        s->gob_index = ff_h263_get_gob_height(s);
        break;
    case CODEC_ID_MPEG4:
        if(s->partitioned_frame)
            ff_mpeg4_init_partitions(s);
        break;
    }
#endif

    s->resync_mb_x=0;
    s->resync_mb_y=0; 
    s->first_slice_line = 1;
    s->ptr_lastgob = s->pb.buf;
    for(mb_y= s->start_mb_y; mb_y < s->end_mb_y; mb_y++) {
//    printf("row %d at %X\n", s->mb_y, (int)s);
        s->mb_x=0;
        s->mb_y= mb_y;

        ff_set_qscale(s, s->qscale);
        ff_init_block_index(s);
        
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            const int xy= mb_y*s->mb_stride + mb_x;
            int mb_type= s->mb_type[xy];
//            int d;
            int dmin= INT_MAX;
            int dir;

            s->mb_x = mb_x;
            ff_update_block_index(s);

            /* write gob / video packet header  */
#ifdef CONFIG_RISKY
            if(s->rtp_mode){
                int current_packet_size, is_gob_start;
                
                current_packet_size= ((put_bits_count(&s->pb)+7)>>3) - (s->ptr_lastgob - s->pb.buf);
                
                is_gob_start= s->avctx->rtp_payload_size && current_packet_size >= s->avctx->rtp_payload_size && mb_y + mb_x>0; 
                
                if(s->start_mb_y == mb_y && mb_y > 0 && mb_x==0) is_gob_start=1;
                
                switch(s->codec_id){
                case CODEC_ID_H263:
                case CODEC_ID_H263P:
                    if(!s->h263_slice_structured)
                        if(s->mb_x || s->mb_y%s->gob_index) is_gob_start=0;
                    break;
                case CODEC_ID_MPEG2VIDEO:
                    if(s->mb_x==0 && s->mb_y!=0) is_gob_start=1;
                case CODEC_ID_MPEG1VIDEO:
                    if(s->mb_skip_run) is_gob_start=0;
                    break;
                }

                if(is_gob_start){
                    if(s->start_mb_y != mb_y || mb_x!=0){
                        write_slice_end(s);

                        if(s->codec_id==CODEC_ID_MPEG4 && s->partitioned_frame){
                            ff_mpeg4_init_partitions(s);
                        }
                    }
                
                    assert((put_bits_count(&s->pb)&7) == 0);
                    current_packet_size= pbBufPtr(&s->pb) - s->ptr_lastgob;
                    
                    if(s->avctx->error_rate && s->resync_mb_x + s->resync_mb_y > 0){
                        int r= put_bits_count(&s->pb)/8 + s->picture_number + s->codec_id + s->mb_x + s->mb_y;
                        int d= 100 / s->avctx->error_rate;
                        if(r % d == 0){
                            current_packet_size=0;
#ifndef ALT_BITSTREAM_WRITER
                            s->pb.buf_ptr= s->ptr_lastgob;
#endif
                            assert(pbBufPtr(&s->pb) == s->ptr_lastgob);
                        }
                    }
        
                    if (s->avctx->rtp_callback)
                        s->avctx->rtp_callback(s->avctx, s->ptr_lastgob, current_packet_size, 0);
                    
                    switch(s->codec_id){
                    case CODEC_ID_MPEG4:
                        ff_mpeg4_encode_video_packet_header(s);
                        ff_mpeg4_clean_buffers(s);
                    break;
                    case CODEC_ID_MPEG1VIDEO:
                    case CODEC_ID_MPEG2VIDEO:
                        ff_mpeg1_encode_slice_header(s);
                        ff_mpeg1_clean_buffers(s);
                    break;
                    case CODEC_ID_H263:
                    case CODEC_ID_H263P:
                        h263_encode_gob_header(s, mb_y);                       
                    break;
                    }

                    if(s->flags&CODEC_FLAG_PASS1){
                        int bits= put_bits_count(&s->pb);
                        s->misc_bits+= bits - s->last_bits;
                        s->last_bits= bits;
                    }
    
                    s->ptr_lastgob += current_packet_size;
                    s->first_slice_line=1;
                    s->resync_mb_x=mb_x;
                    s->resync_mb_y=mb_y;
                }
            }
#endif

            if(  (s->resync_mb_x   == s->mb_x)
               && s->resync_mb_y+1 == s->mb_y){
                s->first_slice_line=0; 
            }

            s->mb_skiped=0;
            s->dquant=0; //only for QP_RD

            if(mb_type & (mb_type-1) || (s->flags & CODEC_FLAG_QP_RD)){ // more than 1 MB type possible
                int next_block=0;
                int pb_bits_count, pb2_bits_count, tex_pb_bits_count;

                copy_context_before_encode(&backup_s, s, -1);
                backup_s.pb= s->pb;
                best_s.data_partitioning= s->data_partitioning;
                best_s.partitioned_frame= s->partitioned_frame;
                if(s->data_partitioning){
                    backup_s.pb2= s->pb2;
                    backup_s.tex_pb= s->tex_pb;
                }

                if(mb_type&CANDIDATE_MB_TYPE_INTER){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->p_mv_table[xy][0];
                    s->mv[0][0][1] = s->p_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER_I){ 
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->p_field_select_table[i][xy];
                        s->mv[0][i][0] = s->p_field_mv_table[i][j][xy][0];
                        s->mv[0][i][1] = s->p_field_mv_table[i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER_I, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_SKIPED){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_SKIPED, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER4V){                 
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->current_picture.motion_val[0][s->block_index[i]][0];
                        s->mv[0][i][1] = s->current_picture.motion_val[0][s->block_index[i]][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER4V, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_FORWARD, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD){
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BACKWARD, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[1][0][0], s->mv[1][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR){
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BIDIR, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_DIRECT){
                    int mx= s->b_direct_mv_table[xy][0];
                    int my= s->b_direct_mv_table[xy][1];
                    
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
#ifdef CONFIG_RISKY
                    ff_mpeg4_set_direct_mv(s, mx, my);
#endif
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_DIRECT, pb, pb2, tex_pb, 
                                 &dmin, &next_block, mx, my);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD_I){ 
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_FORWARD_I, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD_I){ 
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BACKWARD_I, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR_I){ 
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            j= s->field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BIDIR_I, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTRA){
                    s->mv_dir = 0;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 1;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTRA, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                    if(s->h263_pred || s->h263_aic){
                        if(best_s.mb_intra)
                            s->mbintra_table[mb_x + mb_y*s->mb_stride]=1;
                        else
                            ff_clean_intra_table_entries(s); //old mode?
                    }
                }

                if(s->flags & CODEC_FLAG_QP_RD){
                    if(best_s.mv_type==MV_TYPE_16X16 && !(best_s.mv_dir&MV_DIRECT)){
                        const int last_qp= backup_s.qscale;
                        int dquant, dir, qp, dc[6];
                        DCTELEM ac[6][16];
                        const int mvdir= (best_s.mv_dir&MV_DIR_BACKWARD) ? 1 : 0;
                        
                        assert(backup_s.dquant == 0);

                        //FIXME intra
                        s->mv_dir= best_s.mv_dir;
                        s->mv_type = MV_TYPE_16X16;
                        s->mb_intra= best_s.mb_intra;
                        s->mv[0][0][0] = best_s.mv[0][0][0];
                        s->mv[0][0][1] = best_s.mv[0][0][1];
                        s->mv[1][0][0] = best_s.mv[1][0][0];
                        s->mv[1][0][1] = best_s.mv[1][0][1];
                        
                        dir= s->pict_type == B_TYPE ? 2 : 1;
                        if(last_qp + dir > s->avctx->qmax) dir= -dir;
                        for(dquant= dir; dquant<=2 && dquant>=-2; dquant += dir){
                            qp= last_qp + dquant;
                            if(qp < s->avctx->qmin || qp > s->avctx->qmax)
                                break;
                            backup_s.dquant= dquant;
                            if(s->mb_intra){
                                for(i=0; i<6; i++){
                                    dc[i]= s->dc_val[0][ s->block_index[i] ];
                                    memcpy(ac[i], s->ac_val[0][s->block_index[i]], sizeof(DCTELEM)*16);
                                }
                            }

                            encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER /* wrong but unused */, pb, pb2, tex_pb, 
                                         &dmin, &next_block, s->mv[mvdir][0][0], s->mv[mvdir][0][1]);
                            if(best_s.qscale != qp){
                                if(s->mb_intra){
                                    for(i=0; i<6; i++){
                                        s->dc_val[0][ s->block_index[i] ]= dc[i];
                                        memcpy(s->ac_val[0][s->block_index[i]], ac[i], sizeof(DCTELEM)*16);
                                    }
                                }
                                if(dir > 0 && dquant==dir){
                                    dquant= 0;
                                    dir= -dir;
                                }else
                                    break;
                            }
                        }
                        qp= best_s.qscale;
                        s->current_picture.qscale_table[xy]= qp;
                    }
                }

                copy_context_after_encode(s, &best_s, -1);
                
                pb_bits_count= put_bits_count(&s->pb);
                flush_put_bits(&s->pb);
                ff_copy_bits(&backup_s.pb, bit_buf[next_block^1], pb_bits_count);
                s->pb= backup_s.pb;
                
                if(s->data_partitioning){
                    pb2_bits_count= put_bits_count(&s->pb2);
                    flush_put_bits(&s->pb2);
                    ff_copy_bits(&backup_s.pb2, bit_buf2[next_block^1], pb2_bits_count);
                    s->pb2= backup_s.pb2;
                    
                    tex_pb_bits_count= put_bits_count(&s->tex_pb);
                    flush_put_bits(&s->tex_pb);
                    ff_copy_bits(&backup_s.tex_pb, bit_buf_tex[next_block^1], tex_pb_bits_count);
                    s->tex_pb= backup_s.tex_pb;
                }
                s->last_bits= put_bits_count(&s->pb);
               
#ifdef CONFIG_RISKY
                if (s->out_format == FMT_H263 && s->pict_type!=B_TYPE)
                    ff_h263_update_motion_val(s);
#endif
        
                if(next_block==0){ //FIXME 16 vs linesize16
                    s->dsp.put_pixels_tab[0][0](s->dest[0], s->rd_scratchpad                     , s->linesize  ,16);
                    s->dsp.put_pixels_tab[1][0](s->dest[1], s->rd_scratchpad + 16*s->linesize    , s->uvlinesize, 8);
                    s->dsp.put_pixels_tab[1][0](s->dest[2], s->rd_scratchpad + 16*s->linesize + 8, s->uvlinesize, 8);
                }

                if(s->avctx->mb_decision == FF_MB_DECISION_BITS)
                    MPV_decode_mb(s, s->block);
            } else {
                int motion_x, motion_y;
                s->mv_type=MV_TYPE_16X16;
                // only one MB-Type possible
                
                switch(mb_type){
                case CANDIDATE_MB_TYPE_INTRA:
                    s->mv_dir = 0;
                    s->mb_intra= 1;
                    motion_x= s->mv[0][0][0] = 0;
                    motion_y= s->mv[0][0][1] = 0;
                    break;
                case CANDIDATE_MB_TYPE_INTER:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->p_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->p_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_INTER_I:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->p_field_select_table[i][xy];
                        s->mv[0][i][0] = s->p_field_mv_table[i][j][xy][0];
                        s->mv[0][i][1] = s->p_field_mv_table[i][j][xy][1];
                    }
                    motion_x = motion_y = 0;
                    break;
                case CANDIDATE_MB_TYPE_INTER4V:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->current_picture.motion_val[0][s->block_index[i]][0];
                        s->mv[0][i][1] = s->current_picture.motion_val[0][s->block_index[i]][1];
                    }
                    motion_x= motion_y= 0;
                    break;
                case CANDIDATE_MB_TYPE_DIRECT:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
                    motion_x=s->b_direct_mv_table[xy][0];
                    motion_y=s->b_direct_mv_table[xy][1];
#ifdef CONFIG_RISKY
                    ff_mpeg4_set_direct_mv(s, motion_x, motion_y);
#endif
                    break;
                case CANDIDATE_MB_TYPE_BIDIR:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x=0;
                    motion_y=0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    motion_y= s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_FORWARD:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
//                    printf(" %d %d ", motion_x, motion_y);
                    break;
                case CANDIDATE_MB_TYPE_FORWARD_I:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    motion_x=motion_y=0;
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD_I:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    motion_x=motion_y=0;
                    break;
                case CANDIDATE_MB_TYPE_BIDIR_I:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            j= s->field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    motion_x=motion_y=0;
                    break;
                default:
                    motion_x=motion_y=0; //gcc warning fix
                    av_log(s->avctx, AV_LOG_ERROR, "illegal MB type\n");
                }

                encode_mb(s, motion_x, motion_y);

                // RAL: Update last macrobloc type
                s->last_mv_dir = s->mv_dir;
            
#ifdef CONFIG_RISKY
                if (s->out_format == FMT_H263 && s->pict_type!=B_TYPE)
                    ff_h263_update_motion_val(s);
#endif
		
                MPV_decode_mb(s, s->block);
            }

            /* clean the MV table in IPS frames for direct mode in B frames */
            if(s->mb_intra /* && I,P,S_TYPE */){
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
            }
            
            if(s->flags&CODEC_FLAG_PSNR){
                int w= 16;
                int h= 16;

                if(s->mb_x*16 + 16 > s->width ) w= s->width - s->mb_x*16;
                if(s->mb_y*16 + 16 > s->height) h= s->height- s->mb_y*16;

                s->current_picture_ptr->error[0] += sse(
                    s, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*s->linesize*16,
                    s->dest[0], w, h, s->linesize);
                s->current_picture_ptr->error[1] += sse(
                    s, s->new_picture.data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    s->dest[1], w>>1, h>>1, s->uvlinesize);
                s->current_picture_ptr->error[2] += sse(
                    s, s->new_picture    .data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    s->dest[2], w>>1, h>>1, s->uvlinesize);
            }
            if(s->loop_filter)
                ff_h263_loop_filter(s);
//printf("MB %d %d bits\n", s->mb_x+s->mb_y*s->mb_stride, put_bits_count(&s->pb));
        }
    }

#ifdef CONFIG_RISKY
    //not beautifull here but we must write it before flushing so it has to be here
    if (s->msmpeg4_version && s->msmpeg4_version<4 && s->pict_type == I_TYPE)
        msmpeg4_encode_ext_header(s);
#endif

    write_slice_end(s);

    /* Send the last GOB if RTP */    
    if (s->avctx->rtp_callback) {
        pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
        /* Call the RTP callback to send the last GOB */
        emms_c();
        s->avctx->rtp_callback(s->avctx, s->ptr_lastgob, pdif, 0);
    }

    return 0;
}

#define MERGE(field) dst->field += src->field; src->field=0
static void merge_context_after_me(MpegEncContext *dst, MpegEncContext *src){
    MERGE(me.scene_change_score);
    MERGE(me.mc_mb_var_sum_temp);
    MERGE(me.mb_var_sum_temp);
}

static void merge_context_after_encode(MpegEncContext *dst, MpegEncContext *src){
    int i;

    MERGE(dct_count[0]); //note, the other dct vars are not part of the context
    MERGE(dct_count[1]);
    MERGE(mv_bits);
    MERGE(i_tex_bits);
    MERGE(p_tex_bits);
    MERGE(i_count);
    MERGE(f_count);
    MERGE(b_count);
    MERGE(skip_count);
    MERGE(misc_bits);
    MERGE(error_count);
    MERGE(padding_bug_score);

    if(dst->avctx->noise_reduction){
        for(i=0; i<64; i++){
            MERGE(dct_error_sum[0][i]);
            MERGE(dct_error_sum[1][i]);
        }
    }
    
    assert(put_bits_count(&src->pb) % 8 ==0);
    assert(put_bits_count(&dst->pb) % 8 ==0);
    ff_copy_bits(&dst->pb, src->pb.buf, put_bits_count(&src->pb));
    flush_put_bits(&dst->pb);
}

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int i;
    int bits;

    s->picture_number = picture_number;
    
    /* Reset the average MB variance */
    s->me.mb_var_sum_temp    =
    s->me.mc_mb_var_sum_temp = 0;

#ifdef CONFIG_RISKY
    /* we need to initialize some time vars before we can encode b-frames */
    // RAL: Condition added for MPEG1VIDEO
    if (s->codec_id == CODEC_ID_MPEG1VIDEO || s->codec_id == CODEC_ID_MPEG2VIDEO || (s->h263_pred && !s->h263_msmpeg4))
        ff_set_mpeg4_time(s, s->picture_number);  //FIXME rename and use has_b_frames or similar
#endif
        
    s->me.scene_change_score=0;
    
//    s->lambda= s->current_picture_ptr->quality; //FIXME qscale / ... stuff for ME ratedistoration
    
    if(s->pict_type==I_TYPE){
        if(s->msmpeg4_version >= 3) s->no_rounding=1;
        else                        s->no_rounding=0;
    }else if(s->pict_type!=B_TYPE){
        if(s->flipflop_rounding || s->codec_id == CODEC_ID_H263P || s->codec_id == CODEC_ID_MPEG4)
            s->no_rounding ^= 1;          
    }
    
    s->mb_intra=0; //for the rate distoration & bit compare functions
    for(i=1; i<s->avctx->thread_count; i++){
        ff_update_duplicate_context(s->thread_context[i], s);
    }

    ff_init_me(s);

    /* Estimate motion for every MB */
    if(s->pict_type != I_TYPE){
        if(s->pict_type != B_TYPE && s->avctx->me_threshold==0){
            if((s->avctx->pre_me && s->last_non_b_pict_type==I_TYPE) || s->avctx->pre_me==2){
                s->avctx->execute(s->avctx, pre_estimate_motion_thread, (void**)&(s->thread_context[0]), NULL, s->avctx->thread_count);
            }
        }

        s->avctx->execute(s->avctx, estimate_motion_thread, (void**)&(s->thread_context[0]), NULL, s->avctx->thread_count);
    }else /* if(s->pict_type == I_TYPE) */{
        /* I-Frame */
        for(i=0; i<s->mb_stride*s->mb_height; i++)
            s->mb_type[i]= CANDIDATE_MB_TYPE_INTRA;
        
        if(!s->fixed_qscale){
            /* finding spatial complexity for I-frame rate control */
            s->avctx->execute(s->avctx, mb_var_thread, (void**)&(s->thread_context[0]), NULL, s->avctx->thread_count);
        }
    }
    for(i=1; i<s->avctx->thread_count; i++){
        merge_context_after_me(s, s->thread_context[i]);
    }
    s->current_picture.mc_mb_var_sum= s->current_picture_ptr->mc_mb_var_sum= s->me.mc_mb_var_sum_temp;
    s->current_picture.   mb_var_sum= s->current_picture_ptr->   mb_var_sum= s->me.   mb_var_sum_temp;
    emms_c();

    if(s->me.scene_change_score > s->avctx->scenechange_threshold && s->pict_type == P_TYPE){
        s->pict_type= I_TYPE;
        for(i=0; i<s->mb_stride*s->mb_height; i++)
            s->mb_type[i]= CANDIDATE_MB_TYPE_INTRA;
//printf("Scene change detected, encoding as I Frame %d %d\n", s->current_picture.mb_var_sum, s->current_picture.mc_mb_var_sum);
    }

    if(!s->umvplus){
        if(s->pict_type==P_TYPE || s->pict_type==S_TYPE) {
            s->f_code= ff_get_best_fcode(s, s->p_mv_table, CANDIDATE_MB_TYPE_INTER);

            if(s->flags & CODEC_FLAG_INTERLACED_ME){
                int a,b;
                a= ff_get_best_fcode(s, s->p_field_mv_table[0][0], CANDIDATE_MB_TYPE_INTER_I); //FIXME field_select
                b= ff_get_best_fcode(s, s->p_field_mv_table[1][1], CANDIDATE_MB_TYPE_INTER_I);
                s->f_code= FFMAX(s->f_code, FFMAX(a,b));
            }
                    
            ff_fix_long_p_mvs(s);
            ff_fix_long_mvs(s, NULL, 0, s->p_mv_table, s->f_code, CANDIDATE_MB_TYPE_INTER, 0);
            if(s->flags & CODEC_FLAG_INTERLACED_ME){
                int j;
                for(i=0; i<2; i++){
                    for(j=0; j<2; j++)
                        ff_fix_long_mvs(s, s->p_field_select_table[i], j, 
                                        s->p_field_mv_table[i][j], s->f_code, CANDIDATE_MB_TYPE_INTER_I, 0);
                }
            }
        }

        if(s->pict_type==B_TYPE){
            int a, b;

            a = ff_get_best_fcode(s, s->b_forw_mv_table, CANDIDATE_MB_TYPE_FORWARD);
            b = ff_get_best_fcode(s, s->b_bidir_forw_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->f_code = FFMAX(a, b);

            a = ff_get_best_fcode(s, s->b_back_mv_table, CANDIDATE_MB_TYPE_BACKWARD);
            b = ff_get_best_fcode(s, s->b_bidir_back_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->b_code = FFMAX(a, b);

            ff_fix_long_mvs(s, NULL, 0, s->b_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_FORWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BACKWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            if(s->flags & CODEC_FLAG_INTERLACED_ME){
                int dir, j;
                for(dir=0; dir<2; dir++){
                    for(i=0; i<2; i++){
                        for(j=0; j<2; j++){
                            int type= dir ? (CANDIDATE_MB_TYPE_BACKWARD_I|CANDIDATE_MB_TYPE_BIDIR_I) 
                                          : (CANDIDATE_MB_TYPE_FORWARD_I |CANDIDATE_MB_TYPE_BIDIR_I);
                            ff_fix_long_mvs(s, s->b_field_select_table[dir][i], j, 
                                            s->b_field_mv_table[dir][i][j], dir ? s->b_code : s->f_code, type, 1);
                        }
                    }
                }
            }
        }
    }

    if (!s->fixed_qscale) 
        s->current_picture.quality = ff_rate_estimate_qscale(s); //FIXME pic_ptr

    if(s->adaptive_quant){
#ifdef CONFIG_RISKY
        switch(s->codec_id){
        case CODEC_ID_MPEG4:
            ff_clean_mpeg4_qscales(s);
            break;
        case CODEC_ID_H263:
        case CODEC_ID_H263P:
        case CODEC_ID_FLV1:
            ff_clean_h263_qscales(s);
            break;
        }
#endif

        s->lambda= s->lambda_table[0];
        //FIXME broken
    }else
        s->lambda= s->current_picture.quality;
//printf("%d %d\n", s->avctx->global_quality, s->current_picture.quality);
    update_qscale(s);
    
    if(s->qscale < 3 && s->max_qcoeff<=128 && s->pict_type==I_TYPE && !(s->flags & CODEC_FLAG_QSCALE)) 
        s->qscale= 3; //reduce cliping problems
        
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = ff_mpeg1_default_intra_matrix[0];
        for(i=1;i<64;i++){
            int j= s->dsp.idct_permutation[i];

            s->intra_matrix[j] = CLAMP_TO_8BIT((ff_mpeg1_default_intra_matrix[i] * s->qscale) >> 3);
        }
        convert_matrix(&s->dsp, s->q_intra_matrix, s->q_intra_matrix16, 
                       s->intra_matrix, s->intra_quant_bias, 8, 8);
        s->qscale= 8;
    }
    
    //FIXME var duplication
    s->current_picture.key_frame= s->pict_type == I_TYPE; //FIXME pic_ptr
    s->current_picture.pict_type= s->pict_type;

    if(s->current_picture.key_frame)
        s->picture_in_gop_number=0;

    s->last_bits= put_bits_count(&s->pb);
    switch(s->out_format) {
    case FMT_MJPEG:
        mjpeg_picture_header(s);
        break;
#ifdef CONFIG_RISKY
    case FMT_H263:
        if (s->codec_id == CODEC_ID_WMV2) 
            ff_wmv2_encode_picture_header(s, picture_number);
        else if (s->h263_msmpeg4) 
            msmpeg4_encode_picture_header(s, picture_number);
        else if (s->h263_pred)
            mpeg4_encode_picture_header(s, picture_number);
        else if (s->codec_id == CODEC_ID_RV10) 
            rv10_encode_picture_header(s, picture_number);
        else if (s->codec_id == CODEC_ID_FLV1)
            ff_flv_encode_picture_header(s, picture_number);
        else
            h263_encode_picture_header(s, picture_number);
        break;
#endif
    case FMT_MPEG1:
        mpeg1_encode_picture_header(s, picture_number);
        break;
    case FMT_H264:
        break;
    default:
        assert(0);
    }
    bits= put_bits_count(&s->pb);
    s->header_bits= bits - s->last_bits;
        
    for(i=1; i<s->avctx->thread_count; i++){
        update_duplicate_context_after_me(s->thread_context[i], s);
    }
    s->avctx->execute(s->avctx, encode_thread, (void**)&(s->thread_context[0]), NULL, s->avctx->thread_count);
    for(i=1; i<s->avctx->thread_count; i++){
        merge_context_after_encode(s, s->thread_context[i]);
    }
    emms_c();
}

#endif //CONFIG_ENCODERS

static void  denoise_dct_c(MpegEncContext *s, DCTELEM *block){
    const int intra= s->mb_intra;
    int i;

    s->dct_count[intra]++;

    for(i=0; i<64; i++){
        int level= block[i];

        if(level){
            if(level>0){
                s->dct_error_sum[intra][i] += level;
                level -= s->dct_offset[intra][i];
                if(level<0) level=0;
            }else{
                s->dct_error_sum[intra][i] -= level;
                level += s->dct_offset[intra][i];
                if(level>0) level=0;
            }
            block[i]= level;
        }
    }
}

#ifdef CONFIG_ENCODERS

static int dct_quantize_trellis_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow){
    const int *qmat;
    const uint8_t *scantable= s->intra_scantable.scantable;
    const uint8_t *perm_scantable= s->intra_scantable.permutated;
    int max=0;
    unsigned int threshold1, threshold2;
    int bias=0;
    int run_tab[65];
    int level_tab[65];
    int score_tab[65];
    int survivor[65];
    int survivor_count;
    int last_run=0;
    int last_level=0;
    int last_score= 0;
    int last_i;
    int coeff[2][64];
    int coeff_count[64];
    int qmul, qadd, start_i, last_non_zero, i, dc;
    const int esc_length= s->ac_esc_length;
    uint8_t * length;
    uint8_t * last_length;
    const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);
        
    s->dsp.fdct (block);
    
    if(s->dct_error_sum)
        s->denoise_dct(s, block);
    qmul= qscale*16;
    qadd= ((qscale-1)|1)*8;

    if (s->mb_intra) {
        int q;
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
            q = q << 3;
        } else{
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1 << 3;
            qadd=0;
        }
            
        /* note: block[0] is assumed to be positive */
        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        last_non_zero = 0;
        qmat = s->q_intra_matrix[qscale];
        if(s->mpeg_quant || s->out_format == FMT_MPEG1)
            bias= 1<<(QMAT_SHIFT-1);
        length     = s->intra_ac_vlc_length;
        last_length= s->intra_ac_vlc_last_length;
    } else {
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_i= start_i;

    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);

    for(i=63; i>=start_i; i--) {
        const int j = scantable[i];
        int level = block[j] * qmat[j];

        if(((unsigned)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }
    }

    for(i=start_i; i<=last_non_zero; i++) {
        const int j = scantable[i];
        int level = block[j] * qmat[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QMAT_SHIFT;
                coeff[0][i]= level;
                coeff[1][i]= level-1;
//                coeff[2][k]= level-2;
            }else{
                level= (bias - level)>>QMAT_SHIFT;
                coeff[0][i]= -level;
                coeff[1][i]= -level+1;
//                coeff[2][k]= -level+2;
            }
            coeff_count[i]= FFMIN(level, 2);
            assert(coeff_count[i]);
            max |=level;
        }else{
            coeff[0][i]= (level>>31)|1;
            coeff_count[i]= 1;
        }
    }
    
    *overflow= s->max_qcoeff < max; //overflow might have happend
    
    if(last_non_zero < start_i){
        memset(block + start_i, 0, (64-start_i)*sizeof(DCTELEM));
        return last_non_zero;
    }

    score_tab[start_i]= 0;
    survivor[0]= start_i;
    survivor_count= 1;
    
    for(i=start_i; i<=last_non_zero; i++){
        int level_index, j;
        const int dct_coeff= ABS(block[ scantable[i] ]);
        const int zero_distoration= dct_coeff*dct_coeff;
        int best_score=256*256*256*120;
        for(level_index=0; level_index < coeff_count[i]; level_index++){
            int distoration;
            int level= coeff[level_index][i];
            const int alevel= ABS(level);
            int unquant_coeff;
            
            assert(level);

            if(s->out_format == FMT_H263){
                unquant_coeff= alevel*qmul + qadd;
            }else{ //MPEG1
                j= s->dsp.idct_permutation[ scantable[i] ]; //FIXME optimize
                if(s->mb_intra){
                        unquant_coeff = (int)(  alevel  * qscale * s->intra_matrix[j]) >> 3;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                }else{
                        unquant_coeff = (((  alevel  << 1) + 1) * qscale * ((int) s->inter_matrix[j])) >> 4;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                }
                unquant_coeff<<= 3;
            }

            distoration= (unquant_coeff - dct_coeff) * (unquant_coeff - dct_coeff) - zero_distoration;
            level+=64;
            if((level&(~127)) == 0){
                for(j=survivor_count-1; j>=0; j--){
                    int run= i - survivor[j];
                    int score= distoration + length[UNI_AC_ENC_INDEX(run, level)]*lambda;
                    score += score_tab[i-run];
                    
                    if(score < best_score){
                        best_score= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if(s->out_format == FMT_H263){
                    for(j=survivor_count-1; j>=0; j--){
                        int run= i - survivor[j];
                        int score= distoration + last_length[UNI_AC_ENC_INDEX(run, level)]*lambda;
                        score += score_tab[i-run];
                        if(score < last_score){
                            last_score= score;
                            last_run= run;
                            last_level= level-64;
                            last_i= i+1;
                        }
                    }
                }
            }else{
                distoration += esc_length*lambda;
                for(j=survivor_count-1; j>=0; j--){
                    int run= i - survivor[j];
                    int score= distoration + score_tab[i-run];
                    
                    if(score < best_score){
                        best_score= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if(s->out_format == FMT_H263){
                  for(j=survivor_count-1; j>=0; j--){
                        int run= i - survivor[j];
                        int score= distoration + score_tab[i-run];
                        if(score < last_score){
                            last_score= score;
                            last_run= run;
                            last_level= level-64;
                            last_i= i+1;
                        }
                    }
                }
            }
        }
        
        score_tab[i+1]= best_score;

        //Note: there is a vlc code in mpeg4 which is 1 bit shorter then another one with a shorter run and the same level
        if(last_non_zero <= 27){
            for(; survivor_count; survivor_count--){
                if(score_tab[ survivor[survivor_count-1] ] <= best_score)
                    break;
            }
        }else{
            for(; survivor_count; survivor_count--){
                if(score_tab[ survivor[survivor_count-1] ] <= best_score + lambda)
                    break;
            }
        }

        survivor[ survivor_count++ ]= i+1;
    }

    if(s->out_format != FMT_H263){
        last_score= 256*256*256*120;
        for(i= survivor[0]; i<=last_non_zero + 1; i++){
            int score= score_tab[i];
            if(i) score += lambda*2; //FIXME exacter?

            if(score < last_score){
                last_score= score;
                last_i= i;
                last_level= level_tab[i];
                last_run= run_tab[i];
            }
        }
    }

    s->coded_score[n] = last_score;
    
    dc= ABS(block[0]);
    last_non_zero= last_i - 1;
    memset(block + start_i, 0, (64-start_i)*sizeof(DCTELEM));
    
    if(last_non_zero < start_i)
        return last_non_zero;

    if(last_non_zero == 0 && start_i == 0){
        int best_level= 0;
        int best_score= dc * dc;
        
        for(i=0; i<coeff_count[0]; i++){
            int level= coeff[i][0];
            int alevel= ABS(level);
            int unquant_coeff, score, distortion;

            if(s->out_format == FMT_H263){
                    unquant_coeff= (alevel*qmul + qadd)>>3;
            }else{ //MPEG1
                    unquant_coeff = (((  alevel  << 1) + 1) * qscale * ((int) s->inter_matrix[0])) >> 4;
                    unquant_coeff =   (unquant_coeff - 1) | 1;
            }
            unquant_coeff = (unquant_coeff + 4) >> 3;
            unquant_coeff<<= 3 + 3;

            distortion= (unquant_coeff - dc) * (unquant_coeff - dc);
            level+=64;
            if((level&(~127)) == 0) score= distortion + last_length[UNI_AC_ENC_INDEX(0, level)]*lambda;
            else                    score= distortion + esc_length*lambda;

            if(score < best_score){
                best_score= score;
                best_level= level - 64;
            }
        }
        block[0]= best_level;
        s->coded_score[n] = best_score - dc*dc;
        if(best_level == 0) return -1;
        else                return last_non_zero;
    }

    i= last_i;
    assert(last_level);

    block[ perm_scantable[last_non_zero] ]= last_level;
    i -= last_run + 1;
    
    for(; i>start_i; i -= run_tab[i] + 1){
        block[ perm_scantable[i-1] ]= level_tab[i];
    }

    return last_non_zero;
}

//#define REFINE_STATS 1
static int16_t basis[64][64];

static void build_basis(uint8_t *perm){
    int i, j, x, y;
    emms_c();
    for(i=0; i<8; i++){
        for(j=0; j<8; j++){
            for(y=0; y<8; y++){
                for(x=0; x<8; x++){
                    double s= 0.25*(1<<BASIS_SHIFT);
                    int index= 8*i + j;
                    int perm_index= perm[index];
                    if(i==0) s*= sqrt(0.5);
                    if(j==0) s*= sqrt(0.5);
                    basis[perm_index][8*x + y]= lrintf(s * cos((M_PI/8.0)*i*(x+0.5)) * cos((M_PI/8.0)*j*(y+0.5)));
                }
            }
        }
    }
}

static int dct_quantize_refine(MpegEncContext *s, //FIXME breaks denoise?
                        DCTELEM *block, int16_t *weight, DCTELEM *orig,
                        int n, int qscale){
    int16_t rem[64];
    DCTELEM d1[64];
    const int *qmat;
    const uint8_t *scantable= s->intra_scantable.scantable;
    const uint8_t *perm_scantable= s->intra_scantable.permutated;
//    unsigned int threshold1, threshold2;
//    int bias=0;
    int run_tab[65];
    int prev_run=0;
    int prev_level=0;
    int qmul, qadd, start_i, last_non_zero, i, dc;
    uint8_t * length;
    uint8_t * last_length;
    int lambda;
    int rle_index, run, q, sum;
#ifdef REFINE_STATS
static int count=0;
static int after_last=0;
static int to_zero=0;
static int from_zero=0;
static int raise=0;
static int lower=0;
static int messed_sign=0;
#endif

    if(basis[0][0] == 0)
        build_basis(s->dsp.idct_permutation);
    
    qmul= qscale*2;
    qadd= (qscale-1)|1;
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
        } else{
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1;
            qadd=0;
        }
        q <<= RECON_SHIFT-3;
        /* note: block[0] is assumed to be positive */
        dc= block[0]*q;
//        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        qmat = s->q_intra_matrix[qscale];
//        if(s->mpeg_quant || s->out_format == FMT_MPEG1)
//            bias= 1<<(QMAT_SHIFT-1);
        length     = s->intra_ac_vlc_length;
        last_length= s->intra_ac_vlc_last_length;
    } else {
        dc= 0;
        start_i = 0;
        qmat = s->q_inter_matrix[qscale];
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_non_zero = s->block_last_index[n];

#ifdef REFINE_STATS
{START_TIMER
#endif
    dc += (1<<(RECON_SHIFT-1));
    for(i=0; i<64; i++){
        rem[i]= dc - (orig[i]<<RECON_SHIFT); //FIXME  use orig dirrectly insteadof copying to rem[]
    }
#ifdef REFINE_STATS
STOP_TIMER("memset rem[]")}
#endif
    sum=0;
    for(i=0; i<64; i++){
        int one= 36;
        int qns=4;
        int w;

        w= ABS(weight[i]) + qns*one;
        w= 15 + (48*qns*one + w/2)/w; // 16 .. 63

        weight[i] = w;
//        w=weight[i] = (63*qns + (w/2)) / w;
         
        assert(w>0);
        assert(w<(1<<6));
        sum += w*w;
    }
    lambda= sum*(uint64_t)s->lambda2 >> (FF_LAMBDA_SHIFT - 6 + 6 + 6 + 6);
#ifdef REFINE_STATS
{START_TIMER
#endif
    run=0;
    rle_index=0;
    for(i=start_i; i<=last_non_zero; i++){
        int j= perm_scantable[i];
        const int level= block[j];
        int coeff;
        
        if(level){
            if(level<0) coeff= qmul*level - qadd;
            else        coeff= qmul*level + qadd;
            run_tab[rle_index++]=run;
            run=0;

            s->dsp.add_8x8basis(rem, basis[j], coeff);
        }else{
            run++;
        }
    }
#ifdef REFINE_STATS
if(last_non_zero>0){
STOP_TIMER("init rem[]")
}
}

{START_TIMER
#endif
    for(;;){
        int best_score=s->dsp.try_8x8basis(rem, weight, basis[0], 0);
        int best_coeff=0;
        int best_change=0;
        int run2, best_unquant_change=0, analyze_gradient;
#ifdef REFINE_STATS
{START_TIMER
#endif
        analyze_gradient = last_non_zero > 2 || s->avctx->quantizer_noise_shaping >= 3;

        if(analyze_gradient){
#ifdef REFINE_STATS
{START_TIMER
#endif
            for(i=0; i<64; i++){
                int w= weight[i];
            
                d1[i] = (rem[i]*w*w + (1<<(RECON_SHIFT+12-1)))>>(RECON_SHIFT+12);
            }
#ifdef REFINE_STATS
STOP_TIMER("rem*w*w")}
{START_TIMER
#endif
            s->dsp.fdct(d1);
#ifdef REFINE_STATS
STOP_TIMER("dct")}
#endif
        }

        if(start_i){
            const int level= block[0];
            int change, old_coeff;

            assert(s->mb_intra);
            
            old_coeff= q*level;
            
            for(change=-1; change<=1; change+=2){
                int new_level= level + change;
                int score, new_coeff;
                
                new_coeff= q*new_level;
                if(new_coeff >= 2048 || new_coeff < 0)
                    continue;

                score= s->dsp.try_8x8basis(rem, weight, basis[0], new_coeff - old_coeff);
                if(score<best_score){
                    best_score= score;
                    best_coeff= 0;
                    best_change= change;
                    best_unquant_change= new_coeff - old_coeff;
                }
            }
        }
        
        run=0;
        rle_index=0;
        run2= run_tab[rle_index++];
        prev_level=0;
        prev_run=0;

        for(i=start_i; i<64; i++){
            int j= perm_scantable[i];
            const int level= block[j];
            int change, old_coeff;

            if(s->avctx->quantizer_noise_shaping < 3 && i > last_non_zero + 1)
                break;

            if(level){
                if(level<0) old_coeff= qmul*level - qadd;
                else        old_coeff= qmul*level + qadd;
                run2= run_tab[rle_index++]; //FIXME ! maybe after last
            }else{
                old_coeff=0;
                run2--;
                assert(run2>=0 || i >= last_non_zero );
            }
            
            for(change=-1; change<=1; change+=2){
                int new_level= level + change;
                int score, new_coeff, unquant_change;
                
                score=0;
                if(s->avctx->quantizer_noise_shaping < 2 && ABS(new_level) > ABS(level))
                   continue;

                if(new_level){
                    if(new_level<0) new_coeff= qmul*new_level - qadd;
                    else            new_coeff= qmul*new_level + qadd;
                    if(new_coeff >= 2048 || new_coeff <= -2048)
                        continue;
                    //FIXME check for overflow
                    
                    if(level){
                        if(level < 63 && level > -63){
                            if(i < last_non_zero)
                                score +=   length[UNI_AC_ENC_INDEX(run, new_level+64)]
                                         - length[UNI_AC_ENC_INDEX(run, level+64)];
                            else
                                score +=   last_length[UNI_AC_ENC_INDEX(run, new_level+64)]
                                         - last_length[UNI_AC_ENC_INDEX(run, level+64)];
                        }
                    }else{
                        assert(ABS(new_level)==1);
                        
                        if(analyze_gradient){
                            int g= d1[ scantable[i] ];
                            if(g && (g^new_level) >= 0)
                                continue;
                        }

                        if(i < last_non_zero){
                            int next_i= i + run2 + 1;
                            int next_level= block[ perm_scantable[next_i] ] + 64;
                            
                            if(next_level&(~127))
                                next_level= 0;

                            if(next_i < last_non_zero)
                                score +=   length[UNI_AC_ENC_INDEX(run, 65)]
                                         + length[UNI_AC_ENC_INDEX(run2, next_level)]
                                         - length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)];
                            else
                                score +=  length[UNI_AC_ENC_INDEX(run, 65)]
                                        + last_length[UNI_AC_ENC_INDEX(run2, next_level)]
                                        - last_length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)];
                        }else{
                            score += last_length[UNI_AC_ENC_INDEX(run, 65)];
                            if(prev_level){
                                score +=  length[UNI_AC_ENC_INDEX(prev_run, prev_level)]
                                        - last_length[UNI_AC_ENC_INDEX(prev_run, prev_level)];
                            }
                        }
                    }
                }else{
                    new_coeff=0;
                    assert(ABS(level)==1);

                    if(i < last_non_zero){
                        int next_i= i + run2 + 1;
                        int next_level= block[ perm_scantable[next_i] ] + 64;
                            
                        if(next_level&(~127))
                            next_level= 0;

                        if(next_i < last_non_zero)
                            score +=   length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run2, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run, 65)];
                        else
                            score +=   last_length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)]
                                     - last_length[UNI_AC_ENC_INDEX(run2, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run, 65)];
                    }else{
                        score += -last_length[UNI_AC_ENC_INDEX(run, 65)];
                        if(prev_level){
                            score +=  last_length[UNI_AC_ENC_INDEX(prev_run, prev_level)]
                                    - length[UNI_AC_ENC_INDEX(prev_run, prev_level)];
                        }
                    }
                }
                
                score *= lambda;

                unquant_change= new_coeff - old_coeff;
                assert((score < 100*lambda && score > -100*lambda) || lambda==0);
                
                score+= s->dsp.try_8x8basis(rem, weight, basis[j], unquant_change);
                if(score<best_score){
                    best_score= score;
                    best_coeff= i;
                    best_change= change;
                    best_unquant_change= unquant_change;
                }
            }
            if(level){
                prev_level= level + 64;
                if(prev_level&(~127))
                    prev_level= 0;
                prev_run= run;
                run=0;
            }else{
                run++;
            }
        }
#ifdef REFINE_STATS
STOP_TIMER("iterative step")}
#endif

        if(best_change){
            int j= perm_scantable[ best_coeff ];
            
            block[j] += best_change;
            
            if(best_coeff > last_non_zero){
                last_non_zero= best_coeff;
                assert(block[j]);
#ifdef REFINE_STATS
after_last++;
#endif
            }else{
#ifdef REFINE_STATS
if(block[j]){
    if(block[j] - best_change){
        if(ABS(block[j]) > ABS(block[j] - best_change)){
            raise++;
        }else{
            lower++;
        }
    }else{
        from_zero++;
    }
}else{
    to_zero++;
}
#endif
                for(; last_non_zero>=start_i; last_non_zero--){
                    if(block[perm_scantable[last_non_zero]])
                        break;
                }
            }
#ifdef REFINE_STATS
count++;
if(256*256*256*64 % count == 0){
    printf("after_last:%d to_zero:%d from_zero:%d raise:%d lower:%d sign:%d xyp:%d/%d/%d\n", after_last, to_zero, from_zero, raise, lower, messed_sign, s->mb_x, s->mb_y, s->picture_number);
}
#endif
            run=0;
            rle_index=0;
            for(i=start_i; i<=last_non_zero; i++){
                int j= perm_scantable[i];
                const int level= block[j];
        
                 if(level){
                     run_tab[rle_index++]=run;
                     run=0;
                 }else{
                     run++;
                 }
            }
            
            s->dsp.add_8x8basis(rem, basis[j], best_unquant_change);
        }else{
            break;
        }
    }
#ifdef REFINE_STATS
if(last_non_zero>0){
STOP_TIMER("iterative search")
}
}
#endif

    return last_non_zero;
}

static int dct_quantize_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow)
{
    int i, j, level, last_non_zero, q, start_i;
    const int *qmat;
    const uint8_t *scantable= s->intra_scantable.scantable;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;

    s->dsp.fdct (block);

    if(s->dct_error_sum)
        s->denoise_dct(s, block);

    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
            q = q << 3;
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1 << 3;
            
        /* note: block[0] is assumed to be positive */
        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        last_non_zero = 0;
        qmat = s->q_intra_matrix[qscale];
        bias= s->intra_quant_bias<<(QMAT_SHIFT - QUANT_BIAS_SHIFT);
    } else {
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        bias= s->inter_quant_bias<<(QMAT_SHIFT - QUANT_BIAS_SHIFT);
    }
    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);
    for(i=63;i>=start_i;i--) {
        j = scantable[i];
        level = block[j] * qmat[j];

        if(((unsigned)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }else{
            block[j]=0;
        }
    }
    for(i=start_i; i<=last_non_zero; i++) {
        j = scantable[i];
        level = block[j] * qmat[j];

//        if(   bias+level >= (1<<QMAT_SHIFT)
//           || bias-level >= (1<<QMAT_SHIFT)){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QMAT_SHIFT;
                block[j]= level;
            }else{
                level= (bias - level)>>QMAT_SHIFT;
                block[j]= -level;
            }
            max |=level;
        }else{
            block[j]=0;
        }
    }
    *overflow= s->max_qcoeff < max; //overflow might have happend
    
    /* we need this permutation so that we correct the IDCT, we only permute the !=0 elements */
    if (s->dsp.idct_permutation_type != FF_NO_IDCT_PERM)
	ff_block_permute(block, s->dsp.idct_permutation, scantable, last_non_zero);

    return last_non_zero;
}

#endif //CONFIG_ENCODERS

static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];
    
    if (n < 4) 
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
    /* XXX: only mpeg1 */
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = (level - 1) | 1;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = (level - 1) | 1;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg1_inter_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];
    
    quant_matrix = s->inter_matrix;
    for(i=0; i<=nCoeffs; i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = (level - 1) | 1;
                level = -level;
            } else {
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = (level - 1) | 1;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg2_intra_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];
    
    if (n < 4) 
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg2_inter_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;
    int sum=-1;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];
    
    quant_matrix = s->inter_matrix;
    for(i=0; i<=nCoeffs; i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = -level;
            } else {
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
            }
            block[j] = level;
            sum+=level;
        }
    }
    block[63]^=sum&1;
}

static void dct_unquantize_h263_intra_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);
    
    qmul = qscale << 1;
    
    if (!s->h263_aic) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    }else{
        qadd = 0;
    }
    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=1; i<=nCoeffs; i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[i] = level;
        }
    }
}

static void dct_unquantize_h263_inter_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);
    
    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;
    
    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=0; i<=nCoeffs; i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[i] = level;
        }
    }
}

static void dct_unquantize_h261_intra_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, even;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);
    
    if (n < 4) 
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
    even = (qscale & 1)^1;
    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=1; i<=nCoeffs; i++){
        level = block[i];
        if (level){
            if (level < 0){
                level = qscale * ((level << 1) - 1) + even;
            }else{
                level = qscale * ((level << 1) + 1) - even;
            }
        }
        block[i] = level;
    }
}

static void dct_unquantize_h261_inter_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, even;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);

    even = (qscale & 1)^1;
    
    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=0; i<=nCoeffs; i++){
        level = block[i];
        if (level){
            if (level < 0){
                level = qscale * ((level << 1) - 1) + even;
            }else{
                level = qscale * ((level << 1) + 1) - even;
            }
        }
        block[i] = level;
    }
}

static const AVOption mpeg4_options[] =
{
    AVOPTION_CODEC_INT("bitrate", "desired video bitrate", bit_rate, 4, 240000000, 800000),
    AVOPTION_CODEC_INT("ratetol", "number of bits the bitstream is allowed to diverge from the reference"
		       "the reference can be CBR (for CBR pass1) or VBR (for pass2)",
		       bit_rate_tolerance, 4, 240000000, 8000),
    AVOPTION_CODEC_INT("qmin", "minimum quantizer", qmin, 1, 31, 2),
    AVOPTION_CODEC_INT("qmax", "maximum quantizer", qmax, 1, 31, 31),
    AVOPTION_CODEC_STRING("rc_eq", "rate control equation",
			  rc_eq, "tex^qComp,option1,options2", 0),
    AVOPTION_CODEC_INT("rc_minrate", "rate control minimum bitrate",
		       rc_min_rate, 4, 24000000, 0),
    AVOPTION_CODEC_INT("rc_maxrate", "rate control maximum bitrate",
		       rc_max_rate, 4, 24000000, 0),
    AVOPTION_CODEC_DOUBLE("rc_buf_aggresivity", "rate control buffer aggresivity",
			  rc_buffer_aggressivity, 4, 24000000, 0),
    AVOPTION_CODEC_DOUBLE("rc_initial_cplx", "initial complexity for pass1 ratecontrol",
			  rc_initial_cplx, 0., 9999999., 0),
    AVOPTION_CODEC_DOUBLE("i_quant_factor", "qscale factor between p and i frames",
			  i_quant_factor, 0., 0., 0),
    AVOPTION_CODEC_DOUBLE("i_quant_offset", "qscale offset between p and i frames",
			  i_quant_factor, -999999., 999999., 0),
    AVOPTION_CODEC_INT("dct_algo", "dct alghorithm",
		       dct_algo, 0, 5, 0), // fixme - "Auto,FastInt,Int,MMX,MLib,Altivec"
    AVOPTION_CODEC_DOUBLE("lumi_masking", "luminance masking",
			  lumi_masking, 0., 999999., 0),
    AVOPTION_CODEC_DOUBLE("temporal_cplx_masking", "temporary complexity masking",
			  temporal_cplx_masking, 0., 999999., 0),
    AVOPTION_CODEC_DOUBLE("spatial_cplx_masking", "spatial complexity masking",
			  spatial_cplx_masking, 0., 999999., 0),
    AVOPTION_CODEC_DOUBLE("p_masking", "p block masking",
			  p_masking, 0., 999999., 0),
    AVOPTION_CODEC_DOUBLE("dark_masking", "darkness masking",
			  dark_masking, 0., 999999., 0),
    AVOPTION_CODEC_INT("idct_algo", "idct alghorithm",
		       idct_algo, 0, 8, 0), // fixme - "Auto,Int,Simple,SimpleMMX,LibMPEG2MMX,PS2,MLib,ARM,Altivec"

    AVOPTION_CODEC_INT("mb_qmin", "minimum MB quantizer",
		       mb_qmin, 0, 8, 0),
    AVOPTION_CODEC_INT("mb_qmax", "maximum MB quantizer",
		       mb_qmin, 0, 8, 0),

    AVOPTION_CODEC_INT("me_cmp", "ME compare function",
		       me_cmp, 0, 24000000, 0),
    AVOPTION_CODEC_INT("me_sub_cmp", "subpixel ME compare function",
		       me_sub_cmp, 0, 24000000, 0),


    AVOPTION_CODEC_INT("dia_size", "ME diamond size & shape",
		       dia_size, 0, 24000000, 0),
    AVOPTION_CODEC_INT("last_predictor_count", "amount of previous MV predictors",
		       last_predictor_count, 0, 24000000, 0),

    AVOPTION_CODEC_INT("pre_me", "pre pass for ME",
		       pre_me, 0, 24000000, 0),
    AVOPTION_CODEC_INT("me_pre_cmp", "ME pre pass compare function",
		       me_pre_cmp, 0, 24000000, 0),

    AVOPTION_CODEC_INT("me_range", "maximum ME search range",
		       me_range, 0, 24000000, 0),
    AVOPTION_CODEC_INT("pre_dia_size", "ME pre pass diamod size & shape",
		       pre_dia_size, 0, 24000000, 0),
    AVOPTION_CODEC_INT("me_subpel_quality", "subpel ME quality",
		       me_subpel_quality, 0, 24000000, 0),
    AVOPTION_CODEC_INT("me_range", "maximum ME search range",
		       me_range, 0, 24000000, 0),
    AVOPTION_CODEC_FLAG("psnr", "calculate PSNR of compressed frames",
		        flags, CODEC_FLAG_PSNR, 0),
    AVOPTION_CODEC_RCOVERRIDE("rc_override", "ratecontrol override (=startframe,endframe,qscale,quality_factor)",
			      rc_override),
    AVOPTION_SUB(avoptions_common),
    AVOPTION_END()
};

#ifdef CONFIG_ENCODERS
#ifdef CONFIG_RISKY
AVCodec h263_encoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec h263p_encoder = {
    "h263p",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263P,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec flv_encoder = {
    "flv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FLV1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec rv10_encoder = {
    "rv10",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RV10,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec mpeg4_encoder = {
    "mpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG4,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .options = mpeg4_options,
    .capabilities= CODEC_CAP_DELAY,
};

AVCodec msmpeg4v1_encoder = {
    "msmpeg4v1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .options = mpeg4_options,
};

AVCodec msmpeg4v2_encoder = {
    "msmpeg4v2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V2,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .options = mpeg4_options,
};

AVCodec msmpeg4v3_encoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V3,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .options = mpeg4_options,
};

AVCodec wmv1_encoder = {
    "wmv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
    .options = mpeg4_options,
};

#endif

AVCodec mjpeg_encoder = {
    "mjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEG,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

#endif //CONFIG_ENCODERS
