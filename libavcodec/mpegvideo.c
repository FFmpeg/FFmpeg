/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard.
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
 
#include <ctype.h>
#include <limits.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

//#undef NDEBUG
//#include <assert.h>

#ifdef CONFIG_ENCODERS
static void encode_picture(MpegEncContext *s, int picture_number);
#endif //CONFIG_ENCODERS
static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg2_c(MpegEncContext *s,
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void draw_edges_c(uint8_t *buf, int wrap, int width, int height, int w);
#ifdef CONFIG_ENCODERS
static int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
static int dct_quantize_trellis_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);
#endif //CONFIG_ENCODERS

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

#ifdef CONFIG_ENCODERS
static uint8_t (*default_mv_penalty)[MAX_MV*2+1]=NULL;
static uint8_t default_fcode_tab[MAX_MV*2+1];

enum PixelFormat ff_yuv420p_list[2]= {PIX_FMT_YUV420P, -1};

static void convert_matrix(MpegEncContext *s, int (*qmat)[64], uint16_t (*qmat16)[64], uint16_t (*qmat16_bias)[64],
                           const uint16_t *quant_matrix, int bias, int qmin, int qmax)
{
    int qscale;

    for(qscale=qmin; qscale<=qmax; qscale++){
        int i;
        if (s->dsp.fdct == ff_jpeg_fdct_islow) {
            for(i=0;i<64;i++) {
                const int j= s->dsp.idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((uint64_t_C(1) << QMAT_SHIFT) / 
                                (qscale * quant_matrix[j]));
            }
        } else if (s->dsp.fdct == fdct_ifast) {
            for(i=0;i<64;i++) {
                const int j= s->dsp.idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((uint64_t_C(1) << (QMAT_SHIFT + 14)) / 
                                (aanscales[i] * qscale * quant_matrix[j]));
            }
        } else {
            for(i=0;i<64;i++) {
                const int j= s->dsp.idct_permutation[i];
                /* We can safely suppose that 16 <= quant_matrix[i] <= 255
                   So 16           <= qscale * quant_matrix[i]             <= 7905
                   so (1<<19) / 16 >= (1<<19) / (qscale * quant_matrix[i]) >= (1<<19) / 7905
                   so 32768        >= (1<<19) / (qscale * quant_matrix[i]) >= 67
                */
                qmat[qscale][i] = (int)((uint64_t_C(1) << QMAT_SHIFT) / (qscale * quant_matrix[j]));
//                qmat  [qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
                qmat16[qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[j]);

                if(qmat16[qscale][i]==0 || qmat16[qscale][i]==128*256) qmat16[qscale][i]=128*256-1;
                qmat16_bias[qscale][i]= ROUNDED_DIV(bias<<(16-QUANT_BIAS_SHIFT), qmat16[qscale][i]);
            }
        }
    }
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

/* init common dct for both encoder and decoder */
int DCT_common_init(MpegEncContext *s)
{
    s->dct_unquantize_h263 = dct_unquantize_h263_c;
    s->dct_unquantize_mpeg1 = dct_unquantize_mpeg1_c;
    s->dct_unquantize_mpeg2 = dct_unquantize_mpeg2_c;

#ifdef CONFIG_ENCODERS
    s->dct_quantize= dct_quantize_c;
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
    ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_zigzag_direct);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_zigzag_direct);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_alternate_horizontal_scan);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);

    s->picture_structure= PICT_FRAME;
    
    return 0;
}

/**
 * allocates a Picture
 * The pixels are allocated/set by calling get_buffer() if shared=0
 */
static int alloc_picture(MpegEncContext *s, Picture *pic, int shared){
    const int big_mb_num= s->mb_stride*(s->mb_height+1) + 1; //the +1 is needed so memset(,,stride*height) doesnt sig11
    const int mb_array_size= s->mb_stride*s->mb_height;
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
            fprintf(stderr, "get_buffer() failed (%d %d %d %p)\n", r, pic->age, pic->type, pic->data[0]);
            return -1;
        }

        if(s->linesize && (s->linesize != pic->linesize[0] || s->uvlinesize != pic->linesize[1])){
            fprintf(stderr, "get_buffer() failed (stride changed)\n");
            return -1;
        }

        if(pic->linesize[1] != pic->linesize[2]){
            fprintf(stderr, "get_buffer() failed (uv stride missmatch)\n");
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
            CHECKED_ALLOCZ(pic->mb_cmp_score, mb_array_size * sizeof(int32_t))
        }

        CHECKED_ALLOCZ(pic->mbskip_table , mb_array_size * sizeof(uint8_t)+2) //the +2 is for the slice end check
        CHECKED_ALLOCZ(pic->qscale_table , mb_array_size * sizeof(uint8_t))
        CHECKED_ALLOCZ(pic->mb_type_base , big_mb_num    * sizeof(int))
        pic->mb_type= pic->mb_type_base + s->mb_stride+1;
        if(s->out_format == FMT_H264){
            for(i=0; i<2; i++){
                CHECKED_ALLOCZ(pic->motion_val[i], 2 * 16 * s->mb_num * sizeof(uint16_t))
                CHECKED_ALLOCZ(pic->ref_index[i] , 4 * s->mb_num * sizeof(uint8_t))
            }
        }
        pic->qstride= s->mb_stride;
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
    av_freep(&pic->mb_cmp_score);
    av_freep(&pic->mbskip_table);
    av_freep(&pic->qscale_table);
    av_freep(&pic->mb_type_base);
    pic->mb_type= NULL;
    for(i=0; i<2; i++){
        av_freep(&pic->motion_val[i]);
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

/* init common structure for both encoder and decoder */
int MPV_common_init(MpegEncContext *s)
{
    int y_size, c_size, yc_size, i, mb_array_size, x, y;

    dsputil_init(&s->dsp, s->avctx);
    DCT_common_init(s);

    s->flags= s->avctx->flags;

    s->mb_width  = (s->width  + 15) / 16;
    s->mb_height = (s->height + 15) / 16;
    s->mb_stride = s->mb_width + 1;
    mb_array_size= s->mb_height * s->mb_stride;

    /* set default edge pos, will be overriden in decode_header if needed */
    s->h_edge_pos= s->mb_width*16;
    s->v_edge_pos= s->mb_height*16;

    s->mb_num = s->mb_width * s->mb_height;
    
    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->mb_width*2 + 2;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_width + 2;

    y_size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
    c_size = (s->mb_width + 2) * (s->mb_height + 2);
    yc_size = y_size + 2 * c_size;

    /* convert fourcc to upper case */
    s->avctx->codec_tag=   toupper( s->avctx->codec_tag     &0xFF)          
                        + (toupper((s->avctx->codec_tag>>8 )&0xFF)<<8 )
                        + (toupper((s->avctx->codec_tag>>16)&0xFF)<<16) 
                        + (toupper((s->avctx->codec_tag>>24)&0xFF)<<24);

    CHECKED_ALLOCZ(s->allocated_edge_emu_buffer, (s->width+64)*2*17*2); //(width + edge + align)*interlaced*MBsize*tolerance
    s->edge_emu_buffer= s->allocated_edge_emu_buffer + (s->width+64)*2*17;

    s->avctx->coded_frame= (AVFrame*)&s->current_picture;

    CHECKED_ALLOCZ(s->mb_index2xy, (s->mb_num+1)*sizeof(int)) //error ressilience code looks cleaner with this
    for(y=0; y<s->mb_height; y++){
        for(x=0; x<s->mb_width; x++){
            s->mb_index2xy[ x + y*s->mb_width ] = x + y*s->mb_stride;
        }
    }
    s->mb_index2xy[ s->mb_height*s->mb_width ] = (s->mb_height-1)*s->mb_stride + s->mb_width; //FIXME really needed?
    
    if (s->encoding) {
        int mv_table_size= s->mb_stride * (s->mb_height+2) + 1;

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

        //FIXME should be linesize instead of s->width*2 but that isnt known before get_buffer()
        CHECKED_ALLOCZ(s->me.scratchpad,  s->width*2*16*3*sizeof(uint8_t)) 
        
        CHECKED_ALLOCZ(s->me.map      , ME_MAP_SIZE*sizeof(uint32_t))
        CHECKED_ALLOCZ(s->me.score_map, ME_MAP_SIZE*sizeof(uint32_t))

        if(s->codec_id==CODEC_ID_MPEG4){
            CHECKED_ALLOCZ(s->tex_pb_buffer, PB_BUFFER_SIZE);
            CHECKED_ALLOCZ(   s->pb2_buffer, PB_BUFFER_SIZE);
        }
        
        if(s->msmpeg4_version){
            CHECKED_ALLOCZ(s->ac_stats, 2*2*(MAX_LEVEL+1)*(MAX_RUN+1)*2*sizeof(int));
        }
        CHECKED_ALLOCZ(s->avctx->stats_out, 256);

        /* Allocate MB type table */
        CHECKED_ALLOCZ(s->mb_type  , mb_array_size * sizeof(uint8_t)) //needed for encoding
    }
        
    CHECKED_ALLOCZ(s->error_status_table, mb_array_size*sizeof(uint8_t))
    
    if (s->out_format == FMT_H263 || s->encoding) {
        int size;

        /* MV prediction */
        size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        CHECKED_ALLOCZ(s->motion_val, size * 2 * sizeof(int16_t));
    }

    if(s->codec_id==CODEC_ID_MPEG4){
        /* interlaced direct mode decoding tables */
        CHECKED_ALLOCZ(s->field_mv_table, mb_array_size*2*2 * sizeof(int16_t))
        CHECKED_ALLOCZ(s->field_select_table, mb_array_size*2* sizeof(int8_t))
    }
    if (s->out_format == FMT_H263) {
        /* ac values */
        CHECKED_ALLOCZ(s->ac_val[0], yc_size * sizeof(int16_t) * 16);
        s->ac_val[1] = s->ac_val[0] + y_size;
        s->ac_val[2] = s->ac_val[1] + c_size;
        
        /* cbp values */
        CHECKED_ALLOCZ(s->coded_block, y_size);
        
        /* divx501 bitstream reorder buffer */
        CHECKED_ALLOCZ(s->bitstream_buffer, BITSTREAM_BUFFER_SIZE);

        /* cbp, ac_pred, pred_dir */
        CHECKED_ALLOCZ(s->cbp_table  , mb_array_size * sizeof(uint8_t))
        CHECKED_ALLOCZ(s->pred_dir_table, mb_array_size * sizeof(uint8_t))
    }
    
    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        //MN: we need these for error resilience of intra-frames
        CHECKED_ALLOCZ(s->dc_val[0], yc_size * sizeof(int16_t));
        s->dc_val[1] = s->dc_val[0] + y_size;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for(i=0;i<yc_size;i++)
            s->dc_val[0][i] = 1024;
    }

    /* which mb is a intra block */
    CHECKED_ALLOCZ(s->mbintra_table, mb_array_size);
    memset(s->mbintra_table, 1, mb_array_size);
    
    /* default structure is frame */
    s->picture_structure = PICT_FRAME;
    
    /* init macroblock skip table */
    CHECKED_ALLOCZ(s->mbskip_table, mb_array_size+2);
    //Note the +1 is for a quicker mpeg4 slice_end detection
    CHECKED_ALLOCZ(s->prev_pict_types, PREV_PICT_TYPES_BUFFER_SIZE);
    
    s->block= s->blocks[0];

    s->parse_context.state= -1;

    s->context_initialized = 1;
    return 0;
 fail:
    MPV_common_end(s);
    return -1;
}


//extern int sads;

/* init common structure for both encoder and decoder */
void MPV_common_end(MpegEncContext *s)
{
    int i;

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
    
    av_freep(&s->motion_val);
    av_freep(&s->dc_val[0]);
    av_freep(&s->ac_val[0]);
    av_freep(&s->coded_block);
    av_freep(&s->mbintra_table);
    av_freep(&s->cbp_table);
    av_freep(&s->pred_dir_table);
    av_freep(&s->me.scratchpad);
    av_freep(&s->me.map);
    av_freep(&s->me.score_map);
    
    av_freep(&s->mbskip_table);
    av_freep(&s->prev_pict_types);
    av_freep(&s->bitstream_buffer);
    av_freep(&s->tex_pb_buffer);
    av_freep(&s->pb2_buffer);
    av_freep(&s->allocated_edge_emu_buffer); s->edge_emu_buffer= NULL;
    av_freep(&s->field_mv_table);
    av_freep(&s->field_select_table);
    av_freep(&s->avctx->stats_out);
    av_freep(&s->ac_stats);
    av_freep(&s->error_status_table);
    av_freep(&s->mb_index2xy);

    for(i=0; i<MAX_PICTURE_COUNT; i++){
        free_picture(s, &s->picture[i]);
    }
    avcodec_default_free_buffers(s->avctx);
    s->context_initialized = 0;
}

#ifdef CONFIG_ENCODERS

/* init video encoder */
int MPV_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;
    int chroma_h_shift, chroma_v_shift;

    s->bit_rate = avctx->bit_rate;
    s->bit_rate_tolerance = avctx->bit_rate_tolerance;
    s->width = avctx->width;
    s->height = avctx->height;
    if(avctx->gop_size > 600){
        fprintf(stderr, "Warning keyframe interval too large! reducing it ...\n");
        avctx->gop_size=600;
    }
    s->gop_size = avctx->gop_size;
    s->rtp_mode = avctx->rtp_mode;
    s->rtp_payload_size = avctx->rtp_payload_size;
    if (avctx->rtp_callback)
        s->rtp_callback = avctx->rtp_callback;
    s->max_qdiff= avctx->max_qdiff;
    s->qcompress= avctx->qcompress;
    s->qblur= avctx->qblur;
    s->avctx = avctx;
    s->flags= avctx->flags;
    s->max_b_frames= avctx->max_b_frames;
    s->b_frame_strategy= avctx->b_frame_strategy;
    s->codec_id= avctx->codec->id;
    s->luma_elim_threshold  = avctx->luma_elim_threshold;
    s->chroma_elim_threshold= avctx->chroma_elim_threshold;
    s->strict_std_compliance= avctx->strict_std_compliance;
    s->data_partitioning= avctx->flags & CODEC_FLAG_PART;
    s->quarter_sample= (avctx->flags & CODEC_FLAG_QPEL)!=0;
    s->mpeg_quant= avctx->mpeg_quant;

    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }

    s->me_method = avctx->me_method;

    /* Fixed QSCALE */
    s->fixed_qscale = (avctx->flags & CODEC_FLAG_QSCALE);
    
    s->adaptive_quant= (   s->avctx->lumi_masking
                        || s->avctx->dark_masking
                        || s->avctx->temporal_cplx_masking 
                        || s->avctx->spatial_cplx_masking
                        || s->avctx->p_masking)
                       && !s->fixed_qscale;
    
    s->progressive_sequence= !(avctx->flags & CODEC_FLAG_INTERLACED_DCT);

    if((s->flags & CODEC_FLAG_4MV) && s->codec_id != CODEC_ID_MPEG4){
        fprintf(stderr, "4MV not supporetd by codec\n");
        return -1;
    }
    
    if(s->quarter_sample && s->codec_id != CODEC_ID_MPEG4){
        fprintf(stderr, "qpel not supporetd by codec\n");
        return -1;
    }

    if(s->data_partitioning && s->codec_id != CODEC_ID_MPEG4){
        fprintf(stderr, "data partitioning not supporetd by codec\n");
        return -1;
    }
    
    if(s->max_b_frames && s->codec_id != CODEC_ID_MPEG4 && s->codec_id != CODEC_ID_MPEG1VIDEO){
        fprintf(stderr, "b frames not supporetd by codec\n");
        return -1;
    }
    
    if(s->mpeg_quant && s->codec_id != CODEC_ID_MPEG4){ //FIXME mpeg2 uses that too
        fprintf(stderr, "mpeg2 style quantization not supporetd by codec\n");
        return -1;
    }
        
    if(s->codec_id==CODEC_ID_MJPEG){
        s->intra_quant_bias= 1<<(QUANT_BIAS_SHIFT-1); //(a + x/2)/x
        s->inter_quant_bias= 0;
    }else if(s->mpeg_quant || s->codec_id==CODEC_ID_MPEG1VIDEO){
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

    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        s->low_delay= 0; //s->max_b_frames ? 0 : 1;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1);
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
            printf("Input picture size isn't suitable for h263 codec! try h263+\n");
            return -1;
        }
        s->out_format = FMT_H263;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->h263_plus = 1;
	/* Fx */
	s->unrestricted_mv=(avctx->flags & CODEC_FLAG_H263P_UMV) ? 1:0;
	s->h263_aic= (avctx->flags & CODEC_FLAG_H263P_AIC) ? 1:0;
	/* /Fx */
        /* These are just to be sure */
        s->umvplus = 1;
        avctx->delay=0;
        s->low_delay=1;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
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
    
    { /* set up some save defaults, some codecs might override them later */
        static int done=0;
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
    }
    s->me.mv_penalty= default_mv_penalty;
    s->fcode_tab= default_fcode_tab;
    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
 
    /* dont use mv_penalty table for crap MV as it would be confused */
    //FIXME remove after fixing / removing old ME
    if (s->me_method < ME_EPZS) s->me.mv_penalty = default_mv_penalty;

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;
    
    ff_init_me(s);

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

    /* init default q matrix */
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
        { /* mpeg1 */
            s->intra_matrix[j] = ff_mpeg1_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        }
    }

    /* precompute matrix */
    /* for mjpeg, we do include qscale in the matrix */
    if (s->out_format != FMT_MJPEG) {
        convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16, s->q_intra_matrix16_bias, 
                       s->intra_matrix, s->intra_quant_bias, 1, 31);
        convert_matrix(s, s->q_inter_matrix, s->q_inter_matrix16, s->q_inter_matrix16_bias, 
                       s->inter_matrix, s->inter_quant_bias, 1, 31);
    }

    if(ff_rate_control_init(s) < 0)
        return -1;

    s->picture_number = 0;
    s->picture_in_gop_number = 0;
    s->fake_picture_number = 0;
    /* motion detector init */
    s->f_code = 1;
    s->b_code = 1;

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

static int find_unused_picture(MpegEncContext *s, int shared){
    int i;
    
    if(shared){
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL && s->picture[i].type==0) break;
        }
    }else{
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL && s->picture[i].type!=0) break; //FIXME
        }
        for(i=0; i<MAX_PICTURE_COUNT; i++){
            if(s->picture[i].data[0]==NULL) break;
        }
    }

    assert(i<MAX_PICTURE_COUNT);
    return i;
}

/* generic function for encode/decode called before a frame is coded/decoded */
int MPV_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int i;
    AVFrame *pic;

    s->mb_skiped = 0;

    assert(s->last_picture_ptr==NULL || s->out_format != FMT_H264 || s->codec_id == CODEC_ID_SVQ3);

    /* mark&release old frames */
    if (s->pict_type != B_TYPE && s->last_picture_ptr) {
        avctx->release_buffer(avctx, (AVFrame*)s->last_picture_ptr);

        /* release forgotten pictures */
        /* if(mpeg124/h263) */
        if(!s->encoding){
            for(i=0; i<MAX_PICTURE_COUNT; i++){
                if(s->picture[i].data[0] && &s->picture[i] != s->next_picture_ptr && s->picture[i].reference){
                    fprintf(stderr, "releasing zombie picture\n");
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

        i= find_unused_picture(s, 0);
    
        pic= (AVFrame*)&s->picture[i];
        pic->reference= s->pict_type != B_TYPE ? 3 : 0;

        if(s->current_picture_ptr)
            pic->coded_picture_number= s->current_picture_ptr->coded_picture_number+1;
        
        alloc_picture(s, (Picture*)pic, 0);

        s->current_picture_ptr= &s->picture[i];
    }

    s->current_picture_ptr->pict_type= s->pict_type;
    s->current_picture_ptr->quality= s->qscale;
    s->current_picture_ptr->key_frame= s->pict_type == I_TYPE;

    s->current_picture= *s->current_picture_ptr;
  
  if(s->out_format != FMT_H264 || s->codec_id == CODEC_ID_SVQ3){
    if (s->pict_type != B_TYPE) {
        s->last_picture_ptr= s->next_picture_ptr;
        s->next_picture_ptr= s->current_picture_ptr;
    }
    
    if(s->last_picture_ptr) s->last_picture= *s->last_picture_ptr;
    if(s->next_picture_ptr) s->next_picture= *s->next_picture_ptr;
    if(s->new_picture_ptr ) s->new_picture = *s->new_picture_ptr;
    
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
    
    if(s->pict_type != I_TYPE && s->last_picture_ptr==NULL){
        fprintf(stderr, "warning: first frame is no keyframe\n");
        assert(s->pict_type != B_TYPE); //these should have been dropped if we dont have a reference
        goto alloc;
    }
  }
   
    s->hurry_up= s->avctx->hurry_up;
    s->error_resilience= avctx->error_resilience;

    /* set dequantizer, we cant do it during init as it might change for mpeg4
       and we cant do it in the header decode as init isnt called for mpeg4 there yet */
    if(s->out_format == FMT_H263){
        if(s->mpeg_quant)
            s->dct_unquantize = s->dct_unquantize_mpeg2;
        else
            s->dct_unquantize = s->dct_unquantize_h263;
    }else 
        s->dct_unquantize = s->dct_unquantize_mpeg1;

    return 0;
}

/* generic function for encode/decode called after a frame has been coded/decoded */
void MPV_frame_end(MpegEncContext *s)
{
    int i;
    /* draw edge for correct motion prediction if outside */
    if(s->codec_id!=CODEC_ID_SVQ1){
        if (s->pict_type != B_TYPE && !s->intra_only && !(s->flags&CODEC_FLAG_EMU_EDGE)) {
            draw_edges(s->current_picture.data[0], s->linesize  , s->h_edge_pos   , s->v_edge_pos   , EDGE_WIDTH  );
            draw_edges(s->current_picture.data[1], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
            draw_edges(s->current_picture.data[2], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
        }
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
    int t, x, y, f;
    
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
            y= ((x*f) + (1<<15))>>16;
            buf[y*stride + x]+= color;
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
            x= ((y*f) + (1<<15))>>16;
            buf[y*stride + x]+= color;
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
void ff_print_debug_info(MpegEncContext *s, Picture *pict){

    if(!pict || !pict->mb_type) return;

    if(s->avctx->debug&(FF_DEBUG_SKIP | FF_DEBUG_QP | FF_DEBUG_MB_TYPE)){
        int x,y;

        for(y=0; y<s->mb_height; y++){
            for(x=0; x<s->mb_width; x++){
                if(s->avctx->debug&FF_DEBUG_SKIP){
                    int count= s->mbskip_table[x + y*s->mb_stride];
                    if(count>9) count=9;
                    printf("%1d", count);
                }
                if(s->avctx->debug&FF_DEBUG_QP){
                    printf("%2d", pict->qscale_table[x + y*s->mb_stride]);
                }
                if(s->avctx->debug&FF_DEBUG_MB_TYPE){
                    int mb_type= pict->mb_type[x + y*s->mb_stride];
                    
                    //Type & MV direction
                    if(IS_PCM(mb_type))
                        printf("P");
                    else if(IS_INTRA(mb_type) && IS_ACPRED(mb_type))
                        printf("A");
                    else if(IS_INTRA4x4(mb_type))
                        printf("i");
                    else if(IS_INTRA16x16(mb_type))
                        printf("I");
                    else if(IS_DIRECT(mb_type) && IS_SKIP(mb_type))
                        printf("d");
                    else if(IS_DIRECT(mb_type))
                        printf("D");
                    else if(IS_GMC(mb_type) && IS_SKIP(mb_type))
                        printf("g");
                    else if(IS_GMC(mb_type))
                        printf("G");
                    else if(IS_SKIP(mb_type))
                        printf("S");
                    else if(!USES_LIST(mb_type, 1))
                        printf(">");
                    else if(!USES_LIST(mb_type, 0))
                        printf("<");
                    else{
                        assert(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        printf("X");
                    }
                    
                    //segmentation
                    if(IS_8X8(mb_type))
                        printf("+");
                    else if(IS_16X8(mb_type))
                        printf("-");
                    else if(IS_8X16(mb_type))
                        printf("¦");
                    else if(IS_INTRA(mb_type) || IS_16X16(mb_type))
                        printf(" ");
                    else
                        printf("?");
                    
                        
                    if(IS_INTERLACED(mb_type) && s->codec_id == CODEC_ID_H264)
                        printf("=");
                    else
                        printf(" ");
                }
//                printf(" ");
            }
            printf("\n");
        }
    }
    
    if((s->avctx->debug&FF_DEBUG_VIS_MV) && s->motion_val){
        const int shift= 1 + s->quarter_sample;
        int mb_y;
        uint8_t *ptr= pict->data[0];
        s->low_delay=0; //needed to see the vectors without trashing the buffers

        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            int mb_x;
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                const int mb_index= mb_x + mb_y*s->mb_stride;
                if(IS_8X8(s->current_picture.mb_type[mb_index])){
                    int i;
                    for(i=0; i<4; i++){
                        int sx= mb_x*16 + 4 + 8*(i&1);
                        int sy= mb_y*16 + 4 + 8*(i>>1);
                        int xy= 1 + mb_x*2 + (i&1) + (mb_y*2 + 1 + (i>>1))*(s->mb_width*2 + 2);
                        int mx= (s->motion_val[xy][0]>>shift) + sx;
                        int my= (s->motion_val[xy][1]>>shift) + sy;
                        draw_arrow(ptr, sx, sy, mx, my, s->width, s->height, s->linesize, 100);
                    }
                }else{
                    int sx= mb_x*16 + 8;
                    int sy= mb_y*16 + 8;
                    int xy= 1 + mb_x*2 + (mb_y*2 + 1)*(s->mb_width*2 + 2);
                    int mx= (s->motion_val[xy][0]>>shift) + sx;
                    int my= (s->motion_val[xy][1]>>shift) + sy;
                    draw_arrow(ptr, sx, sy, mx, my, s->width, s->height, s->linesize, 100);
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
            int sad = s->dsp.pix_abs16x16(src + offset, ref + offset, stride);
            int mean= (s->dsp.pix_sum(src + offset, stride) + 128)>>8;
            int sae = get_sae(src + offset, mean, stride);
            
            acc+= sae + 500 < sad;
        }
    }
    return acc;
}


static int load_input_picture(MpegEncContext *s, AVFrame *pic_arg){
    AVFrame *pic;
    int i;
    const int encoding_delay= s->max_b_frames;
    int direct=1;

    if(encoding_delay && !(s->flags&CODEC_FLAG_INPUT_PRESERVED)) direct=0;
    if(pic_arg->linesize[0] != s->linesize) direct=0;
    if(pic_arg->linesize[1] != s->uvlinesize) direct=0;
    if(pic_arg->linesize[2] != s->uvlinesize) direct=0;
  
//    printf("%d %d %d %d\n",pic_arg->linesize[0], pic_arg->linesize[1], s->linesize, s->uvlinesize);
    
    if(direct){
        i= find_unused_picture(s, 1);

        pic= (AVFrame*)&s->picture[i];
        pic->reference= 3;
    
        for(i=0; i<4; i++){
            pic->data[i]= pic_arg->data[i];
            pic->linesize[i]= pic_arg->linesize[i];
        }
        alloc_picture(s, (Picture*)pic, 1);
    }else{
        i= find_unused_picture(s, 0);

        pic= (AVFrame*)&s->picture[i];
        pic->reference= 3;

        alloc_picture(s, (Picture*)pic, 0);
        for(i=0; i<4; i++){
            /* the input will be 16 pixels to the right relative to the actual buffer start
             * and the current_pic, so the buffer can be reused, yes its not beatifull 
             */
            pic->data[i]+= 16; 
        }

        if(   pic->data[0] == pic_arg->data[0] 
           && pic->data[1] == pic_arg->data[1]
           && pic->data[2] == pic_arg->data[2]){
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
                uint8_t *dst= pic->data[i];
            
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
    pic->quality= pic_arg->quality;
    pic->pict_type= pic_arg->pict_type;
    pic->pts = pic_arg->pts;
    
    if(s->input_picture[encoding_delay])
        pic->display_picture_number= s->input_picture[encoding_delay]->display_picture_number + 1;

    /* shift buffer entries */
    for(i=1; i<MAX_PICTURE_COUNT /*s->encoding_delay+1*/; i++)
        s->input_picture[i-1]= s->input_picture[i];
        
    s->input_picture[encoding_delay]= (Picture*)pic;

    return 0;
}

static void select_input_picture(MpegEncContext *s){
    int i;
    const int encoding_delay= s->max_b_frames;
    int coded_pic_num=0;    

    if(s->reordered_input_picture[0])
        coded_pic_num= s->reordered_input_picture[0]->coded_picture_number + 1;

    for(i=1; i<MAX_PICTURE_COUNT; i++)
        s->reordered_input_picture[i-1]= s->reordered_input_picture[i];
    s->reordered_input_picture[MAX_PICTURE_COUNT-1]= NULL;

    /* set next picture types & ordering */
    if(s->reordered_input_picture[0]==NULL && s->input_picture[0]){
        if(/*s->picture_in_gop_number >= s->gop_size ||*/ s->next_picture_ptr==NULL || s->intra_only){
            s->reordered_input_picture[0]= s->input_picture[0];
            s->reordered_input_picture[0]->pict_type= I_TYPE;
            s->reordered_input_picture[0]->coded_picture_number= coded_pic_num;
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
                    fprintf(stderr, "warning, too many bframes in a row\n");
                    b_frames = s->max_b_frames;
                }
            }else if(s->b_frame_strategy==0){
                b_frames= s->max_b_frames;
            }else if(s->b_frame_strategy==1){
                for(i=1; i<s->max_b_frames+1; i++){
                    if(s->input_picture[i]->b_frame_score==0){
                        s->input_picture[i]->b_frame_score= 
                            get_intra_count(s, s->input_picture[i  ]->data[0], 
                                               s->input_picture[i-1]->data[0], s->linesize) + 1;
                    }
                }
                for(i=0; i<s->max_b_frames; i++){
                    if(s->input_picture[i]->b_frame_score - 1 > s->mb_num/40) break;
                }
                                
                b_frames= FFMAX(0, i-1);
                
                /* reset scores */
                for(i=0; i<b_frames+1; i++){
                    s->input_picture[i]->b_frame_score=0;
                }
            }else{
                fprintf(stderr, "illegal b frame strategy\n");
                b_frames=0;
            }

            emms_c();
//static int b_count=0;
//b_count+= b_frames;
//printf("b_frames: %d\n", b_count);
                        
            s->reordered_input_picture[0]= s->input_picture[b_frames];
            if(   s->picture_in_gop_number + b_frames >= s->gop_size 
               || s->reordered_input_picture[0]->pict_type== I_TYPE)
                s->reordered_input_picture[0]->pict_type= I_TYPE;
            else
                s->reordered_input_picture[0]->pict_type= P_TYPE;
            s->reordered_input_picture[0]->coded_picture_number= coded_pic_num;
            for(i=0; i<b_frames; i++){
                coded_pic_num++;
                s->reordered_input_picture[i+1]= s->input_picture[i];
                s->reordered_input_picture[i+1]->pict_type= B_TYPE;
                s->reordered_input_picture[i+1]->coded_picture_number= coded_pic_num;
            }
        }
    }
    
    if(s->reordered_input_picture[0]){
        s->reordered_input_picture[0]->reference= s->reordered_input_picture[0]->pict_type!=B_TYPE ? 3 : 0;

        s->new_picture= *s->reordered_input_picture[0];

        if(s->reordered_input_picture[0]->type == FF_BUFFER_TYPE_SHARED){
            // input is a shared pix, so we cant modifiy it -> alloc a new one & ensure that the shared one is reuseable
        
            int i= find_unused_picture(s, 0);
            Picture *pic= &s->picture[i];

            /* mark us unused / free shared pic */
            for(i=0; i<4; i++)
                s->reordered_input_picture[0]->data[i]= NULL;
            s->reordered_input_picture[0]->type= 0;
            
            //FIXME bad, copy * except
            pic->pict_type = s->reordered_input_picture[0]->pict_type;
            pic->quality   = s->reordered_input_picture[0]->quality;
            pic->coded_picture_number = s->reordered_input_picture[0]->coded_picture_number;
            pic->reference = s->reordered_input_picture[0]->reference;
            
            alloc_picture(s, pic, 0);

            s->current_picture_ptr= pic;
        }else{
            // input is not a shared pix -> reuse buffer for current_pix

            assert(   s->reordered_input_picture[0]->type==FF_BUFFER_TYPE_USER 
                   || s->reordered_input_picture[0]->type==FF_BUFFER_TYPE_INTERNAL);
            
            s->current_picture_ptr= s->reordered_input_picture[0];
            for(i=0; i<4; i++){
                //reverse the +16 we did before storing the input
                s->current_picture_ptr->data[i]-=16;
            }
        }
        s->current_picture= *s->current_picture_ptr;
    
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
    int i;

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    s->picture_in_gop_number++;

    load_input_picture(s, pic_arg);
    
    select_input_picture(s);
    
    /* output? */
    if(s->new_picture.data[0]){

        s->pict_type= s->new_picture.pict_type;
        if (s->fixed_qscale){ /* the ratecontrol needs the last qscale so we dont touch it for CBR */
            s->qscale= (int)(s->new_picture.quality+0.5);
            assert(s->qscale);
        }
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
    }

    s->input_picture_number++;

    flush_put_bits(&s->pb);
    s->frame_bits  = (pbBufPtr(&s->pb) - s->pb.buf) * 8;
    
    s->total_bits += s->frame_bits;
    avctx->frame_bits  = s->frame_bits;
    
    return pbBufPtr(&s->pb) - s->pb.buf;
}

#endif //CONFIG_ENCODERS

static inline void gmc1_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int dest_offset,
                               uint8_t **ref_picture, int src_offset)
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
    
    ptr = ref_picture[0] + (src_y * linesize) + src_x + src_offset;

    dest_y+=dest_offset;
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<0 || src_y<0 || src_x + 17 >= s->h_edge_pos
                              || src_y + 17 >= s->v_edge_pos){
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

    offset = (src_y * uvlinesize) + src_x + (src_offset>>1);
    ptr = ref_picture[1] + offset;
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<0 || src_y<0 || src_x + 9 >= s->h_edge_pos>>1
                              || src_y + 9 >= s->v_edge_pos>>1){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
    }
    s->dsp.gmc1(dest_cb + (dest_offset>>1), ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);
    
    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    s->dsp.gmc1(dest_cr + (dest_offset>>1), ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);
    
    return;
}

static inline void gmc_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int dest_offset,
                               uint8_t **ref_picture, int src_offset)
{
    uint8_t *ptr;
    int linesize, uvlinesize;
    const int a= s->sprite_warping_accuracy;
    int ox, oy;

    linesize = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0] + src_offset;

    dest_y+=dest_offset;
    
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


    dest_cb+=dest_offset>>1;
    dest_cr+=dest_offset>>1;
    
    ox= s->sprite_offset[1][0] + s->sprite_delta[0][0]*s->mb_x*8 + s->sprite_delta[0][1]*s->mb_y*8;
    oy= s->sprite_offset[1][1] + s->sprite_delta[1][0]*s->mb_x*8 + s->sprite_delta[1][1]*s->mb_y*8;

    ptr = ref_picture[1] + (src_offset>>1);
    s->dsp.gmc(dest_cb, ptr, uvlinesize, 8,
           ox, 
           oy, 
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1], 
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos>>1, s->v_edge_pos>>1);
    
    ptr = ref_picture[2] + (src_offset>>1);
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


/* apply one mpeg motion vector to the three components */
static inline void mpeg_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int dest_offset,
                               uint8_t **ref_picture, int src_offset,
                               int field_based, op_pixels_func (*pix_op)[4],
                               int motion_x, int motion_y, int h)
{
    uint8_t *ptr;
    int dxy, offset, mx, my, src_x, src_y, height, v_edge_pos, linesize, uvlinesize;
    int emu=0;
#if 0    
if(s->quarter_sample)
{
    motion_x>>=1;
    motion_y>>=1;
}
#endif
    dxy = ((motion_y & 1) << 1) | (motion_x & 1);
    src_x = s->mb_x * 16 + (motion_x >> 1);
    src_y = s->mb_y * (16 >> field_based) + (motion_y >> 1);
                
    /* WARNING: do no forget half pels */
    height = s->height >> field_based;
    v_edge_pos = s->v_edge_pos >> field_based;
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        dxy &= ~1;
    src_y = clip(src_y, -16, height);
    if (src_y == height)
        dxy &= ~2;
    linesize   = s->current_picture.linesize[0] << field_based;
    uvlinesize = s->current_picture.linesize[1] << field_based;
    ptr = ref_picture[0] + (src_y * linesize) + (src_x) + src_offset;
    dest_y += dest_offset;

    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<0 || src_y<0 || src_x + (motion_x&1) + 16 > s->h_edge_pos
                              || src_y + (motion_y&1) + h  > v_edge_pos){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr - src_offset, s->linesize, 17, 17+field_based,  //FIXME linesize? and uv below
                             src_x, src_y<<field_based, s->h_edge_pos, s->v_edge_pos);
            ptr= s->edge_emu_buffer + src_offset;
            emu=1;
        }
    }
    pix_op[0][dxy](dest_y, ptr, linesize, h);

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
    src_y = s->mb_y * (8 >> field_based) + my;
    src_x = clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = clip(src_y, -8, height >> 1);
    if (src_y == (height >> 1))
        dxy &= ~2;
    offset = (src_y * uvlinesize) + src_x + (src_offset >> 1);
    ptr = ref_picture[1] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr - (src_offset >> 1), s->uvlinesize, 9, 9+field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cb + (dest_offset >> 1), ptr, uvlinesize, h >> 1);

    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr - (src_offset >> 1), s->uvlinesize, 9, 9+field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cr + (dest_offset >> 1), ptr, uvlinesize, h >> 1);
}

static inline void qpel_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               int dest_offset,
                               uint8_t **ref_picture, int src_offset,
                               int field_based, op_pixels_func (*pix_op)[4],
                               qpel_mc_func (*qpix_op)[16],
                               int motion_x, int motion_y, int h)
{
    uint8_t *ptr;
    int dxy, offset, mx, my, src_x, src_y, height, v_edge_pos, linesize, uvlinesize;
    int emu=0;

    dxy = ((motion_y & 3) << 2) | (motion_x & 3);
    src_x = s->mb_x * 16 + (motion_x >> 2);
    src_y = s->mb_y * (16 >> field_based) + (motion_y >> 2);

    height = s->height >> field_based;
    v_edge_pos = s->v_edge_pos >> field_based;
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        dxy &= ~3;
    src_y = clip(src_y, -16, height);
    if (src_y == height)
        dxy &= ~12;
    linesize = s->linesize << field_based;
    uvlinesize = s->uvlinesize << field_based;
    ptr = ref_picture[0] + (src_y * linesize) + src_x + src_offset;
    dest_y += dest_offset;
//printf("%d %d %d\n", src_x, src_y, dxy);
    
    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<0 || src_y<0 || src_x + (motion_x&3) + 16 > s->h_edge_pos
                              || src_y + (motion_y&3) + h  > v_edge_pos){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr - src_offset, s->linesize, 17, 17+field_based, 
                             src_x, src_y<<field_based, s->h_edge_pos, s->v_edge_pos);
            ptr= s->edge_emu_buffer + src_offset;
            emu=1;
        }
    }
    if(!field_based)
        qpix_op[0][dxy](dest_y, ptr, linesize);
    else{
        //damn interlaced mode
        //FIXME boundary mirroring is not exactly correct here
        qpix_op[1][dxy](dest_y  , ptr  , linesize);
        qpix_op[1][dxy](dest_y+8, ptr+8, linesize);
    }

    if(s->flags&CODEC_FLAG_GRAY) return;

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

    dxy= (mx&1) | ((my&1)<<1);
    mx>>=1;
    my>>=1;

    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * (8 >> field_based) + my;
    src_x = clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = clip(src_y, -8, height >> 1);
    if (src_y == (height >> 1))
        dxy &= ~2;

    offset = (src_y * uvlinesize) + src_x + (src_offset >> 1);
    ptr = ref_picture[1] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr - (src_offset >> 1), s->uvlinesize, 9, 9 + field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cb + (dest_offset >> 1), ptr,  uvlinesize, h >> 1);
    
    ptr = ref_picture[2] + offset;
    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, ptr - (src_offset >> 1), s->uvlinesize, 9, 9 + field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cr + (dest_offset >> 1), ptr,  uvlinesize, h >> 1);
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
    int dxy, offset, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    uint8_t *ptr, *dest;
    int emu=0;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch(s->mv_type) {
    case MV_TYPE_16X16:
#ifdef CONFIG_RISKY
        if(s->mcsel){
            if(s->real_sprite_warping_points==1){
                gmc1_motion(s, dest_y, dest_cb, dest_cr, 0,
                            ref_picture, 0);
            }else{
                gmc_motion(s, dest_y, dest_cb, dest_cr, 0,
                            ref_picture, 0);
            }
        }else if(s->quarter_sample){
            qpel_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        0, pix_op, qpix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else if(s->mspel){
            ff_mspel_motion(s, dest_y, dest_cb, dest_cr,
                        ref_picture, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else
#endif
        {
            mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        0, pix_op,
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
                    if(src_x<0 || src_y<0 || src_x + (motion_x&3) + 8 > s->h_edge_pos
                                          || src_y + (motion_y&3) + 8 > s->v_edge_pos){
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
                motion_x = s->mv[dir][i][0];
                motion_y = s->mv[dir][i][1];

                dxy = ((motion_y & 1) << 1) | (motion_x & 1);
                src_x = mb_x * 16 + (motion_x >> 1) + (i & 1) * 8;
                src_y = mb_y * 16 + (motion_y >> 1) + (i >>1) * 8;
                    
                /* WARNING: do no forget half pels */
                src_x = clip(src_x, -16, s->width);
                if (src_x == s->width)
                    dxy &= ~1;
                src_y = clip(src_y, -16, s->height);
                if (src_y == s->height)
                    dxy &= ~2;
                    
                ptr = ref_picture[0] + (src_y * s->linesize) + (src_x);
                if(s->flags&CODEC_FLAG_EMU_EDGE){
                    if(src_x<0 || src_y<0 || src_x + (motion_x&1) + 8 > s->h_edge_pos
                                          || src_y + (motion_y&1) + 8 > s->v_edge_pos){
                        ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->linesize, 9, 9, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
                        ptr= s->edge_emu_buffer;
                    }
                }
                dest = dest_y + ((i & 1) * 8) + (i >> 1) * 8 * s->linesize;
                pix_op[1][dxy](dest, ptr, s->linesize, 8);

                mx += s->mv[dir][i][0];
                my += s->mv[dir][i][1];
            }
        }

        if(s->flags&CODEC_FLAG_GRAY) break;
        /* In case of 8X8, we construct a single chroma motion vector
           with a special rounding */
        mx= ff_h263_round_chroma(mx);
        my= ff_h263_round_chroma(my);
        dxy = ((my & 1) << 1) | (mx & 1);
        mx >>= 1;
        my >>= 1;

        src_x = mb_x * 8 + mx;
        src_y = mb_y * 8 + my;
        src_x = clip(src_x, -8, s->width/2);
        if (src_x == s->width/2)
            dxy &= ~1;
        src_y = clip(src_y, -8, s->height/2);
        if (src_y == s->height/2)
            dxy &= ~2;
        
        offset = (src_y * (s->uvlinesize)) + src_x;
        ptr = ref_picture[1] + offset;
        if(s->flags&CODEC_FLAG_EMU_EDGE){
                if(src_x<0 || src_y<0 || src_x + (dxy &1) + 8 > s->h_edge_pos>>1
                                      || src_y + (dxy>>1) + 8 > s->v_edge_pos>>1){
                    ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
                    ptr= s->edge_emu_buffer;
                    emu=1;
                }
            }
        pix_op[1][dxy](dest_cb, ptr, s->uvlinesize, 8);

        ptr = ref_picture[2] + offset;
        if(emu){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ptr= s->edge_emu_buffer;
        }
        pix_op[1][dxy](dest_cr, ptr, s->uvlinesize, 8);
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            if(s->quarter_sample){
                /* top field */
                qpel_motion(s, dest_y, dest_cb, dest_cr, 0,
                            ref_picture, s->field_select[dir][0] ? s->linesize : 0,
                            1, pix_op, qpix_op,
                            s->mv[dir][0][0], s->mv[dir][0][1], 8);
                /* bottom field */
                qpel_motion(s, dest_y, dest_cb, dest_cr, s->linesize,
                            ref_picture, s->field_select[dir][1] ? s->linesize : 0,
                            1, pix_op, qpix_op,
                            s->mv[dir][1][0], s->mv[dir][1][1], 8);
            }else{
                /* top field */       
                mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                            ref_picture, s->field_select[dir][0] ? s->linesize : 0,
                            1, pix_op,
                            s->mv[dir][0][0], s->mv[dir][0][1], 8);
                /* bottom field */
                mpeg_motion(s, dest_y, dest_cb, dest_cr, s->linesize,
                            ref_picture, s->field_select[dir][1] ? s->linesize : 0,
                            1, pix_op,
                            s->mv[dir][1][0], s->mv[dir][1][1], 8);
            }
        } else {
            int offset;
            if(s->picture_structure == s->field_select[dir][0] + 1 || s->pict_type == B_TYPE || s->first_field){
                offset= s->field_select[dir][0] ? s->linesize : 0;
            }else{
                ref_picture= s->current_picture.data;
                offset= s->field_select[dir][0] ? s->linesize : -s->linesize; 
            } 

            mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, offset,
                        0, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }
        break;
    }
}


/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, uint8_t *dest, int line_size)
{
    s->dct_unquantize(s, block, i, s->qscale);
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
                           DCTELEM *block, int i, uint8_t *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize(s, block, i, s->qscale);

        s->dsp.idct_add (dest, line_size, block);
    }
}

/**
 * cleans dc, ac, coded_block for the current non intra MB
 */
void ff_clean_intra_table_entries(MpegEncContext *s)
{
    int wrap = s->block_wrap[0];
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
    wrap = s->block_wrap[4];
    xy = s->mb_x + 1 + (s->mb_y + 1) * wrap;
    s->dc_val[1][xy] =
    s->dc_val[2][xy] = 1024;
    /* ac pred */
    memset(s->ac_val[1][xy], 0, 16 * sizeof(int16_t));
    memset(s->ac_val[2][xy], 0, 16 * sizeof(int16_t));
    
    s->mbintra_table[s->mb_x + s->mb_y*s->mb_stride]= 0;
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
void MPV_decode_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int mb_x, mb_y;
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

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

    /* update motion predictor, not for B-frames as they need the motion_val from the last P/S-Frame */
    if (s->out_format == FMT_H263 && s->pict_type!=B_TYPE) { //FIXME move into h263.c if possible, format specific stuff shouldnt be here
        //FIXME a lot of thet is only needed for !low_delay
        const int wrap = s->block_wrap[0];
        const int xy = s->block_index[0];
        if(s->mv_type != MV_TYPE_8X8){
            int motion_x, motion_y;
            if (s->mb_intra) {
                motion_x = 0;
                motion_y = 0;
            } else if (s->mv_type == MV_TYPE_16X16) {
                motion_x = s->mv[0][0][0];
                motion_y = s->mv[0][0][1];
            } else /*if (s->mv_type == MV_TYPE_FIELD)*/ {
                int i;
                motion_x = s->mv[0][0][0] + s->mv[0][1][0];
                motion_y = s->mv[0][0][1] + s->mv[0][1][1];
                motion_x = (motion_x>>1) | (motion_x&1);
                for(i=0; i<2; i++){
                    s->field_mv_table[mb_xy][i][0]= s->mv[0][i][0];
                    s->field_mv_table[mb_xy][i][1]= s->mv[0][i][1];
                    s->field_select_table[mb_xy][i]= s->field_select[0][i];
                }
            }
            
            /* no update if 8X8 because it has been done during parsing */
            s->motion_val[xy][0] = motion_x;
            s->motion_val[xy][1] = motion_y;
            s->motion_val[xy + 1][0] = motion_x;
            s->motion_val[xy + 1][1] = motion_y;
            s->motion_val[xy + wrap][0] = motion_x;
            s->motion_val[xy + wrap][1] = motion_y;
            s->motion_val[xy + 1 + wrap][0] = motion_x;
            s->motion_val[xy + 1 + wrap][1] = motion_y;
        }

        if(s->encoding){ //FIXME encoding MUST be cleaned up
            if (s->mv_type == MV_TYPE_8X8) 
                s->current_picture.mb_type[mb_xy]= MB_TYPE_L0 | MB_TYPE_8x8;
            else
                s->current_picture.mb_type[mb_xy]= MB_TYPE_L0 | MB_TYPE_16x16;
        }
    }
    
    if ((s->flags&CODEC_FLAG_PSNR) || !(s->encoding && (s->intra_only || s->pict_type==B_TYPE))) { //FIXME precalc
        uint8_t *dest_y, *dest_cb, *dest_cr;
        int dct_linesize, dct_offset;
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        const int linesize= s->current_picture.linesize[0]; //not s->linesize as this woulnd be wrong for field pics
        const int uvlinesize= s->current_picture.linesize[1];

        /* avoid copy if macroblock skipped in last frame too */
        if (s->pict_type != B_TYPE) {
            s->current_picture.mbskip_table[mb_xy]= s->mb_skiped;
        }

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
        }else
            s->mb_skiped= 0;

        if(s->pict_type==B_TYPE && s->avctx->draw_horiz_band && s->picture_structure==PICT_FRAME){ //FIXME precalc
            dest_y  = s->current_picture.data[0] + mb_x * 16;
            dest_cb = s->current_picture.data[1] + mb_x * 8;
            dest_cr = s->current_picture.data[2] + mb_x * 8;
        }else{
            dest_y  = s->current_picture.data[0] + (mb_y * 16* linesize  ) + mb_x * 16;
            dest_cb = s->current_picture.data[1] + (mb_y * 8 * uvlinesize) + mb_x * 8;
            dest_cr = s->current_picture.data[2] + (mb_y * 8 * uvlinesize) + mb_x * 8;
        }

        if (s->interlaced_dct) {
            dct_linesize = linesize * 2;
            dct_offset = linesize;
        } else {
            dct_linesize = linesize;
            dct_offset = linesize * 8;
        }

        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was allready done otherwise) */
            if((!s->encoding) || (s->mb_type[mb_xy]&(s->mb_type[mb_xy]-1))){
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
            if(s->encoding || !(   s->mpeg2 || s->h263_msmpeg4 || s->codec_id==CODEC_ID_MPEG1VIDEO 
                                || (s->codec_id==CODEC_ID_MPEG4 && !s->mpeg_quant))){
                add_dequant_dct(s, block[0], 0, dest_y, dct_linesize);
                add_dequant_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                add_dequant_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                add_dequant_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize);
                    add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize);
                }
            } else if(s->codec_id != CODEC_ID_WMV2){
                add_dct(s, block[0], 0, dest_y, dct_linesize);
                add_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                add_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                add_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    add_dct(s, block[4], 4, dest_cb, uvlinesize);
                    add_dct(s, block[5], 5, dest_cr, uvlinesize);
                }
            } 
#ifdef CONFIG_RISKY
            else{
                ff_wmv2_add_mb(s, block, dest_y, dest_cb, dest_cr);
            }
#endif
        } else {
            /* dct only in intra block */
            if(s->encoding || !(s->mpeg2 || s->codec_id==CODEC_ID_MPEG1VIDEO)){
                put_dct(s, block[0], 0, dest_y, dct_linesize);
                put_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                put_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                put_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    put_dct(s, block[4], 4, dest_cb, uvlinesize);
                    put_dct(s, block[5], 5, dest_cr, uvlinesize);
                }
            }else{
                s->dsp.idct_put(dest_y                 , dct_linesize, block[0]);
                s->dsp.idct_put(dest_y              + 8, dct_linesize, block[1]);
                s->dsp.idct_put(dest_y + dct_offset    , dct_linesize, block[2]);
                s->dsp.idct_put(dest_y + dct_offset + 8, dct_linesize, block[3]);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    s->dsp.idct_put(dest_cb, uvlinesize, block[4]);
                    s->dsp.idct_put(dest_cr, uvlinesize, block[5]);
                }
            }
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
    
    if(s->mb_intra){
        i=1; //skip clipping of intra dc
    }else
        i=0;
    
    for(;i<=last_index; i++){
        const int j= s->intra_scantable.permutated[i];
        int level = block[j];
       
        if     (level>maxlevel) level=maxlevel;
        else if(level<minlevel) level=minlevel;

        block[j]= level;
    }
}

#if 0
static int pix_vcmp16x8(uint8_t *s, int stride){ //FIXME move to dsputil & optimize
    int score=0;
    int x,y;
    
    for(y=0; y<7; y++){
        for(x=0; x<16; x+=4){
            score+= ABS(s[x  ] - s[x  +stride]) + ABS(s[x+1] - s[x+1+stride]) 
                   +ABS(s[x+2] - s[x+2+stride]) + ABS(s[x+3] - s[x+3+stride]);
        }
        s+= stride;
    }
    
    return score;
}

static int pix_diff_vcmp16x8(uint8_t *s1, uint8_t*s2, int stride){ //FIXME move to dsputil & optimize
    int score=0;
    int x,y;
    
    for(y=0; y<7; y++){
        for(x=0; x<16; x++){
            score+= ABS(s1[x  ] - s2[x ] - s1[x  +stride] + s2[x +stride]);
        }
        s1+= stride;
        s2+= stride;
    }
    
    return score;
}
#else
#define SQ(a) ((a)*(a))

static int pix_vcmp16x8(uint8_t *s, int stride){ //FIXME move to dsputil & optimize
    int score=0;
    int x,y;
    
    for(y=0; y<7; y++){
        for(x=0; x<16; x+=4){
            score+= SQ(s[x  ] - s[x  +stride]) + SQ(s[x+1] - s[x+1+stride]) 
                   +SQ(s[x+2] - s[x+2+stride]) + SQ(s[x+3] - s[x+3+stride]);
        }
        s+= stride;
    }
    
    return score;
}

static int pix_diff_vcmp16x8(uint8_t *s1, uint8_t*s2, int stride){ //FIXME move to dsputil & optimize
    int score=0;
    int x,y;
    
    for(y=0; y<7; y++){
        for(x=0; x<16; x++){
            score+= SQ(s1[x  ] - s2[x ] - s1[x  +stride] + s2[x +stride]);
        }
        s1+= stride;
        s2+= stride;
    }
    
    return score;
}

#endif

#endif //CONFIG_ENCODERS

/**
 *
 * @param h is the normal height, this will be reduced automatically if needed for the last row
 */
void ff_draw_horiz_band(MpegEncContext *s, int y, int h){
    if (    s->avctx->draw_horiz_band 
        && (s->last_picture_ptr || s->low_delay) ) {
        uint8_t *src_ptr[3];
        int offset;
        h= FFMIN(h, s->height - y);

        if(s->pict_type==B_TYPE && s->picture_structure == PICT_FRAME)
            offset = 0;
        else
            offset = y * s->linesize;

        if(s->pict_type==B_TYPE || s->low_delay){
            src_ptr[0] = s->current_picture.data[0] + offset;
            src_ptr[1] = s->current_picture.data[1] + (offset >> 2);
            src_ptr[2] = s->current_picture.data[2] + (offset >> 2);
        } else {
            src_ptr[0] = s->last_picture.data[0] + offset;
            src_ptr[1] = s->last_picture.data[1] + (offset >> 2);
            src_ptr[2] = s->last_picture.data[2] + (offset >> 2);
        }
        emms_c();

        s->avctx->draw_horiz_band(s->avctx, src_ptr, s->linesize,
                               y, s->width, h);
    }
}

#ifdef CONFIG_ENCODERS

static void encode_mb(MpegEncContext *s, int motion_x, int motion_y)
{
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    int i;
    int skip_dct[6];
    int dct_offset   = s->linesize*8; //default for progressive frames
    
    for(i=0; i<6; i++) skip_dct[i]=0;
    
    if(s->adaptive_quant){
        s->dquant= s->current_picture.qscale_table[mb_x + mb_y*s->mb_stride] - s->qscale;

        if(s->out_format==FMT_H263){
            if     (s->dquant> 2) s->dquant= 2;
            else if(s->dquant<-2) s->dquant=-2;
        }
            
        if(s->codec_id==CODEC_ID_MPEG4){        
            if(!s->mb_intra){
                if(s->mv_dir&MV_DIRECT)
                    s->dquant=0;

                assert(s->dquant==0 || s->mv_type!=MV_TYPE_8X8);
            }
        }
        s->qscale+= s->dquant;
        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
    }

    if (s->mb_intra) {
        uint8_t *ptr;
        int wrap_y;
        int emu=0;

        wrap_y = s->linesize;
        ptr = s->new_picture.data[0] + (mb_y * 16 * wrap_y) + mb_x * 16;

        if(mb_x*16+16 > s->width || mb_y*16+16 > s->height){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr, wrap_y, 16, 16, mb_x*16, mb_y*16, s->width, s->height);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
        
        if(s->flags&CODEC_FLAG_INTERLACED_DCT){
            int progressive_score, interlaced_score;
            
            progressive_score= pix_vcmp16x8(ptr, wrap_y  ) + pix_vcmp16x8(ptr + wrap_y*8, wrap_y );
            interlaced_score = pix_vcmp16x8(ptr, wrap_y*2) + pix_vcmp16x8(ptr + wrap_y  , wrap_y*2);
            
            if(progressive_score > interlaced_score + 100){
                s->interlaced_dct=1;
            
                dct_offset= wrap_y;
                wrap_y<<=1;
            }else
                s->interlaced_dct=0;
        }
        
	s->dsp.get_pixels(s->block[0], ptr                 , wrap_y);
        s->dsp.get_pixels(s->block[1], ptr              + 8, wrap_y);
        s->dsp.get_pixels(s->block[2], ptr + dct_offset    , wrap_y);
        s->dsp.get_pixels(s->block[3], ptr + dct_offset + 8, wrap_y);

        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
            int wrap_c = s->uvlinesize;
            ptr = s->new_picture.data[1] + (mb_y * 8 * wrap_c) + mb_x * 8;
            if(emu){
                ff_emulated_edge_mc(s->edge_emu_buffer, ptr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr= s->edge_emu_buffer;
            }
	    s->dsp.get_pixels(s->block[4], ptr, wrap_c);

            ptr = s->new_picture.data[2] + (mb_y * 8 * wrap_c) + mb_x * 8;
            if(emu){
                ff_emulated_edge_mc(s->edge_emu_buffer, ptr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr= s->edge_emu_buffer;
            }
            s->dsp.get_pixels(s->block[5], ptr, wrap_c);
        }
    }else{
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        uint8_t *dest_y, *dest_cb, *dest_cr;
        uint8_t *ptr_y, *ptr_cb, *ptr_cr;
        int wrap_y, wrap_c;
        int emu=0;

        dest_y  = s->current_picture.data[0] + (mb_y * 16 * s->linesize    ) + mb_x * 16;
        dest_cb = s->current_picture.data[1] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
        dest_cr = s->current_picture.data[2] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
        wrap_y = s->linesize;
        wrap_c = s->uvlinesize;
        ptr_y  = s->new_picture.data[0] + (mb_y * 16 * wrap_y) + mb_x * 16;
        ptr_cb = s->new_picture.data[1] + (mb_y * 8 * wrap_c) + mb_x * 8;
        ptr_cr = s->new_picture.data[2] + (mb_y * 8 * wrap_c) + mb_x * 8;

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

        if(mb_x*16+16 > s->width || mb_y*16+16 > s->height){
            ff_emulated_edge_mc(s->edge_emu_buffer, ptr_y, wrap_y, 16, 16, mb_x*16, mb_y*16, s->width, s->height);
            ptr_y= s->edge_emu_buffer;
            emu=1;
        }
        
        if(s->flags&CODEC_FLAG_INTERLACED_DCT){
            int progressive_score, interlaced_score;
            
            progressive_score= pix_diff_vcmp16x8(ptr_y           , dest_y           , wrap_y  ) 
                             + pix_diff_vcmp16x8(ptr_y + wrap_y*8, dest_y + wrap_y*8, wrap_y  );
            interlaced_score = pix_diff_vcmp16x8(ptr_y           , dest_y           , wrap_y*2)
                             + pix_diff_vcmp16x8(ptr_y + wrap_y  , dest_y + wrap_y  , wrap_y*2);
            
            if(progressive_score > interlaced_score + 600){
                s->interlaced_dct=1;
            
                dct_offset= wrap_y;
                wrap_y<<=1;
            }else
                s->interlaced_dct=0;
        }
        
	s->dsp.diff_pixels(s->block[0], ptr_y                 , dest_y                 , wrap_y);
        s->dsp.diff_pixels(s->block[1], ptr_y              + 8, dest_y              + 8, wrap_y);
        s->dsp.diff_pixels(s->block[2], ptr_y + dct_offset    , dest_y + dct_offset    , wrap_y);
        s->dsp.diff_pixels(s->block[3], ptr_y + dct_offset + 8, dest_y + dct_offset + 8, wrap_y);
        
        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
            if(emu){
                ff_emulated_edge_mc(s->edge_emu_buffer, ptr_cb, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr_cb= s->edge_emu_buffer;
            }
            s->dsp.diff_pixels(s->block[4], ptr_cb, dest_cb, wrap_c);
            if(emu){
                ff_emulated_edge_mc(s->edge_emu_buffer, ptr_cr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr_cr= s->edge_emu_buffer;
            }
            s->dsp.diff_pixels(s->block[5], ptr_cr, dest_cr, wrap_c);
        }
        /* pre quantization */         
        if(s->current_picture.mc_mb_var[s->mb_stride*mb_y+ mb_x]<2*s->qscale*s->qscale){
            //FIXME optimize
	    if(s->dsp.pix_abs8x8(ptr_y               , dest_y               , wrap_y) < 20*s->qscale) skip_dct[0]= 1;
            if(s->dsp.pix_abs8x8(ptr_y            + 8, dest_y            + 8, wrap_y) < 20*s->qscale) skip_dct[1]= 1;
            if(s->dsp.pix_abs8x8(ptr_y +dct_offset   , dest_y +dct_offset   , wrap_y) < 20*s->qscale) skip_dct[2]= 1;
            if(s->dsp.pix_abs8x8(ptr_y +dct_offset+ 8, dest_y +dct_offset+ 8, wrap_y) < 20*s->qscale) skip_dct[3]= 1;
            if(s->dsp.pix_abs8x8(ptr_cb              , dest_cb              , wrap_c) < 20*s->qscale) skip_dct[4]= 1;
            if(s->dsp.pix_abs8x8(ptr_cr              , dest_cr              , wrap_c) < 20*s->qscale) skip_dct[5]= 1;
#if 0
{
 static int stat[7];
 int num=0;
 for(i=0; i<6; i++)
  if(skip_dct[i]) num++;
 stat[num]++;
 
 if(s->mb_x==0 && s->mb_y==0){
  for(i=0; i<7; i++){
   printf("%6d %1d\n", stat[i], i);
  }
 }
}
#endif
        }

    }
            
#if 0
            {
                float adap_parm;
                
                adap_parm = ((s->avg_mb_var << 1) + s->mb_var[s->mb_stride*mb_y+mb_x] + 1.0) /
                            ((s->mb_var[s->mb_stride*mb_y+mb_x] << 1) + s->avg_mb_var + 1.0);
            
                printf("\ntype=%c qscale=%2d adap=%0.2f dquant=%4.2f var=%4d avgvar=%4d", 
                        (s->mb_type[s->mb_stride*mb_y+mb_x] > 0) ? 'I' : 'P', 
                        s->qscale, adap_parm, s->qscale*adap_parm,
                        s->mb_var[s->mb_stride*mb_y+mb_x], s->avg_mb_var);
            }
#endif
    /* DCT & quantize */
    if(s->out_format==FMT_MJPEG){
        for(i=0;i<6;i++) {
            int overflow;
            s->block_last_index[i] = s->dct_quantize(s, s->block[i], i, 8, &overflow);
            if (overflow) clip_coeffs(s, s->block[i], s->block_last_index[i]);
        }
    }else{
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
        if(s->luma_elim_threshold && !s->mb_intra)
            for(i=0; i<4; i++)
                dct_single_coeff_elimination(s, i, s->luma_elim_threshold);
        if(s->chroma_elim_threshold && !s->mb_intra)
            for(i=4; i<6; i++)
                dct_single_coeff_elimination(s, i, s->chroma_elim_threshold);
    }

    if((s->flags&CODEC_FLAG_GRAY) && s->mb_intra){
        s->block_last_index[4]=
        s->block_last_index[5]= 0;
        s->block[4][0]=
        s->block[5][0]= (1024 + s->c_dc_scale/2)/ s->c_dc_scale;
    }

    /* huffman encode */
    switch(s->codec_id){ //FIXME funct ptr could be slightly faster
    case CODEC_ID_MPEG1VIDEO:
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

/**
 * combines the (truncated) bitstream to a complete frame
 * @returns -1 if no complete frame could be created
 */
int ff_combine_frame( MpegEncContext *s, int next, uint8_t **buf, int *buf_size){
    ParseContext *pc= &s->parse_context;

#if 0
    if(pc->overread){
        printf("overread %d, state:%X next:%d index:%d o_index:%d\n", pc->overread, pc->state, next, pc->index, pc->overread_index);
        printf("%X %X %X %X\n", (*buf)[0], (*buf)[1],(*buf)[2],(*buf)[3]);
    }
#endif

    /* copy overreaded byes from last frame into buffer */
    for(; pc->overread>0; pc->overread--){
        pc->buffer[pc->index++]= pc->buffer[pc->overread_index++];
    }
    
    pc->last_index= pc->index;

    /* copy into buffer end return */
    if(next == END_NOT_FOUND){
        pc->buffer= av_fast_realloc(pc->buffer, &pc->buffer_size, (*buf_size) + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        memcpy(&pc->buffer[pc->index], *buf, *buf_size);
        pc->index += *buf_size;
        return -1;
    }

    *buf_size=
    pc->overread_index= pc->index + next;
    
    /* append to buffer */
    if(pc->index){
        pc->buffer= av_fast_realloc(pc->buffer, &pc->buffer_size, next + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        memcpy(&pc->buffer[pc->index], *buf, next + FF_INPUT_BUFFER_PADDING_SIZE );
        pc->index = 0;
        *buf= pc->buffer;
    }

    /* store overread bytes */
    for(;next < 0; next++){
        pc->state = (pc->state<<8) | pc->buffer[pc->last_index + next];
        pc->overread++;
    }

#if 0
    if(pc->overread){
        printf("overread %d, state:%X next:%d index:%d o_index:%d\n", pc->overread, pc->state, next, pc->index, pc->overread_index);
        printf("%X %X %X %X\n", (*buf)[0], (*buf)[1],(*buf)[2],(*buf)[3]);
    }
#endif

    return 0;
}

#ifdef CONFIG_ENCODERS
void ff_copy_bits(PutBitContext *pb, uint8_t *src, int length)
{
    int bytes= length>>4;
    int bits= length&15;
    int i;

    if(length==0) return;

    for(i=0; i<bytes; i++) put_bits(pb, 16, be2me_16(((uint16_t*)src)[i]));
    put_bits(pb, bits, be2me_16(((uint16_t*)src)[i])>>(16-bits));
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

    d->mb_skiped= s->mb_skiped;
    d->qscale= s->qscale;
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
    int bits_count;
    
    copy_context_before_encode(s, backup, type);

    s->block= s->blocks[*next_block];
    s->pb= pb[*next_block];
    if(s->data_partitioning){
        s->pb2   = pb2   [*next_block];
        s->tex_pb= tex_pb[*next_block];
    }

    encode_mb(s, motion_x, motion_y);

    bits_count= get_bit_count(&s->pb);
    if(s->data_partitioning){
        bits_count+= get_bit_count(&s->pb2);
        bits_count+= get_bit_count(&s->tex_pb);
    }

    if(bits_count<*dmin){
        *dmin= bits_count;
        *next_block^=1;

        copy_context_after_encode(best, s, type);
    }
}
                
static inline int sse(MpegEncContext *s, uint8_t *src1, uint8_t *src2, int w, int h, int stride){
    uint32_t *sq = squareTbl + 256;
    int acc=0;
    int x,y;
    
    if(w==16 && h==16) 
        return s->dsp.sse[0](NULL, src1, src2, stride);
    else if(w==8 && h==8)
        return s->dsp.sse[1](NULL, src1, src2, stride);
    
    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            acc+= sq[src1[x + y*stride] - src2[x + y*stride]];
        } 
    }
    
    assert(acc>=0);
    
    return acc;
}

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y, pdif = 0;
    int i;
    int bits;
    MpegEncContext best_s, backup_s;
    uint8_t bit_buf[2][3000];
    uint8_t bit_buf2[2][3000];
    uint8_t bit_buf_tex[2][3000];
    PutBitContext pb[2], pb2[2], tex_pb[2];

    for(i=0; i<2; i++){
        init_put_bits(&pb    [i], bit_buf    [i], 3000, NULL, NULL);
        init_put_bits(&pb2   [i], bit_buf2   [i], 3000, NULL, NULL);
        init_put_bits(&tex_pb[i], bit_buf_tex[i], 3000, NULL, NULL);
    }

    s->picture_number = picture_number;
    
    /* Reset the average MB variance */
    s->current_picture.mb_var_sum = 0;
    s->current_picture.mc_mb_var_sum = 0;

#ifdef CONFIG_RISKY
    /* we need to initialize some time vars before we can encode b-frames */
    // RAL: Condition added for MPEG1VIDEO
    if (s->codec_id == CODEC_ID_MPEG1VIDEO || (s->h263_pred && !s->h263_msmpeg4))
        ff_set_mpeg4_time(s, s->picture_number); 
#endif
        
    s->scene_change_score=0;
    
    s->qscale= (int)(s->frame_qscale + 0.5); //FIXME qscale / ... stuff for ME ratedistoration
    
    if(s->pict_type==I_TYPE){
        if(s->msmpeg4_version >= 3) s->no_rounding=1;
        else                        s->no_rounding=0;
    }else if(s->pict_type!=B_TYPE){
        if(s->flipflop_rounding || s->codec_id == CODEC_ID_H263P || s->codec_id == CODEC_ID_MPEG4)
            s->no_rounding ^= 1;          
    }
    
    /* Estimate motion for every MB */
    s->mb_intra=0; //for the rate distoration & bit compare functions
    if(s->pict_type != I_TYPE){
        if(s->pict_type != B_TYPE){
            if((s->avctx->pre_me && s->last_non_b_pict_type==I_TYPE) || s->avctx->pre_me==2){
                s->me.pre_pass=1;
                s->me.dia_size= s->avctx->pre_dia_size;

                for(mb_y=s->mb_height-1; mb_y >=0 ; mb_y--) {
                    for(mb_x=s->mb_width-1; mb_x >=0 ; mb_x--) {
                        s->mb_x = mb_x;
                        s->mb_y = mb_y;
                        ff_pre_estimate_p_frame_motion(s, mb_x, mb_y);
                    }
                }
                s->me.pre_pass=0;
            }
        }

        s->me.dia_size= s->avctx->dia_size;
        for(mb_y=0; mb_y < s->mb_height; mb_y++) {
            s->block_index[0]= s->block_wrap[0]*(mb_y*2 + 1) - 1;
            s->block_index[1]= s->block_wrap[0]*(mb_y*2 + 1);
            s->block_index[2]= s->block_wrap[0]*(mb_y*2 + 2) - 1;
            s->block_index[3]= s->block_wrap[0]*(mb_y*2 + 2);
            for(mb_x=0; mb_x < s->mb_width; mb_x++) {
                s->mb_x = mb_x;
                s->mb_y = mb_y;
                s->block_index[0]+=2;
                s->block_index[1]+=2;
                s->block_index[2]+=2;
                s->block_index[3]+=2;
                
                /* compute motion vector & mb_type and store in context */
                if(s->pict_type==B_TYPE)
                    ff_estimate_b_frame_motion(s, mb_x, mb_y);
                else
                    ff_estimate_p_frame_motion(s, mb_x, mb_y);
            }
        }
    }else /* if(s->pict_type == I_TYPE) */{
        /* I-Frame */
        //FIXME do we need to zero them?
        memset(s->motion_val[0], 0, sizeof(int16_t)*(s->mb_width*2 + 2)*(s->mb_height*2 + 2)*2);
        memset(s->p_mv_table   , 0, sizeof(int16_t)*(s->mb_stride)*s->mb_height*2);
        memset(s->mb_type      , MB_TYPE_INTRA, sizeof(uint8_t)*s->mb_stride*s->mb_height);
        
        if(!s->fixed_qscale){
            /* finding spatial complexity for I-frame rate control */
            for(mb_y=0; mb_y < s->mb_height; mb_y++) {
                for(mb_x=0; mb_x < s->mb_width; mb_x++) {
                    int xx = mb_x * 16;
                    int yy = mb_y * 16;
                    uint8_t *pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
                    int varc;
		    int sum = s->dsp.pix_sum(pix, s->linesize);
    
		    varc = (s->dsp.pix_norm1(pix, s->linesize) - (((unsigned)(sum*sum))>>8) + 500 + 128)>>8;

                    s->current_picture.mb_var [s->mb_stride * mb_y + mb_x] = varc;
                    s->current_picture.mb_mean[s->mb_stride * mb_y + mb_x] = (sum+128)>>8;
                    s->current_picture.mb_var_sum    += varc;
                }
            }
        }
    }
    emms_c();

    if(s->scene_change_score > 0 && s->pict_type == P_TYPE){
        s->pict_type= I_TYPE;
        memset(s->mb_type   , MB_TYPE_INTRA, sizeof(uint8_t)*s->mb_stride*s->mb_height);
//printf("Scene change detected, encoding as I Frame %d %d\n", s->current_picture.mb_var_sum, s->current_picture.mc_mb_var_sum);
    }

    if(!s->umvplus){
        if(s->pict_type==P_TYPE || s->pict_type==S_TYPE) {
            s->f_code= ff_get_best_fcode(s, s->p_mv_table, MB_TYPE_INTER);
        
            ff_fix_long_p_mvs(s);
        }

        if(s->pict_type==B_TYPE){
            int a, b;

            a = ff_get_best_fcode(s, s->b_forw_mv_table, MB_TYPE_FORWARD);
            b = ff_get_best_fcode(s, s->b_bidir_forw_mv_table, MB_TYPE_BIDIR);
            s->f_code = FFMAX(a, b);

            a = ff_get_best_fcode(s, s->b_back_mv_table, MB_TYPE_BACKWARD);
            b = ff_get_best_fcode(s, s->b_bidir_back_mv_table, MB_TYPE_BIDIR);
            s->b_code = FFMAX(a, b);

            ff_fix_long_b_mvs(s, s->b_forw_mv_table, s->f_code, MB_TYPE_FORWARD);
            ff_fix_long_b_mvs(s, s->b_back_mv_table, s->b_code, MB_TYPE_BACKWARD);
            ff_fix_long_b_mvs(s, s->b_bidir_forw_mv_table, s->f_code, MB_TYPE_BIDIR);
            ff_fix_long_b_mvs(s, s->b_bidir_back_mv_table, s->b_code, MB_TYPE_BIDIR);
        }
    }
    
    if (s->fixed_qscale) 
        s->frame_qscale = s->current_picture.quality;
    else
        s->frame_qscale = ff_rate_estimate_qscale(s);

    if(s->adaptive_quant){
#ifdef CONFIG_RISKY
        switch(s->codec_id){
        case CODEC_ID_MPEG4:
            ff_clean_mpeg4_qscales(s);
            break;
        case CODEC_ID_H263:
        case CODEC_ID_H263P:
            ff_clean_h263_qscales(s);
            break;
        }
#endif

        s->qscale= s->current_picture.qscale_table[0];
    }else
        s->qscale= (int)(s->frame_qscale + 0.5);
        
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = ff_mpeg1_default_intra_matrix[0];
        for(i=1;i<64;i++){
            int j= s->dsp.idct_permutation[i];

            s->intra_matrix[j] = CLAMP_TO_8BIT((ff_mpeg1_default_intra_matrix[i] * s->qscale) >> 3);
        }
        convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16, 
                       s->q_intra_matrix16_bias, s->intra_matrix, s->intra_quant_bias, 8, 8);
    }
    
    //FIXME var duplication
    s->current_picture.key_frame= s->pict_type == I_TYPE;
    s->current_picture.pict_type= s->pict_type;

    if(s->current_picture.key_frame)
        s->picture_in_gop_number=0;

    s->last_bits= get_bit_count(&s->pb);
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
        else if (s->h263_rv10) 
            rv10_encode_picture_header(s, picture_number);
        else
            h263_encode_picture_header(s, picture_number);
        break;
#endif
    case FMT_MPEG1:
        mpeg1_encode_picture_header(s, picture_number);
        break;
    }
    bits= get_bit_count(&s->pb);
    s->header_bits= bits - s->last_bits;
    s->last_bits= bits;
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
        s->last_dc[i] = 128;
        
        s->current_picture_ptr->error[i] = 0;
    }
    s->mb_skip_run = 0;
    s->last_mv[0][0][0] = 0;
    s->last_mv[0][0][1] = 0;
    s->last_mv[1][0][0] = 0;
    s->last_mv[1][0][1] = 0;
     
    s->last_mv_dir = 0;

#ifdef CONFIG_RISKY
    if (s->codec_id==CODEC_ID_H263 || s->codec_id==CODEC_ID_H263P)
        s->gob_index = ff_h263_get_gob_height(s);

    if(s->codec_id==CODEC_ID_MPEG4 && s->partitioned_frame)
        ff_mpeg4_init_partitions(s);
#endif

    s->resync_mb_x=0;
    s->resync_mb_y=0;
    s->first_slice_line = 1;
    s->ptr_lastgob = s->pb.buf;
    for(mb_y=0; mb_y < s->mb_height; mb_y++) {
        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
        
        s->block_index[0]= s->block_wrap[0]*(mb_y*2 + 1) - 1;
        s->block_index[1]= s->block_wrap[0]*(mb_y*2 + 1);
        s->block_index[2]= s->block_wrap[0]*(mb_y*2 + 2) - 1;
        s->block_index[3]= s->block_wrap[0]*(mb_y*2 + 2);
        s->block_index[4]= s->block_wrap[4]*(mb_y + 1)                    + s->block_wrap[0]*(s->mb_height*2 + 2);
        s->block_index[5]= s->block_wrap[4]*(mb_y + 1 + s->mb_height + 2) + s->block_wrap[0]*(s->mb_height*2 + 2);
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            const int xy= mb_y*s->mb_stride + mb_x;
            int mb_type= s->mb_type[xy];
//            int d;
            int dmin=10000000;

            s->mb_x = mb_x;
            s->mb_y = mb_y;
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;
            s->block_index[4]++;
            s->block_index[5]++;

            /* write gob / video packet header  */
#ifdef CONFIG_RISKY
            if(s->rtp_mode){
                int current_packet_size, is_gob_start;
                
                current_packet_size= pbBufPtr(&s->pb) - s->ptr_lastgob;
                is_gob_start=0;
                
                if(s->codec_id==CODEC_ID_MPEG4){
                    if(current_packet_size >= s->rtp_payload_size
                       && s->mb_y + s->mb_x>0){

                        if(s->partitioned_frame){
                            ff_mpeg4_merge_partitions(s);
                            ff_mpeg4_init_partitions(s);
                        }
                        ff_mpeg4_encode_video_packet_header(s);

                        if(s->flags&CODEC_FLAG_PASS1){
                            int bits= get_bit_count(&s->pb);
                            s->misc_bits+= bits - s->last_bits;
                            s->last_bits= bits;
                        }
                        ff_mpeg4_clean_buffers(s);
                        is_gob_start=1;
                    }
                }else if(s->codec_id==CODEC_ID_MPEG1VIDEO){
                    if(   current_packet_size >= s->rtp_payload_size 
                       && s->mb_y + s->mb_x>0 && s->mb_skip_run==0){
                        ff_mpeg1_encode_slice_header(s);
                        ff_mpeg1_clean_buffers(s);
                        is_gob_start=1;
                    }
                }else{
                    if(current_packet_size >= s->rtp_payload_size
                       && s->mb_x==0 && s->mb_y>0 && s->mb_y%s->gob_index==0){
                       
                        h263_encode_gob_header(s, mb_y);                       
                        is_gob_start=1;
                    }
                }

                if(is_gob_start){
                    s->ptr_lastgob = pbBufPtr(&s->pb);
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

            if(mb_type & (mb_type-1)){ // more than 1 MB type possible
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

                if(mb_type&MB_TYPE_INTER){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->p_mv_table[xy][0];
                    s->mv[0][0][1] = s->p_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_INTER, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&MB_TYPE_INTER4V){                 
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->motion_val[s->block_index[i]][0];
                        s->mv[0][i][1] = s->motion_val[s->block_index[i]][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_INTER4V, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&MB_TYPE_FORWARD){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_FORWARD, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&MB_TYPE_BACKWARD){
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_BACKWARD, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->mv[1][0][0], s->mv[1][0][1]);
                }
                if(mb_type&MB_TYPE_BIDIR){
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_BIDIR, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&MB_TYPE_DIRECT){
                    int mx= s->b_direct_mv_table[xy][0];
                    int my= s->b_direct_mv_table[xy][1];
                    
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
#ifdef CONFIG_RISKY
                    ff_mpeg4_set_direct_mv(s, mx, my);
#endif
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_DIRECT, pb, pb2, tex_pb, 
                                 &dmin, &next_block, mx, my);
                }
                if(mb_type&MB_TYPE_INTRA){
                    s->mv_dir = 0;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 1;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_INTRA, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                    /* force cleaning of ac/dc pred stuff if needed ... */
                    if(s->h263_pred || s->h263_aic)
                        s->mbintra_table[mb_x + mb_y*s->mb_stride]=1;
                }
                copy_context_after_encode(s, &best_s, -1);
                
                pb_bits_count= get_bit_count(&s->pb);
                flush_put_bits(&s->pb);
                ff_copy_bits(&backup_s.pb, bit_buf[next_block^1], pb_bits_count);
                s->pb= backup_s.pb;
                
                if(s->data_partitioning){
                    pb2_bits_count= get_bit_count(&s->pb2);
                    flush_put_bits(&s->pb2);
                    ff_copy_bits(&backup_s.pb2, bit_buf2[next_block^1], pb2_bits_count);
                    s->pb2= backup_s.pb2;
                    
                    tex_pb_bits_count= get_bit_count(&s->tex_pb);
                    flush_put_bits(&s->tex_pb);
                    ff_copy_bits(&backup_s.tex_pb, bit_buf_tex[next_block^1], tex_pb_bits_count);
                    s->tex_pb= backup_s.tex_pb;
                }
                s->last_bits= get_bit_count(&s->pb);
            } else {
                int motion_x, motion_y;
                int intra_score;
                int inter_score= s->current_picture.mb_cmp_score[mb_x + mb_y*s->mb_stride];
                
              if(!(s->flags&CODEC_FLAG_HQ) && s->pict_type==P_TYPE){
                /* get luma score */
                if((s->avctx->mb_cmp&0xFF)==FF_CMP_SSE){
                    intra_score= (s->current_picture.mb_var[mb_x + mb_y*s->mb_stride]<<8) - 500; //FIXME dont scale it down so we dont have to fix it
                }else{
                    uint8_t *dest_y;

                    int mean= s->current_picture.mb_mean[mb_x + mb_y*s->mb_stride]; //FIXME
                    mean*= 0x01010101;
                    
                    dest_y  = s->new_picture.data[0] + (mb_y * 16 * s->linesize    ) + mb_x * 16;
                
                    for(i=0; i<16; i++){
                        *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 0]) = mean;
                        *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 4]) = mean;
                        *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 8]) = mean;
                        *(uint32_t*)(&s->me.scratchpad[i*s->linesize+12]) = mean;
                    }

                    s->mb_intra=1;
                    intra_score= s->dsp.mb_cmp[0](s, s->me.scratchpad, dest_y, s->linesize);
                                        
/*                    printf("intra:%7d inter:%7d var:%7d mc_var.%7d\n", intra_score>>8, inter_score>>8, 
                        s->current_picture.mb_var[mb_x + mb_y*s->mb_stride],
                        s->current_picture.mc_mb_var[mb_x + mb_y*s->mb_stride]);*/
                }
                
                /* get chroma score */
                if(s->avctx->mb_cmp&FF_CMP_CHROMA){
                    int i;
                    
                    s->mb_intra=1;
                    for(i=1; i<3; i++){
                        uint8_t *dest_c;
                        int mean;
                        
                        if(s->out_format == FMT_H263){
                            mean= (s->dc_val[i][mb_x + (mb_y+1)*(s->mb_width+2)] + 4)>>3; //FIXME not exact but simple ;)
                        }else{
                            mean= (s->last_dc[i] + 4)>>3;
                        }
                        dest_c = s->new_picture.data[i] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
                        
                        mean*= 0x01010101;
                        for(i=0; i<8; i++){
                            *(uint32_t*)(&s->me.scratchpad[i*s->uvlinesize+ 0]) = mean;
                            *(uint32_t*)(&s->me.scratchpad[i*s->uvlinesize+ 4]) = mean;
                        }
                        
                        intra_score+= s->dsp.mb_cmp[1](s, s->me.scratchpad, dest_c, s->uvlinesize);
                    }                
                }

                /* bias */
                switch(s->avctx->mb_cmp&0xFF){
                default:
                case FF_CMP_SAD:
                    intra_score+= 32*s->qscale;
                    break;
                case FF_CMP_SSE:
                    intra_score+= 24*s->qscale*s->qscale;
                    break;
                case FF_CMP_SATD:
                    intra_score+= 96*s->qscale;
                    break;
                case FF_CMP_DCT:
                    intra_score+= 48*s->qscale;
                    break;
                case FF_CMP_BIT:
                    intra_score+= 16;
                    break;
                case FF_CMP_PSNR:
                case FF_CMP_RD:
                    intra_score+= (s->qscale*s->qscale*109*8 + 64)>>7;
                    break;
                }

                if(intra_score < inter_score)
                    mb_type= MB_TYPE_INTRA;
              }  
                
                s->mv_type=MV_TYPE_16X16;
                // only one MB-Type possible
                
                switch(mb_type){
                case MB_TYPE_INTRA:
                    s->mv_dir = 0;
                    s->mb_intra= 1;
                    motion_x= s->mv[0][0][0] = 0;
                    motion_y= s->mv[0][0][1] = 0;
                    break;
                case MB_TYPE_INTER:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->p_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->p_mv_table[xy][1];
                    break;
                case MB_TYPE_INTER4V:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->motion_val[s->block_index[i]][0];
                        s->mv[0][i][1] = s->motion_val[s->block_index[i]][1];
                    }
                    motion_x= motion_y= 0;
                    break;
                case MB_TYPE_DIRECT:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
                    motion_x=s->b_direct_mv_table[xy][0];
                    motion_y=s->b_direct_mv_table[xy][1];
#ifdef CONFIG_RISKY
                    ff_mpeg4_set_direct_mv(s, motion_x, motion_y);
#endif
                    break;
                case MB_TYPE_BIDIR:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x=0;
                    motion_y=0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    break;
                case MB_TYPE_BACKWARD:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    motion_y= s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    break;
                case MB_TYPE_FORWARD:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
//                    printf(" %d %d ", motion_x, motion_y);
                    break;
                default:
                    motion_x=motion_y=0; //gcc warning fix
                    printf("illegal MB type\n");
                }

                encode_mb(s, motion_x, motion_y);

                // RAL: Update last macrobloc type
                s->last_mv_dir = s->mv_dir;
            }

            /* clean the MV table in IPS frames for direct mode in B frames */
            if(s->mb_intra /* && I,P,S_TYPE */){
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
            }

            MPV_decode_mb(s, s->block);
            
            if(s->flags&CODEC_FLAG_PSNR){
                int w= 16;
                int h= 16;

                if(s->mb_x*16 + 16 > s->width ) w= s->width - s->mb_x*16;
                if(s->mb_y*16 + 16 > s->height) h= s->height- s->mb_y*16;

                s->current_picture_ptr->error[0] += sse(
                    s,
                    s->new_picture    .data[0] + s->mb_x*16 + s->mb_y*s->linesize*16,
                    s->current_picture.data[0] + s->mb_x*16 + s->mb_y*s->linesize*16,
                    w, h, s->linesize);
                s->current_picture_ptr->error[1] += sse(
                    s,
                    s->new_picture    .data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    s->current_picture.data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    w>>1, h>>1, s->uvlinesize);
                s->current_picture_ptr->error[2] += sse(
                    s,
                    s->new_picture    .data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    s->current_picture.data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,
                    w>>1, h>>1, s->uvlinesize);
            }
//printf("MB %d %d bits\n", s->mb_x+s->mb_y*s->mb_stride, get_bit_count(&s->pb));
        }
    }
    emms_c();

#ifdef CONFIG_RISKY
    if(s->codec_id==CODEC_ID_MPEG4 && s->partitioned_frame)
        ff_mpeg4_merge_partitions(s);

    if (s->msmpeg4_version && s->msmpeg4_version<4 && s->pict_type == I_TYPE)
        msmpeg4_encode_ext_header(s);

    if(s->codec_id==CODEC_ID_MPEG4) 
        ff_mpeg4_stuffing(&s->pb);
#endif

    //if (s->gob_number)
    //    fprintf(stderr,"\nNumber of GOB: %d", s->gob_number);
    
    /* Send the last GOB if RTP */    
    if (s->rtp_mode) {
        flush_put_bits(&s->pb);
        pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
        /* Call the RTP callback to send the last GOB */
        if (s->rtp_callback)
            s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
        s->ptr_lastgob = pbBufPtr(&s->pb);
        //fprintf(stderr,"\nGOB: %2d size: %d (last)", s->gob_number, pdif);
    }
}

static int dct_quantize_trellis_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow){
    const int *qmat;
    const uint8_t *scantable= s->intra_scantable.scantable;
    int max=0;
    unsigned int threshold1, threshold2;
    int bias=0;
    int run_tab[65];
    int level_tab[65];
    int score_tab[65];
    int last_run=0;
    int last_level=0;
    int last_score= 0;
    int last_i= 0;
    int coeff[3][64];
    int coeff_count[64];
    int lambda, qmul, qadd, start_i, last_non_zero, i;
    const int esc_length= s->ac_esc_length;
    uint8_t * length;
    uint8_t * last_length;
    int score_limit=0;
    int left_limit= 0;
        
    s->dsp.fdct (block);

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
        if(s->mpeg_quant || s->codec_id== CODEC_ID_MPEG1VIDEO)
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

    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);

    for(i=start_i; i<64; i++) {
        const int j = scantable[i];
        const int k= i-start_i;
        int level = block[j];
        level = level * qmat[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QMAT_SHIFT;
                coeff[0][k]= level;
                coeff[1][k]= level-1;
//                coeff[2][k]= level-2;
            }else{
                level= (bias - level)>>QMAT_SHIFT;
                coeff[0][k]= -level;
                coeff[1][k]= -level+1;
//                coeff[2][k]= -level+2;
            }
            coeff_count[k]= FFMIN(level, 2);
            max |=level;
            last_non_zero = i;
        }else{
            coeff[0][k]= (level>>31)|1;
            coeff_count[k]= 1;
        }
    }
    
    *overflow= s->max_qcoeff < max; //overflow might have happend
    
    if(last_non_zero < start_i){
        memset(block + start_i, 0, (64-start_i)*sizeof(DCTELEM));
        return last_non_zero;
    }

    lambda= (qscale*qscale*64*105 + 64)>>7; //FIXME finetune
        
    score_tab[0]= 0;
    for(i=0; i<=last_non_zero - start_i; i++){
        int level_index, run, j;
        const int dct_coeff= block[ scantable[i + start_i] ];
        const int zero_distoration= dct_coeff*dct_coeff;
        int best_score=256*256*256*120;

        last_score += zero_distoration;
        for(level_index=0; level_index < coeff_count[i]; level_index++){
            int distoration;
            int level= coeff[level_index][i];
            int unquant_coeff;
            
            assert(level);

            if(s->out_format == FMT_H263){
                if(level>0){
                    unquant_coeff= level*qmul + qadd;
                }else{
                    unquant_coeff= level*qmul - qadd;
                }
            }else{ //MPEG1
                j= s->dsp.idct_permutation[ scantable[i + start_i] ]; //FIXME optimize
                if(s->mb_intra){
                    if (level < 0) {
                        unquant_coeff = (int)((-level) * qscale * s->intra_matrix[j]) >> 3;
                        unquant_coeff = -((unquant_coeff - 1) | 1);
                    } else {
                        unquant_coeff = (int)(  level  * qscale * s->intra_matrix[j]) >> 3;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                    }
                }else{
                    if (level < 0) {
                        unquant_coeff = ((((-level) << 1) + 1) * qscale * ((int) s->inter_matrix[j])) >> 4;
                        unquant_coeff = -((unquant_coeff - 1) | 1);
                    } else {
                        unquant_coeff = (((  level  << 1) + 1) * qscale * ((int) s->inter_matrix[j])) >> 4;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                    }
                }
                unquant_coeff<<= 3;
            }

            distoration= (unquant_coeff - dct_coeff) * (unquant_coeff - dct_coeff);
            level+=64;
            if((level&(~127)) == 0){
                for(run=0; run<=i - left_limit; run++){
                    int score= distoration + length[UNI_AC_ENC_INDEX(run, level)]*lambda;
                    score += score_tab[i-run];
                    
                    if(score < best_score){
                        best_score= 
                        score_tab[i+1]= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if(s->out_format == FMT_H263){
                    for(run=0; run<=i - left_limit; run++){
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
                for(run=0; run<=i - left_limit; run++){
                    int score= distoration + score_tab[i-run];
                    
                    if(score < best_score){
                        best_score= 
                        score_tab[i+1]= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if(s->out_format == FMT_H263){
                    for(run=0; run<=i - left_limit; run++){
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

        for(j=left_limit; j<=i; j++){
            score_tab[j] += zero_distoration;
        }
        score_limit+= zero_distoration;
        if(score_tab[i+1] < score_limit)
            score_limit= score_tab[i+1];
        
        //Note: there is a vlc code in mpeg4 which is 1 bit shorter then another one with a shorter run and the same level
        while(score_tab[ left_limit ] > score_limit + lambda) left_limit++;
    }

        //FIXME add some cbp penalty

    if(s->out_format != FMT_H263){
        last_score= 256*256*256*120;
        for(i= left_limit; i<=last_non_zero - start_i + 1; i++){
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
    
    last_non_zero= last_i - 1 + start_i;
    memset(block + start_i, 0, (64-start_i)*sizeof(DCTELEM));
    
    if(last_non_zero < start_i)
        return last_non_zero;
    
    i= last_i;
    assert(last_level);
//FIXME use permutated scantable
    block[ s->dsp.idct_permutation[ scantable[last_non_zero] ] ]= last_level;
    i -= last_run + 1;
    
    for(;i>0 ; i -= run_tab[i] + 1){
        const int j= s->dsp.idct_permutation[ scantable[i - 1 + start_i] ];
    
        block[j]= level_tab[i];
        assert(block[j]);
    }

    return last_non_zero;
}

static int dct_quantize_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;
    const uint8_t *scantable= s->intra_scantable.scantable;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;

    s->dsp.fdct (block);

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
        i = 1;
        last_non_zero = 0;
        qmat = s->q_intra_matrix[qscale];
        bias= s->intra_quant_bias<<(QMAT_SHIFT - QUANT_BIAS_SHIFT);
    } else {
        i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        bias= s->inter_quant_bias<<(QMAT_SHIFT - QUANT_BIAS_SHIFT);
    }
    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);

    for(;i<64;i++) {
        j = scantable[i];
        level = block[j];
        level = level * qmat[j];

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
            last_non_zero = i;
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

static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];
    
    if (s->mb_intra) {
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
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    } else {
        i = 0;
        quant_matrix = s->inter_matrix;
        for(;i<=nCoeffs;i++) {
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
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    }
}

static void dct_unquantize_mpeg2_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];
    
    if (s->mb_intra) {
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
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    } else {
        int sum=-1;
        i = 0;
        quant_matrix = s->inter_matrix;
        for(;i<=nCoeffs;i++) {
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
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
                sum+=level;
            }
        }
        block[63]^=sum&1;
    }
}


static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);
    
    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;
    
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4) 
                block[0] = block[0] * s->y_dc_scale;
            else
                block[0] = block[0] * s->c_dc_scale;
        }else
            qadd = 0;
        i = 1;
        nCoeffs= 63; //does not allways use zigzag table 
    } else {
        i = 0;
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];
    }

    for(;i<=nCoeffs;i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
            block[i] = level;
        }
    }
}


static const AVOption mpeg4_options[] =
{
    AVOPTION_CODEC_INT("bitrate", "desired video bitrate", bit_rate, 4, 240000000, 800000),
    AVOPTION_CODEC_FLAG("vhq", "very high quality", flags, CODEC_FLAG_HQ, 0),
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

AVCodec mpeg1video_encoder = {
    "mpeg1video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

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

