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
 
#include <ctype.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "simple_idct.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

//#undef NDEBUG
//#include <assert.h>

static void encode_picture(MpegEncContext *s, int picture_number);
static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg2_c(MpegEncContext *s,
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void draw_edges_c(UINT8 *buf, int wrap, int width, int height, int w);
static int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);

void (*draw_edges)(UINT8 *buf, int wrap, int width, int height, int w)= draw_edges_c;
static void emulated_edge_mc(MpegEncContext *s, UINT8 *src, int linesize, int block_w, int block_h, 
                                    int src_x, int src_y, int w, int h);

#define EDGE_WIDTH 16

/* enable all paranoid tests for rounding, overflows, etc... */
//#define PARANOID

//#define DEBUG


/* for jpeg fast DCT */
#define CONST_BITS 14

static const unsigned short aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867, 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

/* Input permutation for the simple_idct_mmx */
static const UINT8 simple_mmx_permutation[64]={
	0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D, 
	0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D, 
	0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D, 
	0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F, 
	0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F, 
	0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D, 
	0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F, 
	0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

static UINT8 h263_chroma_roundtab[16] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
};

static UINT16 default_mv_penalty[MAX_FCODE+1][MAX_MV*2+1];
static UINT8 default_fcode_tab[MAX_MV*2+1];

/* default motion estimation */
int motion_estimation_method = ME_EPZS;

static void convert_matrix(MpegEncContext *s, int (*qmat)[64], uint16_t (*qmat16)[64], uint16_t (*qmat16_bias)[64],
                           const UINT16 *quant_matrix, int bias, int qmin, int qmax)
{
    int qscale;

    for(qscale=qmin; qscale<=qmax; qscale++){
        int i;
        if (s->fdct == ff_jpeg_fdct_islow) {
            for(i=0;i<64;i++) {
                const int j= s->idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((UINT64_C(1) << QMAT_SHIFT) / 
                                (qscale * quant_matrix[j]));
            }
        } else if (s->fdct == fdct_ifast) {
            for(i=0;i<64;i++) {
                const int j= s->idct_permutation[i];
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][i] = (int)((UINT64_C(1) << (QMAT_SHIFT + 14)) / 
                                (aanscales[i] * qscale * quant_matrix[j]));
            }
        } else {
            for(i=0;i<64;i++) {
                const int j= s->idct_permutation[i];
                /* We can safely suppose that 16 <= quant_matrix[i] <= 255
                   So 16           <= qscale * quant_matrix[i]             <= 7905
                   so (1<<19) / 16 >= (1<<19) / (qscale * quant_matrix[i]) >= (1<<19) / 7905
                   so 32768        >= (1<<19) / (qscale * quant_matrix[i]) >= 67
                */
                qmat  [qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
                qmat16[qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[j]);

                if(qmat16[qscale][i]==0 || qmat16[qscale][i]==128*256) qmat16[qscale][i]=128*256-1;
                qmat16_bias[qscale][i]= ROUNDED_DIV(bias<<(16-QUANT_BIAS_SHIFT), qmat16[qscale][i]);
            }
        }
    }
}
// move into common.c perhaps 
#define CHECKED_ALLOCZ(p, size)\
{\
    p= av_mallocz(size);\
    if(p==NULL){\
        perror("malloc");\
        goto fail;\
    }\
}

void ff_init_scantable(MpegEncContext *s, ScanTable *st, const UINT8 *src_scantable){
    int i;
    int end;
    
    st->scantable= src_scantable;

    for(i=0; i<64; i++){
        int j;
        j = src_scantable[i];
        st->permutated[i] = s->idct_permutation[j];
    }
    
    end=-1;
    for(i=0; i<64; i++){
        int j;
        j = st->permutated[i];
        if(j>end) end=j;
        st->raster_end[i]= end;
    }
}

/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
static void ff_jref_idct_put(UINT8 *dest, int line_size, DCTELEM *block)
{
    j_rev_dct (block);
    put_pixels_clamped(block, dest, line_size);
}
static void ff_jref_idct_add(UINT8 *dest, int line_size, DCTELEM *block)
{
    j_rev_dct (block);
    add_pixels_clamped(block, dest, line_size);
}

/* init common dct for both encoder and decoder */
int DCT_common_init(MpegEncContext *s)
{
    int i;

    s->dct_unquantize_h263 = dct_unquantize_h263_c;
    s->dct_unquantize_mpeg1 = dct_unquantize_mpeg1_c;
    s->dct_unquantize_mpeg2 = dct_unquantize_mpeg2_c;
    s->dct_quantize= dct_quantize_c;

    if(s->avctx->dct_algo==FF_DCT_FASTINT)
        s->fdct = fdct_ifast;
    else
        s->fdct = ff_jpeg_fdct_islow; //slow/accurate/default

    if(s->avctx->idct_algo==FF_IDCT_INT){
        s->idct_put= ff_jref_idct_put;
        s->idct_add= ff_jref_idct_add;
        s->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
    }else{ //accurate/default
        s->idct_put= simple_idct_put;
        s->idct_add= simple_idct_add;
        s->idct_permutation_type= FF_NO_IDCT_PERM;
    }
        
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
    MPV_common_init_armv4l();
#endif

    switch(s->idct_permutation_type){
    case FF_NO_IDCT_PERM:
        for(i=0; i<64; i++)
            s->idct_permutation[i]= i;
        break;
    case FF_LIBMPEG2_IDCT_PERM:
        for(i=0; i<64; i++)
            s->idct_permutation[i]= (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
        break;
    case FF_SIMPLE_IDCT_PERM:
        for(i=0; i<64; i++)
            s->idct_permutation[i]= simple_mmx_permutation[i];
        break;
    case FF_TRANSPOSE_IDCT_PERM:
        for(i=0; i<64; i++)
            s->idct_permutation[i]= ((i&7)<<3) | (i>>3);
        break;
    default:
        fprintf(stderr, "Internal error, IDCT permutation not set\n");
        return -1;
    }


    /* load & permutate scantables
       note: only wmv uses differnt ones 
    */
    ff_init_scantable(s, &s->inter_scantable  , ff_zigzag_direct);
    ff_init_scantable(s, &s->intra_scantable  , ff_zigzag_direct);
    ff_init_scantable(s, &s->intra_h_scantable, ff_alternate_horizontal_scan);
    ff_init_scantable(s, &s->intra_v_scantable, ff_alternate_vertical_scan);

    return 0;
}

/* init common structure for both encoder and decoder */
int MPV_common_init(MpegEncContext *s)
{
    UINT8 *pict;
    int y_size, c_size, yc_size, i;

    DCT_common_init(s);
    
    s->flags= s->avctx->flags;

    s->mb_width = (s->width + 15) / 16;
    s->mb_height = (s->height + 15) / 16;
    
    y_size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
    c_size = (s->mb_width + 2) * (s->mb_height + 2);
    yc_size = y_size + 2 * c_size;
    
    /* set default edge pos, will be overriden in decode_header if needed */
    s->h_edge_pos= s->mb_width*16;
    s->v_edge_pos= s->mb_height*16;
    
    /* convert fourcc to upper case */
    s->avctx->fourcc=   toupper( s->avctx->fourcc     &0xFF)          
                     + (toupper((s->avctx->fourcc>>8 )&0xFF)<<8 )
                     + (toupper((s->avctx->fourcc>>16)&0xFF)<<16) 
                     + (toupper((s->avctx->fourcc>>24)&0xFF)<<24);

    s->mb_num = s->mb_width * s->mb_height;
    
    if(!(s->flags&CODEC_FLAG_DR1)){
      s->linesize   = s->mb_width * 16 + 2 * EDGE_WIDTH;
      s->uvlinesize = s->mb_width * 8  +     EDGE_WIDTH;

      for(i=0;i<3;i++) {
        int w, h, shift, pict_start, size;

        w = s->linesize;
        h = s->mb_height * 16 + 2 * EDGE_WIDTH;
        shift = (i == 0) ? 0 : 1;
        size = (s->linesize>>shift) * (h >> shift);
        pict_start = (s->linesize>>shift) * (EDGE_WIDTH >> shift) + (EDGE_WIDTH >> shift);

        CHECKED_ALLOCZ(pict, size)
        s->last_picture_base[i] = pict;
        s->last_picture[i] = pict + pict_start;
        if(i>0) memset(s->last_picture_base[i], 128, size);
    
        CHECKED_ALLOCZ(pict, size)
        s->next_picture_base[i] = pict;
        s->next_picture[i] = pict + pict_start;
        if(i>0) memset(s->next_picture_base[i], 128, size);
        
        if (s->has_b_frames || s->codec_id==CODEC_ID_MPEG4) {
        /* Note the MPEG4 stuff is here cuz of buggy encoders which dont set the low_delay flag but 
           do low-delay encoding, so we cant allways distinguish b-frame containing streams from low_delay streams */
            CHECKED_ALLOCZ(pict, size)
            s->aux_picture_base[i] = pict;
            s->aux_picture[i] = pict + pict_start;
            if(i>0) memset(s->aux_picture_base[i], 128, size);
        }
      }
      s->ip_buffer_count= 2;
    }
    
    CHECKED_ALLOCZ(s->edge_emu_buffer, (s->width+64)*2*17*2); //(width + edge + align)*interlaced*MBsize*tolerance
    
    if (s->encoding) {
        int j;
        int mv_table_size= (s->mb_width+2)*(s->mb_height+2);
        
        CHECKED_ALLOCZ(s->mb_var   , s->mb_num * sizeof(INT16))
        CHECKED_ALLOCZ(s->mc_mb_var, s->mb_num * sizeof(INT16))
        CHECKED_ALLOCZ(s->mb_mean  , s->mb_num * sizeof(INT8))

        /* Allocate MV tables */
        CHECKED_ALLOCZ(s->p_mv_table            , mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_forw_mv_table       , mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_back_mv_table       , mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_bidir_forw_mv_table , mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_bidir_back_mv_table , mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_direct_forw_mv_table, mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_direct_back_mv_table, mv_table_size * 2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->b_direct_mv_table     , mv_table_size * 2 * sizeof(INT16))

        CHECKED_ALLOCZ(s->me_scratchpad,  s->linesize*16*3*sizeof(uint8_t))
        
        CHECKED_ALLOCZ(s->me_map      , ME_MAP_SIZE*sizeof(uint32_t))
        CHECKED_ALLOCZ(s->me_score_map, ME_MAP_SIZE*sizeof(uint16_t))

        if(s->max_b_frames){
            for(j=0; j<REORDER_BUFFER_SIZE; j++){
                int i;
                for(i=0;i<3;i++) {
                    int w, h, shift, size;

                    w = s->linesize;
                    h = s->mb_height * 16;
                    shift = (i == 0) ? 0 : 1;
                    size = (w >> shift) * (h >> shift);

                    CHECKED_ALLOCZ(pict, size);
                    s->picture_buffer[j][i] = pict;
                }
            }
        }

        if(s->codec_id==CODEC_ID_MPEG4){
            CHECKED_ALLOCZ(s->tex_pb_buffer, PB_BUFFER_SIZE);
            CHECKED_ALLOCZ(   s->pb2_buffer, PB_BUFFER_SIZE);
        }
        
        if(s->msmpeg4_version){
            CHECKED_ALLOCZ(s->ac_stats, 2*2*(MAX_LEVEL+1)*(MAX_RUN+1)*2*sizeof(int));
        }
        CHECKED_ALLOCZ(s->avctx->stats_out, 256);
    }
        
    CHECKED_ALLOCZ(s->error_status_table, s->mb_num*sizeof(UINT8))
    
    if (s->out_format == FMT_H263 || s->encoding) {
        int size;
        /* Allocate MB type table */
        CHECKED_ALLOCZ(s->mb_type  , s->mb_num * sizeof(UINT8))

        /* MV prediction */
        size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        CHECKED_ALLOCZ(s->motion_val, size * 2 * sizeof(INT16));
    }

    if(s->codec_id==CODEC_ID_MPEG4){
        /* interlaced direct mode decoding tables */
        CHECKED_ALLOCZ(s->field_mv_table, s->mb_num*2*2 * sizeof(INT16))
        CHECKED_ALLOCZ(s->field_select_table, s->mb_num*2* sizeof(INT8))
    }
    /* 4mv b frame decoding table */
    //note this is needed for h263 without b frames too (segfault on damaged streams otherwise)
    CHECKED_ALLOCZ(s->co_located_type_table, s->mb_num * sizeof(UINT8))
    if (s->out_format == FMT_H263) {
        /* ac values */
        CHECKED_ALLOCZ(s->ac_val[0], yc_size * sizeof(INT16) * 16);
        s->ac_val[1] = s->ac_val[0] + y_size;
        s->ac_val[2] = s->ac_val[1] + c_size;
        
        /* cbp values */
        CHECKED_ALLOCZ(s->coded_block, y_size);
        
        /* divx501 bitstream reorder buffer */
        CHECKED_ALLOCZ(s->bitstream_buffer, BITSTREAM_BUFFER_SIZE);
        
        /* cbp, ac_pred, pred_dir */
        CHECKED_ALLOCZ(s->cbp_table  , s->mb_num * sizeof(UINT8))
        CHECKED_ALLOCZ(s->pred_dir_table, s->mb_num * sizeof(UINT8))
    }
    
    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        //MN: we need these for error resilience of intra-frames
        CHECKED_ALLOCZ(s->dc_val[0], yc_size * sizeof(INT16));
        s->dc_val[1] = s->dc_val[0] + y_size;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for(i=0;i<yc_size;i++)
            s->dc_val[0][i] = 1024;
    }

    CHECKED_ALLOCZ(s->qscale_table  , s->mb_num * sizeof(UINT8))
    
    /* which mb is a intra block */
    CHECKED_ALLOCZ(s->mbintra_table, s->mb_num);
    memset(s->mbintra_table, 1, s->mb_num);
    
    /* default structure is frame */
    s->picture_structure = PICT_FRAME;
    
    /* init macroblock skip table */
    CHECKED_ALLOCZ(s->mbskip_table, s->mb_num+1);
    //Note the +1 is for a quicker mpeg4 slice_end detection
    
    s->block= s->blocks[0];

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
    av_freep(&s->mb_var);
    av_freep(&s->mc_mb_var);
    av_freep(&s->mb_mean);
    av_freep(&s->p_mv_table);
    av_freep(&s->b_forw_mv_table);
    av_freep(&s->b_back_mv_table);
    av_freep(&s->b_bidir_forw_mv_table);
    av_freep(&s->b_bidir_back_mv_table);
    av_freep(&s->b_direct_forw_mv_table);
    av_freep(&s->b_direct_back_mv_table);
    av_freep(&s->b_direct_mv_table);
    av_freep(&s->motion_val);
    av_freep(&s->dc_val[0]);
    av_freep(&s->ac_val[0]);
    av_freep(&s->coded_block);
    av_freep(&s->mbintra_table);
    av_freep(&s->cbp_table);
    av_freep(&s->pred_dir_table);
    av_freep(&s->qscale_table);
    av_freep(&s->me_scratchpad);
    av_freep(&s->me_map);
    av_freep(&s->me_score_map);
    
    av_freep(&s->mbskip_table);
    av_freep(&s->bitstream_buffer);
    av_freep(&s->tex_pb_buffer);
    av_freep(&s->pb2_buffer);
    av_freep(&s->edge_emu_buffer);
    av_freep(&s->co_located_type_table);
    av_freep(&s->field_mv_table);
    av_freep(&s->field_select_table);
    av_freep(&s->avctx->stats_out);
    av_freep(&s->ac_stats);
    av_freep(&s->error_status_table);
    
    for(i=0;i<3;i++) {
        int j;
        if(!(s->flags&CODEC_FLAG_DR1)){
            av_freep(&s->last_picture_base[i]);
            av_freep(&s->next_picture_base[i]);
            av_freep(&s->aux_picture_base[i]);
        }
        s->last_picture_base[i]=
        s->next_picture_base[i]=
        s->aux_picture_base [i] = NULL;
        s->last_picture[i]=
        s->next_picture[i]=
        s->aux_picture [i] = NULL;

        for(j=0; j<REORDER_BUFFER_SIZE; j++){
            av_freep(&s->picture_buffer[j][i]);
        }
    }
    s->context_initialized = 0;
}

/* init video encoder */
int MPV_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    avctx->pix_fmt = PIX_FMT_YUV420P;

    s->bit_rate = avctx->bit_rate;
    s->bit_rate_tolerance = avctx->bit_rate_tolerance;
    s->frame_rate = avctx->frame_rate;
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
    s->qmin= avctx->qmin;
    s->qmax= avctx->qmax;
    s->max_qdiff= avctx->max_qdiff;
    s->qcompress= avctx->qcompress;
    s->qblur= avctx->qblur;
    s->avctx = avctx;
    s->aspect_ratio_info= avctx->aspect_ratio_info;
    if (avctx->aspect_ratio_info == FF_ASPECT_EXTENDED)
    {
	s->aspected_width = avctx->aspected_width;
	s->aspected_height = avctx->aspected_height;
    }
    s->flags= avctx->flags;
    s->max_b_frames= avctx->max_b_frames;
    s->b_frame_strategy= avctx->b_frame_strategy;
    s->codec_id= avctx->codec->id;
    s->luma_elim_threshold  = avctx->luma_elim_threshold;
    s->chroma_elim_threshold= avctx->chroma_elim_threshold;
    s->strict_std_compliance= avctx->strict_std_compliance;
    s->data_partitioning= avctx->flags & CODEC_FLAG_PART;
    s->mpeg_quant= avctx->mpeg_quant;

    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }

    /* ME algorithm */
    if (avctx->me_method == 0)
        /* For compatibility */
        s->me_method = motion_estimation_method;
    else
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

    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        avctx->delay=0; //FIXME not sure, should check the spec
        break;
    case CODEC_ID_MJPEG:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        s->mjpeg_write_tables = 1; /* write all tables */
	s->mjpeg_data_only_frames = 0; /* write all the needed headers */
        s->mjpeg_vsample[0] = 2; /* set up default sampling factors */
        s->mjpeg_vsample[1] = 1; /* the only currently supported values */
        s->mjpeg_vsample[2] = 1; 
        s->mjpeg_hsample[0] = 2;
        s->mjpeg_hsample[1] = 1; 
        s->mjpeg_hsample[2] = 1; 
        if (mjpeg_init(s) < 0)
            return -1;
        avctx->delay=0;
        break;
    case CODEC_ID_H263:
        if (h263_get_picture_format(s->width, s->height) == 7) {
            printf("Input picture size isn't suitable for h263 codec! try h263+\n");
            return -1;
        }
        s->out_format = FMT_H263;
        avctx->delay=0;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->h263_plus = 1;
        s->unrestricted_mv = 1;
        s->h263_aic = 1;
        
        /* These are just to be sure */
        s->umvplus = 0;
        s->umvplus_dec = 0;
        avctx->delay=0;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
        avctx->delay=0;
        break;
    case CODEC_ID_MPEG4:
        s->out_format = FMT_H263;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->has_b_frames= s->max_b_frames ? 1 : 0;
        s->low_delay= !s->has_b_frames;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1);
        break;
    case CODEC_ID_MSMPEG4V1:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 1;
        avctx->delay=0;
        break;
    case CODEC_ID_MSMPEG4V2:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 2;
        avctx->delay=0;
        break;
    case CODEC_ID_MSMPEG4V3:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 3;
        avctx->delay=0;
        break;
    case CODEC_ID_WMV1:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 4;
        avctx->delay=0;
        break;
    case CODEC_ID_WMV2:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 5;
        avctx->delay=0;
        break;
    default:
        return -1;
    }
    
    { /* set up some save defaults, some codecs might override them later */
        static int done=0;
        if(!done){
            int i;
            done=1;
            memset(default_mv_penalty, 0, sizeof(UINT16)*(MAX_FCODE+1)*(2*MAX_MV+1));
            memset(default_fcode_tab , 0, sizeof(UINT8)*(2*MAX_MV+1));

            for(i=-16; i<16; i++){
                default_fcode_tab[i + MAX_MV]= 1;
            }
        }
    }
    s->mv_penalty= default_mv_penalty;
    s->fcode_tab= default_fcode_tab;
    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
 
    /* dont use mv_penalty table for crap MV as it would be confused */
    if (s->me_method < ME_EPZS) s->mv_penalty = default_mv_penalty;

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;
    
    if (s->out_format == FMT_H263)
        h263_encode_init(s);
    else if (s->out_format == FMT_MPEG1)
        ff_mpeg1_encode_init(s);
    if(s->msmpeg4_version)
        ff_msmpeg4_encode_init(s);

    /* init default q matrix */
    for(i=0;i<64;i++) {
        int j= s->idct_permutation[i];
        if(s->codec_id==CODEC_ID_MPEG4 && s->mpeg_quant){
            s->intra_matrix[j] = ff_mpeg4_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg4_default_non_intra_matrix[i];
        }else if(s->out_format == FMT_H263){
            s->intra_matrix[j] =
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        }else{ /* mpeg1 */
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

/* draw the edges of width 'w' of an image of size width, height */
//FIXME check that this is ok for mpeg4 interlaced
static void draw_edges_c(UINT8 *buf, int wrap, int width, int height, int w)
{
    UINT8 *ptr, *last_line;
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

/* generic function for encode/decode called before a frame is coded/decoded */
int MPV_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int i;
    UINT8 *tmp;

    s->mb_skiped = 0;
    avctx->mbskip_table= s->mbskip_table;

    if(avctx->flags&CODEC_FLAG_DR1){
        if(avctx->get_buffer_callback(avctx, s->width, s->height, s->pict_type) < 0){
            fprintf(stderr, "get_buffer() failed\n");
            return -1;
        }

        s->linesize  = avctx->dr_stride;
        s->uvlinesize= avctx->dr_uvstride;
        s->ip_buffer_count= avctx->dr_ip_buffer_count;
    }
    avctx->dr_ip_buffer_count= s->ip_buffer_count;
    
    if (s->pict_type == B_TYPE) {
        for(i=0;i<3;i++) {
            if(avctx->flags&CODEC_FLAG_DR1)
                s->aux_picture[i]= avctx->dr_buffer[i];
            
            //FIXME the following should never be needed, the decoder should drop b frames if no reference is available
            if(s->next_picture[i]==NULL)
                s->next_picture[i]= s->aux_picture[i];
            if(s->last_picture[i]==NULL)
                s->last_picture[i]= s->next_picture[i];

            s->current_picture[i] = s->aux_picture[i];
        }
    } else {
        for(i=0;i<3;i++) {
            /* swap next and last */
            if(avctx->flags&CODEC_FLAG_DR1)
                tmp= avctx->dr_buffer[i];
            else
                tmp = s->last_picture[i];

            s->last_picture[i] = s->next_picture[i];
            s->next_picture[i] = tmp;
            s->current_picture[i] = tmp;

            if(s->last_picture[i]==NULL)
                s->last_picture[i]= s->next_picture[i];

            s->last_dr_opaque= s->next_dr_opaque;
            s->next_dr_opaque= avctx->dr_opaque_frame;

            if(s->has_b_frames && s->last_dr_opaque && s->codec_id!=CODEC_ID_SVQ1)
                avctx->dr_opaque_frame= s->last_dr_opaque;
            else
                avctx->dr_opaque_frame= s->next_dr_opaque;
        }
    }
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
    s->avctx->key_frame   = (s->pict_type == I_TYPE);
    s->avctx->pict_type   = s->pict_type;

    /* draw edge for correct motion prediction if outside */
    if (s->pict_type != B_TYPE && !s->intra_only && !(s->flags&CODEC_FLAG_EMU_EDGE)) {
        draw_edges(s->current_picture[0], s->linesize  , s->h_edge_pos   , s->v_edge_pos   , EDGE_WIDTH  );
        draw_edges(s->current_picture[1], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
        draw_edges(s->current_picture[2], s->uvlinesize, s->h_edge_pos>>1, s->v_edge_pos>>1, EDGE_WIDTH/2);
    }
    emms_c();
    
    s->last_pict_type    = s->pict_type;
    if(s->pict_type!=B_TYPE){
        s->last_non_b_pict_type= s->pict_type;
        s->num_available_buffers++;
        if(s->num_available_buffers>2) s->num_available_buffers= 2;
    }
}

/* reorder input for encoding */
void reorder_input(MpegEncContext *s, AVPicture *pict)
{
    int i, j, index;
            
    if(s->max_b_frames > FF_MAX_B_FRAMES) s->max_b_frames= FF_MAX_B_FRAMES;

//        delay= s->max_b_frames+1; (or 0 if no b frames cuz decoder diff)

    for(j=0; j<REORDER_BUFFER_SIZE-1; j++){
        s->coded_order[j]= s->coded_order[j+1];
    }
    s->coded_order[j].picture[0]= s->coded_order[j].picture[1]= s->coded_order[j].picture[2]= NULL; //catch uninitalized buffers
    s->coded_order[j].pict_type=0;

    switch(s->input_pict_type){
    default: 
    case I_TYPE:
    case S_TYPE:
    case P_TYPE:
        index= s->max_b_frames - s->b_frames_since_non_b;
        s->b_frames_since_non_b=0;
        break;            
    case B_TYPE:
        index= s->max_b_frames + 1;
        s->b_frames_since_non_b++;
        break;          
    }
//printf("index:%d type:%d strides: %d %d\n", index, s->input_pict_type, pict->linesize[0], s->linesize);
    if(   (index==0 || (s->flags&CODEC_FLAG_INPUT_PRESERVED))
       && pict->linesize[0] == s->linesize
       && pict->linesize[1] == s->uvlinesize
       && pict->linesize[2] == s->uvlinesize){
//printf("ptr\n");
        for(i=0; i<3; i++){
            s->coded_order[index].picture[i]= pict->data[i];
        }
    }else{
//printf("copy\n");
        for(i=0; i<3; i++){
            uint8_t *src = pict->data[i];
            uint8_t *dest;
            int src_wrap = pict->linesize[i];
            int dest_wrap = s->linesize;
            int w = s->width;
            int h = s->height;

            if(index==0) dest= s->last_picture[i]+16; //is current_picture indeed but the switch hapens after reordering
            else         dest= s->picture_buffer[s->picture_buffer_index][i];

            if (i >= 1) {
                dest_wrap >>= 1;
                w >>= 1;
                h >>= 1;
            }

            s->coded_order[index].picture[i]= dest;
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += dest_wrap;
                src += src_wrap;
            }
        }
        if(index!=0){
            s->picture_buffer_index++;
            if(s->picture_buffer_index >= REORDER_BUFFER_SIZE) s->picture_buffer_index=0;
        }
    }
    s->coded_order[index].pict_type = s->input_pict_type;
    s->coded_order[index].qscale    = s->input_qscale;
    s->coded_order[index].force_type= s->force_input_type;
    s->coded_order[index].picture_in_gop_number= s->input_picture_in_gop_number;
    s->coded_order[index].picture_number= s->input_picture_number;

    for(i=0; i<3; i++){
        s->new_picture[i]= s->coded_order[0].picture[i];
    }
}

int MPV_encode_picture(AVCodecContext *avctx,
                       unsigned char *buf, int buf_size, void *data)
{
    MpegEncContext *s = avctx->priv_data;
    AVPicture *pict = data;

    s->input_qscale = avctx->quality;

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    if(avctx->flags&CODEC_FLAG_TYPE){
        s->input_pict_type=
        s->force_input_type= avctx->key_frame ? I_TYPE : P_TYPE;
    }else if(s->flags&CODEC_FLAG_PASS2){
        s->input_pict_type=
        s->force_input_type= s->rc_context.entry[s->input_picture_number].new_pict_type;
    }else{
        s->force_input_type=0;
        if (!s->intra_only) {
            /* first picture of GOP is intra */
            if (s->input_picture_in_gop_number % s->gop_size==0){
                s->input_pict_type = I_TYPE;
            }else if(s->max_b_frames==0){
                s->input_pict_type = P_TYPE;
            }else{
                if(s->b_frames_since_non_b < s->max_b_frames) //FIXME more IQ
                    s->input_pict_type = B_TYPE;
                else
                    s->input_pict_type = P_TYPE;
            }
        } else {
            s->input_pict_type = I_TYPE;
        }
    }

    if(s->input_pict_type==I_TYPE)
        s->input_picture_in_gop_number=0;
    
    reorder_input(s, pict);
    
    /* output? */
    if(s->coded_order[0].picture[0]){

        s->pict_type= s->coded_order[0].pict_type;
        if (s->fixed_qscale) /* the ratecontrol needs the last qscale so we dont touch it for CBR */
            s->qscale= s->coded_order[0].qscale;
        s->force_type= s->coded_order[0].force_type;
        s->picture_in_gop_number= s->coded_order[0].picture_in_gop_number;
        s->picture_number= s->coded_order[0].picture_number;

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

        if(!s->fixed_qscale)
            avctx->quality = s->qscale;
        
        if(s->flags&CODEC_FLAG_PASS1)
            ff_write_pass1_stats(s);
    
    }

    s->input_picture_number++;
    s->input_picture_in_gop_number++;

    flush_put_bits(&s->pb);
    s->frame_bits  = (pbBufPtr(&s->pb) - s->pb.buf) * 8;
    
    s->total_bits += s->frame_bits;
    avctx->frame_bits  = s->frame_bits;
//printf("fcode: %d, type: %d, head: %d, mv: %d, misc: %d, frame: %d, itex: %d, ptex: %d\n", 
//s->f_code, avctx->key_frame, s->header_bits, s->mv_bits, s->misc_bits, s->frame_bits, s->i_tex_bits, s->p_tex_bits);
#if 0 //dump some stats to stats.txt for testing/debuging
if(s->max_b_frames==0)
{
    static FILE *f=NULL;
    if(!f) f= fopen("stats.txt", "wb");
    get_psnr(pict->data, s->current_picture,
             pict->linesize, s->linesize, avctx);
    fprintf(f, "%7d, %7d, %2.4f\n", pbBufPtr(&s->pb) - s->pb.buf, s->qscale, avctx->psnr_y);
}
#endif

    if (avctx->get_psnr) {
        /* At this point pict->data should have the original frame   */
        /* an s->current_picture should have the coded/decoded frame */
        get_psnr(pict->data, s->current_picture,
                 pict->linesize, s->linesize, avctx);
//        printf("%f\n", avctx->psnr_y);
    }
    return pbBufPtr(&s->pb) - s->pb.buf;
}

static inline void gmc1_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset)
{
    UINT8 *ptr;
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
        if(src_x<0 || src_y<0 || src_x + (motion_x&15) + 16 > s->h_edge_pos
                              || src_y + (motion_y&15) + 16 > s->v_edge_pos){
            emulated_edge_mc(s, ptr, linesize, 17, 17, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
            ptr= s->edge_emu_buffer;
            emu=1;
        }
    }
    
    if((motion_x|motion_y)&7){
        ff_gmc1(dest_y  , ptr  , linesize, 16, motion_x&15, motion_y&15, 128 - s->no_rounding);
        ff_gmc1(dest_y+8, ptr+8, linesize, 16, motion_x&15, motion_y&15, 128 - s->no_rounding);
    }else{
        int dxy;
        
        dxy= ((motion_x>>3)&1) | ((motion_y>>2)&2);
        if (s->no_rounding){
            put_no_rnd_pixels_tab[0][dxy](dest_y, ptr, linesize, 16);
        }else{
            put_pixels_tab       [0][dxy](dest_y, ptr, linesize, 16);
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
    if(emu){
        emulated_edge_mc(s, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    ff_gmc1(dest_cb + (dest_offset>>1), ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);
    
    ptr = ref_picture[2] + offset;
    if(emu){
        emulated_edge_mc(s, ptr, uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer;
    }
    ff_gmc1(dest_cr + (dest_offset>>1), ptr, uvlinesize, 8, motion_x&15, motion_y&15, 128 - s->no_rounding);
    
    return;
}

static inline void gmc_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset)
{
    UINT8 *ptr;
    int linesize, uvlinesize;
    const int a= s->sprite_warping_accuracy;
    int ox, oy;

    linesize = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0] + src_offset;

    dest_y+=dest_offset;
    
    ox= s->sprite_offset[0][0] + s->sprite_delta[0][0]*s->mb_x*16 + s->sprite_delta[0][1]*s->mb_y*16;
    oy= s->sprite_offset[0][1] + s->sprite_delta[1][0]*s->mb_x*16 + s->sprite_delta[1][1]*s->mb_y*16;

    ff_gmc(dest_y, ptr, linesize, 16, 
           ox, 
           oy, 
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1], 
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos, s->v_edge_pos);
    ff_gmc(dest_y+8, ptr, linesize, 16, 
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
    ff_gmc(dest_cb, ptr, uvlinesize, 8, 
           ox, 
           oy, 
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1], 
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos>>1, s->v_edge_pos>>1);
    
    ptr = ref_picture[2] + (src_offset>>1);
    ff_gmc(dest_cr, ptr, uvlinesize, 8, 
           ox, 
           oy, 
           s->sprite_delta[0][0], s->sprite_delta[0][1],
           s->sprite_delta[1][0], s->sprite_delta[1][1], 
           a+1, (1<<(2*a+1)) - s->no_rounding,
           s->h_edge_pos>>1, s->v_edge_pos>>1);
}


static void emulated_edge_mc(MpegEncContext *s, UINT8 *src, int linesize, int block_w, int block_h, 
                                    int src_x, int src_y, int w, int h){
    int x, y;
    int start_y, start_x, end_y, end_x;
    UINT8 *buf= s->edge_emu_buffer;
    
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

    start_y= MAX(0, -src_y);
    start_x= MAX(0, -src_x);
    end_y= MIN(block_h, h-src_y);
    end_x= MIN(block_w, w-src_x);

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
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int field_based, op_pixels_func (*pix_op)[4],
                               int motion_x, int motion_y, int h)
{
    UINT8 *ptr;
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
    linesize   = s->linesize << field_based;
    uvlinesize = s->uvlinesize << field_based;
    ptr = ref_picture[0] + (src_y * linesize) + (src_x) + src_offset;
    dest_y += dest_offset;

    if(s->flags&CODEC_FLAG_EMU_EDGE){
        if(src_x<0 || src_y<0 || src_x + (motion_x&1) + 16 > s->h_edge_pos
                              || src_y + (motion_y&1) + h  > v_edge_pos){
            emulated_edge_mc(s, ptr - src_offset, s->linesize, 17, 17+field_based, 
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
        emulated_edge_mc(s, ptr - (src_offset >> 1), s->uvlinesize, 9, 9+field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cb + (dest_offset >> 1), ptr, uvlinesize, h >> 1);

    ptr = ref_picture[2] + offset;
    if(emu){
        emulated_edge_mc(s, ptr - (src_offset >> 1), s->uvlinesize, 9, 9+field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cr + (dest_offset >> 1), ptr, uvlinesize, h >> 1);
}

static inline void qpel_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int field_based, op_pixels_func (*pix_op)[4],
                               qpel_mc_func (*qpix_op)[16],
                               int motion_x, int motion_y, int h)
{
    UINT8 *ptr;
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
            emulated_edge_mc(s, ptr - src_offset, s->linesize, 17, 17+field_based, 
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
        emulated_edge_mc(s, ptr - (src_offset >> 1), s->uvlinesize, 9, 9 + field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cb + (dest_offset >> 1), ptr,  uvlinesize, h >> 1);
    
    ptr = ref_picture[2] + offset;
    if(emu){
        emulated_edge_mc(s, ptr - (src_offset >> 1), s->uvlinesize, 9, 9 + field_based, 
                         src_x, src_y<<field_based, s->h_edge_pos>>1, s->v_edge_pos>>1);
        ptr= s->edge_emu_buffer + (src_offset >> 1);
    }
    pix_op[1][dxy](dest_cr + (dest_offset >> 1), ptr,  uvlinesize, h >> 1);
}


static inline void MPV_motion(MpegEncContext *s, 
                              UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                              int dir, UINT8 **ref_picture, 
                              op_pixels_func (*pix_op)[4], qpel_mc_func (*qpix_op)[16])
{
    int dxy, offset, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    UINT8 *ptr, *dest;
    int emu=0;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch(s->mv_type) {
    case MV_TYPE_16X16:
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
        }else{
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
                        emulated_edge_mc(s, ptr, s->linesize, 9, 9, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
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
                        emulated_edge_mc(s, ptr, s->linesize, 9, 9, src_x, src_y, s->h_edge_pos, s->v_edge_pos);
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
        for(i=0;i<4;i++) {
        }
        if (mx >= 0)
            mx = (h263_chroma_roundtab[mx & 0xf] + ((mx >> 3) & ~1));
        else {
            mx = -mx;
            mx = -(h263_chroma_roundtab[mx & 0xf] + ((mx >> 3) & ~1));
        }
        if (my >= 0)
            my = (h263_chroma_roundtab[my & 0xf] + ((my >> 3) & ~1));
        else {
            my = -my;
            my = -(h263_chroma_roundtab[my & 0xf] + ((my >> 3) & ~1));
        }
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
                    emulated_edge_mc(s, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
                    ptr= s->edge_emu_buffer;
                    emu=1;
                }
            }
        pix_op[1][dxy](dest_cb, ptr, s->uvlinesize, 8);

        ptr = ref_picture[2] + offset;
        if(emu){
            emulated_edge_mc(s, ptr, s->uvlinesize, 9, 9, src_x, src_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
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
            

        }
        break;
    }
}


/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, UINT8 *dest, int line_size)
{
    s->dct_unquantize(s, block, i, s->qscale);
    s->idct_put (dest, line_size, block);
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, UINT8 *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->idct_add (dest, line_size, block);
    }
}

static inline void add_dequant_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, UINT8 *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize(s, block, i, s->qscale);

        s->idct_add (dest, line_size, block);
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
    memset(s->ac_val[0][xy       ], 0, 32 * sizeof(INT16));
    memset(s->ac_val[0][xy + wrap], 0, 32 * sizeof(INT16));
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
    memset(s->ac_val[1][xy], 0, 16 * sizeof(INT16));
    memset(s->ac_val[2][xy], 0, 16 * sizeof(INT16));
    
    s->mbintra_table[s->mb_x + s->mb_y*s->mb_width]= 0;
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
    const int mb_xy = s->mb_y * s->mb_width + s->mb_x;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

#ifdef FF_POSTPROCESS
    /* Obsolete. Exists for compatibility with mplayer only. */
    quant_store[mb_y][mb_x]=s->qscale;
    //printf("[%02d][%02d] %d\n",mb_x,mb_y,s->qscale);
#else
    /* even more obsolete, exists for mplayer xp only */
    if(s->avctx->quant_store) s->avctx->quant_store[mb_y*s->avctx->qstride+mb_x] = s->qscale;
#endif
    s->qscale_table[mb_xy]= s->qscale;

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
        
        const int wrap = s->block_wrap[0];
        const int xy = s->block_index[0];
        const int mb_index= s->mb_x + s->mb_y*s->mb_width;
        if(s->mv_type == MV_TYPE_8X8){
            s->co_located_type_table[mb_index]= CO_LOCATED_TYPE_4MV;
        } else {
            int motion_x, motion_y;
            if (s->mb_intra) {
                motion_x = 0;
                motion_y = 0;
                if(s->co_located_type_table)
                    s->co_located_type_table[mb_index]= 0;
            } else if (s->mv_type == MV_TYPE_16X16) {
                motion_x = s->mv[0][0][0];
                motion_y = s->mv[0][0][1];
                if(s->co_located_type_table)
                    s->co_located_type_table[mb_index]= 0;
            } else /*if (s->mv_type == MV_TYPE_FIELD)*/ {
                int i;
                motion_x = s->mv[0][0][0] + s->mv[0][1][0];
                motion_y = s->mv[0][0][1] + s->mv[0][1][1];
                motion_x = (motion_x>>1) | (motion_x&1);
                for(i=0; i<2; i++){
                    s->field_mv_table[mb_index][i][0]= s->mv[0][i][0];
                    s->field_mv_table[mb_index][i][1]= s->mv[0][i][1];
                    s->field_select_table[mb_index][i]= s->field_select[0][i];
                }
                s->co_located_type_table[mb_index]= CO_LOCATED_TYPE_FIELDMV;
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
    }
    
    if (!(s->encoding && (s->intra_only || s->pict_type==B_TYPE))) {
        UINT8 *dest_y, *dest_cb, *dest_cr;
        int dct_linesize, dct_offset;
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];

        /* avoid copy if macroblock skipped in last frame too 
           dont touch it for B-frames as they need the skip info from the next p-frame */
        if (s->pict_type != B_TYPE) {
            UINT8 *mbskip_ptr = &s->mbskip_table[mb_xy];
            if (s->mb_skiped) {
                s->mb_skiped = 0;

                (*mbskip_ptr) ++; /* indicate that this time we skiped it */
                if(*mbskip_ptr >99) *mbskip_ptr= 99;

                /* if previous was skipped too, then nothing to do ! 
                   skip only during decoding as we might trash the buffers during encoding a bit */
                if (*mbskip_ptr >= s->ip_buffer_count  && !s->encoding) 
                    goto the_end;
            } else {
                *mbskip_ptr = 0; /* not skipped */
            }
        }

        if(s->pict_type==B_TYPE && s->avctx->draw_horiz_band){
            dest_y = s->current_picture [0] + mb_x * 16;
            dest_cb = s->current_picture[1] + mb_x * 8;
            dest_cr = s->current_picture[2] + mb_x * 8;
        }else{
            dest_y = s->current_picture [0] + (mb_y * 16* s->linesize  ) + mb_x * 16;
            dest_cb = s->current_picture[1] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;
            dest_cr = s->current_picture[2] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;
        }

        if (s->interlaced_dct) {
            dct_linesize = s->linesize * 2;
            dct_offset = s->linesize;
        } else {
            dct_linesize = s->linesize;
            dct_offset = s->linesize * 8;
        }

        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was allready done otherwise) */
            if((!s->encoding) || (s->mb_type[mb_xy]&(s->mb_type[mb_xy]-1))){
                if ((!s->no_rounding) || s->pict_type==B_TYPE){                
                    op_pix = put_pixels_tab;
                    op_qpix= put_qpel_pixels_tab;
                }else{
                    op_pix = put_no_rnd_pixels_tab;
                    op_qpix= put_no_rnd_qpel_pixels_tab;
                }

                if (s->mv_dir & MV_DIR_FORWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix, op_qpix);
                    op_pix = avg_pixels_tab;
                    op_qpix= avg_qpel_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix, op_qpix);
                }
            }

            /* skip dequant / idct if we are really late ;) */
            if(s->hurry_up>1) goto the_end;

            /* add dct residue */
            if(s->encoding || !(   s->mpeg2 || s->h263_msmpeg4 || s->codec_id==CODEC_ID_MPEG1VIDEO 
                                || (s->codec_id==CODEC_ID_MPEG4 && !s->mpeg_quant))){
                add_dequant_dct(s, block[0], 0, dest_y, dct_linesize);
                add_dequant_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                add_dequant_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                add_dequant_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    add_dequant_dct(s, block[4], 4, dest_cb, s->uvlinesize);
                    add_dequant_dct(s, block[5], 5, dest_cr, s->uvlinesize);
                }
            } else {
                add_dct(s, block[0], 0, dest_y, dct_linesize);
                add_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                add_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                add_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    add_dct(s, block[4], 4, dest_cb, s->uvlinesize);
                    add_dct(s, block[5], 5, dest_cr, s->uvlinesize);
                }
            }
        } else {
            /* dct only in intra block */
            if(s->encoding || !(s->mpeg2 || s->codec_id==CODEC_ID_MPEG1VIDEO)){
                put_dct(s, block[0], 0, dest_y, dct_linesize);
                put_dct(s, block[1], 1, dest_y + 8, dct_linesize);
                put_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
                put_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    put_dct(s, block[4], 4, dest_cb, s->uvlinesize);
                    put_dct(s, block[5], 5, dest_cr, s->uvlinesize);
                }
            }else{
                s->idct_put(dest_y                 , dct_linesize, block[0]);
                s->idct_put(dest_y              + 8, dct_linesize, block[1]);
                s->idct_put(dest_y + dct_offset    , dct_linesize, block[2]);
                s->idct_put(dest_y + dct_offset + 8, dct_linesize, block[3]);

                if(!(s->flags&CODEC_FLAG_GRAY)){
                    s->idct_put(dest_cb, s->uvlinesize, block[4]);
                    s->idct_put(dest_cr, s->uvlinesize, block[5]);
                }
            }
        }
    }
 the_end:
    emms_c(); //FIXME remove
}

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

static inline void requantize_coeffs(MpegEncContext *s, DCTELEM block[64], int oldq, int newq, int n)
{
    int i;

    if(s->mb_intra){
        i=1; //skip clipping of intra dc
         //FIXME requantize, note (mpeg1/h263/h263p-aic dont need it,...)
    }else
        i=0;
    
    for(;i<=s->block_last_index[n]; i++){
        const int j = s->intra_scantable.permutated[i];
        int level = block[j];
        
        block[j]= ROUNDED_DIV(level*oldq, newq);
    }

    for(i=s->block_last_index[n]; i>=0; i--){
        const int j = s->intra_scantable.permutated[i];
        if(block[j]) break;
    }
    s->block_last_index[n]= i;
}

static inline void auto_requantize_coeffs(MpegEncContext *s, DCTELEM block[6][64])
{
    int i,n, newq;
    const int maxlevel= s->max_qcoeff;
    const int minlevel= s->min_qcoeff;
    int largest=0, smallest=0;

    assert(s->adaptive_quant);
    
    for(n=0; n<6; n++){
        if(s->mb_intra){
            i=1; //skip clipping of intra dc
             //FIXME requantize, note (mpeg1/h263/h263p-aic dont need it,...)
        }else
            i=0;

        for(;i<=s->block_last_index[n]; i++){
            const int j = s->intra_scantable.permutated[i];
            int level = block[n][j];
            if(largest  < level) largest = level;
            if(smallest > level) smallest= level;
        }
    }
    
    for(newq=s->qscale+1; newq<32; newq++){
        if(   ROUNDED_DIV(smallest*s->qscale, newq) >= minlevel
           && ROUNDED_DIV(largest *s->qscale, newq) <= maxlevel) 
            break;
    }
        
    if(s->out_format==FMT_H263){
        /* h263 like formats cannot change qscale by more than 2 easiely */
        if(s->avctx->qmin + 2 < newq)
            newq= s->avctx->qmin + 2;
    }

    for(n=0; n<6; n++){
        requantize_coeffs(s, block[n], s->qscale, newq, n);
        clip_coeffs(s, block[n], s->block_last_index[n]);
    }
     
    s->dquant+= newq - s->qscale;
    s->qscale= newq;
}
#if 0
static int pix_vcmp16x8(UINT8 *s, int stride){ //FIXME move to dsputil & optimize
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

static int pix_diff_vcmp16x8(UINT8 *s1, UINT8*s2, int stride){ //FIXME move to dsputil & optimize
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

static int pix_vcmp16x8(UINT8 *s, int stride){ //FIXME move to dsputil & optimize
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

static int pix_diff_vcmp16x8(UINT8 *s1, UINT8*s2, int stride){ //FIXME move to dsputil & optimize
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

void ff_draw_horiz_band(MpegEncContext *s){
    if (    s->avctx->draw_horiz_band 
        && (s->num_available_buffers>=1 || (!s->has_b_frames)) ) {
        UINT8 *src_ptr[3];
        int y, h, offset;
        y = s->mb_y * 16;
        h = s->height - y;
        if (h > 16)
            h = 16;

        if(s->pict_type==B_TYPE)
            offset = 0;
        else
            offset = y * s->linesize;

        if(s->pict_type==B_TYPE || (!s->has_b_frames)){
            src_ptr[0] = s->current_picture[0] + offset;
            src_ptr[1] = s->current_picture[1] + (offset >> 2);
            src_ptr[2] = s->current_picture[2] + (offset >> 2);
        } else {
            src_ptr[0] = s->last_picture[0] + offset;
            src_ptr[1] = s->last_picture[1] + (offset >> 2);
            src_ptr[2] = s->last_picture[2] + (offset >> 2);
        }
        s->avctx->draw_horiz_band(s->avctx, src_ptr, s->linesize,
                               y, s->width, h);
    }
}

static void encode_mb(MpegEncContext *s, int motion_x, int motion_y)
{
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    int i;
    int skip_dct[6];
    int dct_offset   = s->linesize*8; //default for progressive frames
    
    for(i=0; i<6; i++) skip_dct[i]=0;
    
    if(s->adaptive_quant){
        s->dquant= s->qscale_table[mb_x + mb_y*s->mb_width] - s->qscale;

        if(s->out_format==FMT_H263){
            if     (s->dquant> 2) s->dquant= 2;
            else if(s->dquant<-2) s->dquant=-2;
        }
            
        if(s->codec_id==CODEC_ID_MPEG4){        
            if(!s->mb_intra){
                assert(s->dquant==0 || s->mv_type!=MV_TYPE_8X8);

                if(s->mv_dir&MV_DIRECT)
                    s->dquant=0;
            }
        }
        s->qscale+= s->dquant;
        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
    }

    if (s->mb_intra) {
        UINT8 *ptr;
        int wrap_y;
        int emu=0;

        wrap_y = s->linesize;
        ptr = s->new_picture[0] + (mb_y * 16 * wrap_y) + mb_x * 16;

        if(mb_x*16+16 > s->width || mb_y*16+16 > s->height){
            emulated_edge_mc(s, ptr, wrap_y, 16, 16, mb_x*16, mb_y*16, s->width, s->height);
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
        
        get_pixels(s->block[0], ptr                 , wrap_y);
        get_pixels(s->block[1], ptr              + 8, wrap_y);
        get_pixels(s->block[2], ptr + dct_offset    , wrap_y);
        get_pixels(s->block[3], ptr + dct_offset + 8, wrap_y);

        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
            int wrap_c = s->uvlinesize;
            ptr = s->new_picture[1] + (mb_y * 8 * wrap_c) + mb_x * 8;
            if(emu){
                emulated_edge_mc(s, ptr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr= s->edge_emu_buffer;
            }
            get_pixels(s->block[4], ptr, wrap_c);

            ptr = s->new_picture[2] + (mb_y * 8 * wrap_c) + mb_x * 8;
            if(emu){
                emulated_edge_mc(s, ptr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr= s->edge_emu_buffer;
            }
            get_pixels(s->block[5], ptr, wrap_c);
        }
    }else{
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        UINT8 *dest_y, *dest_cb, *dest_cr;
        UINT8 *ptr_y, *ptr_cb, *ptr_cr;
        int wrap_y, wrap_c;
        int emu=0;

        dest_y  = s->current_picture[0] + (mb_y * 16 * s->linesize       ) + mb_x * 16;
        dest_cb = s->current_picture[1] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
        dest_cr = s->current_picture[2] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
        wrap_y = s->linesize;
        wrap_c = s->uvlinesize;
        ptr_y  = s->new_picture[0] + (mb_y * 16 * wrap_y) + mb_x * 16;
        ptr_cb = s->new_picture[1] + (mb_y * 8 * wrap_c) + mb_x * 8;
        ptr_cr = s->new_picture[2] + (mb_y * 8 * wrap_c) + mb_x * 8;

        if ((!s->no_rounding) || s->pict_type==B_TYPE){
            op_pix = put_pixels_tab;
            op_qpix= put_qpel_pixels_tab;
        }else{
            op_pix = put_no_rnd_pixels_tab;
            op_qpix= put_no_rnd_qpel_pixels_tab;
        }

        if (s->mv_dir & MV_DIR_FORWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix, op_qpix);
            op_pix = avg_pixels_tab;
            op_qpix= avg_qpel_pixels_tab;
        }
        if (s->mv_dir & MV_DIR_BACKWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix, op_qpix);
        }

        if(mb_x*16+16 > s->width || mb_y*16+16 > s->height){
            emulated_edge_mc(s, ptr_y, wrap_y, 16, 16, mb_x*16, mb_y*16, s->width, s->height);
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
        
        diff_pixels(s->block[0], ptr_y                 , dest_y                 , wrap_y);
        diff_pixels(s->block[1], ptr_y              + 8, dest_y              + 8, wrap_y);
        diff_pixels(s->block[2], ptr_y + dct_offset    , dest_y + dct_offset    , wrap_y);
        diff_pixels(s->block[3], ptr_y + dct_offset + 8, dest_y + dct_offset + 8, wrap_y);
        
        if(s->flags&CODEC_FLAG_GRAY){
            skip_dct[4]= 1;
            skip_dct[5]= 1;
        }else{
            if(emu){
                emulated_edge_mc(s, ptr_cb, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr_cb= s->edge_emu_buffer;
            }
            diff_pixels(s->block[4], ptr_cb, dest_cb, wrap_c);
            if(emu){
                emulated_edge_mc(s, ptr_cr, wrap_c, 8, 8, mb_x*8, mb_y*8, s->width>>1, s->height>>1);
                ptr_cr= s->edge_emu_buffer;
            }
            diff_pixels(s->block[5], ptr_cr, dest_cr, wrap_c);
        }

        /* pre quantization */         
        if(s->mc_mb_var[s->mb_width*mb_y+ mb_x]<2*s->qscale*s->qscale){
            //FIXME optimize
            if(pix_abs8x8(ptr_y               , dest_y               , wrap_y) < 20*s->qscale) skip_dct[0]= 1;
            if(pix_abs8x8(ptr_y            + 8, dest_y            + 8, wrap_y) < 20*s->qscale) skip_dct[1]= 1;
            if(pix_abs8x8(ptr_y +dct_offset   , dest_y +dct_offset   , wrap_y) < 20*s->qscale) skip_dct[2]= 1;
            if(pix_abs8x8(ptr_y +dct_offset+ 8, dest_y +dct_offset+ 8, wrap_y) < 20*s->qscale) skip_dct[3]= 1;
            if(pix_abs8x8(ptr_cb              , dest_cb              , wrap_y) < 20*s->qscale) skip_dct[4]= 1;
            if(pix_abs8x8(ptr_cr              , dest_cr              , wrap_y) < 20*s->qscale) skip_dct[5]= 1;
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
                
                adap_parm = ((s->avg_mb_var << 1) + s->mb_var[s->mb_width*mb_y+mb_x] + 1.0) /
                            ((s->mb_var[s->mb_width*mb_y+mb_x] << 1) + s->avg_mb_var + 1.0);
            
                printf("\ntype=%c qscale=%2d adap=%0.2f dquant=%4.2f var=%4d avgvar=%4d", 
                        (s->mb_type[s->mb_width*mb_y+mb_x] > 0) ? 'I' : 'P', 
                        s->qscale, adap_parm, s->qscale*adap_parm,
                        s->mb_var[s->mb_width*mb_y+mb_x], s->avg_mb_var);
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
        s->block[5][0]= 128;
    }

    /* huffman encode */
    switch(s->out_format) {
    case FMT_MPEG1:
        mpeg1_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case FMT_H263:
        if (s->h263_msmpeg4)
            msmpeg4_encode_mb(s, s->block, motion_x, motion_y);
        else if(s->h263_pred)
            mpeg4_encode_mb(s, s->block, motion_x, motion_y);
        else
            h263_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case FMT_MJPEG:
        mjpeg_encode_mb(s, s->block);
        break;
    }
}

void ff_copy_bits(PutBitContext *pb, UINT8 *src, int length)
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
    d->mb_incr= s->mb_incr;
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
}

static inline void copy_context_after_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->mv, s->mv, 2*4*2*sizeof(int)); 
    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster then a loop?
    
    /* mpeg1 */
    d->mb_incr= s->mb_incr;
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

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y, pdif = 0;
    int i;
    int bits;
    MpegEncContext best_s, backup_s;
    UINT8 bit_buf[2][3000];
    UINT8 bit_buf2[2][3000];
    UINT8 bit_buf_tex[2][3000];
    PutBitContext pb[2], pb2[2], tex_pb[2];

    for(i=0; i<2; i++){
        init_put_bits(&pb    [i], bit_buf    [i], 3000, NULL, NULL);
        init_put_bits(&pb2   [i], bit_buf2   [i], 3000, NULL, NULL);
        init_put_bits(&tex_pb[i], bit_buf_tex[i], 3000, NULL, NULL);
    }

    s->picture_number = picture_number;

    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->mb_width*2 + 2;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_width + 2;
    
    /* Reset the average MB variance */
    s->mb_var_sum = 0;
    s->mc_mb_var_sum = 0;

    /* we need to initialize some time vars before we can encode b-frames */
    if (s->h263_pred && !s->h263_msmpeg4)
        ff_set_mpeg4_time(s, s->picture_number); 

    s->scene_change_score=0;
    
    s->qscale= (int)(s->frame_qscale + 0.5); //FIXME qscale / ... stuff for ME ratedistoration

    /* Estimate motion for every MB */
    if(s->pict_type != I_TYPE){
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
//                s->mb_type[mb_y*s->mb_width + mb_x]=MB_TYPE_INTER;
            }
        }
        emms_c();
    }else /* if(s->pict_type == I_TYPE) */{
        /* I-Frame */
        //FIXME do we need to zero them?
        memset(s->motion_val[0], 0, sizeof(INT16)*(s->mb_width*2 + 2)*(s->mb_height*2 + 2)*2);
        memset(s->p_mv_table   , 0, sizeof(INT16)*(s->mb_width+2)*(s->mb_height+2)*2);
        memset(s->mb_type      , MB_TYPE_INTRA, sizeof(UINT8)*s->mb_width*s->mb_height);
        
        if(!s->fixed_qscale){
            /* finding spatial complexity for I-frame rate control */
            for(mb_y=0; mb_y < s->mb_height; mb_y++) {
                for(mb_x=0; mb_x < s->mb_width; mb_x++) {
                    int xx = mb_x * 16;
                    int yy = mb_y * 16;
                    uint8_t *pix = s->new_picture[0] + (yy * s->linesize) + xx;
                    int varc;
                    int sum = pix_sum(pix, s->linesize);
    
                    sum= (sum+8)>>4;
                    varc = (pix_norm1(pix, s->linesize) - sum*sum + 500 + 128)>>8;

                    s->mb_var [s->mb_width * mb_y + mb_x] = varc;
                    s->mb_mean[s->mb_width * mb_y + mb_x] = (sum+7)>>4;
                    s->mb_var_sum    += varc;
                }
            }
        }
    }
    if(s->scene_change_score > 0 && s->pict_type == P_TYPE){
        s->pict_type= I_TYPE;
        memset(s->mb_type   , MB_TYPE_INTRA, sizeof(UINT8)*s->mb_width*s->mb_height);
        if(s->max_b_frames==0){
            s->input_pict_type= I_TYPE;
            s->input_picture_in_gop_number=0;
        }
//printf("Scene change detected, encoding as I Frame %d %d\n", s->mb_var_sum, s->mc_mb_var_sum);
    }
    
    if(s->pict_type==P_TYPE || s->pict_type==S_TYPE) 
        s->f_code= ff_get_best_fcode(s, s->p_mv_table, MB_TYPE_INTER);
        ff_fix_long_p_mvs(s);
    if(s->pict_type==B_TYPE){
        s->f_code= ff_get_best_fcode(s, s->b_forw_mv_table, MB_TYPE_FORWARD);
        s->b_code= ff_get_best_fcode(s, s->b_back_mv_table, MB_TYPE_BACKWARD);

        ff_fix_long_b_mvs(s, s->b_forw_mv_table, s->f_code, MB_TYPE_FORWARD);
        ff_fix_long_b_mvs(s, s->b_back_mv_table, s->b_code, MB_TYPE_BACKWARD);
        ff_fix_long_b_mvs(s, s->b_bidir_forw_mv_table, s->f_code, MB_TYPE_BIDIR);
        ff_fix_long_b_mvs(s, s->b_bidir_back_mv_table, s->b_code, MB_TYPE_BIDIR);
    }
    
    if (s->fixed_qscale) 
        s->frame_qscale = s->avctx->quality;
    else
        s->frame_qscale = ff_rate_estimate_qscale(s);

    if(s->adaptive_quant){
        switch(s->codec_id){
        case CODEC_ID_MPEG4:
            ff_clean_mpeg4_qscales(s);
            break;
        case CODEC_ID_H263:
        case CODEC_ID_H263P:
            ff_clean_h263_qscales(s);
            break;
        }

        s->qscale= s->qscale_table[0];
    }else
        s->qscale= (int)(s->frame_qscale + 0.5);
        
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = ff_mpeg1_default_intra_matrix[0];
        for(i=1;i<64;i++){
            int j= s->idct_permutation[i];

            s->intra_matrix[j] = CLAMP_TO_8BIT((ff_mpeg1_default_intra_matrix[i] * s->qscale) >> 3);
        }
        convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16, 
                       s->q_intra_matrix16_bias, s->intra_matrix, s->intra_quant_bias, 8, 8);
    }

    s->last_bits= get_bit_count(&s->pb);
    switch(s->out_format) {
    case FMT_MJPEG:
        mjpeg_picture_header(s);
        break;
    case FMT_H263:
        if (s->h263_msmpeg4) 
            msmpeg4_encode_picture_header(s, picture_number);
        else if (s->h263_pred)
            mpeg4_encode_picture_header(s, picture_number);
        else if (s->h263_rv10) 
            rv10_encode_picture_header(s, picture_number);
        else
            h263_encode_picture_header(s, picture_number);
        break;
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

    /* init last dc values */
    /* note: quant matrix value (8) is implied here */
    s->last_dc[0] = 128;
    s->last_dc[1] = 128;
    s->last_dc[2] = 128;
    s->mb_incr = 1;
    s->last_mv[0][0][0] = 0;
    s->last_mv[0][0][1] = 0;

    if (s->codec_id==CODEC_ID_H263 || s->codec_id==CODEC_ID_H263P)
        s->gob_index = ff_h263_get_gob_height(s);

    if(s->codec_id==CODEC_ID_MPEG4 && s->partitioned_frame)
        ff_mpeg4_init_partitions(s);

    s->resync_mb_x=0;
    s->resync_mb_y=0;
    s->first_slice_line = 1;
    s->ptr_lastgob = s->pb.buf;
    s->ptr_last_mb_line = s->pb.buf;
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
            const int mb_type= s->mb_type[mb_y * s->mb_width + mb_x];
            const int xy= (mb_y+1) * (s->mb_width+2) + mb_x + 1;
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
            if(s->rtp_mode){
                int current_packet_size, is_gob_start;
                
                current_packet_size= pbBufPtr(&s->pb) - s->ptr_lastgob;
                is_gob_start=0;
                
                if(s->codec_id==CODEC_ID_MPEG4){
                    if(current_packet_size + s->mb_line_avgsize/s->mb_width >= s->rtp_payload_size
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
                }else{
                    if(current_packet_size + s->mb_line_avgsize*s->gob_index >= s->rtp_payload_size
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
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mv_type = MV_TYPE_16X16; //FIXME
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_direct_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_direct_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_direct_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_direct_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_DIRECT, pb, pb2, tex_pb, 
                                 &dmin, &next_block, s->b_direct_mv_table[xy][0], s->b_direct_mv_table[xy][1]);
                }
                if(mb_type&MB_TYPE_INTRA){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 1;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, MB_TYPE_INTRA, pb, pb2, tex_pb, 
                                 &dmin, &next_block, 0, 0);
                    /* force cleaning of ac/dc pred stuff if needed ... */
                    if(s->h263_pred || s->h263_aic)
                        s->mbintra_table[mb_x + mb_y*s->mb_width]=1;
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
                s->mv_type=MV_TYPE_16X16;
                // only one MB-Type possible
                switch(mb_type){
                case MB_TYPE_INTRA:
                    s->mv_dir = MV_DIR_FORWARD;
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
                    s->mv[0][0][0] = s->b_direct_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_direct_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_direct_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_direct_back_mv_table[xy][1];
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
            }
            /* clean the MV table in IPS frames for direct mode in B frames */
            if(s->mb_intra /* && I,P,S_TYPE */){
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
            }

            MPV_decode_mb(s, s->block);
//printf("MB %d %d bits\n", s->mb_x+s->mb_y*s->mb_width, get_bit_count(&s->pb));
        }


        /* Obtain average mb_row size for RTP */
        if (s->rtp_mode) {
            if (mb_y==0)
                s->mb_line_avgsize = pbBufPtr(&s->pb) - s->ptr_last_mb_line;
            else {    
                s->mb_line_avgsize = (s->mb_line_avgsize + pbBufPtr(&s->pb) - s->ptr_last_mb_line) >> 1;
            }
            s->ptr_last_mb_line = pbBufPtr(&s->pb);
        }
    }
    emms_c();

    if(s->codec_id==CODEC_ID_MPEG4 && s->partitioned_frame)
        ff_mpeg4_merge_partitions(s);

    if (s->msmpeg4_version && s->msmpeg4_version<4 && s->pict_type == I_TYPE)
        msmpeg4_encode_ext_header(s);

    if(s->codec_id==CODEC_ID_MPEG4) 
        ff_mpeg4_stuffing(&s->pb);

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

static int dct_quantize_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;
    const UINT8 *scantable= s->intra_scantable.scantable;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;
    
    s->fdct (block);

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

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
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
    ff_block_permute(block, s->idct_permutation, scantable, last_non_zero);

    return last_non_zero;
}

static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const UINT16 *quant_matrix;

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
    const UINT16 *quant_matrix;

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
        nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];
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

AVCodec mpeg1video_encoder = {
    "mpeg1video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

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

AVCodec mjpeg_encoder = {
    "mjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEG,
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
};

AVCodec msmpeg4v1_encoder = {
    "msmpeg4v1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec msmpeg4v2_encoder = {
    "msmpeg4v2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V2,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec msmpeg4v3_encoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V3,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec wmv1_encoder = {
    "wmv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec wmv2_encoder = {
    "wmv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV2,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};
